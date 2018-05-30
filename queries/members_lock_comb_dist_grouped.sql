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
		GROUP_CONCAT(IFNULL(CONCAT(l.type, '[', l.sub_lock, ']'),"null") ORDER BY l.id SEPARATOR '+') AS lock_types,
		GROUP_CONCAT(IFNULL(a2.type,"null") ORDER BY l.id SEPARATOR '+') AS embedded_in_type,
		GROUP_CONCAT(IF(l.embedded_in = alloc_id,'1','0') ORDER BY l.id SEPARATOR '+') AS embedded_in_same,
		GROUP_CONCAT(HEX(lh.lastPreemptCount) ORDER BY l.id SEPARATOR '+') AS preemptCount,
		GROUP_CONCAT(lh.lastFn ORDER BY l.id SEPARATOR '+') AS lockContext,
		GROUP_CONCAT(IF(l.type IS NULL, 'null',
		  IF(mn2.name IS NULL,CONCAT('global_',l.type,'_',l.id),CONCAT(mn2.name,'_',IF(l.embedded_in = alloc_id,'1','0')))) ORDER BY l.id SEPARATOR '+') AS lock_member,
		GROUP_CONCAT(DISTINCT
			CASE 
				WHEN lh.lastPreemptCount & 0x0ff00 THEN 'softirq'
				WHEN lh.lastPreemptCount & 0xf0000 THEN 'hardirq'
				WHEN (lh.lastPreemptCount & 0xfff00) = 0 THEN 'noirq'
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
			a.ptr AS a_ptr,
			mn.name AS sl_member
		FROM accesses AS ac
		INNER JOIN allocations AS a ON a.id=ac.alloc_id
		INNER JOIN stacktraces AS st ON ac.stacktrace_id=st.id AND st.sequence=0
		LEFT JOIN structs_layout AS sl ON sl.type_id=a.type AND (ac.address - a.ptr) >= sl.offset AND (ac.address - a.ptr) < sl.offset+sl.size
		LEFT JOIN member_names AS mn ON mn.id = sl.member_id
		LEFT JOIN function_blacklist fn_bl
		  ON fn_bl.datatype_id = a.type
		 AND fn_bl.fn = ac.fn
		 AND (fn_bl.datatype_member_id IS NULL OR fn_bl.datatype_member_id = sl.member_id)
		WHERE
				a.type = (SELECT id FROM data_types WHERE name = 'inode') AND
				mn.name = 'i_acl' AND
				ac.type IN ('r','w') AND 
				fn_bl.fn IS NULL
		GROUP BY ac.id
	) s
	LEFT JOIN locks_held AS lh ON lh.txn_id=ac_txn_id
	LEFT JOIN locks AS l ON l.id=lh.lock_id
	LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
	LEFT JOIN structs_layout AS sl2 ON sl2.type_id=a2.type AND (l.ptr - a2.ptr) >= sl2.offset AND (l.ptr - a2.ptr) < sl2.offset+sl2.size
	LEFT JOIN member_names AS mn2 ON mn2.id = sl2.member_id
	GROUP BY ac_id
) t
GROUP BY ac_type, locks, sl_member, context
ORDER BY ac_type, locks, sl_member, context, num DESC
