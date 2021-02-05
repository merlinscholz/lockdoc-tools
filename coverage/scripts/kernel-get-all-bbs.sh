#!/bin/bash

if [ $# -ne 2 ]; then
	echo "usage: $0 path/to/vmlinux output.map" >&2
	exit 1
fi

objdump -d "$1" | grep -A1 'call.*<__sanitizer_cov_trace_pc>' | grep -v '^--$' | \
while read CALLQ_SANITIZER; do  # we're not interested in the callq, but in the *next* instruction's address
	read NEXT_LINE
	# cover the case with consecutive __sanitizer_cov_trace_pc calls
	while [ "${NEXT_LINE/*call*<__sanitizer_cov_trace_pc>*/match}" = match ]; do
		echo "$NEXT_LINE"
		read NEXT_LINE
	done
	echo "$NEXT_LINE"
done | \
awk -F : '{print $1}' | sort > "$2"
