#!/bin/bash -e
# Alexnader Lochmann 2019
# Based on gather_on_test.sh
# (https://01.org/linuxgraphics/gfx-docs/drm/dev-tools/gcov.html)
# 
# This scripts executes a programm passed as commandline parameter (including its arguments),
# and gathers its kernel code coverage afterwards.
# 
# usage: ./gcov-trace.sh /opt/kernel/linux test.trace sleep 5

GCOV_DIR=${GCOV_DIR:-"/sys/kernel/debug/gcov/"}
OS=`uname`

if [ ${#} -lt 3 ]; then
  echo "Usage: $0 <path to kernel tree> <output filename> <programm to execute>" >&2
  exit 1
fi

KERNEL_TREE=${1};shift;
OUTPUT_FILE=${1};shift;

if [ `id -u` -ne 0 ];
then
  echo "Must be root!" >&2
  exit 1
fi

if [ ! -d ${GCOV_DIR} ];
then
	echo "${GCOV_DIR} does not exist!" >&2
	exit 1
fi
if [ ${OS} == "Linux" ];
then
	echo 0 > ${GCOV_DIR}/reset
elif [ ${OS} == "FreeBSD" ];
then
	echo "Enabling GCOV"
	sysctl debug.gcov.enable=1
	echo "Resetting GCOV"
	sysctl debug.gcov.reset=1
fi

${@}

TEMPDIR=$(mktemp -d)
find ${GCOV_DIR} -type d -exec mkdir -p $TEMPDIR/\{\} \;
find ${GCOV_DIR} -name '*.gcda' -exec sh -c 'cat < $0 > '${TEMPDIR}'/$0' {} \;

if [ ${OS} == "Linux" ];
then
	find ${GCOV_DIR} -name '*.gcno' -exec sh -c 'cp -d $0 '${TEMPDIR}'/$0' {} \;
elif [ ${OS} == "FreeBSD" ];
then
	find ${GCOV_DIR} -name '*.gcno' -exec sh -c 'cp -R $0 '${TEMPDIR}'/$0' {} \;
	echo "Disabing GCOV"
	sysctl debug.gcov.enable=0
fi

geninfo --output-filename ${OUTPUT_FILE} --base-directory ${KERNEL_TREE} ${TEMPDIR}

rm -rf $TEMPDIR
