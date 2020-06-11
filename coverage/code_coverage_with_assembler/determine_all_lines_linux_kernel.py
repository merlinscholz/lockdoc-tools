import argparse
import sys
import pickle
import re


def main():
    argv = sys.argv[1:]
    parser = argparse.ArgumentParser(prog='determine_all_lines')
    # instr_order wird durch write_objdump_to_dict.py erzeugt
    parser.add_argument('--instr-order', help="instr_order is generated with write_objdump_to_dict.py", required=True)
    # instr_lines wird durch write_addr2line_to_dict.py erzeugt
    parser.add_argument('--instr-lines', help="instr_lines is generated with write_addr2line_to_dict.py", required=True)
    parser.add_argument('--file-filter', help="filters with a regex all file names, which should be considered for the coverable lines", required=True)
    parser.add_argument('--output', help="Writes all coverable lines in the output file", required=True)
    parse_out = parser.parse_args(argv)

    instr_order = load_instr(parse_out.instr_order)
    print("instr_order loaded")
    instr_lines = load_instr(parse_out.instr_lines)
    print("instr_lines loaded")

    file_count, line_count, all_lines = determine_all_lines(instr_order, instr_lines, parse_out.file_filter)
    save_all_lines(all_lines, file_count, line_count, parse_out.output)


"""
determine_all_lines(instr_order:list, instr_lines:dict, file_filter:str) -> int, int, dict
Bestimmt alle ueberdeckbare Zeilen des Linux_Kernels unter Beruecksichtigung des Filters
instr_order: Enthaelt alle instr ptr des Linux-Kernels (ausser unrelevante instr ptr) in korrekter Reihenfolge der Ausführung
instr_lines: Enthaelt in Abhaengigkeit zu einem instr ptr alle Namen der ueberdeckbare Dateien und Zeilen des Linux-Kernels in der Form von zb {"ffffffff81545de0": {"file": ["/include/linux/file.h", "/fs/open.c"], "line": [60, 165]}}
file_filter: Filtert mit regex Dateinamen des Linux-Kernels, die bei den ueberdeckbaren Zeilen beruecksichtigt werden sollen
"""
def determine_all_lines(instr_order, instr_lines, file_filter):
    all_lines = {}
    count_question_marks = 0

    for i, ptr in enumerate(instr_order):
        line = instr_lines[ptr]
        if line["file"] != ["??"] and line["line"] != ["?"]:
            for f, l in zip(line["file"], line["line"]):
                if re.search(file_filter, f):
                    if f not in all_lines.keys():
                        all_lines[f] = [l]
                    elif l not in all_lines[f]:
                        all_lines[f].append(l)
        else:
            count_question_marks += 1

        if (i+1) % 100000 == 0:
            print((i+1), "of", len(instr_order), "\t", str(round((i+1)/len(instr_order)*100)) + "%")

    print("Count of ?:", count_question_marks, "of", len(instr_order))

    all_lines_count = 0
    for file in all_lines:
        all_lines_count += len(all_lines[file])
    print("Found", len(all_lines), "files", "with", all_lines_count, "lines")
    return len(all_lines), all_lines_count, all_lines


"""
save_all_lines(all_lines:dict, file_count:int, line_count:int, filename:str) -> None
Schreibt alle überdeckbare Zeilen mit den zugehörigen Dateinamen in eine Datei
In der ersten Zeile steht die Anzahl der Dateinamen und die Gesamtanzahl an Zeilen
all_lines: Alle ueberdeckbaren Zeilen mit den dazu gehoerigen Dateien aus dem Linux-Kernel
file_count: Anzahl der Dateien des Linux Kernels, die ueberdeckbare Zeilen beinhalten
line_count: Gesamtanzahl der ueberdeckbare Zeilen aus dem Linux-Kernel
filename: Dateiname der Datei in die die uerbdeckbaren Zeilen geschrieben werden
"""
def save_all_lines(all_lines, file_count, line_count, filename):
    result = "file_count:" + str(file_count) + " line_count:" + str(line_count) + "\n"
    for file in all_lines:
        result += "file:" + file + "\n"
        for line in sorted([int(l) for l in all_lines[file]]):
            result += str(line) + "\n"
        result += "\n"
    with open(filename, "w") as file:
        file.write(result)
    print("Saved all lines to", filename)


"""
load_instr(filename:str) -> nicht festgelegt (in diesem Fall list oder dict)
Läd alle mit pickle gespeicherten Python Objekte, also hier --instr-order, --instr-jmps und --instr-lines
filename: Speicherort der pickle Dateien
"""
def load_instr(filename):
    with open(filename, "rb") as handle:
        a = pickle.load(handle)
    return a


if __name__ == '__main__':
    main()
