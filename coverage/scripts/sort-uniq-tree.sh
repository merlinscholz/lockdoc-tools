#!/bin/bash

if [ $# -lt 1 ]; then
	echo "usage: $(basename $0) dir-to-transform ..." >&2
	echo "" >&2
	echo "Achtung: This tool will modify files in place without backup." >&2
	exit 1
fi

find "$@" -type f | parallel '
TMP=$(mktemp)
trap "rm -f $TMP" EXIT
sortuniq < {} > $TMP
mv $TMP {}
'
#sort -u {} > $TMP
