SELECT ac_type, sl_member, locks_held, st_fn, st_instrptr, contexts, preemptCounts, COUNT(*) AS num
FROM
(
	SELECT
		ac_type,
		sl_member,
		GROUP_CONCAT(
		CASE
			WHEN l.embedded_in IS NULL AND l.lock_var_name IS NULL
				THEN CONCAT(l.id, '(', l.lock_type_name, '[', l.sub_lock, '])') -- global (or embedded in unknown allocation *and* no name available)
			WHEN l.embedded_in IS NULL AND l.lock_var_name IS NOT NULL
				THEN CONCAT(l.lock_var_name, ':', l.id, '(', l.lock_type_name, '[', l.sub_lock, '])') -- global (or embedded in unknown allocation *and* a name is available)
			WHEN l.embedded_in IS NOT NULL AND l.embedded_in = alloc_id
				THEN CONCAT('EMBSAME(', CONCAT(lock_a_dt.name, '.', IF(l.address - lock_a.base_address = lock_member.offset, mn_lock_member.name, CONCAT(mn_lock_member.name, '?'))), '[', l.sub_lock, '])') -- embedded in same
			ELSE CONCAT('EMBOTHER', '(',  CONCAT(lock_a_dt.name, '.', IF(l.address - lock_a.base_address = lock_member.offset, mn_lock_member.name, CONCAT(mn_lock_member.name, '?'))), '[', l.sub_lock, '])') -- embedded in other
--			ELSE CONCAT('EMB:', l.id, '(',  CONCAT(lock_a_dt.name, '.', IF(l.address - lock_a.base_address = lock_member.offset, mn_lock_member.name, CONCAT(mn_lock_member.name, '?'))), ')') -- embedded in other
			END
			ORDER BY lh.start
			SEPARATOR ' -> '
		) AS locks_held,
		st_fn,
		lower(hex(st_instrptr)) AS st_instrptr,
		GROUP_CONCAT(
			CASE 
			WHEN lh.lastPreemptCount & 0x0ff00 THEN 'softirq'
			WHEN lh.lastPreemptCount & 0xf0000 THEN 'hardirq'
			WHEN (lh.lastPreemptCount & 0xfff00) = 0 THEN 'noirq'
			WHEN lh.lastPreemptCount IS NULL THEN 'nolock'
			ELSE 'unknown'
			END
			ORDER BY lh.start
			SEPARATOR ' -> '
		) AS contexts, GROUP_CONCAT(lh.lastPreemptCount ORDER BY lh.start SEPARATOR ' -> ') AS preemptCounts
	FROM
	(
		-- Get all accesses. Add information about the accessed member, data type, and the function the memory has been accessed from.
		-- Filter out every function that is on our function blacklist.
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
		LEFT JOIN structs_layout_flat sl
		  ON a.data_type_id = sl.type_id
		 AND ac.address - a.base_address = sl.helper_offset
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
			-- Name the data type of interest here
			a.data_type_id in (SELECT id FROM data_types WHERE name in ('journal_t','transaction_t')) AND
			ac.type  IN ('r') AND -- Filter by access type
			sl.member_id IN (SELECT id FROM member_names WHERE name in ('j_barrier_count','j_running_transaction','t_reserved_list')) AND -- Only show results for a certain member
			fn_bl.fn IS NULL
		GROUP BY ac.id -- Remove duplicate entries. Some accesses might be mapped to more than one member, e.g., an union.
	) s
	LEFT JOIN locks_held AS lh
	  ON lh.txn_id=ac_txn_id
	LEFT JOIN locks AS l
	  ON l.id=lh.lock_id
	LEFT JOIN allocations AS lock_a
	  ON lock_a.id=l.embedded_in
	LEFT JOIN data_types lock_a_dt
	  ON lock_a.data_type_id = lock_a_dt.id
	LEFT JOIN structs_layout_flat lock_member
	  ON lock_a.data_type_id = lock_member.type_id
	  AND l.address - lock_a.base_address = lock_member.helper_offset
	JOIN member_names mn_lock_member
	  ON mn_lock_member.id = lock_member.member_id
	GROUP BY ac_id
) t
-- Since we want a detailed view about where an access happenend, the result is additionally grouped by ac_fn and st_instrptr.
GROUP BY ac_type, sl_member, locks_held, st_fn, st_instrptr
ORDER BY ac_type, sl_member, locks_held, st_fn, st_instrptr, num DESC;
