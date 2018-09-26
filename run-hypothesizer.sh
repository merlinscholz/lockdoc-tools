#!/bin/bash
# Just runs the hypothesizer various times.
# All members having a percentage of NO_LOCK_THRESHOLD of accesses without locks are assumed to not require any locking. 
# (1) Run the hypothesizer to generate the normal humand-readable output
# (2) Run it to get a csv file with the winning hypotheses (cut-off threshold is 0.0)
# (3) Run it to get the parameter list for counterexample.sql.sh

TOOLS_PATH=`dirname ${0}`

function usage() {
        echo "usage: $0 <input> <variant> <prefix for the output fname>" >&2
        exit 1
}

if [ ${#} -lt 3 ];
then
        usage
fi
HYPO_INPUT=${1};shift

VARIANT=`echo ${1}| tr '[:upper:]' '[:lower:]'`;shift

PREFIX=${1};shift

echo "Running hypothesizer (${VARIANT})..."
${TOOLS_PATH}/hypothesizer/hypothesizer -r normal    -s member          ${HYPO_INPUT} > ${PREFIX}-hypo_${VARIANT}.txt        &
${TOOLS_PATH}/hypothesizer/hypothesizer -r csvwinner -s member -t 0.0   ${HYPO_INPUT} > ${PREFIX}-hypo-winner-${VARIANT}.csv &
${TOOLS_PATH}/hypothesizer/hypothesizer -r normal    -s member --bugsql ${HYPO_INPUT} > ${PREFIX}-hypo-bugs-${VARIANT}.txt   &
${TOOLS_PATH}/hypothesizer/hypothesizer -r csv       -s member -t 0.0   ${HYPO_INPUT} > ${PREFIX}-hypo-${VARIANT}.csv   &
wait
