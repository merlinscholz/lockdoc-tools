#!/bin/bash

if [ $# -ne 2 ]; then
	echo "usage: $0 png-dir out.mp4" >&2
	exit 1
fi

set -e

PNGDIR=$1
OUT=$2

# tmp file for collecting temporary symlinks
TMP=$(mktemp)
trap "rm -f $TMP" EXIT

echo "Creating symlinks ..."
i=0
for f in $PNGDIR/*.png; do
	LINK=$(dirname $f)/$(printf %06d $i).png
	rm -f $LINK
	ln -s $(basename $f) $LINK
	echo $LINK >> $TMP
	i=$(($i+1))
done

ffmpeg -framerate 25 -i $PNGDIR/%06d.png $OUT

echo "Cleaning up ..."
xargs rm -f < $TMP
echo "Done."
