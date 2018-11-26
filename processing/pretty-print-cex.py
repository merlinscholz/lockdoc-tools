#!/usr/bin/python
# A.Lochmann 2018
# This scripts generates a pretty bug overview as an html site.
# It includes links to the source code for stack entries and for lock
# acquisitions.
# Example: ./pretty-print-cex.py -d {graph,tree} cex-journal_t.csv ../vmlinux-4-10-nococci-20180321-ga8156e4 all_txns_members_locks_hypo_winner_nostack.csv > cex-journal_t.html

from __future__ import print_function
import sys
import csv
import re
import argparse
from pprint import pprint
import logging
import subprocess
import util

logging.basicConfig()
LOGGER = logging.getLogger(__name__)
USERSPACE_ID = 'userspace_root'
USERSPACE_FN = 'userspace'
GRAPH = 'graph'
TREE = 'tree'
DYNAMIC = 'dynamic'

extContent = [
	{
		'type': 'js',
		'fname': 'cytoscape.min.js',
		'rev': '2411ee5',
		'url': 'https://raw.githubusercontent.com/cytoscape/cytoscape.js/{rev}/dist',
		'license': 'MIT',
		'data': None
	},
	{
		'type': 'js',
		'fname': 'dagre.min.js',
		'rev': '4c7dc4c',
		'url': 'https://raw.githubusercontent.com/dagrejs/dagre/{rev}/dist',
		'license': 'MIT',
		'data': None
	},
	{
		'type': 'js',
		'fname': 'cytoscape-dagre.js',
		'rev': 'e1f3758',
		'url': 'https://raw.githubusercontent.com/cytoscape/cytoscape.js-dagre/{rev}',
		'license': 'MIT',
		'data': None
	},
	{
		'type': 'js',
		'fname': 'popper.min.js',
		'rev': '1.14.5',
		'url': 'https://unpkg.com/popper.js@{rev}/dist/umd',
		'license': 'MIT',
		'data': None
	},
	{
		'type': 'js',
		'fname': 'cytoscape-popper.js',
		'rev': '1.0.2',
		'url': 'https://unpkg.com/cytoscape-popper@{rev}',
		'license': 'MIT',
		'data': None
	},
	{
		'type': 'js',
		'fname': 'tippy.all.js',
		'rev': '2.6.0',
		'url': 'https://unpkg.com/tippy.js@{rev}/dist',
		'license': 'MIT',
		'data': None
	},
	{
		'type': 'css',
		'fname': 'Treant.css',
		'rev': 'a5f9562',
		'url': 'https://raw.githubusercontent.com/fperucic/treant-js/{rev}',
		'license': 'MIT',
		'data': None
	},
	{
		'type': 'js',
		'fname': 'vendor/raphael.js',
		'rev': 'a5f9562',
		'url': 'https://raw.githubusercontent.com/fperucic/treant-js/{rev}',
		'license': 'MIT',
		'data': None
	},
	{
		'type': 'js',
		'fname': 'Treant.js',
		'rev': 'a5f9562',
		'url': 'https://raw.githubusercontent.com/fperucic/treant-js/{rev}',
		'license': 'MIT',
		'data': None
	}
]

cexID = 0

def toNodeID(codePos):
	return codePos['fn'] + '_' + codePos['line']

def getDisplayMode(displayMode, hypo):
	if displayMode == DYNAMIC:
		numLeafs = treeCountLeafs(hypo['tree'])
		if numLeafs > 7:
			LOGGER.debug("%s: Using graph, leafs=%d\n" % (hypo['title'], numLeafs))
			return GRAPH
		return TREE
	else:
		return displayMode

def genLockCombTable(displayMode, lockCombinations):
	global cexID
	output = ''
	for lockComb, occurrences in lockCombinations.iteritems():
		if displayMode == TREE:
			indentA = util.genIndentation(2)
			indentB = util.genIndentation(3)
			multiLine = '\\'
		elif displayMode == GRAPH:
			indentA = util.genIndentation(8)
			indentB = util.genIndentation(9)
			multiLine = ''
		output = output + '{indentA}<tr>{multiLine}\n{indentB}<td>{:d}.{:d}</td><td>{:d}</td><td>'.format(hypothesisID, cexID, occurrences,
			indentA=indentA, indentB=indentB, multiLine=multiLine)
		# Split locks_held
		# Example: EMBSAME(j_barrier)@jbd2_journal_lock_updates@fs/jbd2/transaction.c:746, EMBSAME(j_state_lo0ck)@jbd2_journal_lock_updates@fs/jbd2/transaction.c:42
		if lockComb != "nolocks":
			locksHeld = lockComb.split(',')
			locksHeldLen = len(locksHeld)
			k = 0
			# Example: EMBSAME(j_barrier)@jbd2_journal_lock_updates@fs/jbd2/transaction.c:746
			for lockHeld in locksHeld:
				# Index 0: the lock
				# Index 1: the function where the lock was acquired
				# Index 2: the file and line where the lock was acquired
				elems = lockHeld.split('@')
				lockFile = elems[2].split(':')[0]
				lockLine = elems[2].split(':')[1]
				output = output + '{:02d}: <a class="lock" target="_blank" href="{}/source/{}#L{}" title="{}@{}:{}">{}</a>'.format(k + 1,
						crossRefURL, lockFile, lockLine, elems[1], lockFile, lockLine, elems[0])
				if k < (locksHeldLen - 1):
					output = output + '<br/>'
				k = k + 1
		else:
			output = output + 'No Locks'
		output = output + '</td>{multiLine}\n{indentA}</tr>{multiLine}\n'.format(indentA=indentA, indentB=indentB, multiLine=multiLine)
		cexID = cexID + 1
	return output

