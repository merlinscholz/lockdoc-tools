#!/bin/bash

if [ ${#} -lt 1 ];
then
	exit 1
fi

DB=${1}
DB_USER='al'
DB_PASSWD='howaih1S'
DB_SERVER='129.217.43.116'
DELIMITER=';'

TABLES=("data_types" "allocations" "accesses" "locks" "locks_held" "structs_layout")

MYSQL="mysql --host=${DB_SERVER} --user=${DB_USER} --password=${DB_PASSWD} -v ${DB}"
MYSQLIMPORT="mysqlimport --local --fields-terminated-by=${DELIMITER} --ignore-lines=1 --host=${DB_SERVER} --user=${DB_USER} --password=${DB_PASSWD} -v ${DB}"

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
