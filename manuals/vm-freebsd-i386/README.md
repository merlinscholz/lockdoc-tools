# LockDoc Experiments on FreeBSD

⚠️ This README is a rewrite/fixup/combination of older docs. The pre-rewrite version can be found at commit fda44cd05e731c47585cdda490a12d143c0f500b: https://git.cs.tu-dortmund.de/LockDoc/tools/src/commit/fda44cd05e731c47585cdda490a12d143c0f500b/manuals/vm-freebsd-32/README.md

⚠️ This README uses FreeBSD 12. The provided LockDoc kernel source code currently does not successfully compile on FreeBSD 13.

1. Set up an **i386** VM (qemu/libvirt or VirtualBox) for the FreeBSD installation.
    * Disk size: 25G
    * Disk type: RAW `qemu-img create -f raw freebsd.img 25G` (if possible. On VirtualBox the image has to be converted later)
    * Working networking
2. Install FreeBSD 12.3 with the [FreeBSD-12.3-RELEASE-i386-disc1.iso](https://download.freebsd.org/releases/i386/i386/ISO-IMAGES/12.3/FreeBSD-12.3-RELEASE-i386-disc1.iso) installation medium.
    * Choose UFS as file system
    * We don't need any extra components apart from `base` and `kernel` (not even their `-dbg` counterparts)
    * We don't need any users apart from root
    * Enabling SSH is highly recommended
3. Reboot into the VM and make basic preparations:
    * Edit the SSH config to allow root to ssh into the VM (useful for transferring files in the future):
        ```sh
            echo "PermitRootLogin yes" >> /etc/ssh/sshd_config
            /etc/rc.d/sshd restart
        ```
    * Enable Linux emulation layer:
        ```sh
        echo "linux_enable=YES" >> /etc/rc.conf
        ```
    * Install required packages:
        ```sh
        pkg install nano git bash sysbench sudo e2fsprogs gcc gmake debootstrap rsync tmux
        ```
    * Fix paths and shell for `run-bench.sh`:
        ```sh
        ln -s /usr/local/bin/bash /bin/bash
        ln -s /usr/local/bin/sysbench /usr/bin/sysbench
        chsh -s /usr/local/bin/bash
        ```
4. Install & configure Linux subsystem:
    
    ⚠️ We use `/compat/ubuntu` instead of `/compat/{debian,buster,linux}` as to not break compatibility with the LockDoc `run-bench.sh` scripts

    * Install the file system:
        ```sh
        debootstrap buster /compat/ubuntu
        ```
        (Ignore the warning about the missing mounts, those will come next)
    * Modify `/etc/fstab`; append the following:
        ```
        devfs           /compat/ubuntu/dev      devfs           rw,late                      0       0
        tmpfs           /compat/ubuntu/dev/shm  tmpfs           rw,late,size=1g,mode=1777    0       0
        fdescfs         /compat/ubuntu/dev/fd   fdescfs         rw,late,linrdlnk             0       0
        linprocfs       /compat/ubuntu/proc     linprocfs       rw,late                      0       0
        linsysfs        /compat/ubuntu/sys      linsysfs        rw,late                      0       0
        ```
    * Reboot to apply and enable Linux compatibility module
    * Chroot into the Linux Compat folder and install neccesary packages:
        ```sh
        chroot /compat/ubuntu /bin/bash
        apt update
        apt purge rsyslog # This package has issues when running in the FreeBSD compatiblity environment
        apt install dosfstools e2fsprogs build-essential git-core autoconf nano util-linux automake pkg-config
        ```
    * Clone and build LTP (still in chroot):
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
    * Bootloader `/boot/loader.conf`: TODO DISK LABEL
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

At this point, upon rebooting the VM, it should automatically start the benchmark **when the kernel is installed** (and unless a key is pressed when prompted to do so). The image can now be used to run the LockDoc experiments in FAIL*/bochs. If you are using VirtualBox (or another virtualization software that doesn't support raw disk images), you have to convert the disk image before using fail*. On the VM host: ```qemu-img convert -f qcow2 -O raw freebsd.qcow2 freebsd.raw```

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