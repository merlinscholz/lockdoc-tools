-- backing_dev_info member: r:state (15791 lock combinations)
--   hypotheses: 53113
--      96.9% (109708 out of 113193 mem accesses under locks): EMBSAME(wb.list_lock)
--     (Possibly accessible without locks, 5502 accesses without locks [4.64%] out of a total of 118695 observed.)
-- backing_dev_info member: r:wb.b_io (5214 lock combinations)
--   hypotheses: 18109
--      98.9% (108976 out of 110181 mem accesses under locks): EMBSAME(wb.list_lock)
--     (Possibly accessible without locks, 182 accesses without locks [0.165%] out of a total of 110363 observed.)
-- inode member: r:i_data.page_tree (52437 lock combinations)
--   hypotheses: 197817
--      65.5% (2388548 out of 3644080 mem accesses under locks): 16(rcu)
--      46.4% (1691395 out of 3644080 mem accesses under locks): EMBSAME(i_data.tree_lock)
--      28.4% (1034003 out of 3644080 mem accesses under locks): EMBSAME(i_mutex)
--      18.7% (680516 out of 3644080 mem accesses under locks): EMBSAME(i_mutex) -> EMBSAME(i_data.tree_lock)
--      18.6% (676471 out of 3644080 mem accesses under locks): 16(rcu) + EMBSAME(i_data.tree_lock)
--          100% 16(rcu) -> EMBSAME(i_data.tree_lock)
--        0.00414% EMBSAME(i_data.tree_lock) -> 16(rcu)
--     (Possibly accessible without locks, 62079 accesses without locks [1.68%] out of a total of 3706159 observed.)
-- inode member: r:i_lock (312 lock combinations)
--   hypotheses: 2446
--        92% (158813 out of 172622 mem accesses under locks): EMBSAME(i_lock)
--     (Possibly accessible without locks, 2448 accesses without locks [1.4%] out of a total of 175070 observed.)
-- cdev member: w:kobj (9 lock combinations)
--   hypotheses: 32
--      89.4% (42 out of 47 mem accesses under locks): 1143(mutex) -> 1144(mutex)
--      89.4% (42 out of 47 mem accesses under locks): 1143(mutex)
--      89.4% (42 out of 47 mem accesses under locks): 1144(mutex)
--        34% (16 out of 47 mem accesses under locks): 1201(mutex) -> 1143(mutex) -> 1144(mutex)
--        34% (16 out of 47 mem accesses under locks): 1201(mutex) -> 1144(mutex)
--        34% (16 out of 47 mem accesses under locks): 1201(mutex) -> 1143(mutex)
--        34% (16 out of 47 mem accesses under locks): 1201(mutex)
--      25.5% (12 out of 47 mem accesses under locks): 1269(mutex) -> 1143(mutex) -> 1144(mutex)
--      25.5% (12 out of 47 mem accesses under locks): 1269(mutex) -> 1144(mutex)
--      25.5% (12 out of 47 mem accesses under locks): 1269(mutex) -> 1143(mutex)
--      25.5% (12 out of 47 mem accesses under locks): 1269(mutex)
--        17% (8 out of 47 mem accesses under locks): 1160(mutex) -> 1143(mutex) -> 1144(mutex)
--        17% (8 out of 47 mem accesses under locks): 1160(mutex) -> 1143(mutex)
--        17% (8 out of 47 mem accesses under locks): 1160(mutex) -> 1144(mutex)
--        17% (8 out of 47 mem accesses under locks): 1160(mutex)
--      10.6% (5 out of 47 mem accesses under locks): 2474(mutex)
--     (Possibly accessible without locks, 27 accesses without locks [36.5%] out of a total of 74 observed.)
-- inode member: w:i_acl (148 lock combinations)
--   hypotheses: 3065
--       100% (5677 out of 5677 mem accesses under locks): EMBSAME(i_lock)
--      81.1% (4605 out of 5677 mem accesses under locks): EMBSAME(i_mutex) -> EMBSAME(i_lock)
--      81.1% (4605 out of 5677 mem accesses under locks): EMBSAME(i_mutex)
-- inode member: w:i_count (173 lock combinations)
--   hypotheses: 20089
--       100% (2312 out of 2312 mem accesses under locks): EMBSAME(i_lock)
--      95.7% (2212 out of 2312 mem accesses under locks): 45(spinlock_t) -> EMBSAME(i_lock)
--      95.7% (2212 out of 2312 mem accesses under locks): 45(spinlock_t)
--      28.3% (655 out of 2312 mem accesses under locks): EMB:8330(i_mutex) -> 45(spinlock_t) -> EMBSAME(i_lock)
--      28.3% (655 out of 2312 mem accesses under locks): EMB:8330(i_mutex) -> EMBSAME(i_lock)
--      28.3% (655 out of 2312 mem accesses under locks): EMB:8330(i_mutex) -> 45(spinlock_t)
--      28.3% (655 out of 2312 mem accesses under locks): EMB:8330(i_mutex)
--
-- tooltest:
-- inode member: w:i_mtime (282 lock combinations)
--   hypotheses: 1951
--        99% (2222 out of 2245 mem accesses under locks): EMBSAME(i_mutex)
--     (Possibly accessible without locks, 9 accesses without locks [0.399%] out of a total of 2254 observed.)
-- inode member: w:i_nlink-__i_nlink (447 lock combinations)
--   hypotheses: 1577
--      96.9% (16845 out of 17392 mem accesses under locks): 37(mutex)
--     (Possibly accessible without locks, 10 accesses without locks [0.0575%] out of a total of 17402 observed.)
-- inode member: w:i_sb_list (1092 lock combinations)
--   hypotheses: 8933
--       100% (6657 out of 6657 mem accesses under locks): 45(spinlock_t)
--      68.2% (4541 out of 6657 mem accesses under locks): 2127(spinlock_t) -> 45(spinlock_t)
--      68.2% (4541 out of 6657 mem accesses under locks): 2127(spinlock_t)
--      [...]
-- inode member: w:i_security (1070 lock combinations)
--   hypotheses: 2451
--      66.9% (2224 out of 3326 mem accesses under locks): 37(mutex)
--      14.5% (483 out of 3326 mem accesses under locks): EMB:150(i_mutex)
--     (Possibly accessible without locks, 72 accesses without locks [2.12%] out of a total of 3398 observed.)
-- task_struct member: r:cgroups (4787 lock combinations)
--   hypotheses: 30047 
--       100% (207042 out of 207066 mem accesses under locks): 16(rcu)
--     (Possibly accessible without locks, 776 accesses without locks [0.373%] out of a total of 207842 observed.)
-- task_struct member: w:total_link_count (351 lock combinations)
--   hypotheses: 1415
--      99.4% (7624 out of 7670 mem accesses under locks): 16(rcu)
--     (Possibly accessible without locks, 232 accesses without locks [2.94%] out of a total of 7902 observed.)
--
-- Gegenbeispiele: gruppiert nach Funktion und statischer Adresse und gehaltenen Locks

