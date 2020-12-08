#!/bin/bash
# Does all required steps of post processing to derive the lock hypotheses: convert, import, flatten structs layout, extract txns from database, and runs the hypothesizer.
# If required, it will wait for the fail-client to terminate, and automatically start the post processing.
# 
TOOLS_PATH=`dirname ${0}`
CONFIGFILE="convert.conf"
STACK_USAGE=(0 1)
SUBCLASS_USAGE=(0 1)
DURATION_CSV="post-processing-durations.csv"
STATS_CSV="post-processing-stats.csv"

function usage() {
        echo "usage: $0 <database> [ pid of fail-client ]" >&2
        exit 1
}

SKIP_IMPORT=${SKIP_IMPORT:-0}
SKIP_HYPO_QUERY=${SKIP_HYPO_QUERY:-0}
SKIP_HYPO_EXEC=${SKIP_HYPO_EXEC:-0}
SKIP_CEX_QUERIES=${SKIP_CEX_QUERIES:-0}

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

if [ -z ${PSQL_USER} ] || [ -z ${PSQL_HOST} ];
then
	echo "Vars PSQL_USER or PSQL_HOST are not set!" >&2
	exit 1
fi

if [ -z ${ACCEPT_THRESHOLD} ] || [ -z ${SELECTION_STRATEGY} ] || [ -z ${REDUCTION_FACTOR} ];
then
	echo "Vars ACCEPT_THRESHOLD, SELECTION_STRATEGY, or REDUCTION_FACTOR are not set!" >&2
	exit 1
fi

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
PSQL="psql --tuples-only --quiet --echo-errors -h ${PSQL_HOST} -U ${PSQL_USER} ${DB}"
echo "step,section,duration" > ${DURATION_CSV}
echo "topic,param,value" > ${STATS_CSV}

