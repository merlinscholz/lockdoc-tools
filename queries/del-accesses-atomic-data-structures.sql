-- SELECT
-- 	ac.id AS ac_id,
--	ac.txn_id AS ac_txn_id,
--	ac.type AS ac_type,
--	ac.fn AS ac_fn,
--	mn.name AS sl_member,
--	sl.member_id AS sl_member_id,
--	COUNT(*) AS count
DELETE ac
FROM accesses AS ac
INNER JOIN allocations AS a ON a.id=ac.alloc_id
INNER JOIN data_types AS dt ON dt.id=a.data_type_id
LEFT JOIN structs_layout_flat sl
  ON a.data_type_id = sl.type_id
 AND ac.address - a.base_address = sl.helper_offset
LEFT JOIN member_names AS mn
  ON mn.id = sl.member_name_id
WHERE
	sl.data_type_name like "%atomic\_t%" or sl.data_type_name like "%atomic64\_t*" or sl.data_type_name like "%atomic\_long\_t%"
