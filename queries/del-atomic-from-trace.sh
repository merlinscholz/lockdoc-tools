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
DELETE ac
FROM accesses AS ac
INNER JOIN allocations AS a ON a.id=ac.alloc_id
INNER JOIN data_types AS dt ON dt.id=a.data_type_id
LEFT JOIN structs_layout_flat sl
  ON a.data_type_id = sl.data_type_id
 AND ac.address - a.base_address = sl.helper_offset
LEFT JOIN member_names AS mn
  ON mn.id = sl.member_name_id
WHERE
	sl.data_type_name like "%atomic\_t%" or sl.data_type_name like "%atomic64\_t*" or sl.data_type_name like "%atomic\_long\_t%"
EOT


