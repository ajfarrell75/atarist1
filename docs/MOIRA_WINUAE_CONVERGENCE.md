# Convergence Moira ↔ WinUAE — chantier « full timing-engine » (beam-sync)

**But.** Rendre le timing de Moira (cœur 68000 de NeoST) **cycle-identique à WinUAE** (= le cœur
CPU qu'utilise Hatari, `extern/hatari/src/cpu/`), puis **retirer les hacks empiriques** de datation
vidéo de NeoST. C'est la cause racine commune des bugs beam-sync (Lethal Xcess, Enchanted Land,
Cuddly, Super Hang-On). Décision utilisateur (2026-06-16) : **garder le sync-driven (PT=true)** +
**full convergence** (pas un graft chirurgical).

> ⚠️ Ce doc CORRIGE plusieurs notes mémoire optimistes/contradictoires. Lire d'abord §« Vérités
> mesurées » avant de rouvrir une piste.

---

## 1. L'outil n°1 — harnais différentiel de cycles (Moira ↔ WinUAE)

Le SEUL métrique fiable de convergence : comparer **les cycles par itération de boucle** entre les
deux cœurs sur du code identique. Construit cette session (Tracer.cpp opt-in + trace_diff.py).

```sh
python3 tools/make_poll_test.py /tmp/poll.st
# Moira (NeoST) — colonne cycle absolue (opt-in, défaut trace inchangé)
NEOST_TRACE_CYC=1 ./build/neost-headless roms/tos102uk.img --disk /tmp/poll.st \
    --machine st --mem 512k --frames 150 --trace /tmp/neost.txt 2>/dev/null >/dev/null
# WinUAE (Hatari) — colonne cycle absolue (CyclesGlobalClockCounter)
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOME=/tmp/hatari_home; mkdir -p /tmp/hatari_home
/opt/homebrew/bin/hatari --machine st --tos roms/tos102uk.img --monitor rgb --disk-a /tmp/poll.st \
    --sound off --fast-forward on --confirm-quit off --statusbar off --frameskips 0 \
    --alert-level fatal --run-vbls 150 --trace cpu_disasm,cpu_video_cycles \
    --trace-file /tmp/hatari.txt >/dev/null 2>&1
# Différentiel : périodes par PC-landmark (robuste au split de flot)
python3 tools/trace_diff.py /tmp/neost.txt /tmp/hatari.txt --periods 173C 1742
```
- `NEOST_TRACE_CYC=1` → préfixe `cyc=<clock absolu>` (= `busClockNow()`, analogue de Hatari
  `CyclesGlobalClockCounter`). Trace par défaut **byte-identique** sans l'env.
