#!/bin/bash
# A. Lochmann C 2017
# This script updates a database's function blacklist.
# The user just needs to provide the humand-readable names of the data type, and
# the functions which should be blacklisted.

if [ ${#} -lt 3 ];
then
	echo "usage: ${0} <database> <host> <user> <data type> <subclass> <function1> [<function2> ...]" >&2
	exit 1
fi

DB=${1}; shift
HOST=${1}; shift
USER=${1}; shift
DATA_TYPE=${1}; shift
SUBCLASS=${1}; shift
FUNCTION=${1}; shift

while [ ! -z ${FUNCTION} ];
do
	echo "Blacklisting ${FUNCTION} for ${DATA_TYPE}..."
	if [ ${DATA_TYPE} == "any" ];
	then
		psql -A --field-separator='	' --pset footer --echo-errors -h ${HOST} -U ${USER} ${DB} <<EOT
			insert into function_blacklist (id,member_name_id,fn) values (NULL,NULL,'${FUNCTION}');
EOT
	else
		psql -A --field-separator='	' --pset footer --echo-errors -h ${HOST} -U ${USER} ${DB} <<EOT
		DO \$\$
		DECLARE
			scid bigint;
		BEGIN
			SELECT sc.id INTO scid 
			FROM data_types AS dt
			JOIN subclasses AS sc
			ON dt.id = sc.data_type_id
			WHERE dt.name = '${DATA_TYPE}'
			AND sc.name = '${SUBCLASS}';
			insert into function_blacklist (subclass_id,member_name_id,fn) values (scid,NULL,'${FUNCTION}');
		END
		\$\$;
EOT
	fi
	FUNCTION=${1}; shift
done
