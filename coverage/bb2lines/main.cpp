/* Dump a gcov file, for debugging use.
   Copyright (C) 2002-2018 Free Software Foundation, Inc.
   Contributed by Nathan Sidwell <nathan@codesourcery.com>

Gcov is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3, or (at your option)
any later version.

Gcov is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Gcov; see the file COPYING3.  If not see
<http://www.gnu.org/licenses/>.  */

#include "gcov-io.h"
#include <getopt.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <iostream>
#include <vector>
#include <set>
#include <memory>
#include <unordered_map>
#include <bits/unordered_map.h>
#include <fstream>
#include <experimental/filesystem>
#include <system_error>
#include <regex>
#include "binaryread.h"

struct source_line
{
	std::string filename;
	std::vector<unsigned> lines;
};

struct basic_block
{
	unsigned blockno;
	std::vector<source_line> source_lines;

	const unsigned get_start_line() const
	{
		return source_lines.front().lines.front();
	}

	const std::string& get_filename() const
	{
		return source_lines.front().filename;
	}

	bool operator<(const basic_block& rhs) const
	{
		return get_start_line() < rhs.get_start_line();
	}
	bool operator>(const basic_block& rhs) const
	{
		return get_start_line() > rhs.get_start_line();
	}
	bool operator==(const basic_block& rhs) const
	{
		return get_start_line() == rhs.get_start_line();
	}
};


struct function_info
{
	std::string fn_name;
	std::string source;
	std::set<basic_block> basic_blocks;
};

void determine_coverage();
static void read_graph_file (const char *);
static void print_usage();
std::pair<unsigned int, unsigned long> count_all_files_and_lines();
unsigned long count_all_lines_in_file(std::string& filename);
std::vector<std::string> get_all_files_in_dir(const std::string &dirPath);
basic_block* get_basic_block(unsigned long);
void print_basic_block(const basic_block*);
extern int main (int, char **);

//key: addr of basic block; value: basic block
std::unordered_map<unsigned long, basic_block> basic_blocks_addr_map;
// key: name of source file; value: set of covered lines in a source file
std::unordered_map<std::string, std::set<unsigned>> covered_lines;
std::unordered_map<std::string, std::set<unsigned>> coverable_lines;
// key: name of source file; value: map of functions in file with the function name as key
std::unordered_map<std::string, std::unordered_map<std::string, function_info*>> functions_map;

std::unordered_map<std::string, std::set<basic_block>> basic_blocks_map;

// file prefix for the source file names of basic blocks and functions from the .gcno file
// addr2line has the absolute path with filename by default
char *file_prefix;
char *binary;

enum verbosity_level
{
	FATAL_FAILURE, // the program exits after this kind of failure
	RARE_FAILURE, // the program doesn't exit, but it could be a noticeable failure
	COMMON_FAILURE, // the program doesn't exit and the failure is usually not problematic
	ADDITIONAL_INFORMATION // more information is printed how the processing is done
};

int verbose = FATAL_FAILURE;

// let print all covered lines and a summary of the total coverage
int statistic_information;

// failure summary
unsigned addr_not_start_line_count = 0;
unsigned bb_is_already_in_bb_map_count = 0;
unsigned bb_at_addr_not_found_count = 0;
unsigned file_is_null_in_addr2line_count = 0;
unsigned file_found_line_not_found_count = 0;
unsigned fs_not_in_bb_map_count = 0;

unsigned input_addr_count = 0;
unsigned unique_input_addr_count = 0;
unsigned bb_read_count = 0;

int printf_verbose(int verbosity, const char * format, ...)
{
	if(verbose < verbosity)
		return 0;

	va_list args;
	va_start(args, format);
	int ret = vfprintf(stderr, format, args);
	va_end(args);

	return ret;
}

static const struct option options[] =
		{
				{ "help",                  no_argument,       nullptr, 'h' },
				{ "verbose",               required_argument, nullptr, 'v' },
				{ "statistic-information", no_argument,       nullptr, 's' },
				{ "file-prefix",           required_argument, nullptr, 'p' },
				{ "binary",                required_argument, nullptr, 'b' },
				{ 0, 0, 0, 0 }
		};


