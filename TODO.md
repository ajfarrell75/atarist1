# TODO — NeoST

(c) 2026 VERHILLE Arnaud. **Ce qui reste à faire — uniquement l'OUVERT.**

- Ce qui est fait, par puce → [`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md)
- Titres déjà diagnostiqués (corrigés **ou** jugés fidèles) → [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md)
- Chronologie (le clos détaillé vit là-bas, y compris les chantiers retirés d'ici) → [`CHANGELOG.md`](CHANGELOG.md)

**Objectif** : émuler proprement un **MegaSTE** (68000 8/16 MHz, 1/2/4 Mo, TOS 2.05/2.06, STE
vidéo/son/joypads, blitter, RTC, SCC, SCU, ACSI — le disque interne d'époque est un pont
ACSI-SCSI, PAS un NCR5380, DD/HD) avec un timing assez fidèle pour jeux, démos et utilitaires.
**Atteint le 2026-08-27** au sens du diagnostic Atari Field Service : suite Q **12/12** sous
TOS 2.06 (boîtier de test DMA du kit émulé, `--dma-fixture`) ; ce qui suit affine la
fidélité, il ne conditionne plus l'objectif. ⚠ Cette validation est **manuelle** — la
transformer en étalon automatisé est l'item **A25** ci-dessous.

**Sources de vérité à croiser systématiquement** (cf. [`CLAUDE.md`](CLAUDE.md)) :
- **Hatari** (`extern/hatari/src/*.c`) — comportement ST/STE/MegaSTE éprouvé. La référence.
- **MAME** (`src/mame/atari/atarist.cpp`, `stmmu.cpp`, `stvideo.cpp`, devices `mc68901`,
  `wd_fdc`, `6850acia`, `z80scc`, `rp5c15`, `ay8910`, `lmc1992`) — composants séparés.

**Documentation connexe** :
- Précision cycle (modèle, acquis, restant) → [`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md)
- Beam-sync (convergence Moira↔WinUAE — chantier CLOS, journal) → [`docs/MOIRA_WINUAE_CONVERGENCE.md`](docs/MOIRA_WINUAE_CONVERGENCE.md)
- Divergences NeoST↔Hatari (inventaire maître) → [`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md)
- Logiciels étalons par sous-système → [`docs/TEST_SOFTWARE.md`](docs/TEST_SOFTWARE.md)

---

## 🚨 BLOQUANT RELEASE — contenu sous copyright suivi par le dépôt (2026-08-01, recompté 2026-08-27)

Le dépôt `habib256/neost` est **public** (GPL-3.0, GitHub Pages actif) et `git ls-files`
suit :

| Chemin | Contenu | Volume |
|--------|---------|--------|
| `roms/` | **37 images TOS Atari propriétaires** (`tos100*` → `tos402`, `TOS v1.02 …[MEGA TOS]`) | ~11 Mo |
| `disks/st/` (52), `disks/stx/` (16) | 68 images de **jeux commerciaux**, majoritairement CRACKÉS (mentions `[cr Replicants]`, `[cr Elite]`, `[cr Medway Boys]`…) | ~51 Mo |
| `carts/` | 5 cartouches **Atari Field Service** (`ST_Diagnostic_v4.4`, `MegaSTE_Diagnostic_v1.5`, `STE_Test_v1.9`…) | |
| `dev/` | tiers commis : `dev/agt` (« aucun fichier LICENSE explicite »), `dev/reservoir-gods/` sans licence avec `.exe` précompilés et une `license.txt` UnRAR (non libre) | ~50 Mo |

Conséquences : cloner le dépôt (ou télécharger le tarball GitHub) livre une archive de
logiciels sous copyright — et **GitHub Pages sert `main` à la racine**, donc tout est aussi
téléchargeable depuis le web. Verrous techniques levés (Pages libre-seulement, étalons
découplés, `rom_is_free()`, licences gardées par 8 jobs) → détail au `CHANGELOG.md`.

**Séquencement de la purge** (l'ordre casse la circularité « purger ampute le filet ») :

1. **A10 d'abord** — convertir les 7 étalons pixel encore adossés à des ROM/disques
   propriétaires (§ Dette d'architecture). C'est LE verrou qui rend la purge coûteuse.
2. **Débrancher le palier `fast` des fichiers propriétaires** (nouveau, audit 2026-08-27) :
   `run_selftests.py` (`diag_cart` → `tos102uk`), `run_cyclebench.py` (`tos102uk` codé en
   dur) et `run_midi_sequencer.py` (`tos104fr` + **Cubase Lite**, un logiciel commercial
   lui aussi suivi par git) tomberaient en **rouge dur** le jour de la purge — la politique
   « ROM Atari absente = SKIP recensé » de `run_etalons.py` ne les couvre pas. Migrer sur
   EmuTOS quand possible, sinon appliquer la même politique de SKIP recensé.
3. **La purge elle-même** (décision du mainteneur — réécriture d'historique) :
   `git rm --cached` sur `roms/tos*`, `disks/st`, `disks/stx`, `carts/`,
   `dev/reservoir-gods`, `dev/agt`, ajout au `.gitignore`, puis `git filter-repo` — sans
   quoi le contenu reste téléchargeable dans les commits antérieurs.
4. **Basculer le défaut des paquets** : `NEOST_PACKAGE_NO_ATARI_TOS=1` (l'interrupteur
   existe et la CI l'honore ; aujourd'hui les paquets bureau redistribuent `tos102uk.img`
   et `tos162uk.img` par défaut).
5. **Compléter le tableau des composants tiers du README** : Atari (tant que des TOS sont
   livrés), **libmt32emu (LGPL 2.1+, lié statiquement — l'omission n'est pas anodine)**,
   stb_image, libslirp, SDL2 (Android).

Conformité annexe (non bloquante) :
- `packaging/linux/make_appimage.sh` tire `linuxdeploy`/`appimagetool` depuis le tag mouvant
  `continuous` sans somme de contrôle pour arm64, alors que le `Dockerfile.bionic` épingle
  par SHA256.
- `.dmg` macOS ni signé ni notarisé (Gatekeeper : « NeoST est endommagé ») ; `.zip` Windows
  non signé. À traiter **après** la purge (A37) — signer un paquet contenant des ROM Atari
  n'aurait pas de sens.

---

## 🏛 Dette d'architecture — état et plan de correction

**Audit quatre dimensions du 2026-08-27** (cœur, frontends, tests/CI, fidélité/gouvernance —
constats, notes et trouvailles → `CHANGELOG.md` à cette date). La revue A1-A8 du 2026-08-25
est **soldée** (détail → `CHANGELOG.md` 2026-08-26). Les leçons de méthode du 2026-08-25
(seuil absolu sur grandeur dépendante de la charge ; justesse validée sans mesure de coût)
sont archivées au `CHANGELOG.md` — ne pas les recommettre.

### Hérités encore ouverts (A3, A9-A15)

- **A3 ◐ — Le corpus de régression n'est pas livrable.** La couverture repose sur les 68
  jeux crackés et 37 ROM propriétaires du § BLOQUANT. Recompté 2026-08-27 : **8 étalons
  pixel sur 15** survivraient au retrait des TOS Atari (`etos_ste_boot`, `overscan_top`,
  `trace_odd`, `scroll_8264`, `scroll_8265`, `blitter_timer`, `blitter_hog`, `mfp_poll`) ;
  les 7 restants sont le plan **A10**.
- **A9 ⭘ — `src/main.cpp` est un monolithe** (mesuré 2026-08-27 : **5 017 lignes**, dont
  `main()` = **2 421 lignes** avec une boucle de ~1 530 lignes et **84 globaux `g_*`**).
  La dette est confinée (`neost_core` reste sans GUI). ✅ (a) fait le 2026-08-27 : le
  boot GUI (400 trames + capture, sauté-et-dit sans affichage) est une étape de
  `run_all.py`, et `--run-frames` est devenu un vrai mode harnais (gel central de
  `saveConfig` + `imgui.ini` : un run de test ne laisse AUCUNE trace — le boot GUI
  réécrivait `rom=`/`rtc=` du développeur au premier essai). Reste (b) : découper —
  une `struct App` absorbant les globaux, `main()` sous 300 lignes, en généralisant
  le pattern de requêtes de `MediaPages`. Ne PAS refondre la boucle sans ce filet.
- **A10 ⭘ — Convertir les 7 étalons adossés à des ROM propriétaires** :
  `spectrum512_diapo`, `spectrum512_diapo2`, `spectrum512_diapo_ste`, `cuddly_demos`,
  `union_demo`, `nocooper`, `nocooper_greetings`. Deux recettes éprouvées, au choix par
  étalon : migration EmuTOS (capture EmuTOS vs TOS propriétaire = 0 px, contrôlée à
  l'oracle) ou étalon généré (esprit `tools/make_blitter_test.py`).
- **A11 ⭘ — L'oracle ne tourne dans aucune CI.** Job planifié ou manuel (pas au push) qui
  clone Hatari au pin via `tools/setup_hatari.sh`, régénère les captures des étalons
  `ref_kind: oracle` et compare aux réfs commises. Fermerait la dernière boucle de
  validation entièrement manuelle. À la même occasion : **statuer sur les deux étalons
  `snapshot` à écart oracle mesuré et inexpliqué** (`overscan_top` : 194 px,
  `trace_odd` : 22 px — la suite garde NeoST contre lui-même sur ces deux cas).
- **A12 ⭘ — Aucune cible de livraison validée sur du matériel réel.** Windows jamais lancé
  hors CI, APK Android jamais posé sur un appareil (QEMU seul), aucun budget temps réel
  mesuré sur le Raspberry Pi visé (le perfbench ne garde que des ratios sur le poste de
  dev). Une passe de validation PAR CIBLE, consignée avec sa config — de la mesure, pas
  du code.
- **A13** = save-states × GEMDOS HD → § *Périphériques & profils machine*.
- **A14** = garde lecture-seule des balayages de masse → § *Outillage / qualité*.
- **A15** = DSL d'injection sans token « mouvement bouton tenu » (pas de DRAG GEM).

### P1 — corrections rapides : ✅ SOLDÉ le 2026-08-27 (A16-A24)

Détail et verdicts → `CHANGELOG.md` (2026-08-27). Reliquat ouvert né d'A16 :

- **A16b ⭘ — `NEOST_LINELEN_ATTR` (attribution Shifter à la grille réelle) segfaute
  sous `--glue-selftest`.** Découvert en tentant d'unifier le défaut : le chemin
  expérimental V3 (les 4 sites `glueLineStart_` du Shifter) crashe quand il est armé
  hors trame réelle — il crashait déjà via la recette d'A/B documentée
  `NEOST_LINELEN=1` AVANT la séparation des verrous. À corriger avant toute
  promotion du chantier V3 (probable : `glueLineStart_` vide/désynchronisé dans le
  selftest). La config validée en production est Machine-ON/Shifter-OFF.

### P2 — consolidation (quelques jours chacun, par opportunité)

- **A25 ⭘ — Étalon MegaSTE automatisé.** L'objectif de tête du projet — suite Q 12/12 — a
  été validé À LA MAIN, non gardé contre la régression ; `tools/etalons.json` avertit
  lui-même que MegaST/MegaSTE/TOS 2.06 ne sont couverts par AUCUN étalon. Rejouer la
  suite Q en headless (verdicts écran/série, `--dma-fixture`, `--loopback-at`) et
  l'ajouter à un palier.
- **A26 ⭘ — Passe de péremption documentaire + contrôle automatisé des affirmations
  chiffrées.** Le TODO a été purgé le 2026-08-27 (huit affirmations périmées corrigées,
  cf. `CHANGELOG.md`) ; restent `docs/CYCLE_ACCURACY.md` §4 (statuts V1/V2/WS/blitter
  antérieurs aux portages), `docs/HATARI_DIVERGENCES.md` (borne MFP « ≤ 1 instruction »
  contredite par la mesure 157 cycles consignée ailleurs dans le même fichier) et
  `docs/MOIRA_WINUAE_CONVERGENCE.md` (journal par accrétion : poser un « état courant »
  daté en tête plutôt que d'exiger la lecture chronologique des 690 lignes). 🎯 Étendre
  l'esprit de `check_doc_anchors.py` aux **chiffres vérifiables** (nombre d'étalons,
  lignes de `main.cpp`, fichiers suivis par git) : `check_doc_claims.py`.
- **A27 ⭘ — Palier `pixel-fast` + parallélisation.** Le palier `fast` (4,8 s mesuré
  2026-08-27) ne compare AUCUN pixel : la boucle rapide est aveugle au rendu, la règle
  « avant de conclure, `--tier full` » est une discipline humaine, pas un garde-fou.
  3-4 étalons courts (`overscan_top`, `blitter_timer`, `scroll_8264`, `trace_odd` ≈ 4 s)
  en palier intermédiaire ; et paralléliser `run_etalons.py` (palier pixel : 73 s dont
  ~50 s pour le seul `nocooper_greetings` — les étalons sont indépendants).
- **A28 ⭘ — Sortir le servo audio et la cadence dans le cœur.** Le filtre proportionnel
  d'asservissement (même constante `/256`, même clamp ±8, même rampe anti-clic) existe en
  **trois copies** (GUI, web, android) et la boucle de rattrapage de cadence aussi ;
  `kCpuHz` est déclarée quatre fois. Un `AudioPacer`/`FramePacer` dans `neost_core` —
  même recette que `AudioMix`, dont la copie web avait déjà divergé.
- **A29 ⭘ — Étendre le patron « puce nue + Scheduler » au Blitter, DmaSound et Fdc.**
  `selftest_logic.cpp` le fait déjà pour YM2149, MFP+ACIA et RTC : c'est l'étage manquant
  entre la logique pure et le pixel. Sans lui, chaque régression pixel est une enquête
  (« 3 400 px divergents à (112,57) ») là où une table de vérité dirait « 4 cycles de
  trop en mode HOG ».
- **A30 ⭘ — Fuzzing des parseurs d'images disque.** `decodeMsa`/`decodeDim` et
  `StxImage::parse` sont des fonctions pures `octets → bool` : un harnais libFuzzer coûte
  une soirée. Le bornage manuel est déjà excellent (il corrige même une lecture hors
  bornes présente dans Hatari) — le fuzzing le prouverait et le garderait.

### P3 — chantiers structurels (UN à la fois, jamais combinés)

- **A31 ⭘ — Interface `MmioDevice` + table de plages.** Ajouter une puce = 6 points de
  modification dont les deux chaînes de `if` de ~110 lignes de `Bus::mmioRead8` /
  `Bus::mmioWrite8`, à ordre sémantique implicite (l'ISP1160 doit précéder la NE2000 —
  documenté dans l'en-tête, invisible dans le dispatch). Une table triée rend l'ordre
  explicite et vide les deux fonctions.
- **A32 ⭘ — Découper `Shifter` (2 854 lignes, ~90 champs, 6 rôles)** en Shifter
  (registres + rasterisation) / VideoGlue (machine à états DE/HBL/bordures —
  `updateGlueState` fait 278 lignes) / VideoCounter (`videoCounter` fait 200 lignes et
  mute l'état à travers 4 `const_cast`). Rendrait aussi son nom à `Glue.hpp`, réduit à un
  stub de 31 lignes pendant que le vrai GLUE vit dans le Shifter.
- **A33 ⭘ — Lever le mono-instance CPU.** `Cpu68k.cpp:g_bus`/`g_moira`/`g_sched` sont des
  globales ; la classe jette sur une seconde instance. C'est le plafond qui interdit le
  test unitaire d'une `Machine` (686 lignes de tests pour 40 500 de source), l'A/B en un
  processus et tout parallélisme. À faire APRÈS A31, qui en réduit le rayon.
- **A34 ⭘ — Trancher les deux modèles d'exécution.** BLOC (défaut) et SYNC-driven
  (`NEOST_SYNC_DISPATCH`) coexistent dans `Machine::runFrame` et `stepInstruction` ; un
  seul est validé par les étalons, l'autre est une branche morte-vivante. Décider (mesure
  à l'appui), puis supprimer le perdant. Plus largement : sur les **82 variables
  `NEOST_*`** lues par le cœur, isoler celles qui changent le COMPORTEMENT d'émulation
  (`NEOST_V2`, `NEOST_WS`, `NEOST_IACK*`, `NEOST_MFP_EXACT`…) des traces, et faire
  passer les premières en configuration explicite ou les retirer.
- **A35 ⭘ — Le fork Moira n'est pas rebasable.** `extern/moira/NEOST_VENDOR.md` décrit
  les 6 patches locaux mais **n'enregistre ni commit ni tag upstream d'origine** (le
  vendoring de mt32emu, lui, le fait), et l'arbre a été élagué de sa suite de tests
  (`Cputester/`). Noter le pin de départ ; évaluer le rapatriement du Cputester pour
  re-valider les patches hors étalons ST.
- **A36 ⭘ — Chemin de config inadapté à une installation système.** `neost.cfg` est
  cherché en `exeDir/../` (correct pour `build/neost`, douteux pour `/usr/bin`).
  `XDG_CONFIG_HOME` / `%APPDATA%` avec repli sur le comportement actuel.
- **A37 ⭘ — Discipline de release.** Trois tags le même jour (0.5→0.5.2), 0.5.3 sautée
  sans trace, et le travail majeur depuis le 2026-08-23 (MegaSTE 12/12, CAB/theoldnet,
  fenêtres EtherNEC) n'est pas tagué. Taguer, puis signer/notariser `.dmg` et `.zip`
  une fois la purge (§ BLOQUANT) faite.

### Garde-fous du plan (à NE PAS faire)

- **Rouvrir BL5 sans concevoir une 3ᵉ mesure indépendante** : le paradoxe de signe entre
  les deux instrumentations existantes est documenté (`docs/HATARI_DIVERGENCES.md` § BL5,
  6 hypothèses réfutées) — re-mesurer avec les mêmes sondes ne tranchera rien.
- **Combiner A9 + A31 + A32 en un « grand refactor »** : chaque chantier P3 séparément,
  filet de test posé AVANT (A9a, A29).
- **Supprimer un des deux modèles d'exécution sans la mesure d'A34.**

---

## Catalogue logiciels — bugs OUVERTS

Rapports terrain non expliqués. TOS 1.02fr sauf mention. Chemins sous `disks/st/` (`.st`)
ou `disks/stx/` (`.stx`). Pilotage headless : `--keys`/`--joy-at`, trace `--irq`, diff
Hatari.

| Jeu | Symptôme | Piste / renvoi |
|-----|----------|----------------|
| **Shadow Warriors** (2Hot2Handle) | Après SPACE : titre + musique OK ; le bouton joystick ne lance pas le jeu. (Castle Warrior, lui, fonctionne.) | À diff'er Hatari — le pilotage **joystick** de l'oracle est possible (recette A5 → `docs/HATARI_AUTOMATION.md`) ; égaliser la durée d'appui (`--key-hold`). |

Suivis mineurs laissés ouverts sur des cas par ailleurs tranchés :
- **Lethal Xcess** — titre « buggé à ~8 % » constaté en GUI (2026-07-02), probablement la
  même calibration `$8209` que l'in-game déjà réparé ; à re-vérifier en GUI.
- **Stardust STE** — résidu non élucidé du dossier D-PSG (clos) : l'oracle affichait « INSERT
  DISK 2 IN ANY DRIVE » là où NeoST fond au noir puis poll le lecteur B ; à revoir si les
  disquettes 2/3 (absentes du dépôt) réapparaissent.

> ⚠ **Avant de déclarer un bug : vérifier la RAM, puis la ROM.** Le réflexe et les cas
> qu'il a tranchés → [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md). Les « déjà
> expliqués » (Captain Blood, Enchanted Land, Cuddly, Rick Dangerous II, Stardust,
> Spectrum 512 STE, Blood Money, Arkanoid, Wings of Death…) y sont — ne pas rouvrir.

---

## 🔬 Divergences Hatari restantes

**Inventaire maître** (sévérité + impact + `fichier:ligne` des deux côtés) :
[`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md). Fidélité globale **très élevée** ;
**aucune divergence de sévérité haute n'est ouverte** (vérifié entrée par entrée le
2026-08-27). Le terrain logique est épuisé ; le beam-sync joueur (Enchanted Land, Cuddly,
Super Hang-On) est **CLOS** — EL 12402/12402, Cuddly 250/250, SHO résolu, datation re-arm
meilleure que la cible Hatari (2026-07-09 et 2026-08-06, détail → `CHANGELOG.md` et
`docs/MOIRA_WINUAE_CONVERGENCE.md`).

> **L'oracle se bâtit, il n'arrive pas tout seul** : `extern/hatari` est GITIGNORÉ et n'est
> PAS un sous-module — sur une machine fraîche il est ABSENT. `tools/setup_hatari.sh` clone au
> pin (`f0736b2`) et bâtit avec les options macOS obligatoires ; recettes →
> [`docs/HATARI_AUTOMATION.md`](docs/HATARI_AUTOMATION.md).

Restent, par priorité d'impact (toutes de valeur basse à moyenne) :

1. **[VIDÉO]** V3 géométrie mid-trame (50↔60 Hz) : le restart du compteur est porté
   (`VC_RESTART`), reste l'**attribution de ligne** — le chemin expérimental a son
   verrou dédié (`NEOST_LINELEN_ATTR`, OFF) et un segfault à corriger d'abord (**A16b**).
2. **[SON]** quantification HBL du refill FIFO à confronter à l'oracle sur un poll serré de
   `$FF8909/0B/0D` — validable par dump WAV + trace.
3. **[MFP]** `UpdateTimers` avant lecture IPR/ISR/TBDR en mode bloc — retard **mesuré à
   157 cycles** dans le pire cas observé (pas « ≤ 1 instruction »). Le correctif évident
   (dispatch sync-driven) est **réfuté** ; attendre A34.
4. **[FPU]** arrondis de conversion sortante et précision FPCR (détail § Roadmap / FPU).
5. **[BLITTER]** résidu BL5 : ~10 cyc par démarrage de blit + ~3,3 par reprise de tranche,
   **paradoxe de signe non levé** entre les deux instrumentations — aucune correction sans
   3ᵉ mesure indépendante. Hypothèses déjà réfutées (6) → entrée **BL5** de
   `docs/HATARI_DIVERGENCES.md`.

**Faisables sans oracle** : FPU packed decimal bit-exact ; GEMDOS recomposition Unicode NFD→NFC
(cible macOS) — détaillés dans `docs/HATARI_DIVERGENCES.md`.

**Décisions actées (NE PAS « corriger » vers Hatari)** : SCC `WR14` bit4 loopback (datasheet
Zilog, NeoST plus fidèle) ; WRITE/READ TRACK STX réinterprétés (NeoST rend la piste lisible) ;
densité HD/ED STX (NeoST plus cohérent) ; RTC en temps émulé (déterminisme headless).

---

## 🎯 Précision cycle

Convergence **instruction** Moira↔WinUAE : complète (14/14 boucles au harnais différentiel).
Beam-sync : **convergé, transitoire d'entrée inclus** (verdict du 2026-07-09 ; IACK MFP
vectorisé 12→16 cyc le 2026-08-06 a clos Super Hang-On). Le restant est un inventaire de
raffinements à rendement décroissant → [`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md) §4 :
attribution de ligne V3 (= A16b), wakeup-state WS3 sous-pixel, mode 336 px STE
(`bSteBorderFlag`), rendu live du retrait bas, interfoliage blitter, quantification FIFO son.

---

## Roadmap par sous-système — items ouverts

> Le reste (Bus/MMU, FDC, YM2149, GEMDOS, ACSI, SCC, FPU, imprimante, MegaSTE 8/16 MHz + cache…)
> est **fait et validé** — voir `CHANGELOG.md`. Ci-dessous, uniquement ce qui reste ouvert.

### Vidéo / Shifter
- **Raffinements cycle-exact** → § Précision cycle ci-dessus et `docs/CYCLE_ACCURACY.md` §4.
- **Résidu du latch couleur bordure gauche** (le latch lui-même est corrigé, 2026-07-09) :
  16 px (cols 45-60) = la **position horizontale exacte** où l'écriture palette prend effet
  (Hatari bascule ~16 px après le début nominal de l'aire active = latence pipeline ; NeoST
  bascule pile à `activeX_`). Invisible aux étalons. _Valeur très basse._

### Interface — kiosk & effets CRT
- ⭘ **Souris ABSOLUE pour GEM/bureau** (2026-08-27) — la souris ST n'est pilotée qu'en
  mode capturé/relatif (`g_mouseCaptured`, `GLFW_RAW_MOUSE_MOTION`), pensé pour les jeux.
  🎯 Un mode absolu (position curseur hôte → curseur ST, sans capture) pour l'usage
  GEM/desktop/navigateur. (Le symptôme d'origine — « impossible d'ouvrir un dossier sur
  C: » — était l'`EMUDESK.INF` sans `#W`, corrigé → `CHANGELOG.md` ; l'entrée reste comme
  confort, pas comme bug.)
- ⭘ **Trace clavier permanente `NEOST_KBD_TRACE`** (comme `NEOST_ENEC_TRACE`) — éviterait
  le cycle rebuild/revert du 2026-08-27 au prochain doute clavier. _Valeur faible, coût nul._
- Cosmétique : membres `srcW_`/`srcH_` morts dans `CrtEffectStack` ; répétition de
  navigation kiosk (tenir gauche/droite bloque la répétition haut/bas). Limitations CRT v1
  assumées (baril/vignette sur le buffer entier en kiosk ; GL 2.1 → passthrough).

### Son DMA STE
- Quantification HBL du refill FIFO vs oracle (= item 2 du § Divergences). _Effort faible,
  valeur basse._

### Stockage & contrôleurs
- **SCSI / NCR5380** — TT/Falcon **uniquement** (reclassé 2026-08-27 : le MegaSTE n'en a
  pas). Hors périmètre MegaSTE, non commencé. Réf. `ncr5380.c`.
- SCC : restes faible valeur — timers du BRG / Zero Count, baudrate temporisé, série hôte.
- ⭘ **Test F (disquette) de la cartouche STE_Test v1.9 : « Cannot write drive A/B », drives
  vus SS** (constaté 2026-08-27, PRÉ-EXISTANT au chantier MegaSTE). Le test F du diagnostic
  MegaSTE, lui, PASSE (A et B DS, format/écriture/lecture) avec le même FDC émulé : la
  cartouche STE détecte les faces/l'écriture autrement. Trace façon FDC + Hatari en oracle
  sur la même cartouche. _Valeur moyenne._

### FPU MC68881 (audit 2026-07-12 — différés)
- **Arrondis de conversion SORTANTE bit-exacts** : FMOVE.L/W/B (double arrondi 53 bits via
  extToD, INEX2 jamais levé, NaN→0 au lieu du payload) et FMOVE.S/D (mode FPCR ignoré,
  INEX2/UNFL absents, OVFL silencieux en D) → porter `floatx80_to_int32/float32/float64`
  (softfloat.c). FSGLMUL/FSGLDIV : plage d'exposant ÉTENDUE avec mantisse 24 bits → porter
  `roundSigAndPackFloatx80` (softfloat.c:1502).
- **Packed decimal** : ±inf/NaN → exposant $FFF (pas du BCD invalide), INEX1 sur conversion
  inexacte, OPERR si k>17.
- FMOVECR : précision FPCR non appliquée après la table ; offsets indéfinis → table silicium
  (`fpp_cr_undef`) au lieu de 0.0. FMOD précision < étendu : ré-arrondir a (expDiff<−1).

### Périphériques & profils machine
- **A13 — Save-states × GEMDOS HD** : les handles fichiers hôtes ouverts / suivi Pexec de
  `GemdosHd` sont HORS snapshot (bug hunt 2026-07-12, F7) — un état sauvé pendant qu'un
  programme a des fichiers ouverts sur C: donne des handles morts au load. Sérialiser la
  table de handles (chemin + offset + mode) et rouvrir au load. (En attendant, le load est
  refusé si le drapeau GEMDOS diffère — garde en place.)
- **Cartridge port** `$FA0000-$FBFFFF` générique (au-delà du système GEMDOS) — réf. `cart.c`.

### Système de régression — restes
La pyramide P0-P3 est en place (palier `fast` **4,8 s** mesuré 2026-08-27, palier `full` =
pixels ; détail → `DEV.md`). Les manques structurels relevés par l'audit sont au § Dette
(A18, A19, A20, A25, A27, A29, A30). Restent en plus :
- gate `trace_diff --periods` vs oracle Hatari (le cycle-bench actuel est une auto-régression
  NeoST) ;
- self-tests P0 supplémentaires (autres Timers, ACIA) ;
- si une vraie démo spec512 **overscan** (bordures ouvertes) libre est rapatriée un jour →
  l'ajouter en étalon oracle (l'auto_diapo est 100 % borderless).

### Outillage / qualité
- **A14 — Balayage de masse : monter les disques en LECTURE SEULE.** Deux images déjà
  modifiées dans l'arbre git par des runs (Eliminator le 2026-08-25, `diskA.st` par le
  test F du diag le 2026-08-27 — restaurées). Une option `--disk-ro` (ou un `git status`
  systématique en fin de campagne).
- **Étalons headless** : calibrer frames + références Cuddly / Union / Troed / Hatari Test
  Suite ; rapatrier Union (planetemu manuel). Infra en place (`tools/run_etalons.py`).
- **Comparaison MAME ↔ NeoST** (memory map, bus errors, FDC/MMU FIFO, blitter, SCC).
- **Matrice MegaSTE — restes** : combinaisons DD/HD × cache par balayage systématique si un
  jour un titre l'exige (le reste de la matrice est validé → `CHANGELOG.md` 2026-08-27).
- Capturer des **traces Hatari de référence** pour `trace_diff` (Arkanoid & co).
- ⭘ **Hygiène FujiNet — décision de mainteneur** : le code est retiré (2026-08-22), restent
  deux mentions historiques (commentaire de version save-state dans `src/core/Machine.cpp`,
  entrées `CHANGELOG.md`). Reformuler ou assumer — un changelog garde normalement la trace
  de ce qu'il a supprimé.

### Réseau (extensions NeoST — base livrée 2026-08-12, cf. `docs/EXTENSIONS.md`)

> Les chantiers **clos** de ce front (Slirp 5/5 — le coupable était Little Snitch ;
> fenêtres EtherNEC ROM3/ROM4 ; **CAB affiche theoldnet.com** — recettes souris GEM, cache
> CAB, RTO STinG) sont consignés au `CHANGELOG.md` (2026-08-27), recettes incluses.

- **MIDI OUT Windows** : `MidiOutHost` couvre CoreMIDI (macOS) et ALSA (Linux) ; winmm
  reste à écrire — le MT-32 (Munt), lui, est portable.
- **Périphériques des ports — validation** (2026-08-23) : `PortDevices` transcrit
  Steem/WinUAE sans logiciel à clé sous la main. À exercer : Leader Board / 10th Frame
  (dump ST), B.A.T. II, Music Master, et l'option « Pro Sound » de Wings of Death /
  Lethal Xcess (présents en STX) pour entendre le DAC. **Clé Notator** (`--dongle
  notator`) : à confronter à un Notator SL original — deux incertitudes à trancher sur le
  vrai matériel (front de /ROM4 cadençant FEEDB1 ; ordre UDS↔/ROM4 à l'armement). Restent
  sans relevé public : Log 3 (EP330), Pro-24 (GAL16V8), Avalon / Synthworks, Zodiac,
  DynaBlaster. L'outil pour trancher existe : capture matérielle `R3`/`R4`/`U` +
  `--key-replay` (recette → `docs/EXTENSIONS.md`).
- **Dongles — frontends WASM/Android** : `PortDevices`/`CartridgeKey` ne sont exposés que
  par le GUI et le headless ; le menu Android et la démo web n'ont pas de page Dongles.
- **Clé Steinberg — validation** (2026-08-23) : `CartridgeKey` (rouge/noire, équations
  MiSTery) n'a jamais vu un Cubase 3.10 / Score / 2.01 réel — il faut une disquette
  originale (non crackée). Option de confort : choisir une **destination** CoreMIDI
  (`MIDIGetNumberOfDestinations`) au lieu de la seule source virtuelle.
- **NetUSBee — périphériques USB hôte** (2026-08-21) : l'ISP1160 (`io/Isp1160`) est un hub
  racine VIDE ; brancher un clavier/souris HID puis un stockage de masse derrière
  `HcRhPortStatus`. Banc d'essai : pilotes FreeMiNT `netusbee.ucd` + `usb.km`.
- **NetUSBee — fenêtre LSB partagée** : `$FA0000-$FA01FF` = latch ISP1160 ET lecture du
  registre CR NE2000 ; NeoST laisse les deux puces voir l'accès faute de schéma. À
  trancher sur le schéma du NetUSBee (hardware.atari.org), puis ajuster `Bus::read8Slow`.
- **UltraSatan — `US_CONF.TOS` réel** : l'outil de Jookie (ce-atari) compile avec Pure C ;
  le passer sur NeoST pour valider au-delà du programme de test maison. Idem HDDRIVER/ICD
  PRO sur une image 2 slots.
- **EtherNEC — validation TOS 1.04** : le backend réel existe (`SlirpNat`, 5/5) ; reste à
  valider STinG + `ENEC.STX` sous TOS 1.04 (DHCP + ping/GET), consigner dans
  `docs/CASE_STUDIES.md`, et **livrer les pilotes libres GPL** dans les paquets.
- **Modem/STinG** : documenter l'installation STinG (noyau + `sting.inf` dans `AUTO`,
  modules dans `C:\STING`) dans `docs/TEST_SOFTWARE.md` ; banc SLIP bout-en-bout.
- **MIDI ring** : option GUI (saisie du pair) ; test en anneau à 2 nœuds (deux instances
  NeoST — ⚠ bloqué par le mono-instance A33 si c'est en un seul processus).
- **Sécurité** : liste blanche de domaines optionnelle pour les backends sortants.