# tree-specific functions --- BEGIN
def newTreeNode(treeID):
	return { 'id': treeID, 'name': '', 'codePos': dict(), 'locks': dict(), 'children': dict(), 'pseudo': False}

def newTree(treeID):
	temp = newTreeNode(treeID)
	temp['name'] = 'userspace_root'
	temp['codePos']['file'] = None
	temp['codePos']['line'] = None
	temp['codePos']['fn'] = 'userspace'
	return temp

def createInitTreeNode(name, codePos, treeID):
	temp = newTreeNode(treeID)
	temp['name'] = name
	temp['codePos'] = codePos
	return temp

def treeDepth(curTree):
	maxChildDepth = 0
	for name, child in curTree['children'].iteritems():
		temp = treeDepth(child)
		if temp > maxChildDepth:
			maxChildDepth = temp
	return maxChildDepth + 1

def treeCountLeafs(curTree):
	if len(curTree['locks']) > 0 :
		return 1
	else:
		sumLeafs = 0
		for name, child in curTree['children'].iteritems():
			sumLeafs = sumLeafs + treeCountLeafs(child)
		return sumLeafs

def addPseudoNodes(child, num, treeID):
	if num == 0:
		return child
	pseudoNode = newTreeNode(treeID)
	pseudoNode['pseudo'] = True
	pseudoNode['name'] = 'pseudo_node'
	pseudoNode['codePos']['file'] = None
	pseudoNode['codePos']['line'] = None
	pseudoNode['codePos']['fn'] = 'pseudo_fn'

	temp = addPseudoNodes(child, num - 1, treeID)
	pseudoNode['children'][temp['name']] = temp

	return pseudoNode

def adjustSubtreeDepth(curTree, maxDepth, curDepth):
	for child in curTree['children'].values():
		# Found a leaf?
		if len(child['children']) == 0:
			pseudoTree = addPseudoNodes(child, maxDepth - (curDepth + 2), curTree['id'])
			del curTree['children'][child['name']]
			curTree['children'][pseudoTree['name']] = pseudoTree
		else:
			adjustSubtreeDepth(child, maxDepth, curDepth + 1)

def findTree(curTree, treeName):
	if curTree['name'] == treeName:
		return curTree
	else:
		found = None
		for name, child in curTree['children'].iteritems():
			found = findTree(child, treeName)
			if found is not None:
				break
		return found

def toTreeNodeID(codePos):
	return toNodeID(codePos)

def printIndentation(lvl):
	for i in range(0, lvl):
		print("	", end="")

def printTree(baseURL, tree, depth, indentLvl):
	printIndentation(indentLvl)
	print("{")
	if tree['pseudo']:
		printIndentation(indentLvl + 1)
		print('pseudo: true,')
	if len(tree['locks']) > 0 :
		printIndentation(indentLvl + 1)
		print('HTMLclass: \'cexnode\',')
	printIndentation(indentLvl + 1)
	print('innerHTML: \'', end="")
	if len(tree['locks']) > 0 :
		print("\\")
		print("""<div class="cexlist node-desc cexlist_{hypoID:d}">\\
	<p class="cexlist-title"><a target="_blank" href="{crossRefURL}/source/{codeFile}#L{codeLine}" title="{codeFile}:{codeLine}">{codeFN}</a></p><span class="cexlist-title">Found memory accesses violating the hypothesis:</span>\\
	<table>\\
		<tr>\\
			<th>ID</th><th>Occurrences</th><th><span style="color:red">Locks actually held<br/>(in order locks were taken)</span></th>\\
		</tr>\\""".format(hypoID=tree['id'], crossRefURL=crossRefURL, codeFile=tree['codePos']['file'], codeLine=tree['codePos']['line'], codeFN=tree['codePos']['fn']))
		print(genLockCombTable(TREE, tree['locks']), end='')
		print("""	</table>\\
</div>""", end="")
	else:
		if tree['codePos']['file'] is not None:
			print('<a target="_blank" href="{crossRefURL}/source/{codeFile}#L{codeLine}" title="{codeFile}:{codeLine}">{codeFN}</a>'
				.format(crossRefURL=crossRefURL, codeFile=tree['codePos']['file'], codeLine=tree['codePos']['line'], codeFN=tree['codePos']['fn']), end="")
		else:
			print('<span class="node-desc">%s</span>' % (tree['codePos']['fn']),end="")
	print('\'', end="")
	childrenLen = len(tree['children'])
	if childrenLen > 0:
		print(',')
		printIndentation(indentLvl + 1)
		print('children: [')
		i = 0
		for child in tree['children'].itervalues():
			printTree(baseURL, child, depth + 1, indentLvl + 1)
			if i < (childrenLen - 1):
				print(',')
			else:
				print('')
			i = i + 1
		printIndentation(indentLvl + 1)
		print(']')
	else:
		print('')
	printIndentation(indentLvl)
	print('}', end="")
