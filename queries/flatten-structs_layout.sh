#!/bin/bash
#
# Creates structs_layout_flat from current structs_layout contents.
# structs_layout_flat can be joined with eq_ref ("=") instead of a range-based
# join (BETWEEN); MySQL doesn't seem to handle the latter efficiently.
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
CREATE TABLE IF NOT EXISTS looong_sequence (
offset INT UNSIGNED NOT NULL,
PRIMARY KEY (offset)
) ENGINE=MyISAM DEFAULT CHARSET=utf8;

DELETE FROM looong_sequence;
EOT

for i in $(seq 0 4096)
do
	echo "INSERT INTO looong_sequence VALUES ($i);"
done | $MYSQL

$MYSQL <<EOT
CREATE TABLE IF NOT EXISTS structs_layout_flat (
type_id int unsigned NOT NULL,
type varchar(255) NOT NULL,
member varchar(255) NOT NULL,
offset smallint unsigned NOT NULL,
size smallint unsigned NOT NULL,
helper_offset smallint unsigned NOT NULL,
KEY (type_id,helper_offset)
) ENGINE=MyISAM DEFAULT CHARSET=utf8;

DELETE FROM structs_layout_flat;

INSERT INTO structs_layout_flat
SELECT sl.type_id, sl.type, sl.member, sl.offset, sl.size, seq.offset
FROM looong_sequence seq
JOIN structs_layout sl
  ON seq.offset BETWEEN sl.offset AND sl.offset + sl.size - 1
;
EOT
