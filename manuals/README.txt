Kernel
======

- Repo: ssh://ios.cs.tu-dortmund.de/fs/staff/al/repos/lockdebugging/linux
- Branches: lockdebugging-{3-16,4-10}
- Default Config liegt in root-Verzeichnis: config-lockdebugging bzw. default-config
- Nach einem neu Bauen des Kernels:
	* installieren: make install (modules_install is nicht nötig, da es ein statischer Kernel ist)
	* VMLINUX sichern: scp vmlinux ios:/fs/scratch/al/coccinelle/tools/data/vmlinux-4-10-nococci-20170402 (vmlinux-<Kernel-Version>-<mit oder ohne Coccinelle>-<Datum>)


Benchmarks:
===========
Die Benchmark-Namen können direkt als Parameter an den FAIL-Client übergeben werden.

- sysbench
	Parameter für Sysbench: --test=fileio --file-total-size=2G --file-num=20 --file-test-mode=rndrw
- mixed-fs
	Subset der Benchmarks aus dem Linux-Test-Project
- lockdoc-test
	Führt den LockDoc-Test aus. Dabei wird der Code mit der richtigen Lock-Reihenfolge CONFIG_ESS_LOCK_ANALYSIS_TEST_ITERATIONS-mal ausgeführt.
	Dieser Wert kann über die Kernel-Konfig verändert werden.
	Alternativ kann man die Anzahl der Iterationen auch über den Benchmark spezifizieren: lockdoc-test-100.
	Hierbei ist zu beachten, dass die Anzahl *nicht* größer sein darf als CONFIG_ESS_LOCK_ANALYSIS_TEST_ITERATIONS. Ansonsten fällt der Test automatisch
	auf den konfigurierten Wert zurück.
	CONFIG_ESS_LOCK_ANALYSIS_TEST_ITERATIONS wird gleichzeitig verwendet, um die Größe des Ringpuffer zu bestimmen. Ist die Anzahl der Iterationen größer als der Ringpuffer,
	würde der Ringpuffer voll- bzw. leerlaufen. Hierdurch würden andere Codepfade ausgeführt.
	Der Ringpuffer muss in die beobachtete Datenstruktur eingebettet sein, damit unsere Analyse korrekt funktioniert. Daher kann er nicht dynamisch zur Laufzeit alloziert werden.

Datenbanken
===========

- Namensschema: lockdebugging_<Benchmark>_<Kernel-Version>_<mit oder ohne Cocci>


Fail
====

- ./fail-client -Wf,--benchmark=mixed-fs -Wf,--port=<port> -Wf,--vmlinux=/path/to/vmlinux -q -f <bochsrc> 2>&1 | tee out.txt
	Beispiel: ./fail-client -Wf,--benchmark=mixed-fs -Wf,--port=4711 -Wf,--vmlinux=/fs/scratch/al/coccinelle/experiment/vmlinux-4-10-nococci-20180717-g029f74393479-dirty-grub -q -f bochsrc-4-10-testing-4711-al 2>&1 | tee out.txt
- Hinweis für BOCHS:
	- Je nach Festplatten-Image muss die Geometrie in der BOCHS-Konfig angepasst werden:
		Adjust cylinders to your image size: cylinders = imageSize / (heads * spt * 512)
		For more details have a look at $BOCHS/iodev/harddrv.c:{288-291,347}
	- Passiert das nicht, erscheint folgende Fehlermeldung in der Datei bochsout.txt: "00000000000p[HD   ] >>PANIC<< ata0-0 disk size doesn't match specified geometry"
	  BOCHS bricht die Ausführung deshalb aber *nicht* ab, stattdessen wird keine Festplatte emuliert.
- Empfohlene Einstellungen:
	- Linux-Image (25G):	ata0-master: type=disk, mode=volatile, path="$virtuos-vms/lockdoc-fbsd.img", cylinders=102400, heads=16, spt=32, biosdetect=auto
	- FreeBSD-Image (26GB): ata0-master: type=disk, mode=volatile, path="$virtuos-vms/lock-debugging.img", cylinders=163840, heads=16, spt=32, biosdetect=auto
		($virtuos-vms = virtuos:/home/vms)
	- Auch wenn es sinnvoll erscheinen mag, die Systemzeit auf eine bestimmte Uhrzeit bzw. ein bestimmtes Datum festzulegen, um ein deterministisches Verhalten zu erzeugen, ist es das nicht.
	  Da das Plattenabbild für Veränderungen ggf. in QEMU benutzt wird, werden alle Zeitstempel, die irgendwo im Dateisystem gesetzt werden, neuer sein als das gesetzte Datum.
	  Dadurch wird beim Start immer ein Dateisystemcheck ausgelöst. Daher: clock: sync=none, time0=local

VM
==
- Zwei serielle Konsolen
	Die *erste* serielle Schnittstelle: VM --> FAIL*-Experiment
	Die *zweite* serielle Schnittstelle: FAIL*-Experiment --> VM (Dienst nur zum Mitteilen des auszuführenden Benchmarks)
