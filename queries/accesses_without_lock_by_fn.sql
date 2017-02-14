SELECT
	ac_type,
	ac_fn,
	sl_member,
	COUNT(*) AS num
FROM
(
	SELECT
		ac.id AS ac_id,
		ac.txn_id AS ac_txn_id,
		ac.type AS ac_type,
		ac.fn AS ac_fn,
		sl.member AS sl_member
	FROM accesses AS ac
	INNER JOIN allocations AS a ON a.id=ac.alloc_id
	INNER JOIN data_types AS dt ON dt.id=a.type
	LEFT JOIN structs_layout AS sl ON sl.type_id=a.type AND (ac.address - a.ptr) >= sl.offset AND (ac.address - a.ptr) < sl.offset+sl.size
	LEFT JOIN blacklist bl ON bl.datatype_id = a.type AND bl.fn = ac.fn AND (bl.datatype_member IS NULL OR bl.datatype_member = sl.member)
	WHERE 
		a.type = (SELECT id FROM data_types WHERE name = 'inode') AND
		ac.type  IN ('r','w') AND
		bl.fn IS NULL
	GROUP BY ac.id
) s
LEFT JOIN locks_held AS lh ON lh.txn_id=ac_txn_id
LEFT JOIN locks AS l ON l.id=lh.lock_id
LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
LEFT JOIN structs_layout AS sl2 ON sl2.type_id=a2.type AND (l.ptr - a2.ptr) >= sl2.offset AND (l.ptr - a2.ptr) < sl2.offset+sl2.size
 WHERE
	lh.start IS NULL
GROUP BY ac_type, sl_member, ac_fn
ORDER BY ac_type, sl_member, ac_fn, num DESC;
