#!/usr/bin/python
# A.Lochmann 2018
# This script takes the ground-truth.csv and the output of the hypothesizer as input.
# The hypothesizer has to be run with cutoff threshold set to 0.0, and report mode to csv.
# It counts how many of the locking rules found in ground-truth.csv have
# a) 100% support, b) <100% support but are accepted, or c) <100% support and not accepted.
# A detailed descript of what is counted can be found in line 108 ff.

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
	parser.add_argument('-w', '--writes-only', action='store_true', help='Only count the predictions for write accesses')
	parser.add_argument('-m', '--machine-output', action='store_true', help='Produce a machine-readable output')
	parser.add_argument('gtruthcsv', help='Input file containing the ground truth')
	parser.add_argument('hypothesisCSVcsv', help='Input file containing preditions made by the hypothesizer')
	args = parser.parse_args()

	groundtruthCSV = args.gtruthcsv
	hypothesisCSV = args.hypothesisCSVcsv
	separator = ';'
	groundtruthDict = dict()
	count = 0
	hypothesisDict = dict()
	if args.verbose:
		LOGGER.setLevel(logging.DEBUG)
	else:
		LOGGER.setLevel(logging.INFO)

	tempFile = open(groundtruthCSV,'rb')
	tempReader = csv.DictReader(tempFile, delimiter=';')

	# groundtruthDict Layout 
	# key: (datatype, member, accesstype)
	# value: locking rule
	# example: (transaction_t,t_cpprev,w) -> 'EMBOTHER\(journal_t:j_list_lock\)'
	for line in tempReader:
		key = toKey(line['datatype'], line['member'] ,line['accesstype'])
		groundtruthDict[key] = line['rule']
	tempFile.close()
	LOGGER.debug('Read %d locking rules from "%s"', len(groundtruthDict), groundtruthCSV)


	# hypothesisDict layout 
	# key: (datatype, member, accesstype)
	# value: single-entry dictionary with key 'locks'
	#		layout of dictionary 'locks'
	#			key: locking rule
	#			value: dictionary with the following keys: occurrences [int], total [int], percentage [float], accepted [int], confidence [float], counterexamples [string]
	# example: (backing_dev_info, bdi_list, r) -> 'locks'
	#												'3(semaphore)' -> occurences: 9
	#																  total: 54
	#																  percentage: 16.667
	#																  accepted: 0
	#																  confidence: 0 (confidence will be set to 0.0 if TODO is found)
	#																  counterexamples: "counterexample.sql.sh backing_dev_info r:bdi_list CEX SEQ '3(semaphore)"
	tempFile = open(hypothesisCSV,'rb')
	tempReader = csv.DictReader(tempFile, delimiter=';')
	for line in tempReader:
		count = count + 1
		key = toKey(line['type'], line['member'], line['accesstype'])
		if key not in hypothesisDict:
			hypothesisEntry = {'locks': dict()}
			hypothesisDict[key] = hypothesisEntry
		else:
			hypothesisEntry = hypothesisDict[key]
		# ! ! ! Attention ! ! !
		# If this script will be extended beyond its current intention (verifying the groundtruth),
		# one need to filter lock combinations that are actually no real combinations, e.g., 'no hypothesis with locks exceeds cutoff threshold'.
		if line['locks'] not in hypothesisEntry['locks']:
			if line['confidence'] == "TODO":
				temp = 0
			else:
				temp = float(line['confidence'])
			locksHeldEntry = {'occurrences': int(line['occurrences']), 'total': int(line['total']), 'percentage': float(line['percentage']), 'accepted': int(line['accepted']), 'confidence': temp, 'counterexample-parameters': line['counterexample-parameters']}
			hypothesisEntry['locks'][line['locks']] = locksHeldEntry
			LOGGER.debug('Added lock combination (%s) for key %s', line['locks'], key)
		else:
			LOGGER.error('Lock combination (%s) does already exist for key %s', line['locks'], key)
	LOGGER.debug('Read %d locking predictions for %d different (struct,member,accesstype) tuples from "%s"', count, len(hypothesisDict), hypothesisCSV)

	resultsDict = dict()

	# resultsDict layout
	# key: datatype
	# value: dictionary with the following keys: 
	#		count [int] (Total amount of found locking rules)
	#		full [int] (Amount of locking rules with a support of 100%)
	#		winner [int] (Amount of locking rules with a support less than 100% but accepted by the hypothesizer)
	#		found [int] (Amount of locking rules that are found in our dataset but have a support less than 100% and are not accepted)
	#		notfound [int] (Amount of locking rules that are not found in our dataset at all; notfound + count is the total amount of locking rules for a particular datatype in ground-truth.csv)
	#		members [dict]
	# 			layout of dictionary members
	#				key: (member,accesstype)
	#				value: dictionary with the following keys: 
	#					color [string] (For later use; signaling the status of this documented locking rule)
	#					lockringrule [string] (The actual locking rule)
	#					results [dict]
	#						layout of dictionray results
	#							occurrences [int], total [int], percentage [float], accepted [int], confidence [float], counterexamples [string]
	# example
	# 'journal_t' -> 'count': 19,
	#				 'found': 4,
	#				 'full': 15,
	#				 'member':
	#						('j_barrier_count', 'w') -> 'color': 'green'
	#													'lockingrule': 'EMBSAME\\(journal_t:j_state_lock\\)',
	#													'results': 'accepted': 0,
	#															   'confidence': 0,
	#															   'counterexample-parameters': '(no counterexamples to be expected, this hypothesis has 100% support in the observation set)',
	#															   'occurrences': 2,
	#															   'percentage': 100.0,
	#															   'total': 2}},
	LOGGER.debug('Calculating results...')
	for key, lockingRule in groundtruthDict.iteritems():
		# key = (datatype, member, accesstype)
		if args.writes_only and key[2] == 'r':
			continue
		datatype = key[0]
		if datatype not in resultsDict:
			resultsEntry = {'count': 0, 'full': 0, 'winner': 0, 'found': 0, 'notfound': 0, 'members': dict()}
			resultsDict[datatype] = resultsEntry
		else:
			resultsEntry = resultsDict[datatype]
		if key in hypothesisDict:
			locksHeldDict = hypothesisDict[key]['locks']
			matchFound = False
			for locksHeld, locksHeldEntry in locksHeldDict.iteritems():
				LOGGER.debug('SEARCHRULE: Checking %s and %s',lockingRule,locksHeld)
				if re.match('^' + lockingRule + '$',locksHeld):
					matchFound = True
					LOGGER.debug('SEARCHRULE: %s matches %s', lockingRule, locksHeld)
					membersDict = resultsEntry['members']
					membersDictKey = (key[1], key[2])
					resultsEntry['count'] += 1
					if locksHeldEntry['percentage'] == 100:
						resultsEntry['full'] += 1
						_color = 'green'
					elif locksHeldEntry['percentage'] < 100 and locksHeldEntry['accepted'] == 1:
						resultsEntry['winner'] += 1
						_color = 'yellow'
					else:
						resultsEntry['found'] += 1
						_color = 'red'
					if membersDictKey not in membersDict:
						membersDict[membersDictKey] = {'results': locksHeldEntry, 'lockingrule': lockingRule,'color': _color}
					else:
						LOGGER.error('Found another matching rule (%s) for %s in datatype ', lockingRule, membersKey, key[0])
					LOGGER.debug('Added new entry for key %s and locking rule %s: %s\n %s', key, lockingRule, resultsEntry, locksHeldEntry)
					break
				else:
					LOGGER.debug('SEARCHRULE: No match %s and %s',lockingRule,locksHeld)
			if not matchFound:
				resultsEntry['notfound'] += 1
				LOGGER.debug('SEARCHRULE: Lock combination %s not found for %s', lockingRule, key)
		else:
			LOGGER.debug('Key %s not found in hypothesisDict', key)

	if args.machine_output:
			print('Machine-readable output is still broken!')
			sys.exit(1)
	for datatype, resultsEntry in resultsDict.iteritems():
		
		print('%s:' % datatype)
		print('\tcount: %3d,\tfull: %3d (%3.2f%%)\twinner: %3d (%3.2f%%)' % (resultsEntry['count'], resultsEntry['full'], calcPercentage(resultsEntry['count'], resultsEntry['full']), resultsEntry['winner'], calcPercentage(resultsEntry['count'], resultsEntry['winner'])))
		print('\tfound: %3d (%3.2f%%)\tnotfound: %3d' % (resultsEntry['found'], calcPercentage(resultsEntry['count'], resultsEntry['found']), resultsEntry['notfound']))

	#pprint(resultsDict)




