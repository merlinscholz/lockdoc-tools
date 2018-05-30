#!/usr/bin/python
# A.Lochmann 2018
# This scripts generates a pretty bug overview as an html site.
# It includes links to the source code for stack entries and for lock
# acquisitions.
# Example: ./pretty-print-cex.py cex-journal_t.csv ../vmlinux-4-10-nococci-20180321-ga8156e4 all_txns_members_locks_hypo_winner_nostack.csv > cex-journal_t.html

from __future__ import print_function
import sys
import csv
import re
import argparse
from pprint import pprint
import logging
import subprocess

logging.basicConfig()
LOGGER = logging.getLogger(__name__)

# Temproray workaround to filter cex that are on an init or destroy path
# Some day, this array will be merged into function_blacklist.csv.
globalFnBlacklist = [ "atomic_read", "atomic_set", "atomic_add", "atomic_sub", "atomic_sub_return", "atomic_sub_and_test", "atomic_inc", "atomic_dec",
					  "atomic_dec_return", "atomic_dec_and_test", "atomic_inc", "atomic_inc_return", "atomic_inc_and_test",
					  "atomic_add_negative", "atomic_add_return", ",atomic_sub_return", "atomic_fetch_add", "atomic_fetch_sub",
					  "atomic_cmpxchg", "atomic_xchg", "atomic_fetch_and", "atomic_fetch_or", "atomic_fetch_xor", "atomic_inc_short",
					  "__atomic_add_unsless", "set_bit", "__set_bit", "clear_bit", "clear_bit_unlock", "__clear_bit", "clear_bit_unlock_is_negative_byte",
					  "__clear_bit_unlock", "__change_bit", "change_bit", "test_and_set_bit", "test_and_set_bit_lock", "__test_and_set_bit",
					  "test_and_clear_bit", "__test_and_clear_bit", "test_and_change_bit", "__test_and_change_bit", "constant_test_bit", "variable_test_bit",
					  "mount_fs"]

