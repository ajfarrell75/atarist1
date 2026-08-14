# Divergences NeoST ↔ Hatari — cartographie pour corrections futures

**But.** Inventaire des écarts entre NeoST et la **source de vérité Hatari**, sous-système par
sous-système, pour prioriser les corrections futures. Chaque écart est ancré `fichier:ligne`
des deux côtés, classé par sévérité (**haute** = casse logiciels/boot · **moyenne** = fidélité
visible · **basse** = cas-limite/cosmétique) avec son impact connu.

**Méthode.** Comparaison ligne à ligne du code NeoST (`src/`) au source C de Hatari
(`extern/hatari/src`, gitignoré — l'inventaire ci-dessous a été établi sur le commit
`c9906f1` mais l'arbre PRÉSENT est `981f291` : ⚠ les numéros de ligne Hatari cités peuvent
avoir glissé. Lu comme
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
validation exige l'oracle Hatari headless, désormais **BÂTI** (`extern/hatari/build/src/hatari`,
v2.6.1) → ces chantiers sont validables, cf. `docs/HATARI_AUTOMATION.md`. Les
points « choix de comportement » **tranchés** (SC1 loopback SCC honoré = datasheet, D1/D2 WRITE
TRACK STX, NeoST plus correct en HD/ED — tous « NE PAS corriger ») et les marginaux (`$FF8264`,
D4 6250, F1, M1) sont laissés documentés pour suivi.

**⟳ Rafraîchissement (5ᵉ passe, 2026-07-07, 4 agents — § en fin de document).** Beaucoup de
statuts ci-dessous ont bougé depuis : le commit `bc15a67` (2026-07-03, titre trompeur « STOP
handling ») contient l'INTÉGRALITÉ du bug hunt 39-findings — **M1 (fronts GPIP), Timer B evt=0,
$FF8264, VoidRead 0x00, read32 void, trou MMU STF, bruit ≥/250 kHz, `mode_ &0x8f` sont CORRIGÉS** ;
**D4, BL-MST, cartouche 0xFF, bits SR MIDI sont des FAUX POSITIFS** (Hatari fait pareil) ; V3 est
**partiellement résolu** (restart compteur porté). Nouvelles entrées : **S4 table DAC YM**
(défaut Hatari = table mesurée, NeoST = modèle), **hybride WS1/WS3** et, à l'époque,
troncature MFP→CPU sans reste (**portée depuis le 2026-08-14**). La 5ᵉ passe fait foi en cas
de contradiction avec les sections historiques.

---

## Bilan de fidélité par sous-système

| Sous-système | NeoST | Hatari (vérité) | Fidélité | Écarts ouverts M / B (5ᵉ passe 2026-07-07) |
|---|---|---|---|---|
| MFP 68901 | `Mfp.cpp` | `mfp.c` | très élevée | 1 (UpdateTimers, mode bloc) / 5 |
| Vidéo (Shifter/Glue) | `Shifter.cpp` | `video.c` | très élevée (STF WS3 + Glue STE + tricks res) | 0 moyennes (WS/V1/V2 ✅ 2026-07-08 ; résidus V2 : Paulo Simoes, $FF8261) / ~9 |
| FDC + DMA + STX | `Fdc.cpp`, `StxImage.cpp` | `fdc.c`, `floppies/stx.c` | très élevée | 2 (D3, wait-state 4 cyc) / 4 (+2 assumés D1/D2) |
| Son (YM2149 + DMA STE) | `YM2149.cpp`, `DmaSound.cpp` | `psg.c`, `dmaSnd.c`, `sound.c` | très élevée (générateurs 1:1, FIFO DMA au faisceau) | 0 (S2/S3/S4 ✅ 2026-07-07) / ~6 |
| ACIA 6850 / IKBD / MIDI | `Ikbd.cpp`, `MidiAcia.cpp` | `acia.c`, `ikbd.c` | très élevée | 0 / 7 (délais IKBD…) |
| Bus / mémoire / bus-error | `Bus.cpp` | `ioMem*.c`, `memory.c`, `stMemory.c` | très élevée | 0 / 2 ($FF860E STE, $FF8A3E valeur) |
| Blitter | `Blitter.cpp` | `blitter.c` | très élevée (données + bus) | 0 / 2 (CPU parallèle, BL-R) |
| SCC Z85C30 (MegaSTE) | `Scc.cpp` | `scc.c` | élevée (cœur registre) | 1 (SC1, tranché) / 2 |

---

## Priorités — divergences HAUTE et MOYENNE

