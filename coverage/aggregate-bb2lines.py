import sys
import argparse
import re
from os.path import dirname


aggregation = {"lines_covered": 0, "lines": 0}


def main():
    argv = sys.argv[1:]
    parser = argparse.ArgumentParser(prog='aggregate-bb2lines')
    parser.add_argument('--filter-pattern', help="Aggregates all filenames with regex pattern in name.")
    parse_out = parser.parse_args(argv)
    filter_pattern = parse_out.filter_pattern
    if filter_pattern is None:
        filter_pattern_regex = None
    else:
        filter_pattern_regex = re.compile(filter_pattern)

    for i, line in enumerate(sys.stdin):
        if i > 1:
            if line == '':  # If empty string is read then stop the loop
                break
            process_csv_line(line, filter_pattern_regex)

    print("filter-pattern,lines-covered,lines")
    print("{},{},{}".format(filter_pattern, aggregation["lines_covered"], aggregation["lines"]))


def process_csv_line(line, filter_pattern):
    if line == '\n':
        return
    trimmed_line = line[:-1]
    parsed_line = trimmed_line.split(",")
    if len(parsed_line) != 3:
        return
    try:
        parsed_line_dict = {"file_name": parsed_line[0], "lines_covered": int(parsed_line[1]), "lines": int(parsed_line[2])}
        aggregate_line(parsed_line_dict, filter_pattern)
        # print(parsed_line)
    except ValueError:
        return


def aggregate_line(line, filter_pattern):
    global aggregation
    if filter_pattern is None or filter_pattern.search(line["file_name"]):
        aggregation["lines_covered"] += line["lines_covered"]
        aggregation["lines"] += line["lines"]


if __name__ == "__main__":
    main()