int main (int argc, char **argv)
{
	int opt;

	while ((opt = getopt_long (argc, argv, "hvspb", options, nullptr)) != -1)
	{
		switch (opt)
		{
			case 'v':
				verbose = std::stoi(argv[optind++]);
				break;
			case 'h':
				print_usage();
				return 0;
			case 's':
				statistic_information = 1;
				break;
			case 'p':
				file_prefix = argv[optind++];
				break;
			case 'b':
				binary = argv[optind++];
				break;
			default:
				fprintf(stderr, "unknown flag `%c'\n", opt);
		}
	}

	const char *dirname = "";

	while (argv[optind])
		dirname = argv[optind++];

	binaryread_init(binary);

	std::regex search_pattern("(.*?)\\.gcno$");
	std::smatch matches;
	std::vector<std::string> all_files_in_dir = get_all_files_in_dir(dirname);
	for (auto& filename: all_files_in_dir)
	{
		if (std::regex_match(filename, matches, search_pattern)) {
			read_graph_file(filename.c_str());
		}
	}

//	for (auto& functions_it: functions_map)
//	{
//		for (auto& function_it: functions_it.second)
//		{
//			printf_verbose(ADDITIONAL_INFORMATION, "bb_in_map: source=%s lines= fn: %s\n", function_it.second->source.c_str(), function_it.second->fn_name.c_str());
//			for (auto& bb: function_it.second->basic_blocks)
//			{
//				for (auto& source_line: bb.source_lines)
//				{
//					printf_verbose(ADDITIONAL_INFORMATION, "; source=%s, lines=", source_line.filename.c_str());
//					for (unsigned line: source_line.lines)
//					{
//						printf("%d", line);
//					}
//				}
//			}
//		}
//	}

	determine_coverage();

	return 0;
}

static void print_usage ()
{
	printf ("Usage: gcov-dump-modified [OPTION] ... gcovfiles-directory\n");
	printf ("Print coverage file contents\n");
	printf ("  -h, --help                     Print this help\n");
	printf ("  -v, --verbose                  Print in a verbose form\n");
	printf ("  -s, --statistic-information    Print additional statistics and covered lines\n");
	printf ("  -p, --file-prefix              Absolut path prefix of the source files\n");
	printf ("  -b, --binary                   Path of the binary\n");
}


/**
 * Determine the coverage with basic block addresses after functions_map is filled with the data from the .gcno files.
 * The statistics of the coverage is printed as csv to stdout.
 */
void determine_coverage()
{
	printf_verbose(ADDITIONAL_INFORMATION, "covered basic blocks:\n");
	// get all covered basic blocks from the basic block addresses from stdin
	for (std::string line; std::getline(std::cin, line);)
	{
		input_addr_count++;
		unsigned long block_start_address = std::stoul(line, nullptr, 16);
		basic_block *bb = get_basic_block(block_start_address);
		if (bb == nullptr)
		{
			bb_at_addr_not_found_count++;
			continue;
		}
		for (auto &bb_source_line: bb->source_lines)
		{
			for (unsigned bb_line: bb_source_line.lines)
			{
				covered_lines[bb_source_line.filename].insert(bb_line);
			}
		}
		// print_basic_block(bb);
	}
	printf_verbose(ADDITIONAL_INFORMATION, "\n");

	unsigned int file_count = 0;
	unsigned long line_count = 0;

	for (auto &it: covered_lines) {
		line_count += it.second.size();
		if (!it.second.empty()) {
			file_count++;
		}
	}

	// failure summary
	fprintf(stderr, "Failure summary:\n");
	fprintf(stderr, "Address resolves not the start line of a basic block: %d/%d\n",
			addr_not_start_line_count, unique_input_addr_count);
	fprintf(stderr, "Basic block was already read from the .gcno files: %d/%d\n",
			bb_is_already_in_bb_map_count, bb_read_count);
	fprintf(stderr, "Basic block not found in .gcno files: %d/%d\n",
			bb_at_addr_not_found_count, unique_input_addr_count);
	fprintf(stderr, "addr2line returns null for file: %d/%d\n",
			file_is_null_in_addr2line_count, unique_input_addr_count);
	fprintf(stderr, "File from addr2line was found in .gcno files, but not the line: %d/%d\n",
			file_found_line_not_found_count, unique_input_addr_count);
	fprintf(stderr, "bb with fs in name is not in internal data structure: %d/%d\n\n",
			fs_not_in_bb_map_count, unique_input_addr_count);

	if (statistic_information)
	{
		std::pair<unsigned int, unsigned long> files_lines_count = count_all_files_and_lines();
		float file_count_percentage = (float) file_count / files_lines_count.first * 100;
		float line_count_percentage = (float) line_count / files_lines_count.second * 100;
		printf("total: file_count: %u/%u (%3.1f%%) line_count: %lu/%lu (%3.1f%%)\n\n",
			   file_count, files_lines_count.first, file_count_percentage, line_count, files_lines_count.second,line_count_percentage);
	}

	// print csv
	printf("filename\tlines_covered\tlines\n");
	for (auto& it: covered_lines)
	{
		std::string filename = it.first;
		std::set<unsigned> &lines = it.second;
		unsigned long all_lines_count = count_all_lines_in_file(filename);
		printf("%s\t%lu\t%lu\n", filename.c_str(), lines.size(), all_lines_count);
		if (statistic_information) {
			for (unsigned line: lines) {
				printf("%u\n", line);
			}
			printf("\n");
		}
	}
}

