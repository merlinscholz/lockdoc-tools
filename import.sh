#!/bin/bash

if [ ${#} -lt 1 ]; then
	echo "usage: $0 database" >&2
	exit 1
fi

DB=${1}
DELIMITER=';'
TABLE_CMDLINE=${2}

TABLES=("data_types" "allocations" "accesses" "locks" "locks_held" "structs_layout" "txns" "blacklist")

function mysqlimport_warnings() {
mysql -vvv --show-warnings --execute="LOAD DATA LOCAL INFILE '$2' INTO TABLE ${2%%.csv} FIELDS TERMINATED BY '$DELIMITER' IGNORE 1 LINES" $1
}

function import_table() {
	if [ ! -f ${1}.csv ];
	then
		echo "${1}.csv does not exist."
	else
		echo "Truncating ${1} ..."
		echo "DELETE FROM ${1}" | ${MYSQL}
		echo "Importing ${1}.csv ..."
		${MYSQLIMPORT} ${1}.csv
	fi	
}

MYSQL="mysql -vvv ${DB}"
#MYSQLIMPORT="mysqlimport --local --fields-terminated-by=${DELIMITER} --ignore-lines=1 -v ${DB}"
MYSQLIMPORT="mysqlimport_warnings ${DB}"

if [ ! -z ${TABLE_CMDLINE} ];
then
	import_table ${TABLE_CMDLINE}
else
	for table in "${TABLES[@]}"
	do
			import_table ${table}
	done
fi
