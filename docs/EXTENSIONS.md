# Extensions NeoST — matériel d'époque que l'ST n'avait pas d'origine

> **Statut commun.** Toutes **inactives par défaut**, sans effet sur les étalons (aucune
> n'ouvre de socket depuis `tools/run_all.py`), et consignées comme divergences délibérées
> dans `docs/HATARI_DIVERGENCES.md` § Extensions — Hatari n'a aucun équivalent. Le cœur
> (`neost_core`) ne fait que *signaler* ; l'I/O réelle vit dans `neost_net` (frontends).
>
> Toutes reproduisent du matériel que les Atariens ont réellement branché :
>
> | Extension | Nature | Réalité matérielle |
> |-----------|--------|--------------------|
> | [UltraSatan](#ultrasatan--interface-sdmmc-sur-le-bus-acsi---ultrasatan-gui-hard-disks) | stockage SD sur ACSI | **matériel réel** (Jookie) |
> | [NetUSBee](#netusbee--ne2000--hôte-usb-isp1160-sur-le-port-cartouche---netusbee-gui-network) | Ethernet + USB, port cartouche | **matériel réel** |
> | [EtherNEC](#ethernec--ne2000-sur-le-port-cartouche---ethernec-gui-network) | NE2000, port cartouche | **matériel réel** (montage T. Redelberger) |
> | [Modem Hayes](#modem-hayes-sur-rs-232---modem-gui-network) | pont RS-232 → TCP | équivalent des modems WiFi ESP8266 vendus pour ST |
> | [Anneau MIDI](#anneau-midi-réseau--midimaze-en-ligne---midi-net-hpl) | MIDIMaze sur UDP | transpose un câblage MIDI réel |

# Les extensions, une par une

Philosophie commune : backend hôte hors du cœur, OFF par défaut, `NEOST_WITH_NET` pour
celles qui ouvrent des sockets. **UltraSatan** pour le stockage, **NetUSBee**/**EtherNEC**
pour le réseau, **modem Hayes** et **anneau MIDI** pour le reste.

## Modem Hayes sur RS-232 (`--modem`, GUI Network)

Un modem émulé sur l'USART MFP : les commandes `AT` ouvrent de vraies connexions TCP
(`ATDT hôte:port` → `CONNECT`, pont transparent octets ↔ socket ; `+++`/`ATH` pour
raccrocher ; `DCD` suit la porteuse). C'est le grand débloqueur du logiciel d'époque —
terminaux, BBS, et les piles TCP/IP historiques **STiK/STinG en SLIP/PPP**. Repose sur
`Mfp::receiveByte` (injection RX **cadencée** au débit série via `Scheduler::SERIAL_RX`,
IRQ RxFull par octet — un pilote qui compte sur le rythme du fil ne perd aucun octet).
Vérifié : `MODMTEST.TOS` ↔ serveur TCP local (`CONNECT 9600`, bannière reçue), et
**bout-en-bout sur Internet** (2026-08-22) — `tools/make_net_test.py` génère une disquette
dont l'`AUTO\NETTEST.PRG` compose `ATDT theoldnet.com:80`, envoie une requête HTTP à la
main et affiche la réponse : en-têtes (`Content-Length`, `X-Powered-By: Express`) puis le
HTML. C'est le chemin qui marche **sans pile TCP/IP côté ST** (le modem est un pont
transparent octets ↔ socket) ; interactivement, `UNITERM.PRG` fait la même chose.

```sh
python3 tools/make_net_test.py /tmp/nettest.st theoldnet.com /
./build/neost-headless roms/etos192us.img --machine megast --mem 1m --mono \
    --modem --disk /tmp/nettest.st --frames 2500 --screenshot out.ppm
```

Deux pièges appris en l'écrivant, consignés dans le générateur : le compteur 200 Hz du TOS
(`$4BA`) vit en mémoire BASSE, **protégée** — y toucher depuis un PRG (mode utilisateur)
lève un bus error, d'où `Super(0)` ; et la console ST **n'enroule pas** les lignes longues,
elle écrase la 80ᵉ colonne (une ligne HTML n'y laissait voir que son dernier caractère,
une colonne de « > »), d'où le repli à 78 colonnes et le CR ajouté sur les LF seuls.
`src/net/HayesModem.cpp`.

⚠ Le **NetUSBee/EtherNEC ne sort PAS** sur Internet : son backend est une boucle locale
(`NetBackendLoop`). Il faudrait `NetBackendSlirp` (libslirp, NAT mode utilisateur) **et**
une pile TCP/IP côté ST (STinG + `ENEC.STX`) — cf. `TODO.md`.

## EtherNEC — NE2000 sur le port cartouche (`--ethernec`, GUI Network)

Émulation de la carte réseau NE2000 vue par le port ROM (montage
[EtherNEC](https://github.com/EmmanuelKasper/ethernec) de Dr. Thomas Redelberger), pour
faire tourner les pilotes libres **STinG (`.STX`), MiNTnet (`.XIF`) et MagiCNet SANS
modification**. Le port cartouche étant en lecture seule et sans ligne A0, tout est
encodé dans l'adresse :

* **lire** le registre `n` : lecture à `$FB0000 + n*512` ;
* **écrire** `d` dans `n` : *fausse lecture* à `$FA0000 + n*512 + d*2`.

Décodé dans `Bus::read8Slow` (`src/io/Ne2000.cpp`). Modèle DP8390 classique (pages 0/1,
anneau de réception avec en-tête 4 octets, Remote DMA, filtrage MAC/broadcast). Backend
physique = interface `NetBackend` (boucle locale fournie ; SLIRP/pcap = point
d'extension quand la lib est présente). **Exclusif d'une cartouche montée** (conflit de
fenêtre $FA0000 — le montage est refusé avec message). Auto-test : `--enec-selftest`
(palier `fast`).

## Anneau MIDI réseau — MIDIMaze en ligne (`--midi-net H:P[:L]`)

MIDI Maze relie les ST en **anneau** par câbles MIDI (OUT de l'un → IN du suivant). On
transporte l'anneau sur UDP : `MidiAcia::setMidiSink` envoie les octets MIDI OUT vers le
pair aval, et les datagrammes de l'amont entrent par `receiveExternal`. Le 6850 ne tient
que 2 octets → `MidiRing` a un **tampon de gigue** et n'injecte que lorsque l'ACIA a de
la place (fidèle au matériel). Vérifié : `MIDITEST.TOS` (source `dev/netdemo/`) — 10
octets OUT→UDP→pair (ordre exact) et round-trip complet OUT→réseau→IN→ACIA.
`src/net/MidiRing.cpp`.

## UltraSatan — interface SD/MMC sur le bus ACSI (`--ultrasatan`, GUI Hard Disks)

C'est **la** réponse historique de l'écosystème ST au stockage moderne (Jookie, 2009) :
un boîtier ACSI à **deux slots SD/MMC** — chaque slot est une cible ACSI (IDs *n* et
*n+1*, usine 0-1) — et une **horloge temps réel** sauvegardée par pile. Les pilotes
d'époque (HDDRIVER, ICD PRO, AHDI, **EmuTOS sans pilote**) le voient comme deux disques
SCSI ordinaires ; l'outil `US_CONF.TOS` lui parle par des **paquets ICD propres** :

```
octet 0 : (ID << 5) | $1F         marqueur ICD
octet 1 : $20                     groupe 1 (10 octets) — jamais un opcode SCSI utile
octets 2-3 : 'U','S'              signature UltraSatan
octets 4-7 : code ASCII           CurntFW · RdCl/WrCl · RdINQRN/WrINQRN · RdSt/WrSt · RdLog · RdFW/WrFW
octets 8-10 : 3 paramètres        magie 'RTC' (WrCl), $83 $03 $17 (WrSt)
puis UN secteur (512 octets) par DMA, dans le sens de la commande
```

| Élément | NeoST (`src/io/UltraSatan.*`, `Acsi`) | Source de vérité |
|---------|----------------------------------------|------------------|
| INQUIRY | `JOOKIE  ` + nom (10 car., défaut `UltraSatan`) + n° de slot `'1'`/`'2'` + `1.20` + date, bit **RMB** | `scsi6.c SCSI_Inquiry` |
| Slot sans carte | TEST UNIT READY / READ CAPACITY / READ / WRITE → CHECK CONDITION, clé **NOT READY**, ASC `$3A` (medium not present) ; INQUIRY répond quand même | `ReturnStatusAccordingToIsInit` |
| `USCurntFW` | secteur = `UltraSatan v1.20 (NeoST emulation)`, 0-terminé | `special.c` |
| `USRdClRTC` / `USWrClRTC` | `'R','T','C'` + `{année−2000, mois, jour, h, min, s}` binaires ; écriture = magie `RTC` dans le **paquet ET le secteur** | `special.c`, `rtc.c` |
| `USRdINQRN` / `USWrINQRN` | nom INQUIRY (10 octets ; `$00`/`$FF` en tête = nom d'usine) | `special.c` |
| `USRdSt` / `USWrSt` | page de réglages 512 octets (opaque, octet 1 = firmware amorcé) ; écriture **refusée sans la magie** `$83 $03 $17` | `special.c` |
| `USRdLog` | journal de commandes (vide) | `special.c` |
| `USRdFW` / `USWrFW` | **refusés** (CHECK CONDITION) : NeoST n'émule pas la dataflash — impossible de briquer l'appareil | — |
| Horloge | pile propre, **indépendante** du RP5C15 Mega : seedée sur l'hôte, avance avec les **cycles émulés** (comme `Rtc`) | `rtc.c` (année base 2000) |

Sources : firmware v1.20 ([atarijookie/ce-atari `ultrasatan/`](https://github.com/atarijookie/ce-atari/tree/master/ultrasatan)).
Hatari n'a aucun équivalent (disque ACSI générique « Hatari Emulated Harddisk »). Toute
cible qui n'est pas un slot UltraSatan reste **byte-identique** au port de `hdc.c`.

| Où | Comment |
|----|---------|
| Headless | `--ultrasatan` (IDs 0-1), `--ultrasatan-id N`, `--sd1 IMG`, `--sd2 IMG` (`--acsi IMG` = slot 1 quand l'ID est 0) |
| GUI | Configuration → **Hard Disks** : case UltraSatan, slot 1 = l'image ACSI, slot 2 à monter ; `neost.cfg` : `ultrasatan=`, `sd2=` |
| Code | `machine.enableUltraSatan(firstTarget); machine.fdc.mountAcsi(path, firstTarget + slot);` |

Image de carte SD : n'importe quel dump brut partitionné (Atari ou DOS). `tools/make_usatan_hd.py`
en génère une minimale (2 Mo, une partition GEM FAT16) qu'EmuTOS monte en **C:** sans pilote.

## NetUSBee — NE2000 + hôte USB ISP1160 sur le port cartouche (`--netusbee`, GUI Network)

Le [NetUSBee](https://hardware.atari.org/netusbee/netus.htm) = une **RTL8019AS câblée
exactement comme l'EtherNEC** (pilotes rétro-compatibles — NeoST réutilise `io/Ne2000`
tel quel) + un **ISP1160** (hôte USB 1.1, deux ports) sur le même port cartouche. Le port
étant en lecture seule, 16 bits, sans A0, le pilote FreeMiNT (`sys/usb/src.km/ucd/netusbee/
isp116x.h`) encode tout dans l'adresse :

```
$FA0000 + (b << 1)   LSB_WRITE      : verrouille l'octet b
$FB8000 + (b << 1)   MSB_DATA_WRITE : écrit le MOT (b << 8) | latch dans le port DONNÉES
$FBC000              MSB_CMD_WRITE  : écrit le MOT latch dans le port COMMANDE (index | $80 = écriture)
$FA8000              DATA_READ      : lit un mot 16 bits du port DONNÉES (registres 32 bits : bas puis haut)
```

`src/io/Isp1160.*` modélise le contrôleur : `HcChipID` = `$6120` (masque `$FF00` = `$6100`),
`HcSoftwareReset` (`$F6`) et `HcCommandStatus.HCR` auto-effacé, registres OHCI (`HcControl`,
`HcFmInterval`, `HcRhDescriptorA` avec **NDP = 2 figé**, `HcRhPortStatus1/2`…), registres ISP
(`HcHardwareConfiguration`, `HcuPInterrupt`/`Enable`, `HcScratch`, `HcITL/ATLBufferLength`,
`HcBufferStatus`), **FIFO ATL** : les PTD écrits sont « exécutés » à chaque trame et relus avec
**CC = 5 (DeviceNotResponding)**, `Active = 0` — c'est un **hub racine vide** : les pilotes
(FreeMiNT `netusbee.ucd`, NetUSBee TOS) s'initialisent, n'énumèrent rien. Ligne IRQ modélisée
(`irqAsserted()`), non câblée (les pilotes ST tournent en polling).

⚠ **Divergence possible, consignée** : la fenêtre `LSB_WRITE` (`$FA0000-$FA01FF`) est AUSSI
celle des écritures du registre 0 (CR) de la NE2000. Sans schéma du NetUSBee, NeoST applique
ce que les adresses publiées impliquent : **les deux puces voient l'accès**. Si le vrai
matériel gate l'une des deux, ce point est à corriger ici (`Bus::read8Slow`).

| Où | Comment |
|----|---------|
| Headless | `--netusbee` (backend boucle locale ; exclusif de `--cart`) |
| GUI | Configuration → **Network** : case NetUSBee (exclusive d'EtherNEC) ; `neost.cfg` : `netusbee=` |
| Code | `machine.enableNetUsbee()` (= `enableEtherNec()` + ISP1160) |

Point d'extension suivant : brancher un périphérique USB hôte (clavier/souris/stockage) —
l'ATL/ITL et le hub racine sont là, il manque un « device » derrière `HcRhPortStatus`.

## Tests UltraSatan + NetUSBee

* `neost-headless --usatan-selftest` — 15 vérifications au niveau **fil** ($FF8604/06, séquence
  **LongRW** de `US_CONF` : A1 bas/haut, bascule R/W + compteur de secteurs AVANT le dernier
  octet, statut après transfert) : INQUIRY des deux slots, slot vide (NOT READY), slot avec
  carte (TEST UNIT READY, READ CAPACITY, READ(6)), `CurntFW`, `RdCl`/`WrCl` (horloge figée),
  `RdINQRN`/`WrINQRN`, `RdSt`/`WrSt`, refus flash/inconnu, **verrouillage** du paquet `'US'`
  aux cibles UltraSatan. Palier `fast` (`tools/etalons.json`, type `usatan_selftest`).
* `neost-headless --netusbee-selftest` — 11 vérifications : les primitives **exactes** du pilote
  FreeMiNT (`raw_read/write_data16/32`, lectures MOT), ID de puce, scratch, reset logiciel,
  registres 32 bits, hub racine vide, ATL → CC = 5, IRQ, NE2000 toujours décodée, save-state.
  Palier `fast` (type `netusbee_selftest`).
* `tools/selftests.json` → **`usatan_netusbee`** (palier `fast`, verdict série) : une carte SD
  générée (`tools/make_usatan_hd.py`, 16 Mo, partition GEM FAT16) qu'EmuTOS monte en **C:** sans
  pilote et dont il lance **`AUTO\USTEST.PRG`** — le programme de test 68000 RELOGEABLE de
  `tools/make_usatan_test.py` (table de relocation TOS générée, `Super()` puis `Pterm0`), qui parle
  à l'UltraSatan **comme `US_CONF`** (séquence LongRW, attente IRQ GPIP5) et au NetUSBee **comme le
  pilote FreeMiNT** (lectures MOT, primitives raw), et vérifie `_drvbits` bit 2. Verdicts `usfw
  usinq usrtc uscdrv nubid nubscr nubnic`. Trois règles EmuTOS apprises en construisant ce test
  (`bios/blkdev.c`, `bios/disk.c`) : dès qu'un disque dur existe, la **disquette n'est plus
  amorcée** ; un **secteur racine exécutable n'est lancé que sur un disque SANS partition
  reconnue** (`disk_try_dmaboot`) ; et le type de FAT suit la **règle Microsoft** (≤ 4084 clusters
  = FAT12 — une partition de 2 Mo y était lue en FAT12, d'où 16 Mo), là où Atari TOS suppose un
  FAT16 sur tout disque dur. `make_usatan_test.py OUT.st` produit la **disquette** équivalente
  (`A:\AUTO`) pour tester SANS carte SD : `uscdrv` échoue alors, c'est attendu. **Contrôle
  négatif** : la même carte sous Hatari (`--acsi`, `--rs232-out`) donne `uscdrv PASS` et les six
  verdicts matériels **FAIL** — le test ne passe pas par construction.
* Save-states : **v13** (UltraSatan + ISP1160 sérialisés ; drapeaux d'en-tête bit2 = UltraSatan,
  bit3 = NetUSBee — les configs save/load doivent concorder).

