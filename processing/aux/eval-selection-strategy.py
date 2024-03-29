#!/usr/bin/python3
from __future__ import print_function
import os, sys, os.path
import argparse
import logging
import tempfile
import subprocess
import numpy

logging.basicConfig()
LOGGER = logging.getLogger(__name__)
LOGGER.setLevel(logging.INFO)

strategies = {
	"sharpen": {
		'start': 10.0,
		'step': -0.5,
		'end': 0.0
	},
	"bottomup": {
		'start': 100.0,
		'step': -0.5,
		'end': 90.0
	},
	"topdown": {
		'start': 100.0,
		'step': -0.5,
		'end': 90.0
	},
	"lockset": {
		'start': 0.0,
		'step': 1.0,
		'end': 0.0
	}
}

def main():
	basedir = os.path.dirname(sys.argv[0])
	argv = sys.argv[1:]

	parser = argparse.ArgumentParser(prog='PROG')
	parser.add_argument('--groundtruth-csv', help='CSV containing the documented locking rules', required=True)
	parser.add_argument('--hypothesizer-input', help='Input for the hypothesizer', required=True)
	parser.add_argument('--all-csv', help='A csv file containing all hypotheses', required=True)
	parser.add_argument('--selection-strategy', help='Just evaluate a particular selections strategy')
	parser.add_argument('--keep', help='Keep temp files')
	parser.add_argument('--output', help='Redirect output to the given file')
	parser.add_argument('--verbose', help='Be more verbose', action='store_true')
	args = parser.parse_args(argv)

	groundtruthCSV = args.groundtruth_csv
	hypoInput = args.hypothesizer_input
	selStrategy = args.selection_strategy
	allCSV = args.all_csv
	if args.output:
		outFile = args.output
	else:
		outFile = sys.stdout
	if args.verbose:
		LOGGER.setLevel(logging.DEBUG)

	print("strategy;parameter;data_type;observedrules;observedrules_r;observedrules_w;matched;matched_r;matched_w;percentage;percentage_r;percentage_w", file = outFile)

	for key in strategies.keys():
		params = strategies[key]
		if selStrategy != None and selStrategy != key:
			continue
		for i in numpy.arange(params['start'], params['end'] + params['step'], params['step']):
			if args.keep:
				fname = "eval-%s-%.2f-hypothesizer.csv" % (key, i)
				tempWinnerCSV = open(os.path.join(args.keep, fname), 'w')
			else:
				tempWinnerCSV = tempfile.NamedTemporaryFile()
			cmd = basedir + '/../../hypothesizer/hypothesizer -g %s -f %.2f -a %.2f -s member -r csvwinner %s' % (key, i, i, hypoInput)
			LOGGER.debug("Running '%s'" % (cmd))
			hypothesizer = subprocess.Popen(cmd.split(),
							stdout = tempWinnerCSV,
							stderr = subprocess.PIPE)
			stdout, stderr = hypothesizer.communicate()
			if hypothesizer.returncode != 0:
				LOGGER.error("Error running: '%s'\n%s" % (cmd, stderr.decode()))
				continue
			cmd = basedir + '/../locking-rule-minig-verify.py %s %s %s' % (groundtruthCSV, allCSV, tempWinnerCSV.name)
			LOGGER.debug("Running '%s'" % (cmd))
			lock_verify = subprocess.Popen(cmd.split(),
							stdout = subprocess.PIPE,
							stderr = subprocess.PIPE)
			stdout, stderr = lock_verify.communicate()
			if lock_verify.returncode != 0:
				LOGGER.error("Error running: '%s'\n%s" % (cmd, stderr.decode()))
			lines = stdout.decode().splitlines()
			#if len(lines) != 2:
			#	LOGGER.error("locking-rule-minig-verify.py returned more than 2 lines: %s" % (lines))
			#	sys.exit(1)
			for line in lines[1:]:
				print("%s;%.2f;%s" % (key, i, line), file = outFile)
			outFile.flush()
			tempWinnerCSV.close()

if __name__ == "__main__":
	main()
