#!/bin/bash
# Does all required steps of post processing to derive the lock hypotheses: convert, import, flatten structs layout, extract txns from database, and runs the hypothesizer.
# If required, it will wait for the fail-client to terminate, and automatically start the post processing.
# 
TOOLS_PATH=`dirname ${0}`
CONFIGFILE="convert.conf"
STACK_USAGE=(0 1)
SUBCLASS_USAGE=(0 1)

function usage() {
        echo "usage: $0 <database> [ pid of fail-client ]" >&2
        exit 1
}

SKIP_IMPORT=${SKIP_IMPORT:-0}
SKIP_HYPO=${SKIP_HYPO:-0}

if [ -z ${1} ];
then
        usage
fi
DB=$1
shift

if [ ! -f ${CONFIGFILE} ];
then
	echo "${CONFIGFILE} does not exist!" >&2
	exit 1
fi
set -a
. ${CONFIGFILE}
set +a

if [ ! -z ${1} ];
then
	echo "Waiting for fail-client (pid ${1}) to terminate..."
	while kill -0 ${1} 2>/dev/null
	do
		sleep 30;
	done
fi

if [ ${SKIP_IMPORT} -eq 0 ];
then
	time ${TOOLS_PATH}/conv-import.sh ${DB} -1
	if [ ${?} -ne 0 ];
	then
		echo "Cannot convert and import trace!">&2
		exit 1
	fi
	echo "Flatten structs layout..."
	time ${TOOLS_PATH}/queries/flatten-structs_layout.sh ${DB}
	if [ ${?} -ne 0 ];
	then
		echo "Cannot flatten structs layout!">&2
		exit 1
	fi
	echo "Deleting accesses to atomic members..."
	time ${TOOLS_PATH}/queries/del-atomic-from-trace.sh ${DB}
	if [ ${?} -ne 0 ];
	then
		echo "Cannot delete atomic members!">&2
		exit 1
	fi
fi

PREFIX="all_txns_members_locks"

for USE_STACK in "${STACK_USAGE[@]}"
do
	for USE_SUBCLASSES in "${SUBCLASS_USAGE[@]}"
	do
		if [ ${USE_STACK} -eq 0 ];
		then
			VARIANT="nostack"
		else
			VARIANT="stack"
		fi

		if [ ${USE_SUBCLASSES} -eq 0 ];
		then
			VARIANT="${VARIANT}-nosubclasses"
		else
			VARIANT="${VARIANT}-subclasses"
		fi

		echo "Start processing '${VARIANT}'"
		if [ ${SKIP_HYPO} -eq 0 ];
		then
			time ${TOOLS_PATH}/get-run-hypothesizer.sh ${DB} ${USE_STACK} ${USE_SUBCLASSES} ${PREFIX}
			if [ ${?} -ne 0 ];
			then
				echo "Cannot run hypothesizer for ${VARIANT}!">&2
				exit 1
			fi
		fi
		echo "Retrieving counterexamples..."
		time ${TOOLS_PATH}/processing/get-process-cex.sh ${DB} any ${PREFIX}_hypo_bugs_${VARIANT}.txt ${PREFIX}_hypo_winner_${VARIANT}.csv ${VARIANT}
		if [ ${?} -ne 0 ];
		then
			echo "Cannot run get-process-cex.sh for ${VARIANT}!">&2 
			exit 1
		fi

		echo "Finished processing '${VARIANT}'"
		echo "-----------------------------------"
	done
done