| # | Sous-système | Divergence | Sévérité | NeoST | Hatari |
|---|---|---|---|---|---|
| B1 ✅ | Blitter | Compteur X/Y écrit à `0` non interprété comme **65536** (blit avorté au lieu de maximal) | **HAUTE** | `Blitter.cpp:99-101` | `Blitter_WordsPerLine/LinesPerBitblock_WriteWord` `blitter.c:1343-1366` |
| V1 ✅ | Vidéo | ~~Branche STE de la Glue absente~~ **portée (2026-07-08)** : table STE (preload MMU 36/40, pal 56, HSync −52/−12) + `LEFT_OFF_2_STE` (+20 o, −8 px) + latch res sans −1 ; Cuddly-STE casse comme le vrai STE (196/250 = oracle) | moyenne | `Shifter.cpp` (`glue::Timing`, phase 1 STE) | `Video_Update_Glue_State` (branche STE) `video.c:2442-2652` |
| V2 ✅ | Vidéo | ~~Tricks par changement de résolution~~ **portés (2026-07-08)** : overscan med-res (No Cooper greetings **0 px vs oracle**), stab med, scrolls hard 13/9/5/1 px, rendu multi-rés par ligne. Résidus : hardscroll Paulo Simoes, alias $FF8261 | moyenne | `Shifter.cpp` (`updateGlueRes`) | `Video_WriteToGlueRes` `video.c:1618-1820` |
| V3 ◐ | Vidéo | Géométrie mid-trame : **restart compteur PORTÉ** (VC_RESTART, 2026-07-02) ; restent CyclesPerVBL±4 + attribution ligne fixe (canal `NEOST_LINELEN` existant : moitié Machine ON par défaut, moitié Shifter OFF) | moyenne→basse | `Shifter.cpp:602,1722`, `Machine.cpp:250-254` | `Video_RestartVideoCounter` `video.c:4608`, `video.c:2848-2877` |
| WS ✅ | Vidéo | ~~Hybride WS1/WS3~~ **TRANCHÉ : WS3 complet (2026-07-08)** — positions Glue +1 (`glue::kWsInc`), IRQ HBL à cpl (512/508/224, `kHblOff` 0), VBL 64 ✓. Ancres rendu/compteur/spec512 **fixes** 56/376 (≙ `LINE_START/END_CYCLE_*` hors table WS chez Hatari) ; DE stockés re-normalisés −inc au rendu. `NEOST_WS=1..4` pour A/B. Datations read −6/write +2/spec512 −25 **inchangées** (fidèles-théoriques, WS-indépendantes). Validé : étalons TOUS OK, boot STF 50 Hz 0 px, Cuddly menu == HEAD au px près (190/250 vs oracle, identique baseline) | moyenne (systémique) | `Shifter.cpp` (glue::), `Machine.cpp` | `VIDEO_TIMING_DEFAULT=WS3` `video.c:624`, `video.c:976-1007` |
| S1 ✅ | Son | **Filtre passe-bas STF (C10) jamais activé** → STF/Mega ST en PWM (code mort) | moyenne | `setStfLowPass` jamais appelé, `YM2149.hpp:165` | `Sound_Update_Filters` `sound.c:1946-1951` |
| S2 ✅ | Son | ~~DMA STE sans FIFO 8 octets ni avance HBL~~ **corrigé (2026-07-07 soir)** : FIFO 8 octets fetchée par MOTS à chaque HBL (`DmaSound::onHbl` ← `Machine::onHbl`), fin de trame **au fetch** (XSINT/Timer A en avance, HBL-quantifié), octets **capturés au faisceau** pour le rendu (plus de relecture RAM en fin de trame — cas Mental Hangover), réalignement mono→stéréo, compteur $FF8909 = adresse de fetch. Étalon `tools/make_dmasnd_test.py` : 33,3 % d'octets B = oracle Hatari (33,2 %) | moyenne | `DmaSound.cpp` (`fifoRefill`/`updateDac`) | `DmaSnd_FIFO_*` / `DmaSnd_STE_HBL_Update` `dmaSnd.c:342-438,727-741` |
| D1 | FDC/DMA | **WRITE TRACK STX réinterprété** en secteurs (CRC « nettoyé », statut neutralisé) | moyenne | `StxImage.cpp:254-300`, `Fdc.cpp:986-1008` | `FDC_WriteTrack_STX` (TODO, pas de relecture) `stx.c:2027-2134` |
| D2 | FDC/DMA | **READ TRACK STX** renvoie la piste réécrite (conséquence de D1) | moyenne | `Fdc.cpp:1050,1062` | `FDC_ReadTrack_STX` `stx.c:1863` |
| D3 | FDC/DMA | Flush FIFO↔RAM **ne stalle pas le CPU** (wait-state 32 cyc manquant) — cycle-exactness | moyenne *(à confirmer)* | `Fdc.cpp:667-702` (`fifoPush`/`fifoPull`) | `FDC_DMA_FIFO_Push/Pull` `fdc.c:1340,1396` |
| M1 ✅ | MFP | ~~Lignes GPIP on-chip sans machine de fronts AER/DDR~~ **corrigé (bc15a67)** : `gpipSetLine`/`gpipUpdateInterrupt` = port de `MFP_GPIP_Set_Line_Input`, tous les appelants convertis | moyenne | `Mfp.hpp:303-308`, `Mfp.cpp:493-504` | `MFP_GPIP_Set_Line_Input` `mfp.c:1143-1219` |
| S4 ✅ | Son | ~~Table DAC « model » seul~~ **corrigé (2026-07-07 soir)** : table MESURÉE par défaut (`ym2149_fixed_vol.h` vendorisé + port de `interpolate_volumetable`), modèle conservé sous `NEOST_YM_MIXING=model` | moyenne | `YM2149.cpp` `dacTable()` | `YM_TABLE_MIXING` défaut `configuration.c:807`, `sound.c:505-543` |
| S3 ✅ | Son | ~~Gain LMC ×2 manquant (YM STE −6 dB)~~ **corrigé (2026-07-07 soir)** : `kLmcMakeup=2.0` dans `gainLeft/Right/masterGain` + `kDmaGain` 0.7→0.375 (= ¾×½). Validé : cloche GEM ST vs STE ratio RMS **1.000** ; ratio DMA 0.75 exact | moyenne (audible) | `DmaSound.cpp` | ×2 `dmaSnd.c:1152-1153,1460-1461` + « 3/4 level » `dmaSnd.c:1146-1158` |
| MC ✅ | MFP | ~~**Conversion MFP→CPU tronquée par période, sans reste accumulé**~~ **corrigée (2026-08-14)** : échéances absolues ×256, plafond vers le Scheduler, phase sérialisée v11 | moyenne-basse | `Mfp.cpp` (`timerPeriodSubCycles`, `scheduleTimerAt`) | unités internes ×256 `cycInt.c:26-45` |
| BU1 ✅ | Bus | **Miroir matériel du PSG `$FF8804-$FF88FF`** non routé vers le YM2149 (lit `0xFF`, écritures ignorées) | moyenne | `Bus.cpp:531,623` | `IoMem_Init` shadow PSG `ioMem.c:386-393` |
| BL2 ✅ | Blitter | Accès **OCTET** aux registres mot/long non rejetés | moyenne | `Blitter.cpp:50-61` | `Blitter_CheckAccess_Byte` `blitter.c:972-989` |
| SC1 | SCC | **TX émis immédiatement** (pas de cadence baud / Zero Count) ; `WR14` bit4 Local Loopback honoré (datasheet, absent d'Hatari) — **tranché : choix délibéré** | volontaire | `Scc.cpp:234,269-274`, reset `Scc.cpp:61` | `SCC_WriteDataReg` / `SCC_Process_TX` `scc.c:1655-1681,1986` |
| D4 ✖ | FDC | ~~Piste 6268 vs 6250~~ **FAUX POSITIF (5ᵉ passe)** : le `#define FDC_TRACK_BYTES_STANDARD` actif d'Hatari vaut **6268** (la formule 6250 y est commentée) = NeoST | — | `Fdc.cpp:156` | `fdc.c:~410` |

---

## Détail par sous-système

### MFP 68901 — `Mfp.cpp` ↔ `mfp.c`
Chaîne IRQ (IER/IPR/IMR/ISR/VR, élection priorité + chronologie, délai 4 cyc, IACK, EOI
auto/software, antidatage), timers datés, compteur vivant, event-count Timer A/B : **conformes**.

- **[M1 — moyenne] ✅ corrigé (bc15a67, 2026-07-03)** — machine de fronts centralisée :
  `gpipSetLine`/`gpipUpdateInterrupt` (`Mfp.hpp:303-308`, `Mfp.cpp:493-504`) = port de
  `MFP_GPIP_Set_Line_Input`/`_Update_Interrupt` (mfp.c:1143-1219), AER/DDR respectés, tous les
  appelants on-chip convertis (Ikbd/MidiAcia wire-OR/Fdc/Blitter). Cf. 5ᵉ passe.
- **[basse] ✅ corrigé (2026-08-14)** — conversion cycles MFP↔CPU en échéances absolues
  de 1/256 cycle CPU (`timerPeriodSubCycles` + `timerDueSub_`), même `CYCINT_SHIFT=8`
  qu'Hatari. Le plafond n'est appliqué qu'à la frontière du Scheduler et la fraction
  reste l'ancre du rechargement suivant ; phase sérialisée en save-state v11. *Hatari
  ajoute aussi un jitter aléatoire volontaire pour Lethal Xcess, non reproduit.*
- **[basse] ✅ corrigé (bc15a67)** — Event-count Timer B : garde `tbCounter_==0` supprimée,
  wrap uint8 0→255 (`Mfp.cpp:415-432` = `MFP_TimerB_EventCount` mfp.c:1297-1320).
- **[basse]** Retombée d'IRQ immédiate (`Mfp.cpp:529`) vs TODO Hatari d'un délai mesuré (`mfp.c:789`) — identique aujourd'hui, suivi seulement.
- *Faux positif écarté :* les wait-states 4 cyc d'accès MFP **sont** appliqués, mais par le
  `Bus` (`Bus.cpp:540`, `addMfpWaitCycles()`), pas par `Mfp.cpp` → **pas une divergence**.

### Vidéo (Shifter + Glue) — `Shifter.cpp` ↔ `video.c`
Cœur Glue **STF** = transcription quasi ligne-à-ligne (`updateGlueState` ≙ `Video_Update_Glue_State`
branche STF), constantes = `VIDEO_TIMING_STF_WS1`, compteur vidéo, bordures H/B/G/D 50/60 Hz,
spec512, scroll fin STE de base, masquage palette par machine : **conformes**.

- **[V1 — moyenne] ✅ PORTÉ (2026-07-08)** — branche **STE** de la Glue (phase 1 de
  `updateGlueState` + table `glue::Timing` STE : preload MMU, `Line_Set_Pal=56`,
  `LEFT_OFF_2_STE` +20 o/−8 px). Cf. 5ᵉ passe (statut à jour). Le menu Cuddly sur NeoST-STE
  casse désormais comme le vrai STE (196/250 trames = oracle Hatari-STE) — un rapport GUI
  « menu cassé en STE » est FIDÈLE, vérifier la machine avant de conclure à un bug.
- **[V2 — moyenne]** Tricks de bordure par **changement de résolution** non répliqués : pas
  d'équivalent de `Video_WriteToGlueRes` (`video.c:1618-1820`) — overscan med-res, stab
  hi/med/lo, scroll « hardware » 1/5/9/13 px. NeoST verrouille une résolution par trame
  (`frameMode_`). *Impact : No Cooper, Delta Force/PYM, Closure, HighResMode.*
- **[V3 — moyenne] ◐ partiellement résolu (2026-07-02)** — `RestartVideoCounter` **PORTÉ**
  (`Shifter.cpp:602/1722`, événement `VC_RESTART` `Machine.cpp:250-254`, check freq live).
  Restent : CyclesPerVBL ±4 si la dernière ligne change de freq, géométrie verrouillée à
  `beginFrame` ; le canal par-ligne `HBL_Pos/nCyclesPerLine` existe (gated `NEOST_LINELEN`,
  **ON par défaut depuis 2026-07-08**, cf. 5ᵉ passe).
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
byte-compatible : **conformes** (vérifiés ligne à ligne).

- **[D0 — moyenne] ✅ CORRIGÉ (2026-08-01)** — `.MSA`/`.DIM` étaient montées en **LECTURE
  SEULE**, et le bit **WPRT était présenté au programme émulé** (`writeProtect = !raw ||
  !writable`). Hatari ne dérive WPRT que du réglage et de `stat()` — **jamais du format
  d'image** (`floppy.c:205-225` `Floppy_IsWriteProtected`) : les écritures vont dans le
  tampon RAM et sont ré-encodées (`MSA_WriteDisk`, `DIM_WriteDisk`). Chez NeoST le
  drapeau ne bloquait pas que la recopie hôte, il pilotait le statut du WD1772
  (`updateWriteSectors`, `updateWriteTrack`, statut type I) : sur toute `.msa`/`.dim`,
  sauvegardes en jeu, high-scores, écritures depuis le bureau TOS et protections
  « écrit puis relit » échouaient « disque protégé », alors que la même disquette en
  `.st` fonctionnait.
  **Portage** : `encodeMsa` (port de `MSA_WriteDisk`/`MSA_FindRunOfBytes`, msa.c:275-420)
  et dispatch de `writeBack` par `FloppyDisk::imgFormat` — écriture partielle in situ pour
  le `.ST`, idem décalée de l'en-tête 32 o pour le `.DIM` (en-tête préservé, comme
  dim.c:134-149), ré-encodage complet et **atomique** (tmp + rename) pour le `.MSA`.
  `writeProtect` ne vient plus que de `stat()`. `!raw` ne subsiste que là où l'on ne SAIT
  PAS ré-encoder (STX, ou en-tête `.msa`/`.dim` reconnu mais indécodable) — y écrire
  détruirait le fichier. NeoST reste en **write-through** là où Hatari n'écrit qu'à
  l'éjection (une coupure y perd la sauvegarde).
  Couverture : `neost-headless --msa-selftest` (étalon `msa_selftest`, palier *fast*) —
  44 cas, aller-retour byte-exact sur 6 géométries × 7 motifs (dont `$E5` isolé à
  échapper, runs longs, incompressible qui force la piste brute) **plus** deux cas de bout
  en bout montage → écriture → remontage sur fichier réel `.msa` et `.dim`.
  ⚠ Cette ligne affirmait auparavant que `.MSA`/`.DIM` étaient « conformes (vérifiés
  ligne à ligne) » — c'était faux, et cela a masqué l'écart (audit du 2026-08-01).

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
- **[basse] ✅ vérifié SANS OOB (7ᵉ passe, 2026-08-06)** — Bornes parseur STX (`StxImage.cpp:90-200`) :
  rejette une image tronquée que Hatari monterait ; l'**OOB « latent »** annoncé sur le chemin
  « simple » tronqué est FERMÉ. Une piste simple tronquée pose `sectorsCount=0`/`sectors` vide
  (`inBuf` échoue avant `buildSectorsSimple`) ; les 4 consommateurs bornent sur
  `sectorsCountView()` (RNF après 5 tours). Compléments : clamp `TrackImageSize` (commit `7fe14bb`)
  + borne save-state `stxNextSector_` 0..255. Aucune image forgée ne provoque de lecture hors bornes.
