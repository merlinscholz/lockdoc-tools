#!/bin/bash

if [ ${#} -lt 2 ];
then
	echo "usage: ${0} <KCOV maps> <out dir>"
	exit 1
fi

KCOV_DIR=${1}; shift
OUT_DIR=${1}; shift

if [ ! -d ${OUT_DIR} ];
then
	mkdir -p ${OUT_DIR}
fi

mkdir ${OUT_DIR}/unique-maps
for i in `find ${KCOV_DIR} -iname "*.map"`
do
	mkdir -p ${OUT_DIR}/unique-maps/`dirname $i`
	sort -u < $i > ${OUT_DIR}/unique-maps/$i
done

mkdir ${OUT_DIR}/all-maps
for i in `find ${KCOV_DIR} -iname "*.map"`
do
	cp $i ${OUT_DIR}/all-maps/`basename $i`
done
