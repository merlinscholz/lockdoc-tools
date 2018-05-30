#!/bin/bash
# Extract all commandline expressions containing counterexample.sql.sh from INPUTFILE for a given data type.
# The input file should contain the output produced by './hypothesizer --bugsql'.
# Afterwards, all commandline expressions are executed, and its output is concatenated in one file. That file now contains
# all sql statements to find all counterexamples for a given data type.
# If a database is provided, the script automatically executes the queries.

if [ ${#} -lt 3 ]; then
	cat >&2 <<-EOT
	usage: $0 INPUTFILE DATATYPE OUTPUTFILE DATABASE VMLINUX

	INPUTFILE       Input file as generated by './hypothesizer --bugsql'
	DATATYPE        data type
	OUTPUTFILE      Output file
	DATABASE        the database where to execute the query
	[USE_EMBOTHER]  1 if the results should simply show EMBOTHER() instead of EMB:XXX()
	EOT
	exit 1
fi

DIR=`dirname ${0}`
INPUTFILE=${1}; shift
DATATYPE=${1}; shift
OUTPUT=${1}; shift
DATABASE=${1}; shift
USE_EMBOTHER_PARAM=${1}; shift
COUNTEREXAMPLE_SH="counterexample.sql.sh"
QUERY_FILE=$(mktemp /tmp/counter-examples-sql.XXXXXX)
DELIMITER=";"

echo -n "" > ${QUERY_FILE}
echo -n "" > ${OUTPUT}

if [ ${DATATYPE} == "any" ];
then
	GREP_REGEX=""
else
	GREP_REGEX="${COUNTEREXAMPLE_SH} ${DATATYPE}"
fi

export USE_EMBOTHER=${USE_EMBOTHER_PARAM} 

COUNT=0
grep "^\![[:space:]]*${COUNTEREXAMPLE_SH}" ${INPUTFILE} | sed -e "s/^\![ \t]*//" | grep "${GREP_REGEX}" | while read cmd;
do
	echo "Running: ${cmd}:"
	eval ${DIR}/../queries/$cmd > ${QUERY_FILE}
	if [ ! -z ${DATABASE} ];
	then
		echo "Running query..."
		if [ ${COUNT} -gt 0 ];
		then 
			mysql ${DATABASE} < ${QUERY_FILE} | tr '\t' "${DELIMITER}" | sed '/data_type;member;accesstype;occurrences;stacktrace;locks_held/d' >> ${OUTPUT}
		else
			mysql ${DATABASE} < ${QUERY_FILE} | tr '\t' "${DELIMITER}" >> ${OUTPUT}
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
