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
	USE_STACK=${1}
fi

USE_STACK=${USE_STACK:-0}
shift
DATATYPE=${1}
shift
MEMBER=${1}
shift
USE_SUBCLASSES=${1:-0}

if [ ! -z ${DATATYPE} ] && [ "${DATATYPE}" != "any" ];
then
	DATATYPE_FILTER="AND ac.data_type_id = (SELECT id FROM data_types WHERE name = '${DATATYPE}')"
fi

if [ ! -z ${MEMBER} ] && [ "${MEMBER}" != "any" ];
then
	MEMBER_FILTER="AND ac.member_name_id = (SELECT id FROM member_names WHERE name = '${MEMBER}')"
fi

if [ ${USE_SUBCLASSES} -eq 1 ];
then
	TYPE_NAME_COLUMN="(CASE WHEN sc.name IS NULL THEN dt.name ELSE CONCAT(dt.name, ':', sc.name) END)"
	TYPE_ID_ALIAS="subclass_id_group"
	TYPE_ID_COLUMN="subclass_id"
	LOCKNAME_FORMAT="(CASE WHEN lock_sc.name IS NULL THEN lock_a_dt.name ELSE CONCAT(lock_a_dt.name, ':', lock_sc.name) END)"
else
	TYPE_NAME_COLUMN="dt.name"
	TYPE_ID_ALIAS="data_type_id_group"
	TYPE_ID_COLUMN="data_type_id"
	LOCKNAME_FORMAT="lock_a_dt.name"
fi

if [ ${USE_STACK} -eq 0 ];
then
cat <<EOT
-- Count member-access combinations within distinct TXNs, allocations, and
-- locks-held sets (the latter also taking lock order into account).
--
-- This query builds on txns_members.sql, it may help to read that one first to
-- understand this extension.
--
-- Hint: Read subquery comments from the innermost query "upwards".

SELECT
	type_name,
	members_accessed, locks_held,
	COUNT(*) AS occurrences
FROM

