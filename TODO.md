# TODO — NeoST

(c) 2026 VERHILLE Arnaud. **Ce qui reste à faire.** Le fait (implémenté + validé) est dans
[`CHANGELOG.md`](CHANGELOG.md) ; les diagnostics de bugs en cours sont en mémoire projet.

**Objectif** : émuler proprement un **MegaSTE** (68000 8/16 MHz, 1/2/4 Mo, TOS 2.05/2.06, STE
vidéo/son/joypads, blitter, RTC, SCC, SCU, ACSI/SCSI, DD/HD) avec un timing assez fidèle pour
jeux, démos et utilitaires.

**Sources de vérité à croiser systématiquement** (cf. [`CLAUDE.md`](CLAUDE.md)) :
- **Hatari** (`extern/hatari/src/*.c`) — comportement ST/STE/MegaSTE éprouvé. La référence.
- **MAME** (`src/mame/atari/atarist.cpp`, `stmmu.cpp`, `stvideo.cpp`, devices `mc68901`,
  `wd_fdc`, `6850acia`, `z80scc`, `rp5c15`, `ay8910`, `lmc1992`) — composants séparés.

**Documentation connexe** :
- Précision cycle (modèle, acquis, restant) → [`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md)
- Beam-sync (front actif, convergence Moira↔WinUAE) → [`docs/MOIRA_WINUAE_CONVERGENCE.md`](docs/MOIRA_WINUAE_CONVERGENCE.md)
- Divergences NeoST↔Hatari (inventaire maître) → [`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md)
- Logiciels étalons par sous-système → [`docs/TEST_SOFTWARE.md`](docs/TEST_SOFTWARE.md)

---

## Catalogue logiciels — bugs en cours

Rapports terrain. TOS 1.02fr sauf mention contraire. Chemins sous `disks/st/` (`.st`) ou
`disks/stx/` (`.stx`). Pilotage headless : `--keys`/`--joy-at`, trace `--irq`, diff Hatari.

| Jeu | Symptôme | Piste / renvoi |
|-----|----------|----------------|
| **Arkanoid (1987)** (Imagine) | Atteint l'écran-titre mais ne franchit pas la partie (boucle `$31736`/`$26E7`). | Gel FDC `$31736` **résolu** (modèle rotationnel) ; reste une cause distincte (protection ? 2ᵉ chargement ? IRQ ?). 🎯 étalon FDC/protection. |
| **Captain Blood (1988)** (ERE) | Arrive au jeu puis plante sur une erreur clavier et redémarre. | ACIA/IKBD ou timing. |
| **Enchanted Land (1990)** (Thalion) | ✅ Refonte 2026-07-02 : bordure haute **stable** (38-40/40), paysage net, loader réparé. Reste : moteur fullscreen verrouillé 72 % (vs 100 % Hatari, impulsions freq à −16) → micro-sauts de scroll + bande corrompue. Son Thalion absent (bouton joystick). | Résidu = pièce **vidéo/entrée-HBL in-game** (+4 res / −16 freq vs oracle) → `docs/MOIRA_WINUAE_CONVERGENCE.md` (état 2026-07-02). Le CPU-beam est réglé (poll-bench 180/180, loader byte-exact). |
| **Lethal Xcess** (`.STX`, TOS 1.62) | ✅ **RÉSOLU (2026-07-02)** : titre propre ET **parfaitement stable** (0,00 % de churn trame-à-trame, était ~1,5 %) après la refonte beam-sync coordonnée. | — |
| **The Cuddly Demos** (TCB) | 1ʳᵉ page OK ; menu robot : scrolling bugué qui saute + scroller de bordure **basse** non rendu. | Beam-sync par-ligne + rendu live retrait bas. ⚠ menu inatteignable headless. |
| **Shadow Warriors** (2Hot2Handle) | Après SPACE : titre + musique OK ; le bouton joystick ne lance pas le jeu. (Castle Warrior, lui, fonctionne.) | À diff'er Hatari. |
| **Rick Dangerous II (1989)** (Core) | SPACE, `n`, `n` : plante avec 4 bombes. | À diff'er Hatari. |
| **Wings of Death** (`.stx`) | Après bouton : titre **corrompu** + son ralenti ; SPACE lance le jeu, qui tourne ensuite très bien. | Corruption titre (vidéo) + son chargement. |
| **Stardust (1994)** / **Stardust Bloodhouse** (`.STX`) | Plante sur écran noir au démarrage. | À diff'er Hatari. |

