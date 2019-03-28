#!/bin/bash
#
# Delete accesses to atomic accessable datastructures from the trace.
#

set -e

if [ ${#} -lt 1 ];
then
	echo "usage: $0 database host user" >&2
	exit 1
fi
DB=${1};shift
HOST=${1};shift
USER=${1};shift

PSQL="psql --echo-errors -h $HOST -U $USER $DB"

$PSQL <<EOT
--ALTER TABLE accesses DISABLE TRIGGER ALL;
DELETE
FROM accesses AS ac
USING allocations AS a, data_types AS dt, structs_layout_flat AS sl, subclasses AS sc
WHERE
	a.id = ac.alloc_id
	AND sc.id = a.subclass_id
	AND dt.id = sc.data_type_id
	AND sc.data_type_id = sl.data_type_id
	AND ac.address - a.base_address = sl.helper_offset
	AND (sl.data_type_name LIKE '%atomic\_t%' OR sl.data_type_name LIKE '%atomic64\_t*' OR sl.data_type_name LIKE '%atomic\_long\_t%')
--ALTER TABLE accesses ENABLE TRIGGER ALL;
EOT



