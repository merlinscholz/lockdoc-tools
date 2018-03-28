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
	parser.add_argument('cexcsv', help='Input file containing the ground truth')
	args = parser.parse_args()

	cexcsv = args.cexcsv
	separator = ';'
	count = 0
	cexDict = dict()
	if args.verbose:
		LOGGER.setLevel(logging.DEBUG)
	else:
		LOGGER.setLevel(logging.INFO)

	tempFile = open(cexcsv,'rb')
	tempReader = csv.DictReader(tempFile, delimiter=';')

	for line in tempReader:
            count += 1
            if line['data_type'] in cexDict:
                cexEntry = cexDict[line['data_type']]
            else:
                cexEntry = {'members': dict(), 'count': 0, 'locations': dict()}
                cexDict[line['data_type']] = cexEntry
            cexEntry['count'] += 1
            if line['member'] not in cexEntry['members']:
                cexEntry['members'][line['member']] = 1
            locKey = (line['instrptr'], line['stacktrace'])
            if locKey not in cexEntry['locations']:
                cexEntry['locations'][locKey] = 1

	tempFile.close()
	LOGGER.debug('Read %d locking rules from "%s"', count, cexcsv)

        print('data_type,cex,members,locations')
        for key, cexEntry in cexDict.iteritems():
            print('%s,%d,%d,%d' %
                (key, cexEntry['count'], len(cexEntry['members']), len(cexEntry['locations'])))