# tree-specific functions --- END
# graph-specific functions --- BEGIN
def newGraphNode():
	return {'id': '', 'codePos': dict(), 'locks': dict()}

def createInitGraphNode(name, codePos):
	temp = newGraphNode()
	temp['id'] = name
	temp['codePos'] = codePos
	return temp

def toGraphNodeID(codePos):
	return toNodeID(codePos)

def toEdgeID(parentNode, childNode):
	return parentNode['id'] + '_' + childNode['id']

def emplaceGraphNode(nodeDict, traceElem):
	elems = traceElem.split('@')
	codePos = dict()
	codePos['file'] = elems[2].split(':')[0]
	codePos['line'] = elems[2].split(':')[1]
	codePos['fn'] = elems[1]

	nodeID = toGraphNodeID(codePos)
	if nodeID in nodeDict:
		node = nodeDict[nodeID]
	else:
		node = createInitGraphNode(nodeID, codePos)
		nodeDict[nodeID] = node
	return node

def createInitNodesDict():
	nodes = dict()
	temp = newGraphNode()
	temp['id'] = USERSPACE_ID
	temp['codePos']['file'] = None
	temp['codePos']['line'] = None
	temp['codePos']['fn'] = USERSPACE_FN
	nodes[USERSPACE_ID] = temp
	return nodes
# graph-specific functions --- END

