Kernel
======
- Branches: lockdebugging-{3-16,4-10}, lockdoc-5-4
- Default config is located in the root directory:: config-lockdebugging or default-config

VM
==
- Each VM has two serial consoles
	+ The *first* console is used for logging: VM --> FAIL*-Experiment
	+ The *second* console tells the guest OS which benchmark to run: FAIL*-Experiment --> VM 
- For setting up the individual VMs, please have a look at the respective manuals: vm-*-32/README*

Benchmarks:
===========
The desired benchmark is selected via a commandline argument to FAIL*.
The following benchmarks are available:

- sysbench
	Parameters for Sysbench: --test=fileio --file-total-size=2G --file-num=20 --file-test-mode=rndrw

- mixed-fs
	A manually choosen subset of benchmarks from the Linux Test Project

- ltp-syscall{,-custom}
	The LTP syscall benchmark suite. The custom variant is a subset of the original benchmark.
	'Unnecessary' benchmarks have been removed.

- ltp-fs{,-custom}
	The LTP fs benchmark suite. The custom variant is a subset of the original benchmark.
	'Unnecessary' benchmarks have been removed.

- lockdoc-test
	Runs the integerated LockDoc test. It executes various correct and faulty locking patterns on a ring buffer.
	The correct lock pattern is run CONFIG_ESS_LOCK_ANALYSIS_TEST_ITERATIONS times. This value can be overwritten at runtime
	via the benchmark name: lockdoc-test-100. However, the upper bound is given by CONFIG_ESS_LOCK_ANALYSIS_TEST_ITERATIONS.
	For LockDoc to function correctly, the ring buffer must be embedded in the observed datatype. Hence, it's size has to be
	determined at compile time. It is derived using CONFIG_ESS_LOCK_ANALYSIS_TEST_ITERATIONS.

Databases
===========

- LockDoc needs one database for each processed run
- We recommend the following scheme for the database name: lockdoc_<os>_<benchmark>_<bernel version>

Fail
====

- For a detailed guide on how to build FAIL*, pls look at the respective README in the FAIL* repository.
- FAIL* requires an ag++ to be installed. First, check if it is already present. Otherwise, try either the current version 2.2 or the nightly build
  Both are available at http://aspectc.org/Download.php.
- Checkout the FAIL* repo from our project, and use branch lock-debugging
- Building FAIL*
	+ mkdir build
	+ cd build
	+ cmake ..
	+ ccmake .
		# Select: BUILD_BOCHS, BUILD_DUMP_TRACE
		# Set EXPERIMENTS_ACTIVATED to lock-debugging
		# Set PLUGINS_ACTIVATED to tracing
		# Press c
		# Press g
	+ make -j X
