#!/bin/bash
TOOLS_PATH=""
CONFIGFILE="convert.conf"

if [ ! -f ${CONFIGFILE} ];
then
	echo "${CONFIGFILE} does not exist!" >&2
	exit 1
fi
. ${CONFIGFILE}

if [ -z ${DATA} ] || [ -z ${KERNEL} ];
then
	echo "Vars DATA or KERNEL are not set!" >&2
	exit 1
fi


DELIMITER=';'
TABLES=("data_types" "allocations" "accesses" "locks" "locks_held" "structs_layout" "txns" "blacklist")

function mysqlimport_warnings() {
	mysql -vvv --show-warnings --execute="LOAD DATA LOCAL INFILE '$2' INTO TABLE ${2%%.csv.pv} FIELDS TERMINATED BY '$DELIMITER' IGNORE 1 LINES" $1
}
function import_table() {
	if [ ! -e ${1}.csv ];
	then
		echo "${1}.csv does not exist." >&2
	else
		echo "DELETE FROM ${1}" | ${MYSQL}
		${MYSQLIMPORT} ${1}.csv.pv
	fi	
}
function usage() {
	echo "usage: $0 <database> <path to tools repo> [ input-linecount ]" >&2
	exit 1
}


if [ -z "$1" ];
then
	usage
fi
DB=$1
shift

if [ -z "$1" ];
then
	usage
fi
TOOLS_PATH=${1}
shift

if [ -z "$1" ]; then
	HEAD_CMD="| head -n 1000000"
elif [ "$1" -eq -1 ]; then
	HEAD_CMD=""
else
	HEAD_CMD="| head -n $1"
fi

MYSQL="mysql -vvv ${DB}"
#MYSQLIMPORT="mysqlimport --local --fields-terminated-by=${DELIMITER} --ignore-lines=1 -v ${DB}"
MYSQLIMPORT="mysqlimport_warnings ${DB}"

# initialize DB
mysql $DB < ${TOOLS_PATH}/queries/drop-tables.sql
mysql $DB < ${TOOLS_PATH}/queries/db-scheme.sql

# setup named pipes and start importing in the background
for table in "${TABLES[@]}"
do
	rm -f ${table}.csv ${table}.csv.pv
	mkfifo ${table}.csv ${table}.csv.pv
	if [ $table = accesses ]; then BUFSIZE=100m; else BUFSIZE=10m; fi
	pv --buffer-size $BUFSIZE -c -r -a -b -T -l -N ${table} < ${table}.csv > ${table}.csv.pv &
	import_table ${table} &
done

#VALGRIND='valgrind --tool=callgrind'
#GDB='cgdb --args'

if echo $DATA | egrep -q '.bz2$'; then
	$VALGRIND $GDB ${TOOLS_PATH}/convert/build/convert -t ${TOOLS_PATH}/data/data_types.csv -k $KERNEL -b ${TOOLS_PATH}/data/blacklist.csv <( eval pbzip2 -d < $DATA ${HEAD_CMD})
elif echo $DATA | egrep -q '.gz$'; then
	$VALGRIND $GDB ${TOOLS_PATH}/convert/build/convert -t ${TOOLS_PATH}/data/data_types.csv -k $KERNEL -b ${TOOLS_PATH}/data/blacklist.csv <( eval gzip -d < $DATA ${HEAD_CMD})
else
	echo "no idea what to do with filename extension of $DATA" >&2
	exit 1
fi

wait
mysqloptimize $DB
