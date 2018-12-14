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

COUNT=0
grep "^\![[:space:]]*${COUNTEREXAMPLE_SH}" ${INPUTFILE} | sed -e "s/^\![ \t]*//" | grep "${GREP_REGEX}" | while read cmd;
do
	echo "Running: ${cmd}:" >&2
	eval ${DIR}/../queries/$cmd > ${QUERY_FILE}
	if [ ${?} -ne 0 ];
	then
		echo "Cannot generate query" >&2
		continue
	fi
	if [ ! -z ${DATABASE} ];
	then
		echo "Running query..." >&2
		if [ ${COUNT} -gt 0 ];
		then 
			mysql ${DATABASE} < ${QUERY_FILE} | tr '\t' "${DELIMITER}" | sed '/data_type;member;accesstype;stacktrace;locks_held/d'
		else
			mysql ${DATABASE} < ${QUERY_FILE} | tr '\t' "${DELIMITER}"
		fi
		if [ ${?} -ne 0 ];
		then
			echo "Error running query from ${QUERY_FILE}" >&2
			exit 1
		fi
	fi
	let COUNT=COUNT+1
done;
rm "${QUERY_FILE}"