/**
 * Processes the .gcno file generated by gcov.
 * @param filename of the .gcno file
 */
static void read_graph_file (const char *filename)
{
	unsigned version;
	unsigned current_tag = 0;
	unsigned tag;
	bool is_data_type;

	if (!gcov_open (filename, 1))
	{
		printf_verbose (RARE_FAILURE, "%s:cannot open notes file\n", filename);
		return;
	}

	unsigned magic = gcov_read_unsigned ();
	int endianness = 0;

	if ((endianness = gcov_magic (magic, GCOV_DATA_MAGIC)))
		is_data_type = true;
	else if ((endianness = gcov_magic (magic, GCOV_NOTE_MAGIC)))
		is_data_type = false;
	else
	{
		printf_verbose (RARE_FAILURE, "%s:not a gcov file\n", filename);
		gcov_close ();
		return;
	}

	version = gcov_read_unsigned ();
	char v[4], m[4];

	GCOV_UNSIGNED2STRING (v, version);
	GCOV_UNSIGNED2STRING (m, magic);

	printf_verbose (ADDITIONAL_INFORMATION, "%s:%s:magic `%.4s':version `%.4s'%s\n", filename,
					is_data_type ? "data" : "note",
					m, v, endianness < 0 ? " (swapped endianness)" : "");

	unsigned stamp = gcov_read_unsigned ();
	/* Support for unexecuted basic blocks.  */
	unsigned support_unexecuted_blocks = gcov_read_unsigned ();
	if (!support_unexecuted_blocks)
		printf_verbose (RARE_FAILURE, "%s: has_unexecuted_block is not supported\n", filename);

	// iterate over all tags in the .gcno file and fill the function_info for each function
	function_info *fn = nullptr;
	while ((tag = gcov_read_unsigned ()))
	{
		unsigned length = gcov_read_unsigned ();
		unsigned base = gcov_position ();
		// first read the function
		if (tag == GCOV_TAG_FUNCTION)
		{
			const char *name = nullptr;
			const char *source = nullptr;

			// ident
			gcov_read_unsigned();
			// lineno_checksum
			gcov_read_unsigned();
			// cfg_checksum
			gcov_read_unsigned();

			fn = new function_info();

			// fn_name
			name = gcov_read_string();
			// printf("gcov: fn_name: %s\n", fn_name.c_str());
			// artificial
			gcov_read_unsigned();
			// source_name
			source = gcov_read_string();
			// start_line
			gcov_read_unsigned();
			// column_start
			gcov_read_unsigned();
			// end_line
			gcov_read_unsigned();

			if (name == nullptr || source == nullptr)
			{
				fn = nullptr;
				continue;
			}
			fn->fn_name = name;
			fn->source = source;

			functions_map[file_prefix + fn->source][fn->fn_name] = fn;

			current_tag = tag;
		}
		// than read the basic blocks for the function
		else if (fn && tag == GCOV_TAG_LINES)
		{
			basic_block bb;
			bb.blockno = gcov_read_unsigned ();

			printf_verbose(ADDITIONAL_INFORMATION, "gcno_read: bb_no=%d ", bb.blockno);

			while (true)
			{
				const char *source = nullptr;
				unsigned lineno = gcov_read_unsigned ();

				if (!lineno)
				{
					source = gcov_read_string ();
					if (!source)
						break;
				}

				if (lineno)
				{
					if (!bb.source_lines.back().lines.empty())
						printf_verbose(ADDITIONAL_INFORMATION, ",");
					printf_verbose(ADDITIONAL_INFORMATION, "%d", lineno);
					bb.source_lines.back().lines.push_back(lineno);
				}
				else
				{
					if (!bb.source_lines.empty())
						printf_verbose(ADDITIONAL_INFORMATION, "; ", source);
					printf_verbose(ADDITIONAL_INFORMATION, "source=%s lines=", source);
					source_line bb_source_line;
					bb_source_line.filename = source;
					bb.source_lines.push_back(bb_source_line);
				}
			}
			printf_verbose(ADDITIONAL_INFORMATION, "\n");

			bb_read_count++;

			std::string source_name = file_prefix + bb.get_filename();

			// create empty entries for the source names in covered_lines and add all lines of bb to coverable_lines
			for (auto &bb_source_line: bb.source_lines)
			{
				if (covered_lines.count(bb_source_line.filename) == 0)
				{
					covered_lines[bb_source_line.filename] = std::set<unsigned>();
				}
				for (unsigned line: bb_source_line.lines)
				{
					coverable_lines[bb_source_line.filename].insert(line);
				}
			}

			auto search_basic_blocks = basic_blocks_map.find(source_name);
			if (search_basic_blocks != basic_blocks_map.end() && !search_basic_blocks->second.empty())
			{
				auto search_bb = search_basic_blocks->second.find(bb);
				if (search_bb != search_basic_blocks->second.end())
				{
					printf_verbose(COMMON_FAILURE,
								   "bb in %s and start_line %d is already in basic_blocks_map. Merge with existing bb.\n",
								   source_name.c_str(), bb.get_start_line());

					for (auto& sl: search_bb->source_lines)
					{
						bb.source_lines.push_back(sl);
					}
					basic_blocks_map[source_name].erase(search_bb);
					bb_is_already_in_bb_map_count++;
				}
			}

			// fn->basic_blocks.insert(bb);

			basic_blocks_map[source_name].insert(bb);
		}
		else if (current_tag && !GCOV_TAG_IS_SUBTAG (current_tag, tag))
		{
			fn = nullptr;
			current_tag = 0;
		}
		gcov_sync (base, length);
		if (gcov_is_error ())
		{
			corrupt:;
			printf_verbose (RARE_FAILURE, "%s:corrupted\n", filename);
			break;
		}
	}
	gcov_close ();
}

