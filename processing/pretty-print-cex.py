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

# Cache resolved instruction pointers
addressCache = dict()

def addrToFn(vmlinux, addr, pathPrefix):
	addr = addr.lower()
	if addr in addressCache:
		retValue = addressCache[addr]
	else:
		cmd = ['addr2line', '-f', '-e', vmlinux, str(addr)]
		addrProcess = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, stdin=subprocess.PIPE)
		out, err = addrProcess.communicate()
		if addrProcess.returncode != 0:
			LOGGER.error("Cannot resolve function name:\n%s", err)
		# The first line contains the function name
		# The second one contains the filename followed by the linenumber.
		lines = out.split('\n')
		file = lines[1].split(':')[0].replace(pathPrefix,"")
		lineNo = lines[1].split(':')[1] # Get the line number: <file>:<lineno>
		lineNo = lineNo.split('(')[0].strip() # Strip of unwanted chars, e.g., ' (discriminator 5)'
		retValue = {'fn': lines[0] , 'file': file, "line": lineNo}
		addressCache[addr] = retValue
	LOGGER.debug("%s --> %s:%s", addr, retValue['fn'], retValue['line'])
	return retValue

if __name__ == '__main__':
	parser = argparse.ArgumentParser()
	parser.add_argument('-v', '--verbose', action='store_true', help='Be verbose')
	parser.add_argument('-k', '--kerneldir', help='Directory where the kernel was compiled', default='/opt/kernel/linux-32-lockdebugging-4-10/')
	parser.add_argument('-u', '--crossrefurl', help='Base URL to the Linux cross reference site', default='https://elixir.bootlin.com/linux/v4.10')
	parser.add_argument('cexcsv', help='Input file containing the ground truth')
	parser.add_argument('vmlinux', help='VMLINUX')
	parser.add_argument('hypothesescsv', help='Input file containing preditions made by the hypothesizer (winner hypotheses only)')
	args = parser.parse_args()

	cexCSV = args.cexcsv
	vmlinux = args.vmlinux
	kernelDir = args.kerneldir
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
			<td>EMBSAME(x:y)</td><td>While accessing a member of struct <k>x</k>, the lock <k>y</k> of the same instance was held.<td></td>
		</tr>
		<tr>
			<td>EMOTHER(x:y)</td><td>While accessing a member of struct <k>x</k>, the lock <k>y</k> of another instance of <k>x</k> was held.</td>
		</tr>
		<tr>
			<td>EMB:x(y:z)</td><td>While accessing a member of struct <k>y</k>, the lock <k>z</k> of another instance with id <k>x</k> was held.</td>
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
				lockFile = elems[2].split(':')[0].replace(kernelDir, '')
				lockLine = elems[2].split(':')[1]
				print('%02d: <a class="lock" href="%s/source/%s#L%s" title="%s@%s:%s">%s</a>' %(i + 1, crossRefURL, lockFile, lockLine, elems[1], lockFile, lockLine, elems[0]), end='')
				if i < (locksHeldLen - 1):
					print('<br/>', end='')
				i = i + 1
		else:
			print('No locks')
		print('</td><td>', end='')
		traceElems = line['stacktrace'].split(',')
		# Insert the instruction pointer
		traceElems.insert(0, line['instrptr'])
		# Since we want a top-down view, we reverse the stacktrace.
		traceElems.reverse()
		traceElemsLen = len(traceElems)
		# Convert the stacktrace into humanreadable information
		i = 0
		print('<button class="accordion">Stacktrace</button>')
		print('<div class="stacktrace">')
		for traceElem in traceElems:
			codePos = addrToFn(vmlinux, traceElem, kernelDir)
			# Mark the first stacktrace entry since it corresponds
			# to the suspicious memory access.
			if i == (traceElemsLen - 1):
				print('</div>')
				print('%02d: <a class="memaccess" ' % (traceElemsLen - (i + 1)), end='')
			else:
				print('%02d: <a class="stacktraceelem" ' % (traceElemsLen - (i + 1)), end='')
			print('href="%s/source/%s#L%s">%s:%s</a>' %(crossRefURL, codePos['file'], codePos['line'], codePos['fn'], codePos['line']), end='')
			if i < (traceElemsLen - 1):
				print('<br/>', end='')
			i=i+1
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