(
	-- add GROUP_CONCAT of all held locks (in locking order) to each list of accessed members
	SELECT concatgroups.${TYPE_ID_ALIAS}, concatgroups.type_name, concatgroups.members_accessed,
		string_agg(
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
				THEN CONCAT('EMBSAME(', CONCAT(${LOCKNAME_FORMAT}, '.',
					CASE WHEN l.address - lock_a.base_address = lock_member.byte_offset THEN 
						mn_lock_member.name
					ELSE 
						CONCAT(mn_lock_member.name, '?')
					END
					), '[', l.sub_lock, '])') -- embedded in same
			ELSE CONCAT('EMBOTHER', '(',  CONCAT(${LOCKNAME_FORMAT}, '.',
				CASE WHEN l.address - lock_a.base_address = lock_member.byte_offset THEN 
					mn_lock_member.name
				ELSE 
					CONCAT(mn_lock_member.name, '?')
				END
				), '[', l.sub_lock, '])') -- embedded in other
	--			ELSE CONCAT('EMB:', l.id, '(',  CONCAT(lock_a_dt.name, '.', IF(l.address - lock_a.base_address = lock_member.byte_offset, mn_lock_member.name, CONCAT(mn_lock_member.name, '?'))), '[', l.sub_lock, '])') -- embedded in other
			END
			 , ',' ORDER BY lh.start) AS locks_held
	FROM

	(
		-- GROUP_CONCAT all member accesses within a TXN and a specific allocation
		SELECT fac.${TYPE_ID_COLUMN} AS ${TYPE_ID_ALIAS}, ${TYPE_NAME_COLUMN} AS type_name, fac.alloc_id, fac.txn_id,
			string_agg(CONCAT(fac.type, ':', mn.name), ',' ORDER BY fac.byte_offset) AS members_accessed
		FROM

		(
			-- Within each TXN and allocation: fold multiple accesses to the same
			-- data-structure member into one; if there are reads and writes, the resulting
			-- access is a write, otherwise a read.
			-- NOTE: The above property does *NOT* apply if the the results are grouped by stacktrace_id.
			-- NOTE: This does not fold accesses to two different allocations.
			SELECT ac.alloc_id, ac.txn_id, MAX(ac.ac_type) AS type, ac.subclass_id AS subclass_id, ac.member_name_id, ac.byte_offset, ac.data_type_id
			FROM accesses_flat ac
			WHERE True
			${DATATYPE_FILTER}
			${MEMBER_FILTER}
			-- ====================================
			AND NOT EXISTS
			(
				SELECT
				FROM member_blacklist m_bl
				WHERE m_bl.subclass_id = ac.subclass_id
				 AND m_bl.member_name_id = ac.member_name_id
			)
			AND ac.txn_id IS NOT NULL
			AND NOT EXISTS
			(
				SELECT
				FROM  stacktraces AS s_st
				INNER JOIN function_blacklist s_fn_bl
				  ON s_fn_bl.fn = s_st.function
				WHERE ac.stacktrace_id = s_st.id
				AND
				(
					(
					      (s_fn_bl.subclass_id IS NULL  AND s_fn_bl.member_name_id IS NULL) -- globally blacklisted function
					      OR
					      (s_fn_bl.subclass_id = ac.subclass_id AND s_fn_bl.member_name_id IS NULL) -- for this data type blacklisted
					      OR
					      (s_fn_bl.subclass_id = ac.subclass_id AND s_fn_bl.member_name_id = ac.member_name_id) -- for this member blacklisted
					)
					AND
					(s_fn_bl.sequence IS NULL OR s_fn_bl.sequence = s_st.sequence) -- for functions that appear at a certain position within the trace
				)
			)
			-- The fields ac.alloc_id, ac.txn_id, and sl.byte_offset matter for the result.
			-- The remaining fields are just listed to silence the PostgreSQL engine.
			GROUP BY ac.alloc_id, ac.txn_id, ac.byte_offset, ac.data_type_id, ac.subclass_id, ac.member_name_id
		) AS fac -- = Folded ACcesses

		LEFT JOIN member_names mn
		  ON mn.id = fac.member_name_id
		JOIN subclasses sc
		  ON fac.subclass_id = sc.id
		JOIN data_types dt
		  ON dt.id = sc.data_type_id
		GROUP BY fac.alloc_id, fac.txn_id, fac.${TYPE_ID_COLUMN}, type_name

	) AS concatgroups

	JOIN locks_held lh
	  ON lh.txn_id = concatgroups.txn_id
	JOIN locks l
	  ON l.id = lh.lock_id

	-- find out more about each held lock (allocation -> structs_layout
	-- member or contained-in member in case of a complex member)
	LEFT JOIN allocations lock_a
	  ON l.embedded_in = lock_a.id
	LEFT JOIN subclasses lock_sc
	  ON lock_a.subclass_id = lock_sc.id
	LEFT JOIN data_types lock_a_dt
	  ON lock_sc.data_type_id = lock_a_dt.id
	LEFT JOIN structs_layout_flat lock_member
	  ON lock_sc.data_type_id = lock_member.data_type_id
	 AND l.address - lock_a.base_address = lock_member.helper_offset
	-- lock_a.id IS NULL                         => not embedded
	-- l.address - lock_a.base_address = lock_member.byte_offset   => the lock is exactly this member (or at the beginning of a complex sub-struct)
	-- else                                      => the lock is contained in this member, exact name unknown
	LEFT JOIN member_names mn_lock_member
	  ON mn_lock_member.id = lock_member.member_name_id
	GROUP BY concatgroups.alloc_id, concatgroups.txn_id, concatgroups.${TYPE_ID_ALIAS}, concatgroups.type_name, concatgroups.members_accessed

	UNION ALL

	-- Memory accesses to known allocations without any locks held: We
	-- cannot group these into TXNs, instead we assume them each in their
	-- own TXN for the purpose of this query.
	SELECT ac.${TYPE_ID_COLUMN} AS ${TYPE_ID_ALIAS}, ${TYPE_NAME_COLUMN} AS type_name, CONCAT(ac.ac_type, ':', mn.name) AS members_accessed, '' AS locks_held
	FROM accesses_flat ac
	JOIN subclasses sc
	  ON ac.subclass_id = sc.id
	JOIN data_types dt
	  ON dt.id = ac.data_type_id
	JOIN member_names mn
	  ON mn.id = ac.member_name_id
	WHERE True
	${DATATYPE_FILTER}
	${MEMBER_FILTER}
	-- ====================================
	AND NOT EXISTS
	(
		SELECT
		FROM member_blacklist m_bl
		WHERE m_bl.subclass_id = ac.subclass_id
		 AND m_bl.member_name_id = ac.member_name_id
	)
	AND ac.txn_id IS NULL
	AND NOT EXISTS
	(
		SELECT
		FROM  stacktraces AS s_st
		INNER JOIN function_blacklist s_fn_bl
		  ON s_fn_bl.fn = s_st.function
		WHERE ac.stacktrace_id = s_st.id
		AND 
		(
			(
			      (s_fn_bl.subclass_id IS NULL  AND s_fn_bl.member_name_id IS NULL) -- globally blacklisted function
			      OR
			      (s_fn_bl.subclass_id = ac.subclass_id AND s_fn_bl.member_name_id IS NULL) -- for this data type blacklisted
			      OR
			      (s_fn_bl.subclass_id = ac.subclass_id AND s_fn_bl.member_name_id = ac.member_name_id) -- for this member blacklisted
			)
			AND
			(s_fn_bl.sequence IS NULL OR s_fn_bl.sequence = s_st.sequence) -- for functions that appear at a certain position within the trace
		)
	)
) AS withlocks

