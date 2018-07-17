
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
		- [Root Passwort](#root-passwort)
		- [Netzwerk](#netzwerk)
		- [Zeiteinstellung](#zeiteinstellung)
		- [Services](#services)
		- [Hardening](#hardening)
		- [Weitere Benutzer](#weitere-benutzer)
		- [Abschluss](#abschluss)
		- [Fertiges OS](#fertiges-os)
		- [Update to 11.2-RELEASE](#update-to-112-release)
		- [Links](#links)
- [Installation von Programmen](#installation-von-programmen)
	- [Programme updaten](#programme-updaten)
- [Konfiguration und Kompilation vom FreeBSD Kernel](#konfiguration-und-kompilation-vom-freebsd-kernel)
	- [Konfiguration](#konfiguration-1)
	- [Kompilation](#kompilation)
	- [Links](#links-1)
- [Installation vom Init-Script](#installation-vom-init-script)
	- [Vorbeschreibung](#vorbeschreibung)
	- [Kopierer der Scripte](#kopierer-der-scripte)
	- [Anpassen des Bootloaders](#anpassen-des-bootloaders)

<!-- /MarkdownTOC -->

<a id="vorbereitung-und-installation-von-freebsd-unter-qemu"></a>
# Vorbereitung und Installation von FreeBSD unter QEMU

<a id="vorbereitung"></a>
## Vorbereitung

Zunächst laden wir ein **Installer Images** von [freebsd.org/where.html](https://www.freebsd.org/where.html)
herunter. In meinem Beispiele habe ich die Version [11.2-RC1](https://download.freebsd.org/ftp/releases/amd64/amd64/ISO-IMAGES/11.2/FreeBSD-11.2-RC1-amd64-dvd1.iso) als DVD für amd64 herrunter geladen.

Danach erstellen wir entsprechend der geladenen Version ein VM Image. In dem Beispiel
für die Version 11.2-RC1 und mit einer größe von 10GB via

```qemu-img create freebsd.qcow2 10G```


Danach gehen wir in den *failqemu* Ordner (bauen von failqemu nicht vergessen!) 
und starten die VM mit dem installer images und dem VM Image via

```./x86_64-softmmu/qemu-system-x86_64 -smp 1 -boot c -cdrom /path/to/$InstallerImage.iso -m 512 -hda /path/to/VMImage.qcow2```

**Achtung** es empfiehlt sich für die Installation alle Watchpoints auszuschalten 
als auch die Ausgaben ```printf``` ausgaben in ```failqemu.c fail_io(...)```, 
da ansonsten die Installation ziemlich lange dauert.

<a id="installation"></a>
## Installation

<a id="booten"></a>
### Booten

Wir booten das installer image in den gewünschten Modus. Der default ist dabei 
*multi user*. Zu den Modis siehe auch 
[FreeBSD Handbook](https://www.freebsd.org/doc/handbook/boot-introduction.html#boot-singleuser).

![alt text](./img/install-01.png)

<a id="beginn-der-installation"></a>
### Beginn der Installation

Danach wählen wir ``Install`` aus um die Installation zu beginnen.

![alt text](./img/install-02.png)

<a id="keyboard-konfiguration"></a>
### Keyboard konfiguration

Konfiguration des Tastatur Layouts (Default ist US).

![alt text](./img/install-03.png)

Ich habe *German* gewählt.

![alt text](./img/install-04.png)

<a id="konfiguration"></a>
### Konfiguration

Danach geben wir der installation einen Namen.

![alt text](./img/install-05.png)

Und haben nun die Auswahl darüber, was installiert werden soll. Hier wählen wir zusätzlich 
**src** aus um im Anschluss einfacher einen eigenen Kernel configurierungen und kompilieren 
zu könnten. Ansonsten ist noch die Option **ports** zu erwähnen, die eine Ansammlung 
von Scripten entspricht die automatisiert Software (z.B. X11) die nicht direkt zu FreeBSD 
gehört laden, compilieren und installieren. Der Rest sollte sich selbsterklären.

Die Komponenten lassen sich auch alle nachträglich installieren.

![alt text](./img/install-06.png)

<a id="partitionierung"></a>
### Partitionierung

Anschließend partitionieren wir unser VM Image. **UFS** ist dabei für 
eine VM zu empfehlen, da **ZFS** wesentlich mehr RAM und Rechenleistung benötigt.

![alt text](./img/install-07.png)
![alt text](./img/install-08.png)
![alt text](./img/install-09.png)
![alt text](./img/install-10.png)
![alt text](./img/install-11.png)

<a id="eigentliche-installation"></a>
### Eigentliche Installation

Jetzt kopiert und entpackt der Installer die Daten von dem installer image auf das VM Image. 
Das kann je nach Maschine und installer image 3-10 Minuten dauern.

![alt text](./img/install-12.png)

<a id="root-passwort"></a>
### Root Passwort

Jetzt noch das root passwort festlegen und wir sind fast fertig.

![alt text](./img/install-15.png)

<a id="netzwerk"></a>
### Netzwerk

Entsprechende Netzwerk Einstellungen sind nur notwendig falls Software 
(z.B. ```vim```) nachinstalliert werden soll oder falls für die eigentliche 
Aufgabe Netzwerk notwendig ist.

![alt text](./img/install-16.png)
![alt text](./img/install-17.png)
![alt text](./img/install-18.png)
![alt text](./img/install-19.png)

IPv6 können wir überspringen, da die Uni (und das ITMC) eh nicht daran glauben.

![alt text](./img/install-20.png)

Die DNS Einstellungen kann man auf den Default Wert belassen.

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


Da wir erst einmal keine weiteren Benutzer benötigen überspringen wir den Schritt weitere anzulegen.
Analog zu Linux können wir später jeder Zeit neue Nutzer mit ```adduser``` anlegen.

![alt text](./img/install-30.png)

<a id="abschluss"></a>
### Abschluss

Im Anschluss haben wir noch einmal final die Möglichkeit diverse Einstellungen zu ändern.
![alt text](./img/install-31.png)

Und erhalten noch die Option vor dem Neustart mit einer Shell im neuen System vor dem Neustart eigene Änderungen vorzunehmen.

![alt text](./img/install-32.png)
![alt text](./img/install-33.png)
![alt text](./img/install-34.png)

<a id="fertiges-os"></a>
### Fertiges OS

Nach dem Neustart können wir uns anmelden

![alt text](./img/install-35.png)
![alt text](./img/install-36.png)
![alt text](./img/install-37.png)

<a id="update-to-112-release"></a>
### Update to 11.2-RELEASE

Um später wenn 11.2 draußen ist vom RC1 zum RELEASE zu Upgraden siehe
https://www.freebsd.org/releases/11.2R/installation.html#upgrade

<a id="links"></a>
### Links

Nützliche Links:

- http://web.archive.org/web/20180602143516/https://www.freebsd.org/doc/handbook/bsdinstall-start.html
Handbuch zu installation von FreeBSD (achtung, bilder etwas veraltert).
- http://web.archive.org/web/20180602143507/https://www.freebsd.org/doc/handbook/svn.html Anleitung Komponenten wie **ports** oder **src** nachinstalliert.

<a id="installation-von-programmen"></a>
# Installation von Programmen

Mit dem folgenden Befehl lassen sich als **root** alle notwendigen Programme installieren.

```
# pkg install vim mosh byobu git bash
```

<a id="programme-updaten"></a>
## Programme updaten

FYI um Programme zu aktualliseren reicht es aus
```
# pkg upgrade
```
auszuführen.

Siehe auch https://www.freebsd.org/doc/handbook/pkgng-intro.html für weitere Informationen.

<a id="konfiguration-und-kompilation-vom-freebsd-kernel"></a>
# Konfiguration und Kompilation vom FreeBSD Kernel

<a id="konfiguration-1"></a>
## Konfiguration

Zunächst legen wir eine eigene Konfiguration für unseren neuen Kernel an. Dazu nehmen wir
die amd64 Konfiguration als Basis.

```
# cd /usr/src/sys/amd64/conf
# cp GENERIC FAIL_BSD
```

Danach kann die Konfiguration angepasst werden um z.B. einige Sub-Systeme zu entfernen.

**Achtung** unter BSD ist standardmäßig nur ```vi``` installiert. Zum installieren von ```vim```
einfach ```pkg install vim``` als ```root``` eingeben. Eine **Internet-Verbindung** 
ist für die Installation **notwendig** (```vim``` mit Abhängigkeiten benötigt etwa 400MiB).
Aus Gewohnheit gehe ich im folgenden davon aus, dass ```vim``` installiert wurde.

<a id="kompilation"></a>
## Kompilation

Beim ersten mal compilieren wie folgt.

```
# cd /usr/src
# make buildkernel KERNCONF=FAIL_BSD
# make installkernel KERNCONF=FAIL_BSD
```

Danach kann mit der Flag ```-DKERNFAST``` die Kompilation beschleunigt werden in 
dem nur all das neu gebaut wird, dessen Konfiguration oder dessen Sourcecode sich 
geändert hat.

```
# cd /usr/src
# make buildkernel -DKERNFAST KERNCONF=FAIL_BSD
# make installkernel KERNCONF=FAIL_BSD
```

**Anmerkung** um die Bauzeit des Kernels zu reduzieren lohnt es sich, zumindest für das Bauen der VM mehr Kerne zu geben über das Argument ```-smp <number>``` beim starten der VM also z.B. so

```./x86_64-softmmu/qemu-system-x86_64 -smp 4 -boot c -cdrom /path/to/$InstallerImage.iso -m 512 -hda /path/to/VMImage.qcow2```

Anschließend das ```-j<number>``` Argument beim ```makle buildkernel``` nicht vergessen.


<a id="links-1"></a>
## Links

- http://web.archive.org/web/20180602150338/https://www.freebsd.org/doc/handbook/kernelconfig-config.html ausführliche Anleitung zur Konfiguration des Kernels.
- http://web.archive.org/web/20180602152745/https://www.freebsd.org/doc/handbook/kernelconfig-building.html ausführliche Anleitung zur Kompilation des Kernels.


<a id="installation-vom-init-script"></a>
# Installation vom Init-Script

<a id="vorbeschreibung"></a>
## Vorbeschreibung

Falls noch nicht passiert sollte nun ein neuer Benutzer mit dem Usernamen `al` angelegt werden. 
```
# adduser
```

Falls noch nicht passiert sollte nun `BASH` installiert werden mittels `pkg install bash`.

<a id="kopierer-der-scripte"></a>
## Kopierer der Scripte

die Scripte auf KOS im Ordner `/fs/scratch/korner/CLANG_FREEBSD/FREEBSD/SCRIPTS/` via `SCP`
in das *home* von *al* kopieren. Ggf. das Script `run-bench.sh` anpassen.

<a id="anpassen-des-bootloaders"></a>
## Anpassen des Bootloaders

Anschließend muss noch das Script `boot.init.sh` als neues `init_script` in die Datei 
`/boot/loader.conf` eingetragen werden. Dafür entsprechend die Zeile entkommentieren
so, dass dort am Ende steht
```
init_script="/home/al/boot.init.sh"
```

Das Ganze abspeicher, umrühren und fertig. Jetzt wird bei jedem Boot nach dem der Kernel geladen wurde und die 
HW initialisiert hat als erstes dieses Script ausgeführt. Das Script `boot.init.bash.sh` könnte man ggf. 
so anpassen, dass es nicht nur auf die Eingabe eines Buchstabens oder einer Zahl reagiert. 

`Bash` mag mich aber nicht wirklich und k.a. wie das funktioniert. Theoretisch kann man das Bash-Script aber auch
durch ein Programm oder Python-Script ersetzen. Python müsste nur ggf. nachinstalliert werden.

Written by Daniel Korner 2018; extended by Alexander Lochmann 2018
