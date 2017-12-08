-- SELECT
-- 	ac.id AS ac_id,
--	ac.txn_id AS ac_txn_id,
--	ac.type AS ac_type,
--	ac.fn AS ac_fn,
--	mn.name AS sl_member,
--	sl.member_id AS sl_member_id,
--	COUNT(*) AS count
DELETE accesses
FROM accesses AS ac
INNER JOIN allocations AS a ON a.id=ac.alloc_id
INNER JOIN data_types AS dt ON dt.id=a.type
LEFT JOIN structs_layout_flat sl
  ON a.type = sl.type_id
 AND ac.address - a.ptr = sl.helper_offset
LEFT JOIN member_names AS mn
  ON mn.id = sl.member_id
WHERE
	sl.type like "%atomic_t%" or sl.type like "%atomic64_t*";
