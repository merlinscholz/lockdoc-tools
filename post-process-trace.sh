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

OVERALL_EXEC_TIME=0.0
DURATION_FILE=`mktemp /tmp/output.XXXXX`

if [ ${SKIP_IMPORT} -eq 0 ];
then
	IMPORT_EXEC_TIME=0

	/usr/bin/time -f "%e" -o ${DURATION_FILE} ${TOOLS_PATH}/conv-import.sh ${DB} -1
	if [ ${?} -ne 0 ];
	then
		echo "Cannot convert and import trace!">&2
		exit 1
	fi
	EXEC_TIME=`cat ${DURATION_FILE}`
	OVERALL_EXEC_TIME=`echo ${EXEC_TIME}+${OVERALL_EXEC_TIME} | bc`
	IMPORT_EXEC_TIME=`echo ${EXEC_TIME}+${IMPORT_EXEC_TIME} | bc`

	echo "Flatten structs layout..."
	/usr/bin/time -f "%e" -o ${DURATION_FILE} ${TOOLS_PATH}/queries/flatten-structs_layout.sh ${DB}
	if [ ${?} -ne 0 ];
	then
		echo "Cannot flatten structs layout!">&2
		exit 1
	fi
	EXEC_TIME=`cat ${DURATION_FILE}`
	OVERALL_EXEC_TIME=`echo ${EXEC_TIME}+${OVERALL_EXEC_TIME} | bc`
	IMPORT_EXEC_TIME=`echo ${EXEC_TIME}+${IMPORT_EXEC_TIME} | bc`

	echo "Deleting accesses to atomic members..."
	/usr/bin/time -f "%e" -o ${DURATION_FILE} ${TOOLS_PATH}/queries/del-atomic-from-trace.sh ${DB}
	if [ ${?} -ne 0 ];
	then
		echo "Cannot delete atomic members!">&2
		exit 1
	fi
	EXEC_TIME=`cat ${DURATION_FILE}`
	OVERALL_EXEC_TIME=`echo ${EXEC_TIME}+${OVERALL_EXEC_TIME} | bc`
	IMPORT_EXEC_TIME=`echo ${EXEC_TIME}+${IMPORT_EXEC_TIME} | bc`

	echo "Import and atomic handling took ${IMPORT_EXEC_TIME} secs."
fi

PREFIX="all-txns-members-locks"

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
			/usr/bin/time -f "%e" -o ${DURATION_FILE} ${TOOLS_PATH}/get-run-hypothesizer.sh ${DB} ${USE_STACK} ${USE_SUBCLASSES} ${PREFIX}
			if [ ${?} -ne 0 ];
			then
				echo "Cannot run hypothesizer for ${VARIANT}!">&2
				exit 1
			fi
			HYPO_EXEC_TIME=`cat ${DURATION_FILE}`
			OVERALL_EXEC_TIME=`echo ${HYPO_EXEC_TIME}+${OVERALL_EXEC_TIME} | bc`
			echo "Running hypothesizer and generating the input took ${HYPO_EXEC_TIME} secs."
		fi


		if [ ${USE_SUBCLASSES} -eq 0 ];
		then
			DATA_TYPES=`echo "SELECT dt.name FROM data_types AS dt;" | mysql -N ${DB}`
		else
			DATA_TYPES=`echo "SELECT IF(sc.name IS NULL, dt.name, CONCAT(dt.name, ':', sc.name)) FROM data_types AS dt INNER JOIN subclasses AS sc ON dt.id = sc.data_type_id;" | mysql -N ${DB}`
		fi
		CEX_EXEC_TIME=0.0
		for data_type in ${DATA_TYPES}
		do
			echo "Retrieving counterexamples for '${data_type}'..."
			/usr/bin/time -f "%e" -o ${DURATION_FILE} ${TOOLS_PATH}/processing/get-process-cex.sh ${DB} ${data_type} ${PREFIX}-hypo-bugs-${VARIANT}.txt ${PREFIX}-hypo-winner-${VARIANT}.csv ${VARIANT}
			if [ ${?} -ne 0 ];
			then
				echo "Cannot run get-process-cex.sh for ${VARIANT}!">&2
				exit 1
			fi
			EXEC_TIME=`cat ${DURATION_FILE}`
			CEX_EXEC_TIME=`echo ${EXEC_TIME}+${CEX_EXEC_TIME} | bc`
		done

		echo "Processing of CEXs took ${CEX_EXEC_TIME} secs."
		VARIANT_EXEC_TIME=`echo ${HYPO_EXEC_TIME}+${CEX_EXEC_TIME} | bc`
		echo "Finished processing '${VARIANT}'. Took ${VARIANT_EXEC_TIME} secs."
		echo "-----------------------------------"
	done
done
echo "Overall exec time: ${OVERALL_EXEC_TIME} secs."
rm ${DURATION_FILE}
