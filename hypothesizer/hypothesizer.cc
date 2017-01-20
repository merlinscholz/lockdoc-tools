#include <iostream>
#include <sstream>
#include <fstream>
#include <vector>
#include <string>

#include "optionparser.h"

enum optionIndex { UNKNOWN, HELP };
const option::Descriptor usage[] = {
{
  UNKNOWN, 0, "", "", option::Arg::None,
  "Usage: hypothesizer [options] input.csv\n\nOptions:"
}, {
  HELP, 0, "h", "help", option::Arg::None, "--help \tPrint usage and exit"
}, {0,0,0,0,0,0}
};

int main(int argc, char **argv)
{
	// === Command-line parsing ===
	// skip program name argv[0] if present
	argc -= (argc > 0);
	argv += (argc > 0);

	option::Stats stats(usage, argc, argv);
	std::vector<option::Option> options(stats.options_max);
	std::vector<option::Option> buffer(stats.buffer_max);
	option::Parser parse(usage, argc, argv, &options[0], &buffer[0]);

	if (parse.error()) {
		return 1;
	}

	for (option::Option *opt = options[UNKNOWN]; opt; opt = opt->next()) {
		std::cerr << "Unknown option: " << std::string(opt->name, opt->namelen) << "\n";
	}

	if (options[HELP] || options[UNKNOWN] ||
		argc == 0 || parse.nonOptionsCount() != 1) {
		option::printUsage(std::cout, usage);
		return options[HELP] ? 0 : 1;
	}

	const char *filename = parse.nonOption(0);

	// === Load input CSV into memory ===
	// input format example:
	// r:i_mode,r:i_opflags,r:i_uid,r:i_flags,r:i_sb,r:i_rdev  EMB:5705(i_mutex),16(rcu)       1
	std::ifstream infile(filename);
	if (!infile.is_open()) {
		std::cerr << "Cannot open file: " << filename << std::endl;
		return 1;
	}

	std::stringstream ss;
	std::vector<std::string> lineElems;
	std::string inputLine, inputColumn, inputElement;
	for (unsigned lineCounter = 0; getline(infile, inputLine); lineCounter++) {

		// Skip the header if there is one.  This check exploits the fact that
		// any valid input line must end with a decimal digit.
		if (lineCounter == 0) {
			if (inputLine.length() == 0 || !isdigit(inputLine[inputLine.length() - 1])) {
				continue;
			} else {
				std::cerr << "Warning: Input data does not start with a CSV header." << std::endl;
			}
		}

		// Split along tab characters
		ss.clear();
		ss.str("");
		ss << inputLine;
		lineElems.clear();
		while (getline(ss, inputColumn, '\t')) {
			lineElems.push_back(inputColumn);
		}

		// Sanity check
		if (lineElems.size() != 3) {
			std::cerr << "Warning: Line " << lineCounter << " does not have the required number of columns." << std::endl;
			continue;
		}
	}
}
