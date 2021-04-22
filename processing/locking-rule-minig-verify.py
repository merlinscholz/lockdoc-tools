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
	parser.add_argument('-r', '--rejected', action='store_true', help='Print tuples that have accesses but the prediction does not match the ground truth')
	parser.add_argument('-c', '--comparable', action='store_true', help='Print every tuple including YES/NO for a correct prediction to stderr')
	parser.add_argument('gtruthCSV', help='Input file containing the ground truth')
	parser.add_argument('hypoAllCSV', help='Input file containing preditions made by the hypothesizer (all hypotheses)')
	parser.add_argument('hypoWinnerCSV', help='Input file containing preditions made by the hypothesizer (winning hypotheses only)')
	args = parser.parse_args()

	groundtruthDict = dict()
	hypoAllDict = util.readHypothesesDict(args.hypoAllCSV)
	hypoWinnerDict = util.readHypothesesDict(args.hypoWinnerCSV)
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

	results = dict()
	for key, lockingRule in groundtruthDict.items():
		if key in hypoWinnerDict and key not in hypoAllDict:
			LOGGER.error("key '%s' is present in winner csv but not in the overall list" % (key))
			continue
		if key[0] not in results:
			results[key[0]] = { 'observedRulesR': 0.0, 'observedRulesW': 0.0, 'matchedRulesR': 0.0, 'matchedRulesW': 0.0}
		if key in hypoWinnerDict:
			locksHeldDict = hypoWinnerDict[key]['locks']
			if len(locksHeldDict) > 1 :
				LOGGER.error('More than one lock hypothesis for %s', key)
			if key[2] == 'w':
				results[key[0]]['observedRulesW'] += 1
			elif key[2] == 'r':
				results[key[0]]['observedRulesR'] += 1
			else:
				print("Unknown access type: %s" % (key[2]), file = sys.stderr)
				sys.exit(1)
			if len(locksHeldDict.keys()) > 1 :
				LOGGER.error("More than one hypotheses for %s" % (key))
				sys.exit(1)
			locksHeldList = []
			locksHeldList.append(list(locksHeldDict.keys())[0])
			for locks, locksStats in locksHeldDict.items():
				locksHeldList.append(locks)
				for conflictLocks in locksStats['conflictlist']:
					locksHeldList.append(conflictLocks)
			found = False
			for locksHeld in locksHeldList:
				if re.match('^' + lockingRule + '$',locksHeld):
					if key[2] == 'w':
						results[key[0]]['matchedRulesW'] += 1
					elif key[2] == 'r':
						results[key[0]]['matchedRulesR'] += 1
					found = True
					break;
			if found:
				if args.comparable:
					print("YES:{0}".format(key), file = sys.stderr)
			else:
				if args.comparable:
					print("NO :{0}".format(key), file = sys.stderr)
				if args.rejected:
					print("Rejected: {0}".format(key), file = sys.stderr)
		elif key in hypoAllDict:
			if args.comparable:
				print("NO :{0}".format(key), file = sys.stderr)
			if key[2] == 'w':
				results[key[0]]['observedRulesW'] += 1
			elif key[2] == 'r':
				results[key[0]]['observedRulesR'] += 1
			else:
				print("Unknown access type: %s" % (key[2]), file = sys.stderr)
				sys.exit(1)

	allObservedR = 0.0
	allObservedW = 0.0
	allMatchedR = 0.0
	allMatchedW = 0.0
	print("data_type;observedrules;observedrules_r;observedrules_w;matched;matched_r;matched_w;percentage;percentage_r;percentage_w")
	for key, values in results.items():
		matched = values['matchedRulesR'] + values['matchedRulesW']
		observed = values['observedRulesR'] + values['observedRulesW']
		print("%s;%d;%d;%d;%d;%d;%d;%3.2f;%3.2f;%3.2f" %
				(key,
				 observed,
				 values['observedRulesR'],
				 values['observedRulesW'],
				 matched,
				 values['matchedRulesR'],
				 values['matchedRulesW'],
				 util.calcPercentage(observed, matched),
				 util.calcPercentage(values['observedRulesR'], values['matchedRulesR']),
				 util.calcPercentage(values['observedRulesW'], values['matchedRulesW'])))
		allObservedR += values['observedRulesR']
		allObservedW += values['observedRulesW']
		allMatchedR += values['matchedRulesR']
		allMatchedW += values['matchedRulesW']
	allObservedRules = allObservedR + allObservedW
	print("total;%d;%d;%d;%d;%d;%d;%3.2f;%3.2f;%3.2f" %
			(allObservedRules,
			 allObservedR,
			 allObservedW,
			 allMatchedR + allMatchedW,
			 allMatchedR,
			 allMatchedW,
			 util.calcPercentage(allObservedRules, allMatchedR + allMatchedW),
			 util.calcPercentage(allObservedR, allMatchedR),
			 util.calcPercentage(allObservedW, allMatchedW)))
