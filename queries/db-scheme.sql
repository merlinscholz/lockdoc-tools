CREATE TABLE `accesses` (
  `id` int(11) UNSIGNED NOT NULL,		-- an unique id identifying a particular access
  `alloc_id` int(11) UNSIGNED NOT NULL,		-- references the memory area which is accessed by this event
  `txn_id` int(11) UNSIGNED DEFAULT NULL,	-- references the transaction this access occurs in (NULL for none)
  `ts` bigint(20) DEFAULT NULL,
  `type` enum('r','w') NOT NULL,		-- Defines the event type, read vs. write access
  `size` tinyint(11) UNSIGNED NOT NULL,		-- How many bytes were written?
  `address` int(11) UNSIGNED NOT NULL,		-- The start address of this access
  `stackptr` int(11) UNSIGNED NOT NULL,		-- stack pointer
  `instrptr` int(11) UNSIGNED NOT NULL,		-- current instruction pointer when the access happened
  `fn` varchar(255) DEFAULT NULL,		-- The function name (aka resolved instruction pointer)
  PRIMARY KEY (`id`),
  KEY `fk_alloc_id` (`alloc_id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

CREATE TABLE `locks` (
  `id` int(11) UNSIGNED NOT NULL,		-- identifies a particular lock
  `ptr` int(11) UNSIGNED NOT NULL,		-- the address of the lock variable
  `embedded_in` int(11) DEFAULT NULL,		-- allocations.id of the allocation this lock belongs to (or NULL if static/global)
  `type` varchar(255) DEFAULT NULL,		-- describes the lock type
  PRIMARY KEY (`id`),
  KEY `embedded_in` (`embedded_in`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

CREATE TABLE `allocations` (
  `id` int(11) UNSIGNED NOT NULL,		-- identifies a certain allocation
  `type` int(11) UNSIGNED NOT NULL,		-- describes the data type of an allocation. References table datatypes
  `ptr` int(11) UNSIGNED NOT NULL,		-- the start address of an allocation
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
  `lastFile` varchar(255) DEFAULT NULL,		-- Last file
  `lastLine` int(11) UNSIGNED DEFAULT NULL,	-- Last line 
  `lastFn` varchar(255) DEFAULT NULL,		-- Last function where the lock has been acquired from
  `lastPreemptCount` int(11) UNSIGNED DEFAULT NULL,	-- Value of preemptcount() after the lock has been acquired
  `lastLockFn` varchar(255) DEFAULT NULL,
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
  `type_id` int(11) UNSIGNED NOT NULL,		-- Refers to the datatype to which a member belongs to
  `type` varchar(255) NOT NULL,			-- Describes the type of a member
  `member_id` int(11) UNSIGNED NOT NULL,		-- The id of the member
  `offset` smallint(11) UNSIGNED NOT NULL,	-- The offset in bytes from the beginning of a struct
  `size` smallint(11) UNSIGNED NOT NULL,	-- The size in bytes of a member
  KEY (`type_id`, `offset`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

CREATE TABLE `data_types` (
  `id` int(11) UNSIGNED NOT NULL,		-- An unique id identifying a data type
  `name` varchar(255) NOT NULL,			-- A humand-readable id of a datat type
  PRIMARY KEY (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

CREATE TABLE `member_names` (
  `id` int(11) UNSIGNED NOT NULL,		-- An unique id identifying a member name
  `name` varchar(255) NOT NULL,			-- A humand-readable id of a member name
  PRIMARY KEY (`id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

CREATE TABLE `function_blacklist` (			-- A per datatype list of blacklisted functions. We want to ignore memory accesses from these functions.
  `datatype_id` int(11) UNSIGNED NOT NULL,		-- Refers to a data type
  `datatype_member_id` int(11) DEFAULT NULL,
  `fn` varchar(80) NOT NULL,		-- The function name (aka resolved instruction pointer) which we want to ignore
  PRIMARY KEY (`datatype_id`,`fn`),
  KEY `fk_datatype_id` (`datatype_id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;

CREATE TABLE `member_blacklist` (			-- A per datatype list of blacklisted functions. We want to ignore memory accesses from these functions.
  `datatype_id` int(11) UNSIGNED NOT NULL,		-- Refers to a data type
  `datatype_member_id` int(11) DEFAULT NULL,
  PRIMARY KEY (`datatype_id`,`datatype_member_id`),
  KEY `fk_datatype_id` (`datatype_id`)
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;


-- FIXME which of these columns may really become NULL?
CREATE TABLE `lock_symbols` (
  `ac_id` int(11) UNSIGNED DEFAULT NULL,
  `alloc_id` int(11) UNSIGNED NOT NULL,
  `type` enum('r','w') DEFAULT NULL,
  `data_type` int(11) UNSIGNED DEFAULT NULL,
  `locks` varchar(255) DEFAULT NULL,
  `lock_types` varchar(255) DEFAULT NULL,
  `embedded_in_type` varchar(255) DEFAULT NULL,
  `embedded_in_same` varchar(255) DEFAULT NULL,
  `offset` tinyint(11) DEFAULT NULL,
  `size` tinyint(11) DEFAULT NULL,
  `member` varchar(255) DEFAULT NULL,
  `pos` varchar(400) DEFAULT NULL,
  `preemptcount` varchar(255) DEFAULT NULL,
  `ac_fn` varchar(255) DEFAULT NULL
) ENGINE=MyISAM DEFAULT CHARSET=utf8
;
