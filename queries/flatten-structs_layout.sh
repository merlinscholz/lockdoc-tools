#!/bin/bash
#
# Creates structs_layout_flat from current structs_layout contents.
# structs_layout_flat can be joined with eq_ref ("=") instead of a range-based
# join (BETWEEN); MySQL doesn't seem to handle the latter efficiently.
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
CREATE TABLE IF NOT EXISTS looong_sequence (
byte_offset INT CHECK (byte_offset >= 0) NOT NULL,
PRIMARY KEY (byte_offset)
) ;

DELETE FROM looong_sequence;
EOT

echo "INSERT INTO looong_sequence VALUES (generate_series(0,1048576));" | $PSQL

$PSQL <<EOT
DROP TABLE IF EXISTS structs_layout_flat;

CREATE TABLE IF NOT EXISTS structs_layout_flat (
data_type_id int check (data_type_id > 0) NOT NULL,
data_type_name varchar(255) NOT NULL,
member_name_id int NOT NULL,
byte_offset int check (byte_offset >= 0) NOT NULL,
size int check (size > 0) NOT NULL,
helper_offset int check (helper_offset >= 0) NOT NULL
) ;

CREATE INDEX fast_access_idx ON structs_layout_flat(data_type_id,helper_offset);

DELETE FROM structs_layout_flat;

INSERT INTO structs_layout_flat
SELECT sl.data_type_id, sl.data_type_name, sl.member_name_id, sl.byte_offset, sl.size, seq.byte_offset
FROM looong_sequence seq
JOIN structs_layout sl
  ON seq.byte_offset BETWEEN sl.byte_offset AND sl.byte_offset + sl.size - 1
;
EOT
