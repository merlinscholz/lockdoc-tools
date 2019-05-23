#!/bin/bash

#
# This script generates a parametrized SQL statement that searches for
# memory accesses (and corresponding Linux-kernel code locations) that
# serve as [counter]examples to a specified locking rule.  It shows memory
# accesses where the specified set or sequence of locks is [not] held.
#
# Use hypothesizer --bugsql to automatically generate parameter lists for this
# script.
#

if [ $# -lt 5 ]; then
	cat >&2 <<-EOT
	usage: $0 DATATYPE ACCESSTYPE:MEMBER MODE LOCKCONNECTOR LOCK1 [ LOCK2 [ LOCK3 [ ... ] ] ]

	DATATYPE       Data type as shown by the hypothesizer tool (e.g., 'inode')
	ACCESSTYPE     Memory-access type: r or w
	MEMBER         Member name
	MODE           EX = Example, CEX = Counterexample
	LOCKCONNECTOR  ANY = Arbitrary lock order in observation, SEQ = Lock order in this exact sequence
	LOCK1, ...     Locks as shown by the hypothesizer tool (e.g., EMBSAME(i_mutex))

	If the environment variable USE_EMBOTHER is 1, the results simply show EMBOTHER() instead of EMB:XXX().
	example:
	$0 block_device r:bd_invalidated CEX SEQ 'EMB:5779(foo)' 'EMBSAME(bd_mutex)'
	or
	USE_EMBOTHER=1 $0 block_device r:bd_invalidated CEX SEQ 'EMB:5779(foo)' 'EMBSAME(bd_mutex)'

	(Use hypothesizer --bugsql to automatically generate parameter lists
	for this script, and pipe its output, e.g., to "mysql -t" or "mysql -b".)
	EOT
	exit 1
fi

COMBINED_DATATYPE=$1; shift
COMBINED_MEMBER=$1; shift
MODE=$1; shift
LOCKCONNECTOR=$1; shift

RET=`echo ${COMBINED_DATATYPE} | grep -q ":"`
if [ ${?} -eq 0 ];
then
	DATATYPE=`echo ${COMBINED_DATATYPE} | cut -d ":" -f1`
	SUBCLASS=`echo ${COMBINED_DATATYPE} | cut -d ":" -f2`
	SUBCLASS_FILTER=" AND sc.name = '${SUBCLASS}'"
	SUBCLASS_FILTER_SUB=" AND s_sc.name = '${SUBCLASS}'"
	LOCKNAME_FORMAT="(CASE WHEN lock_sc.name IS NULL THEN lock_dt.name ELSE CONCAT(lock_dt.name, ':', lock_sc.name) END)"
else
	DATATYPE=${COMBINED_DATATYPE}
	SUBCLASS_FILTER=""
	SUBCLASS_FILTER_SUB=""
	LOCKNAME_FORMAT="lock_dt.name"
fi

ACCESSTYPE=${COMBINED_MEMBER:0:1}
SANITYCHECK=${COMBINED_MEMBER:1:1}
MEMBER=${COMBINED_MEMBER:2}

if [ "$SANITYCHECK" != : ]; then
	echo "error: 2nd parameter not in ACCESSTYPE:MEMBER form" >&2
	exit 1
fi

EMBOTHER_SQL="ELSE CONCAT('EMB:', l.id, '(', ${LOCKNAME_FORMAT}, '.', (CASE WHEN l.address - lock_a.base_address = lock_member.byte_offset THEN lock_member_name.name ELSE CONCAT(lock_member_name.name, '?') END), '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- embedded in other"
if [ -n "${USE_EMBOTHER}" ];
then
	if [ ${USE_EMBOTHER} -gt 0 ];
	then
		EMBOTHER_SQL="ELSE CONCAT('EMBOTHER', '(', ${LOCKNAME_FORMAT}, '.',  (CASE WHEN l.address - lock_a.base_address = lock_member.byte_offset THEN lock_member_name.name ELSE CONCAT(lock_member_name.name, '?') END), '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- embedded in other"
	fi
fi

cat <<EOT
SELECT data_type, member, accesstype, stacktrace, 
	string_agg(
		CONCAT(locks_held, '#', occurrences)
	, '+' ORDER BY occurrences DESC, locks_held DESC) AS locks_held
FROM
(
	SELECT '${COMBINED_DATATYPE}' AS data_type, '${MEMBER}' AS member, '${ACCESSTYPE}' AS accesstype, COUNT(*) AS occurrences, 
			stacktrace_id, stacktrace, (CASE WHEN (locks_held IS NULL OR locks_held = '([])@@:') THEN 'nolocks' ELSE locks_held END) AS locks_held
	FROM
	(
		SELECT stacktrace_id, stacktrace,
		string_agg(
			CASE
			WHEN l.embedded_in IS NULL AND l.lock_var_name IS NULL
				THEN CONCAT(l.id, '(', l.lock_type_name, '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- global (or embedded in unknown allocation *and* no name available)
			WHEN l.embedded_in IS NULL AND l.lock_var_name IS NOT NULL
				THEN CONCAT(l.lock_var_name, ':', l.id, '(', l.lock_type_name, '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- global (or embedded in unknown allocation *and* a name is available)
			WHEN l.embedded_in IS NOT NULL AND l.embedded_in = alloc_id
				THEN CONCAT('EMBSAME(', ${LOCKNAME_FORMAT}, '.', (CASE WHEN l.address - lock_a.base_address = lock_member.byte_offset THEN lock_member_name.name ELSE CONCAT(lock_member_name.name, '?') END), '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- embedded in same
				${EMBOTHER_SQL}
			END,
		',' ORDER BY lh.start) AS locks_held
		FROM
		(
			SELECT ac.ac_id, ac.alloc_id AS alloc_id, ac.txn_id AS txn_id, stacktrace_id,
				string_agg(
					CONCAT('0x', upper(to_hex(st.instruction_ptr)), '@', st.function, '@', st.file, ':', st.line)
				,',' ORDER BY st.sequence) AS stacktrace
			FROM
			(
				SELECT ac.ac_id, ac.txn_id, ac.alloc_id, st.function, ac.stacktrace_id
				FROM accesses_flat ac
				JOIN subclasses sc
				  ON sc.id = ac.subclass_id
				${SUBCLASS_FILTER}
				JOIN data_types dt
				  ON ac.data_type_id = dt.id
				 AND dt.name = '$DATATYPE'
				JOIN stacktraces AS st
				  ON ac.stacktrace_id = st.id
				 AND st.sequence = 0
				JOIN member_names mn
				  ON mn.id = ac.member_name_id
				 AND mn.name = '$MEMBER'
				WHERE
				ac.ac_type = '$ACCESSTYPE' AND
EOT
if [ $MODE = CEX ]; then
	cat <<EOT
				ac.ac_id NOT IN	-- counterexample
EOT
elif [ $MODE = EX ]; then
	cat <<EOT
				ac.ac_id IN		-- example
EOT
else
	echo "error: unknown MODE $MODE" >&2
	exit 1
fi
cat <<EOT
				(
					SELECT s_ac.ac_id
					FROM accesses_flat s_ac
					JOIN subclasses s_sc
					  ON s_sc.id = s_ac.subclass_id
					${SUBCLASS_FILTER_SUB}
					JOIN data_types s_dt
					  ON s_ac.data_type_id = s_dt.id
					 AND s_dt.name = '$DATATYPE'
					JOIN member_names s_mn
					  ON s_mn.id = s_ac.member_name_id
					 AND s_mn.name = '$MEMBER'
EOT

LOCKNR=1
LAST_EMBOTHER=0
for LOCK in "$@"; do
	if echo $LOCK | grep -q '^EMBSAME'; then # e.g., EMBSAME(i_mutex)
		LOCKCOMBINEDDT=$(echo $LOCK | sed -e 's/^.*(\([a-zA-Z0-9_:]\+\)\.\(.*\)\[\([rw]\)\])$/\1/')
		      LOCKNAME=$(echo $LOCK | sed -e 's/^.*(\([a-zA-Z0-9_:]\+\)\.\(.*\)\[\([rw]\)\])$/\2/')
		       SUBLOCK=$(echo $LOCK | sed -e 's/^.*(\([a-zA-Z0-9_:]\+\)\.\(.*\)\[\([rw]\)\])$/\3/')
		RET=`echo ${LOCKCOMBINEDDT} | grep -q ":"`
		if [ ${?} -eq 0 ];
		then
			LOCKDATATYPE=`echo ${LOCKCOMBINEDDT} | cut -d ":" -f1`
			LOCKSUBCLASS=`echo ${LOCKCOMBINEDDT} | cut -d ":" -f2`
			LOCKSUBCLASS_FILTER=" AND l_sc_sbh${LOCKNR}.name = '${SUBCLASS}'"
		else
			LOCKDATATYPE=${LOCKCOMBINEDDT}
			LOCKSUBCLASS_FILTER=""
		fi
		if [ ${SUBLOCK} == "r" ];
		then
			SUBLOCK_COND="(l_sbh${LOCKNR}.sub_lock = 'r' OR l_sbh${LOCKNR}.sub_lock = 'w')"
		else
			SUBLOCK_COND="l_sbh${LOCKNR}.sub_lock = '${SUBLOCK}'"
		fi
		if echo $LOCKNAME | fgrep '?'; then	 # e.g., i_data?
			echo "error: cannot (yet) deal with EMBSAME locks that are not exactly locatable within the containing data structure ('?' in lock name $LOCKNAME)" >&2
			exit 1
		fi
cat <<EOT
					-- lock #$LOCKNR
					JOIN locks_held lh_sbh${LOCKNR} -- sbh = ShouldBeHeld
					  ON lh_sbh${LOCKNR}.txn_id = s_ac.txn_id
					JOIN locks_embedded_flat l_sbh${LOCKNR}
					  ON l_sbh${LOCKNR}.lock_id = lh_sbh${LOCKNR}.lock_id
					 AND l_sbh${LOCKNR}.alloc_id = s_ac.alloc_id
					 AND ${SUBLOCK_COND}
					JOIN data_types l_dt_sbh${LOCKNR}
					  ON l_sbh${LOCKNR}.data_type_id = l_dt_sbh${LOCKNR}.id
					 AND l_dt_sbh${LOCKNR}.name = '$LOCKDATATYPE'
					JOIN subclasses l_sc_sbh${LOCKNR}
					  ON l_sbh${LOCKNR}.subclass_id = l_sc_sbh${LOCKNR}.id
					  ${LOCKSUBCLASS_FILTER}
					JOIN member_names lock_member_name_sbh${LOCKNR}
					  ON lock_member_name_sbh${LOCKNR}.id = l_sbh${LOCKNR}.member_name_id
					 AND lock_member_name_sbh${LOCKNR}.name = '$LOCKNAME'
EOT

	elif echo $LOCK | grep -q '^\(EMB:\|[A-Za-z_]\+:\)\?[0-9]\+('; then # e.g., EMB:123(i_mutex) or 34(spinlock_t), or console_sem:4711(mutex)
		 LOCKID=$(echo $LOCK | sed -e 's/^\(EMB:\|[A-Za-z_]\+:\)\?\([0-9]\+\)(.*\[\([rw]\)\])$/\2/') # 2st numeric sequence in $LOCK
		SUBLOCK=$(echo $LOCK | sed -e 's/^\(EMB:\|[A-Za-z_]\+:\)\?\([0-9]\+\)(.*\[\([rw]\)\])$/\3/')
		if [ ${SUBLOCK} == "r" ];
		then
			SUBLOCK_COND="(l_sbh${LOCKNR}.sub_lock = 'r' OR l_sbh${LOCKNR}.sub_lock = 'w')"
		else
			SUBLOCK_COND="l_sbh${LOCKNR}.sub_lock = '${SUBLOCK}'"
		fi
		cat <<EOT
					-- lock #$LOCKNR
					JOIN locks_held lh_sbh${LOCKNR}
					  ON lh_sbh${LOCKNR}.txn_id = s_ac.txn_id
					 AND lh_sbh${LOCKNR}.lock_id = $LOCKID
					JOIN locks l_sbh${LOCKNR}
					  ON l_sbh${LOCKNR}.id = lh_sbh${LOCKNR}.lock_id
					 AND ${SUBLOCK_COND}
EOT
	elif echo $LOCK | grep -q '^EMBOTHER'; then # e.g., EMBOTHER(i_mutex)
		LOCKCOMBINEDDT=$(echo $LOCK | sed -e 's/^.*(\([a-zA-Z0-9_:]\+\)\.\(.*\)\[\([rw]\)\])$/\1/')
		      LOCKNAME=$(echo $LOCK | sed -e 's/^.*(\([a-zA-Z0-9_:]\+\)\.\(.*\)\[\([rw]\)\])$/\2/')
		       SUBLOCK=$(echo $LOCK | sed -e 's/^.*(\([a-zA-Z0-9_:]\+\)\.\(.*\)\[\([rw]\)\])$/\3/')
		RET=`echo ${LOCKCOMBINEDDT} | grep -q ":"`
		if [ ${?} -eq 0 ];
		then
			LOCKDATATYPE=`echo ${LOCKCOMBINEDDT} | cut -d ":" -f1`
			LOCKSUBCLASS=`echo ${LOCKCOMBINEDDT} | cut -d ":" -f2`
			LOCKSUBCLASS_FILTER=" AND l_sc_sbh${LOCKNR}.name = '${SUBCLASS}'"
		else
			LOCKDATATYPE=${LOCKCOMBINEDDT}
			LOCKSUBCLASS_FILTER=""
		fi
		if [ ${SUBLOCK} == "r" ];
		then
			SUBLOCK_COND="(l_sbh${LOCKNR}.sub_lock = 'r' OR l_sbh${LOCKNR}.sub_lock = 'w')"
		else
			SUBLOCK_COND="l_sbh${LOCKNR}.sub_lock = '${SUBLOCK}'"
		fi
		if echo $LOCKNAME | fgrep '?'; then	 # e.g., i_data?
			echo "error: cannot (yet) deal with EMBOTHER locks that are not exactly locatable within the containing data structure ('?' in lock name $LOCKNAME)" >&2
			exit 1
		fi
cat <<EOT
					-- lock #$LOCKNR
					JOIN locks_held lh_sbh${LOCKNR} -- sbh = ShouldBeHeld
					  ON lh_sbh${LOCKNR}.txn_id = s_ac.txn_id
					JOIN locks_embedded_flat l_sbh${LOCKNR}
					  ON l_sbh${LOCKNR}.lock_id = lh_sbh${LOCKNR}.lock_id
					 AND l_sbh${LOCKNR}.alloc_id != s_ac.alloc_id
					 AND ${SUBLOCK_COND}
					JOIN data_types l_dt_sbh${LOCKNR}
					  ON l_sbh${LOCKNR}.data_type_id = l_dt_sbh${LOCKNR}.id
					 AND l_dt_sbh${LOCKNR}.name = '$LOCKDATATYPE'
					JOIN subclasses l_sc_sbh${LOCKNR}
					  ON l_sbh${LOCKNR}.subclass_id = l_sc_sbh${LOCKNR}.id
					  ${LOCKSUBCLASS_FILTER}
					JOIN member_names lock_member_name_sbh${LOCKNR}
					  ON lock_member_name_sbh${LOCKNR}.id = l_sbh${LOCKNR}.member_name_id
					 AND lock_member_name_sbh${LOCKNR}.name = '$LOCKNAME'
EOT
		if [ $LAST_EMBOTHER -gt 0 ]; then
				cat <<EOT
					AND lh_sbh${LOCKNR}.start > lh_sbh${LAST_EMBOTHER}.start -- ensure the same EMBOTHER lock gets joined just once
EOT
		fi
		LAST_EMBOTHER=$LOCKNR
	else
		echo "error: unknown lock type $LOCK" >&2
		exit 1
	fi

	if [ $LOCKCONNECTOR = SEQ -a $LOCKNR -gt 1 ]; then
		cat <<EOT
					AND lh_sbh${LOCKNR}.start > lh_sbh$(($LOCKNR - 1)).start -- temporal sequence
EOT
	fi

	LOCKNR=$((LOCKNR + 1))
done
cat <<EOT
					WHERE
					s_ac.ac_id = ac.ac_id AND
					s_ac.ac_type = '$ACCESSTYPE'
				)
				AND NOT EXISTS
				(
					-- Get all accesses that happened on an init path
					SELECT 1
					FROM accesses_flat s_ac
					JOIN subclasses s_sc
					  ON s_sc.id = s_ac.subclass_id
					${SUBCLASS_FILTER_SUB}
					JOIN data_types s_dt
					  ON s_ac.data_type_id = s_dt.id
					  AND s_dt.name = '$DATATYPE'
					JOIN stacktraces AS s_st
					  ON s_ac.stacktrace_id = s_st.id
					LEFT JOIN member_names s_mn
					  ON s_mn.id = s_ac.member_name_id
					 AND s_mn.name = '$MEMBER'
					LEFT JOIN function_blacklist s_fn_bl
					  ON s_fn_bl.fn = s_st.function
					 AND
					 (
					   (
					      (s_fn_bl.subclass_id IS NULL  AND s_fn_bl.member_name_id IS NULL) -- globally blacklisted function
					      OR
					      (s_fn_bl.subclass_id = s_ac.subclass_id AND s_fn_bl.member_name_id IS NULL) -- for this data type blacklisted
					      OR
					      (s_fn_bl.subclass_id = s_ac.subclass_id AND s_fn_bl.member_name_id = s_ac.member_name_id) -- for this member blacklisted
					   )
					   AND
					   (s_fn_bl.sequence IS NULL OR s_fn_bl.sequence = s_st.sequence) -- for functions that appear at a certain position within the trace
					 )
					WHERE
					s_ac.ac_id = ac.ac_id AND
					s_ac.ac_type = '$ACCESSTYPE'
					-- ====================================
					AND s_fn_bl.fn IS NOT NULL
					LIMIT 1
				)
			) ac

			JOIN stacktraces AS st
			  ON ac.stacktrace_id = st.id
			-- Joining the stacktraces table multiplies each row by the number of stackframes an access has.
			-- First, (group) concat all stackframes to a stacktrace.
			GROUP BY ac.ac_id, ac.alloc_id, ac.txn_id, stacktrace_id
		) folded_stacks

		LEFT JOIN locks_held lh
		  ON lh.txn_id = folded_stacks.txn_id
		LEFT JOIN locks l
		  ON l.id = lh.lock_id

		-- find out more about each held lock (allocation -> structs_layout
		-- member or contained-in member in case of a complex member)
		LEFT JOIN allocations lock_a
		  ON l.embedded_in = lock_a.id
		LEFT JOIN subclasses lock_sc
		  ON lock_a.subclass_id = lock_sc.id
		LEFT JOIN structs_layout_flat lock_member
		  ON lock_sc.data_type_id = lock_member.data_type_id
		 AND l.address - lock_a.base_address = lock_member.helper_offset
		LEFT JOIN data_types lock_dt
		  ON lock_sc.data_type_id = lock_dt.id
		LEFT JOIN member_names lock_member_name
		  ON lock_member_name.id = lock_member.member_name_id
		-- lock_a.id IS NULL                         => not embedded
		-- l.address - lock_a.base_address = lock_member.offset   => the lock is exactly this member (or at the beginning of a complex sub-struct)
		-- else                                      => the lock is contained in this member, exact name unknown
		-- Now collapse the stacktrace to one row (aka GROUP_CONCAT)
		GROUP BY ac_id, stacktrace_id, stacktrace
	) all_counterexamples
	GROUP BY data_type, member, accesstype, stacktrace_id, locks_held, stacktrace
) all_counterexamples_by_stack
GROUP BY data_type, member, accesstype, stacktrace_id, stacktrace
ORDER BY data_type, member, accesstype, stacktrace_id
;
EOT
