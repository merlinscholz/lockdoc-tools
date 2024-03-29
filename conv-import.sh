#!/bin/bash
TOOLS_PATH=`dirname ${0}`
CONFIGFILE="convert.conf"
CONVERT_BINARY=${TOOLS_PATH}/convert/build/convert
DB_SCHEME=${TOOLS_PATH}/queries/db-scheme.sql
CONV_OUTPUT=conv-out.txt
PROCESS_CONTEXT=${PROCESS_CONTEXT:-0}
# The config file must contain two variable definitions: (1) DATA which describes the path to the input data, and (2) KERNEL the path to the kernel image

if [ ! -f ${CONFIGFILE} ];
then
	echo "${CONFIGFILE} does not exist!" >&2
	echo "The config file must define the following variables:
KERNEL		The vmlinux image used by the vm
DATA		The csv file generated by FAIL* which contains the events
DELIMITER	Delimiter used by FAIL*, e.g., ';' oder '#'
It may contain one of the following vars to override the default values:
CONVERT_BINARY	An alternative convert binary, e.g., an old version
DATA_TYPES	The list of data types which should be processed
FN_BLACK_LIST	Blacklisted functions
MEMBER_BLACK_LIST Blacklisted members" >&2
	exit 1
fi

if [ ! -z ${DATA} ] && [ ! -z ${KERNEL} ] && [ ! -z ${DELIMITER} ] && [ ! -z ${KERNEL_TREE} ] && [ ! -z ${GUEST_OS} ];
then
	echo "All variable are already set by the caller. Skipping '${CONFIGFILE}'..."
else
	echo "At least one variable is NOT set. Using '${CONFIGFILE}'."
	. ${CONFIGFILE}
fi

if [ -z ${DATA} ] || [ -z ${KERNEL} ] || [ -z ${DELIMITER} ] || [ -z ${KERNEL_TREE} ] || [ -z ${GUEST_OS} ];
then
	echo "Vars DATA, KERNEL, DELIMITER, KERNEL_TREE, or GUEST_OS are not set!" >&2
	exit 1
fi

if [ -z ${DATA_TYPES} ];
then
	DATA_TYPES=${TOOLS_PATH}/data/${GUEST_OS}/data_types.csv
fi
if [ -z ${FN_BLACK_LIST} ];
then
	FN_BLACK_LIST=${TOOLS_PATH}/data/${GUEST_OS}/function_blacklist.csv
fi
if [ -z ${MEMBER_BLACK_LIST} ];
then
	MEMBER_BLACK_LIST=${TOOLS_PATH}/data/${GUEST_OS}/member_blacklist.csv
fi


if [ ${PROCESS_CONTEXT} -gt 0 ];
then
	echo "Enabling context tracing..."
	CTX_PROCESSING="-c"
fi

if [ -z ${PSQL_USER} ] || [ -z ${PSQL_HOST} ];
then
	echo "Vars PSQL_USER or PSQL_HOST are not set!" >&2
	exit 1
fi

if [ ! -f ${DATA_TYPES} ] || [ ! -f ${FN_BLACK_LIST} ] || [ ! -f ${MEMBER_BLACK_LIST} ];
then
	echo "${DATA_TYPES}, or ${FN_BLACK_LIST}, or ${MEMBER_BLACK_LIST} does not exist!" >&2
	exit 1
fi

echo "Using convert binary: ${CONVERT_BINARY}"
echo "Using \"${DB_SCHEME}\", \"${DATA_TYPES}\", \"${FN_BLACK_LIST}\" and \"${MEMBER_BLACK_LIST}\""

if [ ! -f ${CONVERT_BINARY} ];
then
	echo "${CONVERT_BINARY} does not exist!" >&2
	exit 1
fi

if [ -z "$1" ];
then
	usage
fi
USE_DB=true
if [ "$1" = --nodb ]; then
	USE_DB=false
	DB=
else
	DB=$1
fi
shift

if [ -z "$1" ]; then
	HEAD_CMD="| head -n 1000000"
elif [ "$1" -eq -1 ]; then
	HEAD_CMD=""
	shift
