#!/bin/bash
# Does all required steps of post processing to derive the lock hypotheses: convert, import, flatten structs layout, extract txns from database, and runs the hypothesizer.
# If required, it will wait for the fail-client to terminate, and automatically start the post processing.
# 
TOOLS_PATH=`dirname ${0}`

if [ -z ${1} ];
then
        usage
fi
DB=$1
shift

if [ ! -z ${1} ];
then
	echo "Waiting for fail-client (pid ${1}) to terminate..."
	while kill -0 ${1} 2>/dev/null
	do
		sleep 30;
	done
fi

function usage() {
        echo "usage: $0 <database> [ pid of fail-client ]" >&2
        exit 1
}

${TOOLS_PATH}/conv-import.sh ${DB} -1
echo "Flatten structs layout..."
${TOOLS_PATH}/queries/flatten-structs_layout.sh ${DB}
echo "Retrieving txns members locks..."
time mysql ${DB} < ${TOOLS_PATH}/queries/txns_members_locks.sql > all_txns_members_locks_db.csv 
echo "Running hypothesizer..."
${TOOLS_PATH}/hypothesizer/hypothesizer -s member all_txns_members_locks_db.csv > all_txns_members_locks_hypo.txt
${TOOLS_PATH}/hypothesizer/hypothesizer -r csvwinner -t 0.0 -s member all_txns_members_locks_db.csv > all_txns_members_locks_hypo_winner.csv
