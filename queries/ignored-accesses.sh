#!/bin/bash
if [ ${#} -lt 1 ];
then
	echo "${0} <data type> [<member> [<access type (or any)>]]" >&2
	exit 1
fi

COMBINED_DATATYPE=${1}; shift
MEMBER=${1}; shift
ACCESS_TYPE=${1}; shift

if [ ! -z ${COMBINED_DATATYPE} ];
then
	RET=`echo ${COMBINED_DATATYPE} | grep -q ":"`
	if [ ${?} -eq 0 ];
	then
		DATATYPE=`echo ${COMBINED_DATATYPE} | cut -d ":" -f1`
		SUBCLASS=`echo ${COMBINED_DATATYPE} | cut -d ":" -f2`
		DATATYPE_FILTER="AND sc.id = (SELECT id FROM subclasses WHERE name = '${SUBCLASS}') AND sc.data_type_id = (SELECT id FROM data_types WHERE name = '${DATATYPE}')"
	else
		DATATYPE_FILTER="AND sc.data_type_id = (SELECT id FROM data_types WHERE name = '${COMBINED_DATATYPE}')"
	fi
fi

if [ ! -z ${MEMBER} ];
then
	MEMBER_FILTER="AND mn.id = (SELECT id FROM member_names WHERE name = '${MEMBER}') -- Only show results for a certain member"
fi

if [ ! -z ${ACCESS_TYPE} ];
then
	ACCESS_TYPE_FILTER="AND ac.type = '${ACCESS_TYPE}' -- Filter by access type"
fi

cat <<EOT
SELECT ac.id, ac.type AS type, sc.data_type_id AS data_type_id, mn.name AS member, sl.offset,
	sl.size, st.sequence, st.function, m_bl.member_name_id IS NOT NULL AS member_bl, fn_bl.fn IS NOT NULL AS fn_bl
FROM accesses ac
JOIN allocations a
  ON ac.alloc_id = a.id
JOIN subclasses sc
  ON a.subclass_id = sc.id
JOIN stacktraces AS st
  ON ac.stacktrace_id = st.id
-- AND st.sequence = 0
LEFT JOIN structs_layout_flat sl
  ON sc.data_type_id = sl.data_type_id
 AND ac.address - a.base_address = sl.helper_offset
LEFT JOIN member_names mn
  ON mn.id = sl.member_name_id
LEFT JOIN member_blacklist m_bl
  ON m_bl.subclass_id = a.subclass_id
 AND m_bl.member_name_id = sl.member_name_id
LEFT JOIN function_blacklist fn_bl
  ON fn_bl.fn = st.function
 AND 
 (
   (fn_bl.subclass_id IS NULL  AND fn_bl.member_name_id IS NULL) -- globally blacklisted function
   OR
   (fn_bl.subclass_id = a.subclass_id AND fn_bl.member_name_id IS NULL) -- for this data type blacklisted
   OR
   (fn_bl.subclass_id = a.subclass_id AND fn_bl.member_name_id = sl.member_name_id) -- for this member blacklisted
 )
WHERE 1
-- Name the data type of interest here
${DATATYPE_FILTER}
${MEMBER_FILTER}
${ACCESS_TYPE_FILTER}
-- ====================================
AND 
(
	fn_bl.fn IS NOT NULL OR m_bl.member_name_id IS NOT NULL
)
EOT

