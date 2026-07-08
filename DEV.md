# DEV.md — Guide développeur NeoST

(c) 2026 VERHILLE Arnaud. Référence technique : architecture, débogage, pièges matériels.
Orientation et méthode de travail → [`CLAUDE.md`](CLAUDE.md). État → [`CHANGELOG.md`](CHANGELOG.md) / [`TODO.md`](TODO.md).

## Architecture

Deux idées structurantes : **le `Bus` *est* le plan mémoire** (il ne fait que router
read8/write8 vers les composants), et **le cœur ne dépend pas du GUI**.

- **`neost_core`** (lib statique, aucune dépendance GUI) = la carte mère : `Bus`, `Cpu68k`
  (wrapper Moira), `Shifter`, `Mfp`, `Ikbd`, `Fdc`, `YM2149`, `DmaSound`,
  `Blitter`, `Rtc`, `Glue`, plus `Machine`, `Scheduler` et `Tracer`.
- **`Machine`** assemble les composants, les branche au `Bus`, encapsule `runFrame()`.
- **`neost`** (GUI) et **`neost-headless`** partagent `Machine`. Le GUI ajoute
  GLFW/OpenGL/ImGui/miniaudio et bride à 50 fps réels.

```
src/
  main.cpp                  Frontend GUI : GLFW + OpenGL (texture Shifter) + ImGui,
                            clavier/souris → IKBD, barre résolution, persistance.
  core/
    Bus.{hpp,cpp}           Memory map + dispatch MMIO + bus errors (busFault/buildIoFault).
    Cpu68k.{hpp,cpp}        Wrapper Moira (cycle-exact) : accès mémoire, int-ack vectorisé,
                            hook d'instruction (traceur), reset/IPL.
    Shifter.{hpp,cpp}       Décodage planaire basse/moyenne/haute → buffer ARGB.
    YM2149.{hpp,cpp}        PSG : registres + synthèse 3 voies + bruit + enveloppe.
    DmaSound.{hpp,cpp}      Son DMA STE + Microwire/LMC1992.
    Blitter.{hpp,cpp}       Blitter ST (HOG).
    Machine.{hpp,cpp}       Assemble tout + runFrame() événementiel.
    Scheduler.hpp           Ordonnanceur d'événements datés (cycles).
    Tracer.{hpp,cpp}        Trace d'instructions/IRQ.
  io/
    Mfp.{hpp,cpp}           MC68901 : IRQ vectorisées, timers A-D, GPIP.
    Ikbd.{hpp,cpp}          ACIA 6850 clavier + commandes/souris/joystick IKBD.
    MidiAcia.{hpp,cpp}      2e ACIA 6850 (MIDI).
    Fdc.{hpp,cpp}           WD1772 + DMA disquette + ACSI.
    Rtc.{hpp,cpp}           RP5C15 (Mega ST/Mega STE).
  audio/                    Backend miniaudio (Audio, DriveSound).
  headless/                 Runner déterministe + traces.
  web/main_web.cpp          Frontend WebAssembly (Emscripten + WebGL).
extern/  moira/ imgui/ miniaudio/   (sous-modules)
extern/hatari/src           SOURCE DE VÉRITÉ matérielle (lue, pas compilée)
```

## Modèle d'horloge (`Machine::runFrame`)

