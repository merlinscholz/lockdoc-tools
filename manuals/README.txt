Kernel
======
- Branches: lockdebugging-{3-16,4-10}, lockdoc-5-4
- Default config is located in the root directory:: config-lockdebugging or default-config

VM
==
- Each VM has two serial consoles
	+ The *first* console is used for logging: VM --> FAIL*-Experiment
	+ The *second* console tells the guest OS which benchmark to run: FAIL*-Experiment --> VM 

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
- FAIL* requires an ag++ to be installed.
- Building FAIL*
	+ mkdir build
	+ cd build
	+ cmake ..
	+ ccmake .
		# Select: BUILD_BOCHS, BUILD_DUMP_TRACE
		# Set EXPERIMENTS_ACTIVATED to lockdebugging
		# Set PLUGINS_ACTIVATED to tracing
		# Press c
		# Press g
	+ make -j X

- Running FAIL*
	+ An example BOCHSRC is located in this directory.
	+ The experiments communicates via a serial port mapped to TCP socket with the guest OS. The TCP port is set in the BOCHSRC.
	+ ./fail-client -Wf,--benchmark=<benchmark> -Wf,--port=<TCP port> -Wf,--vmlinux=/path/to/vmlinux -q -f <bochsrc> 2>&1 | tee out.txt
	+ Notes about BOCHS:
		# You have to pay attention when setting up the disk image. It has to be a raw image. BOCHS cannot deal with QCOW2 and others.
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


