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

## 5. Prochaine étape — convergence INSTRUCTION systématique (le vrai « full »)

Piloter le **différentiel** vers 0 sur un large jeu de motifs (comme `bra` 10→12), pas le
poll-screenshot. Pour chaque `DIFF`, corriger la DATATION dans `NeostMoira` (datation d'accès,
créneaux, prefetch). Candidats : sweep des classes d'instructions (MOVE/ALU/Bcc/JMP/JSR/accès
RAM vs MMIO), boucles réelles LX `$14ef6` / EL `$ee78`. Une fois le timing instruction = WinUAE
(différentiel vert partout), **réactiver l'E-clock @ IACK** (la phase absolue matchera enfin) puis
**retirer les hacks** (`NEOST_VC_WAIT=2`, `kSyncWriteOffsetCyc=+16`, `syncCpuBus`) et **recalibrer
à l'oracle** (zone active + EL/LX/Cuddly/SHO). Bon candidat **workflow** (fan-out par classe d'instr).

Cf. [[beamsync-busalign-falsified]], [[eclock-convergence-validated]] (CORRIGÉE ici),
[[sync-driven-scheduler-falsified]], [[v2-resswitch-validated]].
