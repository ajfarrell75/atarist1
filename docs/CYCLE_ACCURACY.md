# Précision cycle — modèle, acquis, restant (oracle : Hatari)

> (c) 2026 VERHILLE Arnaud — **référence durable** de la précision cycle de NeoST.
>
> - **Le modèle visé** = Hatari : `extern/hatari/src` (source de vérité, lu pas compilé) — surtout
>   `cycInt.c`/`cycInt.h` (ordonnanceur), `cycles.c` (compteurs), `video.c` (raster), `mfp.c`
>   (timers), `fdc.c` (disque).
> - **Le cœur 68000** = Moira (cycle-exact, `MOIRA_PRECISE_TIMING=true`). Seul cœur ; cf.
>   [`DEV.md`](../DEV.md). *(L'ancien Musashi a été retiré — toute mention « 2 cœurs » dans
>   d'anciennes notes est périmée.)*
> - **Le front actif** (convergence Moira↔WinUAE, beam-sync) vit dans son propre dossier :
>   [`MOIRA_WINUAE_CONVERGENCE.md`](MOIRA_WINUAE_CONVERGENCE.md). Ce document-ci pose le cadre et
>   l'inventaire ; ce doc-là tient le journal de la dernière passe.

## 1. Pourquoi — le timing au cycle change le comportement

Le ST n'a ni scrolling matériel, ni copper : tout effet vidéo passe par la réécriture des
registres Shifter/Glue (`$820A`, `$8260`, palette, base) **à un cycle précis** du 68000 pendant
le balayage. Symétriquement, beaucoup de jeux **pollent** un registre (compteur vidéo `$FF8209`,
flag effacé par une IRQ) et décident en fonction. Si la **phase CPU↔faisceau** dérive ne
serait-ce que de quelques cycles, l'écriture tombe à la mauvaise ligne (image qui saute) ou la
boucle de poll ne sort jamais (gel).

Cas d'école historique : **Arkanoid** boucle sur `tst.b $26E7 / bne` (`$31736`), où `$26E7` doit
être remis à 0 par une IRQ à un instant précis. Le gel `$31736` exigeait un FDC au timing réel
(spin-up + débit MFM) — **résolu** par le modèle FDC rotationnel (cf. `CHANGELOG.md`,
`[[arkanoid-freeze-investigation]]`). Arkanoid atteint désormais son écran-titre mais ne franchit
pas la partie (cause distincte, suivie au catalogue de `TODO.md`).

## 2. Le modèle d'Hatari (l'architecture cible)

### 2.1 Une horloge globale, des compteurs par instruction (`cycles.c`)

Un compteur de cycles **global** (`CyclesGlobalClockCounter`) partagé CPU + vidéo, avancé au
cycle. Chaque instruction CPU rapporte son **coût exact** ; `Cycles_GetInternalCycleOn{Read,
Write}Access` date un accès à la **fin du cycle bus** (`currcycle*2 + 4`), après alignement.

### 2.2 Ordonnanceur d'événements datés (`cycInt.c`)

Le cœur du modèle. Une **table d'interruptions futures**, une par source matérielle
(`INTERRUPT_VIDEO_VBL/HBL/ENDLINE`, `MFP_TIMERA..D`, `ACIA_IKBD`, `FDC`, `BLITTER`,
`DMASOUND_MICROWIRE`, `HDC_ACSI`…). On ne garde dans `PendingInterruptCount` que le nombre de
cycles jusqu'au **prochain** événement ; la boucle CPU le décrémente du coût de chaque
instruction ; à `≤ 0`, le handler dû s'exécute puis se **re-planifie** (absolu = période fixe
sans dérive ; relatif = depuis l'instant courant). Le **retard** (une instruction longue dépasse
l'échéance) est reporté au handler → **pas de dérive cumulée**.

### 2.3 Unité interne CPU↔MFP (anti-arrondi)

Pour synchroniser CPU (8021248 Hz) et MFP 68901 (2457600 Hz) **sans flottant** :
`1 cycle CPU → 9600 unités`, `1 cycle MFP → 31333 unités` (ratio exact `31333/9600`,
`CYCINT_SHIFT`). Idée d'Arnaud Carre (sc68). Indispensable pour des périodes de timers exactes
sur des milliers de trames. NeoST conserve les échéances A/B/C/D en unités internes de
**1/256 cycle CPU** (`Mfp::timerDueSub_`) et n'arrondit au cycle entier qu'au moment de les
présenter au Scheduler ; la fraction reste l'ancre du rechargement suivant et survit au
save/load (format v11).

### 2.4 Vidéo au cycle (`video.c`)

`video.c` planifie **par scanline** des événements à des cycles précis : début/fin de
Display-Enable (pour Timer B event-count et les bordures), `VIDEO_ENDLINE`, `VIDEO_HBL`,
`VIDEO_VBL`, avec gestion 50/60 Hz, écrans courts/longs, et suppression de bordures
(`BORDERMASK_*`). L'origine de trame est `VBL_ClockCounter = GlobalClock − PendingCyclesOver −
VblVideoCycleOffset` (offset 64 STF / 68 STE).

### 2.5 MFP (`mfp.c`) et FDC (`fdc.c`)

- **MFP** : timers A-D = événements datés, modes **delay** (prescaler 4/10/16/50/64/100/200),
  **event-count** (piloté par le Display-Enable vidéo ou un front GPIP), **pulse-width** ;
  latence d'IRQ (4 cyc) et IACK au cycle.
- **FDC** : disque **temporel** — moteur on/off + spin-up, index pulse (3,71 ms/rotation),
  step-rate, BUSY, DRQ/INTRQ via événements `INTERRUPT_FDC`, transfert par blocs dans le temps.

## 3. Ce qui est ACQUIS (phases 0-6 + extensions)

Le squelette cycle-exact est en place. Tout ce qui suit est **fait et validé** (détails et
validations dans `CHANGELOG.md`) :

| Brique | Essence | Réf. NeoST |
|--------|---------|------------|
| **Cycles par instruction** | `Cpu68k::run` rend le coût réel ; Moira est cycle-exact | `Cpu68k.cpp` |
| **Scheduler daté** | une échéance « prochaine » par source, handlers qui se replanifient | `Scheduler.hpp` |
| **Horloge continue + carry** | `runFrame` n'efface plus l'horloge par trame ; le dépassement est reporté | `Machine.cpp` |
| **Vidéo scanline** | `beginFrame` verrouille la résolution, `renderLine(y)` décode avec l'état courant | `Shifter.cpp` |
| **Événements vidéo au cycle** | rendu fin-DE, Timer B event-count à 400, HBL à 508 | `Machine.cpp` |
| **Géométries 50/60/71 Hz** | cycles/ligne (512/508/224), lignes/trame, DE start/end dérivés, verrouillés à `beginFrame` | `Shifter::Geometry` |
| **Timers MFP datés** | A/C/D mode délai + B event-count sur DE ; conversion entière MFP→CPU | `Mfp.cpp` |
| **Chaîne IRQ MFP fine** | délai 4 cyc daté (`MFP_IRQ`), chronologie multi-IRQ (`pendingTime_`), IACK ré-évalué | `Mfp.cpp`, `Cpu68k.cpp` |
| **FDC temporel** | BUSY posé, durée calculée, INTRQ différé ; modèle rotationnel complet (spin-up, débit MFM) | `Fdc.cpp` |
| **Registres STE différés** | compteur vidéo matérialisé (`vcLineBase_`) ; `$8205/07/09`, HSCROLL, LINEWIDTH appliqués en fin de ligne | `Shifter.cpp` |
| **Quantum sous la ligne** | `liveNow()` = cycle absolu exact à l'écriture (sous-instruction Moira) + préemption | `Scheduler`, `Cpu68k` |
| **Bordures H/B/G/D** | machine Glue STF (`updateGlueState` ≙ `Video_Update_Glue_State`), self-test 19/19 | `Shifter.cpp`, `Glue.hpp` |
| **Spec512** | palette intra-ligne pixel-identique à l'oracle (latch + alignement bus shifter) | `Shifter.cpp` |
| **Wait states bus** | shifter/PSG/MFP/ACIA + E-Clock | `Bus.cpp`, `Cpu68k.cpp` |
| **Microwire/LMC1992 datés** | filtres son STE horodatés | `DmaSound.cpp` |

> **Convergence Moira↔WinUAE au niveau instruction = atteinte** (`NEOST_RAM_SLOT` align créneau
> bus + fix DIV). C'est l'objet du doc [`MOIRA_WINUAE_CONVERGENCE.md`](MOIRA_WINUAE_CONVERGENCE.md),
> qui tient le détail et le front beam-sync.

## 4. Ce qui RESTE — inventaire priorisé

Établi en diffant `extern/hatari/src` contre `src/` sous-système par sous-système. Trié par
priorité d'impact. Les divergences logiques bornées (V1, S2, D3, M1…) sont cataloguées avec
sévérité + `fichier:ligne` des deux côtés dans
[`HATARI_DIVERGENCES.md`](HATARI_DIVERGENCES.md) ; ici, l'angle « précision cycle ».

| Priorité | Chantier | Effort | Étalon type |
|----------|----------|--------|-------------|
| **P1** | Beam-sync : phase CPU↔faisceau **par-ligne** (overscan vertical EL) | élevé | Enchanted Land en jeu |
| **P1** | Tricks par changement de résolution mid-ligne (V2 hi/med/lo, overscan med-res) | moyen+ | Cuddly, No Cooper |
| **P1** | Géométrie mid-trame (V3 : 50↔60 Hz en cours de trame, `RestartVideoCounter`) | élevé | overscan plein écran, ULM Dark Side |
| **P1** | Rendu live du retrait BAS + lignes EMPTY/BLANK/NO_DE | moyen | scroller bordure basse Cuddly |
| **P2** | Blitter : interfoliage `CycInt_Process` par accès bus + `cpu_bus_rmw` (le partage 64/64 est PORTÉ) | moyen | démos CPU+blitter simultanés |
| **P2** | Son DMA STE : `$FF8909/0B/0D` au cycle de la lecture (la FIFO 8 octets est PORTÉE) | moyen | STE_Test, sync zik/raster |
| **P2** | Restes vidéo : `VIDEO_ENDLINE`, VBL au cycle exact, phase Timer C | moyen | démos fullscreen |
| **P3** | Wakeup states WS1-4, jitter HBL/VBL, branche STE de la Glue | élevé | démos « extrêmes » (Closure…) |
| **P3** | Unité interne ×256, fréquences exactes centralisées | faible | dérive long terme |

### P1 — Beam-sync (le chantier ouvert)

C'est le front actif. **Convergence instruction faite** ; reste la **phase d'entrée d'IRQ** et la
**géométrie par-ligne**. Détail, mesures, bancs et pistes éliminées :
[`MOIRA_WINUAE_CONVERGENCE.md`](MOIRA_WINUAE_CONVERGENCE.md). En bref :

- **Overscan vertical EL en jeu** : la dérive *moyenne* du faisceau correspond à Hatari (+78/ligne
  avec `NEOST_RAM_SLOT`+`NEOST_IACK`, défaut ON), mais la **phase absolue par-ligne** diffère
  (NeoST culmine cyc 476-492 vs Hatari 500-508) → le retrait haut ne « tient » pas. Fermeture =
  tracking cycle-exact du handler **par ligne** (alternance 76/80 de Hatari), pas un offset
  constant.
- **V2 res-switch** (line-shortening hi-res précoce ≤56 cyc) : porté fidèle, opt-in `NEOST_V2`,
  validé structurellement à l'oracle. Limite prouvée : non déclenchable pixel-stable headless
  (latence HBL ~56 cyc) → couplé à la convergence.

### P1 — Registres vidéo STE : 2 restes

- **`bSteBorderFlag` / mode 336 px** (`video.c:530`) : combo `$FF8265>0` puis `$FF8264=0`
  → 16 px de plus à gauche (prefetch sans scroll). Absent. *Effort moyen.* Étalon : Obsession,
  Pacemaker.
- **`RestartVideoCounter` ligne 310/260** : ✅ **PORTÉ (2026-07-02)** — événement
  `Scheduler::VC_RESTART` (`src/core/Machine.cpp:319-323` callback, `:386-390` planification).
  Reste couplé à la géométrie par-ligne (V3) pour les bascules 50/60 Hz en cours de trame :
  `beginFrame` verrouille encore la géométrie de la trame. Étalon : ULM Dark Side of the Spoon.

### P2 — Blitter non-hog (`blitter.c:251,395`)

✅ **PORTÉ le 2026-07-07** — le partage de bus non-hog est implémenté : tranches de 64 accès
bus / 64 accès CPU **réels**, suspension MID-MOT (l'état du mot en cours survit à la coupure),
et le bug matériel « +1 accès CPU compté blitter » (`busCountError_` → tranche de 63).
Cf. `src/core/Blitter.cpp:22-27` (constantes) et `:247-310` (tranche + comptage).

**Restent** : pas d'interfoliage `CycInt_Process` par accès bus pendant une tranche (le CPU est
stallé en bloc, ses cycles internes ne recouvrent pas le blit), et `cpu_bus_rmw`.

### P2 — Son DMA STE (`dmaSnd.c:737`)

✅ **FIFO 8 octets PORTÉE** (`src/core/DmaSound.cpp:93` `fifoRefill`, `:130` `fifoPull`),
ainsi que le gain LMC ×2 (S3, `:413-420`).

**Reste** : `$FF8909/0B/0D` doit refléter la position **au cycle** de la lecture (`Sound_Update`,
`DmaSnd_GetFrameCount`) ; NeoST n'avance le compteur qu'au rythme de la synthèse, et le refill
FIFO est quantifié différemment. Étalon : STE_Test.

### P2 — Restes vidéo « plan »

`VIDEO_ENDLINE` distinct du HBL ; VBL au cycle exact (vraie fin de trame + offset 64/68) ; phase
exacte du tic Timer C (au cycle de programmation) ; lecture compteur à cheval sur deux lignes
(`Video_CalculateAddress`).

### P3 — Précision « démos extrêmes »

- **Wakeup states WS1-4** (`video.c:626`) : 4 jeux de timings ±1 cyc (NeoST = WS3 figé, choix
  acté pour le mode cycle-exact). Structure : table de timings au lieu de constantes `glue::`.
- **Jitter HBL/VBL** : ⚠ `HblJitterArray/VblJitterArray` sont **morts** dans le Hatari courant
  (déclarés, jamais définis/référencés) — NE PAS porter. Le vrai jitter d'autovecteur = synchro
  **E-Clock** (`M68000_WaitEClock`), porté (`NEOST_IACK`).
- **Branche STE de la Glue** (V1) : `Preload_Start_*`, `LEFT_OFF_2_STE` non gérés → overscan de
  démos STE (E605, DHS) mal placé.
- **Overscan med-res** + **NO_SYNC/SYNC_HIGH** (lignes vides par manipulation du sync hi-res).

### P3 — Horloges & divers

- **Unité interne ×256** (`CYCINT_SHIFT=8`) : NeoST tronque la conversion 31333/9600 à l'entier
  par échéance ; l'unité interne supprime la dérive résiduelle long terme (négligeable
  aujourd'hui car replanification ancrée).
- **Fréquences exactes centralisées** (`clocks_timings.c` : CPU 8021248 Hz PAL, VBL ≈ 50,05 Hz) :
  seul impact réel = synchro audio long terme.
- **Offsets de datation Moira vs Hatari** : `kVideoCounterReadOffsetCyc = −6`
  (`Shifter.cpp:89`), `kSyncWriteOffsetCyc = +2` (`Shifter.cpp:1113`) et
  `kSpec512AlignCyc = −25` (`Shifter.cpp:34`) ne sont **pas** des constantes empiriques : ce
  sont les valeurs fidèles dérivées de `Video_CalculateAddress` et
  `Cycles_GetInternalCycleOn{Read,Write}Access` (2026-07-03). ⚠ **read et write se déplacent
  PAR PAIRE** — bouger l'un seul casse Enchanted Land. Chaque nouveau mécanisme daté choisira
  son offset par la même méthode (dérivation, puis contrôle au sweep vs oracle).

## 5. Ce qu'on ne fera PAS (décisions actées)

- **Contention DMA vidéo générale sur la RAM** (le shifter vole des cycles au CPU pendant le DE) :
  modèle **MAME** (`stmmu.cpp::bus_contention`), **pas Hatari** → l'ajouter ferait diverger NeoST
  de l'oracle qui valide nos étalons au pixel.
- **Vol de cycles DMA son / FDC sur le bus** : Hatari ne le modélise pas non plus.
- **Pulse-width MFP (modes 9-15)** : Hatari l'approxime déjà en mode délai → même approximation.

## 6. Méthode de validation

Chaque chantier : **(1)** porter depuis `extern/hatari/src` → **(2)** re-tester les étalons
(`tools/run_etalons.py`, glue-selftest 19/19, Spec512/overscan_top byte-identiques) → **(3)**
diff à l'oracle Hatari.

```sh
# Hatari (référence) headless — cf. docs/HATARI_AUTOMATION.md
SDL_VIDEODRIVER=dummy hatari --machine st --monitor rgb --sound off \
  --tos <TOS> --disk-a <jeu.st> --trace cpu_disasm --trace-file hatari.txt --run-vbls N
# NeoST
./build/neost-headless <TOS> --frames N --disk <jeu.st> --trace neost.txt --regs --irq
# Diff : première divergence (PC + registres), ou périodes par PC-landmark
python3 tools/trace_diff.py neost.txt hatari.txt --align-pc FC0030 --regs
python3 tools/trace_diff.py neost.txt hatari.txt --periods 173C 1742   # convergence cycle
```

**Indicateur de progrès** : à chaque phase, le point de première divergence recule, et les
points d'insertion d'IRQ coïncident avec Hatari (le diff cesse d'être « bruité » par le décalage
des interruptions). Pour le harnais différentiel de cycles (Moira↔WinUAE) et les bancs dédiés
(`make_cycle_bench.py`, `make_respulse_test.py`), voir
[`MOIRA_WINUAE_CONVERGENCE.md`](MOIRA_WINUAE_CONVERGENCE.md). Catalogue des logiciels étalons par
sous-système : [`TEST_SOFTWARE.md`](TEST_SOFTWARE.md).