-- mem accesses to r:task_struct::cgroups while 16(rcu) is NOT held
-- (sub-query variant)
SELECT fn, instrptr, locks_held, COUNT(*) AS occurrences
FROM
(
	SELECT ac.fn, CONCAT('0x', HEX(ac.instrptr)) AS instrptr,
		GROUP_CONCAT(
			CASE
			WHEN l.embedded_in IS NULL THEN CONCAT(l.id, '(', l.type, ')') -- global (or embedded in unknown allocation)
			WHEN l.embedded_in IS NOT NULL AND l.embedded_in = ac.alloc_id
				THEN CONCAT('EMBSAME(', IF(l.ptr - lock_a.ptr = lock_member.offset, mn_lock.name, CONCAT(mn_lock.name, '?')), ')') -- embedded in same
			ELSE CONCAT('EMB:', l.id, '(',  IF(l.ptr - lock_a.ptr = lock_member.offset, mn_lock.name, CONCAT(mn_lock.name, '?')), ')') -- embedded in other
			END
			ORDER BY lh.start
		) AS locks_held
	FROM accesses ac
	JOIN allocations a
	  ON ac.alloc_id = a.id
	 AND ac.type = 'r'
	JOIN data_types dt
	  ON a.type = dt.id
	 AND dt.name = 'task_struct'
	JOIN structs_layout_flat sl
	  ON sl.type_id = a.type
	 AND sl.helper_offset = ac.address - a.ptr
	JOIN member_names mn
	  ON mn.id = sl.member_id
	 AND mn.name = 'cgroups'
	LEFT JOIN function_blacklist fn_bl
	  ON fn_bl.datatype_id = a.type
	 AND fn_bl.fn = ac.fn
	 AND (fn_bl.datatype_member_id IS NULL OR fn_bl.datatype_member_id = sl.member_id)
	LEFT JOIN locks_held lh
	  ON lh.txn_id = ac.txn_id
	LEFT JOIN locks l
	  ON l.id = lh.lock_id
			
	-- find out more about each held lock (allocation -> structs_layout
	-- member or contained-in member in case of a complex member)
	LEFT JOIN allocations lock_a
	  ON l.embedded_in = lock_a.id
	LEFT JOIN structs_layout_flat lock_member
	  ON lock_a.type = lock_member.type_id
	 AND l.ptr - lock_a.ptr = lock_member.helper_offset
	-- lock_a.id IS NULL                         => not embedded
	-- l.ptr - lock_a.ptr = lock_member.offset   => the lock is exactly this member (or at the beginning of a complex sub-struct)
	-- else                                      => the lock is contained in this member, exact name unknown
	JOIN member_names mn_lock
	  ON mn_lock.id = lock_member.member_id

	WHERE ac.txn_id IS NULL
	   OR ac.txn_id NOT IN
	(
		SELECT lh.txn_id
		FROM locks_held lh
		WHERE lh.lock_id = 16
	)
	AND fn_bl.fn IS NULL
	GROUP BY ac.id
) all_counterexamples
GROUP BY instrptr, locks_held
ORDER BY instrptr, occurrences
;

