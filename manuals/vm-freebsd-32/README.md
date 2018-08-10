
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
	- [FreeBSD-Plattenabbild unter Linux einhägen](#fbsd-plattenabbild-linux)
	- [Links](#links-1)

<!-- /MarkdownTOC -->

<a id="vorbereitung-und-installation-von-freebsd-unter-qemu"></a>
# Vorbereitung und Installation von FreeBSD unter QEMU

<a id="vorbereitung"></a>
## Vorbereitung

Zunächst laden wir ein Installer-Image von [freebsd.org/where.html](https://www.freebsd.org/where.html)
herunter. Wir verwenden das DVD-Image für [FreeBSD 11.2](https://download.freebsd.org/ftp/releases/i386/i386/ISO-IMAGES/11.2/FreeBSD-11.2-RELEASE-i386-dvd1.iso) für i386.

Danach erstellen wir entsprechend der geladenen Version ein VM-Image:

```qemu-img create -f raw freebsd.img 20G```

QEMU kann manuel gestartet werden:

```qemu-system-x86_64 -smp 1 -boot c -cdrom /path/to/FreeBSD-11.2-RELEASE-i386-dvd1.iso -m 512 -hda /path/to/freebsd.img```

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

Anschließend partitionieren wir unser VM-Image. Auch wenn `UFS` eher für 
eine VM zu empfehlen ist, da `ZFS` wesentlich mehr RAM und Rechenleistung benötigt, ist hier `ZFS` zu wählen!
Wird `ZFS` eingesetzt, kann man das Image im Zweifel einfach unter Linux einhängen und bearbeiten. Mit `UFS` geht das nicht ohne Weiteres.
Wenn geht es auch nur lesend. Die Unterstützung für schreibenden Zugriff ist noch sehr experimentell.

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
```local_unbound``` ist [a secure, lightweight and high performance validating, recursive, and caching DNS resolver. It performs DNSSEC validation and it is also really easy to configure](http://hauweele.net/~gawen/blog/?tag=local_unbound)

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
# pkg install vim mosh byobu git bash linux_base-c7-7.4.1708_6 sysbench-1.0.14 sudo
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

Da manche Programme unter FreeBSD an anderer Stelle im Dateisystem als unter Linux liegen, erstellen wir Symlinks, damit dasselbe Benchmark-Skript nutzen können.

```
# ln -s /usr/local/bin/bash /bin/
# ln -s /usr/local/bin/sysbench /usr/bin/
```
Für den Linux-Kompatibilitätslayer sind Linux-spezifische Dateisysteme erforderlich. Diese müssen in der `/etc/fstab` eingetragen werden:

```
linproc         /compat/linux/proc  linprocfs  rw,late  0       0
linsysfs        /compat/linux/sys   linsysfs        rw      0       0
tmpfs           /compat/linux/dev/shm  tmpfs   rw,mode=1777    0       0
```

Die dafür nötigen Kernel-Module sollten automatisch geladen werden.

<a id="installation-des-init-skripts"></a>
## Installation des Init-Skripts

Die Skripte finden sich im tools-Repo unter `manuals/vm-freebsd-32/scripts`. Diese müssen in die VM nach `/lockdoc` kopiert werden.
Anschließend muss das neue Init-Skript im Bootloader vermerkt werden. Hierzu muss folgende Zeile in die Datei `/boot/loader.conf` eingetragen werden:
Da das ZFS-Dateisystem für `/home` einen separaten Pool anlegt, der zum Startzeitpunkt noch nicht eingehängt ist, muss das Init-Skript im root-Dateisystem liegen.

```
init_script="/lockdoc/boot.init.sh"
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
Der Quellcode findet sich unter `https://github.com/linux-test-project/ltp.git`. Aktuell setzen wir Revision `5f8ca6cf` ein.
Mit Hilfe des Linux-Kompatibilitätslayers laufen die Programme aus dem LTP problemlos unter FreeBSD.
Allerdings müssen sie unter Linux übersetzt und in das Verzeichnis `/opt/kernel/ltp-bin` installiert werden. Vor dem Übersetzen ist noch der Patch `ltp-lockdebug.patch` aus `manuals/vm-linux-32/` anzuwenden.
Das Verzeichnis kann man anschließend an dieselbe Position in die FreeBSD-VM kopieren.

<a id="konfiguration-und-%C3%9Cbersetzen-des-freebsd-kernels"></a>
## Konfiguration und Übersetzen des FreeBSD Kernels
Zuerst muss unsere eigene Version des FreeBSD-Trees ausgechecked werden. Wir verwenden einen bestimmten Branch.

```
git clone git@gitos.cs.tu-dortmund.de:lockdoc/freebsd.git -b releng/11.2-lockdoc /opt/kernel/freebsd/src
```
** Achtung: ** Bevor irgendein selbstgebauter Kernel installiert wird, sollte der Standard-Kernel gesichert werden:
```
sudo cp -r /boot/kernel /boot/kernel.bak
```
Bei einem `make install` wird der aktuelle Kernel nach `/boot/kernel.old` kopiert und der neue Kernel in `/boot/kernel` installiert.
Zusätzlich kann man in `/boot/loader.conf` in der Zeile `kernels="kernel kernel.old` noch `kernel.bak` hinzufügen, damit der Standard-Kernel direkt über den Bootloader erreichbar ist.
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
# cd /opt/kernel/freebsd/src/conf
# config -d /opt/kernel/freebsd/obj -I `pwd` `pwd`/LOCKDOC
# cd /$OBJDIR
# MODULES_OVERRIDE="" make [-j X]
# sudo -E MODULES_OVERRIDE="" make install
```
Es ist wichtig, die Variable `MODULES_OVERRIDE=""` zu setzen. Nur so wird verhindert, dass alle Module gebaut werden - was das Standard-Verhalten ist.
Effektiv werden gar keine separaten Kernel-Module gebaut. Alle erforderlichen Treiber und co. werden über die Konfiguration in das Kernel-Image gelinkt.

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

<a id="fbsd-plattenabbild-linux"></a>
### FreeBSD-Plattenabbild unter Linux einhägen
```
# kpartx -a /path/to/image.img
# zpool import -d /dev/mapper			# nach ZFS-Pools scannen
# zpool import -R /mnt -N <name>		# entsprechenden ZFS-Pool einbinden
# zfs mount <name>/ROOT/default			# rootfs unter /mnt/ eingehängen
# zfs mount -a							# alle weiteren Datasets einhängen
# zfs umount -a							# alles aushängen
# zpool export <name>					# ZFS-Pool entfernen
# kpartx -d /path/to/image.img
```

<a id="links-1"></a>
## Links

- http://web.archive.org/web/20180602150338/https://www.freebsd.org/doc/handbook/kernelconfig-config.html ausführliche Anleitung zur Konfiguration des Kernels.
- http://web.archive.org/web/20180602152745/https://www.freebsd.org/doc/handbook/kernelconfig-building.html ausführliche Anleitung zur Übersetzung des Kernels.

Written by Daniel Korner 2018; extended by Alexander Lochmann 2018