if [ ${SKIP_IMPORT} -eq 0 ];
then
	IMPORT_EXEC_TIME=0

	/usr/bin/time -f "%e" -o ${DURATION_FILE} ${TOOLS_PATH}/conv-import.sh ${DB} -1 #2000000
	if [ ${?} -ne 0 ];
	then
		echo "Cannot convert and import trace!">&2
		exit 1
	fi
	EXEC_TIME=`cat ${DURATION_FILE}`
	echo "import,conv-import,${EXEC_TIME}" >> ${DURATION_CSV}
	OVERALL_EXEC_TIME=`echo ${EXEC_TIME}+${OVERALL_EXEC_TIME} | bc`
	IMPORT_EXEC_TIME=`echo ${EXEC_TIME}+${IMPORT_EXEC_TIME} | bc`

	echo "Flatten structs layout..."
	/usr/bin/time -f "%e" -o ${DURATION_FILE} ${TOOLS_PATH}/queries/flatten-structs_layout.sh ${DB} ${PSQL_HOST} ${PSQL_USER}
	if [ ${?} -ne 0 ];
	then
		echo "Cannot flatten structs layout!">&2
		exit 1
	fi
	EXEC_TIME=`cat ${DURATION_FILE}`
	echo "import,flattenstructs,${EXEC_TIME}" >> ${DURATION_CSV}
	OVERALL_EXEC_TIME=`echo ${EXEC_TIME}+${OVERALL_EXEC_TIME} | bc`
	IMPORT_EXEC_TIME=`echo ${EXEC_TIME}+${IMPORT_EXEC_TIME} | bc`

	echo "Deleting accesses to atomic members..."
	/usr/bin/time -f "%e" -o ${DURATION_FILE} ${TOOLS_PATH}/queries/del-atomic-from-trace.sh ${DB} ${PSQL_HOST} ${PSQL_USER}
	if [ ${?} -ne 0 ];
	then
		echo "Cannot delete atomic members!">&2
		exit 1
	fi
	EXEC_TIME=`cat ${DURATION_FILE}`
	echo "import,delatomic,${EXEC_TIME}" >> ${DURATION_CSV}
	OVERALL_EXEC_TIME=`echo ${EXEC_TIME}+${OVERALL_EXEC_TIME} | bc`
	IMPORT_EXEC_TIME=`echo ${EXEC_TIME}+${IMPORT_EXEC_TIME} | bc`

	echo -n "Checking for broken accesses..."
	RET=`/usr/bin/time -f "%e" -o ${DURATION_FILE} ${PSQL} < ${TOOLS_PATH}/queries/check_broken_accesses.sql`
	if [ ${?} -ne 0 ];
	then
		echo "Cannot create table accesses_flat!" >&2
		exit 1
	fi
	EXEC_TIME=`cat ${DURATION_FILE}`
	echo "import,checkbroken,${EXEC_TIME}" >> ${DURATION_CSV}
	echo " took ${EXEC_TIME} sec."
	OVERALL_EXEC_TIME=`echo ${EXEC_TIME}+${OVERALL_EXEC_TIME} | bc`
	IMPORT_EXEC_TIME=`echo ${EXEC_TIME}+${IMPORT_EXEC_TIME} | bc`
	if [ ! -z "${RET}" ];
	then
		echo "Some accesses cannot be mapped to a struct member! Aborting." >&2
		exit 1
	fi

	echo -n "Creating table accesses_flat..."
	/usr/bin/time -f "%e" -o ${DURATION_FILE} ${PSQL} < ${TOOLS_PATH}/queries/accesses_flat_table.sql
	if [ ${?} -ne 0 ];
	then
		echo "Cannot create table accesses_flat!" >&2
		exit 1
	fi
	EXEC_TIME=`cat ${DURATION_FILE}`
	echo "import,accessesflat,${EXEC_TIME}" >> ${DURATION_CSV}
	echo " took ${EXEC_TIME} sec."
	OVERALL_EXEC_TIME=`echo ${EXEC_TIME}+${OVERALL_EXEC_TIME} | bc`
	IMPORT_EXEC_TIME=`echo ${EXEC_TIME}+${IMPORT_EXEC_TIME} | bc`

	echo -n "Creating table locks_embedded_flat..."
	/usr/bin/time -f "%e" -o ${DURATION_FILE} ${PSQL} < ${TOOLS_PATH}/queries/locks_embedded_flat_table.sql
	if [ ${?} -ne 0 ];
	then
		echo "Cannot create table locks_flat!" >&2
		exit 1
	fi
	EXEC_TIME=`cat ${DURATION_FILE}`
	echo "import,locksflat,${EXEC_TIME}" >> ${DURATION_CSV}
	echo " took ${EXEC_TIME} sec."
	OVERALL_EXEC_TIME=`echo ${EXEC_TIME}+${OVERALL_EXEC_TIME} | bc`
	IMPORT_EXEC_TIME=`echo ${EXEC_TIME}+${IMPORT_EXEC_TIME} | bc`

	echo "Import and atomic handling took ${IMPORT_EXEC_TIME} secs."
	echo "import,total,${IMPORT_EXEC_TIME}" >> ${DURATION_CSV}
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

		HYPO_EXEC_TIME=0.0
		echo "Start processing '${VARIANT}'"
		if [ ${SKIP_HYPO_QUERY} -eq 0 ] || [ ${SKIP_HYPO_EXEC} -eq 0 ];
		then
			SKIP_QUERY=${SKIP_HYPO_QUERY} SKIP_EXEC=${SKIP_HYPO_EXEC} /usr/bin/time -f "%e" -o ${DURATION_FILE} ${TOOLS_PATH}/get-run-hypothesizer.sh ${DB} ${USE_STACK} ${USE_SUBCLASSES} ${PREFIX} ${SELECTION_STRATEGY} ${ACCEPT_THRESHOLD} ${REDUCTION_FACTOR} ${PSQL_HOST} ${PSQL_USER}
			if [ ${?} -ne 0 ];
			then
				echo "Cannot run hypothesizer for ${VARIANT}!">&2
				exit 1
			fi
			HYPO_EXEC_TIME=`cat ${DURATION_FILE}`
			echo "Running hypothesizer and generating the input took ${HYPO_EXEC_TIME} secs."
		fi

		if [ ${USE_SUBCLASSES} -eq 0 ];
		then
			DATA_TYPES=`echo "SELECT dt.name FROM data_types AS dt;" | psql --tuples-only --no-align -F $'\t' --pset footer --echo-errors -h ${PSQL_HOST} -U ${PSQL_USER} ${DB}`
		else
			DATA_TYPES=`echo "SELECT (CASE WHEN sc.name IS NULL THEN dt.name ELSE CONCAT(dt.name, ':', sc.name) END) FROM data_types AS dt INNER JOIN subclasses AS sc ON dt.id = sc.data_type_id;" | psql --tuples-only --no-align -F $'\t' --pset footer --echo-errors -h ${PSQL_HOST} -U ${PSQL_USER} ${DB}`
		fi
		CEX_EXEC_TIME=0.0
		for data_type in ${DATA_TYPES}
		do
			echo "Retrieving counterexamples for '${data_type}'..."
			SKIP_QUERIES=${SKIP_CEX_QUERIES} /usr/bin/time -f "%e" -o ${DURATION_FILE} ${TOOLS_PATH}/processing/get-process-cex.sh ${DB} ${data_type} ${PREFIX}-hypo-bugs-${VARIANT}.txt ${PREFIX}-hypo-winner-${VARIANT}.csv ${VARIANT} ${PSQL_HOST} ${PSQL_USER}
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
		OVERALL_EXEC_TIME=`echo ${VARIANT_EXEC_TIME}+${OVERALL_EXEC_TIME} | bc`
		echo "Finished processing '${VARIANT}'. Took ${VARIANT_EXEC_TIME} secs."
		echo "-----------------------------------"
		echo "${VARIANT},hypothesizer,${HYPO_EXEC_TIME}" >> ${DURATION_CSV}
		echo "${VARIANT},cex,${CEX_EXEC_TIME}" >> ${DURATION_CSV}
		echo "${VARIANT},total,${VARIANT_EXEC_TIME}" >> ${DURATION_CSV}
	done