-- mem accesses to r:task_struct::cgroups while 16(rcu) is NOT held
-- (LEFT JOIN variant)
SELECT fn, instrptr, locks_held, COUNT(*) AS occurrences
FROM
(
	SELECT ac.fn, CONCAT('0x', HEX(ac.instrptr)) AS instrptr,
		GROUP_CONCAT(
			CASE
			WHEN l.embedded_in IS NULL THEN CONCAT(l.id, '(', l.type, ')') -- global (or embedded in unknown allocation)
			WHEN l.embedded_in IS NOT NULL AND l.embedded_in = ac.alloc_id
				THEN CONCAT('EMBSAME(', IF(l.ptr - lock_a.ptr = lock_member.offset, mn_lock.name, CONCAT(mn_lock.name, '?')), ')') -- embedded in same
			ELSE CONCAT('EMB:', l.id, '(',  IF(l.ptr - lock_a.ptr = lock_member.offset, mn_lock.name, CONCAT(mn_lock.name, '?')), ')') -- embedded in other
			END
			ORDER BY lh.start
		) AS locks_held
	FROM accesses ac
	JOIN allocations a
	  ON ac.alloc_id = a.id
	 AND ac.type = 'r'
	JOIN data_types dt
	  ON a.type = dt.id
	 AND dt.name = 'task_struct'
	JOIN structs_layout_flat sl
	  ON sl.type_id = a.type
	 AND sl.helper_offset = ac.address - a.ptr
	JOIN member_names mn
	  ON mn.id = sl.member_id
	 AND mn.name = 'cgroups'	 
	LEFT JOIN function_blacklist fn_bl
	  ON fn_bl.datatype_id = a.type
	 AND fn_bl.fn = ac.fn
	 AND (fn_bl.datatype_member_id IS NULL OR fn_bl.datatype_member_id = sl.member_id)

	LEFT JOIN locks_held lh
	  ON lh.txn_id = ac.txn_id
	LEFT JOIN locks l
	  ON l.id = lh.lock_id
			
	-- find out more about each held lock (allocation -> structs_layout
	-- member or contained-in member in case of a complex member)
	LEFT JOIN allocations lock_a
	  ON l.embedded_in = lock_a.id
	LEFT JOIN structs_layout_flat lock_member
	  ON lock_a.type = lock_member.type_id
	 AND l.ptr - lock_a.ptr = lock_member.helper_offset
	-- lock_a.id IS NULL                         => not embedded
	-- l.ptr - lock_a.ptr = lock_member.offset   => the lock is exactly this member (or at the beginning of a complex sub-struct)
	-- else                                      => the lock is contained in this member, exact name unknown
	JOIN member_names mn_lock
	  ON mn_lock.id = lock_member.member_id

	LEFT JOIN locks_held lh_shouldbeheld
	  ON lh_shouldbeheld.txn_id = ac.txn_id
	 AND lh_shouldbeheld.lock_id = 16

	WHERE lh_shouldbeheld.txn_id IS NULL
	AND fn_bl.fn IS NULL
	GROUP BY ac.id
) all_counterexamples
GROUP BY instrptr, locks_held
ORDER BY instrptr, occurrences
;

