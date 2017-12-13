SELECT
	ac_type,
	sl_member,
	IF(l.type IS NULL, 'null',
	  IF(mn2.name IS NULL,CONCAT('global_',l.type,'_',l.id),CONCAT(mn2.name,'_',IF(l.embedded_in = alloc_id,'1','0')))) AS lock_member,
	ac_fn,
	lower(hex(ac_instrptr)) AS ac_instrptr,
	CASE 
		WHEN lh.lastPreemptCount & 0x0ff00 THEN 'softirq'
		WHEN lh.lastPreemptCount & 0xf0000 THEN 'hardirq'
		WHEN (lh.lastPreemptCount & 0xfff00) = 0 THEN 'noirq'
		WHEN lh.lastPreemptCount IS NULL THEN 'nolock'
		ELSE 'unknown'
	END AS context, lh.lastPreemptCount AS preemptCount,
	COUNT(*) AS num
FROM
(
	-- Get all accesses. Add information about the accessed member, data type, and the function the memory has been accessed from.
	-- Filter out every function that is on our function blacklist.
	SELECT
		ac.id AS ac_id,
		ac.txn_id AS ac_txn_id,
		ac.alloc_id AS alloc_id,
		ac.type AS ac_type,
		ac.fn AS ac_fn,
		ac.address AS ac_address,
		a.ptr AS a_ptr,
		ac.instrptr AS ac_instrptr,
		mn.name AS sl_member,
		dt.name AS dt_name
	FROM accesses AS ac
	INNER JOIN allocations AS a ON a.id=ac.alloc_id
	INNER JOIN data_types AS dt ON dt.id=a.type
	LEFT JOIN structs_layout_flat sl
	  ON a.type = sl.type_id
	 AND ac.address - a.ptr = sl.helper_offset
	LEFT JOIN member_names AS mn ON mn.id = sl.member_id
	LEFT JOIN function_blacklist fn_bl
	  ON fn_bl.datatype_id = a.type
	 AND fn_bl.fn = ac.fn
	 AND (fn_bl.datatype_member_id IS NULL OR fn_bl.datatype_member_id = sl.member_id)
	WHERE 
		-- Name the data type of interest here
		a.type = (SELECT id FROM data_types WHERE name = 'transaction_t') AND
		ac.type  IN ('w') AND -- Filter by access type
--		sl.member_id IN (SELECT id FROM member_names WHERE name in ('t_tid')) AND -- Only show results for a certain member
		fn_bl.fn IS NULL
	GROUP BY ac.id -- Remove duplicate entries. Some accesses might be mapped to more than one member, e.g., an union.
) s
LEFT JOIN locks_held AS lh ON lh.txn_id=ac_txn_id
LEFT JOIN locks AS l ON l.id=lh.lock_id
LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
LEFT JOIN structs_layout AS sl2 ON sl2.type_id=a2.type AND (l.ptr - a2.ptr) >= sl2.offset AND (l.ptr - a2.ptr) < sl2.offset+sl2.size
LEFT JOIN member_names AS mn2 ON mn2.id = sl2.member_id
-- Since we want a detailed view about where an access happenend, the result is additionally grouped by ac_fn and ac_instrptr.
GROUP BY ac_type, sl_member, lock_member, ac_fn, ac_instrptr
ORDER BY ac_type, sl_member, lock_member, ac_fn, ac_instrptr, num DESC;
