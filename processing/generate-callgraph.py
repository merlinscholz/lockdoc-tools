#!/usr/bin/python
# A. Lochmann 2018
# This script generates a complete callgraph for one particular trace donated by the database name.
# Each edge is labeled with number of calls made by the source node.
# The script might be called like this:
# ./generate-callgraph.py --host manos --database lockdebugging_mixed_fs_al --user <user> --password <pw> | dot -Tsvg -o foo.svg

# ToDo
# - Find root nodes
# - Allow printing of subtrees

import sys
import MySQLdb
import logging
import argparse 
import subprocess

# Maps all edges seen in the input to the number of calls.
edges = dict()
# Caches a node's resolved function name
nodes = dict()

logging.basicConfig()                               
LOGGER = logging.getLogger(__name__)  

def main():
	

	parser = argparse.ArgumentParser()          
	parser.add_argument('-v', '--verbose', action='store_true', help='Be verbose')
	parser.add_argument('-k', '--host',  help='Host whre the database runs on', required=True) # -h is already in use by help
	parser.add_argument('-d', '--database', help='The database to perform the query on', required=True)
	parser.add_argument('-u', '--user', help='The user used to login', required=True)
	parser.add_argument('-p', '--password', help='The user used to login', required=True)
	parser.add_argument('-s', '--struct', help='Filter by struct', action='store')
	parser.add_argument('-m', '--member', help='Filter by member', action='store')
	parser.add_argument('-t', '--accesstype', help='Filter by access type', action='store')
	args = parser.parse_args()                  

	if args.verbose:
		LOGGER.setLevel(logging.DEBUG)
	else:
		LOGGER.setLevel(logging.INFO)

	host = args.host
	database = args.database
	user = args.user
	password = args.password
	if args.member:
		memberfilter="AND mn.id = (SELECT id FROM member_names WHERE name = '" + args.member + "')"
	else:
		memberfilter=""
	if args.struct:
		types = args.struct.split(':')
		if len(types) == 1:
			datatypefilter="AND sc.data_type_id = (SELECT id FROM data_types WHERE name = '" + types[0] + "')"
		else:
			datatypefilter="AND sc.id = (SELECT id FROM subclasses WHERE name = '" + types[1] + "') AND sc.data_type_id = (SELECT id FROM data_types WHERE name = '" + types[0] + "')"
	else:
		datatypefilter=""
	if args.accesstype:
		accesstypefilter="AND ac.type = '" + args.accesstype + "'"
	else:
		accesstypefilter=""
	
	# Fetach *all* stacktraces, and count them.
	# For now, we also include stacktrace which might be ignored due to 
	# blacklists.
	query = 'SELECT GROUP_CONCAT( \
					CONCAT("0x", HEX(st.instruction_ptr), "@", st.function, "@", st.file, ":", st.line) \
					ORDER BY st.sequence \
					SEPARATOR "," \
				) AS stacktrace, COUNT(*) \
			 FROM accesses AS ac \
			 INNER JOIN stacktraces AS st \
			  ON ac.stacktrace_id = st.id\
			 JOIN allocations a\
			  ON ac.alloc_id = a.id\
			 JOIN subclasses AS sc \
			  ON a.subclass_id = sc.id \
			 LEFT JOIN structs_layout_flat sl\
			  ON sc.data_type_id = sl.data_type_id\
			  AND ac.address - a.base_address = sl.helper_offset\
			 LEFT JOIN member_names mn\
			  ON mn.id = sl.member_name_id\
			 WHERE 1\
			  {member}\
			  {datatype}\
			  {accesstype}\
			 GROUP BY ac.stacktrace_id;'.format(member=memberfilter,datatype=datatypefilter,accesstype=accesstypefilter)

	db = MySQLdb.connect(host,user,password,database)
	cursor = db.cursor()

	try:
		cursor.execute(query)
		results = cursor.fetchall()
		for row in results:
			i = 0
			traceElems = row[0].split(',')
			length = len(traceElems)
			LOGGER.debug("%s --> %s", str(traceElems), str(row[1]))
			# Convert the stacktrace into edges
			# Each returnaddress will be used twice except the first and very last one.
			for i in range(0,length - 2):
				key = (traceElems[i + 1],traceElems[i])
				# If we've seen this edge before, accumulate the count.
				if key in edges:
					edges[key] += row[1]
				else:
					edges[key] = row[1]
				LOGGER.debug("%s --> %s", str(key), str(edges[key]))
	except Exception as e:
		print 'Error: ' + str(e)
		sys.exit(1)

	print 'digraph kernel_callgraph {'
	print 'rankdir=TB;'
	print 'size="8,5"'
	for key, value in edges.iteritems():
		# Resolve each node's address, and cache its function name.
		# If we see a node for the first time, print its label.
		# Later on, only a node's pseudoname (aka <function name>_<linenumber>) will be printed.
		if key[0] not in nodes:
			elems = key[0].split('@')
			codePosFile = elems[2].split(':')[0]
			codePosLine = elems[2].split(':')[1]
			codePosFn = elems[1]
			temp = {'node': codePosFn + '_' + codePosLine, 'label': '[label="' + codePosFn + ':' + codePosLine + '"]'}
			nodes[key[0]] = temp
			print temp['node'] + ' ' + temp['label']
		if key[1] not in nodes:
			elems = key[1].split('@')
			codePosFile = elems[2].split(':')[0]
			codePosLine = elems[2].split(':')[1]
			codePosFn = elems[1]
			temp = {'node': codePosFn + '_' + codePosLine, 'label': '[label="' + codePosFn + ':' + codePosLine + '"]'}
			nodes[key[1]] = temp
			print temp['node'] + ' ' + temp['label']
		print nodes[key[0]]['node'] + ' -> ' + nodes[key[1]]['node'] + ' [ label="' + str(value) + '"]'
		LOGGER.debug("(%s [%s],%s [%s]) --> %d", str(key[0]), nodes[key[0]], str(key[1]), nodes[key[1]], value)
        print '}'
	db.close()

if __name__ == '__main__':
	main()
