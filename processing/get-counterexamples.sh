#!/bin/bash
# Extract all commandline expressions containing counterexample.sql.sh from INPUTFILE for a given data type.
# The input file should contain the output produced by './hypothesizer --bugsql'.
# Afterwards, all commandline expressions are executed, and its output is concatenated in one file. That file now contains
# all sql statements to find all counterexamples for a given data type.
# If a database is provided, the script automatically executes the queries.

if [ ${#} -lt 3 ]; then
	cat >&2 <<-EOT
	usage: $0 INPUTFILE DATATYPE DATABASE VMLINUX

	INPUTFILE       Input file as generated by './hypothesizer --bugsql'
	DATATYPE        data type
	DATABASE        the database where to execute the query
	[USE_EMBOTHER]  1 if the results should simply show EMBOTHER() instead of EMB:XXX()
	EOT
	exit 1
fi

DIR=`dirname ${0}`
INPUTFILE=${1}; shift
DATATYPE=${1}; shift
DATABASE=${1}; shift
HOST=${1}; shift
USER=${1}; shift
USE_EMBOTHER_PARAM=${1}; shift
COUNTEREXAMPLE_SH="counterexample.sql.sh"
QUERY_FILE=$(mktemp /tmp/counter-examples-sql.XXXXXX)
DELIMITER=";"

echo -n "" > ${QUERY_FILE}

if [ ${DATATYPE} == "any" ];
then
	GREP_REGEX=""
else
	# Trailing whitespace is needed. Otherwise 'grep "cdev"' matches 'cdev_priv' as well.
	GREP_REGEX="${COUNTEREXAMPLE_SH} ${DATATYPE} "
fi

export USE_EMBOTHER=${USE_EMBOTHER_PARAM} 

HEADER_PRINTED=0
grep "^\![[:space:]]*${COUNTEREXAMPLE_SH}" ${INPUTFILE} | sed -e "s/^\![ \t]*//" | grep "${GREP_REGEX}" | while read cmd;
do
	echo "Running: ${cmd}:" >&2
	if [ ! -z ${DATABASE} ];
	then
		eval ${DIR}/../queries/$cmd > ${QUERY_FILE}
		if [ ${?} -ne 0 ];
		then
			echo "Cannot generate query" >&2
			continue
		fi
		RESULTS=$(psql -A -F ';' --pset footer --echo-errors -h ${HOST} -U ${USER} ${DATABASE} < ${QUERY_FILE})
		if [ ${?} -ne 0 ];
		then
			echo "Error running query from ${QUERY_FILE}" >&2
			exit 1
		fi
		if [ -n "${RESULTS}" ] && [ ${HEADER_PRINTED} -eq 0 ];
		then
			echo "${RESULTS}"
			HEADER_PRINTED=1
		elif [ -n "${RESULTS}" ] && [ ${HEADER_PRINTED} -eq 1 ];
		then
			echo "${RESULTS}" | tail -n +2
		fi
	fi
done;
rm "${QUERY_FILE}"
