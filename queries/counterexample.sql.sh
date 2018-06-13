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

DATATYPE=$1; shift
COMBINED_MEMBER=$1; shift
MODE=$1; shift
LOCKCONNECTOR=$1; shift

ACCESSTYPE=${COMBINED_MEMBER:0:1}
SANITYCHECK=${COMBINED_MEMBER:1:1}
MEMBER=${COMBINED_MEMBER:2}

if [ "$SANITYCHECK" != : ]; then
	echo "error: 2nd parameter not in ACCESSTYPE:MEMBER form" >&2
	exit 1
fi

EMBOTHER_SQL="ELSE CONCAT('EMB:', l.id, '(',  IF(l.address - lock_a.base_address = lock_member.offset, lock_member_name.name, CONCAT(lock_member_name.name, '?')), '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- embedded in other"
if [ -n "${USE_EMBOTHER}" ];
then
	if [ ${USE_EMBOTHER} -gt 0 ];
	then
		EMBOTHER_SQL="ELSE CONCAT('EMBOTHER', '(',  IF(l.address - lock_a.base_address = lock_member.offset, lock_member_name.name, CONCAT(lock_member_name.name, '?')), '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- embedded in other"
	fi
fi

cat <<EOT
SET SESSION group_concat_max_len = 100000;
SELECT data_type, member, accesstype, stacktrace, 
	GROUP_CONCAT(
		CONCAT(locks_held, '#', occurrences)
		ORDER BY occurrences DESC, locks_held
		SEPARATOR '+'
	) AS locks_held
