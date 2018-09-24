#!/bin/bash
TOOLS_PATH=`dirname ${0}`
DATATYPES=("inode" "journal_t" "transaction_t" "inode:ext4" "inode:proc" "inode:sysfs" "inode:tracefs")
OUT_DIR=jan

if [ ${#} -lt 3 ];
then
	echo "${0} <hypo input> <cex csv> <vmlinux>" >&2
	exit 1
fi

HYPO_INPUT=${1}; shift
CEX_INPUT=${1}; shift
KERNEL=${1}; shift

RESULTS_DIR=`dirname ${HYPO_INPUT}`
CONVERT_CONF=${RESULTS_DIR}/convert.conf

if [ ! -f ${CONVERT_CONF} ];
then
	echo "${CONVERT_CONF} does not exits" >&2
	exit 1
fi

if [ ! -d ${OUT_DIR} ];
then
	mkdir ${OUT_DIR}
fi

cp ${CONVERT_CONF} ${OUT_DIR}/

for datatype in "${DATATYPES[@]}"
do
	echo "Processing data type: ${datatype} ..."
	echo "-------------------------------------"
	${TOOLS_PATH}/hypothesizer/hypothesizer -r normal -s member -d ${datatype} ${HYPO_INPUT} > ${OUT_DIR}/${datatype}-hypotheses.txt
	${TOOLS_PATH}/hypothesizer/hypothesizer -r csvwinner -s member -d ${datatype} ${HYPO_INPUT} > ${OUT_DIR}/${datatype}-hypotheses-winner.csv
	echo "data_type;member;accesstype;stacktrace;locks_held" > ${OUT_DIR}/${datatype}-cex.csv
	grep "^${datatype}\(:[a-zA-Z0-9_]\+\)*;" ${CEX_INPUT} >> ${OUT_DIR}/${datatype}-cex.csv
	${TOOLS_PATH}/processing/pretty-print-cex.py -u https://ess.cs.tu-dortmund.de/pferd/linux-lockdoc/lockdebug-v4.10-0.11 ${OUT_DIR}/${datatype}-cex.csv ${KERNEL} ${OUT_DIR}/${datatype}-hypotheses-winner.csv > ${OUT_DIR}/${datatype}-cex.html
	echo "-------------------------------------"
done