> **Récemment résolus** (passés au CHANGELOG) : **Super Hang-On** (bruit blanc → filtre YM
> LowPass STF ; lignes colorées → rendu raster `PAL_SNAP` ; FDC 9→10 spt) ; **Rick Dangerous**
> (1, exception erreur d'adresse) ; **Castle Warrior**, slideshow Spectrum 512.

---

## 🔬 Divergences Hatari restantes

**Inventaire maître** (sévérité + impact + `fichier:ligne` des deux côtés, 4 passes d'audit) :
[`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md). Fidélité globale **très élevée** ;
aucune divergence ne casse un boot EmuTOS/`.ST`. Le terrain **logique** est épuisé (tous les
écarts bornés et vérifiables sans oracle sont corrigés) ; ne restent que les écarts
**cycle-exacts** (ci-dessous) et quelques cas-limites documentés.

### 🔮 À reprendre une fois l'oracle Hatari bâti (session avec SDL2)

> **Pré-requis : bâtir l'oracle headless.** `extern/hatari/` est cloné. Installer SDL2, puis
> `cmake -S extern/hatari -B extern/hatari/build -DCMAKE_BUILD_TYPE=Release -DENABLE_SDL2=1 &&
> cmake --build extern/hatari/build -j` → `extern/hatari/build/src/hatari`. Recette de
> comparaison cycle-exacte → [`docs/HATARI_AUTOMATION.md`](docs/HATARI_AUTOMATION.md).

Items qui exigent l'oracle pour être traités/validés (par priorité d'impact) :

1. **[JOUEUR] Beam-sync** — phase CPU↔faisceau **par-ligne** (overscan vertical). Casse EL /
   Cuddly / SHO en jeu. → `docs/MOIRA_WINUAE_CONVERGENCE.md`, `docs/CYCLE_ACCURACY.md` §4.
2. **[VIDÉO]** V1 branche STE de la Glue · V2 tricks par changement de résolution (overscan
   med-res, scroll hardware) · V3 géométrie mid-trame (50↔60 Hz, `RestartVideoCounter`).
3. **[SON]** S2 FIFO 8 octets DMA + avance HBL · S3 gain LMC ½-amplitude (~6 dB) — validables
   par dump WAV oracle.
4. **[FDC]** D3 stall FIFO 32 cyc · drive/side « push » — validables par trace FDC byte-exacte.
5. **[MFP]** M1 GPIP on-chip via machine de fronts AER/DDR · `UpdateTimers` avant lecture IPR.
6. **[FPU]** arrondi de précision FMOVE/FABS/FNEG selon FPCR — validable par ROM de test étendue.

**Faisables sans oracle** : FPU packed decimal bit-exact ; GEMDOS recomposition Unicode NFD→NFC
(cible macOS) — détaillés dans `docs/HATARI_DIVERGENCES.md`.

**Décisions actées (NE PAS « corriger » vers Hatari)** : SCC `WR14` bit4 loopback (datasheet
Zilog, NeoST plus fidèle) ; WRITE/READ TRACK STX réinterprétés (NeoST rend la piste lisible) ;
densité HD/ED STX (NeoST plus cohérent) ; RTC en temps émulé (déterminisme headless).

---

## 🎯 Précision cycle

> **Plan, acquis et inventaire priorisé du restant** → [`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md).
> **Front actif (beam-sync, convergence Moira↔WinUAE)** → [`docs/MOIRA_WINUAE_CONVERGENCE.md`](docs/MOIRA_WINUAE_CONVERGENCE.md).

État résumé (détails et pistes éliminées dans les deux docs ci-dessus) :

- ✅ **Convergence INSTRUCTION** Moira↔WinUAE = COMPLÈTE (`NEOST_RAM_SLOT` align créneau bus +
  fix DIV ; harnais différentiel `NEOST_TRACE_CYC` + `trace_diff.py --periods`).
- ✅ **RAM_SLOT+IACK défaut ON** : ensemble, ils déclenchent l'overscan beam-sync (dérive faisceau
  +78/ligne = Hatari). Réparent l'overscan **Lethal Xcess** ; toggle `=0` pour A/B.
- ✅ **Deadlock Enchanted Land = résolu** : dispatch BLOC par défaut (le sync-driven
  mid-instruction était net-négatif → opt-in `NEOST_SYNC_DISPATCH`). Intro propre.
- ✅ **Refonte beam-sync COORDONNÉE = EXÉCUTÉE (2026-07-02)** — le résidu +24 est **attribué et
  corrigé** (+8 IACK sur-compté → hooks au point d'IACK ; +2 alignement mi-accès → début d'accès ;
  −8 origine d'horloge trame → read −14 / write −6 calibrés à l'oracle ; HBL à 512). Les rustines
  (`+16`, read `+4`) sont RETIRÉES. + 2 bugs structurels réparés : commit compteur à DE_end (loader
  EL bloqué) → commit paresseux ; `Video_RestartVideoCounter` (ligne 310) porté (event VC_RESTART).
  Bordure haute EL **stable**, LX titre **0,00 % churn**, poll-bench 180/180, étalons TOUS OK
  (`overscan_top` re-baseliné, l'ancienne réf était fausse de 24 px vs Hatari).
  → Détails + état : `docs/MOIRA_WINUAE_CONVERGENCE.md` (bloc 2026-07-02).
- ◑ **Reste (pièce vidéo, plus CPU)** : le moteur fullscreen d'EL en jeu verrouille à 72 % avec
  ses impulsions freq à −16 vs l'oracle (ratent la fenêtre bordure-droite) → micro-sauts de
  scroll. Piste : entrée du handler HBL in-game (+4 constant sur sites res). Oracle in-game
  opérationnel (cmd-fifo, recette dans le doc).
- Acquis (cf. CYCLE_ACCURACY §3) : phases 0-6, latch palette Spec512, alignement bus shifter +
  wait states PSG/MFP/ACIA, machine Glue live, VDE_On live, Spec512 pixel-perfect, bordures H/B/G/D.

---

## Roadmap par sous-système — items ouverts

> Le reste (Bus/MMU, FDC, YM2149, GEMDOS, ACSI, SCC, FPU, imprimante, MegaSTE 8/16 MHz + cache…)
> est **fait et validé** — voir `CHANGELOG.md`. Ci-dessous, uniquement ce qui reste ouvert.

### Vidéo / Shifter
- **Raffinements cycle-exact** (→ `docs/CYCLE_ACCURACY.md` §4) : beam-sync par-ligne, tricks par
  changement de résolution (V2 hi/med/lo, overscan med-res), géométrie mid-trame (V3), rendu live
  du retrait **bas** (scroller Cuddly) + lignes EMPTY/BLANK/NO_DE, mode 336 px STE
  (`bSteBorderFlag`), wakeup-state WS3 (sous-pixel).

### Son DMA STE
- **FIFO 8 octets + avance HBL** (`DmaSnd_FIFO_*`, S2) + **compteur d'adresse live**
  (`$FF8909/0B/0D` au cycle) — refinement sous-perceptuel (la timeline horodatée capte déjà les
  modifs intra-trame à la granularité trame). _Effort moyen, valeur basse._

### Stockage & contrôleurs
- **SCSI / NCR5380** (MegaSTE/TT) *(gros contrôleur)* — réf. `ncr5380.c`. Non commencé.
- *(SCC : restes faible valeur — timers du BRG / Zero Count, baudrate temporisé, série hôte.)*

### Périphériques & profils machine
- **ROM TOS MegaSTE** : TOS 2.05/2.06 256 Ko à `$E00000` (choix pays, checksums, fallback EmuTOS
  MegaSTE). Aujourd'hui : EmuTOS 256 Ko par défaut.
- **NVRAM / préférences TOS MegaSTE** (résolution / boot device) si TOS 2.x l'exige.
- **Cartridge port** `$FA0000-$FBFFFF` générique (au-delà du système GEMDOS) — réf. `cart.c`.

### Outillage / qualité
- **Étalons headless** : calibrer frames + références Cuddly / Union / Troed / Hatari Test Suite ;
  rapatrier Union (planetemu manuel). Infra en place (`tools/run_etalons.py`).
- **Comparaison MAME ↔ NeoST** (memory map, bus errors, FDC/MMU FIFO, blitter, SCC).
- **Matrice de compatibilité MegaSTE** : TOS 2.05/06, EmuTOS, 1/2/4 Mo, 8/16 MHz, cache on/off,
  DD/HD, mono/couleur.
- Capturer des **traces Hatari de référence** pour `trace_diff` (Arkanoid & co).