-- mem accesses to w:inode::i_mtime while EMBSAME(i_mutex) is NOT held
-- (LEFT JOIN variant)
SELECT fn, instrptr, locks_held, COUNT(*) AS occurrences
FROM
(
	SELECT ac.fn, CONCAT('0x', HEX(ac.instrptr)) AS instrptr,
		GROUP_CONCAT(
			CASE
			WHEN l.embedded_in IS NULL THEN CONCAT(l.id, '(', l.type, ')') -- global (or embedded in unknown allocation)
			WHEN l.embedded_in IS NOT NULL AND l.embedded_in = ac.alloc_id
				THEN CONCAT('EMBSAME(', IF(l.ptr - lock_a.ptr = lock_member.offset, mn_lock.name, CONCAT(mn_lock.name, '?')), ')') -- embedded in same
			ELSE CONCAT('EMB:', l.id, '(',  IF(l.ptr - lock_a.ptr = lock_member.offset, mn_lock.name, CONCAT(mn_lock.name, '?')), ')') -- embedded in other
			END
			ORDER BY lh.start
		) AS locks_held
	FROM accesses ac
	JOIN allocations a
	  ON ac.alloc_id = a.id
	 AND ac.type = 'w'
	JOIN data_types dt
	  ON a.type = dt.id
	 AND dt.name = 'inode'
	JOIN structs_layout_flat sl
	  ON sl.type_id = a.type
	 AND sl.helper_offset = ac.address - a.ptr
	JOIN member_names mn
	  ON mn.id = sl.member_id
	 AND mn.name = 'i_mtime'
	LEFT JOIN function_blacklist fn_bl
	  ON fn_bl.datatype_id = a.type
	 AND fn_bl.fn = ac.fn
	 AND (fn_bl.datatype_member_id IS NULL OR fn_bl.datatype_member_id = sl.member_id)

	LEFT JOIN locks_held lh
	  ON lh.txn_id = ac.txn_id
	LEFT JOIN locks l
	  ON l.id = lh.lock_id
			
	-- find out more about each held lock (allocation -> structs_layout
	-- member or contained-in member in case of a complex member)
	LEFT JOIN allocations lock_a
	  ON l.embedded_in = lock_a.id
	LEFT JOIN structs_layout_flat lock_member
	  ON lock_a.type = lock_member.type_id
	 AND l.ptr - lock_a.ptr = lock_member.helper_offset
	-- lock_a.id IS NULL                         => not embedded
	-- l.ptr - lock_a.ptr = lock_member.offset   => the lock is exactly this member (or at the beginning of a complex sub-struct)
	-- else                                      => the lock is contained in this member, exact name unknown
	JOIN member_names mn_lock
	  ON mn_lock.id = lock_member.member_id

	LEFT JOIN structs_layout_flat lock_member_sbh -- sbh = ShouldBeHeld
	  ON lock_a.type = lock_member_sbh.type_id
	 AND l.ptr - lock_a.ptr = lock_member_sbh.helper_offset
	LEFT JOIN member_names mn_lock_member_sbh
	  ON mn_lock_member_sbh.id = lock_member_sbh.member_id
	 AND mn_lock_member_sbh.name = 'i_mutex'
	
	WHERE (ac.txn_id IS NULL
	   OR lock_member_sbh.type_id IS NULL)
	AND fn_bl.fn IS NULL
	GROUP BY ac.id
) all_counterexamples
GROUP BY instrptr, locks_held
ORDER BY instrptr, occurrences
;