- **[basse]** `pData==NULL` sans RNF → secteur lu 512×`0x00` + statut OK (image de bord, `StxImage.cpp:161`).
- **[basse] ✅ vérifié SANS bug (2026-07-09)** — INTRQ : **pas de double déclenchement**. Le
  `raise(SRC_FDC)` doublé a été retiré à la refonte M1 (`bc15a67`) ; entrée périmée (les anciennes
  lignes 638-645 = aujourd'hui `applyFastFdc`). Chemin unique : `fdcSetIrq` (edge-gardé `if(!was)`)
  → `setIntrqLine` → `Mfp::setFdcLine` → `gpipSetLine` (`if(line==active) return` + front AER,
  `Mfp.hpp:311`). Une INTRQ maintenue ne regénère aucune IRQ ; aucun `raise` manuel ni chemin
  contournant `gpipSetLine` (grep arbre entier).
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
- **[S2 — moyenne] ✅ corrigé (2026-07-07 soir)** — FIFO 8 octets + capture au faisceau portées
  (cf. tableau des priorités et 5ᵉ passe). *Avant :* lecture RAM directe à la cadence DMA en fin
  de trame, pas de réalignement mono→stéréo. *Impact : Mental Hangover, Power Up Plus.*
- ~~**[basse]** Fréquences de coupure du correcteur LMC1992 : RBJ 2e ordre 200/8000 Hz
  vs Savinkoff 1er ordre 118.28/8438.76 Hz (`dmaSnd.c:1418`)~~ **✅ corrigé (10ᵉ passe,
  2026-08-06)** : port exact des plateaux 1er ordre, coupures 118.2763/8438.756 Hz.
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
- **[basse, assumée (2026-07-12)]** PAUSE `$13` : NeoST gèle aussi l'octet déjà « en vol » (relivré à la reprise, rien de perdu) ; Hatari laisse finir TDR + TSR (jusqu'à 2 octets livrés après `$13`, ikbd.c:922-929).
- **[basse, assumée — NeoST plus robuste]** Monitoring `$17` : paquet « entier ou rien » ; Hatari est le SEUL émetteur sans `CheckFreeCount` et peut déchirer un paquet à 1 octet libre (désynchronise le flux, ikbd.c:1484). Observable seulement avec 1024 octets d'arriéré.
- **[basse, assumée]** Horloge IKBD amorcée sur l'heure hôte à la construction (Hatari l'efface au cold boot, ikbd.c:539) — confort bureau ; `--keys`/`setClock` couvrent le déterminisme headless. Idem : état des handlers 6301 custom ré-initialisé à chaque reconnaissance (les `static` d'Hatari casseraient un 2ᵉ lancement).

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
- **[basse] ✅ corrigé (audit blitter 2026-07-07)** — Write-protect `$0-$7` pour les accès
  **non-CPU** : `Blitter::writeWord` ignore désormais `addr < 0x8` (Hatari `SysMem_wput`,
  `memory.c:758`). *Avant :* un blit ciblant la table des vecteurs corrompait la RAM.
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
- **[basse] ✅ corrigé (audit blitter 2026-07-07)** — Masquage matériel à l'écriture des
  incréments (`&0xFFFE`, `blitter.c:1229`) et des adresses src/dst (`&0x00FFFFFE`,
  `Blitter_SourceAddr_WriteLong` blitter.c:1254) : `regWriteMask` masque désormais les octets
  faibles `$FF8A21/23/2F/31/27/35` (0xFE) et les octets forts `$FF8A24/32` (0x00). *Avant :*
  un incrément impair s'accumulait tel quel (dérive progressive des adresses vs Hatari).
- **[basse] ✅ corrigé (audit blitter 2026-07-07)** — Accès bus du blitter via le plan CPU brut
  (`bus_.read16/write16`) au lieu du modèle DMA (`STMemory_DMA_ReadWord/WriteWord`,
  `stMemory.c:724`) : une zone fautive lit désormais `0x0000` (`DMA_READ_WORD_BUS_ERR`) et
  absorbe l'écriture (`Blitter::readWord/writeWord` + `Bus::busFault`). *Avant :* un blit sur
  zone fautive lisait `0xFF`/void via le dispatch CPU.
- **[basse] ✅ corrigé (audit blitter 2026-07-07)** — `busCountError_` (bug « 63 accès ») remis
  à zéro à chaque entrée en phase PRE_START (`start()` non-hog), comme Hatari
  `Blitter_HOG_CPU_BusCountError = 0` (blitter.c:1457) — un accès volé dans une fenêtre
  périmée d'avant restart/reprise ne compte plus.
- **[basse] ✅ corrigé (2026-07-07 soir)** — Suspension **MID-WORD** portée
  (`BLITTER_CONTINUE_LATER_IF_MAX_BUS_REACHED`) : la tranche non-hog rend le bus exactement au
  64ᵉ accès, même entre deux accès d'un même mot — état du mot (`haveSrc_/haveDst_/fetchSrc_/
  dstWord_` ≙ `BlitterState`) persisté entre tranches. ⚠ Piège débusqué à la mise au point : sans
  la SAUVEGARDE de ces drapeaux en fin de tranche, la reprise refait la lecture source (double
  `srcShift` → pipeline skew corrompu → mots perdus au bord des icônes GEM — 6 px verts, repro
  `--walk-mouse` tos106fr STE, diagnostic à la fenêtre d'accès blitter).
- **[basse] ✅ corrigé (2026-07-07 soir)** — Part CPU non-hog = **64 accès bus CPU réels** (port
  `BLITTER_PHASE_COUNT_CPU_BUS` / `Blitter_HOG_CPU_mem_access_after`, modèle CE — celui de
  l'oracle) : comptés par les callbacks mémoire de Moira (`Bus::blitterCountCpu` →
  `Blitter::noteCpuBusAccess`), le 64ᵉ arme PRE_START et date la tranche à +4 cyc. *Avant :*
  forfait fixe 256 cycles (modèle non-CE) — le CPU « payait » sa part même sans toucher le bus.
- **[basse]** Pas d'exécution CPU parallèle pendant un blit (`Blitter_Check_Simultaneous_CPU`
  `blitter.c:1641`) : le CPU est stallé en bloc pendant la tranche, ses cycles internes ne
  recouvrent pas le blit (sauf les 4 cycles PRE_START). Nécessiterait des hooks par-cycle dans
  Moira. Aussi : hits du cache MegaSTE 16 MHz comptés comme accès bus (approximation), et le
  cycle RMW (`bset`/TAS) ne retient pas la prise de bus (`cpu_bus_rmw` d'Hatari).
- ~~**[basse]** HOP/LOP et bit4 du contrôle masqués seulement à l'exécution, pas à la relecture du registre.~~
  *(obsolète : `regWriteMask` masque à l'écriture depuis la refonte — relecture fidèle.)*

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
- **[SC2 — basse, TRANCHÉ : NeoST suit la datasheet (2026-07-12)]** **Lecture RR9** : NeoST
  renvoie `WR[13]` (`Scc.cpp:304`, RR9 = image de RR13 selon la datasheet Zilog) là où Hatari
  renvoie `WR[9]` (`scc.c:1606-1610` — son propre commentaire « also returns RR13 » contredit
  son code). Diff au byte près face à l'oracle si un programme lit RR9 — à garder en tête
  lors d'un diff SCC, ne PAS « corriger » sans le documenter.
- **[SC3 — basse, bug Hatari]** **Lecture $FF8E07 (VME Interrupter)** : Hatari renvoie
  `SCU.SysInterrupter` (copier-coller, `scu_vme.c:282-286`) ; NeoST renvoie bien
  `vmeInterrupter` — conforme au matériel, cf. § « NeoST améliore Hatari ».
- **[SC4 — basse, non porté]** **Accès aux masques SCU ($FF8E01/$FF8E0D)** : chez Hatari,
  LIRE ou ÉCRIRE un registre de masque **purge toutes les IRQ pendantes** SCU
  (`scu_vme.c:202-221,351-369`) ; dans NeoST le modèle « sources vivantes » (`Cpu68k::syncState`
  re-peuple les pendings à chaque updateIpl) les fait survivre. Observable seulement sur
  MegaSTE si un programme polle $FF8E01 avec une VBL/HBL en attente — divergence assumée du
  modèle niveau, à re-trancher si un étalon MegaSTE l'expose.
- **[SC5 — basse, suivi (bug hunt 2026-08-13)]** **Miroir RR6/RR7 (FIFO de statut coupée)** :
  NeoST renvoie RR2/RR3 du **canal A** quel que soit le canal adressé et teste `WR15`
  du canal A (`Scc.cpp:297-299`) ; la sémantique miroir du Z85C30 voudrait RR2B (vecteur
  modifié) / RR3B (=0) pour le canal B. Registres miroirs rarement lus, non tranché faute
  d'oracle (l'arbre Hatari n'est pas dans le checkout d'audit) — à vérifier contre `scc.c`
  avant toute correction. Deux **typos avérées** corrigées le même jour (commit `a5c9e6f`) :
  statut vectorisé lisait RR0 au lieu de RR1, et la lecture RR15 écrasait WR15 (`&=`).

---

### CPU (Moira + intégration) — suivi (audit vendor 2026-08-13)

Audit complet des patches vendorisés (`extern/moira/NEOST_VENDOR.md`) : diff intégral
contre l'upstream — 5 fichiers modifiés, tous les hunks documentés, aucun écart
accidentel. Le crash « BusError hors execute() via GEMDOS HD » a été **corrigé le jour
même** (primitives non-fautives + filet au site d'appel). Restent trois points bas :

- **[CPU1 — basse]** Le fast-forward du STOP (`Cpu68k.cpp`, garde `isStopped() &&
  !irqDeliverable()`) saute par-dessus une exception trace ou privilège en attente
  (`stop` sous bit T armé ; `stop #imm` effaçant S) : l'exception part avec jusqu'à un
  écart d'événement de retard (vs immédiat sur 68000 réel). Timing seul, motifs
  exotiques (moniteurs pas-à-pas) — NE PAS toucher sans re-valider Closure (le
  fast-forward est sur son chemin critique).
- **[CPU2 — basse]** `chipWait16` (Mega STE 16 MHz) n'a pas reçu la correction de
  phase mi-accès `(c − 2)` appliquée à `chipWait8` le 2026-07-02 — skew structurel
  probable de l'alignement slot RAM en 16 MHz. À trancher par A/B contre l'oracle
  Hatari, pas à corriger à l'aveugle (mode peu validé).
- **[CPU3 — basse]** `iplDeferred` (NEOST_IPLFETCH=1, opt-in expérimental) n'est pas
  sérialisé : un échantillon IPL différé armé au save est perdu au load (IRQ reconnue
  une frontière trop tard). Sérialiser = bump de version .state — différé tant que le
  mode reste expérimental.

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
- **SCC RR9** (SC2) : NeoST rend l'image de RR13 (datasheet) ; Hatari rend WR9 (son commentaire
  dit l'inverse de son code).
- **SCU $FF8E07** (SC3) : NeoST renvoie le VME Interrupter ; Hatari renvoie le Sys Interrupter
  (copier-coller `scu_vme.c:282-286`).
- **GPIP bits 3 (blitter) et 6 (RI)** : NeoST les recalcule depuis l'état vivant des lignes
  (`Mfp.cpp:570-583`), Hatari les traite en VERROU que `MFP_Reset` met à 0 et que rien ne
  relève ensuite (bit 6 n'a **aucun** appelant dans tout son arbre ; bit 3 attend le premier
  `Blitter_Start`). Sur vrai matériel ces entrées sont tirées haut au repos → NeoST est plus
  proche du matériel. ⚠ **Conséquence pour les chasses différentielles** : `$FFFA01` au repos
  vaut **$F9** chez NeoST contre **$B1** chez Hatari — un diff de trace sur ce registre est
  ATTENDU et n'est pas une régression.

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
- **Vidéo** : filtre « écriture redondante » absent (freq/res rejouées même inchangées) ;
  ~~`$FF8260` bits 2-7 non forcés à 1 sur ST~~ ✅ **corrigé (2026-07-09)** + ~~alias shifter `$FF8261`
  non géré~~ ✅ **corrigé** (lecture) : `$FF8260`/`$FF8261` = port de `Video_Res_ReadByte`
  (video.c:5281) — STF/Mega ST forcent bits 2-7 à 1, STE à 0 ; `$8261` renvoyait 0xFF (void) → mode
  (`Shifter.cpp` read8). L'écriture `$8261` (Shifter-res seul, sans GLUE) reste NON portée (touche le
  chemin V2 res-switch byte-exact, sans étalon) ; attribution ligne à longueur FIXE (`fc/cpl`) vs
  accumulée ; chemin `PrevSize` partiel ; quirks démos `$FF8205/07/09` (E605/Tekila).
- **FDC** : `dmaResetFifo` ne remet pas `bufPos_`/`dmaBytesToTransfer_` ; recalcul densité superflu
  à chaque lecture statut ; borne parseur STX (chemin secteurs complet) ; pas d'`indexCheckUpdate`
  avant la commande.
- **Son** : compteur bruit testé `>=` dans la garde 125 kHz (vs 250) ; pas de HPF sous-sonique sur
  le canal DMA ; ~~`mode_` non masqué `&0x8f` (relecture)~~ **déjà fait** (masqué à l'écriture,
  `DmaSound.cpp:495` → relecture masquée) ; masque adresse DMA `$3fffff` non appliqué (écriture octet
  haut, dépendant de la RAM ≤4 Mo — `DMA_MaskAddressHigh`).
- **Blitter** : **BL-R** read8 ne rejette pas l'accès octet aux registres mot (rend la valeur vive
  vs IoMem rance) ; **BL-MST** `$FF8A3E/3F` dé-fauté à tort sur Mega ST (void seulement sur STE — carte
  de bus-faults, non touchée).
