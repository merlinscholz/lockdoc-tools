#!/usr/bin/python
# A. Lochmann 2018
# This scripts determines the amount of atomic_t members
# that need locks and which don't according to our trace.
# The script might be called like this:
# ./eval-atomic-member.py --host manos --database lockdebugging_mixed_fs_al --user <user> --password <pw> 


import csv
import sys
import psycopg2
import logging
import argparse 
import subprocess
from pprint import pprint, pformat

noLockString = '(no locks held)'

logging.basicConfig()
LOGGER = logging.getLogger(__name__)  

#all-txns-members-locks-hypo-winner-nostack-nosubclasses.csv

def main():
	parser = argparse.ArgumentParser()
	parser.add_argument('-v', '--verbose', action='store_true', help='Be verbose')
	parser.add_argument('-k', '--host',  help='Host whre the database runs on', required=True) # -h is already in use by help
	parser.add_argument('-d', '--database', help='The database to perform the query on', required=True)
	parser.add_argument('-u', '--user', help='The user used to login', required=True)
	parser.add_argument('-p', '--password', help='The user used to login', required=True)
	parser.add_argument('-s', '--struct', help='Filter by struct', action='store')
	parser.add_argument('-l', '--subclass', action='store_true', help='Use subclasses')
	parser.add_argument('-r', '--report', help='Report mode: summary = just print an overall summary, detailed = print summary per data type', choices=['summary', 'detailed'], required=True)
	parser.add_argument('hypothesesCSV', help='Input file containing preditions made by the hypothesizer')
	args = parser.parse_args()

	if args.verbose:
		LOGGER.setLevel(logging.DEBUG)
	else:
		LOGGER.setLevel(logging.INFO)

	host = args.host
	database = args.database
	user = args.user
	password = args.password
	hypothesesCSV = args.hypothesesCSV

	if args.struct:
		types = args.struct.split(':')
		if len(types) == 1:
			datatypefilter="AND sc.data_type_id = (SELECT id FROM data_types WHERE name = '" + types[0] + "')"
		else:
			datatypefilter="AND sc.id = (SELECT id FROM subclasses WHERE name = '" + types[1] + "') AND sc.data_type_id = (SELECT id FROM data_types WHERE name = '" + types[0] + "')"
			args.subclass = True
	else:
		datatypefilter=""
	if args.subclass:
		type_name_column='(CASE WHEN sc.name IS NULL THEN dt.name ELSE CONCAT(dt.name, \':\', sc.name) END)'
		group_by_column='sc.id,'
	else:
		type_name_column='dt.name'
		group_by_column=''

	query = 'SELECT {type_name} AS type_name, mn.name AS member_name, sl.data_type_name \
		 FROM data_types AS dt \
		 JOIN subclasses AS sc \
		   ON sc.data_type_id = dt.id \
		 JOIN structs_layout AS sl \
		   ON dt.id = sl.data_type_id \
		 JOIN member_names AS mn \
		   ON sl.member_name_id = mn.id \
		 WHERE (sl.data_type_name like \'%atomic\_t%\' or sl.data_type_name like \'%atomic64\_t*\' or sl.data_type_name like \'%atomic\_long\_t%\') \
		      {datatype} \
		GROUP BY dt.id,{group_by}sl.byte_offset, mn.name, sl.data_type_name;'.format(datatype=datatypefilter,type_name=type_name_column,group_by=group_by_column)

	db = psycopg2.connect("dbname='" + database + "' user='" + user + "' host='" + host + "' password='" + password + "'")
	cursor = db.cursor()

	atomicMembers = dict()
	try:
		cursor.execute(query)
		results = cursor.fetchall()
		for row in results:
			if row[0] not in atomicMembers:
				entry = {'r': {'found': 0, 'notlocked': 0, 'locked': 0}, 'w': {'found': 0, 'notlocked': 0, 'locked': 0}, 'members': []}
				atomicMembers[row[0]] = entry
			else:
				entry = atomicMembers[row[0]]
			entry['members'].append(row[1])
	except Exception as e:
		print 'Error: ' + str(e)
		sys.exit(1)

	count = 0
	hypothesesDict = dict()
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
		else:
			LOGGER.error('Lock combination (%s) does already exist for key %s', line['locks'], key)
	LOGGER.debug('Read %d locking predictions for %d different (struct,member,accesstype) tuples from "%s"', count, len(hypothesesDict), hypothesesCSV)

	for datatype, entry in atomicMembers.iteritems():
		 for member in entry['members']:
			for atype in ('r', 'w'):
				key = (datatype, member, atype)
				if key in hypothesesDict:
					entry[atype]['found'] += 1
					hypothesesEntry = hypothesesDict[key]
					if hypothesesEntry['locks'][noLockString]['accepted'] == 1:
						entry[atype]['notlocked'] += 1
					else:
						entry[atype]['locked'] += 1

	totalCount = {'r': 0, 'w': 0}
	totalFound = {'r': 0, 'w': 0}
	totalLocked = {'r': 0, 'w': 0}
	totalNotLocked = {'r': 0, 'w': 0}

	LOGGER.debug(pformat(atomicMembers))

	print('type_name,count_r,found_r,locked_r,notlocked_r,count_w,found_w,locked_w,notlocked_w')
	for datatype, entry in atomicMembers.iteritems():
		totalCount['r'] += len(entry['members'])
		totalCount['w'] += len(entry['members'])
		totalFound['r'] += entry['r']['found']
		totalFound ['w']+= entry['w']['found']
		totalLocked['r'] += entry['r']['locked']
		totalLocked['w'] += entry['w']['locked']
		totalNotLocked['r'] += entry['r']['notlocked']
		totalNotLocked['w'] += entry['w']['notlocked']
		if args.report == 'detailed':
			print('%s,%d,%d,%d,%d,%d,%d,%d,%d' % (datatype, len(entry['members']), entry['r']['found'], entry['r']['locked'], entry['r']['notlocked'],
				len(entry['members']), entry['w']['found'], entry['w']['locked'], entry['w']['notlocked']))

	if args.report == 'summary':
		print('any,%d,%d,%d,%d,%d,%d,%d,%d' % (totalCount['r'], totalFound['r'], totalLocked['r'], totalNotLocked['r'],
			totalCount['w'], totalFound['w'], totalLocked['w'], totalNotLocked['w']))

	db.close()

if __name__ == '__main__':
	main()
