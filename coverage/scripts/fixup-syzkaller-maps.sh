#!/bin/bash
INDIR=syzkaller-maps
OUTDIR=syzkaller-maps-fixpc

for f in $INDIR/*; do
	echo "processing $f ..."
	OUT=syzkaller-maps-fixpc/$(basename $f)
	(
	echo obase=16
	echo ibase=16
	tr a-z A-Z < $f | \
	while read line; do
		echo $line+5
	done
	) | bc | tr A-Z a-z > $OUT
done
