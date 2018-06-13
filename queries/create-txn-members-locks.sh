#!/bin/bash 
# A.Lochmann 2018
# Queries written by H. Schirmeier 2017
# 
# This script generates a query that will fetch the input for the hypothesizer from the database.
# The query used to be in queries/txn-members-locks.sql. That file is now outdated. It's been left for
# debugging purposes.
# With no input given (or 'nostack'), this script will generate the same query as in queries/txn-members-locks.sql.
# Given the parameter 'stack', the resulting query will consider the instruction pointer and the stacktrace id 
# - in addition to the existing keys - to identify unique accesses.

if [ ! -z ${1} ];
then
	MODE=${1}
fi

MODE=${MODE:-nostack}
shift
DATATYPE=${1}
shift
MEMBER=${1}

if [ ! -z ${DATATYPE} ];
then
	DATATYPE_FILTER="AND a.data_type_id = (SELECT id FROM data_types WHERE name = '${DATATYPE}')"
fi

if [ ! -z ${MEMBER} ];
then
	MEMBER_FILTER="AND mn.id = (SELECT id FROM member_names WHERE name = '${MEMBER}')"
fi

if [ ${MODE} == "nostack" ];
then
cat <<EOT
-- Count member-access combinations within distinct TXNs, allocations, and
-- locks-held sets (the latter also taking lock order into account).
--
-- This query builds on txns_members.sql, it may help to read that one first to
-- understand this extension.
--
-- Hint: Read subquery comments from the innermost query "upwards".

SET SESSION group_concat_max_len = 8192;

SELECT
	type_name,
	members_accessed, locks_held,
	COUNT(*) AS occurrences
FROM