PAL basse résolution : **313 lignes × 512 cycles CPU**. `runFrame` est désormais
**événementiel à horloge continue** (`Scheduler`, cycles datés avec carry du dépassement) :
vidéo au cycle (rendu/Timer B/HBL aux cycles 376/400/508), timers MFP A/C/D en mode délai
datés par le MFP, Timer C ≈200 Hz, VBL niveau 4 au début du VBlank. Le GUI bride à 50 fps
réels pour que le temps émulé colle au réel. Le passage au quantum **sous la ligne** (vs par
instruction) reste le grand chantier — cf. [`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md).

## Le Bus

Tout accès CPU passe par `Bus::read8/16/32` et `write8/16/32` (assemblage **big-endian** :
toujours assembler les mots octet par octet). Aiguillage : RAM (`$0`), ROM (`romBase`), MMIO
(`$FF8000+`). `mmioRead8`/`mmioWrite8` routent vers Shifter (`$FF8200`), FDC/DMA (`$FF8600`),
PSG (`$FF8800`), son DMA (`$FF8900`), MFP (`$FFFA00`), ACIA (`$FFFC00`), RTC (`$FFFC21`).

`busFault(addr)` renvoie vrai pour les adresses non décodées qui doivent faire une **bus
error**. Modèle **WHITELIST** porté de Hatari (`ioMem.c`) : tout `$FF8000-$FFFFFF` faute SAUF
les registres câblés du modèle (+ zones « void » silencieuses). Hors IO, `$400000-$F9FFFF` et
`$FF0000-$FF7FFF` fautent ; RAM/ROM/port cartouche jamais.

## Le CPU (Moira, cycle-exact)

NeoST n'a qu'**un seul cœur 68000 : Moira** (vAmiga, MIT, C++20, sous-module `extern/moira`).
L'ancien cœur Musashi — rapide mais **non cycle-exact** — a été retiré : il n'apportait plus
rien face à Moira et doublait inutilement chaque chemin du wrapper. Moira est **requis** pour
bâtir (CMake faute si `extern/moira` est absent), et compilé en mode cycle-exact
(`MOIRA_PRECISE_TIMING=true`, `MOIRA_MIMIC_MUSASHI=false`, cf. `CMakeLists.txt`).

`Cpu68k` (`NeostMoira`, sous-classe de `moira::Moira`) route les accès mémoire vers `g_bus` :
- `read8/16` et `write8/16` consultent `busFaultN` (whitelist Hatari) → lèvent `moira::BusError`
  (trame de groupe 0 reconstruite dans `raiseBusError`) ou haltent le CPU en double faute.
- `readIrqUserVector` (irqMode USER) reproduit le vectoring ST : vecteur MFP (niveau 6) via
  `mfp->iack()`, VBL/HBL (4/2) auto-vectorisés. `neostUpdateIpl` recalcule l'IPL (MFP 6 > VBL 4
  > HBL 2 ; gaté par le SCU sur MegaSTE).
- Le `Tracer` reçoit `onInstruction(pc)` après chaque `execute()` et désassemble via
  `Cpu68k::disassemble` → `moira::disassemble` (syntaxe `Syntax::MUSASHI`, format de trace
  inchangé pour le diff MAME).

L'option `--cpu` (headless) et la clé `cpu=` (`neost.cfg`) ne valent plus que `moira` ; une
ancienne valeur `musashi`/`uae` est tolérée (rétro-compat) mais **avertit** puis bascule sur
Moira (`Cpu68k::parseCore`).

## Chaîne d'interruption (subtile)

Un composant met à jour le `Mfp` (canal ou ligne GPIP), puis le `Bus` appelle
`cpu->updateIpl()` **après** l'accès MMIO. Lignes câblées : I3 blitter, I4 ACIA (clavier+MIDI
en OU câblé), I5 FDC, I7 son DMA XSINT, bit7 moniteur.

## Ajouter / modifier un composant

1. Créer `Xxx.{hpp,cpp}` exposant `read8(addr)` / `write8(addr,v)` (+ état public pour le
   débogueur).
2. L'ajouter en membre de `Machine`, le brancher au `Bus` dans le constructeur de `Machine`,
   router sa plage d'adresses dans `Bus::mmioRead8/Write8`.
3. L'ajouter aux sources de `neost_core` dans `CMakeLists.txt`.
4. **Valider en headless** avant le GUI (`--trace`, `--screenshot`).
5. Pour lever une IRQ : mettre à jour le `Mfp` (canal / ligne GPIP), `updateIpl` est appelé
   par le `Bus` après l'accès MMIO.

## Débogage headless (l'outil n°1)

Pas de « tests » classiques : la validation se fait via `neost-headless` (déterministe, sans
GUI), qui produit des **traces façon MAME** et des **captures PPM**.

```sh
./build/neost-headless <rom> --frames N --trace t.txt --regs --irq
tail t.txt                                   # localiser la boucle d'attente (PC qui tourne)
./build/neost-headless <rom> --frames N --screenshot s.ppm   # sips -s format png s.ppm --out s.png
./build/neost-headless <rom> --frames N --sound-dump s.wav   # WAV 48 kHz (YM+DMA+LMC, chaîne GUI)
#   → A/B audio vs oracle Hatari (WAV) ou entre configs ; RMS/profil par seconde en python
#   DMA STE : NEOST_DMASND_TRACE=1 émet chaque fetch FIFO au format « DMA snd fifo refill »
#   d'Hatari (--trace dmasound) → diff direct des séquences adr/contenu ; étalon dédié
#   tools/make_dmasnd_test.py (tampon modifié pendant la lecture, cas Mental Hangover)

# Suite étalons (captures + régression) : tools/run_etalons.py — voir docs/TEST_SOFTWARE.md
python3 tools/fetch_etalons.py && python3 tools/run_etalons.py --update-ref
python3 tools/run_etalons.py
```

Options : `--cpu moira` (seul cœur, optionnel), `--machine st|megast|ste|megaste`,
`--mem 256k|512k|1m|2m|4m`, `--cart FILE`, `--disk`, `--diskb`, `--mono`, `--until-pc HEX`,
`--walk-mouse`, `--keys "STR"`, `--loopback`. Pilotage daté (menus de jeux/démos) :
`--keys-at N "STR"` (scancodes étendus : flèches `<>[]`, Esc `=`, F1-F5 `!@#$%`),
`--joy-at N VAL`, `--joy-script N "SCRIPT"` (U/D/L/R/F/`.` = 1 trame),
`--mouse-at N "SCRIPT"` (L/R/U/D = ±8 px, `1`/`2` = clic gauche/droit, `.` = idle — c'est
ainsi qu'on pilote Vroom : clic droit au titre, clic droit en course). Debug entrées :
`NEOST_DEBUG_IKBD=1` (commandes reçues par l'IKBD), `NEOST_DEBUG_ACIA=1` (chaque lecture
du data register $FFFC02 : valeur, file restante, cycle).

Format de trace (la séquence de PC est le signal de diff) :
```
FC0030: bra     $fc004e
>>> IRQ niveau 6, vecteur $45        (Timer C du MFP)
```

### Techniques vérifiées
- **Cartouches de diagnostic** (`carts/*.bin|img`, magic `$FA52235F`) : exécutées au reset,
  elles écrivent leur rapport sur le **port série RS-232** (`$FFFA2F`), vidé en fin de run.
  C'est LE moyen de savoir quel sous-système échoue. Bon `--machine` (ST_Diagnostic→st,
  STE_Test→ste, MegaSTE→megaste) ; `--keys "O"` pilote le menu (`O`=ROM, `Z`=tests, `Q`=tout).
- **`--irq` indispensable** pour les bugs d'interruption (sinon le saut vers un vecteur est
  invisible). `grep '>>> IRQ' t.txt`.
