-- Generates the same output as generate-lock-symbols.php
SELECT
--	ac_id,
	ac_type,
	sl_member,
	context,
	lock_member,
	locks,
	lock_types,
--	embedded_in_type,
--	embedded_in_same,
--	lockFn,
--	preemptCount,
	st_fn,
	st_instrptr,
--	lockContext,
	COUNT(*) AS num
FROM
(
	SELECT
		ac_id,
		alloc_id,
		ac_type,
		st_fn,
		ac_address,
		st_instrptr,
		a_ptr,
		sl_member,
		GROUP_CONCAT(IFNULL(lh.lock_id,"null") ORDER BY l.id SEPARATOR '+') AS locks,
		GROUP_CONCAT(IFNULL(CONCAT(l.lock_type_name, '[', l.sub_lock, ']'),"null") ORDER BY l.id SEPARATOR '+') AS lock_types,
		GROUP_CONCAT(IFNULL(a2.data_type_id,"null") ORDER BY l.id SEPARATOR '+') AS embedded_in_type,
		GROUP_CONCAT(IF(l.embedded_in = alloc_id,'1','0') ORDER BY l.id SEPARATOR '+') AS embedded_in_same,
		GROUP_CONCAT(HEX(lh.last_preempt_count) ORDER BY l.id SEPARATOR '+') AS preemptCount,
		GROUP_CONCAT(lh.last_fn ORDER BY l.id SEPARATOR '+') AS lockContext,
		GROUP_CONCAT(IF(l.lock_type_name IS NULL, 'null',
		  IF(mn2.name IS NULL,CONCAT('global_',l.lock_type_name,'_',l.id),CONCAT(mn2.name,'_',IF(l.embedded_in = alloc_id,'1','0')))) ORDER BY l.id SEPARATOR '+') AS lock_member,
		GROUP_CONCAT(DISTINCT
			CASE 
				WHEN lh.last_preempt_count & 0x0ff00 THEN 'softirq'
				WHEN lh.last_preempt_count & 0xf0000 THEN 'hardirq'
				WHEN (lh.last_preempt_count & 0xfff00) = 0 THEN 'noirq'
				ELSE 'unknown'
			END
			SEPARATOR '+') AS context
	FROM
	(
		SELECT
			ac.id AS ac_id, 
			ac.alloc_id AS alloc_id,
			ac.txn_id AS ac_txn_id,
			ac.type AS ac_type,
			st.function AS st_fn,
			ac.address AS ac_address,
			LOWER(HEX(st.instruction_ptr)) AS st_instrptr,
			a.base_address AS a_ptr,
			mn.name AS sl_member
		FROM accesses AS ac
		INNER JOIN allocations AS a ON a.id=ac.alloc_id
		INNER JOIN stacktraces AS st ON ac.stacktrace_id=st.id AND st.sequence=0
		LEFT JOIN structs_layout AS sl ON sl.data_type_id=a.data_type_id AND (ac.address - a.base_address) >= sl.offset AND (ac.address - a.base_address) < sl.offset+sl.size
		LEFT JOIN member_names AS mn ON mn.id = sl.member_name_id
		LEFT JOIN function_blacklist fn_bl
		  ON fn_bl.fn = st.function
		 AND 
		 (
		   (fn_bl.data_type_id IS NULL  AND fn_bl.member_name_id IS NULL) -- globally blacklisted function
		   OR
		   (fn_bl.data_type_id = a.data_type_id AND fn_bl.member_name_id IS NULL) -- for this data type blacklisted
		   OR
		   (fn_bl.data_type_id = a.data_type_id AND fn_bl.member_name_id = sl.member_name_id) -- for this member blacklisted
		 )
		WHERE
				a.data_type_id = (SELECT id FROM data_types WHERE name = 'inode') AND
				mn.name = 'i_acl' AND
				ac.type IN ('r','w') AND 
				fn_bl.fn IS NULL
		GROUP BY ac.id
	) s
	LEFT JOIN locks_held AS lh ON lh.txn_id=ac_txn_id
	LEFT JOIN locks AS l ON l.id=lh.lock_id
	LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
	LEFT JOIN structs_layout AS sl2 ON sl2.data_type_id=a2.data_type_id AND (l.address - a2.base_address) >= sl2.offset AND (l.address - a2.base_address) < sl2.offset+sl2.size
	LEFT JOIN member_names AS mn2 ON mn2.id = sl2.member_name_id
	GROUP BY ac_id
) t
GROUP BY ac_type, locks, sl_member, context
ORDER BY ac_type, locks, sl_member, context, num DESC
