-- SELECT
-- 	ac.id AS ac_id,
--	ac.txn_id AS ac_txn_id,
--	ac.type AS ac_type,
--	ac.fn AS ac_fn,
--	mn.name AS sl_member,
--	sl.member_id AS sl_member_id,
--	COUNT(*) AS count
DELETE
FROM accesses AS ac
USING allocations AS a, data_types AS dt, structs_layout_flat AS sl
WHERE
	a.id = ac.alloc_id
	AND dt.id = a.data_type_id
	AND a.data_type_id = sl.data_type_id
	AND ac.address - a.base_address = sl.helper_offset
	AND (sl.data_type_name LIKE '%atomic\_t%' OR sl.data_type_name LIKE '%atomic64\_t*' OR sl.data_type_name LIKE '%atomic\_long\_t%')
