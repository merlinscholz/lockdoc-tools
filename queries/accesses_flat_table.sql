SELECT ac.id AS ac_id, ac.txn_id, ac.alloc_id, ac.type AS ac_type, a.subclass_id, dt.id AS data_type_id, ac.stacktrace_id, mn.id AS member_name_id, sl.byte_offset
INTO TABLE accesses_flat
FROM accesses ac
JOIN allocations a
  ON ac.alloc_id = a.id
JOIN subclasses sc
  ON a.subclass_id = sc.id
JOIN data_types dt
  ON sc.data_type_id = dt.id
JOIN structs_layout_flat sl
  ON sl.data_type_id = sc.data_type_id
 AND sl.helper_offset = ac.address - a.base_address
JOIN member_names mn
  ON mn.id = sl.member_name_id;
CREATE INDEX accesses_flat_pkey ON accesses_flat (ac_id);
CREATE INDEX accesses_flat_filter_idx ON accesses_flat (ac_type, data_type_id, member_name_id);
