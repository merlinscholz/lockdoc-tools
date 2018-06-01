-- Just check if the is consistent
SELECT *
FROM locks_held AS lh
LEFT JOIN accesses AS as ON as.id=lh.access_id
LEFT JOIN allocations AS a ON a.id=as.alloc_id
LEFT JOIN locks AS l ON l.id=lh.lock_id
LEFT JOIN data_types AS dt ON dt.id=a.type
WHERE as.id IS NULL OR a.id IS NULL OR l.id IS NULL OR dt.id IS NULL;

-- Get a distribution of locks held during a memory access for a certain type or instance
-- SELECT ls.alloc_id,ac.ts,dt.name,a.ptr,ls.type,locks,offset, embedded_in_type, embedded_in_same, count(*) as num,member,member_offset
SELECT ls.type, locks, embedded_in_type, embedded_in_same, count(*) as num, member
FROM lock_symbols AS ls
INNER JOIN allocations AS a ON a.id=ls.alloc_id
INNER JOIN accesses AS ac ON ac.id=ls.ac_id
INNER JOIN data_types AS dt ON dt.id=a.type
-- WHERE a.type=X
WHERE a.id = (SELECT id FROM data_types WHERE name = 'inode')
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
WHERE lh.start IS NULL -- AND a.id = (SELECT id FROM data_types WHERE name = 'inode');

-- Get accesses to $data_type.$member which would have been ignored
SELECT ac.id, ac.type AS type, a.type AS type_id, mn.name AS member, sl.offset,
	sl.size, st.sequence, st.function, m_bl.datatype_member_id IS NOT NULL AS member_bl, fn_bl.fn IS NOT NULL AS fn_bl
FROM accesses ac
JOIN allocations a
  ON ac.alloc_id = a.id
JOIN stacktraces AS st
  ON ac.stacktrace_id = st.id
-- AND st.sequence = 0
LEFT JOIN structs_layout_flat sl
  ON a.type = sl.type_id
 AND ac.address - a.ptr = sl.helper_offset
LEFT JOIN member_names mn
  ON mn.id = sl.member_id
LEFT JOIN member_blacklist m_bl
  ON m_bl.datatype_id = a.type
 AND m_bl.datatype_member_id = sl.member_id
LEFT JOIN function_blacklist fn_bl
  ON fn_bl.fn = st.function
 AND 
 (
   (fn_bl.data_type_id IS NULL  AND fn_bl.member_name_id IS NULL) -- globally blacklisted function
   OR
   (fn_bl.data_type_id = a.type AND fn_bl.member_name_id IS NULL) -- for this data type blacklisted
   OR
   (fn_bl.data_type_id = a.type AND fn_bl.member_name_id = sl.member_id) -- for this member blacklisted
 )
WHERE 1
AND a.type IN (SELECT id FROM data_types WHERE name = 'super_block')
AND mn.id IN (SELECT id FROM member_names WHERE name = 's_writers' OR name = 's_inode_list_lock')
-- === FOR NOW: skip task_struct ===
AND a.type != (SELECT id FROM data_types WHERE name = 'task_struct')
-- ====================================
AND 
(
	fn_bl.fn IS NOT NULL OR m_bl.datatype_member_id IS NOT NULL
)
