import pickle
import argparse
import sys
import time

# Zaehlt wie haeufig ein instr ptr nicht in instr-order gefunden werden konnte (alle fehltreffer sind auf nop zurueckzufuehren)
count_q = 0
# Zaehlt wie haeufig die Datei verlassen wurde, da kein Sprungbefehl zwischen diesen gefunden werden konnte (ist eventuell auf fehlerhafte Debug-Informationen zurueckzufuehren)
count_i = 0


def main():
    argv = sys.argv[1:]
    parser = argparse.ArgumentParser(prog='determine_covered_lines')
    parser.add_argument('--instr-ptrs', help="instr-ptrs is generated with Syzkaller", required=True)
    # instr_order wird durch write_objdump_to_dict.py erzeugt
    parser.add_argument('--instr-order', help="instr_order is generated with write_objdump_to_dict.py", required=True)
    # instr_jmps wird durch write_objdump_to_dict.py erzeugt
    parser.add_argument('--instr-jmps', help="instr_jmps is generated with write_objdump_to_dict.py", required=True)
    # instr_lines wird durch write_addr2line_to_dict.py erzeugt
    parser.add_argument('--instr-lines', help="instr_lines is generated with write_addr2line_to_dict.py", required=True)
    # all-coverable-lines wird durch determine_all_lines_linux_kernel.py erzeugt
    parser.add_argument('--all-coverable-lines', help="all-coverable-lines is generated with determine_all_lines_linux_kernel.py", required=True)
    parser.add_argument('--output', help="Writes all covered lines of the linux kernel in the output file with no respect to all-coverable-lines", required=True)
    parse_out = parser.parse_args(argv)

    t_start = time.time()
    instr_jmps = load_instr(parse_out.instr_jmps)
    print("instr_jmps loaded")
    instr_order = load_instr(parse_out.instr_order)
    print("instr_order loaded")
    instr_lines = load_instr(parse_out.instr_lines)
    print("instr_lines loaded")
    instr_ptrs = load_instr_ptrs(parse_out.instr_ptrs)
    print("instr_ptrs loaded")
    save_covered_lines_filename = parse_out.output
    all_coverable_lines = parse_out.all_coverable_lines

    covered_lines = determine_covered_lines(instr_ptrs, instr_jmps, instr_order, instr_lines)
    all_lines_file_count, all_lines_line_count, all_lines = load_all_lines_in_dict(all_coverable_lines)
    count_covered_lines_in_all_lines, covered_lines_of_coverable = compare_covered_lines_to_all_lines(covered_lines, all_lines)
    save_covered_lines(covered_lines_of_coverable, save_covered_lines_filename)
    print(count_covered_lines_in_all_lines, "of", all_lines_line_count, "(" + str(round(float(count_covered_lines_in_all_lines)/all_lines_line_count*100, 1)) + "%)", "lines are covered  in", all_coverable_lines)
    print(time.time() - t_start, "seconds after start")


"""
determine_covered_lines(instr_ptrs:list, instr_jmps:dict, instr_order:list, instr_lines:dict) -> dict
Bestimmt die ueberdeckten Zeilen aus den instr ptr, die durch den Syzkaller bestimmt wurden, und gibt die Zeilen mit den dazugehoerigen Dateien des Linux-Kernels zurueck
instr_ptrs: Enthaelt die durch den Syzkaller bestimmten instr ptr
instr_jmps: Enthaelt alle instr ptr des Linux-Kernels (ausser unrelevante instr ptr) und einem dazugehoerigen Eintrag ob ein Sprungbefehl vorliegt oder nicht
instr_order: Enthaelt alle instr ptr des Linux-Kernels (ausser unrelevante instr ptr) in korrekter Reihenfolge der Ausführung
instr_lines: Enthaelt in Abhaengigkeit zu einem instr ptr alle Namen der ueberdeckbare Dateien und Zeilen des Linux-Kernels in der Form von zb {"ffffffff81545de0": {"file": ["/include/linux/file.h", "/fs/open.c"], "line": [60, 165]}}
"""
def determine_covered_lines(instr_ptrs, instr_jmps, instr_order, instr_lines):
    covered_lines = {}
    for i, instr_ptr in enumerate(instr_ptrs):
        determine_covered_lines_between_instr_ptr_and_jmp(covered_lines, instr_ptr, instr_jmps, instr_order, instr_lines)
        if (i+1) % 10000 == 0:
            print(str(i+1)+"/"+str(len(instr_ptrs)), str(round((i+1)/len(instr_ptrs)*100)) + "%")

    all_lines_count = 0
    for file in covered_lines:
        all_lines_count += len(covered_lines[file])
    print("Found", len(covered_lines), "files", "with", all_lines_count, "covered lines")
    return covered_lines


