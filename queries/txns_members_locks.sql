-- Count member-access combinations within distinct TXNs, allocations, and
-- locks-held sets (the latter also taking lock order into account).
--
-- This query builds on txns_members.sql, it may help to that one first to
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
--			WHEN l.embedded_in IS NULL THEN CONCAT(l.id, '(', l.type, ')') -- global (or embedded in unknown allocation)
--			WHEN l.embedded_in IS NOT NULL AND l.embedded_in = concatgroups.alloc_id THEN CONCAT('EMBSAME(', l.type, ')') -- embedded in same
----			ELSE CONCAT('EXT(', lock_a_dt.name, '.', l.type, ')') -- embedded in other
--			ELSE CONCAT('EMB:', l.id, '(', l.type, ')') -- embedded in other
			WHEN l.embedded_in IS NULL THEN CONCAT(l.id, '(', l.type, ')') -- global (or embedded in unknown allocation)
			WHEN l.embedded_in IS NOT NULL AND l.embedded_in = concatgroups.alloc_id
				THEN CONCAT('EMBSAME(', IF(l.ptr - lock_a.ptr = lock_member.offset, mn_lock_member.name, CONCAT(mn_lock_member.name, '?')), ')') -- embedded in same
			ELSE CONCAT('EMB:', l.id, '(',  IF(l.ptr - lock_a.ptr = lock_member.offset, mn_lock_member.name, CONCAT(mn_lock_member.name, '?')), ')') -- embedded in other
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
			-- NOTE: This does not fold accesses to two different allocations.
			SELECT ac.alloc_id, ac.txn_id, MAX(ac.type) AS type, a.type AS type_id, mn.name AS member, sl.offset, sl.size
			FROM accesses ac
			JOIN allocations a
			  ON ac.alloc_id = a.id
			LEFT JOIN structs_layout_flat sl
			  ON a.type = sl.type_id
			 AND ac.address - a.ptr = sl.helper_offset
			LEFT JOIN member_names mn
			  ON mn.id = sl.member_id
			LEFT JOIN function_blacklist fn_bl
			  ON fn_bl.datatype_id = a.type
			 AND fn_bl.fn = ac.fn
			 AND (fn_bl.datatype_member_id IS NULL OR fn_bl.datatype_member_id = sl.member_id)
			LEFT JOIN member_blacklist m_bl
			  ON m_bl.datatype_id = a.type
			 AND m_bl.datatype_member_id = sl.member_id
			WHERE 1
			-- === FOR NOW: only look at super_blocks ===
			 AND a.type = (SELECT id FROM data_types WHERE name = 'inode')
			-- ====================================
			AND fn_bl.fn IS NULL
			AND m_bl.datatype_member_id IS NULL
			AND ac.txn_id IS NOT NULL
			GROUP BY ac.alloc_id, ac.txn_id, a.type, sl.offset
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
--	LEFT JOIN data_types lock_a_dt
--	  ON lock_a.type = lock_a_dt.id
	LEFT JOIN structs_layout_flat lock_member
	  ON lock_a.type = lock_member.type_id
	 AND l.ptr - lock_a.ptr = lock_member.helper_offset
	-- lock_a.id IS NULL                         => not embedded
	-- l.ptr - lock_a.ptr = lock_member.offset   => the lock is exactly this member (or at the beginning of a complex sub-struct)
	-- else                                      => the lock is contained in this member, exact name unknown
	LEFT JOIN member_names mn_lock_member
	  ON mn_lock_member.id = lock_member.member_id

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
	  ON dt.id = a.type
	LEFT JOIN structs_layout_flat sl
	  ON a.type = sl.type_id
	 AND ac.address - a.ptr = sl.helper_offset
	LEFT JOIN member_names mn
	  ON mn.id = sl.member_id
	LEFT JOIN function_blacklist fn_bl
	  ON fn_bl.datatype_id = a.type
	 AND fn_bl.fn = ac.fn
	 AND (fn_bl.datatype_member_id IS NULL OR fn_bl.datatype_member_id = sl.member_id)
	LEFT JOIN member_blacklist m_bl
	  ON m_bl.datatype_id = a.type
	 AND m_bl.datatype_member_id = sl.member_id
	WHERE 1
	-- === FOR NOW: only look at super_blocks ===
	 AND a.type = (SELECT id FROM data_types WHERE name = 'inode')
	-- ====================================
	AND fn_bl.fn IS NULL
	AND m_bl.datatype_member_id IS NULL
	AND ac.txn_id IS NULL
) AS withlocks

GROUP BY type_id, members_accessed, locks_held
ORDER BY type_id, occurrences, members_accessed, locks_held
;
