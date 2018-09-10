#!/bin/bash
# Does all required steps of post processing to derive the lock hypotheses: convert, import, flatten structs layout, extract txns from database, and runs the hypothesizer.
# If required, it will wait for the fail-client to terminate, and automatically start the post processing.
# 
TOOLS_PATH=`dirname ${0}`

function usage() {
        echo "usage: $0 <database> [ pid of fail-client ]" >&2
        exit 1
}

if [ -z ${1} ];
then
        usage
fi
DB=$1
shift

if [ ! -z ${1} ];
then
	echo "Waiting for fail-client (pid ${1}) to terminate..."
	while kill -0 ${1} 2>/dev/null
	do
		sleep 30;
	done
fi

${TOOLS_PATH}/conv-import.sh ${DB} -1
if [ ${?} -ne 0 ];
then
	echo "Cannot convert and import trace!">&2 
	exit 1
fi 
echo "Flatten structs layout..."
${TOOLS_PATH}/queries/flatten-structs_layout.sh ${DB}
if [ ${?} -ne 0 ];
then
	echo "Cannot flatten structs layout!">&2 
	exit 1
fi 
echo "Deleting accesses to atomic members..."
${TOOLS_PATH}/queries/del-atomic-from-trace.sh ${DB}
if [ ${?} -ne 0 ];
then
	echo "Cannot delete atomic members!">&2 
	exit 1
fi
${TOOLS_PATH}/get-run-hypothesizer.sh ${DB} nostack
if [ ${?} -ne 0 ];
then
	echo "Cannot run hypothesizer!">&2 
	exit 1
fi
${TOOLS_PATH}/processing/get-process-cex.sh ${DB} any nostack
if [ ${?} -ne 0 ];
then
	echo "Cannot run get-process-cex.sh for nostack!">&2 
	exit 1
fi
echo "Finished processing variant nostack"
echo "-----------------------------------"
${TOOLS_PATH}/get-run-hypothesizer.sh ${DB} stack
if [ ${?} -ne 0 ];
then
	echo "Cannot run hypothesizer!">&2 
	exit 1
fi
${TOOLS_PATH}/processing/get-process-cex.sh ${DB} any stack
if [ ${?} -ne 0 ];
then
	echo "Cannot run get-process-cex.sh for stack!">&2 
	exit 1
fi 
echo "Finished processing variant stack"
echo "-----------------------------------"
