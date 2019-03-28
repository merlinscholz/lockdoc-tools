#!/bin/bash
# A. Lochmann 2018
# This script gets all accesses to data_type.member grouped by data_type, member, access_type, stracktrace and locks_held.
# Usage: ./accesses_by_lock_member_fn.sh transaction_t t_inode_list w 1 | mysql lockdebugging_mixed_fs_al

if [ ${#} -lt 1 ];
then
	echo "${0} <data type> [<member (or any)> [<access type (or any)>] [<ignore fn blacklist>]]" >&2
	exit 1
fi

COMBINED_DATATYPE=${1}; shift
MEMBER=${1}; shift
ACCESS_TYPE=${1}; shift
IGNORE_FUNCTION_BLACKLIST=${1}; shift

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

if [ ! -z ${MEMBER} ] && [ ${MEMBER} != "any" ];
then
	MEMBER_FILTER="AND mn.id = (SELECT id FROM member_names WHERE name = '${MEMBER}') -- Only show results for a certain member"
fi

if [ ! -z ${ACCESS_TYPE} ] && [ ${ACCESS_TYPE} != "any" ];
then
	ACCESS_TYPE_FILTER="AND ac.type = '${ACCESS_TYPE}' -- Filter by access type"
fi

if [ ! -z ${IGNORE_FUNCTION_BLACKLIST} ] && [ ${IGNORE_FUNCTION_BLACKLIST} -eq 1 ];
then
	FUNCTION_BLACKLIST_FILTER=""
else
	FUNCTION_BLACKLIST_FILTER="AND NOT EXISTS
			(
				-- Get all accesses that happened on an init path or accessed a blacklisted member
				SELECT 1
				FROM accesses s_ac
				JOIN allocations s_a
				  ON s_ac.alloc_id = s_a.id
				JOIN subclasses s_sc
				  ON s_a.subclass_id = s_sc.id
				LEFT JOIN structs_layout_flat s_sl
				  ON s_sc.data_type_id = s_sl.data_type_id
				 AND s_ac.address - s_a.base_address = s_sl.helper_offset
				JOIN stacktraces AS s_st
				  ON s_ac.stacktrace_id = s_st.id
				LEFT JOIN member_names s_mn
				  ON s_mn.id = s_sl.member_name_id
				LEFT JOIN function_blacklist s_fn_bl
				  ON s_fn_bl.fn = s_st.function
				 AND
				 (
				   (s_fn_bl.subclass_id IS NULL  AND s_fn_bl.member_name_id IS NULL) -- globally blacklisted function
				   OR
				   (s_fn_bl.subclass_id = s_a.subclass_id AND s_fn_bl.member_name_id IS NULL) -- for this data type blacklisted
				   OR
				   (s_fn_bl.subclass_id = s_a.subclass_id AND s_fn_bl.member_name_id = s_sl.member_name_id) -- for this member blacklisted
				 )
				LEFT JOIN member_blacklist s_m_bl
				  ON s_m_bl.subclass_id = s_a.subclass_id
				 AND s_m_bl.member_name_id = s_sl.member_name_id
				WHERE ac.id = s_ac.id
				${DATATYPE_FILTER}
				${MEMBER_FILTER}
				${ACCESS_TYPE_FILTER}
				-- ====================================
				AND
				(
					s_fn_bl.fn IS NOT NULL OR s_m_bl.member_name_id IS NOT NULL
				)
				LIMIT 1
			)"
fi

cat <<EOT

SELECT dt_name, sl_member, ac_type, stacktrace, (CASE WHEN locks_held IS NULL OR locks_held='([])@@:' THEN 'nolocks' ELSE locks_held END) AS locks_held, 
		string_agg(cast(ac_id AS TEXT), ';' ORDER BY ac_id) AS accesses,
		string_agg(cast(m_bl AS TEXT), ';' ORDER BY ac_id) AS m_bl,
		string_agg(cast(fn_bl AS TEXT), ';' ORDER BY ac_id) AS fn_bl, COUNT(*) AS num
FROM
(
	SELECT
		ac_id,
		dt_name,
		sl_member,
		ac_type,
		string_agg(
		CASE
			WHEN l.embedded_in IS NULL AND l.lock_var_name IS NULL
				THEN CONCAT(l.id, '(', l.lock_type_name, '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- global (or embedded in unknown allocation *and* no name available)
			WHEN l.embedded_in IS NULL AND l.lock_var_name IS NOT NULL
				THEN CONCAT(l.lock_var_name, ':', l.id, '(', l.lock_type_name, '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- global (or embedded in unknown allocation *and* a name is available)
			WHEN l.embedded_in IS NOT NULL AND l.embedded_in = alloc_id
				THEN CONCAT('EMBSAME(', CONCAT(lock_dt.name, '.', (CASE WHEN l.address - lock_a.base_address = lock_member.helper_offset THEN mn_lock_member.name ELSE CONCAT(mn_lock_member.name, '?') END)), '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- embedded in same
			ELSE CONCAT('EMBOTHER', '(',  CONCAT(lock_dt.name, '.', (CASE WHEN l.address - lock_a.base_address = lock_member.helper_offset THEN mn_lock_member.name ELSE CONCAT(mn_lock_member.name, '?') END)), '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- embedded in other
--			ELSE CONCAT('EMB:', l.id, '(',  CONCAT(lock_dt.name, '.', (CASE WHEN l.address - lock_a.base_address = lock_member.helper_offset THEN mn_lock_member.name ELSE CONCAT(mn_lock_member.name, '?') END)), ')', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- embedded in other
			END
		, ' -> ' ORDER BY lh.start) AS locks_held,
		stacktrace,
		m_bl,
		fn_bl
	FROM
	(
		-- Get all accesses. Add information about the accessed member, data type, and the function the memory has been accessed from.
		-- Filter out every function that is on our function blacklist.
		SELECT
			ac.id AS ac_id,
			ac.txn_id AS ac_txn_id,
			ac.alloc_id AS alloc_id,
			ac.type AS ac_type,
			mn.name AS sl_member,
			(CASE WHEN sc.name IS NULL THEN dt.name ELSE CONCAT(dt.name, ':', sc.name) END) AS dt_name,
			string_agg(
				CONCAT(lower(to_hex(st.instruction_ptr)), '@', st.function, '@', st.file, ':', st.line,
					'@m_bl:', (CASE WHEN m_bl.subclass_id IS NOT NULL THEN 1 ELSE 0 END), '@fn_bl:', (CASE WHEN fn_bl.fn IS NOT NULL THEN 1 ELSE 0 END))
			, ',' ORDER BY st.sequence) AS stacktrace,
			(CASE WHEN bool_or(m_bl.subclass_id IS NOT NULL) THEN 1 ELSE 0 END) AS m_bl,
			(CASE WHEN bool_or(fn_bl.fn IS NOT NULL) THEN 1 ELSE 0 END) AS fn_bl
		FROM accesses AS ac
		INNER JOIN allocations AS a
		  ON a.id = ac.alloc_id
		INNER JOIN subclasses AS sc
		  ON sc.id = a.subclass_id
		INNER JOIN data_types AS dt
		  ON dt.id = sc.data_type_id
		INNER JOIN stacktraces AS st
		  ON ac.stacktrace_id = st.id
		LEFT JOIN structs_layout_flat sl
		  ON sc.data_type_id = sl.data_type_id
		 AND ac.address - a.base_address = sl.helper_offset
		LEFT JOIN member_names AS mn
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
			AND
			(fn_bl.sequence IS NULL OR fn_bl.sequence = st.sequence)
		 )
		WHERE True
			-- Name the data type of interest here
			${DATATYPE_FILTER}
			${MEMBER_FILTER}
			${ACCESS_TYPE_FILTER}
			${FUNCTION_BLACKLIST_FILTER}
		GROUP BY ac.id, ac.txn_id, ac.alloc_id, ac.type, mn.name, dt_name -- Remove duplicate entries. Some accesses might be mapped to more than one member, e.g., an union.
	) s
	LEFT JOIN locks_held AS lh
	  ON lh.txn_id = ac_txn_id
	LEFT JOIN locks AS l
	  ON l.id = lh.lock_id
	LEFT JOIN allocations AS lock_a
	  ON lock_a.id = l.embedded_in
	LEFT JOIN subclasses lock_sc
	  ON lock_a.subclass_id = lock_sc.id
	LEFT JOIN data_types lock_dt
	  ON lock_sc.data_type_id = lock_dt.id
	LEFT JOIN structs_layout_flat lock_member
	  ON lock_sc.data_type_id = lock_member.data_type_id
	  AND l.address - lock_a.base_address = lock_member.helper_offset
	LEFT JOIN member_names mn_lock_member
	  ON mn_lock_member.id = lock_member.member_name_id
	GROUP BY ac_id, dt_name, sl_member, ac_type, stacktrace, m_bl, fn_bl
) t
-- Since we want a detailed view about where an access happenend, the result is additionally grouped by ac_fn and st_instrptr.
GROUP BY dt_name, sl_member, ac_type, stacktrace, locks_held
ORDER BY dt_name, sl_member, ac_type, stacktrace, locks_held, num DESC;
EOT