/**
 * @return count of all coverable files and functions
 */
std::pair<unsigned, unsigned long> count_all_files_and_lines()
{
	unsigned long count_lines = 0;
	for (auto &it: coverable_lines)
	{
		count_lines += it.second.size();
	}
	return std::pair<unsigned, unsigned long>{coverable_lines.size(), count_lines};
}

/**
 * @param filename of source file in which all coverable lines are counted
 * @return count of all coverable lines in source file
 */
unsigned long count_all_lines_in_file(std::string& filename)
{
	unsigned long count_lines = 0;
	auto search_lines = coverable_lines.find(filename);
	if (search_lines != coverable_lines.end())
	{
		count_lines += search_lines->second.size();
	}
	return count_lines;
}

/**
 * Get for a basis block address the corresponding basis block.
 * The line determined by bb_addr is smaller than the start line of the returned basic block
 * and smaller than the start line of the succeding basic block.
 * @param bb_addr : Basis block address to be searched
 * @return with the basis block address found basis block and nullptr if there is no basic block found
 */
basic_block* get_basic_block(unsigned long bb_addr)
{
	// first try to find basis block direct in basic_blocks_addr_map
	auto search_addr = basic_blocks_addr_map.find(bb_addr);
	if (search_addr != basic_blocks_addr_map.end())
	{
		printf_verbose(COMMON_FAILURE, "bb_read: addr=%lx, file=%s, start_line=%u, found again\n",
					   bb_addr, search_addr->second.get_filename().c_str(), search_addr->second.get_start_line());
		return &search_addr->second;
	}

	unique_input_addr_count++;

	// find the basis block through the functions in functions_map
	BfdSearchCtx bfdSearchCtx = addr_to_line(bb_addr);
	// a rare error and not really investigated
	if (bfdSearchCtx.file == nullptr)
	{
		printf_verbose(RARE_FAILURE, "file from addr2line was null for bb addr: %lx\n", bb_addr);
		file_is_null_in_addr2line_count++;
		return nullptr;
	}
	// search functions with source file name from addr2line
	auto search_basic_blocks = basic_blocks_map.find(bfdSearchCtx.file);
	if (search_basic_blocks != basic_blocks_map.end() && !search_basic_blocks->second.empty())
	{
		// search basic block with line from addr2line
		// to find the basic block the line has to be smaller than the start line of the basic block
		// and smaller than the start line of the succeding basic block
		basic_block bb;
		source_line bb_source_line;
		bb_source_line.lines.push_back(bfdSearchCtx.line);
		bb.source_lines.push_back(bb_source_line);
		auto search_bb = search_basic_blocks->second.upper_bound(bb);
		if (search_bb != search_basic_blocks->second.begin())
		{
			search_bb--;
			if (search_bb->get_start_line() != bfdSearchCtx.line)
			{
				printf_verbose(ADDITIONAL_INFORMATION,
						"bb addr %lx is not the start line of bb: file=%s, start line=%u\n",
						bb_addr, bfdSearchCtx.file, search_bb->get_start_line());
				addr_not_start_line_count++;
			}
			basic_blocks_addr_map[bb_addr] = *search_bb;
			printf_verbose(COMMON_FAILURE, "bb_read: addr=%lx, file=%s, start_line=%u, fn=%s, found\n",
						   bb_addr, bfdSearchCtx.file, bfdSearchCtx.line, bfdSearchCtx.fn);
			return &basic_blocks_addr_map[bb_addr];
		}
		else
		{
			printf_verbose(RARE_FAILURE, "file %s was found, but not the line %d for bb addr %lx\n",
						   bfdSearchCtx.file, bfdSearchCtx.fn, bfdSearchCtx.line, bb_addr);
			file_found_line_not_found_count++;
		}
	}
	printf_verbose(COMMON_FAILURE, "bb_read: addr=%lx, file=%s, start_line=%u, fn=%s, not found!\n",
			bb_addr, bfdSearchCtx.file, bfdSearchCtx.line, bfdSearchCtx.fn);
	if (std::string(bfdSearchCtx.file).find("/fs/") != std::string::npos) {
		fs_not_in_bb_map_count++;
	}
	return nullptr;
}