-- block_device member: r:bd_invalidated (36 lock combinations) 
--   hypotheses: 181 
--       100% (78 out of 78 mem accesses under locks): EMBSAME(bd_mutex) 
--      11.5% (9 out of 78 mem accesses under locks): EMB:5779(i_mutex) + EMBSAME(bd_mutex) 
--         77.8% EMB:5779(i_mutex) -> EMBSAME(bd_mutex) 
--         22.2% EMBSAME(bd_mutex) -> EMB:5779(i_mutex) 
--      11.5% (9 out of 78 mem accesses under locks): EMB:5779(i_mutex) 
SELECT fn, instrptr, locks_held, COUNT(*) AS occurrences
FROM
(
	SELECT ac.fn, CONCAT('0x', HEX(ac.instrptr)) AS instrptr,
		GROUP_CONCAT(
			CASE
			WHEN l.embedded_in IS NULL THEN CONCAT(l.id, '(', l.type, ')') -- global (or embedded in unknown allocation)
			WHEN l.embedded_in IS NOT NULL AND l.embedded_in = ac.alloc_id
				THEN CONCAT('EMBSAME(', IF(l.ptr - lock_a.ptr = lock_member.offset, mn_lock.name, CONCAT(mn_lock.name, '?')), ')') -- embedded in same
			ELSE CONCAT('EMB:', l.id, '(',  IF(l.ptr - lock_a.ptr = lock_member.offset, mn_lock.name, CONCAT(mn_lock.name, '?')), ')') -- embedded in other
			END
			ORDER BY lh.start
			SEPARATOR ' -> '
		) AS locks_held
	FROM accesses ac
	JOIN allocations a
	  ON ac.alloc_id = a.id
	 AND ac.type = 'r'
	JOIN data_types dt
	  ON a.type = dt.id
	 AND dt.name = 'block_device'
	JOIN structs_layout_flat sl
	  ON sl.type_id = a.type
	 AND sl.helper_offset = ac.address - a.ptr
	JOIN member_names mn
	  ON mn.id = sl.member_id
	 AND mn.name = 'bd_invalidated'
	LEFT JOIN function_blacklist fn_bl
	  ON fn_bl.datatype_id = a.type
	 AND fn_bl.fn = ac.fn
	 AND (fn_bl.datatype_member_id IS NULL OR fn_bl.datatype_member_id = sl.member_id)

	LEFT JOIN locks_held lh
	  ON lh.txn_id = ac.txn_id
	LEFT JOIN locks l
	  ON l.id = lh.lock_id
			
	-- find out more about each held lock (allocation -> structs_layout
	-- member or contained-in member in case of a complex member)
	LEFT JOIN allocations lock_a
	  ON l.embedded_in = lock_a.id
	LEFT JOIN structs_layout_flat lock_member
	  ON lock_a.type = lock_member.type_id
	 AND l.ptr - lock_a.ptr = lock_member.helper_offset
	-- lock_a.id IS NULL                         => not embedded
	-- l.ptr - lock_a.ptr = lock_member.offset   => the lock is exactly this member (or at the beginning of a complex sub-struct)
	-- else                                      => the lock is contained in this member, exact name unknown
	JOIN member_names mn_lock
	  ON mn_lock.id = lock_member.member_id

	LEFT JOIN structs_layout_flat lock_member_sbh -- sbh = ShouldBeHeld
	  ON lock_a.type = lock_member_sbh.type_id
	 AND l.ptr - lock_a.ptr = lock_member_sbh.helper_offset
	LEFT JOIN member_names mn_lock_member_sbh
	  ON mn_lock_member_sbh.id = lock_member_sbh.member_id
	 AND mn_lock_member_sbh.name = 'bd_mutex'
	
	WHERE (ac.txn_id IS NULL
	   OR lock_member_sbh.type_id IS NULL)
	AND fn_bl.fn IS NULL
	GROUP BY ac.id
) all_counterexamples
GROUP BY instrptr, locks_held
ORDER BY instrptr, occurrences
;

--- experimenting here
SELECT fn, instrptr, --- locks_held,
 COUNT(*) AS occurrences
