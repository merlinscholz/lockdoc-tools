SELECT
		ac_type,
		sl_member,
		context,
		lock_member,
		COUNT(*) AS num
FROM
(
	SELECT DISTINCT
		ac_id,
		ac_type,
		sl_member,
		CASE 
			WHEN lh.lastPreemptCount & 0x0ff00 THEN 'softirq'
			WHEN lh.lastPreemptCount & 0xf0000 THEN 'hardirq'
			WHEN (lh.lastPreemptCount & 0xfff00) = 0 THEN 'noirq'
			ELSE 'unknown'
		END AS context,
		IF(l.type IS NULL, 'null',
		  IF(sl2.member IS NULL,CONCAT('global_',l.type,'_',l.id),CONCAT(sl2.member,'_',IF(l.embedded_in = alloc_id,'1','0')))) AS lock_member
	FROM
	(
		SELECT
			ac.id AS ac_id,
			ac.txn_id AS ac_txn_id,
			ac.alloc_id AS alloc_id,
			ac.type AS ac_type,
			ac.fn AS ac_fn,
			ac.address AS ac_address,
			a.ptr AS a_ptr,
			sl.member AS sl_member,
			dt.name AS dt_name
		FROM accesses AS ac
		INNER JOIN allocations AS a ON a.id=ac.alloc_id
		INNER JOIN data_types AS dt ON dt.id=a.type
		LEFT JOIN structs_layout AS sl ON sl.type_id=a.type AND (ac.address - a.ptr) >= sl.offset AND (ac.address - a.ptr) < sl.offset+sl.size
		LEFT JOIN blacklist bl ON bl.datatype_id = a.type AND bl.fn = ac.fn AND (bl.datatype_member IS NULL OR bl.datatype_member = sl.member)
		WHERE 
			a.type = (SELECT id FROM data_types WHERE name = 'inode') AND
			sl.member = 'i_acl' AND
			ac.type  IN ('r','w') AND
			ac.fn = 'get_cached_acl' AND
			bl.fn IS NULL
		GROUP BY ac.id
	) s
	LEFT JOIN locks_held AS lh ON lh.txn_id=ac_txn_id
	LEFT JOIN locks AS l ON l.id=lh.lock_id
	LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
	LEFT JOIN structs_layout AS sl2 ON sl2.type_id=a2.type AND (l.ptr - a2.ptr) >= sl2.offset AND (l.ptr - a2.ptr) < sl2.offset+sl2.size
) t
GROUP BY ac_type, lock_member, sl_member, context
ORDER BY ac_type, lock_member, sl_member, context, num DESC
