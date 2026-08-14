# FujiNet virtuel — spécification du binding Atari ST (extension NeoST)

> **Statut.** Extension NeoST assumée, **inactive par défaut**. Aucun FujiNet matériel
> n'existe pour l'Atari ST (le fil AtariAge de la communauté propose le port ACSI mais
> aucun prototype n'a abouti) : NeoST définit ici un binding ST **de référence**, en
> reprenant tels quels les IDs de devices et les octets de commande du firmware amont
> ([fujinet-firmware](https://github.com/FujiNetWIFI/fujinet-firmware/wiki)). Seul le
> **transport ACSI** est propre à NeoST. Hatari n'a aucun équivalent —
> cf. `docs/HATARI_DIVERGENCES.md` § Extensions.

## Ce que c'est

[FujiNet](https://fujinet.online/what-is-fujinet/) est un périphérique WiFi (ESP32) du
monde rétro qui fait du **déport de protocole** : la machine n'a pas de pile TCP/IP,
elle envoie de petites commandes — `{device, commande, aux1, aux2, direction,
longueur}` + payload — et le périphérique fait le travail (HTTP, TCP, JSON, montage
d'images depuis Internet…). Cette abstraction est identique sur tous les bus du
firmware (SIO Atari 8-bit, SmartPort Apple II, RS-232 IBM PC…) ; NeoST la pose sur un
CDB ACSI.

```
   68000 ── $FF8604/06 ──▶ Fdc::writeAcsi ─▶ Acsi (cible 6) ─▶ FujiDevice (cœur, sans socket)
                                                                    │ interface FujiHost
                                              neost_net : FujiHostLive (sockets) /
                                                          FujiHostReplay (fixtures, étalons) /
                                                          FujiHostNull (hors ligne)
```

* **Cœur pur** : `src/io/FujiDevice.{hpp,cpp}` ne touche jamais au réseau ; il parle à
  l'interface `src/net/FujiHost.hpp`, posée par le frontend. Aucun cycle émulé ne
  s'écoule pendant un appel hôte.
* **Déterminisme** : les étalons (`tools/run_all.py`) n'ouvrent **jamais** de socket —
  l'auto-test `neost-headless --fuji-selftest` pilote le protocole fil complet contre
  le backend de rejeu, avec fixtures auto-générées.

## Activer

| Où | Comment |
|----|---------|
| Headless | `--fujinet` (cible 6), `--fujinet-target N`, `--fujinet-host URL` (slot 0 + auto-montage d'une image), `--fujinet-replay DIR`, `--fujinet-offline` |
| GUI | Configuration → **Network** ; persisté dans `neost.cfg` (`fujinet=`, `fujinet_target=`, `fujinet_hosts=`) |
| Code | `machine.fuji.setHost(&host); machine.enableFujiNet(target);` |

Démonstration sans un octet de code ST — démarrer une disquette hébergée en HTTP :

```sh
./build/neost-headless roms/etos192us.img --fujinet \
    --fujinet-host http://server/games/disk.st --frames 600 --screenshot s.ppm
```

## Transport ACSI

La cible ACSI FujiNet (défaut **6**) répond aux **commandes SCSI standard** (TEST UNIT
READY, INQUIRY `NeoST Emulated Harddisk`, READ CAPACITY, READ/WRITE si une image y est
montée — une image distante montée par FujiNet est donc **bootable sans pilote**) et à
un **opcode vendeur `$60`**, envoyé derrière le marqueur ICD :

```
octet 0 (A1 bas)   : (target << 5) | $1F      ; marqueur ICD étendu
octets 1..10 (A1 haut) — le CDB FUJI de 10 octets :
  [0] $60      opcode vendeur FUJI
  [1] $00      obligatoire (bits 7-5 = LUN, doit rester 0)
  [2] device   $70 Fuji | $71-$78 N1:-N8: | ($31-$38 lecteurs, réservé)
  [3] command  cf. tables ci-dessous
  [4] aux1
  [5] aux2
  [6] dir      0 = pas de données, 1 = device→ST (DMA read), 2 = ST→device (DMA write)
  [7] len_hi   longueur UTILE, gros-boutiste (max 65535 ; ≤ 64 Ko par transfert)
  [8] len_lo
  [9] $00      réservé
```

* **Phase données** : DMA ST classique ($FF8604/06/09/0B/0D). La longueur transférée
  est `len` **complété au multiple de 512 supérieur** (le DMA ST compte des secteurs) ;
  les octets au-delà de `len` sont nuls. Le tampon ST doit donc être dimensionné au
  multiple de 512.
* **Statut** : octet ACSI standard — `0` = OK (« Complete »), `2` = erreur ; le détail
  passe par REQUEST SENSE, et le code FujiNet précis (`FN_ERR_*`) par la commande
  N: `'E'` ou l'octet 3 du Status `'S'`.
* **dir=2 en deux temps** : le CDB annonce le payload (statut OK), le DMA write le
  livre, et le **statut final** (après transfert) porte le verdict de la commande.
* **Garde-fou fidélité** : l'opcode `$60` n'est accepté **que** sur la cible FujiNet
  (`Acsi.cpp`, `fujiCdb`) — sur toute autre cible le rejet `>= $60` historique
  s'applique à l'identique. Une cible non-FujiNet reste strictement conforme à Hatari.

## Device Fuji (`$70`) — commandes implémentées

| Cmd | Nom | dir | len | Payload |
|-----|-----|-----|-----|---------|
| `$FF` | Reset | 0 | — | ferme tous les canaux |
| `$FA` | Get WiFi status | 1 | 1 | 3 = connecté, 6 = déconnecté |
| `$FE` | Get SSID | 1 | 97 | `char ssid[33]; char password[64]` |
| `$E8` | Get adapter config | 1 | 140 | `ssid[32] hostname[64] localIP[4] gateway[4] netmask[4] dnsIP[4] mac[6] bssid[6] version[15]` + pad |
| `$F4` | Read host slots | 1 | 256 | 8 × `char[32]` |
| `$F3` | Write host slots | 2 | 256 | idem |
| `$F2` | Read device slots | 1 | 304 | 8 × `{u8 hostSlot; u8 mode; char file[36]}` |
| `$F1` | Write device slots | 2 | 304 | idem |
| `$F9` | Mount host | 0 | — | aux1 = slot |
| `$F7` | Mount image | 0 | — | aux1 = device slot ; télécharge et monte (`.st/.msa/.dim/.stx` → lecteur A, sinon disque ACSI sur la cible FujiNet) |
| `$E9` | Unmount image | 0 | — | aux1 = device slot |
| `$D2` | Get time | 1 | 7 | `{annéeH, annéeB, mois, jour, h, m, s}` (backend rejeu : 1985-06-01, fixe) |

## Devices N: (`$71`–`$78`) — commandes implémentées

Le canal = device − `$71` (N1: à N8:). Devicespec = `PROTO://hôte[:port]/chemin`,
préfixe `Nx:` toléré. Protocoles v1 : **HTTP** (GET), **TCP** (bidirectionnel),
`telnet` (alias TCP). **HTTPS refusé** (pas de TLS — cf. Limites).

| Cmd | Nom | dir | Notes |
|-----|-----|-----|-------|
| `'O'` | Open | 2 | payload = devicespec ASCIIZ ; aux1 = mode, aux2 = translation |
| `'C'` | Close | 0 | |
| `'R'` | Read | 1 | len ≤ avail du dernier Status, sinon **erreur** (contrat FujiNet) |
| `'W'` | Write | 2 | TCP uniquement v1 |
| `'S'` | Status | 1 | 4 octets `{avail_lo, avail_hi, connected, error}` ; `error` 136 = EOF |
| `'E'` | Last error | 1 | 1 octet `FN_ERR_*` |
| `'P'` | JSON parse | 0 | fige le tampon du canal comme document JSON |
| `'Q'` | JSON query | 2 | payload = chemin `/clé/0/sous-clé` → le résultat **remplace** le tampon de lecture (puis Status + Read) |
| `'T'` | Translation | 0 | accepté, sans effet v1 |

Codes d'erreur (`fujinet-lib`) : `FN_ERR_OK` 0, `IO_ERROR` 1, `BAD_CMD` 2, `OFFLINE` 3,
`WARNING` 4, `NO_DEVICE` 5, `UNKNOWN` $FF.

## Backends hôte (`neost_net`)

| Backend | Usage | Déterminisme |
|---------|-------|--------------|
| `FujiHostLive` | sockets réels (HTTP 1.1 minimal + TCP), cache de téléchargement dans `<tmp>/neost-fujinet/` | non — jamais dans les étalons |
| `FujiHostReplay` | rejoue un dossier de fixtures (`--fujinet-replay DIR`) ; nom de fichier = devicespec assaini (`[^A-Za-z0-9._-]` → `_`) | **oui** |
| `FujiHostNull` | périphérique présent, WiFi hors ligne | oui |

`NEOST_WITH_NET=OFF` (forcé sur WASM/Android) retire `FujiHostLive` ; le reste compile
partout. Trace : `NEOST_FUJI_TRACE=1`.

## Tests

* `neost-headless --fuji-selftest` — 11 vérifications au niveau **fil** (registres
  $FF8604/06, marqueur ICD, phases DMA, JSON, INQUIRY intact, garde `$60`).
  Intégré au palier `fast` (`tools/etalons.json`, type `fuji_selftest`).
* Bout-en-bout : cf. la commande de démonstration ci-dessus ; validée disquette
  HTTP ↔ montage local **0 px d'écart**.
* Save-states : état du protocole introduit en **v10**, format courant **v11** (les
  canaux réseau du backend ne survivent pas à un load — le ST doit rouvrir, comme
  après une coupure WiFi ; flag d'en-tête bit1 = FujiNet, configs save/load doivent
  concorder).

## Limites v1 (assumées)

* **Pas de TLS** : `https://` renvoie une erreur claire. (mbedTLS optionnel à venir.)
* HTTP : GET seulement (pas de POST/headers custom côté N: ; `fetchToFile` suit les
  redirections 301/302/307/308).
* Pas d'UDP, pas de TNFS, pas de FTP, pas d'imprimante P:, pas de modem R:
  (le modem Hayes RS-232 est un chantier séparé — cf. TODO).
* `Mount image` bloque le temps du téléchargement (la machine émulée ne voit AUCUN
  cycle s'écouler ; seul le mur d'horloge attend).
* Lecteur virtuel : montage dans le **lecteur A** uniquement (device slots 0-7 non
  différenciés).

## Côté ST

La couche transport 68000 (CDB ACSI + DMA) vit dans `dev/fujinet/` — l'API
(`network_open`, `network_read`, `network_write`, `network_status`,
`network_json_query`, `FN_ERR_*`) reprend celle de
[`fujinet-lib`](https://github.com/FujiNetWIFI/fujinet-lib). Programmes de démo dans
`gemdos/DEMOS/` : `NWGET.TOS` (téléchargement HTTP → fichier, vérifié par empreinte).
Chaîne : `dev/fujinet/build.sh` (vbcc/vasm, cf. `dev/etalons/build.sh`).

---

# Autres extensions réseau NeoST

Ces trois extensions partagent la même philosophie (backend hôte hors du cœur, OFF par
défaut, `NEOST_WITH_NET`) et le même statut « extension assumée » que FujiNet.

## Modem Hayes sur RS-232 (`--modem`, GUI Network)

Un modem émulé sur l'USART MFP : les commandes `AT` ouvrent de vraies connexions TCP
(`ATDT hôte:port` → `CONNECT`, pont transparent octets ↔ socket ; `+++`/`ATH` pour
raccrocher ; `DCD` suit la porteuse). C'est le grand débloqueur du logiciel d'époque —
terminaux, BBS, et les piles TCP/IP historiques **STiK/STinG en SLIP/PPP**. Repose sur
`Mfp::receiveByte` (injection RX **cadencée** au débit série via `Scheduler::SERIAL_RX`,
IRQ RxFull par octet — un pilote qui compte sur le rythme du fil ne perd aucun octet).
Vérifié : `MODMTEST.TOS` ↔ serveur TCP local (`CONNECT 9600`, bannière reçue).
`src/net/HayesModem.cpp`.

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
la place (fidèle au matériel). C'est exactement ce que fait FujiNet côté 8 bits
(mozzwald/FujiNet-MIDIMaze). Vérifié : `MIDITEST.TOS` — 10 octets OUT→UDP→pair (ordre
exact) et round-trip complet OUT→réseau→IN→ACIA. `src/net/MidiRing.cpp`.

## Pont vers FujiNet-PC (point d'extension)

L'interface `FujiHost` (`src/net/FujiHost.hpp`) est **pluggable** : un `FujiHostBridge`
relayant les commandes vers le **vrai firmware** [FujiNet-PC](https://github.com/FujiNetWIFI/fujinet-pc)
en UDP (à la manière de [NetSIO](https://github.com/FujiNetWIFI/fujinet-emulator-bridge),
avec sa requête de synchronisation qui met l'émulation en pause le temps de la réponse)
est un simple ajout de backend — il hériterait de TNFS, FTP, JSON, imprimante PDF,
`CONFIG` et des mises à jour amont. Non implémenté v1 (impose un second processus et
casse le déterminisme des étalons) ; l'architecture le prévoit sans toucher au cœur.
