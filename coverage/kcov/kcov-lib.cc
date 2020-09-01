/*
 * Compile: g++  -Wall -fPIC -shared -o kcov.so kcov-lib.c -ldl
 * For debug output compile with -DDEBUG.
 * Usage: KCOV_OUT={stderr,progname,FILE} LD_PRELOAD=./kcov.so sleep 2
 * Determines basic block coverages, and outputs a list of unique and sorted bbs.
 * stderr: Writes to stderr. Same as empty KCOV_OUT or not set.
 * progname: Writes to PROGNAME.cov using basename(program_invocation_name).
 * FILE: Any arbitrary file name
 */
#include <dlfcn.h>
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
#include <iostream>
#include <string>
#include <set>
#include <cstdint>
#include <signal.h>
#include <sys/resource.h>
#include <sys/time.h>

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
#define KCOV_ENV_CTL_FD "KCOV_CTL_FD"
#define KCOV_ENV_ERR_FD "KCOV_ERR_FD"
#define KCOV_ENV_OUT_FD "KCOV_OUT_FD"
#define KCOV_ENV_OUT "KCOV_OUT"

static int kcov_fd, err_fd, out_fd, foreign_fd;
static cover_t *cover;
static std::set<cover_t> sortuniq_cover;
#ifdef WRITE_FILE
static char temp[MAX_PATH_NAME];
#endif
extern char *program_invocation_name;
struct sigaction new_action, old_action;
static void finish_kcov(void);

static void cleanup_kcov(int unmap) {
	if (ioctl(kcov_fd, KCOV_DISABLE, 0)) {
#ifdef DEBUG
		dprintf(err_fd, "%d: ioctl disable, %s\n", getpid(), strerror(errno));
#endif
	}
	if (unmap) {
		munmap(cover, COVER_SIZE * KCOV_ENTRY_SIZE);
	}
	close(kcov_fd);
	close(err_fd);
	if (out_fd > 0 && !foreign_fd) {
		close(out_fd);
	}
	foreign_fd = kcov_fd = err_fd = out_fd = 0;
	unsetenv(KCOV_ENV_CTL_FD);
	unsetenv(KCOV_ENV_ERR_FD);
	unsetenv(KCOV_ENV_OUT_FD);
}

static void signal_handler(int signum) {
	if (signum == SIGSEGV) {
#ifdef DEBUG
		dprintf(err_fd, "%d: Caught SIGSEV on '%s'\n", getpid(), basename(program_invocation_name));
#endif
		finish_kcov();
	}
	if (old_action.sa_handler) {
		old_action.sa_handler(signum);
	}
	exit(0);
}

