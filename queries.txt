-- Just check if the is consistent
SELECT *
FROM locks_held AS lh
LEFT JOIN accesses AS as ON as.id=lh.access_id
LEFT JOIN allocations AS a ON a.id=as.alloc_id
LEFT JOIN locks AS l ON l.id=lh.lock_id
LEFT JOIN data_types AS dt ON dt.id=a.type
WHERE as.id IS NULL OR a.id IS NULL OR l.id IS NULL OR dt.id IS NULL;

-- Generates the same output as generate-lock-symbols.php
SELECT ac.id AS ac_id, ac.alloc_id,ac.type,a.type AS datatype,
	   GROUP_CONCAT(lh.lock_id SEPARATOR '+') AS locks, GROUP_CONCAT(l.type SEPARATOR '+') AS lock_types,
	   GROUP_CONCAT(IFNULL(a2.type,"null") SEPARATOR '+') AS embedded_in_type, ac.address - a.ptr AS offset, ac.size, sl.member,
       GROUP_CONCAT(IF(l.embedded_in = a.id,'1','0') SEPARATOR '+') AS embedded_in_same
FROM accesses AS ac
LEFT JOIN structs_layout AS sl ON sl.type_id=a.type AND (ac.address - a.ptr) >= sl.offset AND (ac.address - a.ptr) < sl.offset+sl.size
INNER JOIN allocations AS a ON a.id=ac.alloc_id
INNER JOIN locks_held AS lh ON lh.access_id=ac.id
INNER JOIN locks AS l ON l.id=lh.lock_id
LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
GROUP BY ac.id
-- LIMIT 0,10

-- Get a distribution of locks held during a memory access for a certain type or instance
-- SELECT ls.alloc_id,ac.ts,dt.name,a.ptr,ls.type,locks,offset, embedded_in_type, embedded_in_same, count(*) as num,member,member_offset
SELECT ls.type, locks, embedded_in_type, embedded_in_same, count(*) as num, member
FROM lock_symbols AS ls
INNER JOIN allocations AS a ON a.id=ls.alloc_id
INNER JOIN accesses AS ac ON ac.id=ls.ac_id
INNER JOIN data_types AS dt ON dt.id=a.type
-- WHERE a.type=X
WHERE a.id=2
-- GROUP BY alloc_id,locks
-- GROUP BY alloc_id,locks,type
-- GROUP BY alloc_id,locks,type,member
 GROUP BY ls.alloc_id, ls.type, ls.locks, ls.member
ORDER BY member ASC, num DESC
-- LIMIT 0,10;


-- Count the number of memory accesses without any lock beeing held
SELECT COUNT(*)
FROM accesses As ac
LEFT JOIN locks_held AS lh ON lh.access_id=ac.id
INNER JOIN allocations AS a ON a.id=ac.alloc_id
WHERE lh.start IS NULL -- AND a.id=2;

-- Count the number of memory accesses with at least one lock beeing held
SELECT COUNT(*)
FROM
(
	SELECT ac.id
	FROM accesses As ac
	LEFT JOIN locks_held AS lh ON lh.access_id=ac.id
	INNER JOIN allocations AS a ON a.id=ac.alloc_id
	WHERE lh.start IS NOT NULL -- AND a.ptr=4118814080
	GROUP BY ac.id
) AS foo

SELECT
	ac_id,
	ac_type,
	locks,
	lock_types,
--	embedded_in_type,
--	embedded_in_same,
	lockFn,
--	preemptCount,
--	ac_fn,
--	lockContext,
	context,
	sl_member
	,COUNT(*) AS num
FROM
(
	SELECT  ac_id, alloc_id, ac_type, ac_fn, ac_address, a_ptr, sl_member,
		lh.lock_id AS locks, l.type AS lock_types,
		IFNULL(a2.type,"null") AS embedded_in_type,
		IF(l.embedded_in = alloc_id,'1','0') AS embedded_in_same,
		lh.lastLockFn AS lockFn,
		HEX(lh.lastPreemptCount) AS preemptCount,
		lh.lastFn AS lockContext,
		CASE 
			WHEN lh.lastPreemptCount & 0x0ff00 THEN 'softirq'
			WHEN lh.lastPreemptCount & 0xf0000 THEN 'hardirq'
			WHEN (lh.lastPreemptCount & 0xfff00) = 0 THEN 'noirq'
			ELSE 'unknown'
		END AS context
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
				ac.type ='r'
		GROUP BY ac.id
	) s
	LEFT JOIN locks_held AS lh ON lh.access_id=ac_id
	LEFT JOIN locks AS l ON l.id=lh.lock_id
	LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
	WHERE
		lh.start IS NULL
		OR
		(
			lh.start IS NOT NULL AND
			(l.embedded_in = alloc_id OR l.type = 'rcu' ) AND
			(
				lh.lastPreemptCount & 0xf0000 OR -- in_hardirq
				lh.lastPreemptCount & 0x0ff00 OR -- in_softirq
				lh.lastPreemptCount & 0xfff00 OR -- in_hardirq or in_softirq
				(lh.lastPreemptCount & 0xfff00) = 0 -- neither soft- nor hardirq
			)
		)
	GROUP BY ac_id
) t
GROUP BY alloc_id, ac_type, locks, lockFn, lockContext, member, ac_fn
ORDER BY num DESC

SELECT
	ac_id,
	ac_type,
	locks,
	lock_types,
--	embedded_in_type,
--	embedded_in_same,
	lockFn,
--	preemptCount,
--	ac_fn,
--	lockContext,
	context,
	sl_member
	,COUNT(*) AS num
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
				ac.type ='r'
		GROUP BY ac.id
	) s
	LEFT JOIN locks_held AS lh ON lh.access_id=ac_id
	LEFT JOIN locks AS l ON l.id=lh.lock_id
	LEFT JOIN allocations AS a2 ON a2.id=l.embedded_in
	WHERE
		lh.start IS NULL
		OR
		(
			lh.start IS NOT NULL AND
			(l.embedded_in = alloc_id OR l.type = 'rcu' ) AND
			(
				lh.lastPreemptCount & 0xf0000 OR -- in_hardirq
				lh.lastPreemptCount & 0x0ff00 OR -- in_softirq
				lh.lastPreemptCount & 0xfff00 OR -- in_hardirq or in_softirq
				(lh.lastPreemptCount & 0xfff00) = 0 -- neither soft- nor hardirq
			)
		)
	GROUP BY ac_id
) t
GROUP BY ac_type, locks,sl_member
-- GROUP BY alloc_id, ac_type, locks, lockFn, lockContext, member, ac_fn
-- GROUP BY alloc_id, ac_type, locks, lockFn, member
ORDER BY num DESC
