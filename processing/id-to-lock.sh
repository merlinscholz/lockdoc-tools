#!/bin/bash

if [ ${#} -lt 3 ];
then
	echo "usage: ${0} <database> <vmlinux> <lock-id>"
	exit 1
fi

DB=${1}
shift
VMLINUX=${1}
shift
LOCK_ID=${1}
shift

LOCK_ADDR=`mysql --batch --skip-column-names --execute="SELECT HEX(l.ptr) FROM locks AS l WHERE l.id = ${LOCK_ID} AND l.embedded_in IS NULL;" ${DB}`
if [ -z ${LOCK_ADDR} ];
then
	echo "No such global lock!"
	exit 1
fi
echo "Address is ${LOCK_ADDR}"

RESULT=`nm -n ${VMLINUX} | grep -i "${LOCK_ADDR}"`
if [ ! -z "${RESULT}" ];
then
	echo ${RESULT}
	exit 0
fi
echo "No proper symbol found. Trying it again with a more coarse-grained addr..."

LOCK_ADDR=`mysql --batch --skip-column-names --execute="SELECT HEX(l.ptr >> 8) FROM locks AS l WHERE l.id = ${LOCK_ID} AND l.embedded_in IS NULL;" ${DB}`
echo "Using address ${LOCK_ADDR}"
nm -n ${VMLINUX} | grep -i "${LOCK_ADDR}"