if __name__ == '__main__':
	parser = argparse.ArgumentParser()
	parser.add_argument('-v', '--verbose', action='store_true', help='Be verbose')
	parser.add_argument('-u', '--crossrefurl', help='Base URL to the Linux cross reference site', default='https://elixir.bootlin.com/linux/v4.10')
	parser.add_argument('cexcsv', help='Input file containing the ground truth')
	parser.add_argument('vmlinux', help='VMLINUX')
	parser.add_argument('hypothesescsv', help='Input file containing preditions made by the hypothesizer (winner hypotheses only)')
	parser.add_argument('fnblacklistcsv', help='CSV with blacklisted functions indexed by data type')
	args = parser.parse_args()

	cexCSV = args.cexcsv
	vmlinux = args.vmlinux
	crossRefURL = args.crossrefurl
	hypothesesCSV = args.hypothesescsv
	fnblacklistCSV = args.fnblacklistcsv
	separator = ';'
	count = 0
	cexDict = dict()
	hypothesesDict = dict()
	if args.verbose:
		LOGGER.setLevel(logging.DEBUG)
	else:
		LOGGER.setLevel(logging.INFO)

	# Parse the hypothesizer results, and store them in one large dictionary
	tempFile = open(hypothesesCSV,'rb')
	tempReader = csv.DictReader(tempFile, delimiter=';')
	for line in tempReader:
		count = count + 1
		key = (line['type'], line['member'], line['accesstype'])
		if key not in hypothesesDict:
			hypothesesEntry = {'locks': dict()}
			hypothesesDict[key] = hypothesesEntry
		else:
			hypothesesEntry = hypothesesDict[key]
		# ! ! ! Attention ! ! !
		# If this script will be extended beyond its current intention (verifying the groundtruth),
		# one need to filter lock combinations that are actually no real combinations, e.g., 'no hypothesis with locks exceeds cutoff threshold'.
		if line['locks'] not in hypothesesEntry['locks']:
			if line['confidence'] == "TODO":
				temp = 0
			else:
				temp = float(line['confidence'])
			locksHeldEntry = {'occurrences': int(line['occurrences']), 'total': int(line['total']), 'percentage': float(line['percentage']), 'accepted': int(line['accepted']), 'confidence': temp, 'counterexample-parameters': line['counterexample-parameters']}
			hypothesesEntry['locks'][line['locks']] = locksHeldEntry
			LOGGER.debug('Added lock combination (%s) for key %s', line['locks'], key)
		else:
			LOGGER.error('Lock combination (%s) does already exist for key %s', line['locks'], key)
	LOGGER.debug('Read %d locking predictions for %d different (struct,member,accesstype) tuples from "%s"', count, len(hypothesesDict), hypothesesCSV)
	tempFile.close()

	# Read and parse the function blacklist
	# $data_type --> fn: "list of blacklisted functions"
	#				 members: $member --> fn: "list of blacklisted functions"
	fnBlacklistDict = dict()
	tempFile = open(fnblacklistCSV,'rb')
	tempReader = csv.DictReader(tempFile, delimiter=';')
	for line in tempReader:
		count = count + 1
		key = (line['datatype'])
		if key not in fnBlacklistDict:
			fnBlacklistEntry = {'members': dict(), 'fn': list()}
			fnBlacklistDict[key] = fnBlacklistEntry
		else:
			fnBlacklistEntry = fnBlacklistDict[key]
		if line['datatype_member'] == '\\N':
			fnBlacklistEntry['fn'].append(line['fn'])
		else:
			if line['datatype_member'] not in fnBlacklistEntry['members']:
				memberBlacklistEntry = list()
				fnBlacklistEntry['members'][line['datatype_member']] = memberBlacklistEntry
			else:
				memberBlacklistEntry = fnBlacklistEntry['members'][line['datatype_member']]
			memberBlacklistEntry.append(line['fn'])
	tempFile.close()

	tempFile = open(cexCSV,'rb')
	tempReader = csv.DictReader(tempFile, delimiter=';')
	print("""<html>
<head>
<style>
body {
	font-family: monospace;
}
h1, h2 {
	text-align: center;
}
table {
	margin-left: auto;
	margin-right: auto;
}
th {
	background-color: #eee;
	vertical-align: middle;
	padding: 0.3em;
}
tr {
	border-bottom: 1px solid #7192a8;
}
td {
	padding: 0.3em;
}
tr.line_a {
	background-color:#efefef;
}

tr.line_b {
	background-color:#ffffff;
}

a:visited {
	color:blue;
}
a.memaccess:visited {
	color:green;
}
a.memaccess:link {
	color:green;
}

tr.line_heading {
	background-color:#eee;
	text-align:center;
}

.accordion {
    background-color: #eee;
    color: #444;
    cursor: pointer;
    padding: 18px;
    width: 100%;
    text-align: left;
    border: none;
    outline: none;
    transition: 0.4s;
}

/* Add a background color to the button if it is clicked on (add the .active class with JS), and when you move the mouse over it (hover) */
.active, .accordion:hover {
    background-color: #ccc;
}

/* Style the accordion panel. Note: hidden by default */
.stacktrace {
    /*padding: 0 18px;*/
    background-color: white;
    display: none;
    overflow: hidden;
}
</style>
</head>
<body>
	<h1>Counterexamples</h1>
	<h2>Legend</h2>
	<table>
		<tr>
			<td>EMBSAME(x:y)</td><td>While accessing a member of an instance of struct <k>x</k>, the lock <k>y</k> of the same instance was held.<td></td>
		</tr>
		<tr>
			<td>EMOTHER(x:y)</td><td>While accessing a member of an instance of struct <k>x</k>, the lock <k>y</k> of another instance of <k>x</k> was held.</td>
		</tr>
		<tr>
			<td>EMB:x(y:z)</td><td>While accessing a member of an instance of struct <k>y</k>, the lock <k>z</k> of another instance with id <k>x</k> was held.</td>
		</tr>
		<tr>
			<td>x(y)</td><td>A global lock of type <k>y</k> was held. It has the id <k>x</k>.</td>
		</tr>
		<tr>
			<td>x:y(z)</td><td>The global <k>x</k> lock of type <k>z</k> was held. It has the id <k>y</k>.</td>
		</tr>
	</table>
	<h2>Results</h2>
	<table>
		<tr>
			<!--<th>Data Type</th><th>Member</th><th>Access Type</th>--><th>ID</th><th>Occurrences</th><th>Locks Held<br/>(first&nbsp;&rarr;&nbsp;most recent)</th><th>Stacktrace<br/>(top-down)</th>

		</tr>""")
	
	lineCounter = 0
	lastKey = None
	cexID = 1
	hypothesisID = 0
	for line in tempReader:
		# Translate the stacktrace to code positions
		# Since we want to filter out all cexs which
		# happenend on an init/free path or use atomic funtions,
		# we must do this so early. Otherwise, this script would
		# have produced output.
		traceElems = line['stacktrace'].split(',')
		# Since we want a top-down view, we reverse the stacktrace.
		traceElems.reverse()
		traceElemsLen = len(traceElems)
		LOGGER.debug("%s", line['stacktrace'])
		# Convert the stacktrace into humanreadable information
		i = 0
		formattedStacktrace = ""
		abort = None
		for traceElem in traceElems:
			# Split stacktrace element
			# Example: 0x4711@jbd2_journal_lock_updates@fs/jbd2/transaction.c:746
			elems = traceElem.split('@')
			codePos = dict()
			codePos['file'] = elems[2].split(':')[0]
			codePos['line'] = elems[2].split(':')[1]
			codePos['fn'] = elems[1]
			# Is thise function globally blacklisted?
			if codePos['fn'] in globalFnBlacklist:
				abort = codePos['fn']
				break
			# Do we have blacklisted functions for this data type?
			elif line['data_type'] in fnBlacklistDict:
					fnBlacklistEntry = fnBlacklistDict[line['data_type']]
					# Do we have 'global' (for all members) blacklisted functions?
					if codePos['fn'] in fnBlacklistEntry['fn']:
						abort = codePos['fn']
						break
					# We don't. But we may have member-specific blacklisted functions.
					elif line['member'] in fnBlacklistEntry['members']:
						fnBlMemberEntry = fnBlacklistEntry['members']
						if codePos['fn'] in fnBlMemberEntry['fn']:
							abort = codePos['fn']
							break
			# Mark the first stacktrace entry since it corresponds
			# to the suspicious memory access.
			if i == (traceElemsLen - 1):
				formattedStacktrace = formattedStacktrace + '</div>\n'
				formattedStacktrace = formattedStacktrace + '%02d: <a class="memaccess" ' % (traceElemsLen - (i + 1))
			else:
				formattedStacktrace = formattedStacktrace + '%02d: <a class="stacktraceelem" ' % (traceElemsLen - (i + 1))
			formattedStacktrace = formattedStacktrace + 'href="%s/source/%s#L%s">%s:%s</a>' % (crossRefURL, codePos['file'], codePos['line'], codePos['fn'], codePos['line'])
			if i < (traceElemsLen - 1):
				formattedStacktrace = formattedStacktrace + '<br/>\n'
			i=i+1
		# Abort further processing of this cex, it isn't a real one.
		if abort is not None:
			LOGGER.debug('Filtered cex for (%s,%s,%s) due to blacklisted function: %s.' % (line['data_type'], line['member'], line['accesstype'], codePos['fn']))
			continue
		
		key = (line['data_type'], line['member'], line['accesstype'])
		if key not in hypothesesDict:
			LOGGER.error('Key %s does not exist in %s' %(key, hypothesesCSV))
			continue
		else:
			hypothesesEntry = hypothesesDict[key]
		# Print a header for each tuple of (data_type, member, accesstype)
		if lastKey != key:
			hypothesisID = hypothesisID + 1
			print('		<tr class="line_heading">')
			# The header contains information about the accessed member like
			# the access type or the locking rule.
			# Moreover, it shows statistics about the locking rule, e.g., the fraction ('percentage') of all accesses ('total') that adhere to that rule.
			# E.g.: 94.6% (19069 out of 20155 mem accesses under locks): EMBOTHER(inode:i_rwsem)			
			locksHeldKey = hypothesesEntry['locks'].keys()[0]
			locksHeldEntry = hypothesesEntry['locks'][locksHeldKey]
			print('<td colspan="5"><b>Hypothesis %d</b>: <b>%s</b> access to <b>%s.%s</b> is proteced by <b>%s</b><br/>' % (hypothesisID, 'Read' if line['accesstype'] == 'r' else 'Write', line['data_type'], line['member'], locksHeldKey), end='')
			print('<b>%2.2f%%</b> (%d out of %d mem accesses under locks)</td>' % (locksHeldEntry['percentage'], locksHeldEntry['occurrences'], locksHeldEntry['total']))
			print('		</tr>')
			cexID = 1
		lastKey = key
		
		if lineCounter % 2 == 0:
			lineStyle = 'class="line_a"'
		else:
			lineStyle = 'class="line_b"'
		print('		<tr %s>' % lineStyle)
		print('			<!--<td>%s</td><td>%s</td><td>%s</td>--><td>%d.%d</td><td>%s</td>' %(line['data_type'], line['member'], line['accesstype'], hypothesisID, cexID, line['occurrences']), end='')
		print('<td>', end='')
		
		# Split locks_held
		# Example: EMBSAME(j_barrier)@jbd2_journal_lock_updates@fs/jbd2/transaction.c:746
		if line['locks_held'] != "NULL":
			locksHeld = line['locks_held'].split(',')
			locksHeldLen = len(locksHeld)
			i = 0
			for lockHeld in locksHeld:
				# Index 0: the lock
				# Index 1: the function where the lock was acquired
				# Index 2: the file and line where the lock was acquired
				elems = lockHeld.split('@')
				lockFile = elems[2].split(':')[0]
				lockLine = elems[2].split(':')[1]
				print('%02d: <a class="lock" href="%s/source/%s#L%s" title="%s@%s:%s">%s</a>' %(i + 1, crossRefURL, lockFile, lockLine, elems[1], lockFile, lockLine, elems[0]), end='')
				if i < (locksHeldLen - 1):
					print('<br/>', end='')
				i = i + 1
		else:
			print('No locks')
		print('</td><td><button class="accordion">Stacktrace</button>\n<div class="stacktrace">%s</td>' % (formattedStacktrace))
		print('</td>')
		print('		</tr>')
		lineCounter = lineCounter + 1
		cexID = cexID + 1

	print("""	</table>
<script type='text/javascript'>
var acc = document.getElementsByClassName("accordion");
var i;
for (i = 0; i < acc.length; i++) {
	acc[i].addEventListener("click", function() {
		/* Toggle between adding and removing the "active" class,
		to highlight the button that controls the panel */
		this.classList.toggle("active");
		/* Toggle between hiding and showing the active panel */
		var panel = this.nextElementSibling;
		if (panel.style.display === "block") {
			panel.style.display = "none";
		} else {
			panel.style.display = "block";
		}
	});
}
</script>
</body>
</html>""")
	tempFile.close()


