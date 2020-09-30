#!/usr/bin/python3
# A.Lochmann 2018
# This script takes the ground-truth.csv and the output of the hypothesizer as input.
# The hypothesizer has to be run with cut-off threshold set to 0.0, and report mode to csv.
# It counts how many of the locking rules found in ground-truth.csv have
# a) 100% support, b) <100% support but are accepted, or c) <100% support and not accepted.
# A detailed descript of what is counted can be found in line 108 ff.
# Question answered: 'What is the support of the documented locking rules?'
# ./processing/locking-rule-doc-verify.py ground-truth.csv all_txns_members_locks_hypo_nostack.csv
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

def toKey(dataType, member, accessType):
	return (dataType, member, accessType)

if __name__ == '__main__':

	parser = argparse.ArgumentParser()
	parser.add_argument('-v', '--verbose', action='store_true', help='Be verbose')
	parser.add_argument('-w', '--writes-only', action='store_true', help='Only count the predictions for write accesses')
	parser.add_argument('-m', '--machine-output', choices=['summary','detailed'], help='Produce a machine-readable output. Its value should either be ')
	parser.add_argument('-s', '--struct', help='Filter by struct', action='store', nargs='*')
	parser.add_argument('-p', '--percentage', action='store_true', help='Print relative values instead of absolute ones (Only effective in conjunction with --machine-output summary)')
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

	tempFile = open(groundtruthCSV, 'rt', encoding = 'ascii')
	tempReader = csv.DictReader(tempFile, delimiter=';')

	# groundtruthDict Layout 
	# key: (datatype, member, accesstype)
	# value: locking rule
	# example: (transaction_t,t_cpprev,w) -> 'EMBOTHER\(journal_t:j_list_lock\)'
	for line in tempReader:
		if args.struct is not None and line['datatype'] not in args.struct:
			continue
		key = toKey(line['datatype'], line['member'] ,line['accesstype'])
		if key in groundtruthDict:
			LOGGER.error('Key %s does already exist in groundtruthDict', key)
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
	hypothesisDict = util.readHypothesesDict(hypothesisCSV)
	resultsDict = dict()

	# resultsDict layout
	# key: datatype
	# value: dictionary with the following keys: 
	#		count [int] (Total amount of found locking rules)
	#		full [int] (Amount of locking rules with a support of 100%)
	#		winner [int] (Amount of locking rules with a support less than 100% but accepted by the hypothesizer)
	#		found [int] (Amount of locking rules that are found in our dataset but have a support less than 100% and are not accepted)
	#		notfound [int] (Amount of locking rules that are not found in our dataset at all; notfound + count is the total amount of locking rules for a particular datatype in ground-truth.csv)
	#		noobservations [int] (Amount of locking rules we don't have at least one observation for)
	#		members [dict]
	# 			layout of dictionary members
	#				key: (member,accesstype)
	#				value: dictionary with the following keys: 
	#					color [string] (For later use; signaling the status of this documented locking rule)
	#					lockingrule [string] (The actual locking rule)
	#					results [dict]
	#						layout of dictionray results
	#							occurrences [int], total [int], percentage [float], accepted [int], confidence [float], counterexamples [string]
	# example
	# 'journal_t' -> 'count': 19,
	#				 'found': 4,
	#				 'full': 15,
	#				 'noobservations': 2
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
	for key, lockingRule in groundtruthDict.items():
		# key = (datatype, member, accesstype)
		if args.writes_only and key[2] == 'r':
			continue
		datatype = key[0]
		if datatype not in resultsDict:
			resultsEntry = {'count': 0, 'full': 0, 'winner': 0, 'found': 0, 'notfound': 0, 'noobservations': 0, 'members': dict()}
			resultsDict[datatype] = resultsEntry
		else:
			resultsEntry = resultsDict[datatype]
		# Do we have at least one hypothesis for the current tuple (datatype,member,accesstype)?
		if key in hypothesisDict:
			locksHeldDict = hypothesisDict[key]['locks']
			resultsEntry['count'] += 1
			tempLocksHeldEntry = None
			# Iterate over the complete dictionary of possible lock combinations.
			# '$key in $dict' syntax does *not* do the job, because locksHeld is a regex.
			# We therefore need to use re.match().
			LOGGER.debug('%s: %d hypotheses', key, len(locksHeldDict))
			for locksHeld, locksHeldEntry in locksHeldDict.items():
				if re.match('^' + lockingRule + '$',locksHeld):
					# If a member is protected by a rw lock, the documented locking rules do not specify 
					# which part of the lock (read vs. write) should be used.
					# Our locking rule regex therefore matches both.
					# However, it might be possible that both variants are oberserved for a certain (datatype,member,accesstype)
					# tuple. We choose the one having the higher relative support.
					if tempLocksHeldEntry is None:
						tempLocksHeldEntry = locksHeldEntry
					elif locksHeldEntry['percentage'] > tempLocksHeldEntry['percentage']:
						tempLocksHeldEntry = locksHeldEntry
				else:
					LOGGER.debug('%s:no match:\'%s\',\'%s\'',key,lockingRule,locksHeld)
			if tempLocksHeldEntry is None:
				resultsEntry['notfound'] += 1
				_color = 'red'
				LOGGER.debug('SEARCHRULE:%s,%d', key, 0)
			else:
				if tempLocksHeldEntry['percentage'] == 100:
					resultsEntry['full'] += 1
					_color = 'green'
				else:
					resultsEntry['found'] += 1
					_color = 'yellow'
				LOGGER.debug('SEARCHRULE:%s,%d', key, tempLocksHeldEntry['percentage'])
			membersDict = resultsEntry['members']
			membersDictKey = (key[1], key[2])
			if membersDictKey not in membersDict:
				membersDict[membersDictKey] = {'results': tempLocksHeldEntry, 'lockingrule': lockingRule,'color': _color}
			else:
				LOGGER.error('Found another matching rule (%s) for %s in datatype ', lockingRule, membersKey, key[0])
		else:
			resultsEntry['noobservations'] += 1
			LOGGER.debug('Key %s not found in hypothesisDict', key)

	if args.machine_output == 'summary':
		print('datatype,numrules,noobservations,observations,full,found,notfound')
	elif args.machine_output == 'summary':
		print('datatype,member,accesstype,lockingrule,percentage,color')
	for datatype, resultsEntry in resultsDict.items():
		if args.machine_output == 'summary':
			if args.percentage:
				print('%s,%d,%d,%d,%3.2f,%3.2f,%3.2f' %
					(datatype, resultsEntry['count']  + resultsEntry['noobservations'],
					 resultsEntry['noobservations'], resultsEntry['count'],
					 util.calcPercentage(resultsEntry['count'], resultsEntry['full']), util.calcPercentage(resultsEntry['count'], resultsEntry['found']),
					 util.calcPercentage(resultsEntry['count'], resultsEntry['notfound'])))
			else:
				print('%s,%d,%d,%d,%d,%d,%d' %
					(datatype, resultsEntry['count']  + resultsEntry['noobservations'],
					 resultsEntry['noobservations'], resultsEntry['count'],
					 resultsEntry['full'], resultsEntry['found'],
					 resultsEntry['notfound']))
		elif args.machine_output == 'detailed':
			membersDict = resultsEntry['members']
			for key, memberEntry in membersDict.iteritems():
			# key = (member,accesstype)
				print('%s,%s,%s,%s,' %
					(datatype, key[0], key[1],
					 memberEntry['lockingrule']), end='' )
				if memberEntry['results'] is None:
					print('0.0', end='')
				else:
					print('%3.2f' % (memberEntry['results']['percentage']), end='')
				print(',%s' % memberEntry['color'])
		else:
			print('%s:' % datatype)
			total = resultsEntry['count']
			print('\tobservations: %3d (%3.2f %%),\tfull: %3d (%3.2f%%)\tfound: %3d (%3.2f%%)\tnotfound: %3d (%3.2f%%)'
				% (total, util.calcPercentage(total + resultsEntry['noobservations'], total),
				 resultsEntry['full'], util.calcPercentage(total, resultsEntry['full']),
				 resultsEntry['found'], util.calcPercentage(total, resultsEntry['found']),
				 resultsEntry['notfound'], util.calcPercentage(total, resultsEntry['notfound'])))
			print('\tnoobservations: %3d\t#documented rules: %d'
				% (resultsEntry['noobservations'], total + resultsEntry['noobservations']))

