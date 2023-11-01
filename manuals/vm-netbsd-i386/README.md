# LockDoc Experiments on FreeBSD

1. Set up an **i386** VM (qemu/libvirt or VirtualBox) for the FreeBSD installation.
    * Main `/`disk:
        * Disk size: 25G
        * Disk type: RAW `qemu-img create -f raw netbsd.img 25G` (if possible. On VirtualBox the image has to be converted later)
    * Second disk used for LTP:
        * Disk size: 5G
        * Disk type: RAW `qemu-img create -f raw ltp.img 5G` (this image does not need to be attached to the VM at all times)
    * Working networking
2.  Install NetBSD 9.3 with the [NetBSD-9.3-i386.iso](http://cdn.netbsd.org/pub/NetBSD/NetBSD-9.3/iso/NetBSD-9.3-i386.iso) installation medium.
	
    * MBR, Whole Disk
    * "Installation without X11"
    * Enable Binary Package installation
    * Enable SSH, disable everything else
    * We do not need any other user than root
4.  Reboot into the VM and make basic preparations:
    
    *   Edit the SSH config to allow root to ssh into the VM (useful for transferring files in the future): 
        ```sh
        echo "PermitRootLogin yes" >> /etc/ssh/sshd_config
        /etc/rc.d/sshd restart
        ```
    *   The Linux Emulation is actived at the Kernel config level. It is on by default.
    *   Install required packages: 
        ```sh
        pkgin install nano git bash sysbench sudo e2fsprogs rsync tmux mozilla-rootcerts
        /usr/pkg/sbin/mozilla-rootcerts install
        ```
    * Fix paths and shell for `run-bench.sh`: 
        ```sh
        ln -s /usr/pkg/bin/bash /bin/bash
        ln -s /usr/pkg/bin/sysbench /usr/bin/sysbench
        chsh -s /usr/pkg/bin/bash
        ```

4.  Install & configure Linux subsystem:

    ⚠️ We use `/compat/ubuntu` instead of `/emul/linux` as to not break compatibility with the LockDoc `run-bench.sh` scripts
    
    * There is no working way of bootstrapping a Linux installtion from NetBSD directly (apart from ancient SUSE versions without package managers). To circumvent this, we'll use a i386 Docker image and extract the rootfs from it. In the NetBSD VM:

		```sh
        mkdir -p /compat/ubuntu/opt/kernel/ltp/bin && cd /compat/ubuntu
        curl -LO https://github.com/debuerreotype/docker-debian-artifacts/raw/dist-i386/stable/rootfs.tar.xz
		tar -xvf rootfs.tar.xz
		```

		This will set up a folder with a full Debian stable installation. Note that we cannot use the Debian system proeperly, as a lot of networking and package management is broken at the Linxu emulation layer.

        In previous iterations we used Debian Jessie, but using Debian Bullseye enables easier cross-compilation (with Jessie there were glibc version mismatches, but it turns out that even a current glibc only needs Kernel ~3.2).
    * In the NetBSD VM:
        ```sh
        # Prepare Linux' /dev/:
        echo "procfs /compat/ubuntu/proc procfs ro,linux" >> /etc/fstab
        cp /usr/share/examples/emul/linux/etc/LINUX_MAKEDEV /compat/ubuntu/dev
        cd /compat/ubuntu/dev/ && sh LINUX_MAKEDEV all
        reboot
        ```

    * On your Linux workstation you can now cross-compile LTP:
        ```sh
        git clone ssh://git@git.cs.tu-dortmund.de:2222/merlin.scholz/ltp.git && cd ltp
        git checkout scholz-lockdoc-20230127
        ./lockdoc-cross-compile.sh
        scp -r build root@<VM_IP_ADDRESS>:/compat/ubuntu/opt/kernel/ltp/bin
        # TODO Add custom tests from LockDoc/tools/manuals/vm-linux-32/scripts to bin/runtests
        ```

5. Preparing the neccessary script & config files (outside the chroot)
    * Init script:
        ```sh
            mkdir -p /lockdoc/bench-out
            cp /opt/kernel/tools/manuals/vm-netbsd-i386/scripts/* /lockdoc
            cp /opt/kernel/tools/manuals/vm-linux-32/scripts/run-bench.sh /lockdoc
            cp /opt/kernel/tools/manuals/vm-linux-32/scripts/fork.c /lockdoc/bench-out
        ```

    * Prepare the test disk to run LTP on:

        LTP requires a separate disk image to actually run the test suites on. The device has to be noted in `/lockdoc/run-bench.sh`, the default there is `DEVICE=/dev/ada1`. Please note that the device path may differ when running the system in your usual hypervisor vs running it in BOCHS. If you're planing to run the experiments with KCOV enabled, you have to insert this path also when analyzing the logs (see point "KCOV" further down for details).

At this point, upon rebooting the VM, it should automatically start the benchmark **when the LockDoc kernel is being booted** (and unless a key is pressed when prompted to do so). This is implemented into the service management. The image can now be used to run the LockDoc experiments in FAIL*/BOCHS. If you are using VirtualBox (or another virtualization software that doesn't support raw disk images), you have to convert the disk image before using fail*. On the VM host: ```qemu-img convert -f qcow2 -O raw freebsd.qcow2 freebsd.raw```

6. Building the Kernel
    First of all, you should create a backup of the original Kernel in the VM:
    ```sh
    cp /netbsd /netbsd.orig
    ```

    If you ever install an unusable Kernel from this point an, you can just press `3` at the bootloader prompt and type `boot /netbsd.orig` to return to the original Kernel. To easily identify which kernel is being booted, the LockDoc kernel has a blue output (the default kernel has a green one).

    TODO: lockdoc-test kernel module initialization

    After that, you can start compiling the new Kernel: 
    * On the target system (in-tree)
        TODO
    * On the target system (out-of-tree)
        TODO
    * Cross-compiling
        Cross-compiling from other POSIX operating systems is the most comfortable way of developing in this environemnt. There is an extensive [manual provided by NetBSD](https://www.netbsd.org/docs/guide/en/chap-build.html) but it boils down to:

        ```sh
        git clone ssh://git@git.cs.tu-dortmund.de:2222/LockDoc/netbsd.git && cd netbsd
        ./build.sh -U -O ./obj -j$(nproc) -m i386 -a i386 tools # Build toolchain
        ./build.sh -U -O ./obj -j$(nproc) -m i386 -a i386 -u kernel=LOCKDOC # (Re)build Kernel
        scp obj/sys/arch/i386/compile/LOCKDOC/netbsd root@<VM_IP_ADDRESS>:/netbsd # Copy Kernel to VM
        scp etc/rc root@<VM_IP_ADDRESS>:/etc/rc # Copy service management to VM
        ```

7. KCOV
    TODO
