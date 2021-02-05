#!/bin/bash

if [ ${#} -lt 3 ];
then
	echo "usage: ${0} <drop file> <LTP src dir> <map dir>"
	exit 1
fi

DROP_FILE=${1}; shift
LTP_TESTSUITES=${1}/runtest; shift
MAP_DIR=${1}; shift

for i in ${MAP_DIR}/*.map;
do
	FNAME=`basename ${i}`
	FBASENAME=${FNAME%.*}

	grep -q ${FBASENAME} ${DROP_FILE}
	if [ ${?} -eq 1 ];
	then
		#echo "Didn't find ${FBASENAME} in ${DROP_FILE}"
		TESTSUITE=`echo ${FBASENAME} | cut -d "-" -f 2`
		TEST=`echo ${FBASENAME} | cut -d "-" -f 3`
		grep "^${TEST}" ${LTP_TESTSUITES}/${TESTSUITE}
	fi
done
