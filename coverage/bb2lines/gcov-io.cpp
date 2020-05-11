#include "gcov-io.h"

struct gcov_var
{
	FILE *file;
	unsigned start;	/* Position of first byte of block */
	unsigned offset;		/* Read/write position within the block.  */
	unsigned length;		/* Read limit in the block.  */
	unsigned overread;		/* Number of words overread.  */
	int error;			/* < 0 overflow, > 0 disk error.  */
	int mode;	                /* < 0 writing, > 0 reading */
	int endian;			/* Swap endianness.  */
	/* Holds a variable length block, as the compiler can write
	   strings and needs to backtrack.  */
	size_t alloc;
	unsigned *buffer;
} gcov_var;

void oom_error (const char *function, size_t size)
{
	printf ("%s: unable to allocate %zu bytes: %m\n", function, size);
	exit (1);
}

void *xrealloc (void *p, size_t n)
{
	void *result = realloc (p, n);
	if (result == NULL && (n > 0 || p == NULL))
		oom_error ("realloc", n);
	return result;
}

int gcov_open (const char *name, int mode)
{
	gcov_var.start = 0;
	gcov_var.offset = gcov_var.length = 0;
	gcov_var.overread = -1u;
	gcov_var.error = 0;
	gcov_var.endian = 0;

	if (mode >= 0)
		/* Open an existing file.  */
		gcov_var.file = fopen (name, (mode > 0) ? "rb" : "r+b");

	if (gcov_var.file)
		mode = 1;
	else if (mode <= 0)
		/* Create a new file.  */
		gcov_var.file = fopen (name, "w+b");

	if (!gcov_var.file)
		return 0;

	gcov_var.mode = mode ? mode : 1;

	setbuf (gcov_var.file, (char *)0);

	return 1;
}

int gcov_close (void)
{
	if (gcov_var.file)
	{
		fclose (gcov_var.file);
		gcov_var.file = 0;
		gcov_var.length = 0;
	}
	free (gcov_var.buffer);
	gcov_var.alloc = 0;
	gcov_var.buffer = 0;
	gcov_var.mode = 0;
	return gcov_var.error;
}

int gcov_magic (unsigned magic, unsigned expected)
{
	if (magic == expected)
		return 1;
	magic = (magic >> 16) | (magic << 16);
	magic = ((magic & 0xff00ff) << 8) | ((magic >> 8) & 0xff00ff);
	if (magic == expected)
	{
		gcov_var.endian = 1;
		return -1;
	}
	return 0;
}

static void gcov_allocate (unsigned length)
{
	size_t new_size = gcov_var.alloc;

	if (!new_size)
		new_size = GCOV_BLOCK_SIZE;
	new_size += length;
	new_size *= 2;

	gcov_var.alloc = new_size;
	gcov_var.buffer = XRESIZEVAR (unsigned, gcov_var.buffer, new_size << 2);
}

static inline unsigned from_file (unsigned value)
{
	if (gcov_var.endian)
	{
		value = (value >> 16) | (value << 16);
		value = ((value & 0xff00ff) << 8) | ((value >> 8) & 0xff00ff);
	}
	return value;
}

/* Return a pointer to read BYTES bytes from the gcov file. Returns
   NULL on failure (read past EOF).  */
static const unsigned *gcov_read_words (unsigned words)
{
	const unsigned *result;
	unsigned excess = gcov_var.length - gcov_var.offset;

	if (gcov_var.mode <= 0)
		return NULL;

	if (excess < words)
	{
		gcov_var.start += gcov_var.offset;
		if (excess)
		{
			memmove (gcov_var.buffer, gcov_var.buffer + gcov_var.offset,
					 excess * 4);
		}
		gcov_var.offset = 0;
		gcov_var.length = excess;

		if (gcov_var.length + words > gcov_var.alloc)
			gcov_allocate (gcov_var.length + words);
		excess = gcov_var.alloc - gcov_var.length;

		excess = fread (gcov_var.buffer + gcov_var.length,
						1, excess << 2, gcov_var.file) >> 2;
		gcov_var.length += excess;
		if (gcov_var.length < words)
		{
			gcov_var.overread += words - gcov_var.length;
			gcov_var.length = 0;
			return 0;
		}
	}
	result = &gcov_var.buffer[gcov_var.offset];
	gcov_var.offset += words;
	return result;
}

/* Read unsigned value from a coverage file. Sets error flag on file
   error, overflow flag on overflow */

unsigned gcov_read_unsigned (void)
{
	unsigned value;
	const unsigned *buffer = gcov_read_words (1);

	if (!buffer)
		return 0;
	value = from_file (buffer[0]);
	return value;
}

unsigned gcov_position (void)
{
	return gcov_var.start + gcov_var.offset;
}

void gcov_sync (unsigned base, unsigned length)
{
	base += length;
	if (base - gcov_var.start <= gcov_var.length)
		gcov_var.offset = base - gcov_var.start;
	else
	{
		gcov_var.offset = gcov_var.length = 0;
		fseek (gcov_var.file, base << 2, SEEK_SET);
		gcov_var.start = ftell (gcov_var.file) >> 2;
	}
}

int gcov_is_error (void)
{
	return gcov_var.file ? gcov_var.error : 1;
}

const char *gcov_read_string (void)
{
	unsigned length = gcov_read_unsigned ();
	if (!length)
		return 0;

	return (const char *) gcov_read_words (length);
}