FROM
(
	SELECT ac.fn, CONCAT('0x', HEX(ac.instrptr)) AS instrptr,
--		GROUP_CONCAT(
--			CASE
--			WHEN l.embedded_in IS NULL THEN CONCAT(l.id, '(', l.type, ')') -- global (or embedded in unknown allocation)
--			WHEN l.embedded_in IS NOT NULL AND l.embedded_in = ac.alloc_id
--				THEN CONCAT('EMBSAME(', IF(l.ptr - lock_a.ptr = lock_member.offset, mn_lock.name, CONCAT(mn_lock.name, '?')), ')') -- embedded in same
--			ELSE CONCAT('EMB:', l.id, '(',  IF(l.ptr - lock_a.ptr = lock_member.offset, mn_lock.name, CONCAT(mn_lock.name, '?')), ')') -- embedded in other
--			END
--			ORDER BY lh.start
--			SEPARATOR ' -> '
--		) AS locks_held
	lh.txn_id AS LH_txn_id, l.id AS L_id, l.embedded_in, lock_a.id AS LOCK_A_id, mn_lock.name AS LM_member
	, lh_sbh.txn_id AS LH_SBH_txn_id, l_sbh.id AS L_SBH_id, l_sbh_a.id L_SBH_A_id
	, mn_lock_member_sbh.name AS lock_member_sbh_member
	FROM accesses ac
	JOIN allocations a
	  ON ac.alloc_id = a.id
	 AND ac.type = 'r'
	JOIN data_types dt
	  ON a.type = dt.id
	 AND dt.name = 'block_device'
	JOIN structs_layout_flat sl
	  ON sl.type_id = a.type
	 AND sl.helper_offset = ac.address - a.ptr
	JOIN member_names mn
	  ON mn.id = sl.member_id
	 AND mn.name = 'bd_invalidated'
	LEFT JOIN function_blacklist fn_bl
	  ON fn_bl.datatype_id = a.type
	 AND fn_bl.fn = ac.fn
	 AND (fn_bl.datatype_member_id IS NULL OR fn_bl.datatype_member_id = sl.member_id)

	LEFT JOIN locks_held lh
	  ON lh.txn_id = ac.txn_id
	LEFT JOIN locks l
	  ON l.id = lh.lock_id
			
	-- find out more about each held lock (allocation -> structs_layout
	-- member or contained-in member in case of a complex member)
	LEFT JOIN allocations lock_a
	  ON l.embedded_in = lock_a.id
	LEFT JOIN structs_layout_flat lock_member
	  ON lock_a.type = lock_member.type_id
	 AND l.ptr - lock_a.ptr = lock_member.helper_offset
	-- lock_a.id IS NULL                         => not embedded
	-- l.ptr - lock_a.ptr = lock_member.offset   => the lock is exactly this member (or at the beginning of a complex sub-struct)
	-- else                                      => the lock is contained in this member, exact name unknown
	JOIN member_names mn_lock
	  ON mn_lock.id = lock_member.member_id

 	LEFT JOIN locks_held lh_sbh -- sbh = ShouldBeHeld
 	  ON lh_sbh.txn_id = ac.txn_id
 	LEFT JOIN locks l_sbh
 	  ON l_sbh.id = lh_sbh.lock_id
 	LEFT JOIN allocations l_sbh_a
 	  ON l_sbh.embedded_in = l_sbh_a.id
 	LEFT JOIN structs_layout_flat lock_member_sbh
 	  ON l_sbh_a.type = lock_member_sbh.type_id
 	 AND l_sbh.ptr - l_sbh_a.ptr = lock_member_sbh.helper_offset
	LEFT JOIN member_names mn_lock_member_sbh
	  ON mn_lock_member_sbh.id = lock_member_sbh.member_id
	 AND mn_lock_member_sbh.name = 'bd_mutex'
	
--	WHERE (ac.txn_id IS NULL
--	   OR lock_member_sbh.type_id IS NULL)
	WHERE 1
	AND fn_bl.fn IS NULL
--	GROUP BY ac.id
) all_counterexamples
GROUP BY instrptr -- , locks_held
ORDER BY instrptr, occurrences
;