FROM
(
	SELECT '${DATATYPE}' AS data_type, '${MEMBER}' AS member, '${ACCESSTYPE}' AS accesstype, COUNT(*) AS occurrences, stacktrace_id, stacktrace, locks_held
	FROM
	(
		SELECT stacktrace_id,
			GROUP_CONCAT(
				stacktrace_elem
				ORDER BY st_sequence
				SEPARATOR ','
			) AS stacktrace, IF(locks_held IS NULL, 'nolocks', locks_held) AS locks_held
		
		FROM
		(
			SELECT ac.id AS ac_id, stacktrace_id, CONCAT('0x', HEX(st.instruction_ptr), '@', st.function, '@', st.file, ':', st.line) AS stacktrace_elem, st.sequence AS st_sequence,
			GROUP_CONCAT(
				CASE
				WHEN l.embedded_in IS NULL AND l.lock_var_name IS NULL
					THEN CONCAT(l.id, '(', l.lock_type_name, '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- global (or embedded in unknown allocation *and* no name available)
				WHEN l.embedded_in IS NULL AND l.lock_var_name IS NOT NULL
					THEN CONCAT(l.lock_var_name, ':', l.id, '(', l.lock_type_name, '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- global (or embedded in unknown allocation *and* a name is available)
				WHEN l.embedded_in IS NOT NULL AND l.embedded_in = ac.alloc_id
					THEN CONCAT('EMBSAME(', IF(l.address - lock_a.base_address = lock_member.offset, lock_member_name.name, CONCAT(lock_member_name.name, '?')), '[', l.sub_lock, '])', '@', lh.last_fn, '@', lh.last_file, ':', lh.last_line) -- embedded in same
					${EMBOTHER_SQL}
				END
				ORDER BY lh.start
				SEPARATOR ','
			) AS locks_held

			FROM
			(
				SELECT ac.id, ac.txn_id, ac.alloc_id, st.function, ac.stacktrace_id
				FROM accesses ac
				JOIN allocations a
				  ON ac.alloc_id = a.id
				 AND ac.type = '$ACCESSTYPE'
				JOIN data_types dt
				  ON a.data_type_id = dt.id
				 AND dt.name = '$DATATYPE'
				JOIN stacktraces AS st
				  ON ac.stacktrace_id = st.id
				 AND st.sequence = 0
				JOIN structs_layout_flat sl
				  ON sl.data_type_id = a.data_type_id
				 AND sl.helper_offset = ac.address - a.base_address
				JOIN member_names mn
				  ON mn.id = sl.member_name_id
				 AND mn.name = '$MEMBER'
				WHERE
EOT
if [ $MODE = CEX ]; then
	cat <<EOT
				ac.id NOT IN	-- counterexample
EOT
elif [ $MODE = EX ]; then
	cat <<EOT
				ac.id IN		-- example
EOT
else
	echo "error: unknown MODE $MODE" >&2
	exit 1
fi
cat <<EOT
				(
					SELECT ac.id
					FROM accesses ac
					JOIN allocations a
					  ON ac.alloc_id = a.id
					 AND ac.type = '$ACCESSTYPE'
					JOIN data_types dt
					  ON a.data_type_id = dt.id
					 AND dt.name = '$DATATYPE'
					JOIN structs_layout_flat sl
					  ON sl.data_type_id = a.data_type_id
					 AND sl.helper_offset = ac.address - a.base_address
					JOIN member_names mn
					  ON mn.id = sl.member_name_id
					 AND mn.name = '$MEMBER'
EOT

LOCKNR=1
LAST_EMBOTHER=0
for LOCK in "$@"; do
	if echo $LOCK | grep -q '^EMBSAME'; then # e.g., EMBSAME(i_mutex)
#		LOCKNAME=$(echo $LOCK | sed -e 's/^.*(\(.*\))$/\1/')
		LOCKNAME=$(echo $LOCK | sed -e 's/^.*(.*:\(.*\)\[\([rw]\)\])$/\1/')
		SUBLOCK=$(echo $LOCK | sed -e 's/^.*(.*:\(.*\)\[\([rw]\)\])$/\2/')
		if echo $LOCKNAME | fgrep '?'; then	 # e.g., i_data?
			echo "error: cannot (yet) deal with EMBSAME locks that are not exactly locatable within the containing data structure ('?' in lock name $LOCKNAME)" >&2
			exit 1
		fi
cat <<EOT
					-- lock #$LOCKNR
					JOIN locks_held lh_sbh${LOCKNR} -- sbh = ShouldBeHeld
					  ON lh_sbh${LOCKNR}.txn_id = ac.txn_id
					JOIN locks l_sbh${LOCKNR}
					  ON l_sbh${LOCKNR}.id = lh_sbh${LOCKNR}.lock_id
					 AND l_sbh${LOCKNR}.sub_lock = '${SUBLOCK}'
					JOIN allocations l_sbh_a${LOCKNR}
					  ON l_sbh${LOCKNR}.embedded_in = l_sbh_a${LOCKNR}.id
					JOIN structs_layout_flat lock_member_sbh${LOCKNR}
					  ON l_sbh_a${LOCKNR}.data_type_id = lock_member_sbh${LOCKNR}.data_type_id
					 AND l_sbh${LOCKNR}.address - l_sbh_a${LOCKNR}.base_address = lock_member_sbh${LOCKNR}.helper_offset
					JOIN member_names lock_member_name_sbh${LOCKNR}
					  ON lock_member_name_sbh${LOCKNR}.id = lock_member_sbh${LOCKNR}.member_name_id
					 AND lock_member_name_sbh${LOCKNR}.name = '$LOCKNAME'
EOT

	elif echo $LOCK | grep -q '^\(EMB:\|[A-Za-z_]\+:\)\?[0-9]\+('; then # e.g., EMB:123(i_mutex) or 34(spinlock_t), or console_sem:4711(mutex)
		LOCKID=$(echo $LOCK | sed -e 's/^\(EMB:\|[A-Za-z_]\+:\)\?\([0-9]\+\)(.*\[\([rw]\)\])$/\2/') # 2st numeric sequence in $LOCK
		SUBLOCK=$(echo $LOCK | sed -e 's/^\(EMB:\|[A-Za-z_]\+:\)\?\([0-9]\+\)(.*\[\([rw]\)\])$/\3/')
		cat <<EOT
					-- lock #$LOCKNR
					JOIN locks_held lh_sbh${LOCKNR}
					  ON lh_sbh${LOCKNR}.txn_id = ac.txn_id
					 AND lh_sbh${LOCKNR}.lock_id = $LOCKID
					JOIN locks l_sbh${LOCKNR}
					  ON l_sbh${LOCKNR}.id = lh_sbh${LOCKNR}.lock_id
					 AND l_sbh${LOCKNR}.sub_lock = '${SUBLOCK}'
EOT
	elif echo $LOCK | grep -q '^EMBOTHER'; then # e.g., EMBOTHER(i_mutex)
#		LOCKNAME=$(echo $LOCK | sed -e 's/^.*(\(.*\))$/\1/')
		LOCKNAME=$(echo $LOCK | sed -e 's/^.*(.*:\(.*\)\[\([rw]\)\])$/\1/')
		SUBLOCK=$(echo $LOCK | sed -e 's/^.*(.*:\(.*\)\[\([rw]\)\])$/\2/')
		if echo $LOCKNAME | fgrep '?'; then	 # e.g., i_data?
			echo "error: cannot (yet) deal with EMBOTHER locks that are not exactly locatable within the containing data structure ('?' in lock name $LOCKNAME)" >&2
			exit 1
		fi
cat <<EOT
					-- lock #$LOCKNR
					JOIN locks_held lh_sbh${LOCKNR} -- sbh = ShouldBeHeld
					  ON lh_sbh${LOCKNR}.txn_id = ac.txn_id
					JOIN locks l_sbh${LOCKNR}
					  ON l_sbh${LOCKNR}.id = lh_sbh${LOCKNR}.lock_id
					 AND l_sbh${LOCKNR}.embedded_in != a.id
					 AND l_sbh${LOCKNR}.sub_lock = '${SUBLOCK}'
					JOIN allocations l_sbh_a${LOCKNR}
					  ON l_sbh${LOCKNR}.embedded_in = l_sbh_a${LOCKNR}.id
					JOIN structs_layout_flat lock_member_sbh${LOCKNR}
					  ON l_sbh_a${LOCKNR}.data_type_id = lock_member_sbh${LOCKNR}.data_type_id
					 AND l_sbh${LOCKNR}.address - l_sbh_a${LOCKNR}.base_address = lock_member_sbh${LOCKNR}.helper_offset
					JOIN member_names lock_member_name_sbh${LOCKNR}
					  ON lock_member_name_sbh${LOCKNR}.id = lock_member_sbh${LOCKNR}.member_name_id
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
				)
				AND ac.id NOT IN
				(
				-- Get all accesses that happened on an init path
					SELECT ac.id
					FROM accesses ac
					JOIN allocations a
					  ON ac.alloc_id = a.id
					 AND ac.type = '$ACCESSTYPE'
					JOIN data_types dt
					  ON a.data_type_id = dt.id
					 AND dt.name = '$DATATYPE'
					JOIN stacktraces AS st
					  ON ac.stacktrace_id = st.id
					JOIN structs_layout_flat sl
					  ON sl.data_type_id = a.data_type_id
					 AND sl.helper_offset = ac.address - a.base_address
					JOIN member_names mn
					  ON mn.id = sl.member_name_id
					 AND mn.name = '$MEMBER'
					LEFT JOIN function_blacklist fn_bl
					  ON fn_bl.fn = st.function
					 AND
					 (
					   (fn_bl.data_type_id IS NULL  AND fn_bl.member_name_id IS NULL) -- globally blacklisted function
					   OR
					   (fn_bl.data_type_id = a.data_type_id AND fn_bl.member_name_id IS NULL) -- for this data type blacklisted
					   OR
					   (fn_bl.data_type_id = a.data_type_id AND fn_bl.member_name_id = sl.member_name_id) -- for this member blacklisted
					 )
					WHERE
						fn_bl.fn IS NOT NULL
					GROUP BY ac.id
				)
			) ac

			JOIN stacktraces AS st
			  ON ac.stacktrace_id = st.id

			LEFT JOIN locks_held lh
			  ON lh.txn_id = ac.txn_id
			LEFT JOIN locks l
			  ON l.id = lh.lock_id

			-- find out more about each held lock (allocation -> structs_layout
			-- member or contained-in member in case of a complex member)
			LEFT JOIN allocations lock_a
			  ON l.embedded_in = lock_a.id
			LEFT JOIN structs_layout_flat lock_member
			  ON lock_a.data_type_id = lock_member.data_type_id
			 AND l.address - lock_a.base_address = lock_member.helper_offset
			LEFT JOIN member_names lock_member_name
			  ON lock_member_name.id = lock_member.member_name_id
			-- lock_a.id IS NULL                         => not embedded
			-- l.address - lock_a.base_address = lock_member.offset   => the lock is exactly this member (or at the beginning of a complex sub-struct)
			-- else                                      => the lock is contained in this member, exact name unknown
			-- Joining the stacktraces table multiplies each row by the number of stackframes an access has.
			-- First, (group) concat all locks held during one access, but preserve one row for each stackframe.
			-- One might leave ac.stacktrace_id out, because each access (--> unique ac.id) has exactly one assiotciated stacktrace.
			GROUP BY ac.id, ac.stacktrace_id, st.sequence
		) folded_locks
		-- Now collapse the stacktrace to one row (aka GROUP_CONCAT)
		GROUP BY ac_id
	) all_counterexamples
	GROUP BY data_type, member, accesstype, stacktrace_id, locks_held
) all_counterexamples_by_stack
GROUP BY data_type, member, accesstype, stacktrace_id
ORDER BY data_type, member, accesstype, stacktrace_id
;
EOT