"""
determine_covered_lines_between_instr_ptr_and_jmp(covered_lines:dict, instr_ptr:list, instr_jmps:dict, instr_order:list, instr_lines:dict) -> None
Bestimmt die ueberdeckten Zeilen zwischen dem uebergebenen instr ptr und dem darauf folgenden naechsten Sprungbefehl und speichert diese Zeilen in covered_lines
covered_lines: Enthaelt die aus den instr ptr bestimmten ueberdeckten Zeilen. Die instr ptr werden durch den Syzkaller bestimmt
instr_ptr: Enthaelt einen durch den Syzkaller bestimmten instr ptr
instr_jmps: Enthaelt alle instr ptr des Linux-Kernels (ausser unrelevante instr ptr) und einem dazugehörigen Eintrag ob ein Sprungbefehl vorliegt oder nicht
instr_order: Enthaelt alle instr ptr des Linux-Kernels (ausser unrelevante instr ptr) in korrekter Reihenfolge der Ausführung
instr_lines: Enthaelt in Abhaengigkeit zu einem instr ptr alle Namen der ueberdeckbare Dateien und Zeilen des Linux-Kernels in der Form von zb {"ffffffff81545de0": {"file": ["/include/linux/file.h", "/fs/open.c"], "line": [60, 165]}}
"""
def determine_covered_lines_between_instr_ptr_and_jmp(covered_lines, instr_ptr, instr_jmps, instr_order, instr_lines):
    local_covered_lines_consistency = []
    ptr = instr_ptr
    try:
        while not instr_jmps[ptr]["is_jump"] and instr_lines[ptr]["file"] != ["??"]:
            line = instr_lines[ptr]
            if len(local_covered_lines_consistency) > 0 and line["file"][len(line["file"]) - 1] != local_covered_lines_consistency[0]["file"][len(local_covered_lines_consistency[0]["file"]) - 1]:
                # print(line["file"], local_covered_lines_consistency[0]["file"], ptr)
                # print("Files are not identical")
                global count_i
                count_i += 1

            if len(local_covered_lines_consistency) == 0 or line["file"] != local_covered_lines_consistency[len(local_covered_lines_consistency)-1]["file"] or line["line"] != local_covered_lines_consistency[len(local_covered_lines_consistency)-1]["line"]:
                local_covered_lines_consistency.append({"file": line["file"], "line": line["line"]})
                for f, l in zip(line["file"], line["line"]):
                    if f not in covered_lines.keys():
                        covered_lines[f] = [l]
                    elif l not in covered_lines[f]:
                        covered_lines[f].append(l)

            ptr = instr_order[instr_jmps[ptr]["index"] + 1]
    except IndexError:
        print("Runs out of lines")
    except KeyError:
        global count_q
        count_q += 1


"""
load_instr_ptrs(filename:str) -> list
Läd die instr ptr aus der durch den Syzkaller erstellten Datei in eine Liste und gibt diese zurueck
filename: Speicherort der instr ptr Datei
"""
def load_instr_ptrs(filename):
    with open(filename, "r") as file:
        instr_ptrs = file.readlines()
    # dec ptr in hex ptr konvertieren
    instr_ptrs_n = [str(ptr[2:-1]) for ptr in instr_ptrs]
    return instr_ptrs_n


"""
load_instr(filename:str) -> nicht festgelegt (in diesem Fall list oder dict)
Läd alle mit pickle gespeicherten Python Objekte, also hier --instr-order, --instr-jmps und --instr-lines
filename: Speicherort der pickle Dateien
"""
def load_instr(filename):
    with open(filename, "rb") as handle:
        a = pickle.load(handle)
    return a


"""
save_covered_lines(covered_lines:dict, filename:str) -> None
Speichert die Zeilen, die als ueberdeckt ermittelt wurden
covered_lines: Enthaelt die aus den instr ptr bestimmten ueberdeckten Zeilen. Die instr ptr werden durch den Syzkaller bestimmt
filename: Speicherort der Datei der ueberdeckten Zeilen
"""
def save_covered_lines(covered_lines, filename):
    file_count, line_count = calc_file_and_line_count(covered_lines)
    result = "file_count:" + str(file_count) + " line_count:" + str(line_count) + "\n"
    for file in covered_lines:
        result += "file:" + file + "\n"
        for line in sorted([int(l) for l in covered_lines[file]]):
            result += str(line) + "\n"
        result += "\n"
    with open(filename, "w") as file:
        file.write(result)
    print("Saved all covered lines to", filename)