-- FINALLY
-- block_device member: r:bd_invalidated (36 lock combinations) 
--   hypotheses: 181 
--       100% (78 out of 78 mem accesses under locks): EMBSAME(bd_mutex) 
--      11.5% (9 out of 78 mem accesses under locks): EMB:5779(i_mutex) + EMBSAME(bd_mutex) 
--         77.8% EMB:5779(i_mutex) -> EMBSAME(bd_mutex) 
--         22.2% EMBSAME(bd_mutex) -> EMB:5779(i_mutex) 
--      11.5% (9 out of 78 mem accesses under locks): EMB:5779(i_mutex) 
--
-- Query: Find all read accesses to block_device::bd_invalidated that are
-- counterexamples to the locking rule "EMB:5779(i_mutex) -> EMBSAME(bd_mutex)"
SELECT fn, instrptr, locks_held, COUNT(*) AS occurrences
FROM
(
	SELECT ac.fn, CONCAT('0x', HEX(ac.instrptr)) AS instrptr,
	GROUP_CONCAT(
		CASE
		WHEN l.embedded_in IS NULL THEN CONCAT(l.id, '(', l.type, ')') -- global (or embedded in unknown allocation)
		WHEN l.embedded_in IS NOT NULL AND l.embedded_in = ac.alloc_id
			THEN CONCAT('EMBSAME(', IF(l.ptr - lock_a.ptr = lock_member.offset, mn_lock.name, CONCAT(mn_lock.name, '?')), ')') -- embedded in same
		ELSE CONCAT('EMB:', l.id, '(',  IF(l.ptr - lock_a.ptr = lock_member.offset, mn_lock.name, CONCAT(mn_lock.name, '?')), ')') -- embedded in other
		END
		ORDER BY lh.start
		SEPARATOR ' -> '
	) AS locks_held

	FROM

	(
		SELECT ac.id, ac.txn_id, ac.alloc_id, ac.fn, ac.instrptr
		FROM accesses ac
		JOIN allocations a
		  ON ac.alloc_id = a.id
		 AND ac.type = 'r'
		JOIN data_types dt
		  ON a.type = dt.id
		 AND dt.name = 'block_device'
		JOIN structs_layout_flat sl
		  ON sl.type_id = a.type
		 AND sl.helper_offset = ac.address - a.ptr
		JOIN member_names mn
		  ON mn.id = sl.member_id
		 AND mn.name = 'bd_invalidated'
		LEFT JOIN function_blacklist fn_bl
		  ON fn_bl.datatype_id = a.type
		 AND fn_bl.fn = ac.fn
		 AND (fn_bl.datatype_member_id IS NULL OR fn_bl.datatype_member_id = sl.member_id)
		WHERE fn_bl.fn IS NULL
		AND ac.id NOT IN	-- either counterexample ...
		-- AND ac.id IN		-- ... or example
		(
			SELECT ac.id
			FROM accesses ac
			JOIN allocations a
			  ON ac.alloc_id = a.id
			 AND ac.type = 'r'
			JOIN data_types dt
			  ON a.type = dt.id
			 AND dt.name = 'block_device'
			JOIN structs_layout_flat sl
			  ON sl.type_id = a.type
			 AND sl.helper_offset = ac.address - a.ptr
			JOIN member_names mn
			  ON mn.id = sl.member_id
			 AND mn.name = 'bd_invalidated'
			LEFT JOIN function_blacklist fn_bl
			  ON fn_bl.datatype_id = a.type
			 AND fn_bl.fn = ac.fn
			 AND (fn_bl.datatype_member_id IS NULL OR fn_bl.datatype_member_id = sl.member_id)

			-- 1st lock
			JOIN locks_held lh_sbh1
			  ON lh_sbh1.txn_id = ac.txn_id
			 AND lh_sbh1.lock_id = 5779

			-- 2nd lock
			JOIN locks_held lh_sbh2 -- sbh = ShouldBeHeld
			  ON lh_sbh2.txn_id = ac.txn_id
			JOIN locks l_sbh2
			  ON l_sbh2.id = lh_sbh2.lock_id
			JOIN allocations l_sbh_a2
			  ON l_sbh2.embedded_in = l_sbh_a2.id
			JOIN structs_layout_flat lock_member_sbh2
			  ON l_sbh_a2.type = lock_member_sbh2.type_id
			 AND l_sbh2.ptr - l_sbh_a2.ptr = lock_member_sbh2.helper_offset
			 AND lh_sbh2.start > lh_sbh1.start -- temporal sequence (omit if order is irrelevant)
			JOIN member_names mn_lock_member_sbh2
			  ON mn_lock_member_sbh2.id = lock_member_sbh2.member_id
			 AND mn_lock_member_sbh2.name = 'bd_mutex'

			WHERE fn_bl.fn IS NULL
		)
	) ac

	LEFT JOIN locks_held lh
	  ON lh.txn_id = ac.txn_id
	LEFT JOIN locks l
	  ON l.id = lh.lock_id
			
	-- find out more about each held lock (allocation -> structs_layout
	-- member or contained-in member in case of a complex member)
	LEFT JOIN allocations lock_a
	  ON l.embedded_in = lock_a.id
	LEFT JOIN structs_layout_flat lock_member
	  ON lock_a.type = lock_member.type_id
	 AND l.ptr - lock_a.ptr = lock_member.helper_offset
	-- lock_a.id IS NULL                         => not embedded
	-- l.ptr - lock_a.ptr = lock_member.offset   => the lock is exactly this member (or at the beginning of a complex sub-struct)
	-- else                                      => the lock is contained in this member, exact name unknown
	JOIN member_names mn_lock
	  ON mn_lock.id = lock_member.member_id
	GROUP BY ac.id
) all_counterexamples
GROUP BY instrptr, locks_held
ORDER BY instrptr, occurrences
;