else
	HEAD_CMD="| head -n $1"
	shift
fi

TABLES=("data_types" "allocations" "accesses" "locks" "locks_held" "structs_layout" "txns" "function_blacklist" "member_names" "member_blacklist" "stacktraces" "subclasses")
PSQL="psql --quiet --echo-errors -h ${PSQL_HOST} -U ${PSQL_USER} ${DB}"
PSQLIMPORT="psqlimport_warnings"

function psqlimport_warnings() {
	TABLE=${1%%.csv.pv}
	${PSQL} -c "\COPY ${TABLE} FROM '$1' WITH (FORMAT csv, header true, delimiter '$DELIMITER', NULL '\N');"
}

function import_table() {
	if [ ! -e ${1}.csv ];
	then
		echo "${1}.csv does not exist." >&2
	else
		echo "DELETE FROM ${1}" | ${PSQL}
		${PSQLIMPORT} ${1}.csv.pv
	fi
}

function usage() {
	echo "usage: $0 <database> [ input-linecount ]" >&2
	echo "   or: $0 --nodb     [ input-linecount ]" >&2
	exit 1
}

if [ $USE_DB = true ]; then
	# initialize DB
	echo "Dropping tables..."
	${PSQL} < ${TOOLS_PATH}/queries/drop-tables.sql
	if [ ${?} -ne 0 ];
	then
		echo "Cannot drop tables!" >&2
		exit 1
	fi
	echo "Initializing database..."
	${PSQL} < ${DB_SCHEME}
	if [ ${?} -ne 0 ];
	then
		echo "Cannot apply db scheme!">&2
		exit 1
	fi

	echo "Setting up fifos..."
	# setup named pipes and start importing in the background
	for table in "${TABLES[@]}"
	do
		rm -f ${table}.csv ${table}.csv.pv
		mkfifo ${table}.csv ${table}.csv.pv
		if [ $table = accesses ]; then BUFSIZE=100m; else BUFSIZE=10m; fi
		#pv --buffer-size $BUFSIZE -c -r -a -b -T -l -N ${table} < ${table}.csv > ${table}.csv.pv &
		cat < ${table}.csv > ${table}.csv.pv &
		import_table ${table} &
	done
fi

#VALGRIND='valgrind --leak-check=yes --show-reachable=yes'
#GDB='cgdb --args'

if echo $DATA | egrep -q '.bz2$'; then
	$VALGRIND $GDB ${CONVERT_BINARY} ${CTX_PROCESSING} -g ${KERNEL_TREE} -t ${DATA_TYPES} -k $KERNEL -b ${FN_BLACK_LIST} -m ${MEMBER_BLACK_LIST} -d "${DELIMITER}" <( eval pbzip2 -d < $DATA ${HEAD_CMD} ) > ${CONV_OUTPUT} 2>&1
elif echo $DATA | egrep -q '.gz$'; then
	$VALGRIND $GDB ${CONVERT_BINARY} ${CTX_PROCESSING} -g ${KERNEL_TREE} -t ${DATA_TYPES} -k $KERNEL -b ${FN_BLACK_LIST} -m ${MEMBER_BLACK_LIST} -d "${DELIMITER}" <( eval gzip -d < $DATA ${HEAD_CMD} ) > ${CONV_OUTPUT} 2>&1
elif echo $DATA | egrep -q '.csv$'; then
	$VALGRIND $GDB ${CONVERT_BINARY} ${CTX_PROCESSING} -g ${KERNEL_TREE} -t ${DATA_TYPES} -k $KERNEL -b ${FN_BLACK_LIST} -m ${MEMBER_BLACK_LIST} -d "${DELIMITER}" <( eval cat $DATA ${HEAD_CMD} ) > ${CONV_OUTPUT} 2>&1
else
	echo "no idea what to do with filename extension of $DATA" >&2
	exit 1
fi

if [ $USE_DB = true ]; then
	wait
	#reset

	for table in "${TABLES[@]}"
	do
		rm -f ${table}.csv ${table}.csv.pv
	done
fi