(
	-- add GROUP_CONCAT of all held locks (in locking order) to each list of accessed members
	SELECT concatgroups.type_id, concatgroups.type_name, concatgroups.members_accessed,
		GROUP_CONCAT(
			CASE
--			WHEN l.embedded_in IS NULL THEN CONCAT(l.id, '(', l.data_type_name, '[', l.sub_lock, '])') -- global (or embedded in unknown allocation)
--			WHEN l.embedded_in IS NOT NULL AND l.embedded_in = concatgroups.alloc_id THEN CONCAT('EMBSAME(', l.data_type_name, '[', l.sub_lock, '])') -- embedded in same
----			ELSE CONCAT('EXT(', lock_a_dt.name, '.', l.data_type_name, '[', l.sub_lock, '])') -- embedded in other
--			ELSE CONCAT('EMB:', l.id, '(', l.lock_type_name, '[', l.sub_lock, '])') -- embedded in other
			WHEN l.embedded_in IS NULL AND l.lock_var_name IS NULL
				THEN CONCAT(l.id, '(', l.lock_type_name, '[', l.sub_lock, '])') -- global (or embedded in unknown allocation *and* no name available)
			WHEN l.embedded_in IS NULL AND l.lock_var_name IS NOT NULL
				THEN CONCAT(l.lock_var_name, ':', l.id, '(', l.lock_type_name, '[', l.sub_lock, '])') -- global (or embedded in unknown allocation *and* a name is available)
			WHEN l.embedded_in IS NOT NULL AND l.embedded_in = concatgroups.alloc_id
				THEN CONCAT('EMBSAME(', CONCAT(lock_a_dt.name, ':', IF(l.address - lock_a.base_address = lock_member.offset, mn_lock_member.name, CONCAT(mn_lock_member.name, '?'))), '[', l.sub_lock, '])') -- embedded in same
			ELSE CONCAT('EMBOTHER', '(',  CONCAT(lock_a_dt.name, ':', IF(l.address - lock_a.base_address = lock_member.offset, mn_lock_member.name, CONCAT(mn_lock_member.name, '?'))), '[', l.sub_lock, '])') -- embedded in other
--			ELSE CONCAT('EMB:', l.id, '(',  CONCAT(lock_a_dt.name, ':', IF(l.address - lock_a.base_address = lock_member.offset, mn_lock_member.name, CONCAT(mn_lock_member.name, '?'))), '[', l.sub_lock, '])') -- embedded in other
			END
			ORDER BY lh.start
		) AS locks_held
	FROM

	(
		-- GROUP_CONCAT all member accesses within a TXN and a specific allocation
		SELECT dt.id AS type_id, dt.name AS type_name, fac.alloc_id, fac.txn_id,
			GROUP_CONCAT(CONCAT(fac.type, ':', fac.member) ORDER BY fac.offset) AS members_accessed
		FROM

		(
			-- Within each TXN and allocation: fold multiple accesses to the same
			-- data-structure member into one; if there are reads and writes, the resulting
			-- access is a write, otherwise a read.
			-- NOTE: The above property does *NOT* apply if the the results are grouped by stacktrace_id.
			-- NOTE: This does not fold accesses to two different allocations.
			SELECT ac.alloc_id, ac.txn_id, MAX(ac.type) AS type, a.data_type_id AS type_id, mn.name AS member, sl.offset, sl.size
			FROM accesses ac
			JOIN allocations a
			  ON ac.alloc_id = a.id
			LEFT JOIN structs_layout_flat sl
			  ON a.data_type_id = sl.data_type_id
			 AND ac.address - a.base_address = sl.helper_offset
			LEFT JOIN member_names mn
			  ON mn.id = sl.member_name_id
			WHERE 1
			${DATATYPE_FILTER}
			${MEMBER_FILTER}
			-- === FOR NOW: skip task_struct ===
			AND a.data_type_id != (SELECT id FROM data_types WHERE name = 'task_struct')
			-- ====================================
			AND ac.txn_id IS NOT NULL
			AND ac.id NOT IN
			(
			-- Get all accesses that happened on an init path or accessed a blacklisted member
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
				-- === FOR NOW: skip task_struct ===
				AND a.data_type_id != (SELECT id FROM data_types WHERE name = 'task_struct')
				-- ====================================
				AND
				(
					fn_bl.fn IS NOT NULL OR m_bl.member_name_id IS NOT NULL
				)
				GROUP BY ac.id
			)
			GROUP BY ac.alloc_id, ac.txn_id, a.data_type_id, sl.offset
		) AS fac -- = Folded ACcesses

		JOIN data_types dt
		  ON dt.id = fac.type_id
		GROUP BY fac.alloc_id, fac.txn_id, dt.id
	) AS concatgroups

	JOIN locks_held lh
	  ON lh.txn_id = concatgroups.txn_id
	JOIN locks l
	  ON l.id = lh.lock_id

	-- find out more about each held lock (allocation -> structs_layout
	-- member or contained-in member in case of a complex member)
	LEFT JOIN allocations lock_a
	  ON l.embedded_in = lock_a.id
	LEFT JOIN data_types lock_a_dt
	  ON lock_a.data_type_id = lock_a_dt.id
	LEFT JOIN structs_layout_flat lock_member
	  ON lock_a.data_type_id = lock_member.data_type_id
	 AND l.address - lock_a.base_address = lock_member.helper_offset
	-- lock_a.id IS NULL                         => not embedded
	-- l.address - lock_a.base_address = lock_member.offset   => the lock is exactly this member (or at the beginning of a complex sub-struct)
	-- else                                      => the lock is contained in this member, exact name unknown
	LEFT JOIN member_names mn_lock_member
	  ON mn_lock_member.id = lock_member.member_name_id

	GROUP BY concatgroups.alloc_id, concatgroups.txn_id, concatgroups.type_id

	UNION ALL

	-- Memory accesses to known allocations without any locks held: We
	-- cannot group these into TXNs, instead we assume them each in their
	-- own TXN for the purpose of this query.
	SELECT dt.id AS type_id, dt.name AS type_name, CONCAT(ac.type, ':', mn.name) AS members_accessed, '' AS locks_held
	FROM accesses ac
	JOIN allocations a
	  ON ac.alloc_id = a.id
	JOIN data_types dt
	  ON dt.id = a.data_type_id
	LEFT JOIN structs_layout_flat sl
	  ON a.data_type_id = sl.data_type_id
	 AND ac.address - a.base_address = sl.helper_offset
	LEFT JOIN member_names mn
	  ON mn.id = sl.member_name_id
	WHERE 1
	${DATATYPE_FILTER}
	${MEMBER_FILTER}
	-- === FOR NOW: skip task_struct ===
	AND a.data_type_id != (SELECT id FROM data_types WHERE name = 'task_struct')
	-- ====================================
	AND ac.txn_id IS NULL
	AND ac.id NOT IN
	(
	-- Get all accesses that happened on an init path or accessed a blacklisted member
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
		-- === FOR NOW: skip task_struct ===
		AND a.data_type_id != (SELECT id FROM data_types WHERE name = 'task_struct')
		-- ====================================
		AND
		(
			fn_bl.fn IS NOT NULL OR m_bl.member_name_id IS NOT NULL
		)
		GROUP BY ac.id
	)
) AS withlocks

GROUP BY type_id, members_accessed, locks_held
ORDER BY type_id, occurrences, members_accessed, locks_held
;
EOT
elif [ ${MODE} == "stack" ];
then
cat <<EOT
SELECT 
	type_name,
	member, locks_held,
	COUNT(*) AS occurrences
