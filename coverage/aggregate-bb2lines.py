import sys
import argparse
import re
from os.path import dirname


aggregation = {"lines_covered": 0, "lines": 0}
dirname_dict = {}


def main():
    argv = sys.argv[1:]
    parser = argparse.ArgumentParser(prog='aggregate-bb2lines')
    parser.add_argument('--filter-pattern', help="Aggregates all filenames with regex pattern in name.")
    parser.add_argument('--aggregate-dir', action="store_true", help="Aggregates to dirnames")
    parse_out = parser.parse_args(argv)
    filter_pattern = parse_out.filter_pattern
    if filter_pattern is None:
        filter_pattern_regex = None
    else:
        filter_pattern_regex = re.compile(filter_pattern)

    aggregate_dir = parse_out.aggregate_dir

    for i, line in enumerate(sys.stdin):
        if line == '':  # If empty string is read then stop the loop
            break
        process_csv_line(line, filter_pattern_regex, aggregate_dir)

    if aggregate_dir:
        print_dirname_dict(dirname_dict)
    else:
        print("filter-pattern,lines-covered,lines")
        print("{},{},{}".format(filter_pattern, aggregation["lines_covered"], aggregation["lines"]))


def process_csv_line(line, filter_pattern, aggregate_dir):
    if line == '\n':
        return
    trimmed_line = line[:-1]
    parsed_line = trimmed_line.split(",")
    if len(parsed_line) != 3:
        return
    try:
        parsed_line_dict = {"file_name": parsed_line[0], "lines_covered": int(parsed_line[1]), "lines": int(parsed_line[2])}
        if aggregate_dir:
            aggregate_line_to_dirnames(parsed_line_dict, filter_pattern)
        else:
            aggregate_line(parsed_line_dict, filter_pattern)
        # print(parsed_line)
    except ValueError:
        return


def aggregate_line(line, filter_pattern):
    global aggregation
    if filter_pattern is None or filter_pattern.search(line["file_name"]):
        aggregation["lines_covered"] += line["lines_covered"]
        aggregation["lines"] += line["lines"]


def aggregate_line_to_dirnames(line, filter_pattern):
        file_dirname = dirname(line["file_name"])
        if filter_pattern is None or filter_pattern.search(file_dirname):
            try:
                dirname_dict[file_dirname]["lines"] += line["lines"]
                dirname_dict[file_dirname]["lines_covered"] += line["lines_covered"]
            except KeyError:
                dirname_dict[file_dirname] = {"lines_covered": line["lines_covered"], "lines": line["lines"]}


def print_dirname_dict(dirnames):
    print("dirname,lines-covered,lines")
    for key in dirnames.keys():
        print("{},{},{}".format(key, dirnames[key]["lines_covered"], dirnames[key]["lines"]))


if __name__ == "__main__":
    main()
