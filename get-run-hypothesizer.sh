#!/bin/bash
# Extract txns from database, and runs the hypothesizer various times.
# All members having a percentage of NO_LOCK_THRESHOLD of accesses without locks are assumed to not require any locking. 
# (1) Run the hypothesizer to generate the normal humand-readable output
# (2) Run it to get a csv file with the winning hypotheses (cut-off threshold is 0.0)
# (3) Run it to get the parameter list for counterexample.sql.sh

TOOLS_PATH=`dirname ${0}`

function usage() {
        echo "usage: $0 <database> <variant>" >&2
        exit 1
}

if [ -z ${1} ];
then
        usage
fi
DB=$1
shift

if [ -z ${1} ] || { [ ${1} != "stack" ] && [ ${1} != "nostack" ]; };
then
        usage
fi
VARIANT=`echo ${1}| tr '[:upper:]' '[:lower:]'`
shift

HYPO_INPUT=all_txns_members_locks_db_${VARIANT}.csv
NO_LOCK_THRESHOLD=5.0

echo "Retrieving txns members locks (${VARIANT})..."
time bash -c "${TOOLS_PATH}/queries/create-txn-members-locks.sh nostack | mysql ${DB} > ${HYPO_INPUT}"
${TOOLS_PATH}/run-hypothesizer.sh ${HYPO_INPUT} ${VARIANT}
