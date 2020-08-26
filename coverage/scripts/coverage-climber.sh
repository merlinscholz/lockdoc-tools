#!/bin/bash

if [ $# -ne 2 -a $# -ne 3 ]; then
	echo "usage: $(basename $0) map-dir dropfile [ restrict.map ]" >&2
	echo "" >&2
	echo "map-dir contains the (sorted) syzkaller-generated BB map files." >&2
	echo "Maps of benchmarks that are completely unnecessary (are covered by other benchmarks) are written to dropfile." >&2
	echo "If provided, all map files will initially be intersected with all-fs.map, e.g. to find the most suitable benchmarks for the FS subsystem (using all-fs.map)." >&2
	exit 1
fi

set -e

TMPDIR=$(mktemp -d)
TMPFILE=$(mktemp)
trap "rm -rf $TMPDIR $TMPFILE" EXIT

MAPDIR=$1
DROPFILE=$2
ALLFS=$3

rm -f $DROPFILE

cp -R $MAPDIR $TMPDIR/a

# initially intersect all-fs.map with all map files
if [ -n "$ALLFS" ]; then
	echo "Intersecting $ALLFS with all map files ..." >&2
	find $TMPDIR/a -type f | \
	while read f; do
		$(dirname $0)/set-intersect "$ALLFS" $f > $TMPFILE
		mv $TMPFILE $f
	done
fi

echo "Climbing coverage gradient ..." >&2
TOTAL=0
while [ $(find $TMPDIR/a -type f | wc -l) -gt 0 ]; do
	TOP=$( find $TMPDIR/a -type f | xargs wc -l | sort -rn | head -n2 | tail -n1 | awk '{print $2}' )
	BBCOUNT=$(wc -l < $TOP)
	TOTAL=$(($TOTAL + $BBCOUNT))
	echo "${TOP#$TMPDIR/a/} $BBCOUNT $TOTAL" # remove $TMPDIR/a/ prefix

	mkdir $TMPDIR/b
	find $TMPDIR/a -type f | \
	while read f; do
		if [ $f = $TOP ]; then
			continue
		fi
		DEST=$TMPDIR/b/${f#$TMPDIR/a/} # remove $TMPDIR/a/ prefix
		mkdir -p $(dirname $DEST)
		echo "$(dirname $0)/set-minus $f $TOP > $DEST"
	done | parallel -P 200%
	# FIXME why doesn't this significantly speed things up?

	find $TMPDIR/b -type f | \
	while read f; do
		if [ $(wc -l < $f) -eq 0 ]; then
			echo ${f#$TMPDIR/b/} >> $DROPFILE
			rm $f
		fi
	done

	if [ $(find $TMPDIR/b -type f | wc -l) -eq 0 ]; then
		break
	fi

	rm -r $TMPDIR/a
	mv $TMPDIR/b $TMPDIR/a
done

echo "Done." >&2
