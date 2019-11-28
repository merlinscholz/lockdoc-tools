#!/bin/bash

if [ ${#} -lt 3 ];
then
	echo "${0} <groundtruth.csv> <hypothesizer input> <selection strategy> <max reduction factor|lowest acceptance threshold>" >&2
	exit 1
fi

BASE_DIR=`dirname ${0}`
GROUNDTRUTH_CSV=${1}; shift
HYPOINPUT_CSV=${1}; shift
SELECTION_STRATEGY=${1}; shift
PARAMETER_LIMIT=${1}; shift

TMP_FILE=$(mktemp /tmp/counter-examples-sql.XXXXXX)

if [ ${SELECTION_STRATEGY} == "sharpen" ];
then
	SEQUENCE=`LANG=c seq --format="%1.1f" 0.0 0.5 ${PARAMETER_LIMIT}`
elif [ ${SELECTION_STRATEGY} == "bottomup" ] || [ ${SELECTION_STRATEGY} == "topdown" ];
then
	SEQUENCE=`LANG=c seq --format="%1.1f" 100.0 -0.5 ${PARAMETER_LIMIT}`
else
	echo "Unknown selection strategy!" >&2
fi

echo "parameter;totalrules;matched;percentage"
for i in ${SEQUENCE}
do
	${BASE_DIR}/../hypothesizer/hypothesizer -g ${SELECTION_STRATEGY} -f ${i} -a ${i} -s member -r csvwinner ${HYPOINPUT_CSV} > ${TMP_FILE} 2>/dev/null
	echo -n "${i};"
	${BASE_DIR}/lockmining-verify.py ${GROUNDTRUTH_CSV} ${TMP_FILE} | tail --lines=+2 
done
rm "${TMP_FILE}"
