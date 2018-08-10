1. Eine Debian X VM für i386 aufsetzen
2. Kernel-Repo nach /opt/kernel/ auschecken
	git clone ssh://ios.cs.tu-dortmund.de/fs/staff/al/repos/lockdebugging/linux ssh://ios.cs.tu-dortmund.de/fs/staff/al/repos/lockdebugging/linux
	git checkout -b lockdebugging-4-10
3. Kernel bauen und installieren(v4.10)
	cp config-lockdebugging .config
	make oldconfig
	make -j X
	make install
4. Kernel sichern (v4.10)
	Das *optionale* Suffix kann genutzt werden, um eine spezielle Variante zu identifiezieren.
	Das Kernelimage liegt anschließend in /fs/scratch/al/coccinelle/experiment/ und heißt: vmlinux-4-10-nococci-$DATE-$GIT-HASH$SUFFIX
	./copy-to-ios.sh <suffix>
5. Grub für den Autostart des Benchmarks vorbereiten
	- In /etc/default/grub die Variable GRUB_DEFAULT auf saved setzen.
	- In /etc/grub.d/40_custom Folgendes eintragen:
menuentry 'LockDoc-4.10-al' --class debian --class gnu-linux --class gnu --class os $menuentry_id_option 'lockdoc-4.10.0-al+' {
        load_video
        insmod gzio
        if [ x$grub_platform = xxen ]; then insmod xzio; insmod lzopio; fi
        insmod part_msdos
        insmod ext2
        set root='hd0,msdos1'
        if [ x$feature_platform_search_hint = xy ]; then
          search --no-floppy --fs-uuid --set=root --hint-bios=hd0,msdos1 --hint-efi=hd0,msdos1 --hint-baremetal=ahci0,msdos1  a6a47d48-af9e-4c65-b249-ac1fad11cd2b
        else
          search --no-floppy --fs-uuid --set=root a6a47d48-af9e-4c65-b249-ac1fad11cd2b
        fi
        echo    'Loading Linux 4.10.0-al+ ...'
        linux   /boot/vmlinuz-4.10.0-al+ root=UUID=a6a47d48-af9e-4c65-b249-ac1fad11cd2b ro quiet loglevel=0 init=/lockdoc/run-bench.sh
        echo    'Loading initial ramdisk ...'
        initrd  /boot/initrd.img-4.10.0-al+
}
	- Ggf. sind die Pfade zu dem Benchmark-Skript (siehe init=) anzupassen.
	- Den o.g. Eintrag als Standard setzen
		grub-set-default "lockdoc-4.10.0-al+"
	- Alle Änderungen aktivieren:
		update-grub
	- Achtung: Ab sofort muss bei einem Neustart im Grub-Menü der entsprechende Eintrag für das gewöhnliche Userland ausgewählt werden. Ansonsten startet automatisch der Benchmark.
6. Benchmark-Skript installieren
	- Aus dem tools-Repo aus manuals/vm-linux-32/scripts/ das Skript 'run-bench.sh' in das Home des Nutzer schieben.
	- Sollte der Nutzer *nicht* al heißen, muss der o.g. Pfad zum Init-Skript aktualisiert werden.
7. Benchmark-Suite vorbereiten
	- git clone  https://github.com/linux-test-project/ltp.git /opt/kernel/ltp/src 
	- git checkout -b lockdoc-ltp 5f8ca6cf
	- Den Patch aus dem tools-Repo aus manuals/vm-linux-32/ anwenden
		patch -p1 < ltp-lockdebug.patch
	- ./configure --prefix=/opt/kernel/ltp/bin/ && make && make install
