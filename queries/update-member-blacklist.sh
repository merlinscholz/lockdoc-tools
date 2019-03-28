#!/bin/bash
# A. Lochmann C 2017
# This script updates a database's member blacklist.
# The user just needs to provide the humand-readable names of the data type, and
# the member names which should be blacklisted.

if [ ${#} -lt 3 ];
then
	echo "usage: ${0} <database> <host> <user> <data type> <subclass> <member name 1> [<member name 2> ...]
		  database might be a list of databases separated by a comma." >&2
	exit 1
fi

DATABASES=${1}; shift
HOST=${1}; shift
USER=${1}; shift
DATA_TYPE=${1}; shift
SUBCLASS=${1}; shift
COUNT=1
IFS=','

for db in ${DATABASES};
do
	echo "Inserting into database ${db}..."

	while [ ! -z ${!COUNT} ];
	do
		MEMBER_NAME=${!COUNT}
		echo "   Blacklisting: ${DATA_TYPE}.${MEMBER_NAME}"
psql -A --field-separator='	' --pset footer --echo-errors -h ${HOST} -U ${USER} ${db} <<EOT
		DO \$\$
		DECLARE
			scid bigint;
			mnid bigint;
		BEGIN
			SELECT sc.id INTO scid 
			FROM data_types AS dt
			JOIN subclasses AS sc
			ON dt.id = sc.data_type_id
			WHERE dt.name = '${DATA_TYPE}'
			AND sc.name = '${SUBCLASS}';
			select id into mnid from member_names AS mn where mn.name = '${MEMBER_NAME}';
			insert into member_blacklist values (scid,mnid);
		END
		\$\$;
EOT
		let COUNT=COUNT+1
	done
done