- **`--loopback`** : branche les connecteurs de bouclage (MIDI/Serial/Printer-Joystick), APRÈS
  l'injection `--keys` — sinon l'écho du rapport série console reviendrait en réception et
  casserait la détection clavier. L'ACSI (test J/H) n'a PAS besoin de `--loopback`.
- **Sensibilité à `--mem`** : un même diag peut échouer différemment selon la taille RAM →
  révèle un bug de décodage MMU (`mmuTranslate`).
- **Garde double bus fault** (`Cpu68k.cpp`, `g_inBusError`) : un code en vrille fautait en
  boucle → segfault hôte. On halte désormais le CPU comme un vrai 68000 (Moira
  `flags|=HALTED`, cf. `faultOrHalt`). Si EXIT≠0 réapparaît, vérifier cette garde.
- **`tools/trace_diff.py`** : aligne une trace NeoST et une trace Hatari du même ROM/disquette
  sur un PC commun et localise la première divergence (flux PC + registres) :
  ```sh
  ./build/neost-headless --frames 200 --trace neost.txt --regs --irq
  SDL_VIDEODRIVER=dummy hatari --trace cpu_disasm,cpu_regs --log-file hatari.txt --tos ... --disk-a ...
  python3 tools/trace_diff.py neost.txt hatari.txt --align-pc FC0030 --regs
  ```

