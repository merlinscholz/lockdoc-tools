import logging
import requests
import os.path
import os
import csv

logging.basicConfig()
LOGGER = logging.getLogger(__name__)
LOGGER.setLevel(logging.INFO)

def setVerbose():
	LOGGER.setLevel(logging.DEBUG)

def _retrieveContent(targetURL):
	try:
		headers = {
			'Accept': 'text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8',
			'User-Agent': 'Mozilla/5.0 (X11; Ubuntu; Linux x86_64; rv:63.0) Gecko/20100101 Firefox/63.0',
			'Accept-Language': 'en-US,en;q=0.5',
			'Accept-Encoding': 'gzip, deflate',
			'DNT': '1',
			'Connection': 'keep-alive'
		}
		return requests.get(targetURL, headers=headers, stream=True)
	except (Exception) as e:
		LOGGER.error('Cannot download \'%s\': %s' % (targetURL, e))
		return None

def downloadExtContent(extContent, cacheDir=None):
	for item in extContent:
		targetURL = item['url'].format(rev=item['rev']) + '/' + item['fname']
		if cacheDir is None:
			resp = _retrieveContent(targetURL)
			LOGGER.debug("Downloaded '%s'" % (item['fname']))
			data = resp.content
		else:
			outFile = os.path.join(cacheDir, item['fname'])
			outDir = os.path.dirname(outFile)
			if not os.path.isdir(outDir):
				os.makedirs(outDir)
			if os.path.isfile(outFile):
				LOGGER.debug("Reading cached content of '%s'" % (item['fname']))
				with open(outFile, 'r') as fd:
					data = fd.read()
			else:
				resp = _retrieveContent(targetURL)
				LOGGER.debug("Downloaded '%s'" % (item['fname']))
				LOGGER.debug("Caching file %s for later use" % (item['fname']))
				data = resp.content
				with open (outFile, 'wb') as fd:
					for chunk in resp.iter_content(chunk_size=128):
						fd.write(chunk)
		item['data'] = data

def printExtContent(extContent, contentType):
	for content in extContent:
		if content['type'] != contentType or content['data'] is None:
			continue
		print("""/* %s (rev %s) --- BEGIN */
%s
/* %s (rev %s) --- END */""" % (content['fname'], content['rev'], content['data'], content['fname'], content['rev']))

def readHypothesesDict(hypothesesCSV):
	count = 0
	hypothesesDict = dict()
	# Parse the hypothesizer results, and store them in one large dictionary
	tempFile = open(hypothesesCSV, 'rt', encoding = 'ascii')
	tempReader = csv.DictReader(tempFile, delimiter=';')
	for line in tempReader:
		count = count + 1
		key = (line['type'], line['member'], line['accesstype'])
		if key not in hypothesesDict:
			hypothesesEntry = {'locks': dict()}
			hypothesesDict[key] = hypothesesEntry
		else:
			hypothesesEntry = hypothesesDict[key]
		# ! ! ! Attention ! ! !
		# If this script will be extended beyond its current intention (verifying the groundtruth),
		# one need to filter lock combinations that are actually no real combinations, e.g., 'no hypothesis with locks exceeds cutoff threshold'.
		if line['locks'] not in hypothesesEntry['locks']:
			if line['confidence'] == "TODO":
				temp = 0
			else:
				temp = float(line['confidence'])
			locksHeldEntry = {'occurrences': int(line['occurrences']), 'total': int(line['total']), 'percentage': float(line['percentage']), 'accepted': int(line['accepted']), 'confidence': temp, 'counterexample-parameters': line['counterexample-parameters']}
			hypothesesEntry['locks'][line['locks']] = locksHeldEntry
		else:
			LOGGER.error('Lock combination (%s) does already exist for key %s', line['locks'], key)
	LOGGER.debug('Read %d locking predictions for %d different (struct,member,accesstype) tuples from "%s"', count, len(hypothesesDict), hypothesesCSV)
	tempFile.close()

	return hypothesesDict

def genIndentation(lvl):
	ret = ""
	for i in range(0, lvl):
		ret = ret + "	"
	return ret

def printLicenseExtConent(extContent):
	for content in extContent:
		print('Using %s, version %s, under %s license' % (content['fname'], content['rev'], content['license']))


def calcPercentage(basis, perquot):
	if basis == 0:
		return 0.0
	else:
		return (float(perquot) * 100.0) / float(basis)
