-- which data types are accessed most?
SELECT dt.name, COUNT(*) AS accesscount
FROM accesses ac
JOIN allocations a
 ON ac.alloc_id = a.id
JOIN data_types dt
 ON dt.id = a.type
GROUP BY dt.id
ORDER BY dt.name, accesscount
;
