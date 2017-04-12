#!/bin/bash
# A. Lochmann C 2017
# This script updates a database's member blacklist.
# The user just needs to provide the humand-readable names of the data type, and
# the member names which should be blacklisted.

if [ ${#} -lt 3 ];
then
	echo "usage: ${0} <database> <data type> <member name 1> [<member name 2> ...]
		  database might be a list of databases separated by a comma." >&2
	exit 1
fi

DATABASES=${1}
DATA_TYPE=${2}
MYSQL="mysql ${DB}"
COUNT=1
IFS=','

for db in ${DATABASES};
do
	echo "Inserting into database ${db}..."
	COUNT=3

	while [ ! -z ${!COUNT} ];
	do
		MEMBER_NAME=${!COUNT}
		echo "   Blacklisting: ${DATA_TYPE}.${MEMBER_NAME}"
		${MYSQL} <<EOT
select id into @dtid from data_types where name = '${DATA_TYPE}';
select id into @mnid from member_names where name = '${MEMBER_NAME}';
insert into member_blacklist values (@dtid,@mnid);
EOT
		let COUNT=COUNT+1
	done
done