-- delme
SELECT fn, instrptr, locks_held, COUNT(*) AS occurrences
FROM
(
	SELECT ac.fn, CONCAT('0x', HEX(ac.instrptr)) AS instrptr,
	GROUP_CONCAT(
		CASE
		WHEN l.embedded_in IS NULL THEN CONCAT(l.id, '(', l.type, ')') -- global (or embedded in unknown allocation)
		WHEN l.embedded_in IS NOT NULL AND l.embedded_in = ac.alloc_id
			THEN CONCAT('EMBSAME(', IF(l.ptr - lock_a.ptr = lock_member.offset, mn_lock.name, CONCAT(mn_lock.name, '?')), ')') -- embedded in same
		ELSE CONCAT('EMB:', l.id, '(',  IF(l.ptr - lock_a.ptr = lock_member.offset, mn_lock.name, CONCAT(mn_lock.name, '?')), ')') -- embedded in other
		END
		ORDER BY lh.start
		SEPARATOR ' -> '
	) AS locks_held

	FROM

	(
		SELECT ac.id, ac.txn_id, ac.alloc_id, ac.fn, ac.instrptr
		FROM accesses ac
		JOIN allocations a
		  ON ac.alloc_id = a.id
		 AND ac.type = 'r'
		JOIN data_types dt
		  ON a.type = dt.id
		 AND dt.name = 'inode'
		JOIN structs_layout_flat sl
		  ON sl.type_id = a.type
		 AND sl.helper_offset = ac.address - a.ptr
		JOIN member_names mn
		  ON mn.id = sl.member_id
		 AND mn.name = 'i_blocks'
		LEFT JOIN function_blacklist fn_bl
		  ON fn_bl.datatype_id = a.type
		 AND fn_bl.fn = ac.fn
		 AND (fn_bl.datatype_member_id IS NULL OR fn_bl.datatype_member_id = sl.member_id)
		WHERE fn_bl.fn IS NULL
		-- AND ac.id NOT IN	-- either counterexample ...
		AND ac.id IN		-- ... or example
		(
			SELECT ac.id
			FROM accesses ac
			JOIN allocations a
			  ON ac.alloc_id = a.id
			 AND ac.type = 'r'
			JOIN data_types dt
			  ON a.type = dt.id
			 AND dt.name = 'inode'
			JOIN structs_layout_flat sl
			  ON sl.type_id = a.type
			 AND sl.helper_offset = ac.address - a.ptr
			JOIN member_names mn
			  ON mn.id = sl.member_id
			 AND mn.name = 'i_blocks'
			LEFT JOIN function_blacklist fn_bl
			  ON fn_bl.datatype_id = a.type
			 AND fn_bl.fn = ac.fn
			 AND (fn_bl.datatype_member_id IS NULL OR fn_bl.datatype_member_id = sl.member_id)

			-- 1st lock
--			JOIN locks_held lh_sbh1
--			  ON lh_sbh1.txn_id = ac.txn_id
--			 AND lh_sbh1.lock_id = 5780

			-- 2nd lock
			JOIN locks_held lh_sbh2 -- sbh = ShouldBeHeld
			  ON lh_sbh2.txn_id = ac.txn_id
			JOIN locks l_sbh2
			  ON l_sbh2.id = lh_sbh2.lock_id
			JOIN allocations l_sbh_a2
			  ON l_sbh2.embedded_in = l_sbh_a2.id
			JOIN structs_layout_flat lock_member_sbh2
			  ON l_sbh_a2.type = lock_member_sbh2.type_id
			 AND l_sbh2.ptr - l_sbh_a2.ptr = lock_member_sbh2.helper_offset
--			 AND lh_sbh2.start > lh_sbh1.start -- temporal sequence (omit if order is irrelevant)
			JOIN member_names mn_lock_member_sbh2
			  ON mn_lock_member_sbh2.id = lock_member_sbh2.member_id
			 AND mn_lock_member_sbh2.name = 'i_mutex'

			WHERE fn_bl.fn IS NULL
		)
	) ac

	LEFT JOIN locks_held lh
	  ON lh.txn_id = ac.txn_id
	LEFT JOIN locks l
	  ON l.id = lh.lock_id
			
	-- find out more about each held lock (allocation -> structs_layout
	-- member or contained-in member in case of a complex member)
	LEFT JOIN allocations lock_a
	  ON l.embedded_in = lock_a.id
	LEFT JOIN structs_layout_flat lock_member
	  ON lock_a.type = lock_member.type_id
	 AND l.ptr - lock_a.ptr = lock_member.helper_offset
	-- lock_a.id IS NULL                         => not embedded
	-- l.ptr - lock_a.ptr = lock_member.offset   => the lock is exactly this member (or at the beginning of a complex sub-struct)
	-- else                                      => the lock is contained in this member, exact name unknown
	JOIN member_names mn_lock
	  ON mn_lock.id = lock_member.member_id
	GROUP BY ac.id
) all_counterexamples
GROUP BY instrptr, locks_held
ORDER BY instrptr, occurrences
;
