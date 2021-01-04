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
#ifdef __FreeBSD__
#include <sys/user.h>
#include <libutil.h>
#endif

//#define DEBUG
#define WRITE_FILE

#ifdef __FreeBSD__
#include <sys/kcov.h>
#define KCOV_PATH "/dev/kcov"
#define KIOGETBUFSIZE   _IOR('c', 5, sizeof(int)) /* Get the buffer size */
#define KCOV_MODE_TRACE_PC_UNIQUE      2
typedef uint64_t kernel_long;
#define BITS_PER_LONG 64
#else
#ifdef KERNEL32
typedef uint32_t kernel_long;
#define BITS_PER_LONG 32
#else
typedef uint64_t kernel_long;
#define BITS_PER_LONG 64
#endif
#define KCOV_ENABLE _IO('c', 100)
#define KCOV_DISABLE _IO('c', 101)
#define KCOV_INIT_TRACE _IOR('c', 1, kernel_long)
#define KCOV_INIT_TRACE_UNIQUE          _IOR('c', 2, kernel_long)
#define KCOV_ENTRY_SIZE sizeof(kernel_long)
#define KCOV_PATH "/sys/kernel/debug/kcov"
#endif
#define COVER_SIZE (16 << 20)
#define MAX_PATH_NAME 100
#define KCOV_ENV_CTL_FD "KCOV_CTL_FD"
#define KCOV_ENV_ERR_FD "KCOV_ERR_FD"
#define KCOV_ENV_OUT_FD "KCOV_OUT_FD"
#define KCOV_ENV_OUT "KCOV_OUT"
#define KCOV_ENV_MODE "KCOV_MODE"
#define BASE_ADDRESS 0xffffffff81000000

enum kcov_mode_t {
	KCOV_TRACE_PC = 0,
	KCOV_TRACE_UNIQUE_PC = 2,
};

static kcov_mode_t kcov_mode = KCOV_TRACE_UNIQUE_PC;
static int kcov_fd, err_fd, out_fd, foreign_fd;
static std::set<kernel_long> sortuniq_cover;
static kernel_long *area, area_size, pcs_size;
static char temp[MAX_PATH_NAME];
static const char *kcov_mode_env = NULL;
struct sigaction new_sa, old_sa[2];
static void finish_kcov(void);

#ifdef __FreeBSD__
static void print_program_name(void) {
	struct kinfo_proc *proc = kinfo_getproc(getpid());
	if (proc) {
		dprintf(err_fd, "%s", proc->ki_comm);
		free(proc);
	} else {
		dprintf(err_fd, "unknown");
	}
}
#else
extern char *program_invocation_name;
static void print_program_name(void) {
	dprintf(err_fd, "%s", basename(program_invocation_name));
}
#endif

