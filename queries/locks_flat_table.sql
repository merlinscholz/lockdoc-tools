SELECT l.id AS lock_id, l.sub_lock AS sub_lock, a.id AS alloc_id, sl.member_name_id AS member_name_id
INTO TABLE locks_flat
FROM locks AS l
JOIN allocations AS a
  ON l.embedded_in = a.id
JOIN subclasses AS sc
  ON a.subclass_id = sc.id
JOIN structs_layout_flat AS sl
  ON sc.data_type_id = sl.data_type_id
 AND l.address - a.base_address = sl.helper_offset;
CREATE UNIQUE INDEX locks_flat_pkey ON locks_flat (lock_id);
CREATE INDEX locks_flat_idx1 ON public.locks_flat USING btree (lock_id, sub_lock, member_name_id);
CREATE INDEX locks_flat_idx2 ON public.locks_flat USING btree (lock_id, sub_lock);
CREATE INDEX locks_flat_idx3 ON public.locks_flat USING btree (lock_id, sub_lock, member_name_id);
CREATE INDEX locks_flat_idx4 ON public.locks_flat USING btree (sub_lock);
CREATE INDEX locks_flat_idx5 ON public.locks_flat USING btree (sub_lock, alloc_id);
