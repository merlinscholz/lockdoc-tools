#!/bin/bash
# A. Lochmann 2018
# This script gets all accesses to data_type.member grouped by data_type, member, access_type, stracktrace and locks_held.
# Usage: ./accesses_by_lock_member_fn.sh transaction_t t_inode_list w | mysql lockdebugging_mixed_fs_al

if [ ${#} -lt 1 ];
then
	echo "${0} <data type> [<member> [<access type (or any)>]]" >&2
	exit 1
fi

DATATYPE=${1}; shift
MEMBER=${1}; shift
ACCESS_TYPE=${1}; shift

if [ ! -z ${DATATYPE} ];
then
	DATATYPE_FILTER="AND a.data_type_id = (SELECT id FROM data_types WHERE name = '${DATATYPE}')"
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
SELECT dt_name, sl_member, ac_type, stacktrace, IF(locks_held IS NULL, 'nolocks', locks_held) AS locks_held, COUNT(*) AS num
FROM
(
	SELECT
		dt_name,
		sl_member,
		ac_type,
		GROUP_CONCAT(
		CASE
			WHEN l.embedded_in IS NULL AND l.lock_var_name IS NULL
				THEN CONCAT(l.id, '(', l.lock_type_name, '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- global (or embedded in unknown allocation *and* no name available)
			WHEN l.embedded_in IS NULL AND l.lock_var_name IS NOT NULL
				THEN CONCAT(l.lock_var_name, ':', l.id, '(', l.lock_type_name, '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- global (or embedded in unknown allocation *and* a name is available)
			WHEN l.embedded_in IS NOT NULL AND l.embedded_in = alloc_id
				THEN CONCAT('EMBSAME(', CONCAT(lock_a_dt.name, '.', IF(l.address - lock_a.base_address = lock_member.offset, mn_lock_member.name, CONCAT(mn_lock_member.name, '?'))), '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- embedded in same
			ELSE CONCAT('EMBOTHER', '(',  CONCAT(lock_a_dt.name, '.', IF(l.address - lock_a.base_address = lock_member.offset, mn_lock_member.name, CONCAT(mn_lock_member.name, '?'))), '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- embedded in other
--			ELSE CONCAT('EMB:', l.id, '(',  CONCAT(lock_a_dt.name, '.', IF(l.address - lock_a.base_address = lock_member.offset, mn_lock_member.name, CONCAT(mn_lock_member.name, '?'))), ')', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- embedded in other
			END
			ORDER BY lh.start
			SEPARATOR ' -> '
		) AS locks_held,
		stacktrace
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
			dt.name AS dt_name,
			GROUP_CONCAT(
				CONCAT(lower(hex(st.instruction_ptr)), '@', st.function, '@', st.file, ':', st.line)
				ORDER BY st.sequence
				SEPARATOR ','
			) AS stacktrace
		FROM accesses AS ac
		INNER JOIN allocations AS a
		  ON a.id = ac.alloc_id
		INNER JOIN data_types AS dt
		  ON dt.id = a.data_type_id
		INNER JOIN stacktraces AS st
		  ON ac.stacktrace_id = st.id
		LEFT JOIN structs_layout_flat sl
		  ON a.data_type_id = sl.data_type_id
		 AND ac.address - a.base_address = sl.helper_offset
		LEFT JOIN member_names AS mn
		  ON mn.id = sl.member_name_id
		WHERE 1
			-- Name the data type of interest here
			${DATATYPE_FILTER}
			${MEMBER_FILTER}
			${ACCESS_TYPE_FILTER}
			AND ac.id NOT IN
			(
				  SELECT ac.id
					FROM accesses ac
					JOIN allocations a
					  ON ac.alloc_id = a.id
					JOIN stacktraces AS st
					  ON ac.stacktrace_id = st.id
					LEFT JOIN structs_layout_flat sl
					  ON a.data_type_id = sl.data_type_id
					 AND ac.address - a.base_address = sl.helper_offset
					LEFT JOIN member_names mn
					  ON mn.id = sl.member_name_id
					LEFT JOIN function_blacklist fn_bl
					  ON fn_bl.fn = st.function
					 AND 
					 (
					   (fn_bl.data_type_id IS NULL  AND fn_bl.member_name_id IS NULL) -- globally blacklisted function
					   OR
					   (fn_bl.data_type_id = a.data_type_id AND fn_bl.member_name_id IS NULL) -- for this data type blacklisted
					   OR
					   (fn_bl.data_type_id = a.data_type_id AND fn_bl.member_name_id = sl.member_name_id) -- for this member blacklisted
					 )
					LEFT JOIN member_blacklist m_bl
					  ON m_bl.data_type_id = a.data_type_id
					 AND m_bl.member_name_id = sl.member_name_id
					WHERE 1
					${DATATYPE_FILTER}
					${MEMBER_FILTER}
					${ACCESS_TYPE_FILTER}
					-- === FOR NOW: skip task_struct ===
					AND a.data_type_id != (SELECT id FROM data_types WHERE name = 'task_struct')
					-- ====================================
					AND
					(
						fn_bl.fn IS NOT NULL OR m_bl.member_name_id IS NOT NULL
					)
					GROUP BY ac.id
			)
		GROUP BY ac.id -- Remove duplicate entries. Some accesses might be mapped to more than one member, e.g., an union.
	) s
	LEFT JOIN locks_held AS lh
	  ON lh.txn_id = ac_txn_id
	LEFT JOIN locks AS l
	  ON l.id = lh.lock_id
	LEFT JOIN allocations AS lock_a
	  ON lock_a.id = l.embedded_in
	LEFT JOIN data_types lock_a_dt
	  ON lock_a.data_type_id = lock_a_dt.id
	LEFT JOIN structs_layout_flat lock_member
	  ON lock_a.data_type_id = lock_member.data_type_id
	  AND l.address - lock_a.base_address = lock_member.helper_offset
	LEFT JOIN member_names mn_lock_member
	  ON mn_lock_member.id = lock_member.member_name_id
	GROUP BY ac_id
) t
-- Since we want a detailed view about where an access happenend, the result is additionally grouped by ac_fn and st_instrptr.
GROUP BY dt_name, sl_member, ac_type, stacktrace, locks_held
ORDER BY dt_name, sl_member, ac_type, stacktrace, locks_held, num DESC;
EOT
