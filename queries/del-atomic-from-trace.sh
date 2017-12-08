#!/bin/bash
#
# Delete accesses to atomic accessable datastructures from the trace.
#

set -e

if [ ${#} -lt 1 ];
then
	echo "usage: $0 database" >&2
	exit 1
fi
DB=${1}

MYSQL="mysql $DB"

$MYSQL <<EOT
DELETE accesses
FROM accesses AS ac
INNER JOIN allocations AS a ON a.id=ac.alloc_id
INNER JOIN data_types AS dt ON dt.id=a.type
LEFT JOIN structs_layout_flat sl
  ON a.type = sl.type_id
 AND ac.address - a.ptr = sl.helper_offset
LEFT JOIN member_names AS mn
  ON mn.id = sl.member_id
WHERE
	sl.type like "%atomic_t%" or sl.type like "%atomic64_t*";
EOT