static void __attribute__((constructor)) start_kcov(void) {
	char buff[30], *env;
	struct rlimit max_ofiles;
#ifdef WRITE_FILE
	const char *kcov_out = NULL;
#endif
#ifdef DEBUG
	fprintf(stderr, "%d: Tracing '%s'\n", getpid(), basename(program_invocation_name));
#endif
	if (kcov_fd > 0) {
		return;
	}
	new_action.sa_handler = signal_handler;
	sigemptyset (&new_action.sa_mask);
	new_action.sa_flags = 0;
	if (sigaction (SIGSEGV, &new_action, &old_action) == -1) {
#ifdef DEBUG
		dprintf(STDERR_FILENO, "%d: sigaction SIGTERM, %s\n", getpid(), strerror(errno));
#endif
	}

	/*
	 * For some reasons, getenv() does not work
	 * if bash is started for the first time,
	 * and after setenv() has been called.
	 * setenv() is used below to save the file descriptors.
	 * Any subsequent execution of bash works perfectly fine.
	 * We, therefore, look here for KCOV_OUT, and save a ptr to it.
	 */
	kcov_out = getenv(KCOV_ENV_OUT);
	env = getenv(KCOV_ENV_CTL_FD);
	if (env != NULL) {
#ifdef DEBUG
		fprintf(stderr, "start_kcov() called, but found active KCOV. Possible exec*() detected.\n");
#endif
		kcov_fd = strtol(env, NULL, 10);
		if (fcntl(kcov_fd, F_GETFD)) {
#ifdef DEBUG
			dprintf(STDERR_FILENO, "%d: fcntl kcov_fd, %s\n", getpid(), strerror(errno));
#endif
			kcov_fd = err_fd = out_fd = -1;
			return;
		}
		env = getenv(KCOV_ENV_ERR_FD);
		err_fd = strtol(env, NULL, 10);
		if (fcntl(err_fd, F_GETFD)) {
#ifdef DEBUG
			dprintf(STDERR_FILENO, "%d: fcntl err_fd, %s\n", getpid(), strerror(errno));
#endif
			kcov_fd = err_fd = out_fd = -1;
			return;
		}
		env = getenv(KCOV_ENV_OUT_FD);
		if (env != NULL) {
			out_fd = strtol(env, NULL, 10);
			if (fcntl(out_fd, F_GETFD)) {
#ifdef DEBUG
				dprintf(STDERR_FILENO, "%d: fcntl out_fd, %s\n", getpid(), strerror(errno));
#endif
				kcov_fd = err_fd = out_fd = -1;
				return;
			}
		}
#ifdef DEBUG
		dprintf(err_fd, "%d: Active KCOV disabled. Restarting...\n", getpid());
#endif
#if 1
		cleanup_kcov(0);
#else
		cover = (cover_t*)mmap(NULL, COVER_SIZE * KCOV_ENTRY_SIZE,
				     PROT_READ | PROT_WRITE, MAP_SHARED, kcov_fd, 0);
		if ((void*)cover == MAP_FAILED) {
#ifdef DEBUG
			dprintf(STDERR_FILENO, "%d: mmap re-init");
#endif
			cleanup_kcov(0);
		}
		return;
#endif
	}
	/*
	 * Duplicate the fd for stderr
	 * Some programs such as cat or touch close stderr
	 * before we reach finish_kcov().
	 * This way stderr remains open until we have dumped all pcs.
	 */
	err_fd = dup(STDERR_FILENO);
	if (err_fd == -1) {
#ifdef DEBUG
		dprintf(STDERR_FILENO, "%d: dup, %s\n", getpid(), strerror(errno));
#endif
	}
	kcov_fd = open(KCOV_PATH, O_RDWR);
	if (kcov_fd == -1) {
#ifdef DEBUG
		dprintf(err_fd, "%d: open, %s\n", getpid(), strerror(errno));
#endif
		return;
	}
	if (ioctl(kcov_fd, KCOV_INIT_TRACE, COVER_SIZE)) {
#ifdef DEBUG
		dprintf(err_fd, "%d: ioctl init, %s\n", getpid(), strerror(errno));
#endif
		close(kcov_fd);
		kcov_fd = -1;
		return;
	}
	cover = (cover_t*)mmap(NULL, COVER_SIZE * KCOV_ENTRY_SIZE,
			     PROT_READ | PROT_WRITE, MAP_SHARED, kcov_fd, 0);
	if ((void*)cover == MAP_FAILED) {
#ifdef DEBUG
		dprintf(err_fd, "%d: mmap, %s\n", getpid(), strerror(errno));
#endif
		close(kcov_fd);
		kcov_fd = -1;
		return;
	}
	if (ioctl(kcov_fd, KCOV_ENABLE, KCOV_TRACE_PC)){
#ifdef DEBUG
		dprintf(err_fd, "%d: ioctl enable, %s\n", getpid(), strerror(errno));
#endif
		munmap(cover, COVER_SIZE * KCOV_ENTRY_SIZE);
		close(kcov_fd);
		kcov_fd = -1;
		return;
	}
	snprintf(buff, sizeof(buff), "%d", kcov_fd);
	setenv(KCOV_ENV_CTL_FD, buff, 1);
	snprintf(buff, sizeof(buff), "%d", err_fd);
	setenv(KCOV_ENV_ERR_FD, buff, 1);
#ifdef DEBUG
	dprintf(err_fd, "%d: Wrote settings to env variables: kcov_fd = %s, err_fd = %s\n", 
		getpid(), getenv(KCOV_ENV_CTL_FD), getenv(KCOV_ENV_ERR_FD));
#endif
	__atomic_store_n(&cover[0], 0, __ATOMIC_RELAXED);

	if (atexit(finish_kcov)) {
#ifdef DEBUG
		dprintf(err_fd, "error atexit()\n");
#endif
	}

#ifdef WRITE_FILE
	if (!kcov_out) {
		return;
	} else if (strcmp(kcov_out, "stderr") == 0) {
		return;
	} else if (strcmp(kcov_out, "fd") == 0) {
		if (getrlimit(RLIMIT_NOFILE, &max_ofiles) == -1 ) {
#ifdef DEBUG
			dprintf(err_fd, "%d: getrlimit, %s\n", getpid(), strerror(errno));
#endif
		}
		out_fd = max_ofiles.rlim_cur - 1;
#ifdef DEBUG
		dprintf(err_fd, "%d: Someone provided use with an FD (soft-limit(max_open_files) - 1): %d\n", getpid(), out_fd);
#endif
		if (fcntl(out_fd, F_GETFD)) {
#ifdef DEBUG
			dprintf(err_fd, "%d: fcntl out_fd start, %s\n", getpid(), strerror(errno));
#endif
			cleanup_kcov(1);
			return;
		}
		foreign_fd = 1;
		return;
	} else if (strcmp(kcov_out, "progname") == 0) {
		snprintf(temp, MAX_PATH_NAME, "%s.map", basename(program_invocation_name));
		kcov_out = temp;
	}
	out_fd = open(kcov_out, O_RDWR | O_CREAT | O_APPEND, 0755);
#ifdef DEBUG
	if (out_fd == -1) {
		dprintf(err_fd, "%d: open out_fd, %s\n", getpid(), strerror(errno));
		dprintf(err_fd, "%d: Cannot open '%s'. Falling back to stderr \n", getpid(), kcov_out);
	} else {
		dprintf(err_fd, "%d: Writing bbs to %s (%d)\n", getpid(), kcov_out, out_fd);
	}
#endif
	snprintf(buff, sizeof(buff), "%d", out_fd);
	setenv(KCOV_ENV_OUT_FD, buff, 1);
#ifdef DEBUG
	dprintf(err_fd, "%d: Wrote setting to env variable: out_fd = %s\n", getpid(), getenv(KCOV_ENV_OUT_FD));
#endif
#endif
}

