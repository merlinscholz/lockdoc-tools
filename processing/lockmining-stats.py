#!/usr/bin/python
# A. Lochmann 2018
# ToDo

from __future__ import print_function
import csv
import sys
import MySQLdb
import logging
import argparse 
import subprocess
import operator
from pprint import pprint

logging.basicConfig()                               
LOGGER = logging.getLogger(__name__)  

nolock_string = '(no locks held)';

def main():
	parser = argparse.ArgumentParser()          
	parser.add_argument('-v', '--verbose', action='store_true', help='Be verbose')
	parser.add_argument('-k', '--host',  help='Host whre the database runs on', required=True) # -h is already in use by help
	parser.add_argument('-d', '--database', help='The database to perform the query on', required=True)
	parser.add_argument('-u', '--user', help='The user used to login', required=True)
	parser.add_argument('-p', '--password', help='The user used to login', required=True)
	parser.add_argument('hypothesisCSV', help='Input file containing preditions made by the hypothesizer')
	args = parser.parse_args()                  

	if args.verbose:
		LOGGER.setLevel(logging.DEBUG)
	else:
		LOGGER.setLevel(logging.INFO)

	host = args.host
	database = args.database
	user = args.user
	password = args.password
	hypothesisCSV = args.hypothesisCSV

	db = MySQLdb.connect(host,user,password,database)
	cursor = db.cursor()

	dataTypes = dict()

	query = 'SELECT IF(sc.name IS NULL, dt.name, CONCAT(dt.name, ":", sc.name)), COUNT(*) AS num \
		 FROM subclasses AS sc \
		 INNER JOIN data_types AS dt \
		    ON sc.data_type_id = dt.id \
		 INNER JOIN structs_layout AS sl \
		    ON dt.id = sl.data_type_id \
		 INNER JOIN member_names AS mn \
		    ON sl.member_name_id = mn.id \
		 WHERE dt.name != \'task_struct\' \
		 GROUP BY sc.id;'
	try:
		cursor.execute(query)
		results = cursor.fetchall()
		for row in results:
			if row[0] not in dataTypes:
				dataTypesEntry = {'members': 0, 'observed': list(), 'blacklisted': 0, 'rules_r': 0, 'rules_w': 0, 'nolock_r': 0, 'nolock_w': 0}
				dataTypes[row[0]] = dataTypesEntry
			else:
				dataTypesEntry = dataTypes[row[0]]
			dataTypesEntry['members'] += row[1]
	except Exception as e:
		print('Error: ' + str(e))
		sys.exit(1)

	query = 'SELECT IF(sc.name IS NULL, dt.name, CONCAT(dt.name, ":", sc.name)), COUNT(*) AS num \
		 FROM member_blacklist AS mbl \
		 INNER JOIN subclasses AS sc \
		    ON sc.id = mbl.subclass_id \
		 INNER JOIN data_types AS dt \
		    ON dt.id = sc.data_type_id \
		 INNER JOIN member_names AS mn \
		    ON mn.id = mbl.member_name_id \
		 WHERE dt.name != \'task_struct\' \
		 GROUP BY mbl.subclass_id;'
	try:
		cursor.execute(query)
		results = cursor.fetchall()
		for row in results:
			dataTypes[row[0]]['blacklisted'] += row[1]
	except Exception as e:
		print('Error: ' + str(e))
		sys.exit(1)
	
	tempFile = open(hypothesisCSV,'rb')
	tempReader = csv.DictReader(tempFile, delimiter=';')
	for line in tempReader:
		if line['type'] not in dataTypes:
			LOGGER.error('\'%s\' does not exist in database', line['type'])
			continue
		dataTypesEntry = dataTypes[line['type']]

		if line['member'] not in dataTypesEntry['observed']:
			dataTypesEntry['observed'].append(line['member'])

		if line['accesstype'] == 'r':
			dataTypesEntry['rules_r'] += 1
		elif line['accesstype'] == 'w':
			dataTypesEntry['rules_w'] += 1

		if line['locks'] == nolock_string:
			if line['accesstype'] == 'r':
				dataTypesEntry['nolock_r'] += 1
			elif line['accesstype'] == 'w':
				dataTypesEntry['nolock_w'] += 1
	tempFile.close()

	print('datatype,members,blacklisted,observed,rules_r,rules_w,nolock_r,nolock_w')
	for (dataType, entry) in sorted(dataTypes.items()):
		print('%s,%d,%d,%d,%d,%d,%d,%d' %
			(dataType, entry['members'], entry['blacklisted'], len(entry['observed']), entry['rules_r'], entry['rules_w'], entry['nolock_r'], entry['nolock_w']))


if __name__ == '__main__':
	main()
