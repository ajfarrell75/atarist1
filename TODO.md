# TODO — NeoST

(c) 2026 VERHILLE Arnaud. **Ce qui reste à faire.** Le fait (implémenté + validé) est dans
`[CHANGELOG.md](CHANGELOG.md)` ; les diagnostics de bugs en cours sont en mémoire projet.

**Sources de vérité à croiser systématiquement :**

- **Hatari** (`extern/hatari/src/*.c`) — comportement ST/STE/MegaSTE éprouvé. La référence.
- **MAME** (`src/mame/atari/atarist.cpp`, `stmmu.cpp`, `stvideo.cpp`, devices `mc68901`,
`wd_fdc`, `6850acia`, `z80scc`, `rp5c15`, `ay8910`, `lmc1992`) — composants séparés.

**Objectif** : émuler proprement un **MegaSTE** (68000 8/16 MHz, 1/2/4 Mo, TOS 2.05/2.06,
STE video/sound/joypads, blitter, RTC, SCC, SCU, ACSI/SCSI, DD/HD) avec un timing assez
fidèle pour jeux, démos et utilitaires.

**Légende** : `lot suivant` = portable, faible risque · `précision cycle` = ordonnanceur daté
(`[docs/CYCLE_ACCURACY.md](docs/CYCLE_ACCURACY.md)`) · `risque élevé` = bus/IRQ éprouvé ·
`gros contrôleur` = puce entière · `faible valeur`.

**Validation** : catalogue logiciels étalon → `[docs/TEST_SOFTWARE.md](docs/TEST_SOFTWARE.md)`.
Ordre affichage : **Spectrum 512 ✅ → Enchanted Land → Cuddly Demos** (scrolling robot + scroller bordure basse).

---

## Catalogue logiciels — bugs en cours

Rapports terrain (2026-06). TOS 1.02fr sauf mention contraire. Chemins sous `disks/st/` (`.st`)
ou `disks/stx/` (`.stx`).

- **Arkanoid (1987)** (`Arkanoid (1987)(Imagine).st`) — plante sur la page de titre
  ```
  (« ARkanoid ») sans jamais arriver au jeu, même avec TOS 1.02fr. Cf. aussi §Précision cycle
  / FDC (gel titre → partie). Oracle : `--keys`/`--joy`, trace IRQ, diff Hatari.
  ```
- **Captain Blood (1988)** (`Captain Blood (1988)(ERE)(ST)[cr 42-Crew][one disk].st`) —
  ```
  arrive au jeu puis plante sur une erreur keyboard et redémarre.
  ```
- **Enchanted Land (1990)** (`Enchanted Land (1990)(Thalion).st`) — logo + gouttes Thalion
  ```
  OK ; son Talion absent (press bouton joystick) ; scrolling saute terriblement (Le personnage qui devrait rester au centre saute 'un endroit à l'autre de l'image. Un probleme de synchro de beam) en jeu (symptôme proche du bug Cuddly / sync-scroll). Cf. §Bordures.
  ```
- **Super Hang-On (1988)** (`Super Hang-On (1988)(Sega).st`) — démarre ; musique abîmée
  ```
  par un bruit blanc de fond anormal ; lignes colorées horribles sur les 3/4 bas de l'écran.
  À corriger (son DMA/PSG ? géométrie vidéo ?). Cf. CHANGELOG (retry secteurs FDC).
  ```
- **Shadow Warriors (2Hot2Handle)** (`ShadowWarriors[2Hot2Handle]-D1/2/3.stx`) — après
  ```
  SPACE : page de titre + musique OK ; appuyer sur un bouton joystick ne lance pas le jeu.
  (Castle Warrior fonctionne parfaitement.)
  ```
- **Rick Dangerous II (1989)** (`Rick Dangerous II (1989)(Core Design)[cr Empire][t +2][a].st`) —
  ```
  SPACE, puis `n`, encore `n` : plante avec 4 bombes.
  ```
- **Stardust (1994)** (`Stardust (1994)(Daze Marketing Ltd.)(Disk 1 of 3)[cr Hardcore][t].st`) —
  ```
  plante sur écran noir.
  ```