FROM
(
	SELECT 
		type_name, type_id,
		member, locks_held
	FROM
	(
		SELECT fac.type_id, dt.name AS type_name, fac.member, fac.stacktrace_id,
			GROUP_CONCAT(
				CASE
				WHEN l.embedded_in IS NULL AND l.lock_var_name IS NULL
					THEN CONCAT(l.id, '(', l.lock_type_name, '[', l.sub_lock, '])') -- global (or embedded in unknown allocation *and* no name available)
				WHEN l.embedded_in IS NULL AND l.lock_var_name IS NOT NULL
					THEN CONCAT(l.lock_var_name, ':', l.id, '(', l.lock_type_name, '[', l.sub_lock, '])') -- global (or embedded in unknown allocation *and* a name is available)
				WHEN l.embedded_in IS NOT NULL AND l.embedded_in = fac.alloc_id
					THEN CONCAT('EMBSAME(', CONCAT(lock_a_dt.name, ':', IF(l.address - lock_a.base_address = lock_member.offset, mn_lock_member.name, CONCAT(mn_lock_member.name, '?'))), '[', l.sub_lock, '])') -- embedded in same
				ELSE CONCAT('EMBOTHER', '(',  CONCAT(lock_a_dt.name, ':', IF(l.address - lock_a.base_address = lock_member.offset, mn_lock_member.name, CONCAT(mn_lock_member.name, '?'))), '[', l.sub_lock, '])') -- embedded in other
--				ELSE CONCAT('EMB:', l.id, '(',  CONCAT(lock_a_dt.name, ':', IF(l.address - lock_a.base_address = lock_member.offset, mn_lock_member.name, CONCAT(mn_lock_member.name, '?'))), '[', l.sub_lock, '])') -- embedded in other
				END
				ORDER BY lh.start
			) AS locks_held
		FROM
		(
			SELECT ac.id, ac.alloc_id, ac.txn_id, ac.type AS ac_type, a.data_type_id AS type_id, CONCAT(ac.type, ':', mn.name) AS member, sl.offset, ac.stacktrace_id
			FROM accesses ac
			JOIN allocations a
			  ON ac.alloc_id = a.id
			LEFT JOIN structs_layout_flat sl
			  ON a.data_type_id = sl.data_type_id
			 AND ac.address - a.base_address = sl.helper_offset
			LEFT JOIN member_names mn
			  ON mn.id = sl.member_name_id
			WHERE 1
			${DATATYPE_FILTER}
			${MEMBER_FILTER}
			-- === FOR NOW: skip task_struct ===
			AND a.data_type_id != (SELECT id FROM data_types WHERE name = 'task_struct')
			-- ====================================
			AND ac.txn_id IS NOT NULL
			AND ac.id NOT IN
			(
			-- Get all accesses that happened on an init path or accessed a blacklisted member
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
		) AS fac
		JOIN data_types dt
			ON dt.id = fac.type_id
		JOIN locks_held lh
		  ON lh.txn_id = fac.txn_id
		JOIN locks l
		  ON l.id = lh.lock_id
		-- find out more about each held lock (allocation -> structs_layout
		-- member or contained-in member in case of a complex member)
		LEFT JOIN allocations lock_a
		  ON l.embedded_in = lock_a.id
		LEFT JOIN data_types lock_a_dt
		  ON lock_a.data_type_id = lock_a_dt.id
		LEFT JOIN structs_layout_flat lock_member
		  ON lock_a.data_type_id = lock_member.data_type_id
		 AND l.address - lock_a.base_address = lock_member.helper_offset
		-- lock_a.id IS NULL                         => not embedded
		-- l.address - lock_a.base_address = lock_member.offset   => the lock is exactly this member (or at the beginning of a complex sub-struct)
		-- else                                      => the lock is contained in this member, exact name unknown
		LEFT JOIN member_names mn_lock_member
		  ON mn_lock_member.id = lock_member.member_name_id
		GROUP BY fac.id
		
		UNION ALL
		
		SELECT a.data_type_id AS type_id, dt.name AS type_name, CONCAT(ac.type, ':', mn.name) AS member, ac.stacktrace_id, '' AS locks_held
		FROM accesses ac
		JOIN allocations a
		  ON ac.alloc_id = a.id
		LEFT JOIN structs_layout_flat sl
		  ON a.data_type_id = sl.data_type_id
		 AND ac.address - a.base_address = sl.helper_offset
		LEFT JOIN member_names mn
		  ON mn.id = sl.member_name_id
		JOIN data_types dt
			ON dt.id = a.data_type_id
		WHERE 1
		${DATATYPE_FILTER}
		${MEMBER_FILTER}
		-- === FOR NOW: skip task_struct ===
		AND a.data_type_id != (SELECT id FROM data_types WHERE name = 'task_struct')
		-- ====================================
		AND ac.txn_id IS NULL
		AND ac.id NOT IN
		(
			-- Get all accesses that happened on an init path or accessed a blacklisted member
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
			-- === FOR NOW: skip task_struct ===
			AND a.data_type_id != (SELECT id FROM data_types WHERE name = 'task_struct')
			-- ====================================
			AND
			(
				fn_bl.fn IS NOT NULL OR m_bl.member_name_id IS NOT NULL
			)
			GROUP BY ac.id
		)
		GROUP BY ac.id
	) AS concatlocks
	GROUP BY concatlocks.type_id, concatlocks.member, concatlocks.locks_held, concatlocks.stacktrace_id
) AS fstacks
GROUP BY fstacks.type_id, fstacks.member, fstacks.locks_held
ORDER BY fstacks.type_id, fstacks.member, fstacks.locks_held, occurrences
;
EOT
else
	echo "Unknown mode!"
fi
