SELECT dt.name, sc.name, ac.type, ac.id
FROM accesses ac
JOIN allocations a
  ON ac.alloc_id = a.id
JOIN subclasses sc
  ON a.subclass_id = sc.id
JOIN data_types dt
  ON sc.data_type_id = dt.id
LEFT JOIN structs_layout_flat sl
  ON sl.data_type_id = sc.data_type_id
 AND sl.helper_offset = ac.address - a.base_address
LEFT JOIN member_names mn
  ON mn.id = sl.member_name_id
WHERE sl.byte_offset IS NULL OR mn.id IS NULL;
