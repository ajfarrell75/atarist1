# Divergences NeoST ↔ Hatari — cartographie pour corrections futures

**But.** Inventaire des écarts entre NeoST et la **source de vérité Hatari**, sous-système par
sous-système, pour prioriser les corrections futures. Chaque écart est ancré `fichier:ligne`
des deux côtés, classé par sévérité (**haute** = casse logiciels/boot · **moyenne** = fidélité
visible · **basse** = cas-limite/cosmétique) avec son impact connu.

**Méthode.** Comparaison ligne à ligne du code NeoST (`src/`) au source C de Hatari
(`extern/hatari/src`, cloné au commit `c9906f1`, gitignoré, **non compilé** — lu comme
référence, cf. CLAUDE.md). Produit par un workflow de 7 sous-agents, un par sous-système.
Cible NeoST : ST / Mega ST / STE / Mega STE, 68000 (Moira cycle-exact). Hors périmètre
volontaire (NON comptés comme divergences) : moteur FDC `_MFM`/DPLL (IPF/SCP/KFS), TT/Falcon,
VME, FPU « not found », USART relayée vers un tty hôte, son en mode headless.

**Bilan global.** La fidélité est **élevée à très élevée** sur tous les sous-systèmes. Aucune
divergence ne casse un boot EmuTOS (ST/STE/MegaSTE) ni un boot disquette `.ST` normal. Les
écarts restants sont surtout des cas-limites matériels, des tricks de démos « hardcore », et
quelques branches spécifiques STE/STF non câblées.

**Statut des corrections (passe du 2026-06-15).** Les écarts **bornés et vérifiables** ont été
corrigés (✅ ci-dessous) : B1 (Blitter 65536), BL2 (rejet accès octet aux registres mot),
S1 (filtre LPF STF câblé), BU1 (miroir PSG), MIDI (master reset sans purge + RDR persistant).
Validés : `glue-selftest` 19/0, boots ST/STE/MegaSTE **pixel-identiques** à avant. Les chantiers
**cycle-exacts** (V1-V3 vidéo, S2 FIFO son, D3 stall FIFO) restent **différés** : leur
validation exige l'oracle Hatari headless, **absent de ce conteneur** (cf. CLAUDE.md). Les
points « choix de comportement » (SC1 loopback SCC, D1/D2 WRITE TRACK STX, NeoST plus correct
en HD/ED) et marginaux (`$FF8264`, D4 6250, F1, M1) sont laissés documentés pour décision/suivi.

---

## Bilan de fidélité par sous-système

| Sous-système | NeoST | Hatari (vérité) | Fidélité | Écarts H / M / B |
|---|---|---|---|---|
| MFP 68901 | `Mfp.cpp` | `mfp.c` | très élevée | 0 / 1 / 3 |
| Vidéo (Shifter/Glue) | `Shifter.cpp` | `video.c` | élevée (cœur STF) | 0 / 3 / 5 |
| FDC + DMA + STX | `Fdc.cpp`, `StxImage.cpp` | `fdc.c`, `floppies/stx.c` | très élevée | 0 / 3 / 9 |
| Son (YM2149 + DMA STE) | `YM2149.cpp`, `DmaSound.cpp` | `psg.c`, `dmaSnd.c`, `sound.c` | très élevée | 0 / 2 / 3 |
| ACIA 6850 / IKBD / MIDI | `Ikbd.cpp`, `MidiAcia.cpp` | `acia.c`, `ikbd.c` | très élevée | 0 / 0 / 9 |
| Bus / mémoire / bus-error | `Bus.cpp` | `ioMem*.c`, `memory.c`, `stMemory.c` | très élevée | 0 / 1 / 5 |
| Blitter | `Blitter.cpp` | `blitter.c` | élevée (données) | **1** / 1 / 5 |
| SCC Z85C30 (MegaSTE) | `Scc.cpp` | `scc.c` | élevée (cœur registre) | 0 / 1 / 2 |

---

## Priorités — divergences HAUTE et MOYENNE

