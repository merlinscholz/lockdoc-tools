#!/bin/bash

if [ $# -ne 5 -a $# -ne 7 ]; then
	echo "usage: $(basename $0) path/to/vmlinux syzkaller-dir output-png-dir output.csv ltp-all.map [ all.map all-fs.map ] " >&2
	echo "" >&2
	echo "syzkaller-dir contains the (sorted) syzkaller-generated BB map files." >&2
	echo "If all.map (list of all kernel BBs) and all-fs.map (subset of all.map that belongs to the targeted kernel subsystem) are not provided on the command line, they will be generated." >&2
	exit 1
fi

set -e

export PATH=$(dirname $0):$PATH

SFCTOOL=$(dirname $0)/../sfctool/sfctool
KERNEL=$1
SYZKALLER_DIR=$2
PNGDIR=$3
CSV=$4
LTP=$5

mkdir -p "$PNGDIR"

# Do we have to generate all.map / all-fs.map?
if [ $# -eq 7 ]; then
	ALLBBS=$6
	ALLFS=$7
else
	ALLBBS=all.map
	ALLFS=all-fs.map

	# generate all.map
	echo "generating $ALLBBS from $KERNEL ..."
	$(dirname $0)/kernel-get-all-bbs.sh $KERNEL $ALLBBS

	# generate all-fs.map
	echo "generating $ALLFS from $KERNEL and $ALLBBS ..."
	subsystem-bbs.sh $KERNEL $ALLBBS '/fs/|/mm/|fs\.h|mm\.h' > $ALLFS
fi
ALLBBS_COUNT=$(wc -l < $ALLBBS)
ALLFS_COUNT=$(wc -l < $ALLFS)
echo "total kernel BBs: $ALLBBS_COUNT  belonging to fs subsystem: $ALLFS_COUNT"

AGGREGATE=$(mktemp)
SFC_ORDERING=$(mktemp)
TMP=$(mktemp)
NEWBBS=$(mktemp)
trap "rm -f $AGGREGATE $SFC_ORDERING $TMP $NEWBBS" EXIT

# generate sfc-order.map
cp $ALLFS $SFC_ORDERING
set-minus $ALLBBS $ALLFS >> $SFC_ORDERING

rm -f "$CSV"
echo "hash time current_cov current_fscov aggregate_cov aggregate_fscov aggregate_fscov_notltp total_bbs total_fsbbs" >> $CSV

STARTTIME=UNSET
for f in "$SYZKALLER_DIR"/*; do
	echo processing $f ...

	BASE=$(basename $f)
	TIME=${BASE%%-*}
	HASH=${BASE##*-}

	if [ $STARTTIME = UNSET ]; then
		STARTTIME=$TIME
	fi
	TIME=$( echo -e "scale=6\n($TIME - $STARTTIME) / 10^9" | bc -l )

	# determine newly covered BBs
	set-minus $f $AGGREGATE > $NEWBBS

	# add to aggregate
	set-union $AGGREGATE $f > $TMP
	mv $TMP $AGGREGATE

	# determine FS subset of aggregate
	set-intersect $AGGREGATE $ALLFS > $TMP

	# determine FS subset of aggregate that's not covered by LTP already
	FSCOV_NOTLTP=$( set-minus $TMP $LTP | wc -l )

	false && $SFCTOOL -t spiral \
		--off-map $ALLBBS --color ffffff \
		--off-map $ALLFS --color dddddd \
		--off-map $AGGREGATE --color 9999ff \
		--off-map $TMP --color ff9999 \
		--on-map $ALLBBS --color 3333ff \
		--on-map $ALLFS --color ff3333 \
		--on-map $NEWBBS --color ff00ff \
		$SFC_ORDERING $f "$PNGDIR"/$BASE.png

	# statistics
	echo -n "$HASH $TIME " >> $CSV
	echo -n $(wc -l < $f)" " >> $CSV
	echo -n $(set-intersect $f $ALLFS | wc -l)" " >> $CSV
	echo -n $(wc -l < $AGGREGATE)" " >> $CSV
	echo -n $(wc -l < $TMP)" " >> $CSV
	echo $FSCOV_NOTLTP $ALLBBS_COUNT $ALLFS_COUNT >> $CSV
done
