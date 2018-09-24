CREATE TABLE `accesses` (
  `id` int(11) UNSIGNED NOT NULL,		-- an unique id identifying a particular access
  `alloc_id` int(11) UNSIGNED NOT NULL,		-- references the memory area which is accessed by this event
  `txn_id` int(11) UNSIGNED DEFAULT NULL,	-- references the transaction this access occurs in (NULL for none)
  `ts` bigint(20) DEFAULT NULL,
  `type` enum('r','w') NOT NULL,		-- Defines the event type, read vs. write access
  `size` tinyint(11) UNSIGNED NOT NULL,		-- How many bytes were written?
  `address` int(11) UNSIGNED NOT NULL,		-- The start address of this access
  `stacktrace_id` int(11) UNSIGNED NOT NULL,		-- References a stacktrace
  `preemptcount` int(11) UNSIGNED DEFAULT NULL,		-- current __preempt_count pointer when the access happened
  PRIMARY KEY (`id`),
  KEY `fk_alloc_id` (`alloc_id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

CREATE TABLE `locks` (
  `id` int(11) UNSIGNED NOT NULL,		-- identifies a particular lock
  `address` int(11) UNSIGNED NOT NULL,		-- the address of the lock variable
  `embedded_in` int(11) DEFAULT NULL,		-- allocations.id of the allocation this lock belongs to (or NULL if static/global)
  `lock_type_name` varchar(255) DEFAULT NULL,		-- describes the lock type
  `sub_lock` enum('r','w') NOT NULL,
  `lock_var_name` varchar(255) DEFAULT NULL,		-- the variable name of the global lock.
  `flags` int(11) UNSIGNED NOT NULL,		-- Specifies options for a particular lock, e.g., beeing recursive
  PRIMARY KEY (`id`),
  KEY `embedded_in` (`embedded_in`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

CREATE TABLE `allocations` (
  `id` int(11) UNSIGNED NOT NULL,		-- identifies a certain allocation
  `subclass_id` int(11) UNSIGNED NOT NULL,		-- describes the data type of an allocation. References table subclasses
  `base_address` int(11) UNSIGNED NOT NULL,		-- the start address of an allocation
  `size` int(11) UNSIGNED NOT NULL,		-- size of the memory area
  `start` bigint(20) UNSIGNED DEFAULT NULL,	-- Start of lifetime
  `end` bigint(20) UNSIGNED DEFAULT NULL,	-- End of life
  PRIMARY KEY (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

CREATE TABLE `locks_held` (			-- Normally, there are several entries for one TXN. That means those locks were held during this TXN.
  `txn_id` int(11) UNSIGNED NOT NULL,		-- References the TXN
  `lock_id` int(11) UNSIGNED NOT NULL,		-- References a lock which was held during the access
  `start` bigint(20) DEFAULT NULL,		-- The timestamp when the lock was acquired
  `last_file` varchar(255) DEFAULT NULL,		-- Last file
  `last_line` int(11) UNSIGNED DEFAULT NULL,	-- Last line 
  `last_fn` varchar(255) DEFAULT NULL,		-- Last function where the lock has been acquired from
  `last_preempt_count` int(11) UNSIGNED DEFAULT NULL,	-- Value of preemptcount() after the lock has been acquired
  `last_irq_sync` enum('LOCK_NONE', 'LOCK_IRQ', 'LOCK_IRQ_NESTED', 'LOCK_BH') NOT NULL,		-- Denotes the irq synchronization used
  PRIMARY KEY (`lock_id`,`txn_id`),
  KEY `fk_txn_id` (`txn_id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

CREATE TABLE `txns` (				-- Transactions: sets of memory accesses between a P/V and the next P/V (while at least one lock is held)
  `id` int(11) UNSIGNED NOT NULL,		-- ID
  `start` bigint(20) DEFAULT NULL,		-- Timestamp of the first P or V
  `end` bigint(20) DEFAULT NULL,		-- Timestamp of the second P or V
  PRIMARY KEY (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

CREATE TABLE `structs_layout` (
  `data_type_id` int(11) UNSIGNED NOT NULL,		-- Refers to the datatype to which a member belongs to
  `data_type_name` varchar(255) NOT NULL,			-- Describes the type of a member
  `member_name_id` int(11) UNSIGNED NOT NULL,		-- The id of the member
  `offset` smallint(11) UNSIGNED NOT NULL,	-- The offset in bytes from the beginning of a struct
  `size` smallint(11) UNSIGNED NOT NULL,	-- The size in bytes of a member
  KEY (`data_type_id`, `offset`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

CREATE TABLE `data_types` (
  `id` int(11) UNSIGNED NOT NULL,		-- An unique id identifying a data type
  `name` varchar(255) NOT NULL,			-- A humand-readable id of a datat type
  PRIMARY KEY (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

CREATE TABLE `subclasses` (
  `id` int(11) UNSIGNED NOT NULL,		-- An unique id identifying a data type
  `data_type_id` int(11) UNSIGNED NOT NULL,		-- Refers to the datatype to which a member belongs to
  `name` varchar(255) DEFAULT NULL,			-- A humand-readable id of a datat type
  PRIMARY KEY (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

CREATE TABLE `member_names` (
  `id` int(11) UNSIGNED NOT NULL,		-- An unique id identifying a member name
  `name` varchar(255) NOT NULL,			-- A humand-readable id of a member name
  PRIMARY KEY (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

CREATE TABLE `stacktraces` (
  `id` int(11) UNSIGNED NOT NULL,		-- An unique id identifying a stacktrace
  `sequence` int(11) NOT NULL,			-- The n-th stackframe
  `instruction_ptr` int(11) UNSIGNED NOT NULL,			-- The instruction pointer
  `instruction_ptr_prev` int(11) UNSIGNED NOT NULL,			-- The instruction pointer
  `function` varchar(80) NOT NULL,			-- Function name corresponding to the instruction pointer
  `line` int(11) DEFAULT NULL,			-- A line corresponding to the instruction pointer
  `file` varchar(255) DEFAULT NULL,			-- The filename corresponding to the instruction pointer
  PRIMARY KEY (`id`, `sequence`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

CREATE TABLE `function_blacklist` (			-- A per datatype list of blacklisted functions. We want to ignore memory accesses from these functions.
  `id` int(11) NOT NULL AUTO_INCREMENT,		-- Refers to a data type
  `subclass_id` int(11) unsigned DEFAULT NULL,
  `member_name_id` int(11) DEFAULT NULL,
  `fn` varchar(80) NOT NULL,		-- The function name (aka resolved instruction pointer) which we want to ignore
  PRIMARY KEY (`id`),
  UNIQUE KEY `fn_bl_entry` (`subclass_id`,`member_name_id`,`fn`),
  KEY `fn_class_idx` (`fn`,`subclass_id`)
) ENGINE=MyISAM AUTO_INCREMENT=1 DEFAULT CHARSET=utf8
;

CREATE TABLE `member_blacklist` (			-- A per datatype list of blacklisted functions. We want to ignore memory accesses from these functions.
  `subclass_id` int(11) UNSIGNED NOT NULL,		-- Refers to a data type
  `member_name_id` int(11) DEFAULT NULL,
  PRIMARY KEY (`subclass_id`,`member_name_id`),
  KEY `fk_datatype_id` (`subclass_id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

