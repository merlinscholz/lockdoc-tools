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

if __name__ == '__main__':
	parser = argparse.ArgumentParser()
	parser.add_argument('-v', '--verbose', action='store_true', help='Be verbose')
	parser.add_argument('-u', '--crossrefurl', help='Base URL to the Linux cross reference site', default='https://elixir.bootlin.com/linux/v4.10')
	parser.add_argument('cexcsv', help='Input file containing the ground truth')
	parser.add_argument('vmlinux', help='VMLINUX')
	parser.add_argument('hypothesescsv', help='Input file containing preditions made by the hypothesizer (winner hypotheses only)')
	args = parser.parse_args()

	cexCSV = args.cexcsv
	vmlinux = args.vmlinux
	crossRefURL = args.crossrefurl
	hypothesesCSV = args.hypothesescsv
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
		<tr>
			<td colspan="2">The square brackets specify wether the read or the write side of a lock was held, e.g., [w]. This information is present even if the lock is a reader-only or writer-only lock.</td>
		</tr>
	</table>
	<h2>Results</h2>
	<table>""")
	lastKey = None
	cexID = 1
	hypothesisID = 0
	outerLineStyle = 'a'
	for line in tempReader:
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
			print('				<td colspan="5"><a id="HYPO%d"><b>Hypothesis %d</b></a>: When <b>%s %s.%s</b> the following locks <span style="color:green;font-weight:bold;">should be held</span>: <span style="font-weight:bold;color:blue;">%s</span><br/>'
				% (hypothesisID, hypothesisID, 'reading' if line['accesstype'] == 'r' else 'writing', line['data_type'], line['member'], locksHeldKey), end='')
			print('<b>%2.2f%%</b> (%d out of %d mem accesses under locks)</td>' % (locksHeldEntry['percentage'], locksHeldEntry['occurrences'], locksHeldEntry['total']))
			print('		</tr>')
			print("""		<tr>
			<th>Stacktrace<br/>(00 = stackframe where access occurred)</th><th>ID</th><th>Occurrences</th><th><span style="color:red">Locks actually held<br/>(in order locks were taken)</span></th>
		</tr>""")
			cexID = 1
		lastKey = key

		# Example:
		# EMBSAME(j_barrier)@jbd2_journal_lock_updates@fs/jbd2/transaction.c:746#1+EMBSAME(j_barrier)@jbd2_journal_lock_updates@fs/jbd2/transaction.c:746#1
		lockCombinations = line['locks_held'].split('+')
		print('		<tr class="line_%s">' % ('a' if outerLineStyle == 'a' else 'b'))
		#print('			<td rowspan="%d"><button class="accordion">Stacktrace</button>\n\t\t\t\t<div class="stacktrace">\n' % (len(lockCombinations)), end='')
		print('			<td rowspan="%d">' % (len(lockCombinations)), end='')
		# Extract stacktrace
		traceElems = line['stacktrace'].split(',')
		# Since we want a top-down view, we reverse the stacktrace.
		traceElems.reverse()
		traceElemsLen = len(traceElems)
		LOGGER.debug("%s", line['stacktrace'])
		# Convert the stacktrace into humanreadable information
		i = 0
		formattedStacktrace = ""
		for traceElem in traceElems:
			# Split locks_held
			# Example: 0x4711@jbd2_journal_lock_updates@fs/jbd2/transaction.c:746
			elems = traceElem.split('@')
			codePos = dict()
			codePos['file'] = elems[2].split(':')[0]
			codePos['line'] = elems[2].split(':')[1]
			codePos['fn'] = elems[1]
			# Mark the first stacktrace entry since it corresponds
			# to the suspicious memory access.
			if i == (traceElemsLen - 1):
				#print('				</div>\n', end='')
				print('					%02d: <a class="memaccess" ' % (traceElemsLen - (i + 1)), end='')
			else:
				print('					%02d: <a class="stacktraceelem" ' % (traceElemsLen - (i + 1)), end='')
			print('href="%s/source/%s#L%s">%s:%s</a>' % (crossRefURL, codePos['file'], codePos['line'], codePos['fn'], codePos['line']), end='')
			if i < (traceElemsLen - 1):
				print('<br/>\n', end='')
			i=i+1
		print('\n			</td>')

		# Set the inner line style
		if outerLineStyle == 'a':
			innerLineStyle = 'b'
		else:
			innerLineStyle = 'a'
		# Switch the outer line style *after* the inner line style was set.
		# Otherwise two adjacent rows will have the same color
		if outerLineStyle == 'a':
			outerLineStyle = 'b'
		else:
			outerLineStyle = 'a'

		i = 0
		# $locks_held#occurrences
		# Example:
		# EMBSAME(j_barrier)@jbd2_journal_lock_updates@fs/jbd2/transaction.c:746#1
		for lockComb in lockCombinations:
			locksHeld = lockComb.split('#')[0]
			occurences = lockComb.split('#')[1]

			if i > 0:
				print('		<tr class="line_%s">' % ('a' if innerLineStyle == 'a' else 'b'))
				# Alternate the inner line style if it is the second row or any row later.
				# The first row has the same style as the stacktrace row.
				if innerLineStyle == 'a':
					innerLineStyle = 'b'
				else:
					innerLineStyle = 'a'
			print('			<td><a href="#HYPO%d">%d</a>.%d</td><td>%s</td>' % (hypothesisID, hypothesisID, cexID, occurences), end='')
			print('<td>', end='')
			# Split locks_held
			# Example: EMBSAME(j_barrier)@jbd2_journal_lock_updates@fs/jbd2/transaction.c:746, EMBSAME(j_state_lo0ck)@jbd2_journal_lock_updates@fs/jbd2/transaction.c:42
			if locksHeld != "nolocks":
				locksHeld = locksHeld.split(',')
				locksHeldLen = len(locksHeld)
				k = 0
				# Example: EMBSAME(j_barrier)@jbd2_journal_lock_updates@fs/jbd2/transaction.c:746
				for lockHeld in locksHeld:
					# Index 0: the lock
					# Index 1: the function where the lock was acquired
					# Index 2: the file and line where the lock was acquired
					elems = lockHeld.split('@')
					lockFile = elems[2].split(':')[0]
					lockLine = elems[2].split(':')[1] #
					print('%02d: <a class="lock" href="%s/source/%s#L%s" title="%s@%s:%s">%s</a>' 
						%(k + 1, crossRefURL, lockFile, lockLine, elems[1], lockFile, lockLine, elems[0]), end='')
					if k < (locksHeldLen - 1):
						print('<br/>', end='')
					k = k + 1
			else:
				print('No locks')
			print('</td>')
			print('		</tr>')
			cexID = cexID + 1
			i = i + 1

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


