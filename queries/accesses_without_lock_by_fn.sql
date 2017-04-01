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
		mn.name AS sl_member,
		sl.member_id AS sl_member_id
	FROM accesses AS ac
	INNER JOIN allocations AS a ON a.id=ac.alloc_id
	INNER JOIN data_types AS dt ON dt.id=a.type
	LEFT JOIN structs_layout_flat sl
	  ON a.type = sl.type_id
	 AND ac.address - a.ptr = sl.helper_offset
	LEFT JOIN member_names AS mn
	  ON mn.id = sl.member_id
	LEFT JOIN function_blacklist fn_bl
	  ON fn_bl.datatype_id = a.type
	 AND fn_bl.fn = ac.fn
	 AND (fn_bl.datatype_member_id IS NULL OR fn_bl.datatype_member_id = sl.member_id)
	LEFT JOIN member_blacklist m_bl
	  ON m_bl.datatype_id = a.type
	 AND m_bl.datatype_member_id = sl.member_id
	WHERE
		a.type IN (SELECT id FROM data_types WHERE name IN ('journal_t','transaction_t')) AND
		fn_bl.fn IS NULL AND
		m_bl.datatype_member_id IS NULL
	GROUP BY ac.id
) s
LEFT JOIN locks_held AS lh ON lh.txn_id=ac_txn_id
LEFT JOIN locks AS l ON l.id=lh.lock_id
LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
LEFT JOIN structs_layout_flat sl2
	  ON a2.type = sl2.type_id
	 AND l.ptr - a2.ptr = sl2.helper_offset
LEFT JOIN member_names AS mn2 ON mn2.id = sl2.member_id
 WHERE
	lh.start IS NULL
GROUP BY ac_type, sl_member_id, ac_fn
ORDER BY ac_type, sl_member_id, ac_fn, num DESC;
