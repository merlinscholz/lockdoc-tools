#!/bin/bash

if [ ${#} -lt 1 ]; then
	echo "usage: $0 database" >&2
	exit 1
fi

DB=${1}
DELIMITER=';'

TABLES=("data_types" "allocations" "accesses" "locks" "locks_held" "structs_layout" "txns" "blacklist")

function mysqlimport_warnings() {
mysql -vvv --show-warnings --execute="LOAD DATA LOCAL INFILE '$2' INTO TABLE ${2%%.csv} FIELDS TERMINATED BY '$DELIMITER' IGNORE 1 LINES" $1
}

MYSQL="mysql -vvv ${DB}"
#MYSQLIMPORT="mysqlimport --local --fields-terminated-by=${DELIMITER} --ignore-lines=1 -v ${DB}"
MYSQLIMPORT="mysqlimport_warnings ${DB}"

for table in "${TABLES[@]}"
do
	if [ ! -f ${table}.csv ];
	then
		echo "${table}.csv does not exist."
	else
		echo "Truncating ${table} ..."
		echo "DELETE FROM ${table}" | ${MYSQL}
		echo "Importing ${table}.csv ..."
		${MYSQLIMPORT} ${table}.csv
	fi
done