| # | Sous-système | Divergence | Sévérité | NeoST | Hatari |
|---|---|---|---|---|---|
| B1 ✅ | Blitter | Compteur X/Y écrit à `0` non interprété comme **65536** (blit avorté au lieu de maximal) | **HAUTE** | `Blitter.cpp:99-101` | `Blitter_WordsPerLine/LinesPerBitblock_WriteWord` `blitter.c:1343-1366` |
| V1 | Vidéo | **Branche STE de la Glue absente** (timings preload STE, `LEFT_OFF_2_STE`) | moyenne | `Shifter.cpp:670-839` | `Video_Update_Glue_State` (branche STE) `video.c:2442-2652` |
| V2 | Vidéo | **Tricks par changement de résolution** non répliqués (overscan med-res, scroll hardware hi/med/lo) | moyenne | — (absent) | `Video_WriteToGlueRes` `video.c:1618-1820` |
| V3 | Vidéo | Pas de **changement de géométrie mid-trame** (50↔60 Hz, RestartVideoCounter) | moyenne | `Shifter.cpp:160-163` (figé) | `video.c:2857-2876`, `Video_RestartVideoCounter` `video.c:4608` |
| S1 ✅ | Son | **Filtre passe-bas STF (C10) jamais activé** → STF/Mega ST en PWM (code mort) | moyenne | `setStfLowPass` jamais appelé, `YM2149.hpp:165` | `Sound_Update_Filters` `sound.c:1946-1951` |
| S2 | Son | **DMA STE sans FIFO 8 octets ni avance HBL** (réalignement mono→stéréo non modélisé) | moyenne | `DmaSound.cpp:288-301` | `DmaSnd_FIFO_*` / `DmaSnd_STE_HBL_Update` `dmaSnd.c:342-438,727-741` |
| D1 | FDC/DMA | **WRITE TRACK STX réinterprété** en secteurs (CRC « nettoyé », statut neutralisé) | moyenne | `StxImage.cpp:254-300`, `Fdc.cpp:986-1008` | `FDC_WriteTrack_STX` (TODO, pas de relecture) `stx.c:2027-2134` |
| D2 | FDC/DMA | **READ TRACK STX** renvoie la piste réécrite (conséquence de D1) | moyenne | `Fdc.cpp:1050,1062` | `FDC_ReadTrack_STX` `stx.c:1863` |
| D3 | FDC/DMA | Flush FIFO↔RAM **ne stalle pas le CPU** (wait-state 32 cyc manquant) — cycle-exactness | moyenne *(à confirmer)* | `Fdc.cpp:667-702` (`fifoPush`/`fifoPull`) | `FDC_DMA_FIFO_Push/Pull` `fdc.c:1340,1396` |
| M1 | MFP | Lignes GPIP **on-chip** (ACIA/FDC/blitter) lèvent l'IRQ **sans la machine de fronts AER/DDR** | moyenne | `Mfp.hpp:115-127` + appels `Ikbd/Fdc/Blitter` | `MFP_GPIP_Set_Line_Input` `mfp.c:1180,1142` |
| BU1 ✅ | Bus | **Miroir matériel du PSG `$FF8804-$FF88FF`** non routé vers le YM2149 (lit `0xFF`, écritures ignorées) | moyenne | `Bus.cpp:531,623` | `IoMem_Init` shadow PSG `ioMem.c:386-393` |
| BL2 ✅ | Blitter | Accès **OCTET** aux registres mot/long non rejetés | moyenne | `Blitter.cpp:50-61` | `Blitter_CheckAccess_Byte` `blitter.c:972-989` |
| SC1 | SCC | **TX émis immédiatement** (pas de cadence baud / Zero Count) ; `WR14` bit4 loopback peut-être actif au reset | moyenne *(à vérifier)* | `Scc.cpp:234,269-274`, reset `Scc.cpp:61` | `SCC_WriteDataReg` / `SCC_Process_TX` `scc.c:1655-1681,1986` |
| D4 | FDC | Piste « standard » de repli **6268 o (NeoST) vs 6250 (Hatari)** | faible-moyenne | `Fdc.cpp:156` | `FDC_TRACK_BYTES_STANDARD` `stx.c:1418` |

---

## Détail par sous-système

