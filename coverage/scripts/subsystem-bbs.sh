#!/bin/bash
if [ $# -ne 4 ]; then
	echo "usage: $0 path/to/kernel/src path/to/vmlinux all-kernel-bbs.map pattern" >&2
	echo "pattern is the bash-regex pattern that is searched for, e.g. '/fs/|/mm/|fs\\.h|mm\\.h'" >&2
	exit 1
fi

LASTADDR=NONE

addr2line -a --inlines -e "$2" < "$3" | \
while read LINE; do
	if [[ $LINE == 0x* ]]; then
		LASTADDR=${LINE#0x} # strip leading "0x"
	else
		LINE=`echo "$LINE" | sed -e "s%$1%%g"`
		if [[ $LINE =~ $3 && $LASTADDR != NONE ]]; then
			echo $LASTADDR
			# make sure we output each address only once
			LASTADDR=NONE
		fi
	fi
done