- `--periods PC…` → pour chaque PC, la **période dominante** (delta d'horloge entre visites) côté
  NeoST et côté Hatari + verdict `OK`/`DIFF`. Robuste au split de flot (aligne sur landmarks).
- Landmarks du poll-test : `173C`=`bra.s self`, `1742`=lecture `$8209` du handler HBL.

---

## 2. Carte des divergences (source-grounded, 3 agents — file:line des DEUX côtés)

| # | Mécanisme | Moira | WinUAE/Hatari | État |
|---|---|---|---|---|
| **A** | **Alignement créneau bus 4 cyc sur la RAM (CHIP16)** : avant un accès RAM, WinUAE attend `(4 - clock&3)` cyc (`wait_cpu_cycle_read`, custom.c:148-153). Moira NE l'a PAS. ROM/cart/**IO** = FAST (pas d'alignement, mesuré STF, memory.c:1798). | absent | custom.c:148-153 ; memory.c:1548/1798 | ✅ **PORTÉ** (`NEOST_RAM_SLOT`) |
| **B** | **Délai de reconnaissance IPL** : Moira reconnaît l'IPL immédiatement (POLL_IPL≡`reg.ipl=ipl`, MoiraMacros.h:64). WinUAE diffère d'1 instr si le pin a changé <4 (ou prend l'ancien si <2) cyc avant l'échantillon (`ipl_fetch_next`, newcpu.c:4982-4997). | MoiraMacros.h:64 ; Moira.cpp:419 | newcpu.c:4982-4997, 5672-5673 | ⏳ prototype `NEOST_IPLDELAY` |
| **C** | **E-clock + bloc occupé à l'IACK** : WinUAE applique l'attente E-clock (0..8, motif [0 8 6 4 2]) PUIS `CPU_IACK_CYCLES_VIDEO_CE(10)+idle(4)` au cycle d'IACK (`iack_cycle`, newcpu.c:2958-3019). Moira n'a ni l'un ni l'autre ; NeoST plaçait l'E-clock dans `willInterrupt` (≈14 cyc trop tôt). | MoiraExceptions_cpp.h:508/533-543 | newcpu.c:2958-3019 ; m68000.c:810 | ⏳ `NEOST_IACK` (E-clock @ IACK + bloc) |

**Fait structurel clé (persistance du fork).** Le superprojet **COPIE** `extern/moira/Moira/*` dans
`build/generated/moira/` et **réécrit `MoiraConfig.h`** (force `PRECISE_TIMING=true`,
**`MIMIC_MUSASHI=false`**, `EMULATE_ADDRESS_ERROR=true`) — cf. CMakeLists.txt:76-118. Donc (a)
`MIMIC_MUSASHI` est **false** à la compilation (la note mémoire « true » est trompeuse), (b) éditer
`extern/moira` directement est CLOBBERÉ au `submodule update`. **Surface de fork = la sous-classe
`NeostMoira` dans `Cpu68k.cpp`** (overrides `read*/write*/sync/willInterrupt/readIrqUserVector`).
Si un edit Moira interne devient nécessaire → **vendoriser** (dé-submoduliser).

**En PRECISE_TIMING=true, les tables `CYCLES_*` de MoiraExec_cpp.h sont MORTES** (le timing vient
des `SYNC()` dans les accès/prefetch). Éditer ces tables ne fait RIEN. La convergence se fait par
DATATION (instants des accès), pas par comptage d'instruction.

---

## 3. Vérités MESURÉES cette session (corrigent la mémoire)

- ✅ **A (RAM_SLOT) est un vrai gain de convergence INSTRUCTION** : `bra.s self` en RAM passe de
  **10 (Moira) → 12 (WinUAE) = OK** au différentiel ; le **pas du beat** poll passe de 2 → **4**
  (= Hatari). **Zone active pixel-exacte vs oracle Hatari** (overscan_top crop=active = 0 px ON/OFF).
  ⚠️ L'ancien « chipWait8 FALSIFIÉ » (mémoire) ajoutait un **+4 parasite** (miroir erroné du 16 MHz) ;
  la version FIDÈLE est **align-only** (pas de +4 à 8 MHz, Moira facture déjà l'accès). Gated, sûr.
- ❌ **L'E-clock NE converge PAS en isolation.** Le « 56 % → 34 % » de [[eclock-convergence-validated]]
  est une **FRAME CHERRY-PICKED** (frame 390, phase 2 — PAS phase 8). Sur la **moyenne 5 frames**,
  l'E-clock fait **56 % → 70 %** (PIRE) : il ajoute du **jitter de phase trame-à-trame mal calé** sur
  Hatari. Le baseline 56 % est **stable** (offset constant), pas du jitter.
- ❌ **Aucun offset constant** (`NEOST_VC_OFF` -12..+12) ne descend la moyenne sous ~55 %. Le 56 %
  est **structurel** (motif de barres différent), pas un simple décalage → le **poll-screenshot est
  un métrique saturé/peu fiable**. Ne plus l'utiliser comme cible principale.
- 🔗 **Chicken-and-egg établi** : le jitter E-clock dépend de la phase d'horloge absolue, qui dépend
  de TOUT le timing d'instruction. ⇒ **converger le timing INSTRUCTION d'abord** (différentiel → 0),
  PUIS la phase d'IRQ tombe juste. L'E-clock ne se calibre PAS isolément.
- 🏁 **JALON (banc différentiel multi-instructions, `tools/make_cycle_bench.py`)** : **NEOST_RAM_SLOT
  converge 14/14 boucles d'instructions à WinUAE** (vs 2/14 sans). Le Δ+2 sur quasi toutes les
  instructions (off) = exactement le créneau bus manquant ; WinUAE arrondit chaque période à un
  multiple de 4, RAM_SLOT le réplique. ⇒ **convergence cycle au niveau INSTRUCTION = ATTEINTE**
  (move/ALU/clr/tst/shift/cmp/addi…). Workflow en cours pour vérifier modes d'adressage/mul-div/
  branches/bits/MMIO et trouver les résidus.
- ⚠️ **RAM_SLOT NÉCESSAIRE mais PAS SUFFISANT pour les JEUX** : EL reste deadlock (noir dès frame
  1200, INCHANGÉ ON/OFF). Sa cause = la phase CPU↔faisceau au niveau IRQ/dispatch (EL atteint sa
  boucle beam-sync `$EE78` ~50 lignes trop tard = bordure basse, compteur figé à 0x2c → spin), un
  **artefact du sync-driven** (PT=true re-phase l'entrée de trame), SÉPARÉ du timing d'instruction.
  ⇒ après le timing instruction, le chantier restant = **phase d'entrée d'IRQ/dispatch** (E-clock @
  IACK qui ne compose pas encore avec RAM_SLOT — beat poll reste période-3 vs Hatari période-5 — ET
  la datation de dispatch sync-driven qui décale EL). C'est là que se gagnent les jeux.

---

## 4. Implémenté cette session (tout GATED, défaut OFF = build inchangé/vert)

`src/core/Cpu68k.cpp` (sous-classe `NeostMoira`) :
- `NEOST_RAM_SLOT` (+`_PHASE`) → `chipWait8()` align-only sur RAM <$400000 dans read8/16/write8/16.
- `NEOST_IACK` (+`_VIDEO`/`_MFP`/`_LEAD`) → E-clock @ IACK (via `willInterrupt`+lead-in 14) + bloc
  occupé. (Le bloc constant est ABSORBÉ par l'ordonnanceur beam-anchoré pour le beam-sync en boucle
  d'attente ; il compte pour le code de jeu non-spinnant.)
- `NEOST_IPLDELAY` (préexistant) → retard pin 4 cyc (approx `ipl_fetch_next`).

`src/core/Tracer.cpp` : `NEOST_TRACE_CYC=1` → colonne `cyc=` (harnais). `tools/trace_diff.py` :
mode `--periods`.

**SÛRETÉ vérifiée** : flags OFF → `run_etalons` 19/0 + TOUS OK, EL inchangé. Le différentiel
bascule proprement OFF=DIFF(10) / RAM_SLOT=OK(12).

---

## 5. ✅ CONVERGENCE INSTRUCTION — COMPLÈTE (validée par workflow 6 classes)

Le différentiel a été piloté vers 0 sur **tout le jeu d'instructions courant** (workflow 6 agents,
banc `tools/make_cycle_bench.py` par classe). Verdict **avec NEOST_RAM_SLOT=1** :
- ✅ **5/6 classes 100 % OK** : modes d'adressage (toutes variantes src/dest), ALU & comparaison,
  branches & flot (Bcc/BRA/BSR/JMP/JSR/RTS/DBcc), décalages/rotations/bits, unaires & divers (y
  compris **écritures MMIO shifter `$8240/$8260/$820A`** — le `syncCpuBus` empirique de NeoST datait
  DÉJÀ comme WinUAE `M68000_SyncCpuBus`). MUL/MOVEM/move.l : OK.
- ✅ **DIV** (seule divergence résiduelle, Δ+4) : CORRIGÉE. Cause root-causée : Moira faisait
  `writeD; prefetch; SYNC(idle)` vs WinUAE `idle; store; prefetch` → le prefetch du DIV était aligné
  à la phase PRÉ-idle au lieu de POST-idle (DIFF ssi idle%4==2). **Fix** : reorder dans
  `execDivsMoira`/`execDivuMoira` (MoiraExec_cpp.h, fork Moira committé e4da365). Neutre sans
  RAM_SLOT (étalons inchangés), converge avec.

⇒ **La datation cycle d'instruction de NeoST = WinUAE, cycle pour cycle, sur tout le jeu courant.**
C'est le « full WinUAE timing convergence » au niveau INSTRUCTION. (Garder NEOST_RAM_SLOT opt-in tant
que la phase IRQ n'est pas faite : seul, il décale la phase de trame des hacks empiriques sans les
remplacer — cf. §6.)

## 6. ✅ FONDATION CORRIGÉE — dispatch BLOC (Enchanted Land DÉBLOQUÉ)

**Le DEADLOCK EL N'ÉTAIT PAS la convergence ni PT — c'était le MODÈLE DE DISPATCH sync-driven.**
A/B décisif (réponse au « reconsidérer la fondation ») : repasser au **dispatch BLOC** (CPU borné à
l'événement suivant + dispatch à la frontière via `runTo`, modèle pré-sync-driven) **tout en gardant
PT=true + RAM_SLOT** → **EL DÉ-DEADLOCKÉ** : l'INTRO (logo Thalion + pluie) rend **PROPREMENT** (vérifié
visuellement ; était 0 %/NOIR dès la trame ~1200 sous sync-driven). Le sync-driven (dispatch
mid-instruction `do_cycles` WinUAE) deadlockait la boucle beam-sync `$EE78` d'EL SANS corriger le jitter
(déjà falsifié) = **net-négatif, RÉFUTÉ**. La convergence d'instruction est **indépendante du dispatch**
(PT=true suffit). **FAIT (défaut, commit ff3ab25)** : bloc par défaut, sync-driven en opt-in
`NEOST_SYNC_DISPATCH`. Validé : étalons 19/0 + TOUS OK, différentiel 14/14 (RAM_SLOT), LX inchangé,
`NEOST_SYNC_DISPATCH=1` reproduit le deadlock (A/B intact).

> ⚠️ **NUANCE (vérifiée à l'image)** : le bloc DÉ-DEADLOCKE EL et rend l'INTRO propre, mais le
> niveau EN JEU (recette `--joy-at 3100 0x80` → frame 13000) **SCRAMBLE encore** (garbage plein écran).
> C'est le **résidu EL d'origine** (tricks fullscreen hi-res per-ligne, V2 res-switch / beam-sync), qui
> PRÉ-DATE le sync-driven (lequel l'avait juste enterré sous un deadlock pire). ⇒ le bloc RESTAURE l'état
> pré-sync-driven (intro propre, jeu scramble), il NE corrige PAS le scramble. C'est un VRAI gain de
> fondation (sync-driven était strictement pire) mais EL n'est PAS « réparé » en jeu.
>
> ⚠️ Le dé-deadlock = le DISPATCH BLOC, PAS RAM_SLOT (EL identique avec/sans). RAM_SLOT reste la
> convergence d'INSTRUCTION (fidélité WinUAE), sans impact jeu prouvé → garder opt-in (décale les
> réf-étalons SELF de 56 px en bordure, zone active intacte).

## 7. CHANTIER RESTANT (plus de deadlock, mais EL scramble en jeu)

Avec la fondation bloc+PT, plus de deadlock ; EL boote/intro propre. Reste, par valeur décroissante :
1. **[HAUTE] EL corruption EN JEU (scroll) — ⚠️ DIAGNOSTIC RÉVISÉ (2026-06-17, comparaison Glue
   rigoureuse).** Mon hypothèse « branche Glue manquante / bordures fermées » est **RÉFUTÉE**. Faits
   établis par lecture ligne-à-ligne des 2 sources + glue-selftest 19/19 + exécution du jeu :
   - **`updateGlueState` (Shifter.cpp:677-846) est un PORT FIDÈLE de `Video_Update_Glue_State`
     (video.c:2244-2438).** Constantes de cycle identiques (HDE_On_Hi=4, HDE_Off_Low_50=376,
     DE_end_right=462, Line_Set_Pal=54…). La branche right-border de la freq=60 (video.c:2782-2800) EST
     présente (Shifter.cpp:780-786). **AUCUNE branche manquante.**
   - **Les bordures G/D d'EL S'OUVRENT bien** (terrain vert rendu BORD À BORD, au-delà de 320 px).
     Mon `bm=000 nPix=320` était un FAUX (NEOST_RENDER_TRACE ne trace QUE les 12 1ʳᵉˢ lignes affichées =
     le CIEL en haut, bordures fermées là ; le terrain plus bas ouvre). EL écrit la freq=60 réelle à
     **cyc 376/460** (bord de ligne) — pas 276. Le cluster cyc 276 = la sonde de **calibration du loader**
     (freq=02 50 Hz), qui ne doit PAS ouvrir de bordure (correct).
   - **EL rend correctement les écrans statiques** (logo, crédits) ; la corruption apparaît en
     **SCROLL ACTIF**. `endVideoLine` (Shifter.cpp:383-409) avance DÉJÀ `vcLineBase_` du stride réel via
     `glueLineBytes` (+26/+44 selon bordermask), donc le cas simple EST modélisé.
   - **ANOMALIE LOCALISÉE (NEOST_RENDER_ALL, 2026-06-17)** : la corruption vient du **HAUT de l'écran
     (lignes 36-42, overscan)**, pas du corps. EL y fait des **impulsions res (hi-res) en FIN de ligne**
     (`res=02@cyc~440` puis `res=00@~444`, pc 010b12/010a8e, par ligne) = retrait bordure droite + gauche
     ligne suivante. NeoST en déduit : lignes 36-37 `LEFT_OFF` (+26 chacune), **lignes 40-41 `NO_COUNT`
     (0x2000) = compteur GELÉ** (blank). Net sur 34→43 = **−268 o** (vs nominal 1440) → **décale TOUT
     l'affichage en dessous** (lignes 43-255 ont un stride +160 lisse mais une base décalée de −268). La
     question ouverte : ce −268 (surtout les 2 lignes NO_COUNT) est-il FIDÈLE à Hatari, ou NeoST
     mé-interprète-t-il la séquence res-pulse d'EL ? Le state-machine Glue est fidèle BRANCHE par branche
     (agent), mais la SÉQUENCE complète res-pulse→NO_COUNT n'est pas vérifiée à l'oracle.
   - ⛔ **ORACLE EL EN JEU = BLOQUÉ headless** : Hatari `--cmd-fifo keydown` prend un SCANCODE ST (pas le
     joystick) ; le mapping `--joy0 keys` lit des touches SDL (absentes en vidéo dummy) → **impossible
     d'injecter le FEU joystick d'EL** → EL reste au logo Thalion dans Hatari (vérifié : log « keydown
     1073742052 isn't a valid key scancode » + reste 43 couleurs = logo). ⇒ pas de comparaison d'adresses
     EL-en-jeu directe.
   - ✅ **CAUSE RACINE TROUVÉE (2026-06-17, banc synthétique `tools/make_respulse_test.py`)** : c'est la
     **GÉOMÉTRIE PAR LIGNE (V3)**, pas la Glue, pas le stride seul. Le banc (HBL handler faisant
     res=02/res=00 en fin de ligne par ligne, boote dans NeoST ET Hatari SANS input) reproduit la
     divergence : **screenshot NeoST≠Hatari 40-49 %**. Mesure décisive — distribution de LONGUEUR DE
     LIGNE (deltas de cycle-HBL Hatari) = **{512: 294, 224: 17}** : Hatari **RACCOURCIT 17 lignes en
     hi-res (224 cyc)** quand l'impulsion res aligne, tandis que **NeoST VERROUILLE la géométrie par
     trame** (512 cyc partout, `frameMode_`/`geometry()`) → 0 ligne raccourcie. Le delta de longueur
     (224 vs 512 → ±80 o/ligne sur 17 lignes) décale les adresses → corruption. `NEOST_V2` (raccourci
     sur impulsion hi-res PRÉCOCE ≤56) NE couvre PAS l'impulsion FIN-DE-ligne → 42 % (n'aide pas).
   - **LE FIX = chantier V3** : raccourcissement de ligne PAR LIGNE sur impulsion res (couplage
     Shifter↔Scheduler pour reprogrammer le HBL à 224, modèle d'adresse/longueur par ligne au lieu de
     la grille trame figée). ⚠ ARCHITECTURAL et RISQUÉ (le HBL ancre top/bottom + Timer-B ; cf.
     [[beamsync-busalign-falsified]] §6, `setHblShorten`/V2 existant à étendre du cas précoce au cas
     fin-de-ligne). **Validation = le banc synthétique** (`make_respulse_test.py` → screenshot vs
     `hatari_oracle.sh`, viser 0 %, + distribution longueur 224/512 = Hatari) PUIS EL en jeu. ⚠ NE PAS
     ajouter de branche Glue (la Glue est fidèle). Cf. [[enchanted-land-glue-live]], [[v2-resswitch-validated]].
2. **LX jitter de titre** (~1.5 %, subtil ; LX rend déjà) ; **Cuddly menu robot** / **SHO course**
   (inatteignables headless → navigation requise).
3. **E-clock @ IACK** (poll-beat période-3 vs Hatari période-5) — RAFFINEMENT de phase ; n'a PAS
   amélioré le screenshot ni les jeux → faible priorité. Si repris : éditer `execInterrupt<C68000>`.
4. **RAM_SLOT default-on ?** — faithfulness pure ; re-baseliner les réf-étalons SELF à l'oracle (zone
   active déjà 0 px). Puis retirer les hacks redondants (`NEOST_VC_WAIT`, `kSyncWriteOffsetCyc`).

### (archive) Sous-problèmes d'entrée d'IRQ — désormais RAFFINEMENTS, plus des blocages

1. **E-clock @ IACK ne compose pas encore avec RAM_SLOT** : poll-beat reste période-3 `{0,4,8}` vs
   Hatari période-5 `{0,4,8,12,16}` (= E-clock mod-10 × créneau mod-4 = mod-20). En NeoST, RAM_SLOT
   ABSORBE l'E-clock (appliqué dans willInterrupt, trop tôt). Le fix faisable maintenant (fork Moira
   committable) = éditer `execInterrupt<C68000>` (MoiraExceptions_cpp.h:533-543) pour insérer le wait
   E-clock + bloc occupé `CPU_IACK_CYCLES_VIDEO_CE(10)+idle(4)` AU point d'IACK (entre write PClo et
   read vecteur), comme Hatari `iack_cycle` → les accès suivants (SR/PChi/vecteur/prefetch) s'alignent
   à la phase POST-E-clock → mod-20 émerge. (Même leçon que le fix DIV : l'ORDRE idle↔prefetch fait la
   phase du créneau.) ⚠ Le setClock depuis readIrqUserVector est PERDU (mid-accès) → passer par un
   hook dédié dans execInterrupt, pas readIrqUserVector.
2. **Datation dispatch sync-driven** : EL atteint sa boucle beam-sync `$EE78` (`move.b $8209,d1 / bne`)
   **~50 lignes trop tard** (bordure basse, compteur figé 0x2c → spin infini ; render/glue NORMAL).
   ~50 lignes ≠ un ±2 cyc : c'est la PHASE D'ENTRÉE de trame d'EL décalée par le sync-driven (PT=true ;
   à PT=false EL marche). À root-causer : par quel IRQ (VBL/Timer-B) EL entre `$EE76`, et pourquoi son
   dispatch est ~50 lignes tard sous sync-driven. C'EST le blocage des jeux.

Puis **retirer les hacks** (`NEOST_VC_WAIT=2`, `kSyncWriteOffsetCyc=+16`) devenus redondants et
**recalibrer à l'oracle** (zone active + EL/LX/Cuddly/SHO + poll-beat).

Cf. [[beamsync-busalign-falsified]], [[eclock-convergence-validated]] (CORRIGÉE ici),
[[sync-driven-scheduler-falsified]], [[v2-resswitch-validated]].
