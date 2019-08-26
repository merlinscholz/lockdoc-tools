
<!-- MarkdownTOC -->

- [Vorbereitung und Installation von FreeBSD unter QEMU](#vorbereitung-und-installation-von-freebsd-unter-qemu)
	- [Vorbereitung](#vorbereitung)
	- [Installation](#installation)
		- [Booten](#booten)
		- [Beginn der Installation](#beginn-der-installation)
		- [Keyboard konfiguration](#keyboard-konfiguration)
		- [Konfiguration](#konfiguration)
		- [Partitionierung](#partitionierung)
		- [Eigentliche Installation](#eigentliche-installation)
		- [Root-Passwort](#root-passwort)
		- [Netzwerk](#netzwerk)
		- [Zeiteinstellung](#zeiteinstellung)
		- [Services](#services)
		- [Hardening](#hardening)
		- [Weitere Benutzer](#weitere-benutzer)
		- [Abschluss](#abschluss)
		- [Fertiges OS](#fertiges-os)
		- [Links](#links)
- [FreeBSD für LockDoc vorbereiten](#freebsd-f%C3%BCr-lockdoc-vorbereiten)
	- [Erforderliche Pakete installieren](#erforderliche-pakete-installieren)
	- [Userland einrichten](#userland-einrichten)
	- [Installation des Init-Skripts](#installation-des-init-skripts)
	- [Installation des Benchmark-Skripts](#installation-des-benchmark-skripts)
	- [Installation der Benchmark-Tools](#installation-der-benchmark-tools)
	- [Konfiguration und Übersetzen des FreeBSD Kernels](#konfiguration-und-%C3%9Cbersetzen-des-freebsd-kernels)
		- [Konfiguration](#konfiguration-1)
		- [Übersetzen außerhalb des Source-Trees](#%C3%9Cbersetzen-au%C3%9Ferhalb-des-source-trees)
		- [Übersetzen im Source-Tree](#%C3%9Cbersetzen-im-source-tree)
	- [Links](#links-1)
- [Code-Abdeckung](#freebas-code-abdeckung)
	- [Vorbereitung](#freebsd-code-abdeckung-vorbereitung)
	- [Code-Abdeckung bestimmen - GCOV](#freebsd-code-abdeckung-bestimmen-gcov)
	- [Code-Abdeckung bestimmen - KCOV](#freebsd-code-abdeckung-bestimmen-kcov)
	- [Links](#freebsd-code-abdeckung-links)

<!-- /MarkdownTOC -->

<a id="vorbereitung-und-installation-von-freebsd-unter-qemu"></a>
# Vorbereitung und Installation von FreeBSD unter QEMU

<a id="vorbereitung"></a>
## Vorbereitung

Zunächst laden wir ein Installer-Image von [freebsd.org/where.html](https://www.freebsd.org/where.html)
herunter. Wir verwenden das DVD-Image für [FreeBSD 11.2](https://download.freebsd.org/ftp/releases/i386/i386/ISO-IMAGES/12.0/FreeBSD-12.0-RELEASE-i386-dvd1.iso) für i386.

Danach erstellen wir entsprechend der geladenen Version ein VM-Image:

```qemu-img create -f raw freebsd.img 25G```

QEMU kann manuel gestartet werden:

```qemu-system-x86_64 -smp 1 -boot c -cdrom /path/to/FreeBSD-12.0-RELEASE-i386-dvd1.iso -m 512 -hda /path/to/freebsd.img```

Alternativ kann man die virtuelle Maschine via Virt-Manager erstellen.

<a id="installation"></a>
## Installation

<a id="booten"></a>
### Booten

Wir booten das Installer-Image in den gewünschten Modus. Der Standardfall ist dabei 
`multi user`. Zu den Modis siehe auch 
[FreeBSD Handbook](https://www.freebsd.org/doc/handbook/boot-introduction.html#boot-singleuser).

![alt text](./img/install-01.png)

<a id="beginn-der-installation"></a>
### Beginn der Installation

Danach wählen wir ``Install`` aus, um die Installation zu beginnen.

![alt text](./img/install-02.png)

<a id="keyboard-konfiguration"></a>
### Keyboard konfiguration

Konfiguration des Tastatur Layouts (Default ist US).

![alt text](./img/install-03.png)

Wir haben *German* gewählt.

![alt text](./img/install-04.png)

<a id="konfiguration"></a>
### Konfiguration

Danach geben wir der Maschine einen Namen.

![alt text](./img/install-05.png)

Und haben nun die Auswahl darüber, was installiert werden soll. Entgegen des Screenshots ist das Paket `src` nicht erforderlich, da wir unseren eigenen FreeBSD-Tree nutzen.
Ansonsten ist noch die Option `ports` zu erwähnen, die eine Ansammlung 
von Scripten entspricht, die automatisiert Software (z.B. X11), die nicht direkt zu FreeBSD 
gehört, laden, übersetzt und installiert. Der Rest sollte selbsterklärend sein.

Die Komponenten lassen sich auch alle nachträglich installieren.

![alt text](./img/install-06.png)

<a id="partitionierung"></a>
### Partitionierung

Anschließend partitionieren wir unser VM-Image. Hier wird `UFS`  als Dateisystem gewählt.

![alt text](./img/install-07.png)
![alt text](./img/install-08.png)
![alt text](./img/install-09.png)
![alt text](./img/install-10.png)
![alt text](./img/install-11.png)

<a id="eigentliche-installation"></a>
### Eigentliche Installation

Jetzt kopiert und entpackt der Installer die Daten von dem Installer-Image auf das VM-Image. 
Das kann je nach Maschine und Installer-Image 3-10 Minuten dauern.

![alt text](./img/install-12.png)

<a id="root-passwort"></a>
### Root-Passwort

Jetzt noch das root-Passwort festlegen und wir sind fast fertig.

![alt text](./img/install-15.png)

<a id="netzwerk"></a>
### Netzwerk

Entsprechende Netzwerk Einstellungen sind nur notwendig, falls Software 
(z.B. ```vim```) nachinstalliert werden soll oder falls für die eigentliche 
Aufgabe Netzwerk notwendig ist.

![alt text](./img/install-16.png)
![alt text](./img/install-17.png)
![alt text](./img/install-18.png)
![alt text](./img/install-19.png)

IPv6 können wir überspringen, da die Uni (und das ITMC) eh nicht daran glauben.

![alt text](./img/install-20.png)

Die DNS Einstellungen kann man auf den voreingestellten Werten belassen.

![alt text](./img/install-21.png)

<a id="zeiteinstellung"></a>
### Zeiteinstellung

![alt text](./img/install-22.png)
![alt text](./img/install-23.png)
![alt text](./img/install-24.png)
![alt text](./img/install-25.png)

Falls notwendig setzen des korrekten Datums und Uhrzeit.

![alt text](./img/install-26.png)
![alt text](./img/install-27.png)

<a id="services"></a>
### Services

Hier kann der Default auch belassen werden.
```sshd``` ist natürlich empfehlenswert.

![alt text](./img/install-28.png)

<a id="hardening"></a>
### Hardening

![alt text](./img/install-29.png)

<a id="weitere-benutzer"></a>
### Weitere Benutzer

Hier sollte direkt ein Nutzer angelegt werden.
Analog zu Linux können wir später jeder Zeit neue Nutzer mit `adduser` anlegen.

![alt text](./img/install-30.png)

<a id="abschluss"></a>
### Abschluss

Im Anschluss haben wir noch einmal die Möglichkeit diverse Einstellungen zu ändern.
![alt text](./img/install-31.png)

Außerdem erhalten wir noch die Option vor dem Neustart mit einer Shell im neuen System eigene Änderungen vorzunehmen.
Hier empfhielt es sich, für die erstellten Partitionen bzw. Slices Labels zu vergeben. So kann man in QEMU z.B. den VirtIO-Treiber für die Festplattenemulation nutzen und gleichzeitig in BOCHS eine IDE-Platte emulieren.
Wichtig ist, dass in diesem Schritt **nur** die Labels vergeben werden. Die Änderungen an der ```/etc/fstab``` sollten später **nach** einem Neutstart erfolgen.
```
glabel label swap /dev/XXX
tunefs -L roots /dev/XXX
```
**Achtung:** Das Tool `glabel`  darf **nur** für Partitionen, die kein Dateisystem beinhalten, wie z.B. die swap-Partition, genutzt werden. Für Dateisysteme, wie z.B. UFS, ist `tunefs`  zu nehmen.
![alt text](./img/install-32.png)
![alt text](./img/install-33.png)
![alt text](./img/install-34.png)

<a id="fertiges-os"></a>
### Fertiges OS

Nach dem Neustart können wir uns anmelden:

![alt text](./img/install-35.png)
![alt text](./img/install-36.png)
![alt text](./img/install-37.png)


<a id="links"></a>
### Links

Nützliche Links:

- http://web.archive.org/web/20180602143516/https://www.freebsd.org/doc/handbook/bsdinstall-start.html
Handbuch zu installation von FreeBSD (Achtung: Die Bilder sind etwas veraltert.).
- http://web.archive.org/web/20180602143507/https://www.freebsd.org/doc/handbook/svn.html Anleitung Komponenten wie **ports** oder **src** nachinstalliert.

<a id="freebsd-f%C3%BCr-lockdoc-vorbereiten"></a>
# FreeBSD für LockDoc vorbereiten
Damit LockDoc mit FreeBSD funktioniert, müssen noch ein paar Skripte und Programme installiert sowie Einstellungen vorgenommen werden.

<a id="erforderliche-pakete-installieren"></a>
## Erforderliche Pakete installieren
Mit dem folgenden Befehl lassen sich als **root** alle notwendigen Programme installieren.

```
# pkg install <paketname>
```

Für unser Setup wurden u.a. folgende Pakete installiert:

```
# pkg install vim mosh tmux git bash linux_base-c7-7.4.1708_6 sysbench-1.0.15 sudo clang80 gcc7-7.4.0_1
```

**Achtung**
Ggf. muss der Versions-String für einzelne Pakete angepasst werden.


<a id="userland-einrichten"></a>
## Userland einrichten
Analog zu Linux kann man einem Nutzer ermöglichen, root-Rechte zu erlangen. Hierzu muss der Nutzer Mitglied der Gruppe `wheel` sein.

```
pw groupmod -m al -n wheel
```
In `/usr/local/etc/sudoers` muss nun die Zeile `%wheel ALL=(ALL) ALL` auskommentiert werden.

Möchte man die Bash als Standardshell setzen, geht dies mit folgendem Befehl:
```
chsh -s /usr/local/bin/bash
```
Es empfiehlt sich dies ebenfalls als `root` zu tun.

Da manche Programme unter FreeBSD an anderer Stelle im Dateisystem als unter Linux liegen, erstellen wir Symlinks, damit dasselbe Benchmark-Skript nutzen können.

```
# ln -s /usr/local/bin/bash /bin/
# ln -s /usr/local/bin/sysbench /usr/bin/
# ln -s gcov7 /usr/local/bin/gcov
```
Für den Linux-Kompatibilitätslayer sind Linux-spezifische Dateisysteme erforderlich. Diese müssen in der `/etc/fstab` eingetragen werden:

```
linproc         /compat/linux/proc  linprocfs  rw,late  0       0
linsysfs        /compat/linux/sys   linsysfs        rw      0       0
tmpfs           /compat/linux/dev/shm  tmpfs   rw,mode=1777    0       0
```

Die dafür nötigen Kernel-Module sollten automatisch geladen werden.
Abschließend muss der Bootloader noch passend konfiguriert werden. Dazu trägt man folgenden Inhalt in ```/boot/loader.conf``` ein:
```
kernel="lockdoc"                        # Set FreeBSD's kernel as default
kernels_autodetect="YES"                # Detect all installed kernels automatically
#console="vidconsole,comconsole"        # Use video or video+com as console
console="vidconsole"
vm.kmem_size="512M"
vm.kmem_size_max="512M"
```

Möchte man Labels statt absolute Pfade für die Dateisystem und das root-Device nutzen, muss folgende Zeile in ```/boot/loader.conf```  eingetragen werden:
```
vfs.root.mountfrom="ufs:ufs/rootfs"     # Use labels to detects the rootfs. Enables easy switching of disk technologies, e.g. scsi, ide, or virtio
```
Außerdem muss die `/etc/fstab` noch angepasst werden. Dies kann z.B. so aussehen:
```
# Device        Mountpoint      FStype  Options Dump    Pass#
/dev/ufs/rootfs /               ufs     rw      1       1
/dev/label/swap none            swap    sw      0       0
linproc /compat/linux/proc linprocfs rw,late 0 0
linsysfs /compat/linux/sys linsysfs rw 0 0
tmpfs /compat/linux/dev/shm tmpfs rw,mode=1777 0 0
```
Alle mit `glabel` erstellten Label landen unter `/dev/label`. Die mit `tunefs` erstellten Label liegen unter `/dev/ufs/`.

<a id="installation-des-init-skripts"></a>
## Installation des Init-Skripts

Die Skripte finden sich im tools-Repo unter `manuals/vm-freebsd-32/scripts`. Diese müssen in die VM nach `/lockdoc` kopiert werden.
Anschließend muss das neue Init-Skript im Bootloader vermerkt werden. Hierzu muss folgende Zeile in die Datei `/boot/loader.conf` eingetragen werden:
Da das ZFS-Dateisystem für `/home` einen separaten Pool anlegt, der zum Startzeitpunkt noch nicht eingehängt ist, muss das Init-Skript im root-Dateisystem liegen.

```
init_script="/lockdoc/boot.init.sh"     # Hook in our own init script to automatically start the benchmark
```

Ggf. sind die Zeilen, die ebensfalls `init_script` setzen, auszukommentieren.
Jetzt wird bei jedem Start, nachdem der Kernel geladen wurde, als erstes dieses Script ausgeführt.
**Achtung:** Sollte der Nutzer **nicht** `al` lauten, müssen die Init-Skripte angepasst werden.
Sollte der Nutzer beim Startet **nicht** innerhalb von fünf Sekunden einen beliebigen Buchstaben drücken, wird der Benchmark gestartet.
Andernfalls wird das gewöhnliche FreeBSD-Userland gestartet.

<a id="installation-des-benchmark-skripts"></a>
## Installation des Benchmark-Skripts

Das Skript `run-bench.sh` befindet sich im tools-Repo unter `manuals/vm-linux-32/scripts`. Dies muss in der VM nach `/lockdoc` kopiert werden.
Zusätzlich muss das Verzeichnis `/lockdoc/bench-out` angelegt werden und aus `manuals/vm-linux-32/scripts` die Datei `fork.c` dahin kopiert werden.

<a id="installation-der-benchmark-tools"></a>
## Installation der Benchmark-Tools
Sowohl in der Linux- als auch in der FreeBSD-VM verwenden wir ein Subset des Linux-Test-Project (LTP) für unsere Benchmarksuite.
Der Quellcode findet sich unter `https://github.com/linux-test-project/ltp.git`. Aktuell setzen wir Revision `a6a5caef`, Tag/Release `20190115`, ein.
Mit Hilfe des Linux-Kompatibilitätslayers laufen die Programme aus dem LTP problemlos unter FreeBSD.
Allerdings müssen sie unter Linux übersetzt und in das Verzeichnis `/compat/linux/opt/kernel/ltp/bin` installiert werden. Vor dem Übersetzen müssen noch 
die Dateien `{syscalls,fs}-custom` aus `manuals/vm-linux-32/scripts/` nach `$LTPSRC/runtest/` kopiert werden.

<a id="konfiguration-und-%C3%9Cbersetzen-des-freebsd-kernels"></a>
## Konfiguration und Übersetzen des FreeBSD Kernels
Zuerst muss unsere eigene Version des FreeBSD-Trees ausgechecked werden. Wir verwenden einen bestimmten Branch.

```
git clone git@gitos.cs.tu-dortmund.de:lockdoc/freebsd.git -b releng/12.0-lockdoc /opt/kernel/freebsd/src
```
** Achtung: ** Bevor irgendein selbstgebauter Kernel installiert wird, sollte der Standard-Kernel gesichert werden:
```
sudo cp -r /boot/kernel /boot/kernel.ori
```
Bei einem `make install` wird der aktuelle Kernel nach `/boot/kernel.old` kopiert und der neue Kernel in `/boot/kernel` installiert.
<a id="konfiguration-1"></a>
### Konfiguration

Die Konfiguration für LockDoc befindet sich bereits in `/opt/kernel/freebsd/src/sys/i386/conf`. Daher ist nichts weiter zu tun, sofern diese Konfiguration verwendet werden soll.

Alternativ kann auch mit einer Standard-Konfiguration begonnen werden:
```
# cd /opt/kernel/freebsd/src/sys/i386/conf
# cp GENERIC LOCKDOC
```
Danach kann die Konfiguration angepasst werden, um z.B. einige Sub-Systeme zu entfernen.

<a id="%C3%9Cbersetzen-au%C3%9Ferhalb-des-source-trees"></a>
### Übersetzen außerhalb des Source-Trees

Damit wirklich nur der Kernel übersetzt wird und nicht noch Abhängigkeiten aus dem FreeBSD-Tree, kann man mit einer Konfiguration
einen spezialisierten Source-Tree erzeugen und diesen anschließend übersetzen:

```
# cd /opt/kernel/freebsd/src/sys/i386/conf
# config -d /opt/kernel/freebsd/obj -I `pwd` `pwd`/LOCKDOC
# cd /$OBJDIR
# MODULES_OVERRIDE="" LD=ld.lld CC=clang80 make [-j X]
# sudo -E MODULES_OVERRIDE="" KODIR=/boot/lockdoc LD=ld.lld make install
```
Es ist wichtig, die Variable `MODULES_OVERRIDE=""` zu setzen. Nur so wird verhindert, dass alle Module gebaut werden - was das Standard-Verhalten ist.
Effektiv werden gar keine separaten Kernel-Module gebaut. Alle erforderlichen Treiber und co. werden über die Konfiguration in das Kernel-Image gelinkt.
Durch die Variable ```KODIR``` teilt man dem Makefile mit, dass der Kernel in ```/boot/lockdoc``` installiert werden sollen. Nur wenn den Kernel in dieses Verzeichnis installiert, wird er auch automatisch durch den Bootloader ausgewählt (siehe ```kernel="..."``` in ```/boot/loader.conf```).
Sollte beim Übersetzen eine Fehlermeldung (```line 127: amd64/arm64/i386 kernel requires linker ifunc support```) erscheinen, die den Linker nennt, hilft evtl. das Setzen der Variable ```LD=""```: ```MODULES_OVERRIDE=""  LD=ld.lld make [-j X]```.
Sofern der 13.0er Kernel unter FreeBSD 12.0 übersetzt wird, muss clang 8.0 genutzt werden. Dazu wird die Variable CC passend gesetzt.

<a id="%C3%9Cbersetzen-im-source-tree"></a>
### Übersetzen im Source-Tree

Möchte man einfach den Kernel innerhalb des FreeBSD-Trees bauen, genügen folgende Befehle:

```
# cd /usr/src
# make buildkernel KERNCONF=LOCKDOC
# make installkernel KERNCONF=LOCKDOC
```

Außerdem kann mit dem Flag ```-DKERNFAST``` das Übersetzen beschleunigt werden, in
dem nur die Übersetzungseinheiten neu gebaut werden, deren Konfiguration oder Quellcode sich 
geändert hat.

```
# cd /usr/src
# make buildkernel -DKERNFAST KERNCONF=LOCKDOC
# make installkernel KERNCONF=LOCKDOC
```

<a id="links-1"></a>
## Links

- [ausführliche Anleitung zur Konfiguration des Kernels](http://web.archive.org/web/20180602150338/https://www.freebsd.org/doc/handbook/kernelconfig-config.html)
- [ausführliche Anleitung zur Übersetzung des Kernels](http://web.archive.org/web/20180602152745/https://www.freebsd.org/doc/handbook/kernelconfig-building.html)

<a id="freebas-code-abdeckung"</a>
# Code-Abdeckung
<a id="freebsd-code-abdeckung-vorbereitung"></a>
## Vorbereitung
Zunächst müssen die passenden Kernel gebaut werden.
Um einen Kernel mit KCOV-Unterstützung zu bauen, sind folgende Befehle nötig:
```
# cd /opt/kernel/freebsd/src/sys/i386/conf
# config -d /opt/kernel/freebsd/obj-kcov -I `pwd` `pwd`/LOCKDOC_KCOV
# MK_FORMAT_EXTENSION=no MODULES_OVERRIDE="" LD=ld.lld CC=gcc7 COMPILER_TYPE=gcc make [-j X]
# sudo -E MODULES_OVERRIDE="" KODIR=/boot/lockdoc-kcov LD=ld.lld make install
```
Um einen Kernel mit GCOV-Unterstützung zu bauen, sind folgende Befehle nötig:
```
# cd /opt/kernel/freebsd/src/sys/i386/conf
# config -d /opt/kernel/freebsd/obj-gcov -I `pwd` `pwd`/LOCKDOC_GOV
# MK_FORMAT_EXTENSION=no MODULES_OVERRIDE="" LD=ld.lld CC=gcc7 COMPILER_TYPE=gcc make [-j X]
# sudo -E MODULES_OVERRIDE="" KODIR=/boot/lockdoc-gcov LD=ld.lld make install
```
Außerdem müssen die folgenden zwei Headerdateien in das System-Include-Verzeichnis kopiert werden, damit `kcovtrace` übersetzt werden kann:
```
# cp /opt/kernel/freebsd/src/sys/sys/kcov.h /usr/include/sys/
# cp /opt/kernel/freebsd/src/sys/sys/coverage.h /usr/include/sys/
# clang80 -o kcovtrace kcovtrace.c
```
Das Übersetzen mit dem `clang` geht nur, wenn es nicht für i386 übersetzt wird. Ansonsten muss der GCC genommen werden:
```
# gcc7 -march=i586 -o kcovtrace kcovtrace.c
```
Achuntg: Damit das Setzen der Umgebungsvariable, wie unten, korrekt funktioniert sollte als Standardshell für `root` die Bash eingestellt sein.
<a id="freebsd-code-abdeckung-bestimmen-gcov"></a>
## Code-Abdeckung bestimmen - GCOV
Zunächst das Linux-DebugFS einhängen:
```
# mount -t debugfs debugfs /mnt
```
GCOV muss zunaechst mindestens einmal aktiviert werden. Andernfalls existiert das Verzeichnis `/mnt/gcov` nicht und das Skript `gcov-trace.sh` bricht ab.
```
# sysctl debug.gcov.enable=1
```
Die Variable `GATHER_COV` sorgt dafür, dass das Skript `run-bench.sh` gewisse Initialisierungsbefehle auslässt.
Der erste Parameter von `./gcov-trace.sh` gibt an, wo der Kernel übersetzt wurde - hier `/opt/kernel/freebsd/obj-gcov/`.
Der dritte Parameter gibt den Namen der Ausgabedatei an.
```
# GATHER_COV=1 GCOV_DIR=/mnt/gcov ./gcov-trace.sh /opt/kernel/freebsd/obj-gcov/ test /lockdoc/run_bench.sh <benchmark>
```
<a id="freebsd-code-abdeckung-bestimmen-kcov"></a>
## Code-Abdeckung bestimmen - KCOV
`kcovtrace` muss als `root` ausgeführt werden.
```
# GATHER_COV=1 ./kcovtrace /lockdoc/run_bench.sh <benchmark> 2> pcs.txt
```
<a id="freebsd-code-abdeckung-links"></a>
## Links
- [GCOV on Linux](https://01.org/linuxgraphics/gfx-docs/drm/dev-tools/gcov.html#)
- [Gather GCOV files on test machine](https://01.org/linuxgraphics/gfx-docs/drm/dev-tools/gcov.html#appendix-b-gather-on-test-sh)

Written by Daniel Korner 2018; extended by Alexander Lochmann 2018
