#!/bin/bash

if [ ${#} -lt 1 ];
then
	echo "usage: $0 database" >&2
	exit 1
fi

DB=${1}
DELIMITER=';'

TABLES=("data_types" "allocations" "accesses" "locks" "locks_held" "structs_layout")

MYSQL="mysql -v ${DB}"
MYSQLIMPORT="mysqlimport --local --fields-terminated-by=${DELIMITER} --ignore-lines=1 -v ${DB}"

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
