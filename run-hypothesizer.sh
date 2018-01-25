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

HYPO_INPUT_NOSTACK=all_txns_members_locks_db_nostack.csv 
HYPO_INPUT_STACK=all_txns_members_locks_db_stack.csv 
NO_LOCK_THRESHOLD=5.0

echo "Retrieving txns members locks (nostack)..."
time bash -c "${TOOLS_PATH}/queries/create-txn-members-locks.sh nostack | mysql ${DB} > ${HYPO_INPUT_NOSTACK}"
echo "Retrieving txns members locks (stack)..."
time bash -c "${TOOLS_PATH}/queries/create-txn-members-locks.sh stack | mysql ${DB} > ${HYPO_INPUT_STACK}"
echo "Running hypothesizer..."
${TOOLS_PATH}/hypothesizer/hypothesizer -n ${NO_LOCK_THRESHOLD} -r normal    -s member          ${HYPO_INPUT_NOSTACK} > all_txns_members_locks_hypo_nostack.txt        &
${TOOLS_PATH}/hypothesizer/hypothesizer -n ${NO_LOCK_THRESHOLD} -r csvwinner -s member -t 0.0   ${HYPO_INPUT_NOSTACK} > all_txns_members_locks_hypo_winner_nostack.csv &
${TOOLS_PATH}/hypothesizer/hypothesizer -n ${NO_LOCK_THRESHOLD} -r normal    -s member --bugsql ${HYPO_INPUT_NOSTACK} > all_txns_members_locks_hypo_bugs_nostack.txt   &
wait
${TOOLS_PATH}/hypothesizer/hypothesizer -n ${NO_LOCK_THRESHOLD} -r normal    -s member          ${HYPO_INPUT_STACK} > all_txns_members_locks_hypo_stack.txt        &
${TOOLS_PATH}/hypothesizer/hypothesizer -n ${NO_LOCK_THRESHOLD} -r csvwinner -s member -t 0.0   ${HYPO_INPUT_STACK} > all_txns_members_locks_hypo_winner_stack.csv &
${TOOLS_PATH}/hypothesizer/hypothesizer -n ${NO_LOCK_THRESHOLD} -r normal    -s member --bugsql ${HYPO_INPUT_STACK} > all_txns_members_locks_hypo_bugs_stack.txt   &
wait