- **Lethal Xcess** (`Lethal_Xcess_Disk_1.STX`) — ~~écran noir~~ **DÉMARRE (2026-06-14)** :
  fix = wait-state +2 (valeur d'abord) sur la lecture `$FF8209`, validé oracle sur LX **et**
  Enchanted Land (cf. CHANGELOG §Vidéo). *Reste* le beam-sync en jeu (image qui saute, voir
  ci-dessous — commun à EL/Cuddly).
- **Stardust Bloodhouse** (`stardust_bloodhouse_a/b/c.STX`) — plante au démarrage
  ```
  (écran noir).
  ```
- **Wings of Death** (`Wings_Of_Death_Disk_1/2.stx`) — après bouton : page de titre
  ```
  avec forte corruption graphique ; chargement avec son ralenti/bizarre ; SPACE lance le jeu
  qui fonctionne très bien ensuite.
  ```
- **The Cuddly Demos** (`disks/etalons/cuddly_demos.msa`) — première page OK mais son de
  ```
  mauvaise qualité ; après une touche, menu de sélection (robot) : scrolling complètement
  bugué qui saute. Cf. §Bordures (items 5-6).
  ```

---

## 🔬 Divergences Hatari restantes — cf. [docs/HATARI_DIVERGENCES.md](docs/HATARI_DIVERGENCES.md)

Audit complet NeoST↔Hatari (workflow 8 agents, 2026-06-15). Fidélité globale **très élevée**.
**Corrigés cette passe** (✅) : Blitter compteur 0=65536 + rejet accès octet, filtre LPF STF
câblé, miroir PSG `$FF8800-$FF88FF`, MIDI (master-reset sans purge / RDR persistant). Le doc
ci-dessus est l'inventaire maître (sévérité + impact + `fichier:ligne` des deux côtés).
**Différés** (ordre = valeur décroissante) :

**Cycle-exact** — bloqués par l'oracle Hatari headless (le binaire `extern/hatari/build/src/hatari`
n'est PAS bâti dans un conteneur frais ; à reconstruire pour valider au pixel). Recoupent le
chantier **beam-sync** ci-dessous :
```
• Vidéo — branche STE de la Glue (V1) : timings preload STE, LEFT_OFF_2_STE (video.c:2442-2652).
• Vidéo — tricks par changement de résolution (V2) : overscan med-res, scroll hardware
  hi/med/lo (Video_WriteToGlueRes video.c:1618). [recoupe §Vidéo « med-res overscan »]
• Vidéo — géométrie mid-trame (V3) : 50↔60 Hz en cours de trame, Video_RestartVideoCounter
  (video.c:2857,4608). [recoupe le chantier beam-sync §Précision cycle]
• Son DMA — FIFO 8 octets + avance HBL (S2). [recoupe §Son DMA STE]
• FDC — flush FIFO↔RAM ne stalle pas le CPU (D3 : 32 cyc, fdc.c:1340/1396).
```
**Décisions de comportement** *(tranchées)* :
```
• ✅ SCC WR14 bit4 « loopback » actif au reset (SC1) — TRANCHÉ : NeoST HONORE le bit (Local
  Loopback, datasheet Zilog Z85C30) ; Hatari ne le modélise pas. Inoffensif (tout pilote série/
  LAN réécrit WR14 avant d'émettre) → NeoST reste plus fidèle au chip. NE PAS « corriger ».
  Documenté Scc.cpp:serialWriteByte + docs/HATARI_DIVERGENCES.md (§« NeoST améliore Hatari »).
```
**Marginaux** (faible valeur / risque > bénéfice) :
```
• $FF8264 STE (scroll « no prefetch ») lu en void 0xFF au lieu du registre réel — valeur
  floue chez Hatari (latch IoMem, Video_HorScroll_Read_8264 ne renvoie rien).
• FDC piste « standard » de repli 6268 vs 6250 (D4) — partagé avec la détection densité .ST,
  à isoler (constante STX dédiée) avant de changer.
• FDC reset DMA : ne pas effacer le bit « erreur » dans dmaResetFifo (F1, cf. fdc.c:1233).
• MFP — lignes GPIP on-chip (ACIA/FDC/blitter) lèvent l'IRQ hors machine de fronts AER/DDR
  (M1) — risque de régression IRQ, faible impact (EmuTOS laisse AER=0).
```
*(D1/D2 — ré-interprétation WRITE/READ TRACK STX — sont des choix où NeoST fait MIEUX que
Hatari ; déjà notés « FAIT » en §FDC, à NE PAS « corriger » vers Hatari.)*

### 2ᵉ passe (2026-06-15) — nouvelles trouvailles (4 correctifs mergés confirmés CORRECTS)
**✅ CORRIGÉS** (validés glue-selftest 19/0 + boots ST/STE/MegaSTE pixel-identiques) :
```
• [BUG] Bus — leak ioAccessWidth_ : write16/write32 branche blitter restaurent désormais
  ioAccessWidth_ avant le return (Bus.cpp) → bus-errors d'accès OCTET ($FF9200/lightpen/FDC)
  ré-armées après un blit.
• [MOYENNE] Blitter — GPIP3 (GPU_DONE) ré-armé haut au (re)démarrage de chaque blit (start(),
  setBlitterLine(false)) ; finishTransfer le rabaisse → front correct à chaque fin de blit.
• [MOYENNE] MIDI M-MIDI — rdrf_ MIDI distinct de !rx_.empty() : master reset efface RDRF sans
  purger la file (MidiAcia.cpp/.hpp).
```
**⏸️ DIFFÉRÉS — validation impossible sans oracle/écoute (à reprendre quand l'oracle est bâti)** :
```
• Son S3 — gain LMC ½-ampli : table DAC pleine + outScale_=0.5 SANS le ×2 d'Hatari → YM STE
  ~6 dB trop bas (DmaSound.cpp:274-283). AUDIO non vérifiable headless (risque déséquilibre/clip).
• FDC — changement lecteur/face « pull » au lieu de « push » (refreshDriveSide à l'écriture PSG,
  FDC_SetDriveSide). Ré-ancre l'index du modèle rotationnel → risque de régresser des chargements
  disque sans oracle byte-exact.
• MFP — pas de MFP_UpdateTimers avant lecture IPR/ISR (bit pending ≤1 instr. en retard) — fix
  architectural (réentrance du dispatch d'événements depuis une lecture registre).
```
Détail + basses cycle-exactes/niche (vidéo, FDC, son, blitter BL-R/BL-MST, bus N2-N5) →
`docs/HATARI_DIVERGENCES.md` §2ᵉ passe.

### 3ᵉ passe (2026-06-15) — sous-systèmes périphériques (RTC/ACSI/GEMDOS/FPU/SCU, jamais audités)
**✅ CORRIGÉS** (build + glue 19/0 + boots ST/STE/MegaSTE identiques + **FPU test ROM 9/9**) :
SCU reset · FPU propagation NaN · FPU SNaN→SNAN · FPU masques FPCR/FPSR · FPU FSGLMUL (troncature
24b) · FPU FSCALE ∞/NaN (NaN→propagation, ∞→OPERR, plus d'UB) · FPU octet AEXC (UNFL si INEXACT,
INEX sur OVFL) · ACSI INQUIRY buf[4]=31 · GEMDOS `.`/`..` en sous-répertoire (param subdir) ·
**GEMDOS only_invalid** (passe troncature + passe caractères-invalides séparées, `filenameInvalidChar`
port de `Str_Filename_Invalid_Char`) · **FPU FMOVECR INEX2+arrondi** (table `fpp_cr` `inex`+`rnd[4]`
RN/RZ/RM/RP portée → arme `EXC_INEX2`/`AEXC_INEX`).
**⏸️ Reste à faire** (table de données / plateforme / plomberie / non vérifiable headless) : ACSI
délai IRQ post-transfert ; GEMDOS Unicode macOS ; FPU arrondi de précision FMOVE/FABS/FNEG (plomberie
softfloat) ; FPU packed decimal.
Cf. `docs/HATARI_DIVERGENCES.md` §3ᵉ passe.
Intentionnels / NeoST plus correct (NE PAS corriger) : RTC temps émulé (déterminisme headless),
RTC Mega-only (RP5C15 physiquement Mega), SCU encodage bit IRQ1, transcendantes FPU host.

### 4ᵉ passe (2026-06-15) — couches d'intégration (CPU/Moira, E/S) — dernier terrain LOGIQUE
**✅ CORRIGÉS** (build + glue 19/0 + boots ST/STE/MegaSTE identiques) :
• CPU — SCC No-Vector renvoyait l'auto-vecteur 29 ($74) au lieu du vecteur spurious 24 ($60) sur
  IACK niveau 5 (`Cpu68k.cpp`).
• Centronics — **support imprimante ajouté** : capture des octets dans un fichier (`Machine::
  setPrinterFile`, headless `--printer FILE`) + BUSY (GPIP0) sur strobe, port de `psg.c:388-390`.
  Validé : mini-ROM imprimant « NeoST\n » → fichier capturé identique. (GUI : à câbler via la même API.)
Reste **fidèle** : intégration CPU (trame bus-error, double-fault→HALT, IACK MFP, reset, gating
IPL/SCU, wait-states) et E/S (StePads, paddles, JoystickInput). Cf. `docs/HATARI_DIVERGENCES.md` §4ᵉ passe.

> 🚧 **Terrain LOGIQUE épuisé** (4 passes : toutes les puces + périphériques + intégration). Les
> divergences restantes sont **cycle-exactes** (beam-sync ci-dessous, S2/D3/M1, latence exception/IPL)
> → bloquées tant que l'oracle Hatari n'est pas bâti (**SDL2 absent du conteneur**).

### 🔮 Travaux à reprendre UNE FOIS L'ORACLE HATARI DISPONIBLE (session avec SDL2)

> **Pré-requis : bâtir l'oracle headless.** `extern/hatari/` est déjà cloné (commit `c9906f1`).
> Installer SDL2 (`libsdl2-dev`/`sdl2`), puis :
> ```sh
> cmake -S extern/hatari -B extern/hatari/build -DCMAKE_BUILD_TYPE=Release -DENABLE_SDL2=1
> cmake --build extern/hatari/build -j        # → extern/hatari/build/src/hatari
> ```
> Comparaison cycle-exacte (cf. `docs/HATARI_AUTOMATION.md`) : `hatari --trace video_addr,video_sync`
> + `--cmd-fifo`/`hatari-event keypress 57` (atteindre les menus in-game) face aux traces NeoST
> `NEOST_VC_TRACE=1` / `NEOST_SYNC_TRACE` (même format) ; non-régression `tools/run_etalons.py --max 0`.

**Items qui exigent l'oracle pour être traités/validés (par priorité d'impact) :**
```
1. [IMPACT JOUEUR] BEAM-SYNC — phase CPU↔faisceau cycle-exacte (§Précision cycle ci-dessous).
   Casse 4 jeux : Lethal Xcess, Enchanted Land, Cuddly Demos, Super Hang-On. C'EST le chantier.
   Implique : dater TOUT accès MMIO vidéo en FIN d'accès bus + align 4-cyc systématique ;
   latence d'entrée d'exception (+4 vs +0 Moira) ; échantillonnage IPL au cycle ; jitter E-Clock IACK.
2. [VIDÉO] V1 branche STE de la Glue · V2 tricks par changement de résolution (overscan med-res,
   scroll hardware) · V3 géométrie mid-trame (50↔60 Hz, RestartVideoCounter). Validation pixel oracle.
3. [SON] S2 FIFO 8 octets DMA + avance HBL (réalignement mono→stéréo) · S3 gain LMC ½-ampli
   (~6 dB) — validables par dump WAV oracle (YM_250_DEBUG) + comparaison.
4. [FDC] D3 stall FIFO 32 cyc · drive/side « push » — validables par trace FDC byte-exacte.
5. [MFP] M1 GPIP on-chip via machine de fronts AER/DDR · UpdateTimers avant lecture IPR — vérif latence.
6. [FPU] arrondi de précision FMOVE/FABS/FNEG selon précision FPCR — validable par ROM de test
   étendue (FMOVECR INEX2+rndoff désormais ✅ FAIT).
```
**Sans oracle (faisables hors session SDL2) :** FPU packed decimal bit-exact (algorithme
softfloat_decimal) ; GEMDOS Unicode macOS (sur une cible macOS) — documentés
`docs/HATARI_DIVERGENCES.md`.

---

## 🎯 Précision cycle

> Plan : `[docs/CYCLE_ACCURACY.md](docs/CYCLE_ACCURACY.md)` · Inventaire :
> `[docs/CYCLE_EXACT_INVENTORY.md](docs/CYCLE_EXACT_INVENTORY.md)`.
>
> Phases 1-6, latch palette Spec512, alignement bus shifter + wait states PSG/MFP/ACIA,
> machine Glue live, VDE_On live, Spec512 pixel-perfect, bordures haut/bas/gauche/droite :
> **FAIT** (cf. CHANGELOG).

### 🏗️ CHANTIER MAJEUR — horloge CPU↔vidéo UNIQUE (façon Hatari) [beam-sync]

> 📌 **AUTORITÉ COURANTE = [`docs/MOIRA_WINUAE_CONVERGENCE.md`](docs/MOIRA_WINUAE_CONVERGENCE.md)**
> (session 2026-06-16/17). Cette section ci-dessous est l'ANCIEN dossier (2026-06-14), conservé pour
> l'historique des pistes éliminées, mais ses CONCLUSIONS sont SUPERSÉDÉES. État à jour :
> - ✅ **Convergence INSTRUCTION** Moira↔WinUAE = COMPLÈTE (`NEOST_RAM_SLOT` align créneau bus + fix DIV
>   fork Moira ; harnais différentiel `NEOST_TRACE_CYC` + `trace_diff.py --periods`). Gated/sûr.
> - ✅ **Deadlock Enchanted Land = RÉSOLU** : dispatch BLOC par défaut (le sync-driven mid-instruction
>   était net-négatif → opt-in `NEOST_SYNC_DISPATCH`). Intro/écrans statiques propres.
> - ◑ **Corruption EL EN JEU (scroll)** = caractérisée, NON résolue : chantier vidéo **V3 multi-couches**
>   (largeur d'affichage RIGHT_OFF + longueur de ligne + chemin d'adresse). Banc de repro+validation
>   `tools/make_respulse_test.py`. ⚠ La Glue est FIDÈLE (pas de branche manquante) — ne pas y toucher.
> - ❌ Pistes superSÉDÉES ci-dessous : « le beam-sync EL/Cuddly est V2 » (faux — V2 ne couvre pas le
>   fin-de-ligne), « micro-timing CPU↔WinUAE » comme cause du scramble (la convergence instr est faite,
>   EL en jeu = V3 vidéo, pas le CPU). Lire le doc maître AVANT de rouvrir.

> Dossier de reprise (recherches 2026-06-14, workflow oracle 8 agents + impl + diff Hatari).
> C'est LA cause racine commune de plusieurs bugs ; cadrée et instrumentée, reste à
> implémenter le fond. NE PAS re-creuser les pistes éliminées ci-dessous.

**Symptôme (1 seul bug, 4 jeux) :** en jeu, l'image SAUTE trame à trame — **Lethal Xcess**
(en jeu), **Enchanted Land** (sync-scroll), **Cuddly Demos** (menu robot : bordure basse
qui clignote, scroller écrasé), **Super Hang-On** (écran de course : splits raster mélangés,
table des rangs sur la route). Les écrans STATIQUES (menus, splash) sont OK ; seul le
RENDU COMPOSÉ PAR RASTER (splits `$820A/$8260`/palette/base vidéo synchronisés au faisceau)
casse.

**Cause racine établie (diff oracle décisif) :** NeoST et Hatari **ne partagent pas une
horloge CPU↔vidéo au cycle près**. Le CPU atteint un point de code (poll `$FF8209`, écriture
`$820A`) à une **position-faisceau décalée** de Hatari — **moyenne ~28 cyc + jitter** trame à
trame. Le jeu poll `$FF8209` puis écrit en conséquence ; comme la phase CPU↔faisceau dérive,
l'écriture (ex. retrait bordure basse) tombe parfois à la bonne ligne, parfois non → clignote.

**PISTES ÉLIMINÉES (ne pas y revenir) :**
```
✗ Longueur de ligne variable 508/512 : instrumentée (NEOST_VARLINE_TRACE) = −4 cyc seulement.
✗ Décalage d'origine de trame (VblVideoCycleOffset=64 STF/68 STE) : testé NEOST_ORIGIN_OFF,
  le write cyc reste INVARIANT (le jeu est self-référentiel via son poll) + ça dégrade.
✗ Géométrie/base/startLine/formule du compteur : à pc=6097c (poll Cuddly juste avant le
  write) le jeu lit la MÊME valeur 7dec0 dans NeoST et Hatari. Formule = port fidèle.
✗ Offset de datation des writes (kSyncWriteOffsetCyc) : le besoin empirique +40 contredit
  le modèle +2 → fudge, casserait Enchanted Land (calibré +16). Abandonné.
✗ ALIGNEMENT BUS PAR-ACCÈS (RAM ou I/O) — FALSIFIÉ À L'ORACLE (2026-06-15). La source
  Hatari (memory.c:1777-1798) aligne au créneau 4-cyc la RAM ST (CHIP16/wait_cpu_cycle) mais
  PAS l'I/O ni la ROM (FAST16, « no bus wait for IO memory »/« from ROM », mesuré sur STF).
  MAIS un chipWait8 par-accès (miroir 8 MHz du chipWait16 MegaSTE, gated NEOST_RAM_SLOT)
  SUR-COMPTE : boucle de poll $FF8207 overscan_top pc=1736 = **32 cyc/itér dans NeoST flag-OFF
  ET dans Hatari (oracle --trace video_addr)** ; flag-ON → 36 (+4 parasite). ⇒ le timing
  d'instruction de Moira intègre DÉJÀ le bus ; le répliquer explicitement double-compte.
  L'écart réel est CHIRURGICAL par boucle (LX $14ef6 22 vs 24 ; EL $ee78 18 vs 20 ; +2 sur
  motifs NON slot-phasés), pas un alignement uniforme. NE PAS re-tenter l'alignement par-accès.
La SEULE différence mesurée : le X (cycle dans la ligne) où le CPU échantillonne la valeur
(figée en bordure droite) — NeoST L260 X=488 vs Hatari X=376. = phase CPU↔faisceau pure.
↳ DIRECTION CONFIRMÉE (oracle 2026-06-15) : périodes de boucle = Hatari → l'offset ~28 cyc +
  jitter vient de la PHASE D'ENTRÉE trame/boucle (prise d'IRQ VBL/Timer-B + entrée exception),
  PAS du timing d'instruction ni du bus. Attaquer la latence IRQ/exception cycle-exacte d'abord.
✗ « ~28 cyc systématique » — N'EXISTE PAS (workflow oracle 5 agents, 2026-06-16). C'était une
  mésattribution du NUMÉRO d'exception VBL (28 = 24+niv 4), pas un cycle. VBL @ cycle 64 des DEUX
  côtés, PCs interrompus IDENTIQUES. La divergence est UNIQUEMENT le JITTER de phase.
✗ HblJitterArray/VblJitterArray (jitter statique 0/4/8) — MORT dans ce Hatari (déclaré jamais
  défini ni référencé, includes/video.h:162-169 ; commentaire périmé video.c:200). NE PAS porter.
✓ Vrai jitter auto-vecteur = synchro E-CLOCK (M68000_WaitEClock, m68000.c:810) : 0..8 cyc selon
  la phase bus à l'IACK, sur HBL (niv2)/VBL (niv4) SEULEMENT (pas MFP/SCC). Moira (68000 générique)
  ne l'a pas. PORTÉ (NeostMoira::willInterrupt, opt-in NEOST_ECLOCK_ON, commit 08a10f4) mais
  DÉFAUT OFF : non validable en jeu headless (écrans cassés inatteignables), risque double-comptage.
⚠ LEÇON STRUCTURELLE (chipWait8 + E-clock) : les hacks de datation vidéo EMPIRIQUES de NeoST
  (syncCpuBus par-registre, kSyncWriteOffsetCyc=+16, vcWait=2) AGRÈGENT déjà le timing CPU↔faisceau
  (étalons pixel-exact OK). Ajouter un mécanisme fidèle isolé (align bus, E-clock) DOUBLE-COMPTE.
  La vraie cycle-exactitude exige une REFONTE COORDONNÉE : retirer les hacks ET ajouter les
  mécanismes fidèles ENSEMBLE, puis recalibrer à l'oracle. Le résidu dominant = micro-écarts de
  timing d'instruction Moira↔WinUAE (boucles ±2 cyc) papier-mâchés par les hacks par cas.
✗ overscan_lr rendu À PLAT sous NeoST (pas de bandes per-ligne comme l'oracle) = BUG RENDU SÉPARÉ
  (les tricks bordure L/D per-ligne via gestionnaire HBL ne sont pas rendus live) → bloque la
  validation HBL-raster (et donc E-clock). À corriger pour débloquer un étalon HBL fonctionnel.
  Diag dispo : NEOST_VBL_TRACE (dépassement service VBL = pending_cyc Hatari ; NeoST ~7.8 vs 4.3),
  tools/beamsync_diff.sh (diff cycle NeoST↔oracle IRQ/VBL/compteur).
✅ SYMPTÔME EL EN JEU ATTEINT + ROOT-CAUSÉ (2026-06-16). Recette navigation headless :
  `--frames 13000 --joy-at 3100 0x80` (fire maintenu dès le logo Thalion ~3000 → jeu rocheux ~13000).
  L'image SCRAMBLE plein écran trame à trame : les trames avec impulsions **hi-res per-ligne** (res=02/
  res=00) scramblent, celles à `frq` seul sont propres. PROUVÉ que ce N'EST PAS le timing CPU↔faisceau :
  E-clock neutre (OFF==ON), VC_WAIT/VC_OFF sans effet, SYNC_OFF ne bascule que scramble↔hang noir
  (aucune valeur propre). **VRAIE CAUSE = V2 (changement de résolution)** : la machine Glue ne gère pas
  les res-switch hi-res intra-ligne (ouverture bordures G/D fullscreen) → état de ligne faux → scramble +
  calibration qui ne converge jamais. ⇒ Le « beam-sync » EL/Cuddly est en fait le chantier **V2**
  (`Video_WriteToGlueRes`, overscan med-res, rendu mixte hi/med/lo), PAS l'IRQ/bus/E-clock. La phase
  CPU↔faisceau est, elle, cycle-accurate (détection des tricks OK, périodes = Hatari, pas de décalage).
  ⚠ MÉCANISME PRÉCIS (vérifié source, 2026-06-16) : EL en jeu écrit ses impulsions hi-res à cyc
  **8-16 (PRÉCOCES)** → branches Hatari `video.c:2246/2268/2288` qui posent `HBL_Pos=220` +
  `nCyclesPerLine_new=224` → **la ligne est RACCOURCIE à 224 cyc** (HBL reprogrammé, `Video_AddInterruptHBL`
  video.c:2849). NeoST garde le HBL à `cpl-4` FIXE chaque ligne (grille trame figée) → phase du
  gestionnaire fullscreen NE DÉRIVE PAS → scramble. **Le port exige une LONGUEUR DE LIGNE VARIABLE**
  (224/512 par ligne, vs grille 512 fixe actuelle) + couplage live Shifter↔Scheduler pour
  reprogrammer le HBL — CHANGEMENT ARCHITECTURAL du modèle de timing trame (Machine::runFrame/
  scheduleFrameEvents), RISQUÉ (le HBL ancre top/bottom + Timer-B). ⚠ **overscan_lr N'EST PAS un
  oracle valide** (calibré POUR NeoST, `make_overscan_lr.py` PAD1=12 → blanc plein VOULU = ce que
  NeoST produit ; les bandes Hatari = PAD NeoST inadaptés à Hatari). Valider le port sur EL en jeu
  (recette `--frames 13000 --joy-at 3100 0x80`) + oracle, PAS overscan_lr. Workflow diag wf_3184aab0-31c.
```

**FAIT (committé) :**
```
• 3811869 : wait-state +2 « valeur-d'abord » sur la LECTURE $FF8205/07/09 (Shifter::read8).
  Aligne le timing des boucles serrées `move.b $8209,d0 / beq` (T 22→24 = Hatari). Débloque
  Lethal Xcess (atteint $30142) et Enchanted Land (atteint son jeu). Étalons --max 0 OK.
• fb3688f FIX1 : ancre frameStart_ au VBL THÉORIQUE (frameStart_ += lpf_*cpl_ au lieu de
  sched.now(), retranche le carry δ), port VBL_ClockCounter (video.c:4964). Co-ancre events
  + datation. (N.B. n'a PAS réduit le jitter Cuddly → la cause dominante est ailleurs.)
• fb3688f FIX2 : syncCpuBus() sur $FF820A (manquait vs $FF8260/palette), aligne l'accès sur
  4 cyc (port wait_cpu_cycle_write). Writes alignés (418→420).
Reste : Cuddly bordure basse ouverte seulement ~15% (cible ~100%), l'image saute encore.
```

**MODÈLE DE RÉFÉRENCE Hatari (ce qu'il faut approcher) :** une SEULE horloge globale
`CyclesGlobalClockCounter` partagée CPU+vidéo, avancée au cycle.
```
• Origine vidéo : VBL_ClockCounter = GlobalClock − PendingCyclesOver − VblVideoCycleOffset
  (video.c:4964) → ancre théorique, carry retranché. "cycles since VBL" = GlobalClock − VBL_CC.
• Datation accès : Video_GetCyclesSinceVbl_On{Read,Write}Access (video.c:1282,1289) =
  cycles_since_vbl + Cycles_GetInternalCycleOn{Read,Write}Access (cycles.c:134,180) =
  currcycle*2/CYCLE_UNIT + 4 (= FIN d'accès bus), APRÈS alignement 4 cyc (wait_cpu_cycle_*,
  cpu/custom.c:140,235). Read = write − 8 sur le chemin ADRESSE seulement (video.c:1395).
• L'accès bus MMIO patiente TOUJOURS jusqu'à la frontière de 4 cyc (slot=(GlobalClock+
  currcycle*2)&3) AVANT get/put_byte → phase déterministe. VblVideoCycleOffset = 64/68.
```

**MODÈLE ACTUEL NeoST (à faire évoluer) :** CPU en QUANTA + ordonnanceur, pas une horloge
unique cyclée.
```
• Machine::runFrame (Machine.cpp:271) : cpu.run(want) exécute par BLOCS jusqu'au prochain
  event ; sched.runTo déclenche les handlers échus. beamClock_/liveFrameClock_ =
  sched.liveNow() − frameStart_ ; liveNow = sched.now() + cpu.cyclesRunInQuantum()
  (= horloge Moira live, Cpu68k.cpp:382). Datation : kVideoCounterReadOffsetCyc=−2 (read,
  Shifter.cpp:61) + wait +2 ; kSyncWriteOffsetCyc=+16 (write, Shifter.cpp:576) ; syncCpuBus
  align 4 cyc (Shifter.cpp:560).
• Écart vs Hatari : (a) Moira date write8/read8 au MILIEU de l'accès (début+2,
  MoiraDataflow_cpp.h:417) vs FIN (+4) chez Hatari ; (b) les wait-states bus (4-cyc align)
  ne sont posés que sur CERTAINS registres, pas tout accès MMIO comme wait_cpu_cycle_* ;
  (c) latence d'IRQ (HBL/Timer B) et timing instruction non strictement identiques → la
  position-faisceau du CPU au poll dérive (moyenne ~28 + jitter).
```

**DIRECTION DU FIX — ⛔ ANCIENNE (alignement bus) FALSIFIÉE le 2026-06-16, cf. §PISTES ÉLIMINÉES.**
Moira matche déjà le timing d'instruction (périodes de boucle 32=32, boot 0 divergence).

**🟢 PERCÉE (2026-06-16, /effort) — L'E-CLOCK CORRIGE LA STRUCTURE DE PHASE CPU↔FAISCEAU, VALIDÉ
À L'ORACLE. Premier levier convergence NON falsifié. (⚠ la note « E-clock neutre » était PÉRIMÉE :
testée in-game seulement, non calibrée — FAUX sur la structure de phase.)**
```
NOUVEL ORACLE DE PHASE (headless, la pièce qui manquait pour mesurer la convergence) :
• Test = tools/make_poll_test.py : handler HBL lit $FF8209 → palette[0] CHAQUE ligne → chaque
  scanline = couleur f(compteur à son HBL). Déterministe, à timing fixe (pas auto-calibrant).
• NeoST `NEOST_VC_TRACE` (valeur+X/ligne) vs Hatari `--trace io_read` + screenshot oracle.
• MÉTRIQUE = PÉRIODE du beat des bits bas (position fine du read) par ligne. Robuste au jitter.

MESURE DÉCISIVE :
• La PENTE (≈160 octets/ligne) MATCHE Hatari → le débit d'instructions Moira est bon (confirme).
• NeoST E-clock OFF = beat PÉRIODE-3 {0,2,4} ; Hatari = PÉRIODE-5 {0,2,4,6} ; NeoST E-clock ON
  = PÉRIODE-5. ⇒ la divergence N'EST PAS un offset constant (d'où l'échec de TOUS les sweeps
  d'offset, §PISTES) mais un BEAT structurel = la latence d'ENTRÉE d'exception auto-vecteur.
• L'E-clock (M68000_WaitEClock, jitter d'entrée HBL/VBL {0,8,6,4,2} = PÉRIODE-5) EST le mécanisme :
  `willInterrupt` (Cpu68k.cpp:227), opt-in `NEOST_ECLOCK_ON` + `NEOST_ECLOCK_PHASE` (déjà implémenté
  commit 08a10f4, JAMAIS validé jusqu'ici). Diff plein-cadre du test vs Hatari : OFF 56 % → ON
  phase 8 = 34 % (÷1.6).
• ✅ ÉTALON-SAFE : run_etalons (glue/overscan_top/spec512/scroll_8264-65/etos_ste_boot) =
  BYTE-IDENTIQUE (0 px) avec `NEOST_ECLOCK_ON=1 NEOST_ECLOCK_PHASE=8`. Le « double-comptage »
  redouté (mémoire) NE se manifeste PAS → E-clock ON est SÛR. Candidat default-ON.

REFONTE COORDONNÉE (re-test datation fidèle AVEC E-clock ON, 2026-06-16) — RÉSULTAT :
• Le « −8 » fidèle est RÉFUTÉ une 2ᵉ fois, MAINTENANT avec E-clock ON : sweep NEOST_VC_OFF sur le
  poll-oracle → offset effectif +4 (actuel) = MEILLEUR (34 % diff) ; −4 (le « −8 ») = 60 % (PIRE,
  bits bas saturés à 0). ⇒ le résidu N'EST PAS la datation lecture (déjà optimale à +4).
• La FORMULE E-clock NeoST est FIDÈLE (identique à Hatari M68000_WaitEClock m68000.c:815 :
  `10 - clock%10`, pattern [0 8 6 4 2]). Donc ni datation ni formule E-clock.
• RÉSIDU re-pointé = motif multiset NeoST {2,4,6} vs Hatari {0,2,4,6} (période-5 mais multisets
  ≠ → la PHASE ne peut PAS les aligner, sweepée 0-9). Cause = le POINT D'APPLICATION de l'E-clock
  (NeoST willInterrupt AVANT empilement Cpu68k.cpp:227 vs Hatari pendant l'IACK iack_cycle) couplé
  à la PHASE DE RECONNAISSANCE d'IRQ par-instruction Moira↔WinUAE. ⟵ COUCHE LA PLUS PROFONDE.
  PROCHAINE EXPÉRIENCE (deep, payoff headline incertain car E-clock neutre sur la base EL) : aligner
  le point de reconnaissance/d'application IPL de Moira sur WinUAE (POLL_IPL en frontière + règle
  cpuipldelay ≥4 cyc ; appliquer le wait E-clock au cycle d'IACK, pas avant l'empilement).

EL EN JEU : E-clock NEUTRE sur la base ($8203=80 CONSTANT ON==OFF à frame 13000 ; E-clock ajoute du
jitter de cyc d'écriture sans changer la valeur). ⚠ La recette `--frames 13000 --joy-at 3100 0x80` est
PÉRIMÉE pour le build courant (NOIR ON et OFF) → à RE-DÉRIVER avant tout test du scramble in-game.

RECOMMANDATION : garder E-clock ON comme FONDATION du chantier (faithful + sûr). PROCHAIN PAS = refonte
coordonnée datation+E-clock, puis re-dériver la recette EL in-game et valider.
```

**🟡 IMPLÉMENTÉ (2026-06-16) MAIS HYPOTHÈSE FALSIFIÉE — l'ordonnanceur piloté par `Moira::sync()`
NE corrige PAS le jitter (mais reste la FONDATION sub-instruction nécessaire au sync-driven/PT).**

**SOLUTION (implémentée, conservée) — ORDONNANCEUR PILOTÉ PAR `Moira::sync()` (modèle `do_cycles` WinUAE)**
```
RACINE du jitter = NeoST exécute le CPU en BLOCS (Cpu68k::run lance N instructions PUIS
Scheduler::runTo dispatche les événements À LA FRONTIÈRE DE BLOC). La broche IPL est donc
posée ~1 instruction APRÈS le cycle réel de l'événement → l'IRQ (Timer-B/VBL) est reconnue à
la mauvaise instruction → la boucle de calibration fullscreen reprend à une phase-faisceau
jittery. Mesuré sur EL en jeu : « préemptions=427, timer retard max=156 cyc » ; la base vidéo
($8203) saute ±0x800 trame à trame (joueur immobile) → scramble (cf. [[beamsync-busalign-falsified]] §5-7).

FIX = piloter le Scheduler DEPUIS le hook cycle de Moira (comme WinUAE pilote le chipset via
do_cycles), pour que les événements tombent au CYCLE EXACT en cours d'instruction et que la
broche IPL soit à jour quand le POLL_IPL de Moira l'échantillonne :
• Moira FOURNIT DÉJÀ le hook : `virtual void sync(int cycles)` (extern/moira/Moira/Moira.h:286),
  appelé via la macro SYNC(x) (MoiraMacros.h:19) aux bons sous-points microcode de CHAQUE
  instruction. Config MOIRA_VIRTUAL_API=true → overridable. AUCUN changement du cœur Moira requis.
• Override `NeostMoira::sync(int n)` (src/core/Cpu68k.cpp, classe NeostMoira ~l.63) : avancer le
  Scheduler de n cycles et DISPATCHER les événements échus EN COURS d'instruction (donc appeler
  setIPL au cycle exact). Le POLL_IPL (MoiraMacros.h:64) / checkForIrq (Moira.cpp:417) verra alors
  l'IRQ au bon cycle.
• Refondre Machine::runFrame (src/core/Machine.cpp:286) : le dispatch d'événements migre du
  runTo post-bloc vers le chemin piloté par sync(). Scheduler::runTo/schedule restent, mais
  appelés incrémentalement depuis sync.
• ⚠ PERF (raison du modèle en blocs à l'origine) : sync() est appelé souvent. Ne PAS re-scanner
  tout le Scheduler à chaque appel — garder un `nextDue_` caché et ne dispatcher que si
  `clock_courant >= nextDue_` (comparaison O(1)). Hatari fait pareil (PendingInterruptCount).
• Supprime la quantification frontière-de-bloc = le jitter DOMINANT. Devrait faire converger la
  calibration EL/LX → base stable → plus de scramble/saut.

SI LE JITTER PERSISTE (second ordre, Moira-interne, fignolage faible retour) :
1. Fenêtre d'échantillonnage IPL : aligner les points POLL_IPL de Moira sur la règle WinUAE
   cpuipldelay2/4 (« le changement IPL doit arriver ≥4 cyc avant la fin d'instruction sinon
   reporté d'une instruction », extern/hatari/src/cpu/newcpu.c:4982 ipl_fetch_next).
2. Cycle d'accès bus intra-instruction : la lecture $FF8209 tombe ±2 cyc différemment (preuve =
   hack vcWait=2, Shifter.cpp:1173) → aligner les tables de timing d'accès de Moira sur WinUAE
   par mode d'adressage. NB : on ne « corrige » plus Moira, on le fait MIMER WinUAE.
```

**📊 RÉSULTAT (2026-06-16, implémenté + mesuré A/B baseline vs nouveau) — HYPOTHÈSE FALSIFIÉE :**
```
FAIT (conservé, choix utilisateur « garder + pivot V2 ») :
• Scheduler : cache O(1) nextDue_ + syncTo() (Scheduler.hpp).
• NeostMoira::sync() (Cpu68k.cpp) pilote le Scheduler depuis le hook cycle (do_cycles).
• Machine::runFrame : CPU tourne jusqu'à fin de trame, events dispatchés via sync() (plus de
  préemption/quantum). MOIRA_PRECISE_TIMING=true (sync AVANT chaque accès → lectures vidéo
  sous-instruction). Offset lecture compteur recalé -2 → +4 (Shifter.cpp, datation PT=true).
• 2 bugs introduits & corrigés : (a) ancre 1ʳᵉ trame sur sched.now() (pas busClockNow → décalage
  40 cyc) ; (b) sync() ne dispatche pas pendant cpu.reset() (g_inReset, sinon sched.now()→40).

MESURES (étalons run_etalons.py = 6/6 verts dans les DEUX) :
                       baseline (blocs)   nouveau (sync+PT=true)
  LX titre jitter      ~1.5 %             ~1.8 %   (INCHANGÉ)
  EL EN JEU            scramble ~95 %     scramble ~95 %   (INCHANGÉ)
  EL intro noir 600-1200  5/13            13/18   (RÉGRESSION : intro clignote plus)

PREUVE QUE CE N'EST PAS L'IRQ/HORLOGE : diff trace instruction-par-instruction EL boot
baseline vs nouveau = IDENTIQUE sauf boucles de polling matériel bénignes (handshake ACIA
$FC0E3C, FDC $FC1DDE) → la boucle d'horloge sync-driven n'est PAS le levier. EL EN JEU reste
scramble : NI le sync-driven NI le V2 partiel (NEOST_V2=1 → EL en jeu devient NOIR stable, pas
le jeu) ne le corrigent. ⇒ le résidu = CONVERGENCE beam-sync / micro-timing Moira↔WinUAE +
conflit offset global-vs-per-accès (cf. [[beamsync-busalign-falsified]] §7 : base $8203 jitter,
calibration fullscreen non-convergente), PAS la boucle d'horloge. La sensibilité de lecture
vidéo d'EL est CHAOTIQUE (sweep offset non-monotone) → pas d'offset robuste = symptôme de
convergence, pas de simple datation. V2 (res-switch) reste pertinent pour D'AUTRES cas (Cuddly
bordure basse, démos overscan hi-res), pas pour EL en jeu.

⚠ PERSISTANCE : MOIRA_PRECISE_TIMING=true est édité dans le sous-module extern/moira
(MoiraConfig.h) → NON capté par un commit superprojet (pointeur seul). À pérenniser via -D
CMake guardé #ifndef, ou commit submodule, avant tout `git submodule update`.

PROCHAINE ÉTAPE = la CONVERGENCE beam-sync (§7 : base $8203 jitter, calibration fullscreen non
convergente, résidu micro-timing Moira↔WinUAE). C'est le SEUL levier restant pour EL/LX en jeu ET
pour la PIXEL-exactitude de V2 (cf. limite de validation ci-dessous). Le sync-driven + V2 sont la
fondation propre ; V2 est désormais FAIT + validé oracle (bloc suivant).
```

**📊 V2 res-switch — VALIDÉ À L'ORACLE (2026-06-16) — opt-in NEOST_V2, CONSERVÉ opt-in :**
```
• FIDÉLITÉ SOURCE (audit vs Hatari) : line-shortening sur impulsion hi-res PRÉCOCE (≤56) + longueur
  de ligne variable = port fidèle de video.c:2246/2268/2288 (3 branches → HBL_Pos=220,
  nCyclesPerLine=224) + Video_AddInterruptHBL (2849). NeoST = setHblShorten (Machine.cpp:95, HBL
  reprogrammé à 220 + lineCarry_ += cpl-224) + replayGlue varlen (Shifter.cpp:885) + updateGlueState
  (3 branches DE/bordure 689-715). Reset par trame OK (227-228). Gaps mineurs restants : ajustement
  VBL si la DERNIÈRE ligne raccourcit (video.c:2862, rare) + garde RestartVideoCounter (N/A NeoST).
• NON-INTRUSIF : run_etalons (glue_selftest/overscan_top/spec512/scroll_8264-65/etos_ste_boot)
  PIXEL-EXACT avec NEOST_V2=1 — aucun étalon n'émet d'impulsion précoce, donc V2 ne s'y déclenche pas.
• ORACLE (MÊME binaire des 2 côtés, recette = la pièce manquante « valider V2 headless ») :
    python3 tools/make_overscan_lr.py /tmp/sw62.st 62 4     # PAD1=62 cale l'impulsion hi-res ~cyc 52 (≤56)
    bash tools/hatari_oracle.sh roms/etos192us.img /tmp/sw62.st 400 390 /tmp/hat.png st
    NEOST_V2=1 build/neost-headless roms/etos192us.img --disk /tmp/sw62.st --machine st --mem 512k \
        --frames 400 --screenshot /tmp/v2.ppm
    python3 tools/compare_screenshot.py /tmp/v2.ppm /tmp/hat.png --crop full   # crop full RÉPARÉ ce jour
  RÉSULTAT frame 390 : NeoST-V2 = overscan rayé STRUCTURELLEMENT IDENTIQUE à Hatari (région claire G
  + rayures hi-res D + bordures H/B noires + encoches bord G) ; NeoST-V2-OFF = NOIR total. Diff plein
  cadre vs Hatari : V2-OFF 44.9 % → V2-ON 27.3 % (V2 ≈ ÷1.7 l'écart). Résidu 27 % = jitter de phase
  (feedback impulsion↔HBL fait varier la position des rayures trame à trame), PAS un bug logique V2.
• ⚠ LIMITE DE VALIDATION (PROUVÉE empiriquement) : le line-shortening ≤56 N'EST PAS déclenchable de
  façon PIXEL-STABLE en headless. La latence d'exception HBL du 68000 (~56 cyc) place DÉJÀ l'impulsion
  à ≥cyc 92 depuis un handler HBL ; déclencher ≤56 exige un calage NOP au cycle près (wrap dans la
  fenêtre précoce de la ligne suivante) → sensible à la phase CPU↔faisceau = le résidu convergence (§7).
  ⇒ pas de pixel-exactitude V2 sans la convergence. La validation STRUCTURELLE (ci-dessus) est le
  niveau de rigueur atteignable headless aujourd'hui.
• RECOMMANDATION : GARDER opt-in. V2 est fidèle + non-intrusif + utile (noir→Hatari sur overscan réel),
  MAIS (a) il ne fait PAS le jeu EL (cause = convergence §7) et le change scramble→noir ; (b) en phase
  jittery, V2 peut se déclencher quand le HW ne le voudrait pas (faux positif ≤56) → risque de
  régression sur d'autres titres. Default-ON exige la convergence résolue (impulsions au BON cycle).
• Cuddly : V2 ne s'engage PAS sur les frames atteignables (sonde 1500 = 0 impulsion ≤56) — la bordure
  basse est dans le menu robot inatteignable headless. Oracle Cuddly SANS VALEUR tant que non navigable.
```

**VÉRIFICATION (les 3 cibles + garde-fous) :**
```
• Lethal Xcess  : roms/tos162us.img --disk disks/stx/Lethal_Xcess_Disk_1.STX --machine st --mem 1m
  --fastfdc → la calibration $14ef6 doit CONVERGER (atteint $30142, titre net, pas de saut en jeu).
• Enchanted Land: roms/tos102fr.img --disk "disks/st/Enchanted Land (1990)(Thalion).st" --machine st
  --fastfdc --frames 13000 --joy-at 3100 0x80 → niveau rocheux ; la BASE ($8203, NEOST_BASE_TRACE)
  doit cesser de jitter (joueur immobile) → screenshots 13000/13001/13002 quasi-identiques (≠ scramble).
• Cuddly       : disks/etalons/cuddly_demos.msa → taux d'ouverture bordure basse (NEOST_GLUE_STAT
  end=310 vs 263) → ~100 % (baseline 11-15 %). ⚠ menu robot inatteignable headless (--keys/--joy
  testés KO) ; valider au moins la stabilité trame-à-trame du titre + la convergence LX/EL.
• GARDE-FOUS À CHAQUE PAS : tools/run_etalons.py --max 0 (glue_selftest 19/19, spec512/overscan_top/
  scroll_8264/8265 = 0 px, etos_ste_boot ≤64) ; histogramme IRQ inchangé (EmuTOS/TOS 1.02, --irq) ;
  distribution NEOST_VBL_TRACE (dépassement service VBL) doit se rapprocher du pending_cyc d'Hatari
  (NeoST mean ~7.8 → cible ~4.3 ; range 0..32). Oracle : /opt/homebrew/bin/hatari 2.6.1
  --trace cpu_exception,video_vbl,video_addr,cpu_video_cycles ; tools/beamsync_diff.sh ; tools/hatari_oracle.sh.
```

**OUTILS & MÉTRIQUES de validation :**
```
• Traces NeoST (gated) : NEOST_VC_TRACE (lectures compteur, format = Hatari video_addr),
  NEOST_SYNC_TRACE (écritures freq/res), NEOST_VARLINE_TRACE, NEOST_SYNC_OFF (offset write).
• Oracle Hatari : binaire extern/hatari/build/src/hatari ; --trace video_addr/video_sync ;
  --cmd-fifo + `hatari-event keypress 57` (SPACE) pour atteindre les MENUS in-game
  (recette docs/HATARI_AUTOMATION.md ; le cmd-fifo désactive le fast-forward → temps réel).
• Métrique Cuddly : taux d'ouverture bordure basse (NEOST_GLUE_STAT, end=310 ouverte /
  end=263 fermée ; baseline 11%, FIX1+FIX2 15%, cible ~100%). Write $820A L262 cyc → 444.
• Non-régression : tools/run_etalons.py TOUS OK (5 pixel-exact : overscan_top, spec512,
  scroll_8264/8265, glue) + Lethal Xcess atteint $30142, Enchanted Land atteint son jeu.
```

**FICHIERS CLÉS :** NeoST `Machine.cpp` (runFrame/frameStart_/scheduleFrameEvents/lambdas),
`Cpu68k.cpp` (run/quantum/addBusWaitCycles/cyclesRunInQuantum/horloge Moira), `Scheduler.*`,
`Shifter.cpp` (videoCounter/recordSyncWrite/syncCpuBus). Hatari `video.c` (Video_GetCyclesSinceVbl_*,
VBL_ClockCounter, Video_ConvertPosition), `cycles.c` (Cycles_GetInternalCycleOn*Access),
`m68000.c` (M68000_WaitState/SyncCpuBus), `cpu/custom.c` (wait_cpu_cycle_read/write).
Workflow d'analyse : `subagents/workflows/wf_61686c94-b41`.

- **Contention DMA vidéo sur la RAM** *(précision cycle, reporté)* — modèle MAME
  ```
  (`stmmu.cpp::bus_contention`), **non porté depuis Hatari** (qui ne le modélise pas) ;
  divergerait de l'oracle pixel. À ne traiter que si besoin matériel réel hors Hatari.
  ```
- **Arkanoid** — page de titre « ARkanoid » atteinte (FDC rotationnel, cf. CHANGELOG),
  ```
  **mais plante / ne franchit jamais la partie** (même TOS 1.02fr — protection ? second
  chargement ? IRQ ?). Détail terrain → §Catalogue logiciels. À diff'er contre Hatari
  (`--keys`/`--joy`, trace IRQ). 🎯 étalon suite FDC/protection.
  ```

## Bus / memory map / MMU

- ~~Zone void `[fin RAM, $400000)` : lire le dernier mot du bus~~ → **FAIT**
(`Bus::cpuDb` latché par les overrides Moira, cf. `CHANGELOG.md`).

## Vidéo / Shifter

- **Bordures — raffinements** *(précision cycle, faible priorité)* :
  ```
  (1) wakeup-state WS3 (+1 cyc, sous-pixel) ; (2) med-res overscan ; (3) blank lines /
  NO_SYNC ; (4) pixel-perfect L/D end-to-end ; (5) **BEAM-SYNC : l'image SAUTE trame à
  trame** — bug COMMUN (rapport utilisateur 2026-06) à **Cuddly Demos** (scrolling robot),
  **Enchanted Land** (en jeu) et **Lethal Xcess** (en jeu, après le fix wait-state) : les
  lignes du beam ne sont pas synchronisées → décalage erratique trame après trame. Cœur =
  rendu cycle-exact de la géométrie PER-LIGNE sous écritures sync-raster (`$820a/$8260`/
  palette datées au cycle). Le wait-state `$FF8209` (cf. §FDC) corrige le FEEDBACK compteur
  aux jeux mais PAS le rendu lui-même. 🎯 reproduire les 3 ; (6) **scroller bordure BASSE**
  du menu Cuddly non rendu.
  🎯 étalons : `make_overscan_test.py` / `make_overscan_lr.py` (✅), **The Cuddly Demos**.
  **ROOT-CAUSE (6) TROUVÉE (2026-06-14, oracle cmd-fifo) — mésattribution de ligne :**
  Oracle du menu DÉSORMAIS POSSIBLE (le build local a `--cmd-fifo` → `hatari-event
  keypress 57`, cf. `docs/HATARI_AUTOMATION.md` ; l'ancienne note « impossible » est
  PÉRIMÉE). Diff : Hatari ouvre la bordure basse (`nEndHBL=310`, gros scroller « OUR
  DAY! ») ; NeoST la garde fermée (`end=263`, scroller écrasé) la PLUPART des trames
  (intermittent → l'image saute). Cause : le menu écrit à la ligne 262 `60Hz@cyc~440`
  PUIS un `50Hz`. Hatari date ce 50Hz à la **ligne 263 cyc 16** (ligne SUIVANTE) → la
  décision bordure-basse de la ligne 262 reste « retirée ». NeoST le date à la **ligne
  262 cyc 492** (≤502) → `updateGlueState` RE-FERME (l.819, comme Hatari `Video_EndHBL`
  2973, mais Hatari ne voit pas ce write sur la ligne 262).
  ⚠ MISE À JOUR (instrumentation `NEOST_VARLINE_TRACE`, 2026-06-14) : la **longueur de ligne
  variable N'EST PAS la cause** — le modèle 508/512 ne donne que **−4 cyc** de dérive sur le
  menu (0/62 writes réattribués). VRAIE cause : la **datation du write est ~28 cyc trop TÔT**
  (NeoST date `$FF820A` à fc 134560, cyc 416 ; **Hatari à 134588, cyc 444** — même ligne 262)
  ET **jittery trame à trame** → le 50Hz traverse la frontière de ligne par intermittence
  (`end=263` vs `310`) → bordure basse qui CLIGNOTE → image qui saute. C'est le **MÊME**
  déterminisme de phase CPU↔vidéo que le wait-state Lethal Xcess. `NEOST_SYNC_OFF=28` ouvre
  494/1331 frames SANS casser les étalons `--max 0`, mais le jitter demeure (pas un offset
  constant). (5) tearing du mur = même cause. → chantier **phase CPU↔glue cycle-exacte
  (datation des accès bus)**, PAS longueur de ligne. Outils : `NEOST_VARLINE_TRACE`,
  `NEOST_SYNC_TRACE`, `NEOST_SYNC_OFF` ; Hatari `--trace video_sync` + `--cmd-fifo`.
  Réf. : Hatari /tmp/cudh_3150.png, NeoST /tmp/cud_02900.png.
  ✅ FAIT (workflow oracle 8 agents + impl, 2026-06-14) — FIX1 + FIX2, conformes Hatari,
  ÉTALONS PIXEL-EXACT INCHANGÉS, LX/EL non régressés :
  • FIX1 = ancre de trame FIXE (VBL théorique) : `frameStart_` avance de `lpf_*cpl_` au lieu
    de `sched.now()` (retranche le carry δ, port de `VBL_ClockCounter = GlobalClock −
    PendingCyclesOver`, video.c:4964). Co-ancre events + datation (Machine.cpp:271).
  • FIX2 = `syncCpuBus()` sur `$FF820A` (Shifter.cpp:1394), manquait alors que `$FF8260` /
    palette l'avaient (port `wait_cpu_cycle_write`, align bus 4 cyc). Writes désormais
    alignés (418→420).
  ⚠ RÉSIDU NON RÉSOLU (le beam-sync SAUTE encore, Cuddly bordure basse 11%→15% seulement) :
  (a) FIX1 n'a PAS réduit la variance du write cyc → la cause DOMINANTE n'est ni l'ancre ni
  l'align bus, mais la **variance d'EXÉCUTION CPU trame-à-trame au write** (le menu anime →
  chemin de code + δ varient → write freq à 416…444 selon la trame ; Hatari stable car CPU
  et vidéo partagent UNE horloge au cycle près). (b) ÉCART SYSTÉMATIQUE +28 inexpliqué : pour
  amener le write à cyc 444 (Hatari) il faut offset +40 vs +2 prédit par le modèle « fin
  d'accès » — un fudge masquant ~+38 d'erreur, qui casserait EL (calibré +16). NON appliqué
  (FIX3 abandonné).
  ✅ ORIGINE DU +28 TRANCHÉE (2026-06-14, diff oracle Cuddly video_addr + cmd-fifo) : ce
  N'EST NI l'origine de trame (testé `NEOST_ORIGIN_OFF` : self-référentiel via le poll
  compteur, ruled out + dégrade), NI la géométrie/formule du compteur (à pc=6097c, le poll
  juste avant le write, le jeu lit la MÊME valeur `7dec0` dans NeoST ET Hatari). Le write
  bordure-basse est PILOTÉ PAR UN POLL `$FF8209` (lit à pc=6097c → écrit `$820A` à pc=609c4).
  La SEULE différence : le X (position faisceau) où le CPU échantillonne `7dec0` (figé en
  bordure droite) — NeoST ligne 260 X=488, Hatari X=376. → le +28 est **irréductiblement
  la PHASE CPU↔FAISCEAU au poll** (NeoST atteint le poll à une position décalée + jittery),
  MÊME cause que le jitter et la boucle LX `$14ef6`. PAS un offset/origine/géométrie. Le
  wait-state `$FF8209` (3811869) a réglé les boucles serrées `move.b/beq` ; les polls plus
  complexes (Cuddly 6097c, rendu animé) gardent un résidu de phase. → chantier de fond =
  précision-cycle de l'exécution CPU vs faisceau (timing instruction Moira + latence IRQ +
  wait-states bus), à rapprocher de Hatari (horloge globale unique). Cf. workflow
  wf_61686c94-b41.
  (7) **Lethal Xcess (STX) — écran noir** : la calibration fullscreen (`$14ef6` poll
  `$FF8209`, exige avance `0xbe`=190) deadlocke à cause du TIMING de la lecture compteur
  (pas la géométrie ; l'ancienne analyse 144-vs-190 / `syncWrites_` vide était une fausse
  piste invalidée par l'oracle). Fix candidat (`syncCpuBus` align) fait converger LX mais
  RÉGRESSE le sync-scroll d'Enchanted Land (même famille, ce point !) → opt-in
  `NEOST_VC_SYNC`. La VRAIE solution doit satisfaire LX **et** EL. Détail → §FDC « écran noir ».
  ```

## FDC WD1772 + DMA disquette

- ~~Lecteur HD MegaSTE~~ → **FAIT** : densité DD/HD/ED auto (géométrie), débit MFM
  ```
  ÷ facteur, porte `$FF860E` Mega STE, images 1,44 Mo (cf. `CHANGELOG.md`).
  ```
- ~~WRITE TRACK (format) sur `.ST`~~ → **FAIT** : extraction des secteurs si géométrie
  ```
  standard, sinon `LOST_DATA` tout-ou-rien (limite Hatari). Reformatage non
  standard = images flux (STX/SCP), hors périmètre `.ST`.
  ```
- ~~FIFO DMA/MMU vs MAME `stmmu.cpp`~~ → **TRANCHÉ** (recherche MAME master) : le modèle
  ```
  NeoST (= Hatari) est fidèle ; les écarts MAME (double FIFO 2×16 o, bit2 DRQ live,
  dernier bloc 8 mots) sont des choix que Hatari assume sans impact observable sur ST.
  Seule différence de fond : le chemin ACSI court-circuite le FIFO et `dmaSectorCount_`
  (transfert bloc piloté par le CDB) — à ne corriger QUE si un diagnostic qui
  désaligne sector-count DMA et longueur CDB échoue un jour.
  ```
- ~~STX HD/densité (`nextSectorIDStx`/`MFM_BIT` en cellules DD)~~ → **FAIT** : conversion
  bit/octet→cycles à la densité du média (DD inchangé). Cf. `CHANGELOG.md`.
- ~~Ré-interprétation en LECTURE d'une piste réécrite par WRITE TRACK~~ → **FAIT**
  (au-delà de Hatari) : `StxImage::reinterpretSaveTrack` parse le flux en secteurs lus à
  la place de l'original ; round-trip `.wd1772`. Cf. `CHANGELOG.md` + `tests/stx_writetrack_test.cpp`.
- ~~`Rick Dangerous.stx` « plante après titre »~~ → **rapport périmé, FONCTIONNE**
  (titre + jeu ; le test headless n'injectait pas d'entrée). Cf. `CHANGELOG.md`.
- **« écran noir » Lethal Xcess / Stardust / onslaught — DIAGNOSTIQUÉ : PAS un bug STX**,
  ```
  mais le MÊME chantier sync-raster que §Bordures (Enchanted Land / Cuddly). Le loader STX
  finit sa 1ʳᵉ salve (Lethal Xcess : pistes 0-35 face 0, TOUTES standard, dernier secteur
  lu+INTRQ OK) ; le jeu installe alors un afficheur fullscreen piloté par un état VBL
  (pointeur `$604`, phases `$139e8`/`$1499a`/`$149d4` qui incrémentent `$13a16`,
  vs `$149dc` qui ne l'incrémente PAS et joue un script de splits `$FF820A`/`$FF8260`
  synchronisé en pollant `$FF8209`). Boucle de calage `$14940` (attend que `$14ce8`,
  maj par le handler Timer B event-count `$14cc4`, reste STABLE 20 trames) → puis
  `$604=$149dc` + `bsr $13a18` (`clr $13a16` / `tst` / `beq`) : avec `$604=$149dc` rien
  n'incrémente `$13a16` → **deadlock**. Le calage diverge de la machine réelle car
  notre compteur vidéo `$FF8209` ne se comporte pas au cycle près pendant le poll
  (cf. `Video_RestartVideoCounter` NON porté + géométrie verrouillée par trame,
  `Shifter::videoCounter`). → à reprendre avec le chantier « géométrie par ligne /
  bascule 50-60 Hz + compteur vidéo cycle-exact » (§Bordures, §Précision cycle), PAS ici.
  ⚠ ORACLE DISPO (2026-06-14) : Hatari tourne en headless **sous Linux** aussi (binaire
  `extern/hatari/build/src/hatari`, cf. `docs/HATARI_AUTOMATION.md`). Référence visuelle
  obtenue (écran-titre OK sous Hatari STE/TOS 1.62 vs noir sous NeoST). Vérité-terrain
  cycle-exact du poll dispo via `--trace video_addr` (Hatari) et `NEOST_VC_TRACE=1` (NeoST,
  même format : `base/addr/line/X/start/cpl/liveStart/sync/pc` à chaque lecture
  $FF8205/07/09).
  ```
  **✅ RÉSOLU (2026-06-14, diff oracle) — wait-state +2 (valeur d'abord) sur la lecture `$FF8209` :**
  *(fix dans `Shifter::read8`, validé sur LX ET Enchanted Land + étalons `--max 0`. Reste
  le beam-sync en jeu, cf. §Bordures item (5).)*
  ```
  Diff Hatari↔NeoST de la calibration fullscreen (TOS 1.62us, Disk 1). Le code jeu :
    $14ef6: move.b $8209,d0 / beq $14ef6   ; ATTEND octet bas != 0
    $14f0e: move.b $8209,$14715            ; SAVE START (octet bas)
            ... script splits $820a/$8260 ...
    $14a26: move.b $8209,$14714            ; SAVE END
    $14a32: add.b #$be,d0 / cmp / bne      ; exige (END-START)&0xff == 0xbe=190
  Calibration : le jeu décale son script de 2 octets/trame et attend que l'avance mesurée
  monte LINÉAIREMENT jusqu'à 190 pile. HATARI : START toujours @(ligne63,X=284), rampe
  PROPRE 110,112,…,190 → converge → passe à la boucle de jeu $30142. NeoST : START JITTER
  @X=282/284 ; la sortie de boucle $14ef6 alterne (X=62,low=2) / (X=72,low=8) — un JITTER
  DE PHASE CPU↔faisceau d'~10 cyc au début de trame → deltas erratiques (jamais 190) →
  spin infini dans $14ef6 → JAMAIS $30142 → écran noir. La géométrie est CORRECTE
  (`cpl=512,start=56,liveStart=63` stables, = Hatari) : le repli `liveStartHBL=63` /
  `syncWrites_` vide N'EST PAS la cause (hypothèse précédente INVALIDÉE par l'oracle).
  CAUSE FINALE : la boucle sort à `E mod T` où T = durée d'itération `move.b $8209,d0/beq`.
  T était 2 cyc TROP COURT car la lecture `$FF8209` n'avait pas son WAIT-STATE. Mesuré à
  l'oracle (consécutifs `fc` au même PC) : LX `$14ef6` T=24 cyc chez Hatari vs 22 NeoST ;
  EL `$ee78` T=20 vs 18 → **+2 cyc bus FIXE** sur les DEUX. FIX (retenu) : échantillonner
  la VALEUR au cycle d'accès PUIS `addBusWaitCycles(2)` (ordre crucial : valeur intacte →
  étalons `--max 0` OK ; CPU retardé → T=24/20 = Hatari). ⚠ Un align-4 (`syncCpuBus`) au
  lieu d'un +2 FIXE jitterait et casserait EL — c'est pourquoi le 1ᵉʳ candidat (align)
  régressait EL. Résultat : LX converge ($14ef6 ~94k iters, delta 190, $30142, titre) ET
  EL atteint son jeu, AUCUNE régression étalon. Cf. CHANGELOG §Vidéo. Outil : `NEOST_VC_TRACE=1`.
  ```

## YM2149 PSG

- ~~Données port B Centronics + front strobe (bit5) en sortie~~ → **FAIT** (cf. `CHANGELOG.md`) :
  ```
  R15 (port B) + détection du FRONT DESCENDANT du strobe (R14 bit5) → `printerSink_` optionnel
  reçoit l'octet (handshake parallèle, port `psg.c:PSG_Set_DataRegister`).
  ```
- ~~Read-latch `regReadData_` / `$FF8800`~~ → **FAIT** (cf. `CHANGELOG.md`) : `$FF8800` relit le
  ```
  latch (valeur masquée au choix, NON masquée après écriture `$FF8802`) ; `$FF8801/03` → 0xFF ;
  `$FF8802` relisible (déviation diagnostic NeoST conservée).
  ```
- Filtre passe-bas STF alternatif (`LowPassFilter`) + table 16³ interpolée
  ```
  (`interpolate_volumetable`) en option _(faible valeur ; NeoST a déjà le LowPassFilter C10 et
  la table DAC à modèle de circuit — la table MESURÉE est une alternative inaudible)_.
  ```

## Son DMA STE + Microwire/LMC1992

- ~~Sortie stéréo + panoramique LMC1992 + DMA horodaté intra-trame~~ → **FAIT** (cf. `CHANGELOG.md`) :
  ```
  chaîne audio en 2 canaux entrelacés (image L/R préservée, gains gauche/droite), et transitions
  PLAY/STOP horodatées rejouées par segments (modèle push du YM) → one-shot court plus avalé,
  queue de sample plus écrêtée.
  ```
- FIFO 8 octets du DMA son remplie sur HBL (`DmaSnd_FIFO_Refill/PullByte`,
  ```
  dmaSnd.c:343-410) _(refinement résiduel SOUS-PERCEPTUEL : timing ±8 octets des débuts/fins de
  trame ; la timeline horodatée ci-dessus capte déjà les modifs de données intra-trame à la
  granularité trame)_.
  ```

## CPU : IRQ, Moira, MegaSTE

- ~~MC68881 — arithmétique flottante~~ → **FAIT** (cf. `CHANGELOG.md`,
`src/io/Fpu.{hpp,cpp}`) : FP0-7 étendu 80 bits, formats B/W/L/S/D/X/P, dialogue
Command/Response/Operand/Condition/Save/Restore complet, FMOVECR bit-exact ;
validé mini-ROM SFP004 (`tools/make_fpu_testrom.py`, **9/9**) + diag MegaSTE
« FPU idle ». **Mantisse 64 bits réelle (softfloat 80 bits, `SoftFloatX80.hpp`)** pour
toute l'arithmétique algébrique + **livraison d'exception FP** via le Response CIR — FAIT.
*Reste hors périmètre : les transcendantes en double hôte (le 68881 les approxime lui-même,
non bit-exact, comme MAME/Previous).*

## Stockage & contrôleurs

- **GEMDOS HD** : monter un dossier hôte comme lecteur C: (`--gemdos DIR` /
`NEOST_GEMDOS_DIR`) — port complet de `gemdos.c` (cf. CHANGELOG).
- **ACSI complet** (jusqu'à 8 cibles, boot disque dur TOS depuis une image, R/W,
détection de partitions) — port de `hdc.c` (`io/Acsi`, `--acsi`/`--hd`, cf. CHANGELOG).
- **SCC Z85C30 MegaSTE** : canaux A/B, registres WR/RR, IRQ niv5 vectorisée (SCU),
  reset, TX→RX bouclage — port fonctionnel de `scc.c` (`io/Scc`, cf. CHANGELOG).
  _Reste (faible valeur) : timers du BRG (Zero Count), baudrate temporisé, série hôte._
- **SCSI / NCR5380** (MegaSTE/TT) *(gros contrôleur)* — réf. `ncr5380.c`
- ~~**Imprimante/Centronics** : port B YM, strobe PSG port A bit5, busy MFP I0~~ → **FAIT**
  (port de `printer.c`/`psg.c:388-390`) : capture des octets imprimés dans un fichier
  (`Machine::setPrinterFile`, headless `--printer FILE`) + BUSY GPIP0 sur strobe. Cf. CHANGELOG.

## Périphériques & profils machine

- **ROM TOS MegaSTE** : TOS 2.05/2.06 256 Ko à `$E00000`, choix pays, checksums, fallback
  ```
  EmuTOS MegaSTE.
  ```
- **NVRAM / préférences TOS MegaSTE** (résolution/boot device) si TOS 2.x l'exige.
- **Cartridge port** `$FA0000-$FBFFFF` générique — réf. `cart.c`

## Outillage / qualité

- **Étalons headless** — infra en place (cf. CHANGELOG) ; reste : calibrer frames +
  ```
  références Cuddly/Union/Troed/Hatari Test Suite ; rapatrier Union (planetemu manuel).
  ```
- **Comparaison MAME ↔ NeoST** (memory map, bus errors, FDC/MMU FIFO, blitter, SCC).
- Capturer la **trace Hatari de référence** pour `trace_diff` (Arkanoid & co).
- **Matrice de compatibilité MegaSTE** : TOS 2.05/06, EmuTOS, 1/2/4 Mo, 8/16 MHz, cache
  ```
  on/off, DD/HD, mono/couleur.
  ```
- Tests de non-régression (screenshots de référence EmuTOS/TOS 1.02).

