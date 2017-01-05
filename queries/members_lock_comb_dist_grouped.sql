-- Generates the same output as generate-lock-symbols.php
SELECT
	ac_id,
	ac_type,
	locks,
	lock_types,
	embedded_in_type,
	embedded_in_same,
	lockFn,
	preemptCount,
	ac_fn,
	lockContext,
	context,
	sl_member,
	CASE 
		WHEN lh.lastPreemptCount & 0x0ff00 THEN 'softirq'
		WHEN lh.lastPreemptCount & 0xf0000 THEN 'hardirq'
		WHEN (lh.lastPreemptCount & 0xfff00) = 0 THEN 'noirq'
		ELSE 'unknown'
	END AS context,
	COUNT(*) AS num
FROM
(
	SELECT  ac_id, alloc_id, ac_type, ac_fn, ac_address, a_ptr, sl_member,
		GROUP_CONCAT(IFNULL(lh.lock_id,"null") SEPARATOR '+') AS locks,
		GROUP_CONCAT(IFNULL(l.type,"null") SEPARATOR '+') AS lock_types,
		GROUP_CONCAT(IFNULL(a2.type,"null") SEPARATOR '+') AS embedded_in_type,
		GROUP_CONCAT(IF(l.embedded_in = alloc_id,'1','0') SEPARATOR '+') AS embedded_in_same,
		GROUP_CONCAT(lh.lastLockFn SEPARATOR '+') AS lockFn,
		GROUP_CONCAT(HEX(lh.lastPreemptCount) SEPARATOR '+') AS preemptCount,
		GROUP_CONCAT(lh.lastFn SEPARATOR '+') AS lockContext,
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
			ac.type AS ac_type,
			ac.fn AS ac_fn,
			ac.address AS ac_address,
			a.ptr AS a_ptr,
			sl.member AS sl_member
		FROM accesses AS ac
		INNER JOIN allocations AS a ON a.id=ac.alloc_id
		LEFT JOIN structs_layout AS sl ON sl.type_id=a.type AND (ac.address - a.ptr) >= sl.offset AND (ac.address - a.ptr) < sl.offset+sl.size
		WHERE
				alloc_id=2 AND
--				sl.member = 'i_flags' AND
				ac.type IN ('r','w') AND 
				ac.fn NOT IN
				(
					SELECT bl.fn
					FROM blacklist AS bl
					WHERE
					bl.datatype_id = a.type AND
					(
						bl.datatype_member IS NULL OR
						bl.datatype_member = sl.member
					)
				)				
		GROUP BY ac.id
	) s
	LEFT JOIN locks_held AS lh ON lh.access_id=ac_id
	LEFT JOIN locks AS l ON l.id=lh.lock_id
	LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
	GROUP BY ac_id
) t
GROUP BY ac_type, locks,sl_member
ORDER BY num DESC
