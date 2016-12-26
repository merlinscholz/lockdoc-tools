-- which data-structure members are accessed most?
SELECT dt.name, sl.type, sl.member, COUNT(*) AS accesscount
FROM data_types dt
JOIN allocations a
ON dt.id = a.type
JOIN accesses ac
ON ac.alloc_id = a.id join structs_layout sl
ON a.type = sl.type_id and sl.offset = ac.address - a.ptr
GROUP BY dt.id, sl.type, sl.member
ORDER BY accesscount
;
