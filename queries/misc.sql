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
