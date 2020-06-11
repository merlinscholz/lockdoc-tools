import sys
import argparse
import time
import pickle


# Enthaelt die zu beruecksichtigende Sprungbefehle
jmp_instr = ["jmp", "je", "jne", "jg", "jge", "ja", "jae", "jl", "jle",
             "jb", "jbe", "jo", "jno", "jz", "jnz", "js", "jns", "call",
             "loop", "loopcc", "loope", "loopne", "loopnz", "loopz", "ret", "iret"]

# Enthaelt alle zu entfernende nop
nop = ["nop"] + [x+"nop"+y for x in ["b", "w", "l", "q"] for y in ["b", "w", "l", "q"]]\
            + [x+"nop" for x in ["b", "w", "l", "q"]] \
            + ["nop"+y for y in ["b", "w", "l", "q"]]

# Erweitert die Sprungbefehle
jmp_instr = jmp_instr + [x+instr+y for instr in jmp_instr for x in ["b", "w", "l", "q"] for y in ["b", "w", "l", "q"]]\
            + [x+instr for instr in jmp_instr for x in ["b", "w", "l", "q"]] \
            + [instr+y for instr in jmp_instr for y in ["b", "w", "l", "q"]]


def main():
    argv = sys.argv[1:]
    parser = argparse.ArgumentParser(prog='PROG')
    # objdump wird durch "objdump -dj .text" erzeugt
    parser.add_argument('--objdump', help="objdump is generated with $objdump -dj .text", required=True)
    # Enthält die instr-ptrs des linux kernel in korrekter Reihenfolge als Liste
    parser.add_argument('--output-instr-order', help="Writes all instr ptr of the linux kernel to the output-instr-order file in the correct order", required=True)
    # Enthält die instr-ptrs des linux kernel mit einem Boolean, ob ein Sprungbefehl vorliegt oder nicht, in einem Dictionary
    parser.add_argument('--output-instr-jmps', help="Writes all instr ptr with a jump flag to the output-instr-jmps file", required=True)
    parse_out = parser.parse_args(argv)

    tstart = time.time()
    instr_order, instr_jmps = analyse_objdump(parse_out.objdump)
    save_instr(instr_order, parse_out.output_instr_order)
    save_instr(instr_jmps, parse_out.output_instr_jmps)
    print(time.time() - tstart, "seconds after start")


"""
analyse_objdump(linux_objdump_filename:str) -> dict, dict
Ermittelt die instr ptr und ob bei diesen ein Sprungbefehl vorliegt
linux_objdump_filename: Speicherort der mit "objdump -dj .text" erzeugten Datei
"""
def analyse_objdump(linux_objdump_filename):
    instructions_jmps = {}
    instructions_order = []
    with open(linux_objdump_filename) as f:
        lines = f.readlines()
    lines = [l[:len(l) - 1] for l in lines if l[:len(l) - 1] != ""]

    count_useless_lines = 0
    count_nop = 0
    for count, line in enumerate(lines):
        if check_if_line_is_instruction(line):
            instr = extract_instruction(line)
            if instr not in nop:
                instr_count = get_instr_count(line)
                instructions_jmps[instr_count] = {"is_jump": check_line_for_jmp(instr),
                                                  "index": len(instructions_order)}
                instructions_order.append(instr_count)
            else:
                count_nop += 1
        else:
            count_useless_lines += 1
        if count % 100000 == 0:
            print(str(count) + "/" + str(len(lines)), "\t", str(round(float(count)/len(lines)*100)) + "%")

    print("useless_instr:", count_useless_lines, "count of nop:", count_nop)

    return instructions_order, instructions_jmps


"""
get_instr_count(line:str) -> str
Ermittelt den instr ptr einer Assembler-Zeile
line: Eine Zeile im Assembler-Code
"""
def get_instr_count(line):
    return line[:8]


"""
check_if_line_is_instruction(line:str) -> bool
Ueberprueft ob ein Befehl in der Assembler-Zeile enthalten ist
line: Eine Zeile im Assembler-Code
"""
def check_if_line_is_instruction(line):
    if len(line) > 32:
        return line[8] == ":"
    return False


"""
check_line_for_jmp(instr:str) -> bool
Ueberprueft ob ein Sprungbefehl vorliegt
instr: Befehl aus einer Assembler-Zeile
"""
def check_line_for_jmp(instr):
    return instr in jmp_instr


"""
extract_instruction(line:str) -> str
Ermittelt den Befehl einer Assembler-Zeile
line: Eine Zeile im Assembler-Code
"""
def extract_instruction(line):
    splitted = line[32:].split(" ")
    splitted = [x for x in splitted if x is not ""]
    if len(splitted) > 0:
        return splitted[0]
    else:
        print(line)
        return ""


"""
save_instr(instr_lines:dict, filename:str) -> None
Speichert die instr_lines in eine Pickle-Datei
instr_lines: Enthaelt in Abhaengigkeit zu einem instr ptr alle Namen der ueberdeckbare Dateien und Zeilen des Linux-Kernels in der Form von zb {"ffffffff81545de0": {"file": ["/include/linux/file.h", "/fs/open.c"], "line": [60, 165]}}
filename: Speicherort der Pickle-Datei
"""
def save_instr(instr_lines, filename):
    with open(filename, "wb") as handle:
        pickle.dump(instr_lines, handle, protocol=pickle.HIGHEST_PROTOCOL)


if __name__ == "__main__":
    main()