## Vérité matérielle : composant NeoST ↔ Hatari

`extern/hatari/src` = la référence (lue, pas compilée). EmuTOS
([github.com/emutos/emutos](https://github.com/emutos/emutos)) documente ce que le firmware
attend du matériel.

| NeoST                    | Hatari `src/`                                  |
|--------------------------|------------------------------------------------|
| `Bus` / MMIO bus errors  | `ioMem.c`, `ioMemTabST.c`, `ioMemTabSTE.c`     |
| `Bus` régions hors-IO    | `cpu/memory.c` (init_mem_banks, BusErrMem_bank)|
| `Bus::mmuTranslate`      | `stMemory.c` (STMemory_MMU_Translate_*)        |
| `Cpu68k`                 | `m68000.c`, `cycInt.c`, `cycles.c`             |
| 8/16 MHz + cache MegaSTE | `m68000.c` (`MegaSTE_CPU_Cache_Update`, `MegaSTE_Cache_*`, `mem_access_delay_*_megaste_16`) |
| `Fpu` (68881 optionnel)  | (Hatari n'émule pas le socket — réf. MC68881 UM §7 + AN-947, glue SFP004 MiNTLib ; émulation fonctionnelle, test : `tools/make_fpu_testrom.py`) |
| `Mfp`                    | `mfp.c` (timers A-D, modes, GPIP)              |
| `Ikbd` / `MidiAcia`      | `ikbd.c`, `acia.c`, `midi.c`, `keymap.c`       |
| `Shifter` / `Machine`    | `video.c` (HBL/VBL/Timer B, bordures, spec512), `screen.c` |
| `Fdc`                    | `fdc.c`, `floppy.c`                            |
| `Acsi` (disque dur ACSI) | `hdc.c` (routage DMA via `Fdc`)                |
| `Scc` (série Z85C30 Mega STE) | `scc.c` (IRQ niv5 via `Scu`)              |
| `YM2149` / `DmaSound`    | `psg.c`, `sound.c`, `dmaSnd.c`                 |
| `Blitter` / `Rtc`        | `blitter.c`, `rtc.c`                           |
| `GemdosHd` (disque dur GEMDOS) | `gemdos.c`, `cpu/hatari-glue.c` (`OpCode_GemDos/Pexec/SysInit`), `cart.c`/`cart_asm.s`/`cartData.c` |

### Disque dur GEMDOS (`GemdosHd`)

Redirection des appels GEMDOS d'un lecteur virtuel (C:…) vers un dossier hôte, au
lieu d'émuler un contrôleur ACSI/IDE. Activé par `--gemdos DIR` (headless) ou
`NEOST_GEMDOS_DIR` (GUI) ; **exclusif d'une cartouche externe** (`--cart`).

Mécanisme (port fidèle, adapté à Moira) :

1. **Cartouche système à `$FA0000`** : `setDirectory` recopie les octets assemblés de
   `cart_asm.s` (= `cartData.c`) dans `bus.cart`. Le TOS y détecte le magic
   `$ABCDEF42` et exécute son C-INIT (`sys_init`) au boot (drapeau bit 3 = après
   init GEMDOS, avant boot disque).
2. **Opcodes « illégaux » magiques** : le code cartouche déclenche les opcodes
   `$0008` (GEMDOS), `$0009` (PEXEC), `$000A` (SYSINIT). Hatari patche sa table
   d'opcodes ; NeoST/Moira les capte dans `Cpu68k::run` **avant `execute()`** : si le
   PC est dans la cartouche (`$FA0000-$FBFFFF`) et `bus.gemdos` actif, on appelle
   `GemdosHd::handleOpcode`, puis on remplace l'IRD par un `NOP` (`$4E71`) que le
   `execute()` suivant consomme (avance PC + prefetch + 4 cyc) — équivalent exact du
   `CpuDoNOP()` d'Hatari.
3. **`SYSINIT`** installe le hook : sauve l'ancien vecteur GEMDOS dans la cartouche
   (`CART_OLDGEMDOS=$FA0024`, écrit DIRECTEMENT dans `bus.cart`), pose `$84` →
   `CART_GEMDOS=$FA002A`, calcule `act_pd` (osheader+`$28`) et ajoute C: au masque
   `_drvbits` (`$4C2`).
4. **`GEMDOS`** (`GemdosHd::trap`) lit le n° de fonction sur la pile (USP si appelant
   user, SSP+6 sinon), dispatche, pose D0 et les codes condition **N/Z/V** du SR que
   le code cartouche teste : Z=1 → `rte` (traité), Z=0 → ancien vecteur (TOS), V=1 →
   Pexec.
5. **`PEXEC`** : `gemPexec` fait créer la basepage par le TOS (Pexec 5/7 via la
   cartouche) puis `pexecBpCreated` charge+relocalise le PRG depuis C:
   (`loadAndReloc`) et relance un Pexec « just-go » (6/4) pour l'exécuter.

Helpers Bus : `hostRamPtr(addr,len)` (pointeur RAM contigu, traduction MMU — port de
`STMemory_STAddrToPointer`+`CheckAreaType`) et `tosVersion` (en-tête ROM offset 2).
Debug : `NEOST_GEMDOS_TRACE=1` journalise hook, traductions de chemin et appels
fichier. Simplifications vs Hatari : `bUseTos` toujours vrai, pas d'images
ACSI/IDE (lecteurs dès C:), pas d'autostart INF ni de conversion de charset.

## Pièges matériels (vérifiés en debug)

- **Big-endian** : assembler les mots octet par octet (`read16` etc.).
- **Bus error = WHITELIST, pas blacklist** : règle word/long → l'accès ne faute que si TOUS
  ses octets fautent (`busFaultN`). C'est pourquoi `move.w $FF8204` marche mais
  `move.b $FF8204` faute. Les octets PAIRS du MFP (`$FFFAxx`) fautent (registres aux adresses
  **impaires** uniquement : `$FFFA01`, `$FFFA03`…).
- **Protection superviseur (GLUE)** : en mode utilisateur (bit S=0), `$0-$7FF` et TOUT
  l'espace IO `$FF8000-$FFFFFF` fautent AVANT la whitelist (`busFaultN(addr, n, write)`).
  Les écritures ROM TOS / cartouche / `$0-$7` fautent même en superviseur. Le CPU seul est
  concerné — blitter et DMA passent par `read8/write8` sans test (BusMode Hatari).
- **MegaSTE 16 MHz** : l'ordonnanceur reste en cycles BUS 8 MHz ; seul `Cpu68k` convertit
  (×2 sous `$FF8E21` bit1). Une boucle en RAM SANS cache ne va PAS plus vite à 16 MHz
  (accès cadencés bus) ; ROM et cache 16 Ko si (cf. `readMste16Mhz`/`chipWait16`).
- **VBL/HBL autovecteurs LATCHÉS** (comme Hatari, « cleared only when processed ») :
  `g_vblPending` reste armé tant que le CPU n'a pas servi l'IRQ (mask ≥ niveau). Si le SR
  ré-autorise le niveau 4 après une longue période masquée, la VBL en attente part AUSSITÔT
  vers `$70` → crash si le handler n'y est plus.
- **Vecteurs MFP** : canal = n° de source (Timer A=13, B=8, C=5, D=4, ACIA=6, FDC=7) ;
  vecteur présenté = `(VR & 0xF0) | canal`. En software-EOI (VR bit3), le handler DOIT
  effacer l'ISR sinon le canal reste bloqué.
- **Bits d'entrée GPIP** (moniteur bit7, ACIA bit4, FDC bit5) forcés en lecture — ne PAS les
  laisser écraser par une écriture CPU sur `$FFFA01`.
- **bit7 GPIP = 1 → moniteur couleur** (basse rés) ; 0 → mono (haute rés). Haute rés =
  **monochrome** (blanc/noir), ignorer la palette couleur.
- **Différences de modèle** (`IoMem_FixVoidAccess*`) : le ST (Ricoh) faute là où le Mega ST
  (IMP) est « void » (`$FF8002-$FF800D`) — un des signaux qu'EmuTOS lit pour distinguer les
  machines. Le STE expose le son DMA (`$FF8900`) et le joypad (`$FF9200`) ; le ST non.
