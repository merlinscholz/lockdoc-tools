-- Count member-access combinations within distinct TXNs and allocations.
--
-- Hint: Read subquery comments from the innermost query "upwards".

SET SESSION group_concat_max_len = 8192;

SELECT type_name, members_accessed, COUNT(*) AS occurrences
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

GROUP BY type_id, members_accessed
ORDER BY type_name, occurrences
;
