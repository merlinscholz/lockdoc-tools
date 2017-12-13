#!/bin/bash
# A. Lochmann C 2017
# This script updates a database's function blacklist.
# The user just needs to provide the humand-readable names of the data type, and
# the functions which should be blacklisted.

if [ ${#} -lt 3 ];
then
	echo "usage: ${0} <database> <data type> <function1> [<function2> ...]" >&2
	exit 1
fi

DB=${1}; shift
DATA_TYPE=${1}; shift
FUNCTION=${1}; shift
MYSQL="mysql ${DB}"

while [ ! -z ${FUNCTION} ];
do
	echo "Blacklisting ${FUNCTION} for ${DATA_TYPE}..."
	${MYSQL} <<EOT
select id into @dtid from data_types where name = '${DATA_TYPE}';
insert into member_blacklist values (@dtid,NULL,'${FUNCTION}');
EOT
	FUNCTION=${1}; shift
done