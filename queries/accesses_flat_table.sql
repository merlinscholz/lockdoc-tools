SELECT ac.id AS ac_id, ac.txn_id, ac.alloc_id, ac.type AS ac_type, ac.context, a.subclass_id, dt.id AS data_type_id, ac.stacktrace_id, mn.id AS member_name_id, sl.byte_offset
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
  ON mn.id = sl.member_name_id
WHERE
NOT EXISTS
(
	SELECT
	FROM member_blacklist m_bl
	WHERE m_bl.subclass_id = a.subclass_id
	 AND m_bl.member_name_id = sl.member_name_id
)
AND NOT EXISTS
(
	SELECT
	FROM  stacktraces AS s_st
	INNER JOIN function_blacklist s_fn_bl
	  ON s_fn_bl.fn = s_st.function
	WHERE ac.stacktrace_id = s_st.id
	AND 
	(
		(
		      (s_fn_bl.subclass_id IS NULL  AND s_fn_bl.member_name_id IS NULL) -- globally blacklisted function
		      OR
		      (s_fn_bl.subclass_id = a.subclass_id AND s_fn_bl.member_name_id IS NULL) -- for this data type blacklisted
		      OR
		      (s_fn_bl.subclass_id = a.subclass_id AND s_fn_bl.member_name_id = sl.member_name_id) -- for this member blacklisted
		)
		AND
		(s_fn_bl.sequence IS NULL OR s_fn_bl.sequence = s_st.sequence) -- for functions that appear at a certain position within the trace
	 )
);

CREATE INDEX accesses_flat_pkey ON accesses_flat (ac_id);
CREATE INDEX accesses_flat_filter_idx ON accesses_flat (ac_type, data_type_id, member_name_id);
