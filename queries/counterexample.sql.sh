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

	example:
	$0 block_device r:bd_invalidated CEX SEQ 'EMB:5779(foo)' 'EMBSAME(bd_mutex)'

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

cat <<EOT
SELECT fn, instrptr, locks_held, COUNT(*) AS occurrences
FROM
(
	SELECT ac.fn, CONCAT('0x', HEX(ac.instrptr)) AS instrptr,
	GROUP_CONCAT(
		CASE
		WHEN l.embedded_in IS NULL THEN CONCAT(l.id, '(', l.type, ')') -- global (or embedded in unknown allocation)
		WHEN l.embedded_in IS NOT NULL AND l.embedded_in = ac.alloc_id
			THEN CONCAT('EMBSAME(', IF(l.ptr - lock_a.ptr = lock_member.offset, lock_member.member, CONCAT(lock_member.member, '?')), ')') -- embedded in same
		ELSE CONCAT('EMB:', l.id, '(',  IF(l.ptr - lock_a.ptr = lock_member.offset, lock_member.member, CONCAT(lock_member.member, '?')), ')') -- embedded in other
		END
		ORDER BY lh.start
		SEPARATOR ' -> '
	) AS locks_held

	FROM

	(
		SELECT ac.id, ac.txn_id, ac.alloc_id, ac.fn, ac.instrptr
		FROM accesses ac
		JOIN allocations a
		  ON ac.alloc_id = a.id
		 AND ac.type = '$ACCESSTYPE'
		JOIN data_types dt
		  ON a.type = dt.id
		 AND dt.name = '$DATATYPE'
		JOIN structs_layout_flat sl
		  ON sl.type_id = a.type
		 AND sl.helper_offset = ac.address - a.ptr
		 AND sl.member = '$MEMBER'
		LEFT JOIN function_blacklist fn_bl
		  ON fn_bl.datatype_id = a.type
		 AND fn_bl.fn = ac.fn
		 AND (fn_bl.datatype_member_id IS NULL OR fn_bl.datatype_member_id = sl.member_id)
		WHERE bl.fn IS NULL
EOT
if [ $MODE = CEX ]; then
	cat <<EOT
		AND ac.id NOT IN	-- counterexample
EOT
elif [ $MODE = EX ]; then
	cat <<EOT
		AND ac.id IN		-- example
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
			  ON a.type = dt.id
			 AND dt.name = '$DATATYPE'
			JOIN structs_layout_flat sl
			  ON sl.type_id = a.type
			 AND sl.helper_offset = ac.address - a.ptr
			 AND sl.member = '$MEMBER'
			LEFT JOIN function_blacklist fn_bl
			  ON fn_bl.datatype_id = a.type
			 AND fn_bl.fn = ac.fn
			 AND (fn_bl.datatype_member_id IS NULL OR fn_bl.datatype_member_id = sl.member_id)
EOT

LOCKNR=1
for LOCK in "$@"; do
	if ! echo $LOCK | grep -q '^EMBSAME'; then # e.g., EMBSAME(i_mutex)
		LOCKNAME=$(echo $LOCK | sed -e 's/^.*(\(.*\))$/\1/')
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
			JOIN allocations l_sbh_a${LOCKNR}
			  ON l_sbh${LOCKNR}.embedded_in = l_sbh_a${LOCKNR}.id
			JOIN structs_layout_flat lock_member_sbh${LOCKNR}
			  ON l_sbh_a${LOCKNR}.type = lock_member_sbh${LOCKNR}.type_id
			 AND l_sbh${LOCKNR}.ptr - l_sbh_a${LOCKNR}.ptr = lock_member_sbh${LOCKNR}.helper_offset
			 AND lock_member_sbh${LOCKNR}.member = '$LOCKNAME'
EOT

	elif echo $LOCK | grep -q '^\(EMB:\)\?[0-9]\+('; then # e.g., EMB:123(i_mutex) or 34(spinlock_t)
		LOCKID=$(echo $LOCK | sed -e 's/^[^0-9]*\([0-9]\+\).*$/\1/') # 1st numeric sequence in $LOCK
		cat <<EOT
			-- lock #$LOCKNR
			JOIN locks_held lh_sbh${LOCKNR}
			  ON lh_sbh${LOCKNR}.txn_id = ac.txn_id
			 AND lh_sbh${LOCKNR}.lock_id = $LOCKID
EOT
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

			WHERE bl.fn IS NULL

		)
	) ac

	LEFT JOIN locks_held lh
	  ON lh.txn_id = ac.txn_id
	LEFT JOIN locks l
	  ON l.id = lh.lock_id
			
	-- find out more about each held lock (allocation -> structs_layout
	-- member or contained-in member in case of a complex member)
	LEFT JOIN allocations lock_a
	  ON l.embedded_in = lock_a.id
	LEFT JOIN structs_layout_flat lock_member
	  ON lock_a.type = lock_member.type_id
	 AND l.ptr - lock_a.ptr = lock_member.helper_offset
	-- lock_a.id IS NULL                         => not embedded
	-- l.ptr - lock_a.ptr = lock_member.offset   => the lock is exactly this member (or at the beginning of a complex sub-struct)
	-- else                                      => the lock is contained in this member, exact name unknown
	GROUP BY ac.id
) all_counterexamples
GROUP BY instrptr, locks_held
ORDER BY instrptr, occurrences
;
EOT
