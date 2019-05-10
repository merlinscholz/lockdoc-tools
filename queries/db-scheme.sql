CREATE TYPE access_type AS enum('r','w');

CREATE TABLE data_types (
  id int CHECK (id > 0) NOT NULL,		-- An unique id identifying a data type
  name varchar(255) NOT NULL,			-- A humand-readable id of a datat type
  PRIMARY KEY (id)
) 
;


CREATE TABLE subclasses (
  id int CHECK(id > 0) NOT NULL,		-- An unique id identifying a data type
  data_type_id int CHECK(data_type_id > 0) NOT NULL,		-- Refers to the datatype to which a member belongs to
  name varchar(255) DEFAULT NULL,			-- A humand-readable id of a datat type
  PRIMARY KEY (id)
)
;


CREATE TABLE allocations (
  id int CHECK (id > 0) NOT NULL,		-- identifies a certain allocation
  subclass_id int NOT NULL,		-- describes the data type of an allocation. References table datatypes
  base_address bigint NOT NULL,		-- the start address of an allocation
  size int NOT NULL,		-- size of the memory area
  start_ts bigint DEFAULT NULL,	-- Start of lifetime
  end_ts bigint DEFAULT NULL,	-- End of life
  PRIMARY KEY (id)
) 
;



CREATE TABLE accesses (
  id bigint CHECK (id > 0) NOT NULL,		-- an unique id identifying a particular access
  alloc_id int CHECK (alloc_id > 0) NOT NULL,		-- references the memory area which is accessed by this event
  txn_id int CHECK (txn_id > 0) DEFAULT NULL,	-- references the transaction this access occurs in (NULL for none)
  ts bigint DEFAULT NULL,
  type access_type NOT NULL,		-- Defines the event type, read vs. write access
  size smallint CHECK (size > 0) NOT NULL,		-- How many bytes were written?
  address bigint CHECK (address > 0) NOT NULL,		-- The start address of this access
  stacktrace_id int CHECK (stacktrace_id > 0) NOT NULL,		-- References a stacktrace
  preemptcount bigint CHECK (preemptcount > 0) DEFAULT NULL,		-- current __preempt_count pointer when the access happened
  PRIMARY KEY (id)
) 
;
	
CREATE INDEX fk_alloc_id ON accesses (alloc_id, address);


CREATE TYPE sub_lock_type AS enum('r', 'w');
CREATE TABLE locks (
  id int CHECK (id > 0) NOT NULL,		-- identifies a particular lock
  address bigint CHECK (address > 0) NOT NULL,		-- the address of the lock variable
  embedded_in int DEFAULT NULL,		-- allocations.id of the allocation this lock belongs to (or NULL if static/global)
  lock_type_name varchar(255) DEFAULT NULL,		-- describes the lock type
  sub_lock sub_lock_type NOT NULL,
  lock_var_name varchar(255) DEFAULT NULL,		-- the variable name of the global lock.
  flags int NOT NULL,
  PRIMARY KEY (id)
) 
;

CREATE INDEX embedded_in ON locks (embedded_in);


CREATE TYPE irq_sync_type AS enum('LOCK_NONE', 'LOCK_IRQ', 'LOCK_IRQ_NESTED', 'LOCK_BH');
CREATE TABLE locks_held (			-- Normally, there are several entries for one TXN. That means those locks were held during this TXN.
  txn_id int CHECK (txn_id > 0) NOT NULL,		-- References the TXN
  lock_id int CHECK (lock_id > 0) NOT NULL,		-- References a lock which was held during the access
  start bigint DEFAULT NULL,		-- The timestamp when the lock was acquired
  last_file varchar(255) DEFAULT NULL,		-- Last file
  last_line int CHECK (last_line > 0) DEFAULT NULL,	-- Last line 
  last_fn varchar(255) DEFAULT NULL,		-- Last function where the lock has been acquired from
  last_preempt_count bigint CHECK (last_preempt_count >= 0) DEFAULT NULL,	-- Value of preemptcount() after the lock has been acquired
  last_irq_sync irq_sync_type NOT NULL,		-- Denotes the irq synchronization used
  PRIMARY KEY (lock_id,txn_id)
) 
;

