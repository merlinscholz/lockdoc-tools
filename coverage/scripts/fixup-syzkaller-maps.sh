#!/bin/bash

DELTA=5

if [ $# -lt 1 ]; then
	echo "usage: $(basename $0) dir-to-transform ..." >&2
	echo "" >&2
	echo "Adds $DELTA to all addresses in all files in all directories. " >&2
	echo "" >&2
	echo "Achtung: This tool will modify files in place without backup." >&2
	exit 1
fi

set -e

TMP=$(mktemp)
trap "rm -f $TMP" EXIT

find "$@" -type f | \
while read f; do
	echo "processing $f ..."
	(
	echo obase=16
	echo ibase=16
	tr a-z A-Z < $f | \
	while read line; do
		echo $line+5
	done
	) | bc | tr A-Z a-z > $TMP
	mv $TMP $f
done
