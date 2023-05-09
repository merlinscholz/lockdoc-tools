# How to start

1. Set up a VM, install and configure the guest operating system according to their respective manuals in this folder.
2. Set up FAIL* as described below in section 'Fail'. On your host (outside the VM).
3. See sections 'Post Processing' for details on how to process the trace. On your host (outside the VM).

# Kernel

The LockDoc experiments have Kernel support patches for Linux (http://git.cs.tu-dortmund.de/lockDoc/linux) and FreeBSD (https://git.cs.tu-dortmund.de/lockDoc/freebsd)

For Linux, the appropriate config file resides in the root folder of the kernel source tree (`lockdoc.config`), while on FreeBSD, the config resides in the architecture-specific config directory (`sys/i386/conf/LOCKDOC`). For all OSs, there also is a KCOV-enabled configuration file.

# VM

* Each VM has two serial consoles
	* The *first* console is used for logging: VM -> FAIL*-Experiment
	* The *second* console tells the guest OS which benchmark to run: FAIL*-Experiment -> VM
* For setting up the individual VMs, please have a look at the respective manuals: vm-\*/README.md

# Benchmarks

The desired benchmark is selected via a commandline argument to FAIL*.
The following benchmarks are available:

* `sysbench`: Parameters: `--test=fileio --file-total-size=2G --file-num=20 --file-test-mode=rndrw`

* `mixed-fs`: A manually choosen subset of benchmarks from the Linux Test Project

* `ltp-syscall{,-custom}`: The LTP syscall benchmark suite. The custom variant is a subset of the original benchmark. 'Unnecessary' benchmarks have been removed.

* `ltp-fs{,-custom}`: The LTP fs benchmark suite. The custom variant is a subset of the original benchmark.
	'Unnecessary' benchmarks have been removed.

* `lockdoc-test`: Runs the integerated LockDoc test. It executes various correct and faulty locking patterns on a ring buffer. The correct lock pattern is run CONFIG_ESS_LOCK_ANALYSIS_TEST_ITERATIONS times. This value can be overwritten at runtime via the benchmark name: lockdoc-test-100. However, the upper bound is given by CONFIG_ESS_LOCK_ANALYSIS_TEST_ITERATIONS. For LockDoc to function correctly, the ring buffer must be embedded in the observed datatype. Hence, it's size has to be determined at compile time. It is derived using CONFIG_ESS_LOCK_ANALYSIS_TEST_ITERATIONS.

# Databases

* LockDoc needs one database for each processed trace
* We recommend the following scheme for the database name: lockdoc\_\<os>\_\<benchmark>\_\<kernel version>
* For a starter, we recommend using the mixed-fs (or lockdoc-test) benchmark. It produces a small and handy trace that doesn't take too much time to be processed.
* For all other benchmarks, especially the ltp-* ons, I recommend using a *large* SSD/HDD to store your PostgreSQL databases on. Refer to the PostgreSQL manual on how to move the databases to another drive. It is also recommended to configure a second spare harddrive for the database. This one should be configured as temp tablespace for your databases. Some details on that could be found here:
	* https://www.postgresql.org/docs/10/runtime-config-client.html#GUC-TEMP-TABLESPACES
	* https://www.postgresql.org/docs/10/manage-ag-tablespaces.html

	Our experiments showed that the ltp-syscall benchmark, for example, uses several hundred GBs for the database. One of the post processing query can take up to 300GB of spare space while processing.

# FAIL*

## Building FAIL*

* For a detailed guide on how to build FAIL*, please look at the respective manual (doc/how-to-build.txt) in the FAIL* repository. It should be sufficient to follow the instructions below.
* Checkout the FAIL* repo from our project, and use branch lockdoc
* Building FAIL*: See `files-host/helper-scripts/build-fail.sh`. That script has to be run from the root of the FAIL* repository. It also (re)downloads the required [ag++](http://aspectc.org/Download.php) and installes required packages via `sudo apt`. You may want to comment out these parts.

## Running FAIL*
* Sample BOCHSRCs are located in the respective directories belonging to the supported OSs. You may want to change:
	* `display_library`: Enabling a GUI makes the process easier to debug in case anything goes wrong
	* `ata0-master`: To point to the correct, prepared OS disk image
	* `ata0-slave`:  To point to the correct LTP disk image
	For the correct ata0 settings, see below
* Create a temporary directory where you can run the fail client
* Copy all files from manuals/files-host/bochs/* and your modified BOCHSRC to that directory
* Run the test:
	* Linux: `/path/to/fail-client -Wf,--os=linux -Wf,--benchmark=<benchmark> -Wf,--port=4711 -Wf,--vmlinux=/path/to/vmlinux -q -f BOCHSRC 2>&1 | tee out.txt`
	* FreeBSD: `/path/to/fail-client -Wf,--os=freebsd -Wf,--benchmark=<benchmark> -Wf,--port=4711 -Wf,--vmlinux=/path/to/kernel.debug -q -f BOCHSRC 2>&1 | tee out.txt`
	
	Since the tests are emulated and not virtualized this will take a long time. The smaller test suites (mixed-fs/lockdoc-test) are done in under an hour on a system with reasonable single-core performance, while the complete test suite may take days.

## Notes about FAIL*/BOCHS
* BOCHS can only use raw disk images. If you set up your VM in VirtualBox (or other virtualization software that can't use raw images), you have to convert your image before running the experiments: `qemu-img convert -f qcow2 -O raw disk.qcow2 disk.raw`
* The disk geometry specified in the BOCHSRC has to meet the actual image: Adjust cylinders to your image size: cylinders = imageSize / (heads * spt * 512) For more details have a look at $BOCHS/iodev/harddrv.c:{288-291,347}. A wrong geometry will first result in BOCHS warning messages, and second in the guest OS crashing due to reading garbage from the disk. Recommended settings:
	* 40G HDD image: ata0-master: type=disk, mode=volatile, path="XX.img", cylinders=163840, heads=16, spt=32, biosdetect=auto
	* 25G HDD image: ata0-master: type=disk, mode=volatile, path="XX.img", cylinders=102400, heads=16, spt=32, biosdetect=auto
* It might seem a good idea to set the system time via BOCHSRC to a specific time. This might create a deterministic behavior. If you, however, run the image in QEMU to make some changes, this setting also creates the need for the OS to run a filesystem check when the BOCHS is started the next time. We, therefore, set the system time to: clock: sync=none, time0=local
* The experiments communicates via a serial port mapped to TCP socket (default port 4711) with the guest OS. The TCP port is set in the BOCHSRC as option `com2`

# Post Processing

* Compile the convert tool (`cd convert && make`)
* Compile the hypothesizer (`cd hypothesizer && make`)
* For the PostgreSQL cmdline tool to work properly, create the `.pgpass`-file in your home directory:
	```sh
	echo "host_ip:5432:*:username:password" >> ~/.pgpass
	chmod 0600 ~/.pgpass
	```
	
* Do not forget to create the database, and grant your user the proper permissions. The recommended naming scheme is described in the Database section
* Create a directory for the results of the post processing
* In there, create the config file 'convert.conf':
	```conf
	# FAIL*-Output. The tool accepts gzipped files.
	DATA=/path/to/actions-xyz.csv.gz

	# Path to the compiled vmlinux/kernel.debug (on the host)
	KERNEL=/path/to/vmlinux
	#KERNEL=/path/to/kernel.debug

	# VM path to the kernel source tree. The following vaules are the defaults per the respective manuals
	KERNEL_TREE=/opt/kernel/linux
	#KERNEL_TREE=/opt/kernel/freebsd

	# Base URL to the elixir instance (used by the bug report generator)
	BASE_URL=https://iris.cs.tu-dortmund.de/lockdoc-elixir/linux-lockdoc/lockdoc-v5.4.0-rc4-0.2/source

	GUEST_OS=linux
	#GUEST_OS=freebsd

	# PostgreSQL connection parameters
	PSQL_HOST=127.0.0.1
	PSQL_USER=username

	DELIMITER='#'
	ACCEPT_THRESHOLD=99.0
	SELECTION_STRATEGY=bottomup
	REDUCTION_FACTOR=5.0
	#USE_STACK=1
	#USE_SUBCLASSES=1
	```
	Uncomment the proper variables if you want stack and/or subclass processing. Without them, the variant nostack-nosubclasses is analysed.
	* nostack vs. stack:  Uses the stack trace to generate the hypotheses
	* nosubclasses vs. subclasses: Generate hypotheses for each subclass, e.g., inode:ext4 vs. inode:devtmpfs
* Run the post processing script: `/path/to/tools/post-process-trace.sh DATABASE_NAME 2>&1 | tee post-process-trace.out`
	* `post-process-trace.sh` will do the rest for you including calling convert
		* The convert tool (see convert) converts the trace into the relational database
		* convert's log is stored in conv-out.txt
* Output:
	* `all-txns-members-locks-db-*.csv`: Input for the hypothesizer; each row contains one transaction including the held locks and the accessed members
	* `all-txns-members-locks-hypo-*`: Contains the generated hypotheses for each tuple of (data type, member, access type)
	* `all-txns-members-locks-hypo-bugs-*`: Same as `all-txns-members-locks-hypo-*`; additionally includes the generated calls to counterexamples.sql.sh
	* `all-txns-members-locks-hypo-winner-*.csv`: Just contains the winning hypothesis for each tuple of (data type, member, access type)
	* The files `cex-*{.csv,html}` contain the counterexamples. One file per data type. The html variant contains a pretty printed overview.


# How it works

* FAIL* runs our experiment called lockdoc. The source code for it is located in `fail/src/experiments/lockdoc/`.
* FAIL* uses BOCHS for running an emulated VM. Hence, it has access to the guest memory.
* FAIL* experiment and guest exchange information via a shared buffer in the guest memory.
* The experiment reads the address of the buffer (called `la_buffer`) on expertiment startup from the vmlinux/kernel.debug. It uses the embedded ELF information for that.
* Instead of starting the OS-specific init program, it launches our script: `/lockdoc/run-bench.sh` (found in `manuals/files-vm/run-bench.sh`)
* First, it asks the running kernel for its version string. On linux: `/proc/version-git`, on FreeBSD: `/dev/lockdoc/version`. The version string is sent to the experiment via the first serial console. The experiments intercepts the serial console by listening to the respective IO port(s). See `handleIOConsole()` in `fail/src/experiments/lockdoc/experiment.cc` for more details.
* The script asks the experiment via the first serial console for the benchmark to run.
* The experiment responds via the second serial console (TCP socket in `serialSentThreadWork()` in `fail/src/experiments/lockdoc/experiment.cc)`.
* `run-bench.sh` then starts the selected benchmark
* When the instrumented kernel reaches an instrumented lock operation, an alloc, or a free, it gathers all needed information. These happens from the moment the guest kernel is alive. This is mostly way before run-bench.sh is executed. For more details on how to gather those information look at `log_lock()` and `log_memory()` in `linux/include/linux/lockdoc.h`.
* Those information are then written into the aforementioned buffer.
* The guest OS sends a `P` (ping) via the IO port at adress 0xe9.
* At this moment, the FAIL* experiment takes over, and the guest OS is suspended. This happens for every event an experiment has registered for. See `handleKernelConsole()` in `fail/src/experiments/lockdoc/experiment.cc`
* The experiment copies the content from the shared buffer (guest memory) to its own memory.
* Based on the value of the member 'action' it performs certain actions:
	* If it is a memory operation, it starts/ends observing a certain memory area for memory accesses.
	* There's nothing to do for a lock operation.
	* Finally, it logs the event to the output file.
* The experiment yields, and the guest OS resumes.

# Code coverage

See the manuals of the respecive OSs
