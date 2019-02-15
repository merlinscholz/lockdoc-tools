#!/bin/bash -e
# Alexnader Lochmann 2019
# Based on gather_on_test.sh
# (https://01.org/linuxgraphics/gfx-docs/drm/dev-tools/gcov.html)
# 
# This scripts executes a programm passed as commandline parameter (including its arguments),
# and gathers its kernel code coverage afterwards.
# 
# usage: ./gcov-trace.sh /opt/kernel/linux test.trace sleep 5

GCOV_DIR="/sys/kernel/debug/gcov/"

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

echo 0 > ${GCOV_DIR}/reset

${@}

TEMPDIR=$(mktemp -d)
find ${GCOV_DIR} -type d -exec mkdir -p $TEMPDIR/\{\} \;
find ${GCOV_DIR} -name '*.gcda' -exec sh -c 'cat < $0 > '${TEMPDIR}'/$0' {} \;
find ${GCOV_DIR} -name '*.gcno' -exec sh -c 'cp -d $0 '${TEMPDIR}'/$0' {} \;

geninfo --output-filename ${OUTPUT_FILE} --base-directory ${KERNEL_TREE} ${TEMPDIR}

rm -rf $TEMPDIR
