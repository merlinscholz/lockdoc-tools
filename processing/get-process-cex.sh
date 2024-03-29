#!/bin/bash
CONFIGFILE="convert.conf"
TOOLS_PATH=`dirname ${0}`

function usage() {
	echo "usage: $0 <database> <data_type> <input bugs> <input winner> <output suffix>" >&2
	exit 1
}

BASE_URL=${BASE_URL:-"https://ess.cs.tu-dortmund.de/lockdoc-elixir/linux-lockdoc/lockdoc-v5.4.0-8"}
SKIP_QUERIES=${SKIP_QUERIES:-0}
DISPLAY_VARIANT=${DISPLAY_VARIANT:-dynamic}

if [ ${DISPLAY_VARIANT} != "tree" ] && [ ${DISPLAY_VARIANT} != "graph" ] && [ ${DISPLAY_VARIANT} != "dynamic" ];
then
	echo "Unknown display variant: ${DISPLAY_VARIANT}" >&2
	exit 1
fi

if [ ${#} -lt 5 ];
then
	usage ${0}
fi

DB=${1};shift
DATA_TYPE=${1};shift
INPUT_BUGS=${1};shift
INPUT_WINNER=${1};shift
OUTPUT_SUFFIX=${1};shift
HOST=${1}; shift
USER=${1}; shift

if [ ! -f ${CONFIGFILE} ];
then
	echo "${CONFIGFILE} does not exist!" >&2
	exit 1
fi
. ${CONFIGFILE}

if [ ${DATA_TYPE} == "any" ];
then
	CEX_CSV="cex-${OUTPUT_SUFFIX}.csv"
	CEX_HTML="cex-${OUTPUT_SUFFIX}.html"
else
	CEX_CSV="cex-${OUTPUT_SUFFIX}-${DATA_TYPE}.csv"
	CEX_HTML="cex-${OUTPUT_SUFFIX}-${DATA_TYPE}.html"
fi

if [ ${SKIP_QUERIES} -eq 0 ];
then
	${TOOLS_PATH}/get-counterexamples.sh ${INPUT_BUGS} ${DATA_TYPE} ${DB} ${HOST} ${USER} 1 > ${CEX_CSV}
	if [ ${?} -ne 0 ];
	then
		echo "Cannot run get-counterexamples.sh!">&2
		exit 1
	fi
fi

if [ ! -f ${CEX_CSV} ];
then
	echo "${CEX_CSV} does not exist. Stop processing '${INPUT_BUGS}' for '${DATA_TYPE}'"
	exit 0
fi

LINES=`wc -l ${CEX_CSV} | cut -d ' ' -f1`
# Since we use psql, queries/get-counterexamples.sh will print at least the header.
# Therefore, LINES will be 1 at least.
if [ ${LINES} -le 1 ];
then
	echo "${CEX_CSV} is empty. Stop processing '${INPUT_BUGS}' for '${DATA_TYPE}'"
	rm ${CEX_CSV}
	exit 0
fi

${TOOLS_PATH}/pretty-print-cex.py -c cex-temp/ -d ${DISPLAY_VARIANT} -u ${BASE_URL} ${CEX_CSV} ${INPUT_WINNER} > ${CEX_HTML}
if [ ${?} -ne 0 ];
then
	echo "Cannot run pretty-print-cex.py!">&2 
	exit 1
fi
