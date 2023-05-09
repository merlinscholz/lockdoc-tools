# LockDoc Experiments on Linux

⚠️ This README is a rewrite/fixup/combination of older docs. The pre-rewrite version can be found at commit fda44cd05e731c47585cdda490a12d143c0f500b: https://git.cs.tu-dortmund.de/LockDoc/tools/src/commit/fda44cd05e731c47585cdda490a12d143c0f500b/manuals/vm-linux-32/README.txt

1. Set up an **i386** VM (qemu/libvirt or VirtualBox) for the Linux installation.
    * Main `/`disk:
        * Disk size: 25G
        * Disk type: RAW `qemu-img create -f raw linux.img 40G` (if possible. On VirtualBox the image has to be converted later)
    * Second disk used for LTP:
        * Disk size: 5G
        * Disk type: RAW `qemu-img create -f raw ltp.img 5G` (this image does not need to be attached to the VM at all times)
    * Working networking
2. Install Debian Buster with the [debian-10.13.0-i386-netinst.iso](https://cdimage.debian.org/cdimage/archive/10.13.0/i386/iso-cd/debian-10.13.0-i386-netinst.iso) installation medium.
3. Reboot into the VM and make basic preparations:
    * Edit the SSH config to allow root to ssh into the VM (useful for transferring files in the future):
        ```sh
            echo "PermitRootLogin yes" >> /etc/ssh/sshd_config
            /etc/rc.d/sshd restart
        ```
    * Install required packages:
        ```sh
        apt install nano git bash sysbench sudo e2fsprogs tmux lcov libncurses5-dev git-core build-essential bison flex libssl-dev libgmp-dev libmpfr-dev libmpc-dev bc dosfstools autoconf util-linux automake pkg-config libaio-dev quota procps libattr1-dev libnuma-dev libcap-dev
        ```
4. Preparing LTP

	```sh
	mkdir -p /opt/kernel/ltp
	git clone https://git.cs.tu-dortmund.de/lockdoc/ltp.git /opt/kernel/ltp/src
	git clone https://git.cs.tu-dortmund.de/lockdoc/tools.git /opt/kernel/tools
	cp /opt/kernel/tools/manuals/vm-linux-i386/scripts/fs-custom /opt/kernel/tools/manuals/vm-linux-i386/scripts/syscalls-custom /opt/kernel/ltp/src/runtest
	cd /opt/kernel/ltp/src
	aclocal
	make autotools
	./configure --prefix=/opt/kernel/ltp/bin
	make -j$(nproc)
	make install
	```
5. Preparing the neccessary script & config files
	* Init script:
		* Create the neccesary directories:
		```sh
		mkdir -p /lockdoc/bench-out
		```
		* Copy 'run-bench.sh' and fork.c from manuals/vm-linux-i386/scripts to /lockdoc:
		```sh
		cp /opt/kernel/tools/manuals/vm-linux-i386/scripts/run-bench.sh /opt/kernel/tools/manuals/vm-linux-i386/scripts/run-bench.sh /lockdoc 
		```
	* Bootloader
		* Set variable GRUB_DEFAULT to saved in /etc/default/grub, i.e., `GRUB_DEFAULT=saved`
		* Add the following to /etc/grub.d/40_custom:
			```
			menuentry 'LockDoc-X.YY-al' --class debian --class gnu-linux --class gnu --class os $menuentry_id_option 'lockdoc-X.YY-al+' {
					load_video
					insmod gzio
					if [ x$grub_platform = xxen ]; then insmod xzio; insmod lzopio; fi
					insmod part_msdos
					insmod ext2
					set root='hd0,msdos1'
					if [ x$feature_platform_search_hint = xy ]; then
					search --no-floppy --fs-uuid --set=root --hint-bios=hd0,msdos1 --hint-efi=hd0,msdos1 --hint-baremetal=ahci0,msdos1  <blkid of the root partition>
					else
					search --no-floppy --fs-uuid --set=root a
					fi
					echo    'Loading Linux X.YY-al+ ...'
					linux   /boot/vmlinuz-X.YY-al+ root=UUID=<blkid of the root partition> ro quiet loglevel=0 init=/lockdoc/run-bench.sh
					echo    'Loading initial ramdisk ...'
					initrd  /boot/initrd.img-X.YY-al+
			}
			```
		* Use tool blkid to determine the UUID of your root partition. Use `mount` to determine the proper partition, and then run `blkid`, e.g., `blkid /dev/sda2`
		* Replace `vmlinuz-X.YY-al+` and `initrd.img-X.YY-al+` by the respective filenames. Look at /boot if you're uncertain.
		* Set the default entry:
			```sh
			grub-set-default "lockdoc-X.YY-al+"
			update-grub
			```
		* Attention: From now on, you have to intercept the Grub menu to boot the usual userland. Otherwise, the benchmark starts automatically.

At this point, upon rebooting the VM, it should automatically start the benchmark **when the kernel is installed** (and unless a key is pressed when prompted to do so). The image can now be used to run the LockDoc experiments in FAIL*/BOCHS. If you are using VirtualBox (or another virtualization software that doesn't support raw disk images), you have to convert the disk image before using fail*. On the VM host: ```qemu-img convert -f qcow2 -O raw freebsd.qcow2 freebsd.raw```

6. Kernel
	* Checkout kernel in `/opt/kernel/XXX`, for the moment use branch lockdoc-5-4:
		```sh
		git clone -b lockdoc-5-4 https://git.cs.tu-dortmund.de/LockDoc/linux.git /opt/kernel/linux
		```
	* We recommend using GCC 7. GCC 8 has issues resolving instruction pointers that point to a leaf in the callgraph correctly.
		```sh
		git clone git://gcc.gnu.org/git/gcc.git /opt/kernel/gcc/src
		cd /opt/kernel/gcc/src/
		git checkout releases/gcc-7
		./configure --enable-lto --prefix=/opt/kernel/gcc/installed/ --enable-languages=c,c++,lto
		make -j$(nproc)
		make install
		```

		If you encoter the following error, follow the instructions below:
		```/libsanitizer/sanitizer_common/sanitizer_platform_limits_posix.cc error: sys/ustat.h: No such file or directory```

		Fix: "[...] in order to fix the above error the included header in the line 157 of the file ```sanitizer_platform_limits_posix.cc``` shall be removed, and its usage in the line 250."
		(https://bobsteagall.com/2017/12/30/gcc-builder/#comment-87)
	* Building the kernel:
		If you have choosen to build your own gcc, make it sure it is used: `export PATH=/opt/kernel/gcc/installed/bin/:$PATH`. After that/otherwise, run:

		```sh
		cd /opt/kernel/linux
		cp lockdoc.config .config
		make oldconfig
		make -j$(nproc)
		make install
		```
	* Copy the built vmlinux to your host. You need it for running the experiment as well as for the post processing. The easiest way is to use `scp`:

		```sh
		scp vmlinux username@yourworkstation.local:~/lockdoc-vmlinux
		```

⚠️ The following steps may be optional, depending on what you're trying to analyze.

7. KCOV
	* Compile the aformentioned Linux kernel with a KCOV-enabled config: lockdoc-coverage-{amd64,386}.config
	* Compile the kcov-lib (run `make` in `tools-repo/coverage/kcov`) on the target machine
	* Gather basic-block coverage
		* For any program:

			```
			LD_PRELOAD=/path/to/tools/coverage/kcov/kcovlib.so KCOV_MODE=trace_{order,unique} KCOV_OUT={fd,progname,stderr,FILENAME} PROGRAM
			KCOV_OUT 
				fd: kcovlib expects the output file/pipe to opened at fd MAX_FD-1; for a Bash example see below:
					MAX_FD=`ulimit -Sn`
						OUT_FD=`echo  ${MAX_FD} - 1 | bc`
					exec ${OUT_FD}> > OUT.TXT
				progname: kcovlib uses the program name for the output file
				stderr: obvious.
			```

		* For LTP:

			Use the provided script `tools-repo/coverage/kcov-all-ltp-tests.sh`
			We recommend disabling sortuniq for large or parallel programs setting `USE_SORTUNIQ=0`.
			Please providate a secondary device for LTP using DEVICE env variable.
			Use TESTS env variable to run a subset of all LTP testsuites, e.g. TESTS=syscalls,fs
			Usage: `trace-all-ltp-tests.sh <kcov|strace> <path to kcovlib.so> <path to LTP> <output directory>`
		  Example usage:
			```sh
			USE_SORTUNIQ=0 DEVICE=/dev/vdb TESTS=fsx,fs_readonly /home/al/tools/coverage/trace-all-ltp-tests.sh kcov /home/al/tools/coverage/kcov/kcovlib.so /home/al/ltp/bin/ /home/al/ltp-coverage/
			```
		* For LockDoc's benchmark used for the EuroSys paper:
			```sh
			LD_PRELOAD=/home/al/tools/coverage/kcov/kcovlib.so KCOV_OUT=/tmp/bar.map GATHER_COV=1 /lockdoc/run-bench.sh mixed-fs
			```

8. Using Moonshine (https://github.com/shankarapailoor/moonshine)
	* Activate [Debian Buster Backports](https://backports.debian.org/Instructions/)
	* Install packages:
		```sh
		apt install ragel
		apt install -t buster-backports golang-1.14-go
		```
	* Set up Moonshine:
		```sh
		mkdir moonshine; export GOROOT=/usr/lib/go-1.14/; export GOPATH=$HOME/moonshine; export PATH=$GOPATH/bin:$GOROOT/bin:$PATH;
		cd moonshine
		go get golang.org/x/tools/cmd/goyacc
		go get -u -d github.com/google/syzkaller/ (maybe with /prog at the end)
		cd src/github.com/google/syzkaller/ && git checkout -b moonshine f48c20b8f9b2a6c26629f11cc15e1c9c316572c8
		cd ~/moonshine/src/github.com/shankarapailoor/moonshine && make
		cd ~/moonshine
		git clone https://github.com/strace/strace strace
		cd strace && git checkout -b moonshine a8d2417e97e71ae01095bee1a1e563b07f2d6b41
		git apply $GOPATH/src/github.com/shankarapailoor/moonshine/strace_kcov.patch
		./bootstrap
		./configure
		make
		./strace -o tracefile -s 65501 -v -xx -f -k /path/to/executable arg1 arg2 .. argN
		````

		Example usage for LTP:
		```sh
		DEVICE=/dev/vdb TESTS=fs-moonshine-lockdoc /home/al/tools/coverage/trace-all-ltp-tests.sh strace /home/al/moonshine/strace/ /home/al/ltp/bin/ /home/al/ltp-strace/
		/bin/moonshine -dir TRACES_DIR -distill getting-started/distill.json
		```
	* Place resulting corpus.db in your syzkaller's working directory
	* To directly convert strace output to syzkaller programs, and convert them to a corpus.db:
		```sh
		cd ~/moonshine/src/github.com/google/syzkaller/ && git checkout master && make trace2syz
		./bin/syz-trace2syz -deserialize TARGET_DIR -dir INPUT_DIR -vv 2
		./bin/syz-db pack TARGET_DIR foo.db
		```
