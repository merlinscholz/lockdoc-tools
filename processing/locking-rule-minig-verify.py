#!/usr/bin/python3
# A.Lochmann 2019
# This scripts verifies the mined results against the existing documentation.
# Question answered: 'How many winning hypotheses, predicted by LockDoc, match the documentation?'
# ./locking-rule-mining-verify.py <groundtruth csv> <winner hypotheses csv>

from __future__ import print_function
import sys
import csv
import re
import argparse
from pprint import pprint
import logging
import util

logging.basicConfig()
LOGGER = logging.getLogger(__name__)

if __name__ == '__main__':

	parser = argparse.ArgumentParser()
	parser.add_argument('-v', '--verbose', action='store_true', help='Be verbose')
	parser.add_argument('gtruthCSV', help='Input file containing the ground truth')
	parser.add_argument('hypothesisCSV', help='Input file containing preditions made by the hypothesizer (winning hypotheses only)')
	args = parser.parse_args()

	groundtruthDict = dict()
	hypothesisDict = util.readHypothesesDict(args.hypothesisCSV)
	if args.verbose:
		LOGGER.setLevel(logging.DEBUG)
	else:
		LOGGER.setLevel(logging.INFO)

	tempFile = open(args.gtruthCSV, 'rt', encoding = 'ascii')
	tempReader = csv.DictReader(tempFile, delimiter=';')
	for line in tempReader:
		key = (line['datatype'], line['member'] ,line['accesstype'])
		if key in groundtruthDict:
			LOGGER.error('Key %s does already exist in groundtruthDict', key)
		groundtruthDict[key] = line['rule']
	tempFile.close()
	LOGGER.debug('Read %d locking rules from "%s"', len(groundtruthDict), args.gtruthCSV)

	totalRules = 0
	matchedRules = 0
	for key, lockingRule in groundtruthDict.items():
		if key in hypothesisDict:
			locksHeldDict = hypothesisDict[key]['locks']
			if len(locksHeldDict) > 1 :
				LOGGER.error('More than one lock hypothesis for %s', key)
			totalRules = totalRules + 1
			if len(locksHeldDict.keys()) > 1 :
				LOGGER.error("More than one hypotheses for %s" % (key))
			locksHeld = list(locksHeldDict.keys())[0]
			if re.match('^' + lockingRule + '$',locksHeld):
				matchedRules = matchedRules + 1
	print("totalrules;matched;percentage")
	print("%d;%d;%3.2f" % (totalRules, matchedRules, util.calcPercentage(totalRules, matchedRules)))
