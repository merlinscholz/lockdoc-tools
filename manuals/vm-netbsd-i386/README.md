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
        pkgin install nano git bash sysbench sudo e2fsprogs rsync tmux
        ```
    * Fix paths and shell for `run-bench.sh`: 
        ```sh
        ln -s /usr/local/bin/bash /bin/bash ln -s /usr/local/bin/
        sysbench /usr/bin/sysbench chsh -s /usr/local/bin/bash
        ```

4.  Install & configure Linux subsystem:

    ⚠️ We use `/compat/ubuntu` instead of `/emul/linux` as to not break compatibility with the LockDoc `run-bench.sh` scripts
    
    * There is no working way of bootstrapping a Linux installtion from NetBSD directly (apart from ancient SUSE versions). To circumvent this, on a Linux machine (your workstation), install the necessary packages and run:
		```sh
		curl https://ftp-master.debian.org/keys/release-8.asc | gpg --import --no-default-keyring --keyring ./debian-release-8.gpg
		sudo debootstrap --arch=i386 --keyring=./debian-release-8.gpg stretch ./stretch
		```
		This will set up a folder with a full Debian Jessie installation. We have to use Jessie because NetBSD's emulation layer has difficulties with Kernels newer than 3.16.
    - Transfer the Debian installation to your NetBSD VM:
        ```sh
        rsync -a ./jessie root@<IP_ADDRESS>:/compat/ubuntu
        ```
    -   Chroot into the Linux Compat folder and install neccesary packages:
        ```sh
        chroot /compat/ubuntu /bin/bash
        apt update apt purge rsyslog # This package has issues when running in the NetBSD compatiblity environment
        apt install dosfstools e2fsprogs build-essential git-core autoconf nano util-linux automake pkg-config
        ```
    -   Clone and build LTP (still in chroot): 
        ```sh
        git clone https://git.cs.tu-dortmund.de/lockdoc/ltp.git /opt/kernel/ltp/src
        git clone https://git.cs.tu-dortmund.de/lockdoc/tools.git /opt/kernel/tools
        cp /opt/kernel/tools/manuals/vm-linux-32/scripts/fs-custom /opt/kernel/tools/manuals/vm-linux-32/scripts/syscalls-custom /opt/kernel/ltp/src/runtest
        cd /opt/kernel/ltp/src
        aclocal
        make autotools
        ./configure --prefix=/opt/kernel/ltp/bin
        make -j$(nproc)
        make install
        ```
5. Preparing the neccessary script & config files (outside the chroot)
    * Init script:
        ```sh
            mkdir -p /lockdoc/bench-out
            cp /compat/ubuntu/opt/kernel/tools/manuals/vm-freebsd-i386/scripts/* /lockdoc
            cp /compat/ubuntu/opt/kernel/tools/manuals/vm-linux-32/scripts/run-bench.sh /lockdoc
            cp /compat/ubuntu/opt/kernel/tools/manuals/vm-linux-32/scripts/fork.c /lockdoc/bench-out
        ```
    * Find out the UFS ID of your `/` partition:
        ```sh
        mount # look for the disk that is mounted on "/"
        dumpfs -l /dev/vtbd0s1a # Adapt disk as neccessary
        ```
    * Bootloader `/boot/loader.conf`:
        ```conf
        kernel="lockdoc" # Set FreeBSD's kernel as default
        kernels_autodetect="YES" # Detect all installed kernels automatically
        #console="vidconsole,comconsole"
        console="vidconsole" # Disable com console as not to interfer with LockDoc
        vm.kmem_size="512M"
        vm.kmem_size_max="512M"
        vfs.root.mountfrom="ufs:/dev/ufsid/change-me" # Insert the UFS ID of the previous step here
        init_script="/lockdoc/boot.init.sh"
        ```
        From this point on, if you want to revert to the normal Kernel, you have to press "6" at the bootloader prompt.

    * Prepare the test disk to run LTP on:

        LTP requires a separate disk image to actually run the test suites on. The device has to be noted in `/lockdoc/run-bench.sh`, the default there is `DEVICE=/dev/ada1`. Please note that the device path may differ when running the system in your usual hypervisor vs running it in BOCHS. If you're planing to run the experiments with KCOV enabled, you have to insert this path also when analyzing the logs (see point "KCOV" further down for details).


At this point, upon rebooting the VM, it should automatically start the benchmark **when the kernel is installed** (and unless a key is pressed when prompted to do so). The image can now be used to run the LockDoc experiments in FAIL*/BOCHS. If you are using VirtualBox (or another virtualization software that doesn't support raw disk images), you have to convert the disk image before using fail*. On the VM host: ```qemu-img convert -f qcow2 -O raw freebsd.qcow2 freebsd.raw```

6. Building the Kernel
    * On the target system (in-tree)
        TODO
    * On the target system (out-of-tree)
        ```sh
        cp -r /boot/kernel /boot/kernel.orig
        git clone https://git.cs.tu-dortmund.de/LockDoc/freebsd.git -b 13.0-lockdoc /opt/kernel/freebsd/src
        cd /opt/kernel/freebsd/src/sys/i386/conf
        config -d /opt/kernel/freebsd/obj -I `pwd` `pwd`/LOCKDOC
        cd /opt/kernel/freebsd/obj
        MODULES_OVERRIDE="" make -j$(sysctl -n hw.ncpu)
        sudo -E MODULES_OVERRIDE="" KODIR=/boot/lockdoc make install
        ```
    * Cross-compiling

        Cross-compiling from other POSIX operating systems is not (yet) supported on FreeBSD
    Regardless of the build process, remember to save the `kernel.debug` file (usually in `/opt/kernel/freebsd/obj/kernel.debug`) outside of the VM, as it is a requirement for the FAIL* framework to work.

7. KCOV

    To build the kernel with KCOV support, you can use the LOCKDOC_KCOV config instead of LOCKDOC. Remember to adapt the Kernel name.
    Furthermore, you have to add the proper `DEVICE` environment variable upon running `trace-all-ltp-tests.sh`
