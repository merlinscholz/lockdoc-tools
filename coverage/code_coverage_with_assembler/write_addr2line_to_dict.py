import sys
import argparse
import pickle
import subprocess
import time
import os


# Groesse der Liste von instr ptr, die von addr2line auf einmal verarbeitet werden sollen
slices = 50000


def main():
    argv = sys.argv[1:]
    parser = argparse.ArgumentParser(prog='write_addr2line_to_dict')
    parser.add_argument('--linux-kernel', help="Path to the linux-kernel", required=True)
    # instr_order wird durch write_objdump_to_dict.py erzeugt
    parser.add_argument('--instr-order', help="instr_order is generated with write_objdump_to_dict.py", required=True)
    parser.add_argument('--output-instr-lines', help="Writes all instr-lines in the output-instr-lines file", required=True)
    parse_out = parser.parse_args(argv)

    instr_order = load_instr(parse_out.instr_order)
    print("instr_order loaded")
    write_addr2line_to_dict(instr_order, parse_out.linux_kernel, parse_out.output_instr_lines)


"""
write_addr2line_to_dict(instr_order:list, linux_kernel:str, output_instr_lines:str) -> None
Löst alle instr ptrs des Linux-Kernels zu den Dateinamen und Zeilennummern auf
instr_order: Enthaelt alle instr ptr des Linux-Kernels (ausser unrelevante instr ptr) in korrekter Reihenfolge der Ausführung
linux_kernel: Speicherort des Linux-Kernels
output_instr_lines: Speicherziel der instr_lines
"""
def write_addr2line_to_dict(instr_order, linux_kernel, output_instr_lines):
    print("instr_order loaded with", len(instr_order), "instructions")
    linux_dirname = os.path.dirname(linux_kernel)
    count = int(len(instr_order) / slices)
    lines = []
    t_start = time.time()
    # Aufteilen der instr ptrs in Teillisten, da addr2line maximal 99999 instr ptrs auf einmal verarbeiten kann
    # Trotzdem soll eine Möglichst grosse Anzahl an instr ptrs gleichzeitig verarbeitet werden, da ein haeufiger Aufruf von addr2line sonst sehr langsam ist
    for i in range(count):
        lines += addr_to_lines(instr_order[i * slices:(i + 1) * slices], linux_kernel)
        print(i + 1, "of", count, "\t", str(round((i+1)/count*100)) + "%")
    if len(instr_order[count * slices:]) > 0:
        lines += addr_to_lines(instr_order[count * slices:], linux_kernel)
        print(count, "of", count, "\t", "100%")
    instr_lines = {}
    print("Convert to dict")
    for index in range(len(lines)):
        file_names = []
        file_lines = []
        for line in lines[index]:
            splitted_line = line.split(":")
            # entferne /./ aus dem Dateinamen und entferne den dirname des Speicherortes des linux kernels
            line_without_points = "".join(["/" + x for x in splitted_line[0][len(linux_dirname):].split("/") if x != "." and x != ""])
            file_names.append(line_without_points)
            file_lines.append(splitted_line[1])
        instr_ptr = instr_order[index]
        instr_lines[instr_ptr] = {"file": file_names, "line": file_lines}
    print(len(instr_lines))
    save_instr(instr_lines, output_instr_lines)
    print(time.time() - t_start, "seconds after start")


"""
addr_to_lines(instr_ptrs:list, linux_kernel:str) -> list
Löst instr ptrs mit addr2line zu Dateinamen und Zeilennummer auf und gibt diese zurueck
instr_ptrs: instr ptrs die in Zeile und Dateiname konvertiert werden sollen
linux_kernel: Speicherort des Linux-Kernels
"""
def addr_to_lines(instr_ptrs, linux_kernel):
    lines = subprocess.check_output(["addr2line", "-p", "-i", "-C", "-e", linux_kernel] + instr_ptrs).decode("utf-8").split("\n")
    result = []
    elem = []
    for line in lines:
        if line[:14] != " (inlined by) ":
            if elem != []:
                result.append(elem)
            elem = [line.split(" ")[0]]
        else:
            elem.append(line[14:].split(" ")[0])
    return result


"""
save_instr(instr_lines:dict, filename:str) -> None
Speichert die instr_lines in eine Pickle-Datei
instr_lines: Enthaelt in Abhaengigkeit zu einem instr ptr alle Namen der ueberdeckbare Dateien und Zeilen des Linux-Kernels in der Form von zb {"ffffffff81545de0": {"file": ["/include/linux/file.h", "/fs/open.c"], "line": [60, 165]}}
filename: Speicherort der Pickle-Datei
"""
def save_instr(instr_lines, filename):
    with open(filename, "wb") as handle:
        pickle.dump(instr_lines, handle, protocol=pickle.HIGHEST_PROTOCOL)


"""
load_instr(filename:str) -> nicht festgelegt (in diesem Fall list oder dict)
Läd alle mit pickle gespeicherten Python Objekte, also hier --instr-order, --instr-jmps und --instr-lines
filename: Speicherort der Pickle Dateien
"""
def load_instr(filename):
    with open(filename, "rb") as handle:
        a = pickle.load(handle)
    return a


if __name__ == '__main__':
    main()
