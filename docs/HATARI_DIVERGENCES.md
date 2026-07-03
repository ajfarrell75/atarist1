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
points « choix de comportement » **tranchés** (SC1 loopback SCC honoré = datasheet, D1/D2 WRITE
TRACK STX, NeoST plus correct en HD/ED — tous « NE PAS corriger ») et les marginaux (`$FF8264`,
D4 6250, F1, M1) sont laissés documentés pour suivi.

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
| SC1 | SCC | **TX émis immédiatement** (pas de cadence baud / Zero Count) ; `WR14` bit4 Local Loopback honoré (datasheet, absent d'Hatari) — **tranché : choix délibéré** | volontaire | `Scc.cpp:234,269-274`, reset `Scc.cpp:61` | `SCC_WriteDataReg` / `SCC_Process_TX` `scc.c:1655-1681,1986` |
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
  overscan de démos STE (E605, DHS) mal placé.* **Contrepartie mesurée (2026-07-03, oracle
  AVI)** : les démos STF rendent « trop propre » en config STE — le menu robot Cuddly est
  PARFAIT chez NeoST STE (timings STF appliqués partout) alors que Hatari STE (= vrai STE) le
  CASSE (couleurs faussées, bordures non ouvertes, scroller haché). Un rapport GUI « menu
  cassé en STE » peut donc être FIDÈLE — vérifier la machine avant de conclure à un bug.
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

- **[B1 — HAUTE] ✅ corrigé (2 passes)** — Compteur X **ou** Y écrit à `0` interprété comme **65536**
  (X via bouclage 16 bits ; Y via `yLatch_`, conversion **à l'écriture** du registre `$FF8A38` ≙
  `Blitter_LinesPerBitblock_WriteWord` blitter.c:1356). ⚠ La 1ʳᵉ passe (0→65536 relu dans
  `runSlice`) avait INTRODUIT une régression HAUTE : le restart du driver TOS (`bset #7` après
  chaque blit, y_count **résiduel** 0) relançait un blit de 65536 lignes → RAM labourée →
  **bureau TOS 1.06 STE scramblé au moindre redraw**. Modèle complet fidèle (commit c96311c) :
  start avec résiduel 0 → BUSY+HOG **effacés**, aucun blit (`Blitter_Control_WriteByte`
  blitter.c:1433-1437) ; écrire 0 PUIS démarrer → 65536 lignes (légal).
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
  ni interruption Zero Count (BRG) vs `SCC_Process_TX` cadencé (`scc.c:1986,2231`) — conséquence
  volontaire de l'absence de BRG cadencé (cible NeoST).
- **[SC1b — TRANCHÉ : choix délibéré, NE PAS corriger]** **`WR14` bit4 (Local Loopback) `|= 0x30`
  au reset** (`Scc.cpp:61`) → tant que bit4=1, TX reboucle en interne (TxD→RxD) et n'atteint pas
  `sink_`. NeoST **honore** ce bit (datasheet Zilog Z85C30 §WR14) ; Hatari NE le modélise pas
  (`SCC_Process_TX` émet toujours). **Inoffensif** : tout pilote série/LAN réécrit `WR14` (réglage
  du BRG, bit4=0) avant d'émettre. NeoST reste donc plus fidèle au chip — cf. § « NeoST améliore
  Hatari » ci-dessous. (`Scc.cpp:serialWriteByte`)
- **[basse]** Horloges/baud (PCLK, BCLK, time constant `WR12/13`) entièrement absentes
  (`SCC_Compute_BaudRate` `scc.c:1027`) — conséquence de SC1, volontaire (cible NeoST).

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

### 🔴 Nouveaux — BUGS NETS actionnables — ✅ CORRIGÉS
- **BUS-LEAK ✅ corrigé** *(basse-moyenne, bug réel)* — `Bus::write16`/`write32` branche blitter
  faisaient `return` **sans restaurer `ioAccessWidth_`** → après le 1ᵉʳ blit mot, les bus-errors
  d'accès **octet** ($FF9200/lightpen/FDC) restaient désarmées en permanence. **Fix :**
  `ioAccessWidth_ = saved` ajouté avant le `return` des deux branches blitter (`Bus.cpp`).
- **BL-GPIP3 ✅ corrigé** *(moyenne, bug réel)* — la ligne GPU_DONE (GPIP3) était posée à
  `finishTransfer` mais **jamais ré-armée haute** → blit-done « toujours vrai » dès le 2ᵉ blit.
  **Fix :** `start()` dé-asserte la ligne (`setBlitterLine(false)`) au (re)démarrage de chaque blit
  (`Blitter.cpp`), comme Hatari `Blitter_Start` (blitter.c:895) ; `finishTransfer` la rabaisse.

### 🟠 Nouveaux — MOYENNES (fidélité)
- **MFP** — pas de dispatch des timers échus (`MFP_UpdateTimers`) avant lecture des registres
  d'IRQ → un code qui *poll* IPR/ISR voit un bit pending ≤ 1 instruction en retard.
- **SON S3** — gain LMC ½-amplitude : table DAC pleine + `outScale_=0.5` **sans le ×2** que Hatari
  met dans `left/right_gain` → le YM STE ressort **~6 dB trop bas** relativement quand le LMC est à
  plein volume ; ratio YM:DMA aussi décalé.
- **FDC** — changement lecteur/face « **pull** » (`refreshDriveSide` au prochain accès registre)
  au lieu de « push » (immédiat à l'écriture PSG, `FDC_SetDriveSide`) → index ré-ancré tard ; un
  flip de face en plein transfert utilise l'ancienne face. Boot `.ST`/TOS non affectés.
- **MIDI M-MIDI ✅ corrigé** — l'ACIA MIDI a désormais un `rdrf_` **distinct** de `!rx_.empty()`
  (`MidiAcia.cpp/.hpp`) : le master reset l'efface (SR → TDRE seul, conforme `ACIA_MasterReset`)
  **sans purger la file** → l'octet reste relisible via RDR, et RDRF retombe correctement. Le
  bouclage « M MIDI » reste fonctionnel (file vide au reset du diagnostic).

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
La 1ʳᵉ passe avait capté l'essentiel ; les 4 correctifs sont validés. **Corrigés à la 2ᵉ passe
(✅)** : BUS-LEAK, BL-GPIP3, MIDI M-MIDI — validés `glue-selftest` 19/0 + boots ST/STE/MegaSTE
pixel-identiques. **Différés** (validation impossible ici) : **S3 gain LMC** (audio non vérifiable
sans WAV oracle ni écoute — risque de déséquilibre/clip), **FDC drive/side push** (ré-ancrage
d'index du modèle rotationnel — risque de régresser des chargements disque sans oracle byte-exact),
**MFP UpdateTimers** (dispatch d'événements avant lecture IPR = risque de réentrance), et les basses
**cycle-exactes** (vidéo) / niche (bus N2-N5, blitter BL-R/BL-MST). À reprendre quand l'oracle
Hatari headless (`extern/hatari/build/src/hatari`) est bâti.

---

## 🧩 3ᵉ passe — sous-systèmes périphériques (2026-06-15, workflow 5 agents)

Couvre les sous-systèmes **NON audités** par les passes 1-2 : **RTC RP5C15, ACSI/DMA HD,
GEMDOS HD, FPU MC68881, SCU**. Verdict : portages globalement **très fidèles** ; aucune des
divergences ne casse le boot. Trouvailles actionnables ci-dessous (terrain neuf → vrais bugs).

### 🔴 Actionnables (par sévérité)

> **Statut des corrections (2026-06-15).** ✅ **CORRIGÉS** (validés : build, `glue-selftest` 19/0,
> boots ST/STE/MegaSTE pixel-identiques, **FPU test ROM 9/9**) : **SCU reset** (`Scu::reset(cold)`
> + appels `Machine::reset/hardReset`) · **FPU propagation NaN** (renvoie l'opérande NaN quiété) ·
> **FPU SNaN→SNAN** (flag softfloat `flag_signaling` distinct → `EXC_SNAN`) · **FPU masques
> FPCR/FPSR** (`&0xFFF0` / `&0x0FFFFFF8`) · **FPU FSGLMUL** (entrées tronquées 24 bits) ·
> **ACSI INQUIRY `buf[4]`** (valeur fixe 31) · **GEMDOS `.`/`..` en sous-répertoire** (paramètre
> `subdir`) · **FPU FSCALE ∞/NaN** (NaN→propagation, ∞→OPERR+NaN, plus d'UB) · **FPU octet AEXC**
> (UNFL accumulé seulement si INEXACT ; INEX sur INEX2 OU OVFL) · **GEMDOS `only_invalid`** (passe
> « caractères invalides » séparée de la troncature, `?` ne matche que les caractères invalides) ·
> **FPU FMOVECR INEX2+arrondi** (table `fpp_cr` portée : `inex` + `rnd[4]` par mode RN/RZ/RM/RP,
> arme `EXC_INEX2`/`AEXC_INEX`). ⏸️ **DIFFÉRÉS** (table de données / plateforme / plomberie / non
> vérifiables headless) : ACSI délai IRQ post-transfert · GEMDOS recomposition Unicode macOS · FPU
> arrondi de précision FMOVE/FABS/FNEG (plomberie softfloat) · FPU packed decimal bit-exact.

- **SCU — jamais réinitialisé au reset** *(HAUTE)* : il n'existe **aucun `Scu::reset()`**, et ni
  `Machine::reset()` ni `hardReset()` ne réinitialisent le SCU → `SysIntMask`/`VmeIntMask`/états
  **persistent** au reset doux (Hatari `SCU_Reset` les met à 0, GPR1=0x01, cold/warm). *Fix :
  ajouter `Scu::reset(bool cold)` et l'appeler.* (`Scu.hpp`, `Machine.hpp:73-92`)
- **FPU — propagation des NaN perd le payload** *(ÉLEVÉE)* : `propagateNaN` renvoie toujours le
  default-NaN `0x7FFF8000…` au lieu de l'opérande NaN quiété (signe+payload). (`SoftFloatX80.hpp:59-66`
  vs `softfloat-specialize.h:321-365`)
- **FPU — SNaN lève OPERR au lieu de SNAN** *(ÉLEVÉE)* : le bit `EXC_SNAN`/vecteur 54 n'est jamais
  posé ; une entrée SNaN replie `flag_invalid → OPERR` (vecteur 52). (`SoftFloatX80.hpp:60,64`,
  `Fpu.cpp:119` vs `fpp.c:88-89`)
- **ACSI — INQUIRY `buf[4]` erroné** *(MOYENNE)* : NeoST écrase l'« Additional Length » avec
  `count()-5` (variable) au lieu de la valeur fixe **31** d'Hatari → pilote HD lisant ce champ
  trompé. (`Acsi.cpp:134` vs `hdc.c:218-246`)
- **ACSI — pas de délai d'IRQ post-transfert** *(MOYENNE)* : IRQ HDC levée immédiatement ; Hatari
  la diffère de `ACSI_TRANSFER_MIN_CYCLES=1000` (requis par « Idris OS »). (`Fdc.cpp:2043` vs `hdc.c:1162`)
- **GEMDOS — `Fsfirst`/`Fsnext` n'énumèrent jamais `.`/`..` en sous-répertoire** *(MOYENNE)* :
  `fsfirst_match` rejette tout nom en `.` sans paramètre `subdir` → gestionnaires de fichiers /
  archiveurs récursifs affectés. (`GemdosHd.cpp:135-149` vs `gemdos.c:433-484`)
- ✅ **GEMDOS — matching « caractères invalides » (`only_invalid`)** *(MOYENNE)* : `addPathComponent`
  fait désormais DEUX passes distinctes comme Hatari — troncature (`*`, `onlyInvalid=false`) puis
  caractères invalides (`+`→`?`, `onlyInvalid=true`) — et `fsfirst_match` ne fait matcher un `?`
  « invalide » qu'un caractère réellement invalide pour Atari (`filenameInvalidChar`, port de
  `Str_Filename_Invalid_Char`). Plus de risque d'ouvrir le mauvais fichier hôte.
  (`GemdosHd.cpp` vs `gemdos.c:1374-1396`)
- **GEMDOS — recomposition Unicode NFD→NFC macOS non portée** *(MOYENNE, macOS)* :
  `Str_DecomposedToPrecomposedUtf8` absent → fichiers accentués introuvables sur macOS (nul sur Linux).
- **FPU — divers** *(MOYENNE)* : ✅ FSGLMUL tronque désormais ses entrées à 24 bits ; ✅ FSCALE par
  ∞/NaN gère NaN→propagation / ∞→OPERR (plus d'UB) ; ✅ octet AEXC corrigé (UNFL conditionné par
  INEXACT, INEX sur OVFL) ; ✅ FMOVECR arme INEX2/AEXC_INEX et applique l'ajustement d'arrondi
  RN/RZ/RM/RP (table `fpp_cr` `inex`+`rnd[4]` portée). ⏸️ Reste : FMOVE/FABS/FNEG n'arrondissent
  pas selon la précision FPCR ; packed decimal via libc hôte (drapeaux INEX1/OPERR du format P
  absents) ; octet AEXC accumulé (UNFL non conditionné par INEX2).
- **FPU — masques FPCR/FPSR non appliqués** *(BASSE)* : bits réservés (FPCR 3-0, FPSR 0-2/28-31)
  conservés au lieu d'être forcés à 0 (`fpcr_mask=0xfff0`, `fpsr_mask=0x0ffffff8`). (`Fpu.cpp:467-469`)

### Choix intentionnels / NeoST plus correct que Hatari (NE PAS « corriger »)
- **RTC temps émulé** (cycles) vs `localtime()` hôte — déterminisme headless (assumé).
- **RTC Mega-only** — le RP5C15 n'existe **physiquement que sur Mega** (NeoST > simplification Hatari
  qui l'expose sur toute ST/STE).
- **RTC registre reset / bissextile** — NeoST suit le datasheet RP5C15.
- **SCU encodage bit IRQ1 logicielle** — NeoST utilise le bit 1 (correct) vs bit 0 chez Hatari.
- **FPU transcendantes** en double hôte ; socket vide → « not found » — corrects/hors périmètre.

### Confirmé FIDÈLE 1:1 (scruté cette passe)
ACSI : réception paquet (A1/ICD/LUN), **toutes** les commandes SCSI (TUR/REQ SENSE/RD-WR 6-10/
INQUIRY/READ CAPACITY/MODE SENSE 00-04-3f/SEEK/FORMAT/SHIP/REPORT LUNS), MODE SENSE géométrie,
table de partitions DOS+Atari, incrément/masque adresse DMA, court-circuit FIFO. · GEMDOS : table
d'interception (23 opcodes), Fopen/Fcreate/Fread/Fwrite/Fseek/Fattrib/Fdatime/Frename/Fdelete,
Dcreate/Ddelete/Dsetpath/Dgetpath/Dfree, handles+aliasing, Pexec/LoadAndReloc/reloc, conversion
chemins, codes d'erreur, multi-partitions C..Z. · FPU : cœur softfloat add/sub/mul/div/sqrt/rem/mod/
roundToInt, vecteurs d'exception+priorités, prédicats FBcc/FScc, **valeurs** des constantes FMOVECR,
FCMP/FTST, octet quotient FMOD/FREM, FSAVE/FRESTORE (idle $1F18/null). · RTC : 16 registres, bank
AM/PM, mode, adresses impaires, init heure hôte. · SCU : gating IRQ (masques Sys/Vme), MFP6/SCC5 sur
VmeIntMask, niveaux 4/2/1, IRQ niveau 5 vectorisée, décodage adresses impaires.

**Bilan 3ᵉ passe** : 1 HAUTE (SCU reset) + 2 ÉLEVÉES FPU (NaN/SNaN) + ~8 moyennes — tous **bornés et
corrigeables sans oracle** (logique pure, pas de cycle-exactness). C'est la passe la plus productive
en correctifs actionnables, justifiant le ciblage des sous-systèmes vierges plutôt qu'un re-balayage.

---

## 🧷 4ᵉ passe — couches d'intégration (2026-06-15, workflow 2 agents)

Couvre les **dernières couches jamais auditées** : le **wrapper CPU/Moira** (`Cpu68k`, câblage
exceptions/IPL/bus-error/IACK/reset) et les **E/S** (joypads/paddles `StePads`, `JoystickInput`,
Centronics). Verdict : **très fidèles**. **2 divergences réelles — toutes deux corrigées** (SCC
No-Vector ; support imprimante Centronics) + des points cycle-exacts déjà cadrés.

> ⚠️ **Oracle Hatari non bâtissable ici** : SDL2 absent du conteneur → les divergences
> **cycle-exactes** (phase CPU↔faisceau, échantillonnage IPL au cycle, latence d'exception, jitter
> E-Clock à l'IACK) restent **bloquées dans cet environnement** ; elles nécessitent une session avec
> SDL2 pour construire `extern/hatari/build`. Déjà cadrées dans `TODO.md §beam-sync`.

### Trouvailles
- **CPU — SCC No-Vector → mauvais vecteur** ✅ **corrigé** *(moyenne)* : sur IACK niveau 5 avec WR9
  NV armé (`Scc::processIack()` = -1), NeoST renvoyait l'auto-vecteur **29** (`24+level`, $74) au
  lieu du vecteur **spurious 24** ($60) d'Hatari (`iack_cycle` : `vector<0 → 24`). La branche MFP
  était déjà conforme. **Fix** : `Cpu68k.cpp` renvoie 24 sur `v<0` pour le SCC. (Validé : build,
  glue 19/0, boots ST/STE/MegaSTE pixel-identiques.)
- **Centronics — support imprimante** ✅ **corrigé** *(moyenne)* : le `printerSink_` du PSG est
  désormais câblé (`Machine`) → sur chaque front de strobe, l'octet du port B est **capturé dans un
  fichier hôte** (`Machine::setPrinterFile`, option headless `--printer FILE`) et la ligne BUSY
  (GPIP0) est assertée bas, port fidèle de `psg.c:388-390` (`Printer_TransferByteTo` +
  `MFP_GPIP_Set_Line_Input LINE0 LOW`). Sans fichier imprimante : no-op (défaut inchangé). Validé :
  mini-ROM imprimant « NeoST\n » via le protocole Centronics → fichier capturé identique.
- **PSG port A/B en entrée (joysticks parallèles) absent** *(très basse)* : Hatari recompose R14
  bit5 / R15 depuis les joysticks « parallel port » à la lecture (`psg.c:289-312`) — périphérique de
  niche, non émulé par NeoST.

### Confirmé FIDÈLE 1:1
**CPU** : trame bus-error/address-error groupe 0, double-bus-fault→HALT (2ᵉ faute + SSP impair),
vectorisation IACK MFP niveau 6, auto-vecteurs VBL/HBL + clear pending, reset SSP/PC via overlay ROM,
gating IPL (priorité MFP>VBL>HBL ; SCU sur MegaSTE), STOP, wait-states PSG/MFP/ACIA/bus + E-Clock,
MegaSTE 16 MHz (créneau bus + cache). **E/S** : StePads (multiplexage $FF9202, boutons/directions
pads A/B, DIP MegaSTE 0xBF, paddles REALSTICK, lightpen, bus-errors octet/mot $FF9200-23),
JoystickInput (mapping, ports), suivi du front de strobe Centronics.

**Bilan 4ᵉ passe** : couche d'intégration **solide** ; SCC NV **corrigé** ; support imprimante
Centronics **ajouté** (capture `--printer` + BUSY). Le terrain LOGIQUE est désormais **épuisé** — il
ne reste que le **cycle-exact**, qui exige l'oracle (SDL2 absent ici).
