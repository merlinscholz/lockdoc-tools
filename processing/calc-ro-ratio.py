#!/usr/bin/python
# A.Lochmann 2017
# This script takes the groundtruth.csv and the output of the hypothesizer as input.
# It counts the true and false predictions for ro members only as well as how many members were not found.
# Last but not least, it calculates the fraction of correct ro predictions.

import sys
import csv
import re
import argparse
from pprint import pprint
import logging

logging.basicConfig()
LOGGER = logging.getLogger(__name__)

RO_MEMBER_STRING = ".*ro\-member.*"

def toKey(dataType, member, accessType):
	#return dataType + "," + member + "," + accessType
	return (dataType, member, accessType)

if __name__ == '__main__':

	parser = argparse.ArgumentParser()
	parser.add_argument('-v', '--verbose', action='store_true', help='Be verbose')
	parser.add_argument('-m', '--machine-output', action='store_true', help='Produce a machine-readable output')
	parser.add_argument('gtruthcsv', help='Input file containing the ground truth')
	parser.add_argument('resultscsv', help='Input file containing preditions made by the hypothesizer')
	args = parser.parse_args()

	groundtruthCSV = args.gtruthcsv
	resultsCSV = args.resultscsv
	separator = ';'
	groundtruthDict = dict()
	count = 0
	results = dict()
	if args.verbose:
		LOGGER.setLevel(logging.DEBUG)
	else:
		LOGGER.setLevel(logging.INFO)

	tempFile = open(groundtruthCSV,'rb')
	tempReader = csv.DictReader(tempFile, delimiter=';')

	for line in tempReader:
		key = toKey(line['datatype'], line['member'] ,line['accesstype'])
		temp = dict()
		temp['comment'] = line['comment']
		temp['origin'] = line['origin']
		temp['rule'] = line['rule']
		groundtruthDict[key] = temp
	tempFile.close()
	LOGGER.debug('Read %d locking rules from "%s"', len(groundtruthDict), groundtruthCSV)

	tempFile = open(resultsCSV,'rb')
	tempReader = csv.DictReader(tempFile, delimiter=';')
	for line in tempReader:
		if line['accesstype'] == 'w':
			continue
		count = count + 1
		key = toKey(line['type'], line['member'], line['accesstype'])
		if line['type'] not in results:
			dataTypeResults = {'count': 0, 'positive': 0, 'negative': 0, 'notfound': 0}
			results[line['type']] = dataTypeResults
		else:
			dataTypeResults = results[line['type']]
		if key in groundtruthDict:
			locks = groundtruthDict[key]
			if re.match(RO_MEMBER_STRING, locks['comment']):
				dataTypeResults['count'] = dataTypeResults['count'] + 1
				if line['locks'] == 'nolock' and (line['type'], line['member'], 'w') not in results:
					LOGGER.debug('Match for key "%s" found: Is a ro member (%s).', key, locks['comment'])
					dataTypeResults['positive'] = dataTypeResults['positive'] + 1
				else:
					LOGGER.debug('No match for key "%s" found: Is NOT a ro member.', key)
					dataTypeResults['negative'] = dataTypeResults['negative'] + 1
			else:
				LOGGER.debug('Key "%s" should not be a ro member.', key)
		else:
			LOGGER.debug('Key "%s" not found', key)
			dataTypeResults['notfound'] = dataTypeResults['notfound'] + 1
	LOGGER.debug('Read %d locking predictions from "%s"', count, resultsCSV)

	if args.machine_output:
		print('datatype' + separator + 'count' + separator + 'positive' + separator + 'negative' + separator + 'notfound' + separator + 'percentage')
	for datatype in results:
		result = results[datatype]
		if result['count'] != 0:
			percentage = float(result['positive']) / float(result['count']) * 100
		else:
			percentage = 0
		if args.machine_output:
			print('%s%c%d%c%d%c%d%c%d%c%3.2f' % (datatype, separator, result['count'], separator, result['positive'], separator, result['negative'], separator, result['notfound'], separator, percentage))
		else:
			print('%s:' % datatype)
			print('\t count: %3d,\tnotfound: %3d' % (result['count'], result['notfound']))
			print('\tpositive: %3d\tnegative: %3d' % (result['positive'], result['negative']))
			print('\tpercentage: %3.2f' % percentage)
	tempFile.close()