CREATE INDEX fk_txn_id ON locks_held (txn_id);

CREATE TABLE txns (				-- Transactions: sets of memory accesses between a P/V and the next P/V (while at least one lock is held)
  id int CHECK (id > 0) NOT NULL,		-- ID
  start_ts bigint DEFAULT NULL,		-- Timestamp of the first P or V
  end_ts bigint DEFAULT NULL,		-- Timestamp of the second P or V
  PRIMARY KEY (id)
) 
;

CREATE TABLE structs_layout (
  data_type_id int CHECK (data_type_id > 0) NOT NULL,		-- Refers to the datatype to which a member belongs to
  data_type_name varchar(255) NOT NULL,			-- Describes the type of a member
  member_name_id int CHECK (member_name_id > 0) NOT NULL,		-- The id of the member
  byte_offset smallint CHECK (byte_offset >= 0) NOT NULL,	-- The offset in bytes from the beginning of a struct
  size int CHECK (size > 0) NOT NULL,	-- The size in bytes of a member
  PRIMARY KEY (data_type_id, byte_offset)
) 
;

CREATE INDEX sl_idx ON structs_layout(data_type_id, data_type_name, byte_offset);

CREATE INDEX sl_pattern_idx ON structs_layout (data_type_name varchar_pattern_ops);


CREATE TABLE member_names (
  id int CHECK (id > 0) NOT NULL,		-- An unique id identifying a member name
  name varchar(255) NOT NULL,			-- A humand-readable id of a member name
  PRIMARY KEY (id)
) 
;

CREATE TABLE stacktraces (
  id int CHECK (id > 0) NOT NULL,		-- An unique id identifying a stacktrace
  sequence int NOT NULL,			-- The n-th stackframe
  instruction_ptr bigint CHECK (instruction_ptr > 0) NOT NULL,			-- The instruction pointer
  instruction_ptr_prev bigint CHECK(instruction_ptr_prev > 0) NOT NULL,
  function varchar(80) NOT NULL,			-- Function name corresponding to the instruction pointer
  line int DEFAULT NULL,			-- A line corresponding to the instruction pointer
  file varchar(255) DEFAULT NULL,			-- The filename corresponding to the instruction pointer
  PRIMARY KEY (id, sequence)
) 
;

-- CREATE SEQUENCE function_blacklist_seq;

CREATE TABLE function_blacklist (			-- A per datatype list of blacklisted functions. We want to ignore memory accesses from these functions.
--  id int NOT NULL DEFAULT NEXTVAL ('function_blacklist_seq'),		-- Refers to a data type
  id serial NOT NULL,
  subclass_id int check (subclass_id > 0) DEFAULT NULL,
  member_name_id int DEFAULT NULL,
  fn varchar(80) NOT NULL,		-- The function name (aka resolved instruction pointer) which we want to ignore
  sequence int DEFAULT NULL,		-- The position in the stack trace to which an entry should be applied to
  PRIMARY KEY (id),
  CONSTRAINT fn_bl_entry UNIQUE  (subclass_id,member_name_id,fn)
)  
;

-- ALTER SEQUENCE function_blacklist_seq RESTART WITH 1;

CREATE TABLE member_blacklist (			-- A per datatype list of blacklisted functions. We want to ignore memory accesses from these functions.
  subclass_id int CHECK (subclass_id > 0) NOT NULL,		-- Refers to a data type
  member_name_id int DEFAULT NULL,
  PRIMARY KEY (subclass_id,member_name_id)
) 
;

CREATE INDEX fk_subclass_id ON member_blacklist (subclass_id);