done
echo "Overall exec time: ${OVERALL_EXEC_TIME} secs."
echo "total,total,${VARIANT_EXEC_TIME}" >> ${DURATION_CSV}
echo "Gathering stats about ${DATA}.."
if echo $DATA | egrep -q '.gz$';
then
	zcat ${DATA} | tail --lines=+2 | cut -d '#' -f2 | awk '{c[$1]++}END{for (x in c) print "events,"x","c[x]}' >> ${STATS_CSV}
	echo -n "events,total," >> ${STATS_CSV}
	zcat ${DATA} | tail --lines=+2 | wc -l >> ${STATS_CSV}

	echo -n "locks,static," >> ${STATS_CSV}
	echo "SELECT COUNT(*) FROM locks WHERE embedded_in IS NULL;" | psql --tuples-only --no-align --quiet --echo-errors -h ${PSQL_HOST} -U ${PSQL_USER} ${DB} >> ${STATS_CSV}

	echo -n "locks,dynamic," >> ${STATS_CSV}
	echo "SELECT COUNT(*) FROM locks WHERE embedded_in IS NOT NULL;" | psql --tuples-only --no-align --quiet --echo-errors -h ${PSQL_HOST} -U ${PSQL_USER} ${DB} >> ${STATS_CSV}

	echo -n "locks,total," >> ${STATS_CSV}
	echo "SELECT COUNT(*) FROM locks;" | psql --tuples-only --no-align --quiet --echo-errors -h ${PSQL_HOST} -U ${PSQL_USER} ${DB} >> ${STATS_CSV}

	echo -n "accesses,total," >> ${STATS_CSV}
	echo "SELECT COUNT(*) FROM accesses;" | psql --tuples-only --no-align --quiet --echo-errors -h ${PSQL_HOST} -U ${PSQL_USER} ${DB} >> ${STATS_CSV}
else
	echo "${DATA} is not a valid gzip file! No stats produced!"
	exit 1
fi


rm ${DURATION_FILE}