static void __attribute__((destructor)) finish_kcov(void) {
	cover_t  n, i;
	int fd;

	if (kcov_fd < 1 || err_fd < 1) {
#ifdef DEBUG
		dprintf(err_fd, "%d: No KCOV fd or no fd for stderr present\n", getpid());
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
	dprintf(err_fd, "%d: out_fd = %d\n", getpid(), out_fd);
#endif

	n = __atomic_load_n(&cover[0], __ATOMIC_RELAXED);
#if 1
	for (i = 0; i < n; i++) {
		sortuniq_cover.insert(cover[i + 1]);
	}
	for (auto elem : sortuniq_cover) {
		dprintf(fd, "0x%jx\n", (uintmax_t)elem);
	}
#endif
#ifdef DEBUG
	dprintf(err_fd, "%d: Done dumping %jd unique bbs of %ju/%ju recorded bbs for '%s'(%d), isatty(err_fd = %d) = %d, isatty(out_fd = %d) = %d\n",
		getpid(),
		(uintmax_t)sortuniq_cover.size(), (uintmax_t)n, (uintmax_t)COVER_SIZE,
		program_invocation_name, getpid(), 
		err_fd, isatty(err_fd),
		out_fd, isatty(out_fd));
#endif
	cleanup_kcov(1);
}

typedef pid_t (*fork_fn_ptr)(void);
extern "C" pid_t fork(void) {
	fork_fn_ptr old_fork;
	pid_t ret;

	old_fork = (fork_fn_ptr)dlsym(RTLD_NEXT, "fork");
	ret = old_fork();
	if (ret != 0) {
		return ret;
	}
	if (kcov_fd > 0 ) {
#ifdef DEBUG
		dprintf(err_fd, "%d: fork() on the traced process '%s'(%d). Disabling KCOV for child %d.\n",
			getpid(),
			basename(program_invocation_name), getppid(), getpid());
#endif
		cleanup_kcov(1);
#ifdef DEBUG
		dprintf(err_fd, "%d: Re-enabling KCOV for child %d.\n", getpid(), getpid());
#endif
		start_kcov();
	}
	return ret;
}
