#!/usr/bin/python3

import sys
import argparse
import re
from os.path import dirname

# Robin Thunig 2019, Alexander Lochmann 2019
# This scripts parses the output written by geninfo (lcov), and
# outputs execution statistics on a per-file basis (or per directory).
# You can restrict filenames/directories using a regex: --filter="^fs/".
# Usage: ./gcov-summary.py --kernel-directory /opt/kernel/linux-4-10 --filter "^fs/" example.out


def main():
    argv = sys.argv[1:]

    parser = argparse.ArgumentParser(prog='PROG')
    parser.add_argument('--not-filter-doubling', action="store_true")
    parser.add_argument('--kernel-directory', help='Path to the kernel source tree. Used to strip the filenames.')
    parser.add_argument('--filter', help='Filter filenames or directories by a regex')
    parser.add_argument('--per-directory', action="store_true", help='Summarize statistics per directory')
    parser.add_argument('input', nargs=1)
    parse_out = parser.parse_args(argv)
    kernel_directory = parse_out.kernel_directory
    filename = parse_out.input[0]
    filter_pattern = parse_out.filter
    filter_doubling = not parse_out.not_filter_doubling
    is_per_dir = parse_out.per_directory

    trace_dict = parse_trace_into_dict(filename, kernel_directory, filter_doubling)
    first_column = "filename"
    if is_per_dir:
        trace_dict = convert_dict_to_dirname_dict(trace_dict)
        first_column = "directory"
    write_dict_into_csv(trace_dict, first_column, filter_pattern)

def parse_trace_into_dict(filename, kernel_directory, filter_doubling):
    with open(filename) as f:
        lines = f.readlines()
    f.close()

    result_dict = {}
    parsed_filename = ""
    for i, line in enumerate(lines):
        if has_prefix("SF:", line):
            parsed_filename = parse_SF(line)
            if parsed_filename.startswith(kernel_directory):
                parsed_filename = parsed_filename[len(kernel_directory):]
            else:
                print("param --kernel_directory does not match")
                sys.exit(1)

            if parsed_filename not in result_dict.keys():
                if filter_doubling:
                    result_dict[parsed_filename] = {"found_lines_list": [], "covered_lines_list": [], "lines_covered": 0, "lines": 0, "functions": {}}
                else:
                    result_dict[parsed_filename] = {"lines_covered": 0, "lines": 0, "functions": {}}

        if parsed_filename is not None:
            if filter_doubling and has_prefix("DA:", line):
                line_number, execution_count = parse_DA(line)
                if line_number not in result_dict[parsed_filename]["found_lines_list"]:
                    result_dict[parsed_filename]["found_lines_list"].append(line_number)
                    result_dict[parsed_filename]["lines"] += 1
                if (execution_count > 0) and (line_number not in result_dict[parsed_filename]["covered_lines_list"]):
                    result_dict[parsed_filename]["covered_lines_list"].append(line_number)
                    result_dict[parsed_filename]["lines_covered"] += 1

            elif not filter_doubling and has_prefix("LF:", line):
                result_dict[parsed_filename]["lines"] += parse_LF(line)

            elif not filter_doubling and has_prefix("LH:", line):
                result_dict[parsed_filename]["lines_covered"] += parse_LH(line)

            elif has_prefix("FN:", line):
                line_number_of_fn_start, fn_name = parse_FN(line)
                try:
                    result_dict[parsed_filename]["functions"][fn_name]["line_number_of_fn_start"] = line_number_of_fn_start
                except KeyError:
                    result_dict[parsed_filename]["functions"][fn_name] = {"line_number_of_fn_start": line_number_of_fn_start, "execution_count": 0}

            elif has_prefix("FNDA:", line):
                execution_count, fn_name = parse_FNDA(line)
                try:
                    result_dict[parsed_filename]["functions"][fn_name]["execution_count"] += execution_count
                except KeyError:
                    result_dict[parsed_filename]["functions"][fn_name] = {"line_number_of_fn_start": -1, "execution_count": execution_count}

    return result_dict


    print(first_column + ",lines,lines_covered,functions,functions_covered")
def write_dict_into_csv(trace_dict, first_column, filter_pattern):
    trace_dict_keys = list(trace_dict.keys())
    trace_dict_keys = sorted(trace_dict_keys)
    for file_name in trace_dict_keys:
        functions_hit = 0
        if filter_pattern is not None and not re.search(filter_pattern, file_name):
            continue
        for func in trace_dict[file_name]["functions"].keys():
            if trace_dict[file_name]["functions"][func]["execution_count"] > 0:
                functions_hit += 1
        print("%s,%s,%s,%s,%s" % (file_name, trace_dict[file_name]["lines"], trace_dict[file_name]["lines_covered"], len(trace_dict[file_name]["functions"].keys()), functions_hit))


def convert_dict_to_dirname_dict(trace_dict):
    dirname_dict = {}
    for file_name in trace_dict.keys():
        file_dirname = dirname(file_name)
        try:
            dirname_dict[file_dirname]["lines"] += trace_dict[file_name]["lines"]
            dirname_dict[file_dirname]["lines_covered"] += trace_dict[file_name]["lines_covered"]
        except KeyError:
            dirname_dict[file_dirname] = {"lines": trace_dict[file_name]["lines"], "lines_covered": trace_dict[file_name]["lines_covered"], "functions": {}}
        for function in trace_dict[file_name]["functions"].keys():
            try:
                dirname_dict[file_dirname]["functions"][function]["execution_count"] += trace_dict[file_name]["functions"][function]["execution_count"]
                dirname_dict[file_dirname]["functions"][function]["line_number_of_fn_start"] = trace_dict[file_name]["functions"][function]["line_number_of_fn_start"]
            except KeyError:
                dirname_dict[file_dirname]["functions"][function] = {"execution_count": trace_dict[file_name]["functions"][function]["execution_count"], "line_number_of_fn_start": trace_dict[file_name]["functions"][function]["line_number_of_fn_start"]}

    return dirname_dict



def has_prefix(prefix, line):
    return prefix == line[:len(prefix)]


def parse_FNH(input):
    input = input[4:len(input) - 1]
    return int(input)


def parse_FNF(input):
    input = input[4:len(input) - 1]
    return int(input)


def parse_LF(input):
    input = input[3:len(input)-1]
    return int(input)


def parse_LH(input):
    input = input[3:len(input)-1]
    return int(input)


def parse_FN(input):
    input = input[3:len(input)-1]
    splitted = input.split(",")
    return int(splitted[0]), splitted[1]


def parse_FNDA(input):
    input = input[5:len(input)-1]
    splitted = input.split(",")
    return int(splitted[0]), splitted[1]


def parse_SF(input):
    input = input[3:len(input)-1]
    return input


def parse_DA(input):
    input = input[3:len(input)-1]
    splitted = input.split(",")
    return int(splitted[0]), int(splitted[1])


if __name__ == "__main__":
    main()
