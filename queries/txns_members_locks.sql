-- Count member-access combinations within distinct TXNs, allocations, and
-- locks-held sets (the latter also taking lock order into account).
--
-- This query builds on txns_members.sql, it may help to that one first to
-- understand this extension.
--
-- Hint: Read subquery comments from the innermost query "upwards".

SET SESSION group_concat_max_len = 8192;

SELECT type_name, members_accessed, locks_held,
	COUNT(*) AS occurrences
FROM

(
	-- add GROUP_CONCAT of all held locks (in locking order) to each list of accessed members
	SELECT type_id, type_name, members_accessed,
		GROUP_CONCAT(
			CASE
			WHEN l.embedded_in IS NULL THEN CONCAT(l.id, '(', l.type, ')')
			WHEN l.embedded_in IS NOT NULL AND l.embedded_in = concatgroups.alloc_id THEN CONCAT('EMB(', l.type, ')')
			ELSE CONCAT('EXT(', lock_a_dt.name, '.', l.type, ')')
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
			SELECT ac.alloc_id, ac.txn_id, MAX(ac.type) AS type, a.type AS type_id, sl.member, sl.offset, sl.size
			FROM accesses ac
			JOIN allocations a
			  ON ac.alloc_id = a.id
			LEFT JOIN structs_layout sl
			  ON a.type = sl.type_id
			 AND ac.address - a.ptr BETWEEN sl.offset AND sl.offset + sl.size - 1
			GROUP BY ac.alloc_id, ac.txn_id, a.type, sl.offset
		) AS fac -- = Folded ACcesses

		JOIN data_types dt
		  ON dt.id = fac.type_id
		GROUP BY dt.id, fac.alloc_id, fac.txn_id
	) AS concatgroups

	JOIN locks_held lh
	  ON lh.txn_id = concatgroups.txn_id
	JOIN locks l
	  ON l.id = lh.lock_id
	LEFT JOIN allocations lock_a
	  ON l.embedded_in = lock_a.id
	LEFT JOIN data_types lock_a_dt
	  ON lock_a.type = lock_a_dt.id

	GROUP BY concatgroups.type_id, concatgroups.alloc_id, concatgroups.txn_id
) AS withlocks

GROUP BY type_id, members_accessed, locks_held
ORDER BY type_id, occurrences
;
