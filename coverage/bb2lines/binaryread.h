#ifndef MODIFIED_GCOV_BINARYREAD_H
#define MODIFIED_GCOV_BINARYREAD_H

#include <iostream>
#include <string.h>
#include <bfd.h>

static bfd *kernelBfd;
/**
 * Passes context information to find_address_in_section
 * which resolves a instruction pointer to a code location.
 * @pc: The instruction pointer
 * @found: Indicates that the locations has already been found.
 * @file: Source file
 * @fn: Function name
 * @line: Line number in @file
 */
struct BfdSearchCtx {
	bfd_vma pc;
	bfd_boolean found;
	const char *file;
	const char *fn;
	unsigned int line;
};

/**
 * Symbol table used by bfd
 */
static asymbol **bfdSyms;

/**
* Size of symbol table @bfdSyms
*/
static long bfdSymcount;

int binaryread_init(const char *filename);
BfdSearchCtx addr_to_line(uint64_t addr);

#endif //MODIFIED_GCOV_BINARYREAD_H
