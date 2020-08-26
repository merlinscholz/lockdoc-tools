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
SORTUNIQ=`dirname ${0}`"/sortuniq"
if [ ! -e ${SORTUNIQ} ];
then
	echo "${SORTUNIQ} does not exist" >&2
	exit 1
fi

export LTPROOT=${1}; shift;
export LTP_DEV=${DEVICE}
export LTP_DEV_FS_TYPE=ext4
export LTP_BIG_DEV=${DEVICE}
export LTP_BIG_DEV_FS_TYPE=ext4
LTP_TEST_DIR=${LTPROOT}/runtest
export PATH="$PATH:$LTPROOT/testcases/bin:${LTPROOT}/bin"
export TMPDIR=`mktemp -d /tmp/kcov.XXX`
chmod 0777 ${TMPDIR}

OUT_DIR=`readlink -f ${1}`; shift;
if [ ! -e ${OUT_DIR} ];
then
	mkdir -p ${OUT_DIR}
fi

function run_cmd() {
	OUTFILE=${2}
	MAX_FD=`ulimit -Sn`
	OUT_FD=`echo  ${MAX_FD} - 1 | bc`
	FOO="exec ${OUT_FD}> >(sed -e 's/^0x//' | ${SORTUNIQ} > ${OUTFILE}.map)"
	eval $FOO
	CMD="KCOV_OUT=fd LD_PRELOAD=${KCOV_BINARY} ${1}"
	fi
	if [ -z ${DUMP} ];
	then
		eval ${CMD}
		if [ ${?} -ne 0 ];
		then
			echo "Error running: ${CMD}"
		fi
		FOO="exec ${OUT_FD}>&-"
		eval ${FOO}
	else
		echo "${CMD} 2> ${OUTFILE}"
	fi
}

if [ -z ${TESTS} ];
then
	TESTS_TO_RUN=`find ${LTP_TEST_DIR}/ -type f -printf "%f\n"`
else
	TESTS_TO_RUN=${TESTS}
	IFS=","
fi

echo "LTPROOT="${LTPROOT}
echo "LTP_DEV="${LTP_DEV}
echo "LTP_DEV_FS_TYPE="${LTP_DEV_FS_TYPE}
echo "LTP_BIG_DEV="${LTP_BIG_DEV}
echo "LTP_BIG_DEV_FS_TYPE="${LTP_BIG_DEV_FS_TYPE}

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
			run_cmd "/bin/bash -c \"${TEST_CMD}\"" ${TEST_SUITE_OUT_DIR}/ltp-${TEST_SUITE}-${TEST_NAME}
		else
			if [ -z ${TEST_PARAMS} ];
			then
				run_cmd "`which ${TEST_BIN}`" ${TEST_SUITE_OUT_DIR}/ltp-${TEST_SUITE}-${TEST_NAME}
			else
				run_cmd "`which ${TEST_BIN}` ${TEST_PARAMS}" ${TEST_SUITE_OUT_DIR}/ltp-${TEST_SUITE}-${TEST_NAME}
			fi
		fi
	done < ${TEST_SUITE_FILE}
	#echo "Running testsuite '${TEST_SUITE}'"
	#run_cmd "${LTPROOT}/runltp -q -f ${TEST_SUITE}" ${OUT_DIR}/ltp-${TEST_SUITE}
done
#echo "Running ltp"
#run_cmd "${LTPROOT}/runltp -q" ${OUT_DIR}/ltp.cov
rm -r ${TMPDIR}