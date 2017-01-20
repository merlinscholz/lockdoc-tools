#include <iostream>
#include <vector>
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
}
