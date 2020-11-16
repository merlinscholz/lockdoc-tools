#!/bin/bash
# Extract txns from database, and runs the hypothesizer various times.
# (1) Run the hypothesizer to generate the normal humand-readable output
# (2) Run it to get a csv file with the winning hypotheses (cut-off threshold is 0.0)
# (3) Run it to get the parameter list for counterexample.sql.sh

TOOLS_PATH=`dirname ${0}`

function usage() {
        echo "usage: $0 <database> <use stack> <use subclasses> <prefix for the output fname> <acceptance threshold> <host> <user>" >&2
        exit 1
}

if [ ${#} -lt 4 ];
then
        usage
fi

SKIP_EXEC=${SKIP_EXEC:-0}
SKIP_QUERY=${SKIP_QUERY:-0}

DB=$1;shift

USE_STACK=${1};shift
if [ -z ${USE_STACK} ] || { [ ${USE_STACK} -ne 0 ] && [ ${USE_STACK} -ne 1 ]; };
then
        usage
fi
if [ ${USE_STACK} -eq 0 ];
then
	VARIANT="nostack"
else
	VARIANT="stack"
fi

USE_SUBCLASSES=${1};shift
if [ -z ${USE_SUBCLASSES} ] || { [ ${USE_SUBCLASSES} -ne 0 ] && [ ${USE_SUBCLASSES} -ne 1 ]; };
then
        usage
fi
if [ ${USE_SUBCLASSES} -eq 0 ];
then
	VARIANT="${VARIANT}-nosubclasses"
else
	VARIANT="${VARIANT}-subclasses"
fi

PREFIX=${1};shift
SELECTION_STRATEGY=${1};shift
ACCEPT_THRESHOLD=${1};shift
REDUCTION_FACTOR=${1};shift
HOST=${1};shift
USER=${1};shift

HYPO_INPUT=${PREFIX}-db-${VARIANT}.csv
DURATION_FILE=`mktemp /tmp/output.XXXXX`

if [ ${SKIP_QUERY} -eq 0 ];
then
	echo "Retrieving txns members locks (${VARIANT}). Storing results in '${HYPO_INPUT}'."
	/usr/bin/time -f "%e" -o ${DURATION_FILE} bash -c "${TOOLS_PATH}/queries/create-txn-members-locks.sh ${USE_STACK} any any ${USE_SUBCLASSES} | psql -A -F $'\t' --pset footer=off --echo-errors -h ${HOST} -U ${USER} ${DB} > ${HYPO_INPUT}"
	EXEC_TIME=`cat ${DURATION_FILE}`
	echo "Generating hypothesizer input took ${EXEC_TIME} secs."
fi

if [ ${SKIP_EXEC} -eq 0 ];
then
	/usr/bin/time -f "%e" -o ${DURATION_FILE} ${TOOLS_PATH}/run-hypothesizer.sh ${HYPO_INPUT} ${VARIANT} ${PREFIX} ${SELECTION_STRATEGY} ${ACCEPT_THRESHOLD} ${REDUCTION_FACTOR}
	EXEC_TIME=`cat ${DURATION_FILE}`
	echo "Hypothesizer took ${EXEC_TIME} secs."
fi
rm ${DURATION_FILE}