### MFP 68901 — `Mfp.cpp` ↔ `mfp.c`
Chaîne IRQ (IER/IPR/IMR/ISR/VR, élection priorité + chronologie, délai 4 cyc, IACK, EOI
auto/software, antidatage), timers datés, compteur vivant, event-count Timer A/B : **conformes**.

- **[M1 — moyenne]** Lignes GPIP on-chip (ACIA/FDC/blitter) : NeoST fait `raise(SRC_xxx)`
  directement (`Mfp.hpp:115-127`) au lieu de passer par `MFP_GPIP_Set_Line_Input` +
  détection de front AER/DDR (`mfp.c:1142,1180`). Conséquences : ne respecte pas l'AER si un
  soft le reprogramme pour GPIP3/4/5, ne lève rien à la retombée, n'inhibe pas via DDR=sortie.
  *Impact : faible (EmuTOS laisse AER=0, front actif-bas standard, correctement émulé).*
- **[basse]** Conversion cycles MFP↔CPU par troncature (`Mfp.cpp:255`) vs unités internes
  haute résolution + reste accumulé chez Hatari → jitter ±1 cyc (atténué par ré-ancrage sur
  l'échéance servie). *Hatari ajoute même un jitter aléatoire volontaire pour Lethal Xcess,
  non reproduit.*
- **[basse]** Event-count Timer B : `hblank()` (`Mfp.cpp:402-410`) retourne tôt si
  `tbCounter_==0` → TBDR=0 (256 lignes) gelé au lieu de wrap 0→255. *Timer A conforme (`==1`).*
- **[basse]** Retombée d'IRQ immédiate (`Mfp.cpp:529`) vs TODO Hatari d'un délai mesuré (`mfp.c:789`) — identique aujourd'hui, suivi seulement.
- *Faux positif écarté :* les wait-states 4 cyc d'accès MFP **sont** appliqués, mais par le
  `Bus` (`Bus.cpp:540`, `addMfpWaitCycles()`), pas par `Mfp.cpp` → **pas une divergence**.

### Vidéo (Shifter + Glue) — `Shifter.cpp` ↔ `video.c`
Cœur Glue **STF** = transcription quasi ligne-à-ligne (`updateGlueState` ≙ `Video_Update_Glue_State`
branche STF), constantes = `VIDEO_TIMING_STF_WS1`, compteur vidéo, bordures H/B/G/D 50/60 Hz,
spec512, scroll fin STE de base, masquage palette par machine : **conformes**.

- **[V1 — moyenne]** Branche **STE** de la Glue absente (`Shifter.cpp:670-839` = STF seul) :
  `Preload_Start_*`, `Line_Set_Pal=56`, `LEFT_OFF_2_STE` (+20 o, shift −8) non gérés. *Impact :
  overscan de démos STE (E605, DHS) mal placé.*
- **[V2 — moyenne]** Tricks de bordure par **changement de résolution** non répliqués : pas
  d'équivalent de `Video_WriteToGlueRes` (`video.c:1618-1820`) — overscan med-res, stab
  hi/med/lo, scroll « hardware » 1/5/9/13 px. NeoST verrouille une résolution par trame
  (`frameMode_`). *Impact : No Cooper, Delta Force/PYM, Closure, HighResMode.*
- **[V3 — moyenne]** Géométrie de trame figée à `beginFrame` (`Shifter.cpp:160-163`) :
  pas de 50↔60 Hz mid-trame (CyclesPerVBL ±4), `RestartVideoCounter` non porté
  (`Shifter.cpp:1328-1337`). *Impact : overscan plein écran à trame allongée, synchros fines.*
- **[basse]** spec512 : pas de stagger +4 cyc des écritures `move.l`/`movem` au même cycle ni
  wrap de fin de ligne (`Spec512_StoreCyclePalette` `spec512.c:185-199`). *0 px de diff
  revendiqué sur le slideshow Spectrum 512.*
- **[basse]** STE « left+16 px » (écran 336, `bSteBorderFlag`) absent (`Video_HorScroll_Write`
  `video.c:5860-5916`). *Impact : Obsession, Pacemaker.*
- **[basse]** Signal **VBlank** (lignes basses masquées) non modélisé (`video.c:3443-3487,3987`).
- **[basse]** Datation des écritures couleur spec512 (offset calibré) ≠ datation freq/res (+16) —
  fonctionne par calibration séparée, fragile.
- **[basse]** Bordures uniquement en basse résolution (overscan med-res non géré, `Shifter.hpp:334`).

### FDC WD1772 + DMA + STX — `Fdc.cpp`, `StxImage.cpp` ↔ `fdc.c`, `floppies/stx.c`
Machines à états type I-IV, bits de statut par type, timings (spin-up, head-load, index),
`NextSectorID`, FIFO 16 o, densité `$FF860E`, fuzzy/timing STX, persistance `.wd1772`
byte-compatible, `.MSA`/`.DIM` : **conformes** (vérifiés ligne à ligne).

- **[D1 — moyenne]** WRITE TRACK STX : NeoST parse le flux MFM écrit (`$FE`→ID, `$FB/$F8`→data)
  et le rend visible aux lectures, **régénère un CRC valide + `fdcStatus=0`** (`StxImage.cpp:254-300`).
  Hatari laisse un TODO (`pDataRead=NULL`, `stx.c:2027`) → lecture inchangée. *Impact :
  protections « formate + relit une piste corrompue » (Copylock/Rob Northen) « nettoyées » par
  NeoST ; mais utilisabilité accrue pour les formateurs ordinaires.*
- **[D2 — moyenne]** READ TRACK STX renvoie la piste réécrite (conséquence de D1, `Fdc.cpp:1050`).
- **[D3 — moyenne, à confirmer]** `fifoPush`/`fifoPull` ne stallent pas le CPU (32 cyc manquants,
  `Fdc.cpp:667-702`) vs `M68000_AddCycles_CE(32)` (`fdc.c:1340,1396`) → contention DMA/CPU non
  modélisée (cycle-exactness ; fonctionnel OK).
- **[D4 — faible-moyenne]** Piste standard de repli **6268** (`Fdc.cpp:156`) vs **6250**
  (`FDC_TRACK_BYTES_STANDARD`) → longueur d'un READ TRACK sur piste absente (~0,3 %).
- **[basse]** Reset DMA force bit0 « no error » (`Fdc.cpp:708`) ; Hatari ne touche `Status`
  qu'au cold reset (`fdc.c:1233`).
- **[basse]** Ripple-carry adder d'adresse DMA `$FF860B/0D` (ST only) non émulé (`Fdc.cpp:1974`
  vs `fdc.c:5042`).