- **Bus** : wait-state 4 cyc des registres FDC/DMA non facturé ; `$FF860E/0F` densité routé sur STE
  simple ; ~~`$FF8A3E/3F`→0x00 au lieu de 0xFF~~ ✅ **corrigé (2026-07-09)** : `Blitter::read8`/`write8`
  traitent `$3E/3F` en void (lit 0xFF, écritures ignorées ; ioMemTabSTE.c:199) ; trou MMU STF
  bank0=128K/bank1=2048K non émulé.

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
- **GEMDOS — `Dsetpath` retire un masque de fichier final** *(DIVERGENCE VOLONTAIRE, 2026-07-08)* :
  le bureau TOS ouvre un dossier via `Dsetpath("C:\DOSSIER\*.*")` (chemin + masque). Hatari
  laisse `access("…/DOSSIER/*.*")` échouer → EPTHNF → alerte « Impossible de définir le dossier
  par défaut ». NeoST retire un dernier composant contenant `*`/`?` avant le test (Dsetpath ne
  définit qu'un RÉPERTOIRE ; vrai TOS ignore la partie fichier). Débloque entrer/écrire/exécuter
  dans les dossiers au bureau. Dossier inexistant reste -34. (`GemdosHd.cpp` gemChDir)
- **GEMDOS — `Fsfirst`/`Fsnext` n'énumèrent JAMAIS `.`/`..`** *(DIVERGENCE VOLONTAIRE, 2026-07-08)* :
  `subdir` forcé à `false` dans `gemSFirst` → les points Unix `.`/`..` n'apparaissent plus dans
  aucun listing TOS (Hatari les expose en sous-répertoire, comme un vrai FAT). Choix produit :
  bureau plus propre. Contrepartie : plus d'icône parent « +. » dans une fenêtre dossier ; la
  navigation parent d'un CHEMIN (`..\FOO`) reste gérée par `createHostFileName`.
  (`GemdosHd.cpp:135-149`/gemSFirst vs `gemdos.c:433-484`)
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

---

## 🔬 5ᵉ passe — rafraîchissement complet des statuts (2026-07-07, 4 agents)

**Contexte.** Passe déclenchée après l'audit blitter du même jour : (1) mettre tous les statuts
en phase avec le code réel (le commit `bc15a67` du 2026-07-03, au titre trompeur « fix(cpu):
improve STOP handling », contient EN RÉALITÉ l'intégralité du bug hunt 39-findings : 23 fichiers,
+750/−193 — `seedResetVectors`, `gpipSetLine`, Timer B evt=0, `Fdc::reset(bool)`, `Shifter::reset`,
`Ikbd::bootRom`, `$FFFC01/03` void, `peek8/16`, `busFaultN` par banque, DmaSound wrap 2²⁴,
etc.) ; (2) préparer les deux chantiers suivants : **lignes raster transitoires de Super Hang-On**
(in-game) et **son YM « différent » en STE** (Rick Dangerous). 4 agents : vidéo/raster (priorité),
MFP, périphériques (FDC/son-statuts/bus/SCC/ACIA), son approfondi (cœur YM + chemin de rendu).

### Statuts corrigés par cette passe (le détail ↑ dans les sections historiques est PÉRIMÉ)

**Passés à ✅ corrigé (tous dans `bc15a67` sauf mention) :**
- **M1 fronts GPIP** — `gpipSetLine`/`gpipUpdateInterrupt` (`Mfp.hpp:303-308`, `Mfp.cpp:493-504`)
  = port exact de `MFP_GPIP_Set_Line_Input`/`_Update_Interrupt` (mfp.c:1143-1219) : DDR=entrée,
  GPIP^AER, front actif si `GPIP_new == AER`. Appelants convertis : Ikbd.cpp:965, MidiAcia.cpp:88
  (wire-OR ACIA ≙ `MFP_Main_Compute_GPIP_LINE_ACIA`), Fdc.cpp:691, Blitter.cpp:203/308.
  L'écriture AER re-déclenche les fronts (Mfp.cpp:115-116 ≙ mfp.c:2787-2790).
- **Timer B event-count** — wrap uint8 0→255, garde `tbCounter_==0` supprimée (`Mfp.cpp:415-432`
  = `MFP_TimerB_EventCount` mfp.c:1297-1320), antidatage `firingDue()`.
- **FDC INTRQ** — plus de `raise(SRC_FDC)` doublé : `setFdcLine` seul (Fdc.cpp:684-692).
- **$FF8264 STE** — relit la valeur brute écrite (`Shifter.cpp:1632/1933` ≙
  `Video_HorScroll_Read_8264`).
- **VoidRead 0x00 vs 0xFF** — l'entrée était mal cadrée : Hatari n'utilise `VoidRead_00` QUE sur
  `$FF820B/$FF8262-63/$FF8266-7F` STE (ioMemTabSTE.c:98,121,124) ; NeoST rend 0x00 exactement là
  (`Shifter.cpp:1644-1647`), 0xFF ailleurs — couverture exacte (commit 88ec84a).
- **read32/zone RAM void** — mécanisme `cpuDb` (`Bus.cpp:215-216`) ≙ `VoidMem_wget/lget` (la zone
  vide rend la dernière valeur du bus), latch `Cpu68k.cpp:261-262`.
- **Trou MMU STF bank 128K/2048K** — ⚠ l'entrée précédente (« commit 6df9432 ») était
  FAUSSE : ce hash n'existe pas et aucune révision de `Bus.cpp` n'a jamais porté le trou —
  seuls `mmuXlatSTF/STE` + `ConfToBank` l'étaient. RÉELLEMENT porté le 2026-08-13 (3ᵉ
  passe de bug hunt) : `mmuTranslate` rend void `$40000-$7FFFF` quand bank0 = 128 Ko et
  bank1 = 2 Mo (≙ `memory_map_Standard_RAM`, cpu/memory.c — quirk mesuré sur STF).
- **Son : bruit ≥/250 kHz** (incrément 125 kHz, comparaison 250 kHz, plancher per=1 retiré —
  `YM2149.cpp:184-193` = sound.c:1050-1058) et **`mode_ &= 0x8F`** (`DmaSound.cpp:434`).
- **Filtre « écriture redondante » freq/res** — PRÉSENT (`recordSyncWrite`, `Shifter.cpp:871-879`,
  persistant inter-trames ≙ `ShifterFrame.Freq/Res`) — l'entrée « absent » de la 2ᵉ passe est fausse.
- **V3 partiel : restart du compteur vidéo PORTÉ** (2026-07-02) — `restartVideoCounter`
  (`Shifter.cpp:602-605`, early-return `videoCounter`:1722) + événement `VC_RESTART`
  (`Machine.cpp:250-254`, ligne 310/260 cycle 56 STF / 60 STE, check freq live) ≙
  `Video_RestartVideoCounter` + HBL intermédiaire video.c:3262-3286.

**Reclassés FAUX POSITIFS (Hatari fait pareil — NE PAS « corriger ») :**
- **D4 6268/6250** — le `FDC_TRACK_BYTES_STANDARD` actif d'Hatari vaut 6268 (formule 6250 commentée).
- **BL-MST** — `IoMem_FixVoidAccessForMegaST` (ioMem.c:172-196) dé-faute `$FF8A3E/3F` sur Mega ST
  AUSSI : la carte de fautes NeoST est identique. Ne reste que la VALEUR (0x00 vs 0xFF void) et le
  latch d'écriture (`Blitter.cpp:95-96` vs `IoMem_VoidWrite` qui jette).
- **Cartouche vide 0xFF** — Hatari `memset 0xFF` (cart.c:117) = NeoST.
- **MIDI bits SR (OVRN/DCD/CTS/FE/PE)** — midi.c:39-41 ne les modélise pas non plus : parité exacte
  avec l'oracle.

### Divergences OUVERTES à jour (par sous-système, sévérité décroissante)

