#!/bin/bash
if [ ${#} -lt 3 ];
then
	echo "usage: ${0} <KCOV binary> <LTP root> <output directory>" >&2
	exit 2
fi
if [ `id -u` -ne 0 ];
then
	echo "Must be root" >&2
	exit 3
fi

#DUMP=${DUMP:-0}
KCOV_BINARY=${1}; shift;

export LTPROOT=${1}; shift;
export PATH="$PATH:$LTPROOT/testcases/bin"
export TMPDIR=`mktemp -d /tmp/kcov.XXX`
LTP_TEST_DIR=${LTPROOT}/runtest

OUT_DIR=${1}; shift;
if [ ! -e ${OUT_DIR} ];
then
	mkdir -p ${OUT_DIR}
fi

function run_cmd() {
	CMD="${KCOV_BINARY} ${1}"
	if [ -z ${DUMP} ];
	then
		eval ${CMD} 2> >(sort -u | sed -e s/^0x// > ${2})
		if [ ${?} -ne 0 ];
		then
			echo "Error running: ${CMD}"
		fi
	else
		echo "${CMD} 2> ${2}"
	fi
}

if [ -z ${TESTS} ];
then
	TESTS_TO_RUN=`find ${LTP_TEST_DIR}/ -type f -printf "%f\n"`
else
	TESTS_TO_RUN=${TESTS}
	IFS=","
fi

for i in ${TESTS_TO_RUN};
do
	TEST_SUITE=${i}
	TEST_SUITE_FILE=${LTP_TEST_DIR}/${TEST_SUITE}
	TEST_SUITE_OUT_DIR=${OUT_DIR}/${TEST_SUITE}
	if [ ! -e ${TEST_SUITE_FILE} ];
	then
		echo "${TEST_SUITE_FILE} does not exist"
		continue
	fi
	if [ ! -e ${TEST_SUITE_OUT_DIR} ];
	then
		mkdir -p ${TEST_SUITE_OUT_DIR}
	fi
	while read line
	do
		if [[ ${line} =~ ^# ]] || [[ -z "${line}" ]];
		then
			continue
		fi
		TEST_NAME=`echo "${line}" | tr "\t" " " | cut --delimiter=' ' --field=1`
		TEST_CMD=`echo "${line}" | tr "\t" " " | cut --delimiter=' ' --field="2-"`
		TEST_BIN=`echo "${line}" | tr "\t" " " | cut --delimiter=' ' --field="2"`
		TEST_PARAMS=`echo "${line}" | tr "\t" " " | cut --delimiter=' ' --field="3-"`
		echo "Running test '${TEST_NAME}'"
		if [[ ${TEST_CMD} =~ [\"\'\;|\<\>\$\\]+ ]];
		then
			echo "Using bash"
			run_cmd "/bin/bash -c \"${TEST_CMD}\"" ${TEST_SUITE_OUT_DIR}/ltp-${TEST_SUITE}-${TEST_NAME}.cov
		else
			if [ -z ${TEST_PARAMS} ];
			then
				run_cmd "`which ${TEST_BIN}`" ${TEST_SUITE_OUT_DIR}/ltp-${TEST_SUITE}-${TEST_NAME}.cov
			else
				run_cmd "`which ${TEST_BIN}` ${TEST_PARAMS}" ${TEST_SUITE_OUT_DIR}/ltp-${TEST_SUITE}-${TEST_NAME}.cov
			fi
		fi
	done < ${TEST_SUITE_FILE}
	echo "Running testsuite '${TEST_SUITE}'"
	run_cmd "${LTPROOT}/runltp -q -f ${TEST_SUITE}" ${OUT_DIR}/ltp-${TEST_SUITE}.cov
done
#echo "Running ltp"
#run_cmd "${LTPROOT}/runltp -q" ${OUT_DIR}/ltp.cov
rm -r ${TMPDIR}
