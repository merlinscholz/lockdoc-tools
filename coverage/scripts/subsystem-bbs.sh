#!/bin/bash
if [ $# -ne 3 ]; then
	echo "usage: $0 path/to/vmlinux all-kernel-bbs.map pattern" >&2
	echo "pattern is the substring that is searched for, e.g. '/fs/'" >&2
	exit 1
fi
	
addr2line -a -e "$1" < "$2" | \
while read ADDR; do
	read LINE
	if [[ $LINE == *$3* ]]; then
		echo ${ADDR#0x} # strip leading "0x"
	fi
done