**Vidéo/Glue** — cœur STF = très élevée (glue-selftest 19/0, datations read −6 / write +2 / align
−25 actives par défaut et validées Cuddly 250/250, EL 40/40, spec512 10/10) :
- **[moyenne, systémique] Hybride WS1/WS3 ✅ TRANCHÉ : WS3 complet (2026-07-08)** — NeoST adopte
  le wakestate par défaut de l'oracle (`VIDEO_TIMING_DEFAULT = WS3`, video.c:624), qui partage
  avec le STE l'IRQ HBL à cpl. Implémentation (cf. `glue::` en tête de Shifter.cpp) : positions
  horizontales Glue **+1** (`kWsInc`, fenêtres de tricks + DE stockés + Remove*_Pos 503 + HSync
  −49/−9 + canal Hbl_Pos 512/508/224), **IRQ HBL à cpl** (`kHblOff` défaut 0), VBL 64 (déjà bon).
  Séparation clef découverte dans le code Hatari : `Video_CalculateAddress` (compteur $FF8205+),
  `spec512.c` et la copie écran utilisent les constantes **FIXES** `LINE_START/END_CYCLE_*`
  (video.h:91-95), HORS table wakestate — les ancres de rendu NeoST (56/376) restent donc fixes,
  les DE stockés (table WS) sont re-normalisés −inc au rendu, et les datations read −6 / write +2 /
  spec512 −25 sont **inchangées** (fidèles-théoriques, WS-indépendantes — aucune recalibration
  nécessaire). Idem Timer B par défaut (`Video_TimerB_GetDefaultPos` = constantes fixes → 400).
  `NEOST_WS=1..4` pour A/B (WS1 remet HBL cpl−4 + VBL 60). **Validé** : glue-selftest 19/19
  (stimulus LEFT_OFF ajusté @8 — l'ancien @6, 4 cyc d'écart impossible sur HW, retombait
  fidèlement dans la fenêtre de restauration WS3), étalons TOUS OK, boot STF 50 Hz tos102fr
  **0 px** vs oracle, menu Cuddly **pixel-identique à la baseline HEAD** (190/250 trames à 0 px
  vs oracle sur les mêmes trames — les 60 restantes = phase d'animation des drapeaux due à la
  latence de la touche fifo côté oracle, identique baseline), flicker spec512 inchangé (résidu
  838→839 préexistant, invariant WS), son STE : cloche bit-identique, fetch FIFO déplacé de
  +4 cyc avec le HBL (0,003 % de l'étalon DMA — voulu). Le terrain est prêt pour V1/V2 (les
  valeurs de la branche STE d'Hatari se porteront telles quelles).
- **[moyenne] V1 branche STE Glue ✅ PORTÉ (2026-07-08)** — table STE dans `glue::Timing`
  (Preload_Start 0/36/40 : le GST MCU teste les positions de PRELOAD du MMU ; Line_Set_Pal 56,
  HSync −52/−12, Remove 500, HBL à cpl, inc=0 — pas de wakestate) + phase 1 STE de
  `updateGlueState` (video.c:2444-2651) : fenêtres preload, restauration LEFT_OFF stricte `<` 4
  avec variante **LEFT_OFF_2_STE** pile à 4 (+20 octets, DE_start 16, écran −8 px,
  `BORDERBYTES_LEFT_2_STE`), ligne NO_DE freq dans (36,40], latch res **sans** le −1 STF
  (video.c:2224 « not the case for the STE GST MCU »). Phases 2/3 communes (elles lisent la
  table machine). **Validé** : selftest 22/22 sur STE (3 cas LEFT_OFF_2_STE ajoutés), étalons
  TOUS OK, chemin ST strictement inchangé (Cuddly-ST 0 px vs oracle) ; le menu Cuddly sur
  NeoST-STE **casse désormais comme le vrai STE** — couleurs faussées/hachures/scroller
  corrompu, **196/250 trames pixel-identiques à l'oracle Hatari-STE** (Δ=−10, zone mur+robot ;
  le reste = glissements de phase du clignotement bistable de la casse, sensible au cycle exact
  de la touche fifo — la « contrepartie » documentée du 2026-07-03 est résorbée : NeoST-STE
  n'est plus « trop propre »).
  **[moyenne] V2 tricks résolution ✅ PORTÉ (cœur, 2026-07-08)** — `updateGlueRes`
  (Shifter.cpp) = port de `Video_WriteToGlueRes` (video.c:1637-1753) : **overscan MED-RES**
  (LEFT_OFF + med@20/28/36 → `OVERSCAN_MED_RES` + champ décalage source bits 20-23 ; hi→med ≤20
  → `LEFT_OFF_MED` +26 o ; variante STE `LEFT_OFF_2_STE_MED` +20 o/−16 px), **stab med**
  (hi/med/lo@16 → retrait gauche low propre) et **scrolls « hardware » droite 13/9/5/1 px**
  (les deux familles de fenêtres, lo ≤32 après hi/med et lo ≤40 après hi/lo/med). Rendu
  **multi-résolution PAR LIGNE** (port `Video_StoreResolution`) : lignes med décodées en
  2 plans (2 px/cyc) au sein d'une trame basse rés, base de décodage décalée de (2−champ)
  OCTETS (⚠ pas en pixels : stride 186 ≢ 0 mod 4 → l'appariement des mots de plans dépend de
  l'origine octet), émission = MOYENNE des 2 px med par colonne (même réduction que l'oracle
  2×, les DEUX phases vérifiées), calage émission −4 px med (1er mot med 2 cyc après DE_start).
  **VALIDÉ : écran greetings No Cooper 0 px vs oracle Hatari** (étalon `nocooper_greetings`,
  RÉFÉRENCÉ SUR L'ORACLE, max_diff 0 — chaque ligne : hi@0/lo@12/med@20 + 60/50@376/384 + stab
  hi/lo@444/456) ; selftest +4 scénarios V2 (31 ST / 34 STE) ; étalons TOUS OK, spectrum/Cuddly
  inchangés. L'écran principal (`nocooper`) : nappe/logo/bordures déjà 0 px, texte scrollé 2 px/
  trame à phase de touche près — l'« écart 891 px » d'hier était un artefact de phase (mesuré :
  0 px à sa phase). **Restent hors périmètre, documentés** : hardscroll 4 px plein écran de
  Paulo Simoes (med@84 → lo@92-104, ajuste le pointeur vidéo par ligne — étalon requis), hacks
  « TEMP » Closure/DOLB (reniflage PC/opcode chez Hatari — non portables tels quels), alias
  $FF8261, rendu med 640 natif en trame mixte (buffer 416 : réduction ×2 documentée).
- **[basse→moyenne] Rendu par-ligne daté à la frontière d'instruction** : `renderLine(y)` part
  APRÈS l'instruction qui enjambe DE_end (376) — une écriture palette dans `[376, 376+carry)` est
  vue par la ligne y (Hatari : y+1). Intermittent par nature (cf. candidats SHO).
- **[basse] Seuil spec512 `kSpec512Threshold=512`** : bascule de CHEMIN de rendu par trame
  (par-ligne ↔ palette roulante + relecture RAM) — cliff propre à NeoST, Hatari n'en a pas.
- **[basse] `lineSnap_` seulement sur trames à écritures freq/res** — une trame sans trick mais
  en course avec le faisceau est relue en fin de trame (Hatari copie TOUJOURS par-ligne).
- **[basses]** $FF8260 lu sans `|0xFC` sur ST (video.c:5281-5298) ; quirks E605/Tekila
  $FF8205/07/09 pendant DE (+6 movep / +2 wrap, video.c:5222-5241) ; signaux VBlank/VSync non
  modélisés (video.c:3443-3487) ; attribution replayGlue à longueur fixe hors `NEOST_LINELEN`
  (canal complet existant mais OFF) ; CyclesPerVBL±4 si la dernière ligne change de freq ;
  STOP réveil granularité 2 cyc (Moira) vs quantum 4 (Hatari) → phase E-clock d'IACK mod 4.

**MFP** — chaîne IRQ/timers/GPIP quasi 1:1 (NeoST fait même le spurious $60 que Hatari laisse en
TODO) :
- **[moyenne-basse] Pas de `MFP_UpdateTimers` avant lecture IPR/ISR/TBDR en mode bloc** (défaut) :
  un timer expirant PENDANT l'instruction qui polle est vu ≤ 1 instruction en retard. Compensé
  pour les data-registers en mode délai (`Mfp.cpp:337-341`) ; fermé par `NEOST_SYNC_DISPATCH=1`
  (réfuté par ailleurs). L'IRQ elle-même n'est PAS affectée (antidatage + commit frontière).
- **[moyenne-basse] ✅ corrigé (2026-08-14)** — conversion MFP→CPU et grille périodique
  conservées en unités ×256 comme `cycInt` : Timer C 200 Hz = 40106,238 cycles sans
  perdre la fraction à chaque recharge. Échéances `timerDueSub_` incluses au save-state v11.
- **[basses]** AER bit3 écrit mid-ligne : tic Timer B déjà armé non repositionné (mfp.c:2772-2815,
  cas Seven Gates of Jambala) ; `setBusyLine` GPIP0 Centronics sans détection de front (fix
  trivial : `gpipSetLine(busyLine_, a)`) ; `setXsintLine` sans test DDR bit7 ; RS232 CTS/DCD/RI
  `raise()` direct (volontaire, fixture) ; élection chronologique par lot (`MFP_UpdateNeeded`)
  non groupée — rarissime. **[volontaires]** jitter Lethal Xcess (hack PC codé en dur, résolu
  par le vrai beam-sync) et « PATCH TIMER D » (hack de performance inutile ici) non portés.

**Son** — générateurs YM = port exact (tons/bruit LFSR 17 bits/enveloppes/mixeur/masques/
read-latch/PWM/LPF C10/HPF/resampler 16.16 : tous vérifiés 1:1 contre sound.c/psg.c) :
- **[moyenne, audible] S3 gain LMC ×2 manquant** : Hatari compense la table STE demi-amplitude
  par ×2 dans `left/right_gain` (dmaSnd.c:1152-1153,1460-1461) ; NeoST `outScale_=0.5`
  (`Machine.cpp:194`) × gains plafonnés 1.0 (`DmaSound.cpp:303-312`) → **YM STE −6 dB vs
  Hatari-STE et vs NeoST-ST**, ratio YM:DMA décalé. Candidat n°1 « Rick Dangerous ».
- **[moyenne] S4 (NOUVEAU) table DAC** : NeoST n'a QUE le modèle circuit
  (`YM2149.cpp:72-95` ≙ `YM2149_BuildModelVolumeTable`, utilisé par Hatari seulement avec
  `--ym-mixing model`) ; le DÉFAUT Hatari est `YM_TABLE_MIXING` (configuration.c:807) — mesures
  Paulo Simoes 16³ (`ym2149_fixed_vol.h`) interpolées 32³ (`interpolate_volumetable`,
  sound.c:505-543). Timbre/balance différents sur accords 2-3 voies, ST ET STE. Test 30 s :
  oracle `hatari --ym-mixing model` — s'il se met à sonner comme NeoST, c'est ça. Fix :
  vendoriser la table 16³ + l'interpolation.
- **[moyenne] S2 FIFO 8 octets + avance HBL DMA ✅ corrigé (2026-07-07 soir)** — port complet du
  modèle Hatari : FIFO anneau 8 octets fetchée par MOTS (`fifoRefill` ≙ dmaSnd.c:342), entretenue
  à chaque HBL (`DmaSound::onHbl` appelé de `Machine::onHbl` ≙ `DmaSnd_STE_HBL_Update`
  video.c:3322), consommation DAC datée au reste fractionnaire (`updateDac` ≙ Sound_Update +
  frameCounter_float) qui **capture les octets au fetch** dans un anneau fixe consommé par le
  rendu audio (mixSegment/mix ne relisent PLUS la RAM en fin de trame — un programme qui modifie
  le tampon pendant la lecture est maintenant fidèle : cas Mental Hangover / Power Up Plus). Fin
  de trame détectée **au fetch** (XSINT/Timer A en avance ≤ 8 octets, quantifiée HBL, ≙ vrai HW) ;
  `Scheduler::DMASND` n'est plus armé. Compteur $FF8909+ = adresse de FETCH après synchronisation
  (≙ `DmaSnd_GetFrameCount`). Réalignement mono→stéréo (`fifoSetStereo` ≙ dmaSnd.c:419) + événement
  MODE daté pour la cadence du rendu. Piège débusqué au passage : la dette de rééchantillonnage du
  rendu hôte doit être PLAFONNÉE à 1 octet en sous-alimentation, sinon le rattrapage saute des
  octets en rafale (l'étalon montrait 21 % d'octets B au lieu de 33 %). **Validation** : étalon
  dédié `tools/make_dmasnd_test.py` (tampon modifié mid-trame par le handler VBL, fenêtre B
  transitoire jamais visible à la frontière de trame) → NeoST capture 33,3 % de mots B, oracle
  Hatari (bâti DANS ce conteneur, SDL2 2.30 présent — AVI + trace `--trace dmasound`) 33,2 % ;
  `NEOST_DMASND_TRACE=1` émet le refill au format Hatari pour diff direct. Étalons 19/0 + 8 OK,
  WAV cloche GEM / Rick Dangerous II bit-identiques (YM intact).
- **[basses]** ~~Horloge YM figée 250 000 Hz~~ **✅ 10ᵉ passe : 250 663 Hz** (= MCLK/2/2/4/8,
  MÊME MCLK ST et STE chez Hatari — le « 250 332 STE » de cette liste était erroné) ;
  écritures YM rejouées au grain ~48 kHz vs frontière 250 kHz (`Sound_Update` avant
  `Sound_WriteReg`, psg.c:346) — jitter ≤ 21 µs (sync-buzzer) ; ~~HPF appliqué au YM seul vs
  au MIX YM+DMA en STE~~ **✅ 10ᵉ passe : HPF déplacé sur le mix** (`applyHpfStereo/Mono`) ;
  `mixing≠1`+DMA arrêté ne mute pas le YM (volontaire, `DmaSound.hpp:53-54`) ;
  ~~signe DMA non inversé~~ **✅ 10ᵉ passe : `kDmaGain=−0.375`** ; garde
  `nAudioFrequency≥40000` du LPF STF non portée (sans effet à 48 kHz).

**FDC/DMA** — [moyennes] D3 flush FIFO sans stall 32 cyc (`Fdc.cpp:714-749` vs fdc.c:1340,1396) ;
wait-state 4 cyc des registres FDC/DMA non facturé (`Bus.cpp:579-580` vs `M68000_WaitState(4)`
fdc.c:4688+ — ⚠ asymétrie Hatari : rien sur la lecture $8606 ni $8609/0B/0D). [basses]
drive/side « pull » vs « push » (psg.c:420 → `FDC_SetDriveSide` immédiat) ; `dmaResetFifo` du
toggle bit 8 sans remise de `bufPos_`/`dmaBytesToTransfer_` (le reset machine, lui, est couvert
par `Fdc::reset` depuis bc15a67) ; `indexCheckUpdate` partiel (présent boucle événements +
statut, absent executeCommand/TR/SR/DR vs `FDC_UpdateAll` fdc.c:4724). [assumés] D1/D2 STX.

**Bus** — [basses] $FF860E/0F routé FDC sur STE simple (void chez Hatari, effet nul :
`canHandleDensity` gate) ; $FF8A3E/3F rend 0x00 latché au lieu de void 0xFF (+ écritures
latchées/relisibles vs jetées).

**Blitter** — à jour au 2026-07-07 (audit + port mid-word + fenêtre CPU 64 accès réels, § dédié
plus haut). Restent [basses] : exécution CPU parallèle pendant la tranche, BL-R (lecture octet
des registres mot rend la valeur vive), cache MegaSTE 16 MHz compté comme accès bus, RMW sans
rétention de prise de bus.

**ACIA/IKBD/MIDI** — [basses, inchangés] délais de réponse IKBD absents (`IKBD_Delay_Random` —
un seul chantier couvre readClock ET Froggies $83) ; autosend « chaque VBL » approx des deux
côtés ; taux $17 ignoré (assumé) ; DoubleClicks/joy-espace = conforts Hatari non matériels.
[NOUVEAU, basse] MIDI : TDRE ne tombe jamais sans TIE (`MidiAcia.cpp:65-68` vs midi.c:271,188 —
un poll du SR MIDI voit un émetteur infiniment rapide).

**SCC** — inchangé depuis 2026-06-15 (SC1/SC1b tranchés).

### Chantier « lignes transitoires Super Hang-On » — candidats classés (agent vidéo + MFP)

> **✅ RÉSOLU (10ᵉ passe, 2026-08-06)** : aucun des candidats ci-dessous — la cause était
> l'**IACK MFP vectorisé 4 cyc trop court** (12 → 16, mesuré à l'oracle instrumenté).
> Cf. § 10ᵉ passe. La section est conservée pour l'historique de la méthode.

