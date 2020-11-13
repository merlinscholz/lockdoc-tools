- Setup a Debian X i386 VM
	+ Attention: You must use a raw image for your VM. Do *not* use QCOW2 and others.
	+ Install the following packages (incomplete list): lcov libncurses5-dev git-core build-essential bison flex libssl-dev libgmp-dev libmpfr-dev libmpc-dev bc
	+ We recommend 2 cores, 2GB of RAM and 40GB of hdd space for the VM.
- Build your own kernel
	+ Checkout kernel in /opt/kernel/XXX, for the moment use branch lockdoc-4-10
	+ We recommend using GCC 7.X. GCC 8.x has issues resolving instruction pointers that point to a leaf in the callgraph correctly.
		# git clone git://gcc.gnu.org/git/gcc.git /opt/kernel/gcc/src
		# cd /opt/kernel/gcc/src/
		# git checkout -b releases/gcc-7.2.0 releases/gcc-7.2.0
		# ./configure --enable-lto --prefix=/opt/kernel/gcc/installed/ --enable-languages=c,c++,lto
		# make -j3
			~ If you encoter the following error, follow the instructions below:
				/libsanitizer/sanitizer_common/sanitizer_platform_limits_posix.cc error: sys/ustat.h: No such file or directory
				Fix: "[...] in order to fix the above error the included header in the line 157 of the file sanitizer_platform_limits_posix.cc shall be removed, and its usage in the line 250."
				(https://bobsteagall.com/2017/12/30/gcc-builder/#comment-87)

		# make install
	+ Building the kernel:
		# If you have choosen to build your own gcc, make it sure it is used: export PATH=/opt/kernel/gcc/installed/bin/:$PATH
		# cp lockdoc.config .config
		# make oldconfig
		# make -j X
		# make install
	+ Copy the built vmlinux to your host. You need it for running the experiment as well as for the post processing.
	  You can use the copy-to-host.sh script located in the kernel src directory.
		 ./copy-to-host.sh thasos ~/lockdoc/experiment "-gcc73" -> Copies the vmlinux to ~/lockdoc/experiment on host thasos, and adds the suffix "-gcc73" to the vmlinux.
	  The script adds a version string to the vmlinux before copying it.
- Setup Grub to automatically start the benchmark:
	+ Set variable GRUB_DEFAULT to saved in /etc/default/grub, i.e., GRUB_DEFAULT=saved
	+ Add the following to /etc/grub.d/40_custom:
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
	+ Use tool blkid to determine the UUID of your root partition. Use 'mount' to determine the proper partition, and then run blkid, e.g., blkid /dev/sda5
	+ Replace 'vmlinuz-X.YY-al+' and 'initrd.img-X.YY-al+' by the respective filenames. Look at /boot if you're uncertain.
	+ Set the default entry:
		# grub-set-default "lockdoc-X.YY-al+"
		# update-grub
	+ Attention: From now on, you have to intercept the Grub menu to boot the usual userland. Otherwise, the benchmark starts automatically.

- Install the benchmark
	+ Create the following directories: /lockdoc and /lockdoc/bench-out
	+ Copy 'run-bench.sh' and fork.c from manuals/vm-linux-32/scripts to /lockdoc
	+ Get LTP
		# Clone our ltp repo to /opt/kernel/ltp/src 
 		# git checkout lockdoc
		# Copy {syscalls,fs}-custom from manuals/vm-linux-32/scripts/ to /opt/kernel/ltp/src/runtest/
		# Install the following packages: libaio-dev quota procps libattr1-dev libnuma-dev libcap-dev
		# ./configure --prefix=/opt/kernel/ltp/bin/ && make && make install
		# If configure does not exists, run 'make autotools' first.

