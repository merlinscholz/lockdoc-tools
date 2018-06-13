SELECT
	ac_id, alloc_id, 
	ac_type, st_fn,
	ac_address,
--	ac_ptr,
	lower(hex(st_instrptr)) AS st_instrptr,
	sl_member,
	dt_name,
	IFNULL(lh.lock_id,'null') AS locks,
	IFNULL(CONCAT(l.lock_type_name, '[', l.sub_lock, ']'),'null') AS lock_types,
	IF(l.embedded_in = alloc_id,'1','0') AS embedded_in_same,
	CASE 
		WHEN lh.lastPreemptCount & 0x0ff00 THEN 'softirq'
		WHEN lh.lastPreemptCount & 0xf0000 THEN 'hardirq'
		WHEN (lh.lastPreemptCount & 0xfff00) = 0 THEN 'noirq'
		ELSE 'unknown'
	END AS context,
	IF(l.lock_type_name IS NULL, 'null',
	  IF(sl2.member_id IS NULL,CONCAT('global_',l.lock_type_name,'_',l.id),CONCAT(mn2.name,'_',IF(l.embedded_in = alloc_id,'1','0')))) AS lock_member,
	COUNT(*) AS num
FROM
(
	SELECT
		ac.id AS ac_id,
		ac.txn_id AS ac_txn_id,
		ac.alloc_id AS alloc_id,
		ac.type AS ac_type,
		st.function AS st_fn,
		ac.address AS ac_address,
		a.base_address AS a_ptr,
		st.instruction_ptr AS st_instrptr,
		mn.name AS sl_member,
		dt.name AS dt_name
	FROM accesses AS ac
	INNER JOIN allocations AS a ON a.id=ac.alloc_id
	INNER JOIN data_types AS dt ON dt.id=a.data_type_id
	INNER JOIN stacktraces AS st ON ac.stacktrace_id=st.id AND st.sequence=0
	LEFT JOIN structs_layout AS sl ON sl.type_id=a.data_type_id AND (ac.address - a.base_address) >= sl.offset AND (ac.address - a.base_address) < sl.offset+sl.size
	LEFT JOIN member_names AS mn ON mn.id = sl.member_id
	LEFT JOIN function_blacklist fn_bl
	  ON fn_bl.fn = st.function
	 AND 
	 (
	   (fn_bl.data_type_id IS NULL  AND fn_bl.member_name_id IS NULL) -- globally blacklisted function
	   OR
	   (fn_bl.data_type_id = a.data_type_id AND fn_bl.member_name_id IS NULL) -- for this data type blacklisted
	   OR
	   (fn_bl.data_type_id = a.data_type_id AND fn_bl.member_name_id = sl.member_id) -- for this member blacklisted
	 )
	WHERE 
		a.data_type_id = (SELECT id FROM data_types WHERE name = 'inode') AND
		ac.type  IN ('r','w') AND
--		sl.member IN ('i_sb_list') AND
		fn_bl.fn IS NULL
	GROUP BY ac.id
) s
LEFT JOIN locks_held AS lh ON lh.txn_id=ac_txn_id
LEFT JOIN locks AS l ON l.id=lh.lock_id
LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
LEFT JOIN structs_layout AS sl2 ON sl2.type_id=a2.data_type_id AND (l.address - a2.base_address) >= sl2.offset AND (l.address - a2.base_address) < sl2.offset+sl2.size
LEFT JOIN member_names AS mn2 ON mn2.id = sl2.member_id
-- WHERE
--	sl2.member IS NULL
--	lh.start IS NULL
GROUP BY ac_type, lock_member, sl_member, st_fn
ORDER BY ac_type, lock_member, sl_member, st_fn, num DESC;