Contexte : per-HBL, handler « Timer B → table → `stop #$2100` → HBL écrit couleurs 2+3 » ;
bande d'horizon fixe déjà corrigée (STOP niveau-sensible). Les lignes brèves à position
aléatoire demandent soit un changement de chemin de rendu, soit une écriture attribuée à la
mauvaise ligne, soit un handler très en retard :
1. **Bascule du seuil spec512 trame à trame** (`kSpec512Threshold=512` ; SHO ≈ 420 accès
   palette/trame, juste sous le seuil) → le rendu ALTERNE par-ligne ↔ spec512 (palette roulante
   + relecture RAM). *Test : logguer `paletteAccesses_`/`spec512Active_` par trame pendant
   l'artefact ; forcer le seuil très haut et voir si ça disparaît.*
2. **Écriture palette dans `[376, 376+carry)`** (handler retardé par préemption Timer C/IKBD) →
   ligne y chez NeoST, y+1 chez Hatari. *Test : `NEOST_PAL_TRACE`, chercher cyc ∈ [340..512],
   corréler avec la position de l'artefact ; oracle `--trace video_color`.*
3. **HBL coalescé** (handler+préemption > 1 ligne → `g_hblPending` absorbe un HBL → le dégradé
   glisse d'une ligne jusqu'à la resynchro Timer B). Hatari a le même latch mais le LOGGE
   (video.c:3292-3305). *Test : compteur « pending déjà levé » dans raiseHbl vs log Hatari.*
4. **Amplificateurs** : phase de réveil STOP mod 2 vs mod 4 (E-clock IACK mod 10) ; HBL à 508
   (WS1) vs oracle 512 (WS3) — *A/B `NEOST_HBL_OFF=0` sur la repro in-game* ; dérive de
   troncature MFP→CPU (si un timer en mode délai est impliqué).
5. *(écartés)* : Timer B position/fenêtre (convergents), V2/LINELEN (SHO ne change pas de freq
   in-game — vérifier `syncWrites_` vide), MFP UpdateTimers (l'IRQ n'est pas affectée).

