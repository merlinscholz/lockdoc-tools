#!/bin/bash
CONFIGFILE="convert.conf"
TOOLS_PATH=`dirname ${0}`
BASE_URL="https://ess.cs.tu-dortmund.de/pferd/linux-lockdoc/lockdebug-v4.10-0.12"

function usage() {
	echo "usage: $0 <database> <data_type> <input bugs> <input winner> <output suffix>" >&2
	exit 1
}

if [ ${#} -lt 5 ];
then
	usage ${0}
fi
DB=${1};shift
DATA_TYPE=${1};shift
INPUT_BUGS=${1};shift
INPUT_WINNER=${1};shift
OUTPUT_SUFFIX=${1};shift

if [ ! -f ${CONFIGFILE} ];
then
	echo "${CONFIGFILE} does not exist!" >&2
	exit 1
fi
. ${CONFIGFILE}

if [ -z ${KERNEL} ];
then
	echo "Variable KERNEL is not set!" >&2
	exit 1
fi

if [ ${DATA_TYPE} == "any" ];
then
	CEX_CSV="cex-${OUTPUT_SUFFIX}.csv"
	CEX_HTML="cex-${OUTPUT_SUFFIX}.html"
else
	CEX_CSV="cex-${OUTPUT_SUFFIX}-${DATA_TYPE}.csv"
	CEX_HTML="cex-${OUTPUT_SUFFIX}-${DATA_TYPE}.html"
fi


${TOOLS_PATH}/get-counterexamples.sh ${INPUT_BUGS} ${DATA_TYPE} ${DB} 1 > ${CEX_CSV}
if [ ${?} -ne 0 ];
then
	echo "Cannot run get-counterexamples.sh!">&2 
	exit 1
fi

SIZE=`stat --printf="%s" ${CEX_CSV}`
if [ ${SIZE} -eq 0 ];
then
	echo "${CEX_CSV} is empty. Stop processing '${INPUT_BUGS}' for '${DATA_TYPE}'"
	rm ${CEX_CSV}
	exit 0
fi

${TOOLS_PATH}/pretty-print-cex.py -u ${BASE_URL} ${CEX_CSV} ${KERNEL} ${INPUT_WINNER} > ${CEX_HTML}
if [ ${?} -ne 0 ];
then
	echo "Cannot run pretty-print-cex.py!">&2 
	exit 1
fi
