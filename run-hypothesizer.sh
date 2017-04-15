#!/bin/bash
# Extract txns from database, and runs the hypothesizer various times.
# All members having a percentage of NO_LOCK_THRESHOLD of accesses without locks are assumed to not require any locking. 
# (1) Run the hypothesizer to generate the normal humand-readable output
# (2) Run it to get a csv file with the winning hypotheses (cut-off threshold is 0.0)
# (3) Run it to get the parameter list for counterexample.sql.sh

TOOLS_PATH=`dirname ${0}`

function usage() {
        echo "usage: $0 <database>" >&2
        exit 1
}

if [ -z ${1} ];
then
        usage
fi
DB=$1
shift

HYPO_INPUT=all_txns_members_locks_db.csv 
NO_LOCK_THRESHOLD=5.0

echo "Retrieving txns members locks..."
time mysql ${DB} < ${TOOLS_PATH}/queries/txns_members_locks.sql > ${HYPO_INPUT}
echo "Running hypothesizer..."
${TOOLS_PATH}/hypothesizer/hypothesizer -n ${NO_LOCK_THRESHOLD} -r normal    -s member          ${HYPO_INPUT} > all_txns_members_locks_hypo.txt        &
${TOOLS_PATH}/hypothesizer/hypothesizer -n ${NO_LOCK_THRESHOLD} -r csvwinner -s member -t 0.0   ${HYPO_INPUT} > all_txns_members_locks_hypo_winner.csv &
${TOOLS_PATH}/hypothesizer/hypothesizer -n ${NO_LOCK_THRESHOLD} -r normal    -s member --bugsql ${HYPO_INPUT} > all_txns_members_locks_hypo_bugs.txt   &
wait
