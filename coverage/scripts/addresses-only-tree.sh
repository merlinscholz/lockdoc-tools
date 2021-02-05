#!/bin/bash

if [ $# -lt 1 ]; then
	echo "usage: $(basename $0) dir-to-transform ..." >&2
	echo "" >&2
	echo "Achtung: This tool will modify files in place without backup." >&2
	exit 1
fi

set -e

TMP=$(mktemp)
trap "rm -f $TMP" EXIT

find "$@" -type f | \
while read f; do
	grep -P '^[0-9a-z]{16}$' < $f > $TMP || true
	if ! diff -q $TMP $f >/dev/null; then
		echo "fixed $f"
		mv $TMP $f
	fi
done