- **[basse]** Densité **HD/ED** STX : NeoST recadre `BitPosition` sur la densité (`Fdc.cpp:862-897`)
  → **plus cohérent que Hatari** (qui a une incohérence HD/ED) ; **identique en DD**.
- **[basse]** Bornes parseur STX (`StxImage.cpp:90-200`) : rejette une image tronquée que Hatari
  monterait ; **OOB latent** sur chemin « simple » tronqué (`Fdc.cpp:907`) — à corriger.
- **[basse]** `pData==NULL` sans RNF → secteur lu 512×`0x00` + statut OK (image de bord, `StxImage.cpp:161`).
- **[basse]** INTRQ : `raise(SRC_FDC)` **en plus** de la ligne GPIP5 (`Fdc.cpp:638-645`) — court-circuite
  l'edge GPIP du MFP (cf. M1) ; à vérifier qu'il n'y a pas double déclenchement.
- **[basse]** Signal DC (Disk Change)→GPIP4 (TT-only chez Hatari) non émulé ; modèle WPRT distinct.
- **[basse]** Hot-swap : 1 phase (eject) vs 2 (eject+insert) chez Hatari (`floppy.c:438-514`).

### Son (YM2149 + DMA STE) — `YM2149.cpp`, `DmaSound.cpp` ↔ `psg.c`, `dmaSnd.c`, `sound.c`
Moteur 250 kHz, table DAC 32³, ET logique ton/bruit, LFSR, enveloppe, demi-amplitude STE,
PWM+HPF, FIR anti-repliement DMA, Microwire/LMC (gains/panoramique), XSINT→GPIP7+Timer A,
cold/warm reset : **conformes** (l'ancien doc `SOUND_HATARI_DIFF.md`, périmé, a été **supprimé**).

- **[S1 — moyenne] ✅ corrigé** — `setStfLowPass(!STE)` est désormais appelé (`Machine.cpp`/`Machine.hpp`)
  → ST/Mega ST utilisent `applyLpfStf250`. *Avant :* filtre câblé mais `setStfLowPass()` défini mais
  **jamais appelé** (`YM2149.hpp:165`) → STF/Mega ST en PWM au lieu de `LPF_STF`
  (`sound.c:1946-1951`). Code `applyLpfStf250` mort. *Impact : timbre YM plus dur sur ST.*
- **[S2 — moyenne]** DMA STE sans FIFO 8 octets ni `DmaSnd_STE_HBL_Update` (`dmaSnd.c:342-438,727`) :
  lecture RAM directe à la cadence DMA, pas de réalignement mono→stéréo. *Impact : Mental
  Hangover, Power Up Plus (faible via le modèle push horodaté du chemin GUI).*
- **[basse]** Fréquences de coupure du correcteur LMC1992 : RBJ 2e ordre 200/8000 Hz
  (`DmaSound.cpp:218`) vs Savinkoff 1er ordre 118.28/8438.76 Hz (`dmaSnd.c:1418`). Gains ±dB corrects.
- **[basse]** Chemin DMA mono `mix()` : YM nu dès PLAY=0, pas de drainage de queue FIFO
  (`DmaSound.cpp:435` vs `dmaSnd.c:548`) — WASM/mono uniquement.
- **[basse, méthodo]** Son absent en headless → pas d'oracle audio. *Reco : option `--sound-trace`
  (registres horodatés + dump PCM).*

### ACIA 6850 / IKBD HD6301 / MIDI — `Ikbd.cpp`, `MidiAcia.cpp` ↔ `acia.c`, `ikbd.c`
**Très complet** : modèle TIE, master reset 6850, overrun/cadence série, fenêtre critique de
reset, buffer borné 1024, pause `$13`, toutes les commandes `$07-$9A`, les 6 handlers 6301
custom (Froggies, Transbeauce, Dragonnels, Chaos, Audio Sculpture), quirks reset, duplication
feu/boutons : **conformes** (l'ancien doc `IKBD_HATARI_DIFF.md`, périmé, a été **supprimé**).

- **[basse]** Délai variable de « réflexion » des réponses (`$16`/`$1C`/`$0D`/`$21`/`$87-$9A`) non
  modélisé — seul l'espacement série inter-octets (~10240 cyc) est présent (`IKBD_Cmd_Return_Byte_Delay`).
- **[basse]** Froggies : délai 7000 cyc sur l'octet `$83` non répliqué (espacement série joue le rôle).
- **[basse]** Cadence autosend liée au VBL (pas au timer 150000 cyc) ; **taux monitoring `$17` ignoré**.
- **[basse]** `IKBD_CheckForDoubleClicks` et joystick→barre d'espace absents (confort fast-forward, non matériel).
- **[basse] ✅ corrigé** — MIDI : master reset ne **purge plus** la file RX (`MidiAcia.cpp`), aligné sur l'ACIA clavier et la note Hatari (« don't clear bytes in transit »).
- **[basse] ✅ partiel** — MIDI : lecture RDR à vide rend désormais le **dernier octet** (`rdr_` persistant, comme le 6850). Restent non modélisés (sans impact ST) : bit OVRN, SR sans DCD/CTS/FE/PE (modèle réduit).

### Bus / mémoire / bus-error — `Bus.cpp` ↔ `ioMem*.c`, `memory.c`, `stMemory.c`
MMU/banques ST vs STE (`ConfToBank` : `bank1=bank0` sur STE), aliasing RAS/CAS, whitelist
bus-error par modèle (octet par octet, patches MegaST/MegaSTE, SCU octets impairs), fenêtres
ROM, accès DMA (retour 0/écriture ignorée en zone fautive), protection superviseur `<0x800`,
règle word/long : **conformes**.

- **[BU1 — moyenne] ✅ corrigé** — Miroir matériel du PSG `$FF8804-$FF88FF` désormais routé vers
  le YM2149 (`Bus.cpp` : plage étendue à `+0x100`, décodage `addr&3`, wait-states inclus), comme
  le shadow Hatari `ioMem.c:386-393`.
- **[basse]** `$FF8264` STE (scroll fin) lu comme void `0xFF` au lieu du registre réel
  (`Video_HorScroll_Read_8264` `ioMemTabSTE.c:122`).
- **[basse]** Distinction `IoMem_VoidRead`(0xFF)/`_00`(0x00) gérée seulement pour les registres
  vidéo STE (dans le Shifter), pas générale.
- **[basse]** Zone cartouche vide `$FA0000-$FBFFFF` lit `0xFF` au lieu du tampon ROM (sans impact : le magic ne matche pas).
- **[basse]** Pas de write-protect `$0-$7` pour les accès **non-CPU** (blitter via `write16`) →
  un blit ciblant la table des vecteurs corromprait la RAM (Hatari l'ignore, `memory.c:758`).
- **[basse]** `read32` en zone RAM void assemblé octet par octet vs `(db<<16)|db` — équivalent en accès aligné.
- *Note :* la zone RAM vide renvoie `0x00` (NeoST) ; Hatari renvoie la dernière valeur du bus
  (`regs.db`, `VoidMem`). Différence théorique, sans impact connu.

### Blitter — `Blitter.cpp` ↔ `blitter.c`
Logique de données **fidèle** (HOP/LOP 16 cas + tables `need_src`/`need_dst`, FXSR/NFSR, smudge,
halftone, masques de bord, bug « 63 accès », IRQ GPIP3 fin de blit, arbitration MegaSTE/STE).

- **[B1 — HAUTE] ✅ corrigé** — Compteur X **ou** Y écrit à `0` désormais interprété comme **65536**
  (`Blitter.cpp` : early-return dégénéré supprimé ; X via bouclage 16 bits, Y via `int 0→65536`).
  *Description initiale :* compteur à `0` non interprété comme **65536** :
  `start()` traite `xc==0 || yc==0` comme blit dégénéré « rien à faire » et efface BUSY/HOG
  (`Blitter.cpp:99-101`). Hatari : `x_count==0 → 65536`, `y_count==0 → 65536`
  (`blitter.c:1343-1366`). *Impact : un blit chargeant 0 pour 65536 (légal) est avorté ; bug latent.*
- **[BL2 — moyenne] ✅ corrigé** — `write8` ignore désormais un accès octet aux registres MOT
  (`off < 0x3A`, `Blitter.cpp`) ; seuls HOP/LOP/contrôle/skew restent accessibles en octet.
  *Avant :* accès **octet** aux registres mot/long non rejetés (`Blitter.cpp:50-61`)
  vs `Blitter_CheckAccess_Byte` (`blitter.c:972`) qui les ignore.
- **[basse]** Pas de masquage `&0xFFFE` des incréments à l'écriture (`blitter.c:1229`).
- **[basse]** Découpe non-hog seulement à la frontière de mot (dépasse le budget de ≤3 accès) vs
  suspension mid-word d'Hatari (`BLITTER_CONTINUE_LATER_IF_MAX_BUS_REACHED`).
- **[basse]** Pas d'exécution CPU parallèle pendant un blit (`Blitter_Check_Simultaneous_CPU` `blitter.c:1641`).
- **[basse]** HOP/LOP et bit4 du contrôle masqués seulement à l'exécution, pas à la relecture du registre.

### SCC Z85C30 (MegaSTE) — `Scc.cpp` ↔ `scc.c`
Cœur registre/IRQ **fidèle** (RR/WR, `updateRR0/RR2/RR3`, `updateIRQ` IUS/prio, IACK VIS/NV,
reset HW/canal, IRQ niveau 5 gatée par le SCU).

- **[SC1 — moyenne]** TX émis immédiatement à l'écriture data (`Scc.cpp:269-274`) sans timer baud
  ni interruption Zero Count (BRG) vs `SCC_Process_TX` cadencé (`scc.c:1986,2231`). De plus
  **`WR14` bit4 (loopback local) `|= 0x30` au reset** (`Scc.cpp:61`) → TX réel potentiellement
  jamais sorti vers `sink_` tant que bit4=1. **À vérifier** (datasheet vs Hatari).
- **[basse]** Horloges/baud (PCLK, BCLK, time constant `WR12/13`) entièrement absentes
  (`SCC_Compute_BaudRate` `scc.c:1027`) — conséquence de SC1, volontaire (cible NeoST).
- *Note :* le bouclage local `WR14` bit4 n'existe pas chez Hatari mais correspond à la datasheet
  Zilog → NeoST potentiellement **plus** fidèle au chip, mais divergent d'Hatari.

---

## Documents internes périmés — ✅ SUPPRIMÉS

Deux docs décrivaient un état du code **antérieur** et listaient comme « absents » des
comportements aujourd'hui implémentés (faux positifs). Ils ont été **supprimés** et remplacés
par le présent document :

- ~~`docs/IKBD_HATARI_DIFF.md`~~ : TIE, master reset, `$08`/`$1A`/`$19`/`$20`/`$21`/`$87-$9A`,
  pause `$13`, overrun, cadence série, buffer borné, quirks reset — tous implémentés.
- ~~`docs/SOUND_HATARI_DIFF.md`~~ : table DAC, ET ton/bruit, filtres, FIFO/anti-repliement,
  start==end, cold/warm reset, reset port A, `Mfp::reset()` — tous implémentés.

## Cas où NeoST améliore Hatari (choix à préserver / documenter)

- **STX densité HD/ED** (D, F3) : NeoST recadre `BitPosition` sur la densité → cohérent là où
  Hatari a une incohérence interne (sans effet en DD).
- **WRITE TRACK STX** (D1) : NeoST rend la piste réécrite lisible (Hatari = TODO) → utile aux
  formateurs ordinaires ; à arbitrer pour les protections format-and-verify.
- **WRITE TRACK `.ST`** : NeoST écrit les secteurs d'une géométrie standard (Hatari renvoie
  LOST_DATA, TODO).
- **SCC `WR14` bit4** : bouclage local conforme à la datasheet Zilog (absent d'Hatari).

---

## 🔁 2ᵉ passe d'audit approfondi (2026-06-15, workflow 7 agents)

Objectif : creuser **plus profond** que la 1ʳᵉ passe (lecture ligne-à-ligne des fonctions clés)
et **vérifier les 4 correctifs mergés**. Verdict global : fidélité **confirmée**, **aucune nouvelle
divergence HAUTE**, les 4 correctifs sont CORRECTS. La passe remonte surtout des cas-limites
**basses**, quelques **moyennes** nouvelles, et **2 bugs nets actionnables**.

### Vérification des correctifs mergés
- ✅ **B1** (Blitter 65536) — CORRECT : X (bouclage 16 bits) et Y (`int 0→65536`) = 65536 exact, readback 16 bits fidèle, pas de débordement.
- ✅ **BL2** (rejet accès octet) — CORRECT en **écriture** (liste `$3A-$3D` exacte vs `Blitter_CheckAccess_Byte`) ; **incomplet en lecture** → voir BL-R.
- ✅ **S1** (filtre LPF STF) — CORRECT (`applyLpfStf250` = `LowPassFilter`, activation `!STE` = `Config_IsMachineST`). Réserve : condition `nAudioFrequency≥40000` d'Hatari ignorée (sans impact à 48 kHz).
- ✅ **BU1** (miroir PSG) — CORRECT : décodage `addr&3`, plage close à `$FF88FF` (pas de chevauchement `$FF8900`), wait-states inclus.
- ✅ **MIDI** (no-purge + RDR persistant) — CORRECT, mais effet de bord **M-MIDI** (ci-dessous).

### 🔴 Nouveaux — BUGS NETS actionnables
- **BUS-LEAK** *(basse-moyenne, bug réel)* — `Bus::write16`/`write32` branche blitter font `return`
  **sans restaurer `ioAccessWidth_ = saved`** (`Bus.cpp:504-521`). Après le 1ᵉʳ blit mot (donc dès
  le 1ᵉʳ blit VDI d'EmuTOS sur Mega ST/STE/MegaSTE), `ioAccessWidth_` reste ≥2 en permanence →
  les bus-errors d'accès **octet** de `$FF9200` (joypad), du lightpen `$FF9220-23` et du FDC
  `$FF8604-07` (`==1` requis) sont **désarmées définitivement**. *Fix : restaurer `ioAccessWidth_`
  avant le `return`, ou poser la largeur dans read8/write8.*
- **BL-GPIP3** *(moyenne, bug réel)* — la ligne GPU_DONE (GPIP3) du blitter est posée à
  `finishTransfer` (`Blitter.cpp:194`) mais **jamais ré-armée haute** ; Hatari la met haute au
  démarrage (`blitter.c:895`) et basse à l'achèvement (`:916`). → après le 1ᵉʳ blit, GPIP3 lit
  « fini » en permanence ; les blits suivants ne génèrent **plus de front** → un programme qui
  *scrute* GPIP3 / son IRQ pour la fin de blit voit un faux positif dès le 2ᵉ blit. *Fix : ligne
  haute au (re)démarrage dans `start()`.* (Lié à M1.)

### 🟠 Nouveaux — MOYENNES (fidélité)
- **MFP** — pas de dispatch des timers échus (`MFP_UpdateTimers`) avant lecture des registres
  d'IRQ → un code qui *poll* IPR/ISR voit un bit pending ≤ 1 instruction en retard.
- **SON S3** — gain LMC ½-amplitude : table DAC pleine + `outScale_=0.5` **sans le ×2** que Hatari
  met dans `left/right_gain` → le YM STE ressort **~6 dB trop bas** relativement quand le LMC est à
  plein volume ; ratio YM:DMA aussi décalé.
- **FDC** — changement lecteur/face « **pull** » (`refreshDriveSide` au prochain accès registre)
  au lieu de « push » (immédiat à l'écriture PSG, `FDC_SetDriveSide`) → index ré-ancré tard ; un
  flip de face en plein transfert utilise l'ancienne face. Boot `.ST`/TOS non affectés.
- **MIDI M-MIDI** — l'ACIA MIDI fusionne RDRF avec `!rx_.empty()` (pas de `rdrf_` séparé). Comme le
  correctif préserve `rx_` au master reset, le SR montre **RDRF=1 juste après reset** si un octet
  restait (Hatari efface RDRF, garde l'octet dans RDR). *Fix complet : `rdrf_` MIDI distinct.*

### 🟡 Nouveaux — BASSES (cas-limites/cosmétiques)
- **Vidéo** : filtre « écriture redondante » absent (freq/res rejouées même inchangées) ; `$FF8260`
  bits 2-7 non forcés à 1 sur ST ; alias shifter `$FF8261` non géré ; attribution ligne à longueur
  FIXE (`fc/cpl`) vs accumulée ; chemin `PrevSize` partiel ; quirks démos `$FF8205/07/09` (E605/Tekila).
- **FDC** : `dmaResetFifo` ne remet pas `bufPos_`/`dmaBytesToTransfer_` ; recalcul densité superflu
  à chaque lecture statut ; borne parseur STX (chemin secteurs complet) ; pas d'`indexCheckUpdate`
  avant la commande.
- **Son** : compteur bruit testé `>=` dans la garde 125 kHz (vs 250) ; pas de HPF sous-sonique sur
  le canal DMA ; `mode_` non masqué `&0x8f` (relecture) ; masque adresse DMA `$3fffff` non appliqué.
- **Blitter** : **BL-R** read8 ne rejette pas l'accès octet aux registres mot (rend la valeur vive
  vs IoMem rance) ; **BL-MST** `$FF8A3E/3F` dé-fauté à tort sur Mega ST (void seulement sur STE).
- **Bus** : wait-state 4 cyc des registres FDC/DMA non facturé ; `$FF860E/0F` densité routé sur STE
  simple ; `$FF8A3E/3F`→0x00 au lieu de 0xFF ; trou MMU STF bank0=128K/bank1=2048K non émulé.

### Conclusion
La 1ʳᵉ passe avait capté l'essentiel ; les 4 correctifs sont validés. Priorités issues de la 2ᵉ
passe : **BUS-LEAK** (bug clair, fix trivial) et **BL-GPIP3** (bug fonctionnel pour le polling de
fin de blit), puis S3 (gain LMC) et les moyennes de fidélité. Le reste est cas-limite.