/**
 * Get the list of all files in given directory and its sub directories.
 * @param dirPath : Path of directory to be traversed
 * @return vector containing paths of all the files in given directory and its sub directories
 */
std::vector<std::string> get_all_files_in_dir(const std::string &dirPath)
{

	// Create a vector of string
	std::vector<std::string> listOfFiles;
	try {
		// Check if given path exists and points to a directory
		if (std::experimental::filesystem::exists(dirPath) && std::experimental::filesystem::is_directory(dirPath))
		{
			// Create a Recursive Directory Iterator object and points to the starting of directory
			std::experimental::filesystem::recursive_directory_iterator iter(dirPath);

			// Create a Recursive Directory Iterator object pointing to end.
			std::experimental::filesystem::recursive_directory_iterator end;

			// Iterate till end
			while (iter != end)
			{
				// Add the name in vector
				listOfFiles.push_back(iter->path().string());

				std::error_code ec;
				// Increment the iterator to point to next entry in recursive iteration
				iter.increment(ec);
				if (ec) {
					std::cerr << "Error While Accessing : " << iter->path().string() << " :: " << ec.message() << '\n';
				}
			}
		}
	}
	catch (std::system_error & e)
	{
		std::cerr << "Exception :: " << e.what();
	}
	return listOfFiles;
}

void print_basic_block(const basic_block* bb)
{
	if (bb != nullptr && verbose >= ADDITIONAL_INFORMATION)
	{
		printf_verbose(ADDITIONAL_INFORMATION, "Basicblock %d: %s: ", bb->blockno, bb->get_filename().c_str());
		for (auto &bb_source_line: bb->source_lines)
		{
			printf_verbose(ADDITIONAL_INFORMATION, "%s ", bb_source_line.filename.c_str());
			for (unsigned line: bb_source_line.lines)
			{
				printf_verbose(ADDITIONAL_INFORMATION, "%d ", line);
			}
		}
		printf_verbose(ADDITIONAL_INFORMATION, "\n");
	}
}