"""
load_all_lines_in_dict(filename:str) -> int, int, dict
Läd alle Zeilen aus filename in ein dict und ermittelt aus der ersten Zeilen Anzahl der ueberdeckbaren Dateien und Zeilen und gibt dies alles zurueck
filename: --all-coverable-lines Datei, die alle ueberdeckbaren Zeilen zb aus dem fs Subsystem enthält
"""
def load_all_lines_in_dict(filename):
    file_count, line_count, lines = load_all_lines(filename)
    all_lines = {}
    current_file = ""
    for line in lines:
        if line[:5] == "file:":
            current_file = line[5:]
            all_lines[current_file] = []
        elif line != "":
            all_lines[current_file].append(line)
    return file_count, line_count, all_lines


"""
load_all_lines(filename:str) -> int, int, list
Läd alle Zeilen aus filename und ermittelt aus der ersten Zeilen Anzahl der ueberdeckbaren Dateien und Zeilen und gibt dies alles zurueck
filename: --all-coverable-lines Datei, die alle ueberdeckbaren Zeilen zb aus dem fs Subsystem enthält
"""
def load_all_lines(filename):
    with open(filename, "r") as file:
        lines = file.readlines()
    lines = [l[:len(l) - 1] for l in lines]
    splitted_line = lines[0].split(" ")
    file_count = splitted_line[0].split(":")[1]
    line_count = splitted_line[1].split(":")[1]
    try:
        file_count = int(file_count)
        line_count = int(line_count)
    except ValueError:
        print("Bad file format of", filename)
        sys.exit(1)
    return file_count, line_count, lines[1:]


"""
calc_file_and_line_count(covered_lines:dict) -> int, int
Berechnet die ueberdeckten Zeilen aus den entsprechenden Dateien und gibt die Anzahl der Dateien und die Gesamtzahl der Zeilen zurueck
covered_lines: Enthaelt die aus den instr ptr bestimmten ueberdeckten Zeilen. Die instr ptr werden durch den Syzkaller bestimmt
"""
def calc_file_and_line_count(covered_lines):
    line_count = 0
    for file in covered_lines:
        line_count += len(covered_lines[file])
    return len(covered_lines), line_count


"""
compare_covered_lines_to_all_lines(covered_lines:dict, all_coverable_lines:dict) -> int, dict
Ermittelt alle Zeilen, die in der all_coverable_lines Datei ueberdeckt werden und zaehlt diese
covered_lines: Enthaelt die aus den instr ptr bestimmten ueberdeckten Zeilen. Die instr ptr werden durch den Syzkaller bestimmt.
all_lines: Enthaelt zb alle ueberdeckbaren Zeilen des fs Subsystems
"""
def compare_covered_lines_to_all_lines(covered_lines, all_coverable_lines):
    covered_lines_of_coverable = {}
    count_covered_lines = 0
    for file in covered_lines:
        if file in all_coverable_lines:
            comp_lines = all_coverable_lines[file]
            count_covered_lines += compare_covered_lines_of_file_to_all_lines_of_file(covered_lines, comp_lines, covered_lines_of_coverable, file)
    return count_covered_lines, covered_lines_of_coverable


"""
compare_covered_lines_of_file_to_all_lines_of_file(covered_lines:dict, comp_lines:list, covered_lines_of_coverable:dict, file:str) -> int
covered_lines: Enthaelt die aus den instr ptr bestimmten ueberdeckten Zeilen. Die instr ptr werden durch den Syzkaller bestimmt.
comp_lines: Liste von ueberdeckbaren Zeilen einer Datei, die von covered_lines ueberdeckt werden koennten
covered_lines_of_coverable: Bereits gefundende Ueberdeckungen von all_coverable_lines
file: Aktuell zu untersuchender Dateiname
"""
def compare_covered_lines_of_file_to_all_lines_of_file(covered_lines, comp_lines, covered_lines_of_coverable, file):
    count_covered_lines_file = 0
    for line in covered_lines[file]:
        if line in comp_lines:
            if file in covered_lines_of_coverable:
                covered_lines_of_coverable[file].append(line)
            else:
                covered_lines_of_coverable[file] = [line]
            count_covered_lines_file += 1
    return count_covered_lines_file


if __name__ == '__main__':
    main()
