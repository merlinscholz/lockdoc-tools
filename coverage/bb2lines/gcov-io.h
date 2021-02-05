#ifndef MODIFIED_GCOV_GCOV_IO_H
#define MODIFIED_GCOV_GCOV_IO_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
// #include "libiberty.h"

#define GCOV_TAG_FUNCTION	 ((unsigned)0x01000000)
#define GCOV_TAG_LINES		 ((unsigned)0x01450000)
/* File magic. Must not be palindromes.  */
#define GCOV_DATA_MAGIC ((unsigned)0x67636461) /* "gcda" */
#define GCOV_NOTE_MAGIC ((unsigned)0x67636e6f) /* "gcno" */
#define GCOV_TAG_COUNTER_BASE 	 ((unsigned)0x01a10000)
#define GCOV_COUNTER_FOR_TAG(TAG)					\
	((unsigned)(((TAG) - GCOV_TAG_COUNTER_BASE) >> 17))
/* Counters that are collected.  */
#define DEF_GCOV_COUNTER(COUNTER, NAME, MERGE_FN) COUNTER,
enum {
#include "gcov-counter.def"
	GCOV_COUNTERS
};
#undef DEF_GCOV_COUNTER
/* Check whether a tag is a counter tag.  */
#define GCOV_TAG_IS_COUNTER(TAG)				\
	(!((TAG) & 0xFFFF) && GCOV_COUNTER_FOR_TAG (TAG) < GCOV_COUNTERS)
/* The tag level mask has 1's in the position of the inner levels, &
   the lsb of the current level, and zero on the current and outer
   levels.  */
#define GCOV_TAG_MASK(TAG) (((TAG) - 1) ^ (TAG))
/* Return nonzero if SUB is an immediate subtag of TAG.  */
#define GCOV_TAG_IS_SUBTAG(TAG,SUB)				\
	(GCOV_TAG_MASK (TAG) >> 8 == GCOV_TAG_MASK (SUB) 	\
	 && !(((SUB) ^ (TAG)) & ~GCOV_TAG_MASK (TAG)))
/* Optimum number of gcov_unsigned_t's read from or written to disk.  */
#define GCOV_BLOCK_SIZE (1 << 10)
/* Convert a magic or version number to a 4 character string.  */
#define GCOV_UNSIGNED2STRING(ARRAY,VALUE)	\
  ((ARRAY)[0] = (char)((VALUE) >> 24),		\
   (ARRAY)[1] = (char)((VALUE) >> 16),		\
   (ARRAY)[2] = (char)((VALUE) >> 8),		\
   (ARRAY)[3] = (char)((VALUE) >> 0))
#define XRESIZEVAR(T, P, S)	((T *) xrealloc ((P), (S)))

int gcov_open (const char *name, int mode);
int gcov_close ();
int gcov_magic (unsigned magic, unsigned expected);
static void gcov_allocate (unsigned length);
unsigned gcov_read_unsigned ();
unsigned gcov_position ();
void gcov_sync (unsigned base, unsigned length);
int gcov_is_error ();
const char *gcov_read_string ();

#endif //MODIFIED_GCOV_GCOV_IO_H
