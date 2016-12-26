-- which data types are accessed most?
SELECT dt.name, COUNT(*) AS accesscount
FROM data_types dt
JOIN allocations a
ON dt.id = a.type
JOIN accesses ac
ON ac.alloc_id = a.id join structs_layout sl
ON a.type = sl.type_id and sl.offset = ac.address - a.ptr
GROUP BY dt.id
ORDER BY accesscount
;
