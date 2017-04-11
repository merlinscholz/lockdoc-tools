#!/bin/bash
# A. Lochmann C 2017
# This script updates a database's member blacklist.
# The user just needs to provide the humand-readable names of the data type, and
# the member names which should be blacklisted.

if [ ${#} -lt 3 ];
then
	echo "usage: ${0} <database> <data type> <member name 1> [<member name 2> ...]" >&2
	exit 1
fi

DB=${1}; shift
DATA_TYPE=${1}; shift
MEMBER_NAME=${1}; shift
MYSQL="mysql ${DB}"

while [ ! -z ${MEMBER_NAME} ];
do
	echo "Blacklisting ${DATA_TYPE}.${MEMBER_NAME}..."
	${MYSQL} <<EOT
select id into @dtid from data_types where name = '${DATA_TYPE}';
select id into @mnid from member_names where name = '${MEMBER_NAME}';
insert into member_blacklist values (@dtid,@mnid);
EOT
	MEMBER_NAME=${1}; shift
done
