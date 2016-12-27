-- which data-structure members are accessed most?
SELECT dt.name, sl.offset, sl.size, sl.type, sl.member, COUNT(*) AS accesscount
FROM accesses ac
JOIN allocations a
 ON ac.alloc_id = a.id
JOIN data_types dt
 ON dt.id = a.type
LEFT JOIN structs_layout sl
 ON a.type = sl.type_id
AND ac.address - a.ptr BETWEEN sl.offset AND sl.offset + sl.size - 1
GROUP BY dt.id, sl.offset
ORDER BY dt.name, accesscount
;