Oracle : repro in-game headless (DESKTOP.INF #Z + `--keys-at`/`--mouse-at`, cf. mémoire),
`NEOST_PAL_TRACE` par trame, `hatari --trace video_color,video_hbl` fastfdc des deux côtés,
diff des (ligne, cyc) des écritures couleurs 2/3 sur ~200 trames.

### Chantier « Rick Dangerous sonne différent en STE » — candidats classés (agent son)

**✅ RÉSOLU (2026-07-07 soir)** — les deux candidats principaux sont CORRIGÉS :
1. **S3** : `kLmcMakeup=2.0` (port du ×2 de dmaSnd.c:1152) + `kDmaGain` 0.7→0.375 — le YM STE
   ressort à pleine amplitude comme le ST (cloche GEM : ratio RMS STE/ST = **1.000** mesuré) et
   le DMA à ¾ exactement comme Hatari. 2. **S4** : table DAC mesurée par défaut
   (`src/core/ym2149_fixed_vol.h` vendorisé, port exact d'`interpolate_volumetable`), l'ancien
   modèle reste sous `NEOST_YM_MIXING=model` pour A/B. Outillage créé : **`--sound-dump F.wav`**
   headless (48 kHz stéréo s16, chaîne GUI complète YM+DMA+LMC) — validé keyclick/cloche GEM +
   musique Rick Dangerous II STE (profil RMS par seconde, pas d'écrêtage). Reste à comparer À
   L'OREILLE contre le vrai Hatari (machine STE des deux côtés !) — les [basses] (pitch −2,3
   cents, jitter 21 µs, HPF mix) sont documentées si un écart subsiste.

### État des mécanismes CPU « gated » (Cpu68k.cpp, référence rapide)

ON par défaut : `NEOST_LINELEN` (par-ligne, 2026-07-08), `NEOST_IACK` + `NEOST_IACK_AT` (E-clock + bloc au point d'IACK réel ≙
iack_cycle), **`NEOST_IACK_SYNC`** (dispatch des événements échus AU point d'IACK ≙ CycInt_Process,
2026-08-07 — rebase de quantum inclus), `NEOST_IACK_MFP=16` (bloc IACK MFP mesuré à l'oracle),
`NEOST_RAM_SLOT` (créneau bus RAM 4 cyc ≙ wait_cpu_cycle_*), `NEOST_RAISE_COMMIT=3`
(commit IPL au dispatch HBL+VBL), dispatch BLOC. OFF par défaut : `NEOST_RAISE_WINDOW`
(différé ipl_fetch à la frontière — mesuré : Hatari CE committe sans différé), `NEOST_ECLOCK_ON` (legacy),
`NEOST_IPLDELAY` (crude), `NEOST_IPLFETCH` (fidèle ipl_fetch_next dans Moira — candidat de
refonte non validé in-game), `NEOST_PIN_ARM` (réfuté), `NEOST_VC_WAIT` (redondant avec
RAM_SLOT), `NEOST_LINELEN=0` (désactive le canal par-ligne, **ON par défaut** depuis 2026-07-08),
`NEOST_V2` (squelette), `NEOST_SYNC_DISPATCH` (réfuté).

**Bilan 5ᵉ passe** : sur 26 entrées re-vérifiées — 9 corrigées (5 par bc15a67), 4 faux positifs,
11 ouvertes (majorité basses), 2 assumées. Fidélité « très élevée » partout ; les deux fronts
utiles sont désormais **ciblés et testables** : raster SHO (candidats 1-3 + traces) et son STE
(S3/S4 + oracle WAV).

## 6ᵉ passe — bug hunt pré-release (2026-07-12, agents parallèles)

Audit ciblé des composants MODIFIÉS depuis la 5ᵉ passe (Ikbd, Mfp, DmaSound, ACSI,
MidiAcia, Fpu, Shifter) contre `extern/hatari/src`. **Corrigés dans la foulée** :

- **ACSI statut DMA inversé dans `writeAcsi`** (jumeau du bug d'`acsiDmaTransfer` corrigé
  au hunt précédent) : bit0 de $FF8606 rapportait « erreur » après chaque octet de
  commande accepté, y compris juste après un transfert réussi → `Fdc.cpp` (H1).
- **IKBD $0D** : le feu du joystick 1 compte désormais comme bouton droit quand la souris
  est active (même ligne physique, cf. `IKBD_DuplicateMouseFireButtons`) → `Ikbd.cpp` (M1).
- **FSCALE** : n extrait de l'étendu, exact jusqu'à ±131071 puis saturation −$6001/$E000
  (port exact de `floatx80_scale` ; l'ancien clamp ±32768 faussait la bande étroite
  |n| ∈ (32768, 131071]) → `Fpu.cpp` (M2).
- **±inf décodé S/D** : mantisse 0 (`floatx80_default_infinity_low`, forme canonique
  68881) au lieu de bit 63 posé → `Fpu.cpp` (M4).

**Ouvertes (basses, consignées sans correction)** :

| # | Où | Divergence |
|---|----|------------|
| M3 ✅ | `SoftFloatX80.hpp:170-177` | **CORRIGÉ 2026-07-31 (`7a9e03b`)** — `normalizeSubnormal` passé en convention 68881 (`−sc`, bit entier EXPLICITE) : c'est la branche `#ifdef SOFTFLOAT_68K` de `softfloat.c:1061-1065` qu'il fallait porter, pas celle du x87. Supprime le ×2 systématique sur FADD/FSUB/FMUL/FDIV/FSQRT/FREM/FMOD/FGETEXP/FGETMAN/FSCALE. Vérifié par fuzz différentiel contre `softfloat.c` d'Hatari : **0 écart** (plusieurs milliers avant). |
| B1 | `Ikbd.cpp` flush $80,$01 | avale l'octet « en vol » (Hatari : l'octet tiré vers TDR/TSR survit). |
| B2 | `Ikbd.cpp` bootRom | ne remet pas `mDeltaX_/Y_` (Hatari : Delta remis à 0). |
| B4 | `Ikbd.cpp` master reset TIE | Hatari force SR=TDRE immédiatement ; NeoST à l'échéance TX. |
| B6 | `Fdc.cpp` DMA ACSI hors plage | Hatari pose aussi `status=ERROR` en lecture ; accepte la ROM comme source en écriture. |
| B7 | `Acsi.cpp` `dmaWrite_` | remis à false à chaque commande ; Hatari le fait persister jusqu'au transfert (refus de sens contradictoire). |
| B8 | `Fdc.cpp`/`Acsi.cpp` | sans image ACSI, Hatari ignore l'octet ($FF8604=0) ; NeoST pose status=2 (probe TOS identique). |
| B9 ✅ | `MidiAcia.hpp:44-58` | **CORRIGÉ 2026-07-31 (`e499eba`)** — `MidiAcia::reset()` ajouté et appelé par les resets CHAUD et FROID de `Machine`, comme `reset.c:111` (`ACIA_Reset`) + `:124` (`Midi_Reset`). |
| B10 | `DmaSound.cpp` $FF8922 | octet compagnon : Hatari lit 0 (réécrit à chaque shift) ; NeoST combine avec le dernier mot latché. |
| B11 | `Fpu.cpp` SNaN double | NeoST livre SNAN (plus fidèle au 68881 réel) ; Hatari lève OPERR à la conversion — assumé façon SC2. |
| B14 | `Shifter.cpp` | capture `lineSnap_`/comptage palette actifs en mono (Hatari : skip hi-res) — surcoût pur, rendu inchangé. |
| B15 | `Shifter.cpp` | pas de wrap 22 bits (`Video_GetAddrMask` $3FFFFF) à l'usage du compteur vidéo — cas pathologique. |

Divergences **assumées** confirmées cette passe : symlinks suivis dans un montage GEMDOS
(même modèle de confiance que Hatari) ; VME/FPU MegaSTE « not found » correct.

## 7ᵉ passe — bug hunt post-perf (2026-08-06, 4 agents parallèles)

Chasse déclenchée après le commit de performance `65b1bb9` (chemins chauds inlinés + caches
MMU/Scheduler) et le port `MSA_WriteDisk` (`0d404cc`). Quatre agents : commit perf
(Bus/Scheduler/Shifter), FDC/STX/MSA, CPU/MFP/reset/save-state, Ikbd/DmaSound/YM/Shifter-rendu.
**Corrigés dans la foulée** (tous validés : selftests glue 36 / spec512 15 / bus 12 / mfp 16 /
msa 51, boots ST/STE/MegaSTE pixel-identiques, save-state déterministe) :

- **[MOYENNE] `Bus::read16` chemin rapide ROM — OOB hôte** : `off + 1 < rom.size()` débordait
  à `addr == romBase-1` (`off = 0xFFFFFFFF` → `off+1 = 0`, `0 < rom.size()` vrai → `rom[0xFFFFFFFF]`,
  lecture hôte ~4 Go hors tampon). Atteignable par un programme invité : `GemdosHd::trap` lit
  `read16` à une adresse prise dans la USP. Garde `off < rom.size()` ajoutée avant l'addition.
- **[MOYENNE] `Shifter` save-state — OOB pile scroll STE** : `hwScrollCount`/`newHwScrollCount_`
  restaurés sans borne (les 2 seuls champs oubliés par la campagne de durcissement). Compteur
  4 bits servant d'offset `idx[c + scroll]` dans `renderLine` (tampon 660 o) → un `.state` forgé
  (CRC valide) à `hwScrollCount=255` lit ~234 o hors pile. Gardes `ar.check` ajoutées.
- **[MOYENNE-BASSE] `Fdc::encodeMsa` — `.msa` 87 pistes non persistée** : borne `tracks > 86`
  asymétrique de `decodeMsa` (piste de fin ≤ 86, 0-based → 87 pistes). Une `.msa` de 87 pistes se
  montait inscriptible mais aucune sauvegarde n'était persistée (perdue au remontage). Borne
  rendue symétrique (`> 87`) + cas 87 pistes ajouté au `msa-selftest`.
- **[BASSE] `DmaSound::liveCounter`** : `fifoRefill()` de trop (≙ `DmaSnd_GetFrameCount` n'appelle
  que `Sound_Update`) → poll serré du compteur $FF8909 voyait le fetch avancer en continu au lieu
  de sauts HBL. **[BASSE] `DmaSound::startNewFrame`** : `phase_`/`haveCur_` remis à chaque relatch
  repeat au lieu du seul front PLAY 0→1 (≙ `DmaInitSample`) → click/dérive mono. **[BASSE]
  `Shifter::videoCounter`** : extrapolation créditant `line-offset+prefetch` aux lignes NO_DE,
  incohérente avec le commit `endVideoLine` (`bpl>0`) → compteur reculant entre 2 lectures $8205.
- **[BASSE] Écritures secteur `.st`/`.dim`** : `fstream::write` jamais vérifié (disque plein →
  secteur déchiré silencieux). Avertissement ajouté (le `.msa` est déjà atomique).
- **[BASSE, latents durcis] `Bus` cache MMU** : `machine` ajouté à la clé de revalidation (le
  décodage banque 1 en dépend). **`Scheduler`** : `schedule(kInactive)` traité comme cancel (ne
  corrompt plus `nextDue_`) ; `nextDue_` recalculé au chargement de save-state (comme `armed_`).

**Vérifiés FIDÈLES sans régression** : `MFP_Reset_All` (d410048), instruction RESET (7af3a7f),
STOP/liveNow + double-faute de bus (f755c76/6d45a5f), chemins DMA FDC/ACSI, parseur STX (OOB
« latent » fermé, cf. ligne ~180), Ikbd (table commandes + souris + handlers 6301 = port 1:1),
YM2149 (générateurs + table DAC mesurée), et les chemins rapides read8/write8/busFaultN du commit
perf (whitelist bus-error préservée, `write8` rapide ≡ lent sur le préfixe identité).

## 8ᵉ passe — workflow (6 chasseurs + vérif adversariale, 2026-08-06)

Orchestration multi-agents : 6 agents chasseurs (GEMDOS/sécurité, Bus/MMIO, Blitter, FPU, série
ACIA/SCC, CPU/save-state) → dédup → **réfutation adversariale** de chaque finding par un second
agent. 7 findings uniques, 5 CONFIRMÉS (2 faux positifs écartés : Bus/MMIO et un doublon).
**Corrigés** (mêmes validations que la 7ᵉ passe + montage GEMDOS symlinké/direct) :

- **[MOYENNE] GEMDOS montage symlinké** — `hdEmuDir` restait LEXICAL (`makeAbsoluteName`) alors
  que tout `host` produit par `clampToSandbox` est `physicalCanon()` (realpath, liens résolus).
  Sur un montage traversant un lien symbolique (macOS `/tmp→/private/tmp`, `~`/`​/var` sous Linux)
  les deux espaces de noms divergeaient : `gemChDir` (`:861`) rejetait TOUT `Dsetpath` en EPTHNF ;
  `gemSFirst("C:\")` (`:1221`) calculait `rootLen` sur la longueur brute → scan du dossier PARENT
  du bac à sable. Fix racine : `physicalCanon(hdEmuDir)` une fois au montage (`initDrives`, dossier
  confirmé existant) → toutes les comparaisons `host↔hdEmuDir` partagent l'espace canonique.
  Idempotent ; montage normal inchangé.
- **[BASSE, save-state — trous résiduels de durcissement] `Shifter::glueLineStart_`** : restauré
  sans check de taille, indexé sans garde de lecture dans `advanceGlueLive` (`Shifter.cpp:454/461/
  493`) → lecture hors-tas d'un `int64_t` (invariant `== glueLines_.size()` ajouté).
  **`Machine::curLineLen_`** : consommé par `advanceLine` (`lineCarry_ += cpl_ - curLineLen_`) →
  forgé, désancre `lineCarry_` après son propre check (borné `]0,4096]`). **`Cpu68k::g_cpuMul`** :
  multiplie l'horloge Moira (`addBusWaitCycles`) → forgé énorme = bond d'horloge (validé ∈ {1,2}).

**Vérifiés FIDÈLES (adversarial)** : Bus/MMIO whitelist bus-error, Blitter (données + arbitration),
FPU (NaN/exceptions/décodage), ACIA/SCC série.

## 9ᵉ passe — workflow zones peu couvertes (2026-08-06)

6 chasseurs sur les zones les moins auditées (balayage save-state exhaustif, Shifter tricks/
spec512, STX en angle image malformée, IKBD 6301, audio/WASM lock-free, config/E-S fichier) +
réfutation adversariale. 6 findings uniques, **2 CONFIRMÉS** (tous deux save-state), 4 faux
positifs écartés. **Corrigés** :

- **[BASSE] Enums IKBD `joyMode_`/`customRead_`/`customWrite_`** : sérialisés par `ar()` brut →
  un `.state` forgé les matérialise hors domaine → UB (comparaisons/switch de dispatch, UBSan
  enum). Même classe que `mouseMode_` déjà durci. `customWrite_` avait un `ar.check` mais qui
  lisait l'enum déjà hors-domaine. Fix : transit par `std::underlying_type_t` + validation avant
  cast (Ikbd.hpp). Format de save-state inchangé.
- **[BASSE] `Bus::cart` save-state** : `ar.vec(cart)` sans le plafond 128 Ko de `loadCart`.
  `read8Slow` décode la cartouche avant le MMIO (`Bus.cpp:369→373`) → un cart forgé > 128 Ko
  masque les registres `$FF8xxx`. Garde `ar.check(cart.size() <= CART_END-CART_BASE)` ajoutée.

**Vérifiés FIDÈLES (adversarial, 4 faux positifs)** : Shifter tricks/spec512 (rendu multi-rés,
palette roulante), parseur STX (chemins malformés — OOB « simple tronqué » toujours fermé),
audio lock-free/WASM (ring, points d'entrée JS), config/E-S fichier (parsing .cfg/ROM/disque).

**Bilan des 3 passes de la session (7ᵉ-9ᵉ)** : 17 correctifs — 3 moyens à impact réel (OOB hôte
`read16`, OOB pile save-state Shifter, franchissement/blocage GEMDOS symlink), 1 perte de données
(`.msa` 87 pistes), le reste en fidélité et durcissement save-state (9 champs/enums bornés au
total). Aucune valeur émulée ne change sur les chemins normaux ; selftests + boots pixel-identiques
à chaque étape. Le terrain save-state a été balayé exhaustivement ; les faux positifs en hausse
(4/6 à la 9ᵉ passe) signalent un rendement décroissant — la fidélité est très élevée.

## 10ᵉ passe — cycle-exact ciblée à l'oracle instrumenté : raster SHO + son STE (2026-08-06/07)

### Chantier « lignes transitoires Super Hang-On » — ✅ CAUSE TROUVÉE ET CORRIGÉE

**Cause racine : l'IACK MFP vectorisé était 4 cycles trop court** (`g_iackMfp` 12 → **16**,
`Cpu68k.cpp`). Aucun des candidats classés de la 5ᵉ passe (seuil spec512, attribution ligne
376+carry, HBL coalescé, troncature MFP) n'était la cause.

**Banc forgé (réutilisable)** : repro in-game SHO headless — script souris daté (`--mouse-at
1300`, clics titre, tenue accélérateur 700 trames/pas de classe, nitro = token `3` = 2 boutons,
SPACE ×4 pour « no music », accélérateur tenu en course) → course AFRICA déterministe,
~2 500 trames tracées `NEOST_PAL_TRACE_ALL=1` (nouveau : mode cumulatif « frame N » de
NEOST_PAL_TRACE, `Shifter.cpp`). Oracle : `--cmd-fifo` temps réel + événements **`leftdown`/
`leftup` AJOUTÉS à `control.c`** + `--trace video_color` en course + **[HEXC] étendu**
(position vidéo `hbl=`/`lc=` + `pc=`, vecteurs MFP inclus — `newcpu.c`, gate
`NEOST_HAT_IPLDIAG`). Diff : `compare.py` (scratchpad) — activations (paires idx 2+3, pc 1b30)
par ordinal.

**Mécanique élucidée (jeu, per-HBL)** : Timer B (fin de DE, ligne n−1) interrompt la boucle
principale à lc 404-416 → handler `$1ad6` : push ×3, `clr.b $fffffa1b`, lit `$FF8244.l`
(pc 1ae2), **`stop #$2100`** ($1ae8) → l'HBL de la ligne n, PENDANTE depuis lc 0 (masquée
IPL 6 pendant le handler), est prise À LA FRONTIÈRE DU STOP (cas niveau-sensible, pc0=$1aec)
→ handler HBL `$1b30` écrit couleurs 2+3 (1re écriture de paire, lc ~104-124) ; le stop
suivant ($1b10) est réveillé par l'HBL de la ligne n+1 à lc 0 (2e écriture, lc 68-76).

**Mesures (oracle instrumenté, 2 runs in-game ~20 000 exceptions)** :
- réveil STOP : **EXACT avant comme après** — écritures {68 : 40 %, 72 : 40 %, 76 : 20 %}
  identiques au point près des deux côtés ; exceptions à lc 0 pile.
- frontières d'entrée Timer B : identiques (lc {404, 408, 412, 416} aux mêmes poids) → le
  modèle « raise+commit à la frontière, sans différé ipl_fetch » est CONFIRMÉ fidèle
  (`NEOST_RAISE_WINDOW`, nouveau mécanisme opt-in de différé, mesuré : K>0 SUR-diffère —
  défaut 0).
- chaîne FIXE « exception TB → handler → stop → prise HBL » : **Hatari 144 cyc, NeoST 140**
  → +4 sur l'IACK (le `CPU_IACK_CYCLES_MFP_CE=12` d'Hatari — « not measured » — ne couvre
  pas le cycle bus d'IACK lui-même).
- après correction : histogramme 1re écriture {104 : 16,7/16,0 ; 108 : 24,5/25,9 ;
  112 : 23,9/23,8 ; 116 : 18,8/17,7 ; 120 : 10,4/10,8 ; 124 : 2,4/1,8} (NeoST/Hatari) —
  **verrouillé à ±1 pt partout** sur du code in-game vivant.

Effet de bord assumé : le boot glisse d'une trame (Timer C ×milliers d'IACK) → étalon
`nocooper` RECALÉ 6801→6802 par la méthode documentée dans sa note (0 px bit-identique à
l'oracle à la trame voisine, rendu intact). Étalons full tier verts.

### Son STE — restes [basses] portés (chaîne de sortie, ✅)

1. **Signe DMA ×−1** (`kDmaGain = −0.375`) : le LMC1992 inverse le canal DMA
   (dmaSnd.c:520-535) — phase relative YM↔DMA désormais fidèle.
2. **HPF sous-sonique déplacé sur le MIX en STE** : ST = HPF dans la chaîne YM
   (sound.c:1744) ; STE = YM **brut** dans le mix (`YM2149::setHpfBypass`), HPF appliqué au
   mélange YM+DMA (DC du DMA compris) dans `DmaSound::applyHpfStereo/Mono`
   (≙ dmaSnd.c:699,706), branché GUI + `--sound-dump` + WASM. États non sérialisés
   (format save-state inchangé).
3. **Correcteur LMC1992 en plateaux 1er ordre Savinkoff** : port exact de
   `DmaSnd_Bass_Shelf`/`DmaSnd_Treble_Shelf` (dmaSnd.c:1366-1404), coupures mesurées
   **118.2763 / 8438.756 Hz** — remplace le modèle RBJ 2e ordre 200/8000 Hz (audible dès que
   basses/aigus ≠ 0 dB). À 0 dB : identité exacte (bypass conservé).
4. **Horloge YM réelle 250 663 Hz** (`YM_250_HZ`) = MCLK 32084988 ÷2÷2÷4÷8 — Hatari utilise
   le MÊME MCLK sur ST et STE (clocks_timings.c ; l'ancien « 250 332 STE » de la 5ᵉ passe
   était erroné). L'ancien 250 000 rond jouait ~4,6 cents trop bas partout.

Validation : selftests 39/15/12/16/51, tier full vert, étalon `make_dmasnd_test` : fenêtre B
mid-trame toujours capturée (l'écrêtage sur ce signal synthétique extrême est FIDÈLE — Hatari
écrête pareil après son HPF de mix, dmaSnd.c:700-711). B10 ($FF8922) reste consignée ouverte.

### Bug hunt de la 10ᵉ passe (workflow 6 chasseurs + vérif adversariale, 2026-08-07)

16 trouvailles brutes (3 doublons inter-chasseurs), **12 corrigées le jour même** :

- **[MOYENNE] `kDmaGain` −0.375 → −0.1875** : le ÷4 d'Hatari (dmaSnd.c:520-535) couvre la
  demi-table STE **ET** la pré-compensation du ×2 `kLmcMakeup` — appliqué ensuite au mix
  ENTIER, DMA compris. L'ancien module ne retranchait que la demi-table : **canal DMA +6 dB
  vs Hatari** (0.744 fs au lieu de 0.372 à 0 dB LMC), écrêtage accru sur mix riche. La
  « validation S3 ratio DMA 0.75 » de la 5ᵉ passe mesurait NeoST contre sa propre lecture
  (erronée) du commentaire « 3/4 level », pas contre un WAV oracle.
- **[MOYENNE] `Machine::reconfigure` ne reposait pas `setHpfBypass`** : bascule STE→ST à
  chaud = YM sans recentrage DC (offset permanent, clics) ; ST→STE = YM filtré deux fois.
- **[MOYENNE] WASM : chaîne LMC non gatée par la machine** (le fix natif « gate LMC/ST »
  n'avait pas été porté au web) : sur `?machine=st`, gain ×2 sur un YM déjà pleine échelle
  → +6 dB + écrêtage. Gate `machineHasDmaSound` posé (HPF + gains + tonalité).
- **[MOYENNE] WASM : `adjustMachineForTos` absent** (boot ET menu ROM du shell) : un TOS
  ≤ 1.04 sur STE/MegaSTE haltait le CPU, écran figé sans message. Garde posée aux deux
  endroits (reconfigure du profil comme GUI/headless).
- **[MOYENNE] save-state : bit `LOOPING` de Moira forgeable** → `(this->*loop[ird])` sur
  pointeur-membre NUL en Release (l'assert saute). `ar.check` ajouté (jamais posé sur 68000).
- **[MOYENNE] `renderGlueFrame` appliquait le scroll fin STE de FIN de trame à toutes les
  lignes** (échantillonné une fois) : un split $FF8264/65 mi-trame + une seule écriture
  palette (seuil spec512 = 1) re-rendait la moitié haute avec le mauvais décalage. Corrigé
  par capture PAR LIGNE (`lineScrollSnap_`, même datation que `lineSnap_`) → **save-state v9**.
- **[BASSE] $FFFA31-$FFFA3F (impairs)** : void chez Hatari (ioMemTabST.c:143-150) — NeoST
  en faisait 8 octets de RAM relisible AVEC wait-state 4 cyc. Lecture 0xFF, écriture
  absorbée, sans wait (le dernier registre câblé est l'UDR $FFFA2F).
- **[BASSE] $FF8900 octet pair lit 0x00** (registre MOT, ioMemTabSTE.c:142) et **$FF8920 =
  octet scratch relisible** (non intercepté chez Hatari) — l'ancien 0xFF figé faisait
  échouer un `move.w $FF8900,d0` de diagnostic.
- **[BASSE] `NEOST_PAL_TRACE(_ALL)` : 1re ouverture du run en "w"** — un fichier survivant
  concaténait les sessions (« frame 0.. » dupliqués, diff désynchronisé).
- **[BASSE] headless : `--load-state` écrasait `--joy`** (hostJoy_/stePads restaurés) —
  le joystick est re-posé après restauration, avec log.
- **[BASSE] `parseMachine`/`parseRamBytes` : valeur inconnue → défaut ANNONCÉ** sur stderr
  (« mega-ste » silencieusement remplacé par STE faisait diffier contre le mauvais profil).

- **[BASSE→MOYENNE] ✅ Dispatch des événements échus AU point d'IACK** (`NEOST_IACK_SYNC`,
  défaut ON) — port du « `CycInt_Process()` + `MFP_UpdateIRQ_All` juste avant la séquence
  d'IACK » d'Hatari (newcpu.c:2938-2946 branche MFP, :2998-3003 branche vidéo). En mode bloc,
  les SYNC de l'entrée d'exception ne dispatchaient rien : un timer dont l'échéance tombait
  dans la fenêtre « frontière d'instruction → IACK » (~10-26 cyc) n'avait pas posé son bit IPR
  quand `Mfp::iack` élisait le vecteur. **Mesuré : un événement est réellement échu à 5,7-7 %
  des IACK** (banc SHO in-game, `NEOST_IACK_DISP=1`) — deux ordres de grandeur au-dessus de
  l'estimation « ~26/40106 » du rapport de chasse, qui ne considérait que le Timer C.
  ⚠ **PIÈGE (régression attrapée par le banc)** : le `syncTo` doit être précédé du **rebase du
  quantum** (`Cpu68k::rebaseQuantumAndSync`), exactement comme le saut d'attente STOP — sinon
  le temps couru depuis le début du quantum est facturé une DEUXIÈME fois par le
  `runTo(now + ran)` de `Machine`, et tout le raster glisse de ~16 cycles (mesuré : écritures
  palette {104..128} → {120..156}, réveil STOP {68,72,76} → {80..96}). C'est le troisième
  incident de cette classe : **tout dispatch déclenché depuis un hook Moira doit rebaser**.
  Validation : banc SHO inchangé (±0,1 pt vs oracle, réveil STOP 40/40/20), A/B `NEOST_IACK_SYNC`
  = comportement bien modifié (un handler démarre une ligne plus tôt), tier full vert.

**Vérifiés/écartés** : rien d'autre — 0 réfutation sur 13 uniques, taux inhabituel signalant
des chasseurs bien ciblés (code frais de la session) plus que des vérificateurs laxistes :
chaque correctif ci-dessus a été RE-vérifié à la main contre les sources Hatari avant d'être
appliqué.

## Extensions NeoST sans équivalent Hatari (divergences délibérées, hors fidélité)

Ces fonctionnalités **n'existent pas dans Hatari** et ne sont donc pas des écarts à
corriger : ce sont des extensions NeoST, **inactives par défaut** et **sans effet sur
les étalons de fidélité** (réseau OFF pendant `run_all.py --tier fast/full`).

- **[EXTENSION] FujiNet virtuel sur le bus ACSI** (`src/io/FujiDevice.*`,
  `src/net/*`, cible ACSI dédiée). Périphérique WiFi de déport de protocole (HTTP/TCP/
  JSON, montage d'images distantes) attaché via un **opcode vendeur ACSI `$60`**. La
  méthode imposée (« porter Hatari d'abord ») s'applique seulement aux **formes** :
  `rs232.c` (redirection série hôte), `midi.c` (I/O MIDI hôte), `hdc.c` (déjà porté
  dans `Acsi`). Le protocole FujiNet lui-même vient du firmware amont
  ([fujinet-firmware](https://github.com/FujiNetWIFI/fujinet-firmware/wiki)) — pas
  d'Hatari. Spec complète : `docs/FUJINET.md`. Garde-fou : l'opcode `$60` n'est routé
  que sur la cible FujiNet ; toute autre cible ACSI reste **byte-identique** au port
  de `hdc.c`. Auto-test déterministe : `neost-headless --fuji-selftest` (palier `fast`).
