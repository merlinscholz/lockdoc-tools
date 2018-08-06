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

VM
==
- Zwei serielle Konsolen
	Die *erste* serielle Schnittstelle: VM --> FAIL*-Experiment
	Die *zweite* serielle Schnittstelle: FAIL*-Experiment --> VM (Dienst nur zum Mitteilen des auszuführenden Benchmarks)
