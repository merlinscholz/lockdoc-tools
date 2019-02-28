#!/usr/bin/python
# A.Lochmann 2018

from __future__ import print_function
import sys
import csv
import re
import argparse
from pprint import pprint
import logging

logging.basicConfig()
LOGGER = logging.getLogger(__name__)

def toKey(dataType, member, accessType):
	return (dataType, member, accessType)

def calcPercentage(basis, perquot):
	if basis == 0:
		return 0.0
	else:
		return (float(perquot) * 100.0) / float(basis)

if __name__ == '__main__':

	parser = argparse.ArgumentParser()
	parser.add_argument('-v', '--verbose', action='store_true', help='Be verbose')
	parser.add_argument('hypocsv', help='Input file containing all winning hypotheses')
	parser.add_argument('cexcsv', nargs=argparse.REMAINDER, help='Input file containing the ground truth')
	args = parser.parse_args()

	separator = ';'
	totalCount = 0
	cexDict = dict()
	if args.verbose:
		LOGGER.setLevel(logging.DEBUG)
	else:
		LOGGER.setLevel(logging.INFO)


	tempFile = open(args.hypocsv,'rb')
	tempReader = csv.DictReader(tempFile, delimiter=';')
	count = 0

	for line in tempReader:
		count += 1
		if line['type'] in cexDict:
			cexEntry = cexDict[line['type']]
		else:
			cexEntry = {'members': dict(), 'count': 0, 'locations': dict()}
			cexDict[line['type']] = cexEntry

	for cexFile in args.cexcsv:
		tempFile = open(cexFile,'rb')
		tempReader = csv.DictReader(tempFile, delimiter=';')
		count = 0

		for line in tempReader:
			count += 1
			cexEntry = cexDict[line['data_type']]

			lockCombinations = line['locks_held'].split('+')
			for lockComb in lockCombinations:
				locksHeld = lockComb.split('#')[0]
				occurences = lockComb.split('#')[1]
				cexEntry['count'] += int(occurences)
			if line['member'] not in cexEntry['members']:
				cexEntry['members'][line['member']] = 1
			locKey = (line['stacktrace'])
			if locKey not in cexEntry['locations']:
				cexEntry['locations'][locKey] = 1

		totalCount = totalCount + count
		tempFile.close()
		LOGGER.debug('Read %03d locking rules from "%s"', count, cexFile)
	LOGGER.debug('Read %03d locking rules in total', totalCount)

	print('data_type,cex,members,locations')
	for key in sorted(cexDict.iterkeys()):
		cexEntry = cexDict[key]
		print('%s,%d,%d,%d' %
			(key, cexEntry['count'], len(cexEntry['members']), len(cexEntry['locations'])))