GROUP BY ${TYPE_ID_ALIAS}, members_accessed, locks_held, type_name
ORDER BY ${TYPE_ID_ALIAS}, occurrences, members_accessed, locks_held
;
EOT
elif [ ${USE_STACK} -eq 1 ];
then
cat <<EOT
SELECT
	type_name,
	member, locks_held,
	COUNT(*) AS occurrences
FROM
(
	SELECT 
		type_name, ${TYPE_ID_ALIAS},
		member, locks_held
	FROM
	(
		SELECT fac.${TYPE_ID_ALIAS}, ${TYPE_NAME_COLUMN} AS type_name, fac.member, fac.stacktrace_id,
			string_agg(
				CASE
				WHEN l.embedded_in IS NULL AND l.lock_var_name IS NULL
					THEN CONCAT(l.id, '(', l.lock_type_name, '[', l.sub_lock, '])') -- global (or embedded in unknown allocation *and* no name available)
				WHEN l.embedded_in IS NULL AND l.lock_var_name IS NOT NULL
					THEN CONCAT(l.lock_var_name, ':', l.id, '(', l.lock_type_name, '[', l.sub_lock, '])') -- global (or embedded in unknown allocation *and* a name is available)
				WHEN l.embedded_in IS NOT NULL AND l.embedded_in = fac.alloc_id
					THEN CONCAT('EMBSAME(', CONCAT(${LOCKNAME_FORMAT}, '.',
						CASE WHEN l.address - lock_a.base_address = lock_member.byte_offset THEN 
							mn_lock_member.name
						ELSE 
							CONCAT(mn_lock_member.name, '?')
						END
						), '[', l.sub_lock, '])') -- embedded in same
				ELSE CONCAT('EMBOTHER', '(',  CONCAT(${LOCKNAME_FORMAT}, '.',
					CASE WHEN l.address - lock_a.base_address = lock_member.byte_offset THEN 
						mn_lock_member.name
					ELSE 
						CONCAT(mn_lock_member.name, '?')
					END
					), '[', l.sub_lock, '])') -- embedded in other
		--			ELSE CONCAT('EMB:', l.id, '(',  CONCAT(lock_a_dt.name, '.', IF(l.address - lock_a.base_address = lock_member.byte_offset, mn_lock_member.name, CONCAT(mn_lock_member.name, '?'))), '[', l.sub_lock, '])') -- embedded in other
				END
				, ',' ORDER BY lh.start) AS locks_held
		FROM
		(
			SELECT ac.ac_id, ac.alloc_id, ac.txn_id, ac.ac_type AS ac_type, ac.subclass_id AS subclass_id, ac.${TYPE_ID_COLUMN} AS ${TYPE_ID_ALIAS}, CONCAT(ac.ac_type, ':', mn.name) AS member, ac.byte_offset, ac.stacktrace_id
			FROM accesses_flat ac
			JOIN member_names mn
			  ON mn.id = ac.member_name_id
			WHERE True
			${DATATYPE_FILTER}
			${MEMBER_FILTER}
			-- ====================================
			AND NOT EXISTS
			(
				SELECT
				FROM member_blacklist m_bl
				WHERE m_bl.subclass_id = ac.subclass_id
				 AND m_bl.member_name_id = ac.member_name_id
			)
			AND ac.txn_id IS NOT NULL
			AND NOT EXISTS
			(
				SELECT
				FROM  stacktraces AS s_st
				INNER JOIN function_blacklist s_fn_bl
				  ON s_fn_bl.fn = s_st.function
				WHERE ac.stacktrace_id = s_st.id
				AND
				(
					(
					      (s_fn_bl.subclass_id IS NULL  AND s_fn_bl.member_name_id IS NULL) -- globally blacklisted function
					      OR
					      (s_fn_bl.subclass_id = ac.subclass_id AND s_fn_bl.member_name_id IS NULL) -- for this data type blacklisted
					      OR
					      (s_fn_bl.subclass_id = ac.subclass_id AND s_fn_bl.member_name_id = ac.member_name_id) -- for this member blacklisted
					)
					AND
					(s_fn_bl.sequence IS NULL OR s_fn_bl.sequence = s_st.sequence) -- for functions that appear at a certain position within the trace
				)
			)
			GROUP BY ac.ac_id, ac.alloc_id, ac.txn_id, ac.ac_type, ac.subclass_id, ac.${TYPE_ID_COLUMN}, member, ac.byte_offset, ac.stacktrace_id
		) AS fac -- = Folded ACcesses
		JOIN subclasses sc
		  ON fac.subclass_id = sc.id
		JOIN data_types dt
		  ON dt.id = sc.data_type_id
		JOIN locks_held lh
		  ON lh.txn_id = fac.txn_id
		JOIN locks l
		  ON l.id = lh.lock_id
		-- find out more about each held lock (allocation -> structs_layout
		-- member or contained-in member in case of a complex member)
		LEFT JOIN allocations lock_a
		  ON l.embedded_in = lock_a.id
		LEFT JOIN subclasses lock_sc
		  ON lock_a.subclass_id = lock_sc.id
		LEFT JOIN data_types lock_a_dt
		  ON lock_sc.data_type_id = lock_a_dt.id
		LEFT JOIN structs_layout_flat lock_member
		  ON lock_sc.data_type_id = lock_member.data_type_id
		 AND l.address - lock_a.base_address = lock_member.helper_offset
		-- lock_a.id IS NULL                         => not embedded
		-- l.address - lock_a.base_address = lock_member.offset   => the lock is exactly this member (or at the beginning of a complex sub-struct)
		-- else                                      => the lock is contained in this member, exact name unknown
		LEFT JOIN member_names mn_lock_member
		  ON mn_lock_member.id = lock_member.member_name_id
		GROUP BY fac.ac_id, fac.${TYPE_ID_ALIAS}, type_name, fac.member, fac.stacktrace_id

		UNION ALL

		SELECT ac.${TYPE_ID_COLUMN} AS ${TYPE_ID_ALIAS}, ${TYPE_NAME_COLUMN} AS type_name, CONCAT(ac.ac_type, ':', mn.name) AS members, ac.stacktrace_id, '' AS locks_held
		FROM accesses_flat ac
		JOIN subclasses sc
		  ON ac.subclass_id = sc.id
		JOIN data_types dt
		  ON dt.id = ac.data_type_id
		JOIN member_names mn
		  ON mn.id = ac.member_name_id
		WHERE True
		${DATATYPE_FILTER}
		${MEMBER_FILTER}
		-- ====================================
		AND NOT EXISTS
		(
			SELECT
			FROM member_blacklist m_bl
			WHERE m_bl.subclass_id = ac.subclass_id
			 AND m_bl.member_name_id = ac.member_name_id
		)
		AND ac.txn_id IS NULL
		AND NOT EXISTS
		(
			SELECT
			FROM  stacktraces AS s_st
			INNER JOIN function_blacklist s_fn_bl
			  ON s_fn_bl.fn = s_st.function
			WHERE ac.stacktrace_id = s_st.id
			AND
			(
				(
				      (s_fn_bl.subclass_id IS NULL  AND s_fn_bl.member_name_id IS NULL) -- globally blacklisted function
				      OR
				      (s_fn_bl.subclass_id = ac.subclass_id AND s_fn_bl.member_name_id IS NULL) -- for this data type blacklisted
				      OR
				      (s_fn_bl.subclass_id = ac.subclass_id AND s_fn_bl.member_name_id = ac.member_name_id) -- for this member blacklisted
				)
				AND
				(s_fn_bl.sequence IS NULL OR s_fn_bl.sequence = s_st.sequence) -- for functions that appear at a certain position within the trace
			)
		)
		GROUP BY ac.ac_id, ac.${TYPE_ID_COLUMN}, type_name, members, ac.stacktrace_id
	) AS concatlocks
	GROUP BY concatlocks.${TYPE_ID_ALIAS}, concatlocks.member, concatlocks.locks_held, concatlocks.stacktrace_id, concatlocks.type_name
) AS fstacks
GROUP BY fstacks.${TYPE_ID_ALIAS}, fstacks.member, fstacks.locks_held, fstacks.type_name
ORDER BY fstacks.${TYPE_ID_ALIAS}, fstacks.member, fstacks.locks_held, occurrences
;
EOT
else
	echo "Unknown mode!"
fi
