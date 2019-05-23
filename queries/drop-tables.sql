drop table if exists locks_embedded_flat, accesses_flat, accesses,allocations,data_types,locks,locks_held,structs_layout,txns,member_names,function_blacklist,member_blacklist,stacktraces,subclasses;
drop type if exists access_type, sub_lock_type, irq_sync_type;
drop sequence if exists function_blacklist_seq;