static void cleanup_kcov(int unmap) {
#ifdef __FreeBSD__
	if (ioctl(kcov_fd, KIODISABLE, 0)) {
#else
	if (ioctl(kcov_fd, KCOV_DISABLE, 0)) {
#endif
#ifdef DEBUG
		dprintf(err_fd, "%d: ioctl disable in %s: %s\n", getpid(), __func__, strerror(errno));
#endif
	}
	if (unmap) {
		munmap(area, area_size);
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
		dprintf(err_fd, "%d: Caught SIGSEV on '", getpid());
		print_program_name();
		dprintf(err_fd, "'\n");
#endif
		finish_kcov();
		if (old_sa[0].sa_handler) {
			old_sa[0].sa_handler(signum);
		}
		exit(1);
	} else if (signum == SIGTERM) {
#ifdef DEBUG
		dprintf(err_fd, "%d: Caught SIGTERM on '", getpid());
		print_program_name();
		dprintf(err_fd, "'\n");
#endif
		finish_kcov();
		if (old_sa[1].sa_handler) {
			old_sa[1].sa_handler(signum);
		}
		raise(SIGTERM);
	}
}

static void parse_env(void) {
	char *env;
	struct rlimit max_ofiles;
	const char *kcov_out = NULL;
	char buff[30];
	/*
	 * For some reasons, getenv() does not work
	 * if bash is started for the first time,
	 * and after setenv() has been called.
	 * setenv() is used below to save the file descriptors.
	 * Any subsequent execution of bash works perfectly fine.
	 * We, therefore, look here for KCOV_OUT, and save a ptr to it.
	 */
	kcov_out = getenv(KCOV_ENV_OUT);
	kcov_mode_env = getenv(KCOV_ENV_MODE);
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
		cleanup_kcov(0);
	}

	if (!kcov_out) {
		return;
	} else if (strcmp(kcov_out, "stderr") == 0) {
		return;
	} else if (strncmp(kcov_out, "fd", 2) == 0) {
		if (sscanf(kcov_out, "fd:%d", &out_fd) < 0) {
#ifdef DEBUG
			dprintf(err_fd, "%d: sscanf: cannot parse fd from: '%s', falling back to the getrlimit() method\n", getpid(), kcov_out);
#endif
			if (getrlimit(RLIMIT_NOFILE, &max_ofiles) == -1 ) {
				dprintf(err_fd, "%d: getrlimit, %s\n", getpid(), strerror(errno));
				cleanup_kcov(1);
				return;
			}
			out_fd = max_ofiles.rlim_cur - 1;
#ifdef DEBUG
			dprintf(err_fd, "%d: Someone provided use with an FD (soft-limit(max_open_files) - 1): %d\n", getpid(), out_fd);
#endif
		} else {
#ifdef DEBUG
			dprintf(err_fd, "%d: Someone provided use with an FD: %d\n", getpid(), out_fd);
#endif
		}
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
#ifdef __FreeBSD__
		struct kinfo_proc *proc = kinfo_getproc(getpid());
		snprintf(temp, MAX_PATH_NAME, "%s.map", proc->ki_comm);
		free(proc);
#else
		snprintf(temp, MAX_PATH_NAME, "%s.map", basename(program_invocation_name));
#endif
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
}

static void setup_signal_handler(void) {
	struct sigaction temp_sa;
	new_sa.sa_handler = signal_handler;
	sigemptyset (&new_sa.sa_mask);
	new_sa.sa_flags = 0;
	// Signal handler already present for SIGSEGV? (aka fork detection)
	if (sigaction(SIGSEGV, NULL, &temp_sa) == -1) {
#ifdef DEBUG
		dprintf(STDERR_FILENO, "%d: sigaction old SIGSEGV, %s\n", getpid(), strerror(errno));
#endif
		return;
	}
	if (temp_sa.sa_handler != signal_handler) {
		if (sigaction(SIGSEGV, &new_sa, &old_sa[0]) == -1) {
#ifdef DEBUG
			dprintf(STDERR_FILENO, "%d: sigaction SIGSEGV, %s\n", getpid(), strerror(errno));
#endif
		}
	} else {
#ifdef DEBUG
		dprintf(STDERR_FILENO, "%d: signal handler already set for SIGSEGV\n", getpid());
#endif
	}
	new_sa.sa_flags = SA_RESETHAND;
	// Signal handler already present for SIGTERM? (aka fork detection)
	if (sigaction(SIGTERM, NULL, &temp_sa) == -1) {
#ifdef DEBUG
		dprintf(STDERR_FILENO, "%d: sigaction old SIGTERM, %s\n", getpid(), strerror(errno));
#endif
		return;
	}
	if (temp_sa.sa_handler != signal_handler) {
		if (sigaction(SIGTERM, &new_sa, &old_sa[1]) == -1) {
#ifdef DEBUG
			dprintf(STDERR_FILENO, "%d: sigaction SIGTERM, %s\n", getpid(), strerror(errno));
#endif
		}
	} else {
#ifdef DEBUG
		dprintf(STDERR_FILENO, "%d: signal handler already set for SIGTERM\n", getpid());
#endif
	}
}

static void __attribute__((constructor)) start_kcov(void) {
	int ret;
	char buff[30];
#ifdef DEBUG
	fprintf(stderr, "%d: Tracing '", getpid());
	print_program_name();
	dprintf(err_fd, "'\n");
#endif
	if (kcov_fd > 0) {
#ifdef DEBUG
		fprintf(stderr, "%d: KCOV already active\n", getpid());
#endif
		return;
	}

	if (atexit(finish_kcov)) {
#ifdef DEBUG
		dprintf(err_fd, "error atexit()\n");
#endif
	}

	setup_signal_handler();

	parse_env();

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
	if (kcov_mode_env == NULL) {
		kcov_mode = KCOV_TRACE_UNIQUE_PC;
	} else if (strcmp(kcov_mode_env, "trace_order") == 0) {
		kcov_mode = KCOV_TRACE_PC;
	} else if (strcmp(kcov_mode_env, "trace_unique") == 0) {
		kcov_mode = KCOV_TRACE_UNIQUE_PC;
	} else {
#ifdef DEBUG
		dprintf(err_fd, "%d: Unknown tracing mode: %s\n", getpid(), kcov_mode_env);
#endif
		close(kcov_fd);
		kcov_fd = -1;
		return;
	}
#ifdef DEBUG
	dprintf(err_fd, "%d: Using tracing mode '%s'\n", getpid(), (kcov_mode == KCOV_TRACE_PC ? "trace_order" : "trace_unique"));
#endif

	if (kcov_mode == KCOV_TRACE_UNIQUE_PC) {
		int temp_size = 0x4711;
#ifdef __FreeBSD__
		ret = ioctl(kcov_fd, KIOGETBUFSIZE, &temp_size);
#else
		ret = ioctl(kcov_fd, KCOV_INIT_TRACE_UNIQUE, 0);
		temp_size = ret * KCOV_ENTRY_SIZE;
#endif
		if (ret == -1) {
#ifdef DEBUG
			dprintf(err_fd, "%d: ioctl init trace unique: %s\n", getpid(), strerror(errno));
#endif
			close(kcov_fd);
			kcov_fd = -1;
			return;
		}
		area_size = pcs_size = temp_size;
		pcs_size /= sizeof(kernel_long);
#ifdef DEBUG
		dprintf(err_fd, "%d: Kernel told us shared memory size: 0x%jx (0x%jx)\n", getpid(), (uintmax_t)area_size, (uintmax_t)pcs_size);
#endif
	} else {
		area_size = COVER_SIZE * KCOV_ENTRY_SIZE;
#ifdef __FreeBSD__
		ret = ioctl(kcov_fd, KIOSETBUFSIZE, COVER_SIZE);
#else
		ret = ioctl(kcov_fd, KCOV_INIT_TRACE, COVER_SIZE);
#endif
		if (ret == -1) {
#ifdef DEBUG
			dprintf(err_fd, "%d: ioctl init trace pc: %s\n", getpid(), strerror(errno));
#endif
			close(kcov_fd);
			kcov_fd = -1;
			return;
		}
	}
	area = (kernel_long*)mmap(NULL, area_size,
			     PROT_READ | PROT_WRITE, MAP_SHARED, kcov_fd, 0);
	if ((void*)area == MAP_FAILED) {
#ifdef DEBUG
		dprintf(err_fd, "%d: mmap, %s\n", getpid(), strerror(errno));
#endif
		close(kcov_fd);
		kcov_fd = -1;
		return;
	}
#ifdef __FreeBSD__
	if (kcov_mode == KCOV_TRACE_UNIQUE_PC) {
		ret = ioctl(kcov_fd, KIOENABLE, KCOV_MODE_TRACE_PC_UNIQUE);
	} else {
		ret = ioctl(kcov_fd, KIOENABLE, KCOV_MODE_TRACE_PC);
	}
#else
	ret = ioctl(kcov_fd, KCOV_ENABLE, kcov_mode);
#endif
	if (ret) {
#ifdef DEBUG
		dprintf(err_fd, "%d: ioctl enable, %s\n", getpid(), strerror(errno));
#endif
		munmap(area, area_size);
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
	if (kcov_mode == KCOV_TRACE_PC) {
		// Disabled because does not work on i386. Kernel uses uint64_t elements for the buffer
		// __atomic_store_n(&area[0], 0, __ATOMIC_RELAXED);
		area[0] = 0;
	}

}

static void __attribute__((destructor)) finish_kcov(void) {
	int fd;
	kernel_long num_pcs = 0, i, n;

	if (kcov_fd < 1 || err_fd < 1) {
#ifdef DEBUG
		dprintf(err_fd, "%d: No KCOV fd or no fd for stderr present\n", getpid());
#endif
		return;
	}
#ifdef __FreeBSD__
	if (ioctl(kcov_fd, KIODISABLE, 0)) {
#else
	if (ioctl(kcov_fd, KCOV_DISABLE, 0)) {
#endif
#ifdef DEBUG
		dprintf(err_fd, "%d: ioctl disable in %s, %s\n", getpid(), __func__, strerror(errno));
#endif
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
	if (kcov_mode == KCOV_TRACE_UNIQUE_PC) {
#ifdef DEBUG
		dprintf(err_fd, "%d: kernel text base address: 0x%jx\n", getpid(), (uintmax_t)BASE_ADDRESS);
		dprintf(err_fd, "%d: pcs_size = 0x%jx, &pcs_size = %p, &pcs = %p\n", getpid(), (uintmax_t)pcs_size, &area[0], &area[1]);
#endif

		for (i = 1; i < pcs_size; i++) {
			for (int j = 0; j < BITS_PER_LONG; j++) {
				if (area[i] & (1 << j)) {
					dprintf(fd, "0x%jx\n", (uintmax_t)(BASE_ADDRESS + ((i - 1) * BITS_PER_LONG + j)));
					num_pcs++;
				}
			}
		}
#ifdef DEBUG
		dprintf(err_fd, "%d: Done dumping %jd unique bbs for '", getpid(), (uintmax_t)num_pcs);
		print_program_name();
		dprintf(err_fd, "'(%d), isatty(err_fd = %d) = %d, isatty(out_fd = %d) = %d\n",
			getpid(),
			err_fd, isatty(err_fd),
			out_fd, isatty(out_fd));
#endif
	} else {
		// Disabled because does not work on i386. Kernel uses uint64_t elements for the buffer
		// n = __atomic_load_n(&area[0], __ATOMIC_RELAXED);
		n = area[0];
		if (n >= (area_size - 1)) {
			dprintf(err_fd, "%d: Possible buffer overrun detected!\n", getpid());
		}
		for (i = 0; i < n; i++) {
			sortuniq_cover.insert(area[i + 1]);
		}
		for (auto elem : sortuniq_cover) {
			dprintf(fd, "0x%jx\n", (uintmax_t)elem);
		}
#ifdef DEBUG
		dprintf(err_fd, "%d: Done dumping %jd unique bbs of %ju/%ju recorded bbs for '",
			getpid(),
			(uintmax_t)sortuniq_cover.size(), (uintmax_t)n, (uintmax_t)area_size);
		print_program_name();
		dprintf(err_fd, "'(%d), isatty(err_fd = %d) = %d, isatty(out_fd = %d) = %d\n",
			getpid(),
			err_fd, isatty(err_fd),
			out_fd, isatty(out_fd));
#endif
	}
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
		sigset_t new_sig, old_sig;
		sigemptyset(&new_sig);
		sigaddset(&new_sig, SIGTERM);
		sigprocmask(SIG_BLOCK, &new_sig, &old_sig);
#ifdef DEBUG
		dprintf(err_fd, "%d: fork() on the traced process '", getpid());
		print_program_name();
		dprintf(err_fd, "'(%d). Disabling KCOV for child %d.\n", getppid(), getpid());
#endif
		cleanup_kcov(1);
#ifdef DEBUG
		dprintf(err_fd, "%d: Re-enabling KCOV for child %d.\n", getpid(), getpid());
#endif
		start_kcov();
		sigprocmask(SIG_SETMASK, &old_sig, NULL);
	}
	return ret;
}