- Running FAIL*
	+ An example BOCHSRC is located in this directory.
	+ The experiments communicates via a serial port mapped to TCP socket with the guest OS. The TCP port is set in the BOCHSRC.
	* Create a temporary directory where you can run the fail client
	* Copy all files from manuals/bochs/* to that directory. Copy also the example BOCHSRC (bochsrc-example).
	* Adapt the path to your hdd image in your BOCHSRC
	+ /path_to_fail_src/build/bin/fail-client -Wf,--benchmark=<benchmark> -Wf,--port=<TCP port> -Wf,--vmlinux=/path/to/vmlinux -q -f <bochsrc> 2>&1 | tee out.txt
		Example: /home/al/fail/build/bin/fail-client -Wf,--benchmark=lockdoc-test -Wf,--port=4711 -Wf,--vmlinux=/home/al/experiments/vmlinux-4-10-0-20191105-00115-gad4d2ad86498-gcc73 -q -f ./bochsrc 2>&1 | tee out.txt
	+ Notes about BOCHS:
		# You have to pay attention when setting up the disk image. It has to be a raw image. BOCHS cannot deal with QCOW2 and others.
		  You can, however, convert a QCOW2 image, for example, to a raw image. For that, refer to qemu-img.
		# The disk geometry specified in the BOCHSRC has to meet the actual image:
			Adjust cylinders to your image size: cylinders = imageSize / (heads * spt * 512)
			For more details have a look at $BOCHS/iodev/harddrv.c:{288-291,347}
		# If you do not adjust the geometry, the following errors occurs in bochsout.txt: "00000000000p[HD   ] >>PANIC<< ata0-0 disk size doesn't match specified geometry"
		  BOCHS, however, will continue executing. It does not emulate a HDD.

		# Recommended settings:
			- 40G HDD image: ata0-master: type=disk, mode=volatile, path="XX.img", cylinders=163840, heads=16, spt=32, biosdetect=auto
			- 25G HDD image: ata0-master: type=disk, mode=volatile, path="XX.img", cylinders=102400, heads=16, spt=32, biosdetect=auto
		# It might seem a good idea to set the system time via BOCHSRC to a specific time. This might create a deterministic behavior.
		  If you, however, run the image in QEMU to make some changes, this setting also creates the need for the OS to run a filesystem check when
		  the BOCHS is started the next time.
		  We, therefore, set the system time to: clock: sync=none, time0=local

Post Processing
===============

- Change to directory convert, and run make
- Change to directory hypothesizer, and run make
- For the PostgreSQL cmdline tool to work properly, create '.pgpass' in your home directory, and add the following line:
IP address of the PostgreSQL host:5432:*:username:passwort
  Ensure it is only writeable by your user (-> chmod 0600 .pgpass)
- Do not forget to create the database, and grant your user the proper permissions
- Create a directory for the results of the post processing
- Change into that directory
- Create the config file 'convert.conf':
DATA=path to the output produced by FAIL*, e.g., actions.csv.gz. The precise name can be obtained by reading the output of fail-client.
KERNEL=path to the vmlinux (outside the VM)
KERNEL_TREE=directory where the kernel tree is located within your VM
BASE_URL=base URL to the elixir instance, it is used by the bug report generator: e.g. https://ess.cs.tu-dortmund.de/lockdoc-elixir/linux-lockdoc/lockdoc-v5.4.0-rc4-0.2/source
GUEST_OS=guest OS type, linux or freebsd
PSQL_HOST=IP of the PostgreSQL host
PSQL_USER=username
DELIMITER='#'
ACCEPT_THRESHOLD=99.0
- Run the post processing script: $PATH_TO_TOOLS_REPO/post-process-trace.sh DATABASE_NAME 2>&1 | tee post-process-trace.out
	+ post-process-trace.sh will do the rest for you including calling convert
		* The convert tool (see convert/) converts the trace into the relational database
		* convert's log is stored in conv-out.txt
		* Explanation of the variants
			* nostack vs. stack:  Uses the stack trace to generate the hypotheses
			* nosubclasses vs. subclasses: Generate hypotheses for each subclass, e.g., inode:ext4 vs. inode:devtmpfs
	+ output files
		* all-txns-members-locks-db-*.csv: Input for the hypothesizer; each row contains one transaction including the held locks and the accessed members
		* all-txns-members-locks-hypo-*: Contains the generated hypotheses for each tuple of (data type, member, access type)
		* all-txns-members-locks-hypo-bugs-*: Same as all-txns-members-locks-hypo-*; additionally includes the generated calls to counterexamples.sql.sh
		* all-txns-members-locks-hypo-winner-*.csv: Just contains the winning hypothesis for each tuple of (data type, member, access type)
		* The files cex-*{.csv,html} contain the counterexamples. One file per data type. The html variant contains a pretty printed overview.


Behind the scenes
=================

- FAIL* runs our experiment called lock-debugging. Src is located in fail/src/experiments/lock-debugging/.
- FAIL* uses BOCHS for running a VM. Hence, it has access to the guest memory.
- FAIL* experiment and guest exchange information via a shared buffer in the guest memory.
- The experiment reads the address of the buffer on expertiment startup from the vmlinux. It uses the embedded ELF information for that.
- Instead of starting the OS-specific init program, it launches our script: /lockdoc/run-bench.sh (--> manuals/vm-linux-32/scripts/run-bench.sh)
- First, it asks the running kernel for its version string. On linux: /proc/version-git, on FreeBSD: /dev/lockdoc/version. 
  The version string is send to the experiment via the first serial console. The experiments intercepts the serial console by listening to the respective IO port(s).
  See handleIOConsole() in fail/src/experiments/lock-debugging/experiment.cc
- The script asks the experiment via the first serial console for the benchmark to run.
- The experiment responds via the second serial console (TCP socket in serialSentThreadWork() in fail/src/experiments/lock-debugging/experiment.cc).
- run-bench.sh then starts the selected benchmark
- When the instrumented kernel reaches an instrumented lock operation, an alloc, or a free, it gathers all needed information.
  These happens from the moment the guest kernel is alive. This is mostly way before run-bench.sh is executed.
  For more details on how to gather those information look at log_lock() and log_memory() in linux/include/linux/lockdoc.h
- Those information are then written into the aforementioned buffer.
- The guest OS sends a 'P' via the IO port at adress 0xe9.
- At this moment, the FAIL* experiment takes over, and the guest OS is suspended. This happens for every event an experiment has registered for.
  See handleKernelConsole() in fail/src/experiments/lock-debugging/experiment.cc
- The experiment copies the content from the shared buffer (guest memory) to its own memory.
- Based on the value of the member 'action' it performs certain actions:
	* If it is a memory operation, it starts/ends observing a certain memory area for memory accesses.
	* There's nothing to do for a lock operation.
	* Finally, it logs the event to the output file.
- The experiment yields, and the guest OS resumes.

Code Coverage in Linux
======================

- Checkout the kernel tree again in a separate directory, e.g., linux-32-gcov. For now, please stick to branch lockdebugging-4-10.
- cp config-lockdebugging .config
- make menuconfig
	* Uncheck "Enable ESS lock analysis facility"
	* "General Setup" --> "Local version - append to kernel release" --> Enter "gcov"
	* "General Setup" --> "GCOV-based kernel profiling" --> "Enable gcov-based kernel profiling"
							    |
							    --> "Specify GCOV format" --> "Autodetect"
- "Exit" --> "Save config"
- make -j 3
- make install
- On reboot, select the GCOV kernel. The GRUB entry should end with "-gcov".
- Ensure the debugfs is mounted. Otherwise, run as root: mount -t debugfs none /sys/kernel/debug
- Copy processing/gcov-trace.sh to your VM
- Gather profiling information using gcov-trace.sh
	* Become root, e.g., sudo u
	* ./gcov-trace.sh PATH_TO_YOUR_KERNEL_TREE OUTPUT_FILE COMMAND ARGS
	* Example for profiling "sleep 5": ./gcov-trace.sh /opt/kernel/linux-32-gcov test.trace sleep 5
- Copy the output to your host
- To retrieve the code coverage from the output file, use processing/gcov-summary.py
	* ./gcov-summary.py --kernel-directory PATH_TO_YOUR_KERNEL_TREE_IN_VM --per-directory [--filter 'REGEX']
	* Example: Summarize code coverage for each directory containing fs, expect fs/proc
		+ gcov-summary.py --kernel-directory /opt/kernel/linux-32-gcov/ --per-directory --filter '^fs(?!/proc)' test.trace
