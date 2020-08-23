/*
 * Compile: gcc  -Wall -fPIC -shared -o kcov.so kcov-lib.c
 * For debug output compile with -DDEBUG.
 * Usage: KCOV_OUT={stderr,progname,FILE} LD_PRELOAD=./kcov.so sleep 2
 * stderr: Writes bbs to stderr. Same as empty KCOV_OUT or not set.
 * progname: Writes bbs to PROGNAME.cov using basename(program_invocation_name).
 * FILE: Any arbitrary file name
 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <libgen.h>
#include <string.h>

//#define DEBUG
#define WRITE_FILE

#ifdef KERNEL64
#define KCOV_INIT_TRACE _IOR('c', 1, uint64_t)
#define KCOV_ENTRY_SIZE sizeof(uint64_t)
typedef uint64_t cover_t;
#else
#define KCOV_INIT_TRACE _IOR('c', 1, uint32_t)
#define KCOV_ENTRY_SIZE sizeof(uint32_t)
typedef uint32_t cover_t;
#endif
#define KCOV_ENABLE _IO('c', 100)
#define KCOV_DISABLE _IO('c', 101)
#define KCOV_PATH "/sys/kernel/debug/kcov"
#define KCOV_TRACE_PC 0
#define COVER_SIZE (16 << 20)
#define MAX_PATH_NAME 100

static int kcov_fd, err_fd, out_fd;
static cover_t *cover;
#ifdef WRITE_FILE
static char temp[MAX_PATH_NAME];
#endif
extern char *program_invocation_name;

static void __attribute__((constructor)) start_kcov(void) {
#ifdef WRITE_FILE
	const char *kcov_out = NULL;
#endif
#ifdef DEBUG
	fprintf(stderr, "Tracing '%s'(%d)\n", basename(program_invocation_name), getpid());
#endif
	/*
	 * Duplicate the fd for stderr
	 * Some programs such as cat or touch close stderr
	 * before we reach finish_kcov().
	 * This way stderr remains open until we have dumped all pcs.
	 */
	err_fd = dup(STDERR_FILENO);
	if (err_fd == -1) {
#ifdef DEBUG
		perror("dup");
#endif
		return;
	}
	kcov_fd = open(KCOV_PATH, O_RDWR);
	if (kcov_fd == -1) {
#ifdef DEBUG
		perror("open");
#endif
		return;
	}
	if (ioctl(kcov_fd, KCOV_INIT_TRACE, COVER_SIZE)) {
#ifdef DEBUG
		perror("ioctl init");
#endif
		close(kcov_fd);
		kcov_fd = -1;
		return;
	}
	cover = (cover_t*)mmap(NULL, COVER_SIZE * KCOV_ENTRY_SIZE,
				     PROT_READ | PROT_WRITE, MAP_SHARED, kcov_fd, 0);
	if ((void*)cover == MAP_FAILED) {
#ifdef DEBUG
		perror("mmap");
#endif
		close(kcov_fd);
		kcov_fd = -1;
		return;
	}
	if (ioctl(kcov_fd, KCOV_ENABLE, KCOV_TRACE_PC)){
#ifdef DEBUG
		perror("ioctl enable");
#endif
		munmap(cover, COVER_SIZE * KCOV_ENTRY_SIZE);
		close(kcov_fd);
		kcov_fd = -1;
		return;
	}
	__atomic_store_n(&cover[0], 0, __ATOMIC_RELAXED);

#ifdef WRITE_FILE
	kcov_out = getenv("KCOV_OUT");
	if (!kcov_out) {
		return;
	} else if (strcmp(kcov_out, "stderr") == 0) {
		return;
	} else if (strcmp(kcov_out, "progname") == 0) {
		snprintf(temp, MAX_PATH_NAME, "%s.cov", basename(program_invocation_name));
		kcov_out = temp;
	}
	out_fd = open(kcov_out, O_RDWR | O_CREAT | O_TRUNC, 0755);
#ifdef DEBUG
	if (out_fd == -1) {
		perror("open out_fd");
		fprintf(stderr, "Cannot open '%s'. Falling back to stderr \n", kcov_out);
	} else {
		fprintf(stderr, "Writing bbs to %s (%d)\n", kcov_out, out_fd);
	}
#endif
#endif
}

static void __attribute__((destructor)) finish_kcov(void) {
	cover_t  n, i;
	int fd;

	if (kcov_fd < 1 || err_fd < 1) {
#ifdef DEBUG
		fprintf(stderr, "No KCOV fd or no fd for stderr present\n");
#endif
		return;
	}
	// Open output file wins over stderr
	if (out_fd > 0) {
		fd = out_fd;
	} else {
		fd = err_fd;
	}
#ifdef DEBUG
	dprintf(err_fd, "out_fd = %d\n", out_fd);
#endif

	n = __atomic_load_n(&cover[0], __ATOMIC_RELAXED);
#if 1
	for (i = 0; i < n; i++) {
		dprintf(fd, "0x%jx\n", (uintmax_t)cover[i + 1]);
	}
#endif
#ifdef DEBUG
	dprintf(err_fd, "Done dumping bbs for '%s'(%d), isatty(err_fd = %d) = %d, isatty(out_fd = %d) = %d\n",
		program_invocation_name, getpid(), 
		err_fd, isatty(err_fd),
		out_fd, isatty(out_fd));
#endif

	munmap(cover, COVER_SIZE * KCOV_ENTRY_SIZE);
	close(kcov_fd);
	close(err_fd);
	if (out_fd > 0) {
		close(out_fd);
	}
}
