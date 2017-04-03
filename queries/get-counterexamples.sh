#!/bin/bash
# Extract all commandline expressions containing counterexample.sql.sh from INPUTFILE for a given data type.
# The input file should contain the output produced by './hypothesizer --bugsql'.
# Afterwards, all commandline expressions are executed, and its output is concatenated in one file. That file now contains
# all sql statements to find all counterexamples for a given data type.
# If a database is provided, the script automatically executes the queries.

if [ ${#} -lt 3 ]; then
	cat >&2 <<-EOT
	usage: $0 INPUTFILE DATATYPE OUTPUTFILE [DATABASE]

	INPUTFILE       Input file as generated by './hypothesizer --bugsql'
	DATATYPE        data type
	OUTPUTFILE      Output file
	DATABASE        the database where to execute the query. If present, the script will automatically run the query.
	EOT
	exit 1
fi

DIR=`dirname ${0}`
INPUTFILE=${1}; shift
DATATYPE=${1}; shift
OUTPUTFILE=${1}; shift
DATABASE=${1}; shift
COUNTEREXAMPLE_SH="counterexample.sql.sh"
RESULTS="counterexamples-results.txt"

echo "" > ${OUTPUTFILE}

grep "^\![[:space:]]*${COUNTEREXAMPLE_SH}" ${INPUTFILE} | sed -e "s/^\![ \t]*//" | grep "${COUNTEREXAMPLE_SH} ${DATATYPE}" | while read cmd;
do
	echo "Running: ${cmd}:"
	eval ${DIR}/$cmd >> ${OUTPUTFILE}
done;

if [ ! -z ${DATABASE} ];
then
	echo "Running query..."
	mysql -t ${DATABASE} < ${OUTPUTFILE} > ${RESULTS}
fi
