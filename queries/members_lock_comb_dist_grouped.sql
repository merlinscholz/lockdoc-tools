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
	ac_fn,
	ac_instrptr,
--	lockContext,
	COUNT(*) AS num
FROM
(
	SELECT
		ac_id,
		alloc_id,
		ac_type,
		ac_fn,
		ac_address,
		ac_instrptr,
		a_ptr,
		sl_member,
		GROUP_CONCAT(IFNULL(lh.lock_id,"null") ORDER BY l.id SEPARATOR '+') AS locks,
		GROUP_CONCAT(IFNULL(l.type,"null") ORDER BY l.id SEPARATOR '+') AS lock_types,
		GROUP_CONCAT(IFNULL(a2.type,"null") ORDER BY l.id SEPARATOR '+') AS embedded_in_type,
		GROUP_CONCAT(IF(l.embedded_in = alloc_id,'1','0') ORDER BY l.id SEPARATOR '+') AS embedded_in_same,
		GROUP_CONCAT(lh.lastLockFn ORDER BY l.id SEPARATOR '+') AS lockFn,
		GROUP_CONCAT(HEX(lh.lastPreemptCount) ORDER BY l.id SEPARATOR '+') AS preemptCount,
		GROUP_CONCAT(lh.lastFn ORDER BY l.id SEPARATOR '+') AS lockContext,
		GROUP_CONCAT(IF(l.type IS NULL, 'null',
		  IF(sl2.member IS NULL,CONCAT('global_',l.type,'_',l.id),CONCAT(sl2.member,'_',IF(l.embedded_in = alloc_id,'1','0')))) ORDER BY l.id SEPARATOR '+') AS lock_member,
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
			ac.fn AS ac_fn,
			ac.address AS ac_address,
			LOWER(HEX(ac.instrptr)) AS ac_instrptr,
			a.ptr AS a_ptr,
			sl.member AS sl_member
		FROM accesses AS ac
		INNER JOIN allocations AS a ON a.id=ac.alloc_id
		LEFT JOIN structs_layout AS sl ON sl.type_id=a.type AND (ac.address - a.ptr) >= sl.offset AND (ac.address - a.ptr) < sl.offset+sl.size
		LEFT JOIN blacklist bl ON bl.datatype_id = a.type AND bl.fn = ac.fn AND (bl.datatype_member IS NULL OR bl.datatype_member = sl.member)
		WHERE
				a.type = 2 AND
				sl.member = 'i_acl' AND
				ac.type IN ('r','w') AND 
				bl.fn IS NULL
		GROUP BY ac.id
	) s
	LEFT JOIN locks_held AS lh ON lh.txn_id=ac_txn_id
	LEFT JOIN locks AS l ON l.id=lh.lock_id
	LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
	LEFT JOIN structs_layout AS sl2 ON sl2.type_id=a2.type AND (l.ptr - a2.ptr) >= sl2.offset AND (l.ptr - a2.ptr) < sl2.offset+sl2.size
	GROUP BY ac_id
) t
GROUP BY ac_type, locks, sl_member, context
ORDER BY ac_type, locks, sl_member, context, num DESC
