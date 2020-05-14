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


struct basic_block
{
	unsigned blockno;
	std::string source;
	std::vector<unsigned> lines;
	bool operator<(const basic_block& rhs) const
	{
		return lines.front() < rhs.lines.front();
	}
	bool operator>(const basic_block& rhs) const
	{
		return lines.front() > rhs.lines.front();
	}
	bool operator==(const basic_block& rhs) const
	{
		return lines.front() == rhs.lines.front();
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
static void print_usage (void);
std::pair<unsigned int, unsigned long> count_all_files_and_lines();
unsigned long count_all_lines_in_file(std::string& filename);
unsigned long count_all_lines_in_function(function_info *fn);
std::vector<std::string> get_all_files_in_dir(const std::string &dirPath);
basic_block* get_basic_block(unsigned long);
void print_basic_block(const basic_block*);
extern int main (int, char **);

//key: addr of basic block; value: basic block
std::unordered_map<unsigned long, basic_block> basic_blocks_addr_map;
// key: name of source file; value: set of covered lines in a source file
std::unordered_map<std::string, std::set<unsigned>> covered_lines;
// key: name of source file; value: map of functions in file with the function name as key
std::unordered_map<std::string, std::unordered_map<std::string, function_info*>> functions_map;

std::unordered_map<std::string, std::set<basic_block*>> basic_blocks_map;

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

int printf_verbose(int verbosity, const char * format, ...)
{
	if(verbose < verbosity)
		return 0;

	va_list args;
	va_start(args, format);
	int ret = vprintf(format, args);
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

	for (auto& functions_it: functions_map)
	{
		for (auto& function_it: functions_it.second)
		{
			printf("source: %s\t\tfunction name: %s\n", function_it.second->source.c_str(), function_it.second->fn_name.c_str());
			for (auto& bb: function_it.second->basic_blocks)
			{
				printf("blockno: %d\n", bb.blockno);
				for (auto& line: bb.lines)
				{
					printf("%d\n", line);
				}
			}
			printf("------------------------------------------------------------------------\n");
		}
	}

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
void determine_coverage() {
	printf_verbose(ADDITIONAL_INFORMATION, "covered basic blocks:\n");
	// get all covered basic blocks from the basic block addresses from stdin
	for (std::string line; std::getline(std::cin, line);) {
		unsigned long block_start_address = std::stoul(line, nullptr, 16);
		basic_block *bb = get_basic_block(block_start_address);
		if (bb == nullptr) {
			printf_verbose(COMMON_FAILURE, "Basic block at start address %lx not found.\n", block_start_address);
			continue;
		}
		for (auto &bb_line: bb->lines) {
			covered_lines[bb->source].insert(bb_line);
		}
		print_basic_block(bb);
	}
	printf_verbose(COMMON_FAILURE, "\n");

	unsigned int file_count = 0;
	unsigned long line_count = 0;

	for (auto &it: covered_lines) {
		line_count += it.second.size();
		if (it.second.size() > 0) {
			file_count++;
		}
	}

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
		std::string full_filename = file_prefix + it.first;
		unsigned long all_lines_count = count_all_lines_in_file(full_filename);
		printf("%s\t%lu\t%lu\n", it.first.c_str(), it.second.size(), all_lines_count);
		if (statistic_information) {
			for (auto &line: it.second) {
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
			bool read_basic_block = false;
			bool is_in_fs = false;
			bb.blockno = gcov_read_unsigned ();

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
					if (is_in_fs)
					{
						printf("%d\n", lineno);
						bb.lines.push_back(lineno);
					}
				}
				else
				{
					printf("source: %s\n", source);
					std::string source_tmp = source;
					if (source_tmp.substr(0, 3) == "fs/")
					{
						bb.source = source;
						is_in_fs = true;
						read_basic_block = true;
					}
					else
					{
						is_in_fs = false;
					}
				}
			}
			printf("_________________________________________\n");

			if (read_basic_block)
			{
				std::string source_name = file_prefix + bb.source;
				if (fn->basic_blocks.count(bb) > 0) {
					printf_verbose(COMMON_FAILURE,
								   "Basic block in %s and start_line %d is already in basic_blocks_map.\n",
								   source_name.c_str(), bb.lines.front());
					continue;
				}

				// create empty entry for the source name in covered_lines
				if (covered_lines.count(bb.source) == 0) {
					covered_lines[bb.source] = std::set<unsigned int>();
				}

				fn->basic_blocks.insert(bb);
			}
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
std::pair<unsigned int, unsigned long> count_all_files_and_lines()
{
	std::set<std::string> all_files;
	unsigned long count_lines = 0;
	for (auto& functions_in_file: functions_map)
	{
		for (auto& it_fn: functions_in_file.second)
		{
			all_files.insert(it_fn.second->source);
			count_lines += count_all_lines_in_function(it_fn.second);
		}
	}
	return std::pair<unsigned int, unsigned long>{all_files.size(), count_lines};
}

/**
 * @param filename of source file in which all coverable lines are counted
 * @return count of all coverable lines in source file
 */
unsigned long count_all_lines_in_file(std::string& filename)
{
	unsigned long count_lines = 0;
	auto search_functions = functions_map.find(filename);
	if (search_functions != functions_map.end())
	{
		for (auto& it_fn: search_functions->second)
		{
			count_lines += count_all_lines_in_function(it_fn.second);
		}
	}
	return count_lines;
}

/**
 * @param function in which all coverable lines are counted
 * @return count of all coverable lines in function
 */
unsigned long count_all_lines_in_function(function_info *function)
{
	unsigned long count_lines = 0;
	for (auto& bb: function->basic_blocks)
	{
		count_lines += bb.lines.size();
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
		return &search_addr->second;
	}

	// find the basis block through the functions in functions_map
	BfdSearchCtx bfdSearchCtx = addr_to_line(bb_addr);
	printf_verbose(ADDITIONAL_INFORMATION, "addr2line: file: %s line: %d fn: %s\n", bfdSearchCtx.file, bfdSearchCtx.line, bfdSearchCtx.fn);
	// a rare error and not really investigated
	if (bfdSearchCtx.file == nullptr)
	{
		printf_verbose(RARE_FAILURE, "file from addr2line was null for basic block address: %lx\n", bb_addr);
		return nullptr;
	}
	// search functions with source file name from addr2line
	auto search_functions = functions_map.find(bfdSearchCtx.file);
	if (search_functions != functions_map.end() && search_functions->second.size() != 0)
	{
		// search function with function name from addr2line
		auto search_function = search_functions->second.find(bfdSearchCtx.fn);
		if (search_function != search_functions->second.end())
		{
			// search basic block with line from addr2line
			// to find the basic block the line has to be smaller than the start line of the basic block
			// and smaller than the start line of the succeding basic block
			basic_block bb;
			bb.lines.push_back(bfdSearchCtx.line);
			function_info *fn = search_function->second;
			auto search_bb = fn->basic_blocks.upper_bound(bb);
			if (search_bb != fn->basic_blocks.begin())
			{
				search_bb--;
				if (search_bb->lines.front() != bfdSearchCtx.line)
				{
					printf_verbose(ADDITIONAL_INFORMATION,
							"basic block addr %lx is not the start line of the basic block: file: %s; start line: %u\n",
							bb_addr, bfdSearchCtx.file, *search_bb->lines.begin());
				}
				basic_blocks_addr_map[bb_addr] = *search_bb;
				return &basic_blocks_addr_map[bb_addr];
			}
			else
			{
				printf_verbose(RARE_FAILURE, "file %s and function %s was found, but not the line %d for basic block address %lx\n",
							   bfdSearchCtx.file, bfdSearchCtx.fn, bfdSearchCtx.line, bb_addr);
			}
		}
		else
		{
			printf_verbose(RARE_FAILURE, "file %s was found, but not the function %s with line %d for basic block address %lx\n",
						   bfdSearchCtx.file, bfdSearchCtx.fn, bfdSearchCtx.line, bb_addr);
		}
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
		printf_verbose(ADDITIONAL_INFORMATION, "Basicblock %d: %s: ", bb->blockno, bb->source.c_str());
		for (unsigned line: bb->lines)
		{
			printf_verbose(ADDITIONAL_INFORMATION, "%d ", line);
		}
		printf_verbose(ADDITIONAL_INFORMATION, "\n");
	}
}
