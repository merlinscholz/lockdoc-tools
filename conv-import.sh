#!/bin/bash
TOOLS_PATH=`dirname ${0}`
CONFIGFILE="convert.conf"
DATA_TYPES=${TOOLS_PATH}/data/data_types.csv
FN_BLACK_LIST=${TOOLS_PATH}/data/function_blacklist.csv
MEMBER_BLACK_LIST=${TOOLS_PATH}/data/member_blacklist.csv
CONVERT=${TOOLS_PATH}/convert/build/convert
CONV_OUTPUT=conv-out.txt
# The config file must contain two variable definitions: (1) DATA which describes the path to the input data, and (2) KERNEL the path to the kernel image


if [ ! -f ${CONFIGFILE} ];
then
	echo "${CONFIGFILE} does not exist!" >&2
	exit 1
fi
. ${CONFIGFILE}

if [ -z ${DATA} ] || [ -z ${KERNEL} ] || [ -z ${DELIMITER} ];
then
	echo "Vars DATA, KERNEL, or DELIMITER are not set!" >&2
	exit 1
fi

if [ ! -f ${DATA_TYPES} ] || [ ! -f ${FN_BLACK_LIST} ] || [ ! -f ${MEMBER_BLACK_LIST} ];
then
	echo "${DATA_TYPES}, or ${FN_BLACK_LIST}, or ${MEMBER_BLACK_LIST} does not exist!" >&2
	exit 1
fi

if [ ! -f ${CONVERT} ];
then
	echo "${CONVERT} does not exist!" >&2
	exit 1
fi


TABLES=("data_types" "allocations" "accesses" "locks" "locks_held" "structs_layout" "txns" "function_blacklist" "member_names" "member_blacklist")

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
	echo "usage: $0 <database> [ input-linecount ]" >&2
	exit 1
}


if [ -z "$1" ];
then
	usage
fi
DB=$1
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
if [ ${?} -ne 0 ];
then
	echo "Cannot drop tables!" >&2
	exit 1
fi
mysql $DB < ${TOOLS_PATH}/queries/db-scheme.sql
if [ ${?} -ne 0 ];
then
	echo "Cannot apply db scheme!">&2
	exit 1
fi

# setup named pipes and start importing in the background
for table in "${TABLES[@]}"
do
	rm -f ${table}.csv ${table}.csv.pv
	mkfifo ${table}.csv ${table}.csv.pv
	if [ $table = accesses ]; then BUFSIZE=100m; else BUFSIZE=10m; fi
	pv --buffer-size $BUFSIZE -c -r -a -b -T -l -N ${table} < ${table}.csv > ${table}.csv.pv &
	import_table ${table} &
done

#VALGRIND='valgrind --leak-check=yes --show-reachable=yes'
#GDB='cgdb --args'

if echo $DATA | egrep -q '.bz2$'; then
	$VALGRIND $GDB ${CONVERT} -t ${DATA_TYPES} -k $KERNEL -b ${FN_BLACK_LIST} -m ${MEMBER_BLACK_LIST} -d "${DELIMITER}" <( eval pbzip2 -d < $DATA ${HEAD_CMD} ) > ${CONV_OUTPUT} 2>&1
elif echo $DATA | egrep -q '.gz$'; then
	$VALGRIND $GDB ${CONVERT} -t ${DATA_TYPES} -k $KERNEL -b ${FN_BLACK_LIST} -m ${MEMBER_BLACK_LIST} -d "${DELIMITER}" <( eval gzip -d < $DATA ${HEAD_CMD} ) > ${CONV_OUTPUT} 2>&1
else
	echo "no idea what to do with filename extension of $DATA" >&2
	exit 1
fi

wait
mysqloptimize $DB
reset

for table in "${TABLES[@]}"
do
	rm -f ${table}.csv ${table}.csv.pv
done
