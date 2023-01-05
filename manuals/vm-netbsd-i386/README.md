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
    
    * There is no working way of bootstrapping a Linux installtion from NetBSD directly (apart from ancient SUSE versions without package managers). To circumvent this, on a Linux machine (your workstation), install the necessary packages and run:
		```sh
		curl https://ftp-master.debian.org/keys/release-8.asc | gpg --import --no-default-keyring --keyring ./debian-release-8.gpg
		sudo debootstrap --arch=i386 --keyring=./debian-release-8.gpg --include=dosfstools,e2fsprogs,build-essential,git,autoconf,nano,util-linux,automake,pkg-config,ssh,curl jessie ./jessie
		```
		This will set up a folder with a full Debian Jessie installation. We have to use Jessie because NetBSD's emulation layer has difficulties with Kernels newer than 3.16. THe commands require `sudo` because Debians files already have their proper permissions. The command also installs the necessary packages. Also note that for some reason, we cannot some packages via `apt` after this, probably a bug.
    - Transfer the Debian installation to your NetBSD VM:
        ```sh
        sudo rsync -a ./jessie/ root@<VM_IP_ADDRESS>:/compat/ubuntu
        ```
        The trailing slash is important.
    - In the NetBSD VM:
        ```sh
        # Prepare Linux' /dev/:
        echo "procfs /compat/ubuntu/proc procfs ro,linux" >> /etc/fstab
        cp /usr/share/examples/emul/linux/etc/LINUX_MAKEDEV /compat/ubuntu/dev
        cd /compat/ubuntu/dev/ && sh LINUX_MAKEDEV all
        reboot
        ```
        ```sh
        chroot /compat/ubuntu /bin/bash
        git clone https://git.cs.tu-dortmund.de/lockdoc/ltp.git /opt/kernel/ltp/src
        git clone https://git.cs.tu-dortmund.de/lockdoc/tools.git /opt/kernel/tools
        ```

        Be aware that cloning over https will probably not work and cloning via ssh requires ssh keys to be set up in the chroot. It may be easier to clone from the NetBSD VM into the chroot:

        ```sh
        git clone https://git.cs.tu-dortmund.de/lockdoc/ltp.git /compat/ubuntu/opt/kernel/ltp/src
        git clone https://git.cs.tu-dortmund.de/lockdoc/tools.git /compat/ubuntu/opt/kernel/tools
        ```

        Set up files and compile LTP (back in the chroot)

        ```sh
        cp /opt/kernel/tools/manuals/vm-linux-32/scripts/fs-custom /opt/kernel/tools/manuals/vm-linux-32/scripts/syscalls-custom /opt/kernel/ltp/src/runtest
        cd /opt/kernel/ltp/src
        aclocal
        make autotools
        ./configure --prefix=/opt/kernel/ltp/bin
        make -j$(nproc)
        make install
        ```

        In case this fails to build (very likely), you can also use the Makefile in the `ltp-crosscompile` folder to compile everything on your workstation:
        ```sh
            cd ltp-crosscompile
            make
            rsync -a output/ltp/ root@<VM_IP_ADDRESS>:/compat/ubuntu/opt/kernel/ltp/bin
        ```
5. Preparing the neccessary script & config files (outside the chroot)
    * Init script:
        ```sh
            mkdir -p /lockdoc/bench-out
            cp /compat/ubuntu/opt/kernel/tools/manuals/vm-netbsd-i386/scripts/* /lockdoc
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
    First of all, you should create a backup of the original Kernel in the VM:
    ```sh
    cp /netbsd /netbsd.orig
    ```

    After that, you can start compiling the new Kernel: 
    * On the target system (in-tree)
        TODO
    * On the target system (out-of-tree)
        TODO
    * Cross-compiling
        Cross-compiling from other POSIX operating systems is the most comfortable way of developing in this environemnt. There is an extensive [manual provided by NetBSD](https://www.netbsd.org/docs/guide/en/chap-build.html) but it boils down to:

        ```sh
        git clone ssh://git@git.cs.tu-dortmund.de:2222/LockDoc/netbsd.git
        cd netbsd
        ./build.sh -U -O ./obj -j16 -m i386 -a i386 tools # Build toolchain
        ./build.sh -U -O ./obj -j16 -m i386 -a i386 -u kernel=LOCKDOC # (Re)build Kernel
        rsync -v obj/sys/arch/i386/compile/LOCKDOC/netbsd root@<VM_IP_ADDRESS>:/netbsd # Copy Kernel to VM
        ```

7. KCOV

    To build the kernel with KCOV support, you can use the LOCKDOC_KCOV config instead of LOCKDOC. Remember to adapt the Kernel name.
    Furthermore, you have to add the proper `DEVICE` environment variable upon running `trace-all-ltp-tests.sh`
