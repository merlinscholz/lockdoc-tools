SELECT
	ac_type,
	sl_member,
	IF(l.type IS NULL, 'null',
	  IF(sl2.member IS NULL,CONCAT('global_',l.type,'_',l.id),CONCAT(sl2.member,'_',IF(l.embedded_in = alloc_id,'1','0')))) AS lock_member,
	ac_fn,
	lower(hex(ac_instrptr)) AS ac_instrptr,
	CASE 
		WHEN lh.lastPreemptCount & 0x0ff00 THEN 'softirq'
		WHEN lh.lastPreemptCount & 0xf0000 THEN 'hardirq'
		WHEN (lh.lastPreemptCount & 0xfff00) = 0 THEN 'noirq'
		ELSE 'unknown'
	END AS context,
	COUNT(*) AS num
FROM
(
	-- Get all accesses. Add information about the accessed member, data type, and the function the memory has been accessed from.
	-- Filter out every function that is on our blacklist.
	SELECT
		ac.id AS ac_id,
		ac.txn_id AS ac_txn_id,
		ac.alloc_id AS alloc_id,
		ac.type AS ac_type,
		ac.fn AS ac_fn,
		ac.address AS ac_address,
		a.ptr AS a_ptr,
		ac.instrptr AS ac_instrptr,
		sl.member AS sl_member,
		dt.name AS dt_name
	FROM accesses AS ac
	INNER JOIN allocations AS a ON a.id=ac.alloc_id
	INNER JOIN data_types AS dt ON dt.id=a.type
	LEFT JOIN structs_layout AS sl ON sl.type_id=a.type AND (ac.address - a.ptr) >= sl.offset AND (ac.address - a.ptr) < sl.offset+sl.size
	LEFT JOIN blacklist bl ON bl.datatype_id = a.type AND bl.fn = ac.fn AND (bl.datatype_member IS NULL OR bl.datatype_member = sl.member)
	WHERE 
		a.type = 2 AND  -- Put the id of your data type of interest here
--		ac.type  IN ('r','w') AND -- Filter by access type
--		sl.member IN ('i_hash') AND -- Only show results for a certain member
		bl.fn IS NULL
	GROUP BY ac.id -- Remove duplicate entries. Some accesses might be mapped to more than one member, e.g., an union.
) s
LEFT JOIN locks_held AS lh ON lh.txn_id=ac_txn_id
LEFT JOIN locks AS l ON l.id=lh.lock_id
LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
LEFT JOIN structs_layout AS sl2 ON sl2.type_id=a2.type AND (l.ptr - a2.ptr) >= sl2.offset AND (l.ptr - a2.ptr) < sl2.offset+sl2.size
-- Since we want a detailed view about where an access happenend, the result is additionally grouped by ac_fn and ac_instrptr.
GROUP BY ac_type, sl_member, lock_member, ac_fn, ac_instrptr
ORDER BY ac_type, sl_member, lock_member, ac_fn, ac_instrptr, num DESC;