if __name__ == '__main__':
	parser = argparse.ArgumentParser()
	parser.add_argument('-v', '--verbose', action='store_true', help='Be verbose')
	parser.add_argument('-u', '--crossrefurl', help='Base URL to the Linux cross reference site', default='https://elixir.bootlin.com/linux/v4.10')
	parser.add_argument('-c', '--cachedir', help='Activate caching of JS and CSS files. Specifies the directory where the downloaded files are stored.', default=None)
	parser.add_argument('-d', '--display-mode', choices=[GRAPH, TREE, DYNAMIC], help='Display mode of the callgraph: graph vs. tree', default=TREE)
	parser.add_argument('cexcsv', help='Input file containing the ground truth')
	parser.add_argument('vmlinux', help='VMLINUX')
	parser.add_argument('hypothesescsv', help='Input file containing preditions made by the hypothesizer (winner hypotheses only)')
	args = parser.parse_args()

	cexCSV = args.cexcsv
	vmlinux = args.vmlinux
	crossRefURL = args.crossrefurl
	hypothesesCSV = args.hypothesescsv
	cacheDir = args.cachedir
	displayMode = args.display_mode
	separator = ';'
	count = 0
	cexDict = dict()
	hypothesesDict = dict()
	if args.verbose:
		LOGGER.setLevel(logging.DEBUG)
		util.setVerbose()
	else:
		LOGGER.setLevel(logging.INFO)

	util.downloadExtContent(extContent, cacheDir=cacheDir)

	hypothesesDict = util.readHypothesesDict(hypothesesCSV)
	print('<!DOCTYPE html>\n<!--')
	util.printLicenseExtConent(extContent)
	print('-->')
	print("""<html>
<head>
<meta charset="UTF-8">
<style>
body {
	font-family: monospace;
	margin: 0px;
	overflow: scroll;
}

h1, h2 {
	text-align: center;
}

.cex {
}

.tree {
	/* 
	 * If we use the Treant js library, the cex div *must* be shown.
	 * Otherwise, the tree won't be drawn in a readable way.
	 * It will be hidden after the body has been loaded by JavaScript.
	 */
	display: block;
}

.graph {
	display: none;
}

.cexlist-title {
	font-size: medium;
	font-weight: bold;
}

.cexlist table {
	margin-left: auto;
	margin-right: auto;
}

.cexlist th {
	background-color: #eee;
	vertical-align: middle;
	padding: 0.3em;
}

.cexlist tr {
	border-bottom: 1px solid #7192a8;
	background-color:#efefef;
}

.cexlist td {
	padding: 0.3em;
	text-align:left;
}

.cexlists {
	margin-bottom: 20px;
	text-align: center;
}

.cexlistcontainer {
	display: inline-block;
	/*
	 * If one ever changes one of these margins below, 
	 * he or she has to adapt the factor in resizeGraph() as well.
	 * In line: sumWidth + cexListArr.length * 8 <-- this one.
	 */
	margin-left: 4px;
	margin-right: 4px;
}

.cexlist {
	display: inline-block;
	text-align: center;
	border-radius: 25px;
	border: 2px solid #000000;
	padding-bottom: 20px;
}

.cexgraph {
	margin-left: auto;
	margin-right: auto;
	width: 100%;
	height: 0px;
}

a.source-link,
a.source-link:hover,
a.source-link:visited {
	color: #ffffff;
}

a:visited {
	color:blue;
}

.btn {
	border: 2px solid black;
	background-color: white;
	color: black;
	padding: 8px 10px;
	font-size: 14px;
	cursor: pointer;
}

/* Gray */
.btn.default {
	border-color: #e7e7e7;
	color: black;
}

.btn.default.default:hover {
	background: #e7e7e7;
}

/* Hide zoom button by default */
#zoombtn {
	display: none;
}

/* The sidebar menu - Thx to https://www.w3schools.com/howto/howto_css_fixed_sidebar.asp */
/* Sidenav overlay - https://www.w3schools.com/howto/howto_js_sidenav.asp */
.sidebar {
	height: 100%; /* Full-height: remove this if you want "auto" height */
	position: fixed; /* Fixed Sidebar (stay in place on scroll) */
	/*
	 * Use a z-index above the one used by #heading, which is 30. 
	 * This ensure the sidebar will always be on top.
	 */
	z-index: 31;
	top: 0; /* Stay at the top */
	background-color: #eee;
	overflow-x: hidden; /* Disable horizontal scroll */
	width: 0;
	padding-top: 20px;
	transition: 0.5s;
}

.hypothesis {
	display: none;
	text-align: center;
	margin-top: 20px;
}

.cextree {
	margin-left: auto;
	margin-right: auto;
}

#container {
	height: 100%;
	width: 100%;
}

#desc {
	text-align: center;
	margin-top: 20px;
	font-size: 18px;
}

#sidenav {
	left: 0;
}

#legend {
	right:0;
	float: right;
}

/* The navigation menu links */
#sidenav a, #legend a {
	padding: 6px 8px 6px 16px;
	text-decoration: none;
	font-size: 12px;
	color: #111;
	display: block;
}

/* When you mouse over the navigation links, change their color */
#sidenav a:hover, #legend a {
	color: #7D7D7D;
}

#sidenav #closebtn, #legend #closebtn {
	position: absolute;
	top: 0;
	right: 25px;
	font-size: 36px;
	margin-left: 50px;
}

#heading {
	overflow: hidden;
	background-color: #eee;
	z-index: 30;
	position: fixed;
	top: 0;
	left: 0;
	width: 100%;
}

/* Style page content */
#main {
	padding: 0px;
	position: relative;
	top: 200px;
}

/* On smaller screens, where height is less than 450px, change the style of the sidebar (less padding and a smaller font size) */
@media screen and (max-height: 450px) {
	.sidenav {padding-top: 15px;}
	.sidenav a {font-size: 18px;}
}

/* Adapted tree layout based on collapsable.css --- BEGIN */
/* http://fperucic.github.io/treant-js/examples/collapsable/collapsable.css */
.Treant .node.cexnode {
	/*
	 * Hide the usual node border if the node
	 * contains counterexamples. The border is provided by the 
	 * 'cexlist' class.
	 * The additional class 'cexnode' is added by the python script.
	 */
	border: none;
}
.Treant > .node {
	padding: 3px;
	border: 1px solid #484848;
	border-radius: 3px;
}
.Treant .node .node-desc {
	/* Use relative positioning for the z-index to be effective. */
	position: relative;
	/*
	 * Use a z-index above the one used by normal nodes.
	 * This ensures that the link (to elixir) is reachable.
	 */
	z-index:1;
	position: relative;
}
.Treant .node .node-title {
	font-size: larger;
	text-align: center;
	margin: 4px;
}
/* Adapted tree layout based on collapsable.css --- END */""")
	util.printExtContent(extContent, 'css')
	print("""</style>
</head>
<body onLoad="hideCexs('tree');fixCexTreeSize();">
	<div class="sidebar" id="legend">
		<a href="javascript:void(0)" id="closebtn" onclick="closeBar('legend')">&times;</a>
		<h1>Legend</h1>
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
	</div>""")
	lastKey = None
	hypothesisID = 0
	hypothesesList = []
	tree = None
	nodes = None
	edges = None
	hypothesisTitle = None
	hypothesisText = None
	hypothesisDesc = None

	tempFile = open(cexCSV,'rb')
	tempReader = csv.DictReader(tempFile, delimiter=';')
	for line in tempReader:
		key = (line['data_type'], line['member'], line['accesstype'])
		if key not in hypothesesDict:
			LOGGER.error('Key %s does not exist in %s' %(key, hypothesesCSV))
			continue
		else:
			hypothesesEntry = hypothesesDict[key]
		# Print a header for each tuple of (data_type, member, accesstype)
		if lastKey != key:
			# Save hypothesis info (id, text, description and {graph,tree}) before we process a new hypothesis
			if edges is not None and nodes is not None and tree is not None:
				temp = { 'title': hypothesisTitle, 
						 'id': hypothesisID,
						 'desc': hypothesisDesc,
						 'tree': tree,
						 'nodes': nodes,
						 'edges': edges}
				temp['displaymode'] = getDisplayMode(displayMode, temp)
				hypothesesList.append(temp)
			hypothesisID = hypothesisID + 1
			# The header contains information about the accessed member like
			# the access type or the locking rule.
			# Moreover, it shows statistics about the locking rule, e.g., the fraction ('percentage') of all accesses ('total') that adhere to that rule.
			# E.g.: 94.6% (19069 out of 20155 mem accesses under locks): EMBOTHER(inode:i_rwsem)
			locksHeldKey = hypothesesEntry['locks'].keys()[0]
			locksHeldEntry = hypothesesEntry['locks'][locksHeldKey]
			hypothesisTitle = line['accesstype'] + ':' + line['member']
			hypothesisDesc = """<b>Hypothesis %d</b>: When <b>%s %s.%s</b> the following locks <span style="color:green;font-weight:bold;">should be held</span>: <span style="font-weight:bold;color:blue;">%s</span><br/>
					<b>%2.2f%%</b> (%d out of %d mem accesses under locks)""" % (hypothesisID, 'reading' if line['accesstype'] == 'r' else 'writing', line['data_type'], line['member'],
					locksHeldKey, locksHeldEntry['percentage'], locksHeldEntry['occurrences'], locksHeldEntry['total'])
			nodes = createInitNodesDict()
			edges = dict()
			tree = newTree(hypothesisID)
		lastKey = key

		locks = dict()
		# Example:
		# EMBSAME(j_barrier)@jbd2_journal_lock_updates@fs/jbd2/transaction.c:746#1+EMBSAME(j_barrier)@jbd2_journal_lock_updates@fs/jbd2/transaction.c:746#1
		locksHeldList = line['locks_held'].split('+')
		for entry in locksHeldList:
			# $locks_held#occurrences
			# Example:
			# EMBSAME(j_barrier)@jbd2_journal_lock_updates@fs/jbd2/transaction.c:746#1
			locksHeld = entry.split('#')[0]
			occurrences = int(entry.split('#')[1])
			if locksHeld in locks:
				sys.stderr.write("ERROR: %s\n" % locksHeld)
			locks[locksHeld] = occurrences

		# Extract stacktrace
		traceElems = line['stacktrace'].split(',')
		# Since we want a top-down view, we reverse the stacktrace.
		traceElems.reverse()
		traceElemsLen = len(traceElems)
		# Convert the stacktrace into humanreadable information
		i = 0
		formattedStacktrace = ""
		treeIter = tree
		for i in range(0,traceElemsLen - 1):
			# Graph stuff
			parentNode = emplaceGraphNode(nodes, traceElems[i])
			childNode = emplaceGraphNode(nodes, traceElems[i + 1])
			# Since there is more than one entry point to the kernel,
			# we won't have a single root node for each cex tree.
			# We fake *the* root node, which represents the userspace,
			# and create edges from that node the frist entry of each stacktrace.
			if i == 0:
				userspaceNode = nodes[USERSPACE_ID]
				edgeID = toEdgeID(userspaceNode, parentNode)
				if edgeID not in edges:
					edges[edgeID] = (userspaceNode, parentNode)
			edgeID = toEdgeID(parentNode, childNode)
			if edgeID not in edges:
				edges[edgeID] = (parentNode, childNode)
			# Reached the last edge of the stacktrace: parentIter -> childIter
			# childNode is the stacktrace entry that corresponds to the actual memory access
			if i == (traceElemsLen - 2):
				for locksHeld, occurrences in locks.iteritems():
					if locksHeld in childNode['locks']:
						childNode['locks'][locksHeld] = childNode['locks'][locksHeld] + occurrences
					else:
						childNode['locks'][locksHeld] = occurrences
			# Tree stuff
			elems = traceElems[i].split('@')
			codePos = dict()
			codePos['file'] = elems[2].split(':')[0]
			codePos['line'] = elems[2].split(':')[1]
			codePos['fn'] = elems[1]
			parentID = toTreeNodeID(codePos)
			parentIter = findTree(treeIter, parentID)

			# Found another root node
			if parentIter is None:
				parentIter = createInitTreeNode(parentID, codePos, tree['id'])
				tree['children'][parentID] = parentIter
			treeIter = parentIter

			elems = traceElems[i + 1].split('@')
			codePos['file'] = elems[2].split(':')[0]
			codePos['line'] = elems[2].split(':')[1]
			codePos['fn'] = elems[1]
			childID = toTreeNodeID(codePos)
			if childID in parentIter['children']:
				childIter = parentIter['children'][childID]
			else:
				childIter = createInitTreeNode(childID, codePos, tree['id'])
				parentIter['children'][childID] = childIter
			# Reached the last edge of the stacktrace: parentIter -> childIter
			# childIter is the stacktrace entry that corresponds to the actual memory access
			if i == (traceElemsLen - 2):
				for locksHeld, occurrences in locks.iteritems():
					if locksHeld in childIter['locks']:
						childIter['locks'][locksHeld] = childIter['locks'][locksHeld] + occurrences
					else:
						childIter['locks'][locksHeld] = occurrences
	temp = { 'title': hypothesisTitle,
			 'id': hypothesisID,
			 'desc': hypothesisDesc,
			 'tree': tree,
			 'nodes': nodes,
			 'edges': edges}
	temp['displaymode'] = getDisplayMode(displayMode, temp)
	hypothesesList.append(temp)

	print("""	<div class="sidebar" id="sidenav">
		<a href="javascript:void(0)" id="closebtn" onclick="closeBar('sidenav')">&times;</a>
		<h1>Members</h1>""")
	for value in hypothesesList:
		print('			<a href="#" onClick="toogleCex({hypoID:d});closeBar(\'sidenav\');">{hypoTitle}</a>'.format(hypoID=value['id'], hypoTitle=value['title']))
	print("""	</div>
	<div id="heading">
		<h1>Counterexamples</h1>
		<div style="text-align:center;">
			<button class="btn default" onclick="openBar('sidenav', '10%')">Show Member List</button>&nbsp;
			<button class="btn default" onclick="openBar('legend', '20%')">Show Legend</button>&nbsp;
			<button class="btn default" id="zoombtn" onclick="toggleZoom();">Enable Zoom</button>
		</div>
		<div id="desc">To view counterexamples, please select one member from the member list.</div>""")
	for value in hypothesesList:
		print("""			<div class="hypothesis" id="hypothesis_%d">%s
			</div>""" % (value['id'], value['desc']))
	print("""	</div>
	<div id="main">""")
	for value in hypothesesList:
		cexID = 1
		print('		<div class="cex %s" id="cex_%d">' % (value['displaymode'], value['id']))
		if value['displaymode'] == GRAPH:
			# Count nodes that have counterexamples attached
			lockCombsDistinct = 0
			for nodeID, node in value['nodes'].iteritems():
				if len(node['locks']) > 0:
					lockCombsDistinct = lockCombsDistinct + 1

			print('			<div class="cexlists" id="cexlists_%d">' % (value['id']))
			i = 0
			for nodeID, node in value['nodes'].iteritems():
				lenLockCombs = len(node['locks'])
				if lenLockCombs == 0:
					continue
				width = 100
				if lockCombsDistinct > 1:
					width = (100 / lockCombsDistinct) - 1
				if i == 0:
					print(util.genIndentation(4), end='')
					i = 1
				print("""<div class="cexlistcontainer">
					<div class="cexlist cexlist_{hypoID:d}">
						<p class="cexlist-title"><a target="_blank" href="{crossRefURL}/source/{file}#L{line}" title="{file}:{line}">{fn}</a></p><span class="cexlist-title">Found memory accesses violating the hypothesis:</span>
						<table>
							<tr>
								<th>ID</th><th>Occurrences</th><th><span style="color:red">Locks actually held<br/>(in order locks were taken)</span></th>
							</tr>""".format(hypoID=value['id'], width=width, lockCombs=lockCombsDistinct, crossRefURL=crossRefURL,
								 file=node['codePos']['file'], line=node['codePos']['line'], fn=node['codePos']['fn']))
				print(genLockCombTable(value['displaymode'], node['locks']), end="")
				print("""						</table>
					</div>
				</div>""", end="")
			print('\n				</div>')
			print('			<div class="cexgraph" id="cexgraph_%d"></div>' % (value['id']))
		elif value['displaymode'] == TREE:
			print('			<div class="cextree" id="cextree_%d"></div>' % (value['id']))
		print('		</div>')
	print("""	</div>
<script>""")
	util.printExtContent(extContent, 'js')
	print("""</script>
<script>
	window.visibleCexGraph = null;
	window.hypothesisID = null;
	//window.addEventListener("resize", resizeGraph);
	//var resizeTimer;
	function resizeGraph() {
		if (window.visibleCexGraph == null) {
			return;
		}
		//clearTimeout(resizeTimer);
		//resizeTimer = setTimeout(function() {
			var cexLists = document.getElementById('cexlists_' + window.hypothesisID);
			var cexListArr = document.getElementsByClassName('cexlist_' + window.hypothesisID);
			var i, sumWidth = 0;
			for (i = 0; i < cexListArr.length; i++) {
				sumWidth += cexListArr[i].offsetWidth;
			}
			if (sumWidth > cexLists.clientWidth) {
				var temp = sumWidth + cexListArr.length * 8;
				cexLists.style.width = temp + 'px';
				var cexGraphCont = document.getElementById('cexgraph_' + window.hypothesisID);
				cexGraphCont.style.width = temp + 'px';
			}
			window.visibleCexGraph.resize();
			/* A value of 1.2 has proven to be 'good'. */
			window.visibleCexGraph.zoom(1.2);
			/* Center and fit the graph in the viewport. */
			window.visibleCexGraph.center();
			window.visibleCexGraph.fit();
			/* Move the graph to the top of the viewport (y=0). */
			var pos = window.visibleCexGraph.pan();
			window.visibleCexGraph.pan({ x: pos.x, y: 0});
		//}, 500);
	};
	function toggleZoom() {
		if (window.visibleCexGraph == null) {
			alert('No graph displayed!');
			return;
		}
		var zoomBtn = document.getElementById('zoombtn');
		if (window.visibleCexGraph.userZoomingEnabled()) {
			window.visibleCexGraph.userZoomingEnabled(false);
			window.visibleCexGraph.userPanningEnabled(false);
			zoomBtn.innerText = 'Enable Zoom';
		} else {
			window.visibleCexGraph.userZoomingEnabled(true);
			window.visibleCexGraph.userPanningEnabled(true);
			zoomBtn.innerText = 'Disable Zoom';
		}
	};
	/* Sidenav overlay - https://www.w3schools.com/howto/howto_js_sidenav.asp --- Begin */
	function openBar(elemID, width) {
		document.getElementById(elemID).style.width = width;
	}
	/* Set the width of the side navigation to 0 */
	function closeBar(elemID) {
		document.getElementById(elemID).style.width = "0";
	}
	/* Sidenav overlay - https://www.w3schools.com/howto/howto_js_sidenav.asp --- End */
	function hideCexs(addClass = null) {
		// Hide alle cex trees
		var classes = 'cex';
		if (addClass != null) {
			classes = classes + ' ' + addClass
		}
		var cexs = document.getElementsByClassName(classes);
		var i;
		for (i = 0; i < cexs.length; i++) {
			cexs[i].style.display = 'none';
		}
	}
	/* graph-specific functions --- BEGIN */
	function toogleCex(hypoNo) {
		// Hide description box
		document.getElementById('desc').style.display = 'none';
		// Hide all other cex
		hideCexs();
		// Display the cex for the selected member
		document.getElementById('cex_' + hypoNo).style.display = 'block';
		// Hide all hypotheses
		var hypotheses = document.getElementsByClassName('hypothesis');
		var i;
		for (i = 0; i < hypotheses.length; i++) {
			hypotheses[i].style.display = 'none';
		}
		// Display the hyothesis for the selected member
		document.getElementById('hypothesis_' + hypoNo).style.display = 'block';
		// Do the tree-specific or graph-specific stuff
		var divCex = document.getElementById('cex_' + hypoNo);
		if (divCex.className.includes('tree')) {
			document.getElementById('zoombtn').style.display = 'none';
		} else if (divCex.className.includes('graph')) {
			document.getElementById('zoombtn').style.display = 'inline';
			var cexGraph = eval('cexGraph_' + hypoNo);
			// Display the cex tree for the selected member
			var cexGraphCont = document.getElementById('cexgraph_' + hypoNo);
			cexGraphCont.style.height = window.innerHeight * 2 + 'px';
			// Redraw the graph and fit it into the container
			window.visibleCexGraph = cexGraph;
			window.hypothesisID = hypoNo;
			resizeGraph();
			var zoomBtn = document.getElementById('zoombtn');
			if (window.visibleCexGraph.userZoomingEnabled()) {
				zoomBtn.innerText = 'Disable Zoom';
			} else {
				zoomBtn.innerText = 'Enable Zoom';
			}
		}
	};
	function makeTag(tag, attrs, children) {
		var el = document.createElement(tag);

		Object.keys(attrs).forEach(function(key) {
			var val = attrs[key];
			el.setAttribute(key, val);
		});

		children.forEach(function(child) {
			el.appendChild(child);
		});
		return el;
	};
	function makeText(text) {
		var el = document.createTextNode(text);
		return el;
	};
	function makeTippy(node, html) {
		return tippy( node.popperRef(), {
			html: html,
			trigger: 'manual',
			arrow: true,
			placement: 'bottom',
			theme: 'dark',
			hideOnClick: false,
			interactive: true,
			/*
			 * Since the graph itself has a z-index of -1,
			 * an index of 0 will make that tippy reachable.
			 * It does not conflict with the heading, which uses
			 * a z-index of 30 or above.
			 */
			zIndex: 0
		} ).tooltips[0];
	};
	function hideTippy(node) {
		var tippy = node.data('tippy');
		if(tippy != null){
			tippy.hide();
		}
	};
	function hideAllTippies() {
		if (window.visibleCexGraph != null) {
			window.visibleCexGraph.nodes().forEach(hideTippy);
		}
	};
	var cexTreeLayout = {
		name: 'dagre',
		animate: false,
		nodeSep: 80,
		rankSep: 30,
		padding: 0,
		fit: false,
		nodeDimensionsIncludeLabels: true,
		ranker: 'longest-path',
		rankDir: 'TB'
	}
	var cexTreeStyle =  [
		{
			selector: 'node',
			style: {
				'label': 'data(fn)',
				'height': 'label',
				'width': 'label',
				'text-halign': 'center',
				'text-valign': 'center',
				'background-color': '#ffffff',
				'shape' : 'rectangle'
			}
		},
		{
			selector: 'edge',
			style: {
				'curve-style': 'bezier',
				'target-arrow-shape': 'triangle',
				'width': 3,
				'line-color': '#000000',
				'target-arrow-color': '#000000'
			}
		},
		{
			selector: '.highlighted',
			style: {
				'background-color': '#61bffc',
				'line-color': '#61bffc',
				'target-arrow-color': '#61bffc',
				'transition-property': 'background-color, line-color, target-arrow-color',
				'transition-duration': '0.5s'
			}
		}
	];
	/* graph-specific function --- END */
	/* tree-specific functions --- BEGIN */
	/*
	 * Resize the container div (class cextree) of each cex tree to the same size as the tree (aka the svg).
	 * Since the #main div which contains all cex trees is as width as 
	 * the window, a scrollbar will be visible in the #main div.
	 */
	function fixCexTreeSize() {
		var cextrees = document.getElementsByClassName('cextree');
		var i;
		for (i = 0; i < cextrees.length; i++) {
			var svg = cextrees[i].getElementsByTagName('svg')[0];
			/*
			 * Information about SVGAnimated*:
			 * - https://developer.mozilla.org/en-US/docs/Web/API/SVGAnimatedLength
			 * - https://developer.mozilla.org/en-US/docs/Web/API/SVGLength
			 */
			cextrees[i].style.width = svg.width.baseVal.value + 'px';
		}
	};
	/* tree-specific functions --- END */""")
	for hypo in hypothesesList:
		cexID = 1
		if hypo['displaymode'] == GRAPH:
			print("""	var cexGraphConfig_%d = {
			nodes: [""" % (hypo['id']))
			nodesLen = len(hypo['nodes'])
			i = 0
			for nodeID, node in hypo['nodes'].iteritems():
				print("			{ data : { id: '%s', name: '%s', 'fn': '%s', 'file': '%s', 'line': '%s', lockComb: '"
					% (node['id'], node['codePos']['fn'], node['codePos']['fn'], node['codePos']['file'], node['codePos']['line']), end="")
				#for lockComb in node['locks']:
				#	print('%s ' %(lockComb), end="")
				print("'}, selected: false, selectable: true, locked: false, grabbable: false}", end="")
				if i < nodesLen - 1:
					print(',')
				i = i + 1
			print("""
			],
			edges: [""")
			edgesLen = len(hypo['edges'])
			i = 0
			for edgeID, edge in hypo['edges'].iteritems():
				print("			{ data : { id: '%s', source: '%s', target: '%s'}}" % (edgeID, edge[0]['id'], edge[1]['id']), end="")
				if i < edgesLen - 1:
					print(',')
				i = i + 1
			print("""
		]
	}};
	var cexGraph_{hypoID:d} = cytoscape({{
		container: document.getElementById('cexgraph_{hypoID:d}'),
		boxSelectionEnabled: false,
		autounselectify: true,
		layout: cexTreeLayout,
		style: cexTreeStyle,
		panningEnabled: true,
		userPanningEnabled: false,
		zoomingEnabled: true,
		userZoomingEnabled: false,
		wheelSensitivity: 0.5,
		elements: cexGraphConfig_{hypoID:d}
	}});
	cexGraph_{hypoID:d}.on('tap', function(e) {{
		if(e.target === window.visibleCexGraph) {{
			hideAllTippies();
		}}
	}});
	cexGraph_{hypoID:d}.on('tap', 'edge', function(e) {{
		hideAllTippies();
	}});
	cexGraph_{hypoID:d}.nodes().forEach(function(n) {{
		var g = n.data('name');

		if (n.data('file') == 'None') {{
			return;
		}}
		linkCode = makeTag('a', {{ target: '_blank',
			href: '{crossRefURL}/source/'+n.data('file') + '#L' + n.data('line'),
			'class': 'source-link' }}, [ makeText(n.data('fn') + '@' + n.data('file') + ':' + n.data('line')) ]);
		var tippy = makeTippy(n, makeTag('div', {{}}, [linkCode]));
		n.data('tippy', tippy);
		n.on('mouseover', function(e) {{
			tippy.show();
			cexGraph_{hypoID:d}.nodes().not(n).forEach(hideTippy);
		}});
	}});""".format(hypoID=hypo['id'], crossRefURL=crossRefURL))
		elif hypo['displaymode'] == TREE:
			adjustSubtreeDepth(hypo['tree'], treeDepth(hypo['tree']), 0)
			print("""	var cextree_%d_config = {
			chart : {
				container: "#cextree_%d",
				animateOnInit: false,
				nodeAlign: 'BOTTOM',
				node: {
					collapsable: false
				},
				connectors: {
					type: 'curve',
					style: {
						'arrow-end': 'classic-wide-midium',
						'stroke-width': 3
					}
				},
				levelSeparation: 30, /* Default Value: 30 */
				siblingSeparation: 30, /* Default Value: 30 */
				scrollbar: 'none'
			},
			nodeStructure: """ % ( hypo['id'], hypo['id']))
			printTree(crossRefURL, hypo['tree'], 0, 2)
			print('\n	};')
			print('	cextree_%d = new Treant(cextree_%d_config);' % (hypo['id'], hypo['id']))
	print("	</script>")
	print("""</body>
</html>""")
	tempFile.close()


