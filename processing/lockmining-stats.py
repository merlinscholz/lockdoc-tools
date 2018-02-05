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

logging.basicConfig()                               
LOGGER = logging.getLogger(__name__)  

def main():
        parser = argparse.ArgumentParser()          
        parser.add_argument('-v', '--verbose', action='store_true', help='Be verbose')
        parser.add_argument('-k', '--host',  help='Host whre the database runs on', required=True) # -h is already in use by help
        parser.add_argument('-d', '--database', help='The database to perform the query on', required=True)
        parser.add_argument('-u', '--user', help='The user used to login', required=True)
        parser.add_argument('-p', '--password', help='The user used to login', required=True)
	parser.add_argument('hypothesisCSVcsv', help='Input file containing preditions made by the hypothesizer')
        args = parser.parse_args()                  

        if args.verbose:                            
                LOGGER.setLevel(logging.DEBUG)      
        else:                                       
                LOGGER.setLevel(logging.INFO)

	host = args.host
	database = args.database
	user = args.user
	password = args.password
	hypothesisCSV = args.hypothesisCSVcsv

	db = MySQLdb.connect(host,user,password,database)
	cursor = db.cursor()

	dataTypes = dict()

	query = 'SELECT dt.name, COUNT(*) AS num \
		 FROM structs_layout AS st \
		 INNER JOIN data_types AS dt \
		    ON dt.id = st.type_id \
		 INNER JOIN member_names AS mn \
		    ON mn.id = st.member_id \
		 WHERE dt.name != \'task_struct\' \
		 GROUP BY st.type_id;'
	try:
		cursor.execute(query)
		results = cursor.fetchall()
		for row in results:
			if row[0] not in dataTypes:
				dataTypesEntry = {'members': 0, 'observed': list(), 'blacklisted': 0, 'rules': 0, 'acceptedrules': 0}
				dataTypes[row[0]] = dataTypesEntry
			dataTypesEntry['members'] += row[1]
	except Exception as e:
		print('Error: ' + str(e))

	query = 'SELECT dt.name, COUNT(*) AS num \
		 FROM member_blacklist AS mbl \
		 INNER JOIN data_types AS dt \
		    ON dt.id = mbl.datatype_id \
		 INNER JOIN member_names AS mn \
		    ON mn.id = mbl.datatype_member_id \
		 WHERE dt.name != \'task_struct\' \
		 GROUP BY mbl.datatype_id;'
	try:
		cursor.execute(query)
		results = cursor.fetchall()
		for row in results:
			dataTypes[row[0]]['blacklisted'] += row[1]
	except Exception as e:
		print('Error: ' + str(e))
	
	tempFile = open(hypothesisCSV,'rb')
	tempReader = csv.DictReader(tempFile, delimiter=';')
	for line in tempReader:
		dataTypesEntry = dataTypes[line['type']]
		if line['member'] not in dataTypesEntry['observed']:
			dataTypesEntry['observed'].append(line['member'])
		dataTypesEntry['rules'] += 1
		if line['accepted'] == '1':
			dataTypesEntry['acceptedrules'] += 1
	tempFile.close()

	print('datatype,members,blacklisted,observed,rules,acceptedrules')
	for (dataType, entry) in sorted(dataTypes.items()):
		print('%s,%d,%d,%d,%d,%d' %
			(dataType, entry['members'], entry['blacklisted'], len(entry['observed']), entry['rules'], entry['acceptedrules']))


if __name__ == '__main__':
	main()
