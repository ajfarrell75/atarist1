# TODO — NeoST

(c) 2026 VERHILLE Arnaud. **Ce qui reste à faire.**

- Ce qui est fait, par puce → [`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md)
- Titres déjà diagnostiqués (corrigés **ou** jugés fidèles) → [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md)
- Chronologie → [`CHANGELOG.md`](CHANGELOG.md)

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

## 🚨 BLOQUANT RELEASE — contenu sous copyright suivi par le dépôt (2026-08-01)

Le dépôt `habib256/neost` est **public** (GPL-3.0, GitHub Pages actif) et `git ls-files`
suit :

| Chemin | Contenu | Volume |
|--------|---------|--------|
| `roms/` | **44 images TOS Atari propriétaires** (`tos100*` → `tos404`, `TOS v1.02 …[MEGA TOS]`) | ~11 Mo |
| `disks/st/`, `disks/stx/` | ~80 images de **jeux commerciaux**, majoritairement CRACKÉS (mentions `[cr Replicants]`, `[cr Elite]`, `[cr Medway Boys]`…) | ~51 Mo |
| `carts/` | cartouches **Atari Field Service** (`ST_Diagnostic_v4.4`, `MegaSTE_Diagnostic_v1.5`, `STE_Test_v1.9`) | |
| `wasm/index.data` | **artefact de build commis** (reconstruit et recommité par la CI) qui ré-embarque une partie des fichiers ci-dessus | 2,4 Mo |

Conséquences : cloner le dépôt (ou télécharger le tarball GitHub) livre une archive de
logiciels sous copyright.

✅ **Déjà fait** : `deploy-web.yml` est repassé à `NEOST_WEB_FREE_ONLY=ON`, donc Pages ne
sert plus que EmuTOS + `diskA.st`.

✅ **Découplage étalons ↔ ROM propriétaires — FAIT (2026-08-19)**, c'était le verrou qui
rendait le retrait « impossible sans casser la CI » :
- les **4 étalons à disque GÉNÉRÉ** (`overscan_top`, `trace_odd`, `scroll_8264`,
  `scroll_8265`) tournent désormais sur **EmuTOS** (`etos192fr` / `etos256us`). Neutre,
  vérifié : leur secteur de boot est autonome (il pose lui-même résolution, palette et base
  écran), capture EmuTOS vs capture TOS propriétaire = **0 px**, références `tests/reference/`
  **inchangées**, et l'oracle Hatari donne lui aussi **0 px entre les deux ROM** ;
- `run_etalons.py` distingue maintenant **ROM libre** et **ROM propriétaire**
  (`rom_is_free()`) : une ROM `etos*` absente reste un ÉCHEC (dépôt cassé), une ROM Atari
  absente **saute l'étalon et le RECENSE** (bloc « ⚠ NON EXÉCUTÉS — ROM propriétaire
  absente »), sans faux vert ;
- vérifié bout-en-bout : `roms/tos102uk.img` et `roms/tos162us.img` retirés → suite
  **verte** avec 6 étalons explicitement recensés comme non exécutés (spec512 ×3, No Cooper
  ×2, Union), au lieu de 8 échecs. Couverture qui SURVIT au retrait : 7 auto-tests + 5
  étalons machine (ST ×2, STE ×3).

✅ **Licences dans les paquets — FAIT (2026-08-19)** : `stage_free_data.sh` copie
`licenses/{GPL-3.0,GPL-2.0,THIRD-PARTY}.txt` (offre de source incluse), idem pour l'APK
(`packaging/android/stage_assets.sh`), et les **8 jobs de vérification de paquet** de
`release.yml` / `pi-borne.yml` échouent désormais si une licence manque.

❌ **Reste à trancher (décision du mainteneur)** :
1. `git rm --cached` sur `roms/tos*`, `disks/st`, `disks/stx`, `carts/`, `wasm/index.*`,
   les ajouter au `.gitignore`, puis **purger l'historique** (`git filter-repo`) — sans
   quoi le contenu reste téléchargeable dans les commits antérieurs. ⚠ GitHub Pages sert
   la branche `main` À LA RACINE : tout ce contenu est donc aussi téléchargeable **depuis
   le web** (habib256.github.io/neost/roms/…), pas seulement depuis git. Le déploiement
   par artefact réglerait ce point ; écarté le 2026-08-22 (le bundle doit rester dans
   l'arbre de travail). **Plus rien ne s'y
   oppose côté CI** (cf. découplage ci-dessus) ; c'est une réécriture d'historique, donc
   un choix, pas une tâche.
2. **Les paquets bureau redistribuent DEUX ROM Atari propriétaires** (`tos102uk.img`,
   `tos162uk.img`, profils « 520 ST » / « 1040 STE » — `src/main.cpp:1948-1949`). Ce
   n'était PAS écrit ici : la ligne « les paquets bureau étaient déjà propres » était
   fausse, la garde `STRAY` les autorise nommément. L'interrupteur existe désormais —
   `NEOST_PACKAGE_NO_ATARI_TOS=1` produit un paquet 100 % libre (EmuTOS seul), et la CI
   l'honore — mais le **défaut reste inchangé** : le basculer est une décision.
3. `README.md` dit maintenant la vérité (« The packages also carry TOS 1.02 UK and TOS
   1.62 UK ») ; il reste à les faire figurer au **tableau des composants tiers**, qui ne
   mentionne toujours pas Atari.

Autres points de conformité relevés à la même passe (non bloquants mais à traiter) :
- `dev/` (52 Mo de tiers commis) contient `dev/agt` — dont le `NEOST_VENDOR.md` écrit
  lui-même « aucun fichier LICENSE explicite … vérifier les conditions de l'auteur avant
  toute redistribution » — et `dev/reservoir-gods/` sans licence, avec des `.exe`
  précompilés et une `license.txt` **UnRAR** (non libre). Rien de tout cela n'apparaît au
  tableau des composants tiers du README.
- `packaging/linux/make_appimage.sh` tire `linuxdeploy`/`appimagetool` depuis le tag
  mouvant `continuous` sans somme de contrôle pour arm64/Raspberry, alors que le
  `Dockerfile.bionic` les épingle par SHA256.
- `.dmg` macOS ni signé ni notarisé : Gatekeeper affichera « NeoST est endommagé » sans
  que rien ne l'explique à l'utilisateur (documenter `xattr -dr com.apple.quarantine`).
- Le GUI n'a pas de `--help` et avale en silence toute option inconnue commençant par `-`.

---

## Catalogue logiciels — bugs OUVERTS

Rapports terrain non expliqués. TOS 1.02fr sauf mention. Chemins sous `disks/st/` (`.st`)
ou `disks/stx/` (`.stx`). Pilotage headless : `--keys`/`--joy-at`, trace `--irq`, diff
Hatari.

| Jeu | Symptôme | Piste / renvoi |
|-----|----------|----------------|
| **Arkanoid (1987)** (Imagine) | Atteint l'écran-titre mais ne franchit pas la partie (boucle `$31736`/`$26E7`). | Gel FDC `$31736` **résolu** (modèle rotationnel) ; reste une cause distincte (protection ? 2ᵉ chargement ? IRQ ?). 🎯 étalon FDC/protection. |
| **Beyond the Ice Palace** (D-BUG) | Rapport GUI : écran scramblé en jeu. **NON reproduit en headless** : gameplay PROPRE (ST et STE, 1 Mo, boot AUTO). ⚠ Le PRG exige > 512 Ko (BSS dépack 384 Ko → TPA ~471 Ko) : en 512 Ko le TOS skippe l'AUTO — comportement CORRECT (pas un bug). Le chemin GUI = double-clic bureau GEM (Pexec sous AES) ≠ AUTO — à reproduire avec la config GUI exacte. | Recette headless : copier le disque + `mmd ::AUTO` + `mcopy` ; `--mem 1m --keys-at 4000 "n" --keys-at 6200 " " --keys-at 9600 " " --keys-at 12000 "y" --keys-at 14500 "s" --keys-at 16000 " " --keys-at 19600 "y"` → jeu ≈ trame 21000. |
| **Shadow Warriors** (2Hot2Handle) | Après SPACE : titre + musique OK ; le bouton joystick ne lance pas le jeu. (Castle Warrior, lui, fonctionne.) | À diff'er Hatari. |
| **Wings of Death** (`.stx`) | Après bouton : titre **corrompu** + son ralenti ; SPACE lance le jeu, qui tourne ensuite très bien. | Corruption titre (vidéo) + son chargement. |
| **Lethal Xcess sur Mega ST** (`.stx`) | ⛔ **NOUVEAU (2026-08-19)** : le jeu **se bloque** en `machine=megast` alors qu'il va **en jeu** en `machine=st` — MÊME ROM, mêmes disques, mêmes entrées (vérifié avec `etos192fr` ET `tos102uk`). Ce n'est donc pas « le piège megast » côté utilisateur, c'est un écart de NeoST à instruire. | Mesuré : les **796 premières commandes FDC sont IDENTIQUES** aux deux profils (`NEOST_FDC_DEBUG=1`), puis le jeu émet un Force Interrupt (`cmd=d0`) et **cesse de demander des données** ; il tourne alors en boucle sur `$14206 : tst.w $13a16 / btst #5,$fa01` = attente d'IRQ FDC (GPIP bit 5) qui n'arrive plus. En `st` le jeu reprend 40 s plus tard (`cmd=13` puis pistes 49-50) et démarre. IRQ prises identiques (vecteurs $48/$46/$1C) dans la fenêtre. Recette : `--disk Disk_1.STX --diskb Disk_2.STX --keys-at 3000 " " --joy-script 16500 "FFFF" --joy-at 21000 0x80`, écran en jeu ≈ trame 21500 en `st`. ⚠ **Cross-check Hatari BLOQUÉ** : `--cmd-fifo` n'injecte que des scancodes ST, pas de bit joystick — il faut un autre biais pour piloter le tir sous l'oracle. |

Deux suivis mineurs laissés ouverts sur des cas par ailleurs tranchés :
- **Lethal Xcess** — titre « buggé à ~8 % » constaté en GUI (2026-07-02), probablement la
  même calibration `$8209` que l'in-game déjà réparé ; à re-vérifier en GUI.
- **Stardust** — sur ST, NeoST reste en écran noir là où Hatari **halte** (double-fault) :
  la détection double-fault ne se déclenche pas sur cette séquence. Faible valeur (ne
  concerne qu'un jeu STE lancé par erreur sur ST).

> ⚠ **Avant de déclarer un bug : vérifier la RAM, puis la ROM.** Le réflexe et les cas
> qu'il a tranchés → [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md).

> **Déjà expliqués** (9 titres, corrigés ou jugés fidèles) : Captain Blood, Enchanted
> Land, Lethal Xcess, The Cuddly Demos, Rick Dangerous II, Stardust, Spectrum 512 STE,
> Blood Money, HotPot → [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md).

---

## 🔬 Divergences Hatari restantes

**Inventaire maître** (sévérité + impact + `fichier:ligne` des deux côtés, 4 passes d'audit) :
[`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md). Fidélité globale **très élevée** ;
aucune divergence ne casse un boot EmuTOS/`.ST`. Le terrain **logique** est épuisé (tous les
écarts bornés et vérifiables sans oracle sont corrigés) ; ne restent que les écarts
**cycle-exacts** (ci-dessous) et quelques cas-limites documentés.

### 🔮 Items qui exigent l'oracle Hatari

> **L'oracle se bâtit, il n'arrive pas tout seul** : `extern/hatari` est GITIGNORÉ et n'est
> PAS un sous-module — sur une machine fraîche il est simplement ABSENT (constaté ici le
> 2026-08-19). `git clone --depth 1 https://framagit.org/hatari/hatari.git extern/hatari`
> puis `cmake -S extern/hatari -B extern/hatari/build -DCMAKE_BUILD_TYPE=Release
> [-DCMAKE_OSX_ARCHITECTURES=arm64 -DENABLE_OSX_BUNDLE=0 sous macOS] && cmake --build
> extern/hatari/build -j` → `extern/hatari/build/src/hatari` (v2.6.1-devel bâti ce jour-là).
> Les deux options macOS sont obligatoires, cf. le doc. Recette de comparaison cycle-exacte →
> [`docs/HATARI_AUTOMATION.md`](docs/HATARI_AUTOMATION.md).

Ce qui a été traité GRÂCE à l'oracle depuis que cette liste a été écrite : **V1** (branche STE
de la Glue) et **V2** (tricks par changement de résolution) portés le 2026-07-08 · **S2** (FIFO
8 octets DMA + avance HBL) et **S3** (gain LMC ×2) corrigés le 2026-07-07 · **M1** (GPIP on-chip
via machine de fronts AER/DDR) corrigé (`bc15a67`). Détails et ancres →
[`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md).

Restent, par priorité d'impact :

1. **[JOUEUR] Beam-sync** — phase CPU↔faisceau **par-ligne** (overscan vertical). Casse EL /
   Cuddly / SHO en jeu. → `docs/MOIRA_WINUAE_CONVERGENCE.md`, `docs/CYCLE_ACCURACY.md` §4.
2. **[VIDÉO]** V3 géométrie mid-trame (50↔60 Hz) : le restart du compteur est porté
   (`VC_RESTART`), restent `CyclesPerVBL`±4 et l'attribution de ligne fixe.
3. **[SON]** quantification HBL du refill FIFO à confronter à l'oracle sur un poll serré de
   `$FF8909/0B/0D` — validable par dump WAV + trace.
4. **[FDC]** D3 stall FIFO 32 cyc · drive/side « push » — validables par trace FDC byte-exacte.
5. **[MFP]** `UpdateTimers` avant lecture IPR/ISR/TBDR en mode bloc (≤ 1 instruction de retard).
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
- ✅ **Lock moteur EL = 100 % (5ᵉ passe 2026-07-02)** : le dernier verrou était le **double
  comptage du saut STOP** dans la comptabilité de quantum (datation vidéo en avance de δ 4..26
  cyc sur l'horloge CPU) — rebase `quantumStartBus_` au saut (`Cpu68k::run`). Résout AUSSI les
  mystères « commit VBL casse le loader » (→ `NEOST_RAISE_COMMIT` défaut 3 = HBL+VBL, modèle
  fidèle complet) et « IPLFETCH casse le loader ». + broche MFP exacte portée (`NEOST_MFP_EXACT`,
  anti-datation Timer B event-count + prise à la frontière, fidèle Hatari, non nécessaire au lock).
  → `docs/MOIRA_WINUAE_CONVERGENCE.md` (5ᵉ passe).
- ✅ **Flicker bordure haute EL RE-MESURÉ + diff `$8209` d'entrée à l'oracle (2026-07-09) = CONVERGÉ,
  transitoire inclus.** Régime établi : re-arm write cyc **441±3 (spread 8)** BAT Hatari ~444±8,
  retrait haut **249/249**. Transitoire d'entrée : le stabilisateur EL spin-poll `$FF8209` (pc
  `$ee78/$ee80`) ; DE-start détecté **byte-identique** NeoST↔Hatari (ligne 63, X 60-68, val 2-6,
  ~7965 lectures). Le transitoire (freq ligne 63 → lock ligne 32) existe **des DEUX côtés, durée
  comparable ~17-26 frames** = convergence propre au stabilisateur d'EL, pas une divergence NeoST.
  ⚠ Deux verdicts intermédiaires (« Hatari lock en 1 frame ») étaient des artefacts de segmentation ;
  seul le diff `$8209` a tranché. Détails → `docs/MOIRA_WINUAE_CONVERGENCE.md` (bloc 2026-07-09 d).
- ✅ **Bancs poll RE-FERMÉS contre oracle frais (2026-07-09).** `make_poll_test` NeoST↔Hatari
  (fastfdc des deux côtés, alignement de trame exact, périodicité E-clock 5 trames confirmée) :
  **aire active BYTE-EXACTE** → phase CPU↔faisceau validée. Seul résidu = le latch couleur bordure
  gauche (1 ligne, cf. § Vidéo/Shifter ci-dessous). Banc ENTRY (`make_poll_entry_test`, lecture en
  pleine DE) : ~4440 px actifs de diff = la micro-structure latence d'exception / reconnaissance
  IPL, RAFFINEMENT dé-priorisé (n'affecte ni jeux ni screenshot). Recette : `--shot-from/-every`
  côté NeoST + AVI png `--avirecord` côté Hatari, diff PIL (downscale ×2 nearest).
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
- ✅ **Latch couleur bordure GAUCHE (registre 0) par-ligne — CORRIGÉ 2026-07-09.**
  `Shifter.cpp:724-729` (réamorçage `:404`) : bord gauche[N] = palette[0] de la ligne N−1 (`leftBorderPal0_`, réamorcé
  au registre 0 de début de trame), bord droit[N] = courant. Raison HW : les pixels du bord gauche
  sortent cyc ~0-56, AVANT l'écriture palette du handler HBL (cyc 508 « pour la ligne suivante ») →
  latché en fin de ligne précédente ; `renderLine` est appelé 1×/scanline en ordre croissant
  (`Machine::onRender`, cyc 376) → un membre « ligne précédente » suffit (renderFrame est mort, le
  re-rendu spec512 ne touche que l'aire active). Validé à l'oracle frais (poll-bench **10680→1330 px,
  −88 %**), **étalons 19/19 intacts** (comparés `--crop active`).
  ◑ **Résidu (finer, non corrigé)** : 16 px (cols 45-60) = la **position horizontale exacte** où
  l'écriture palette prend effet (Hatari bascule ~16 px APRÈS le début nominal de l'aire active =
  latence pipeline ; NeoST bascule pile à `activeX_`). Le poll-test à aire active unie ne le
  contraint pas plus ; nécessiterait de modéliser le cycle d'effet de l'écriture registre 0 vs
  DE-start par ligne. Invisible aux étalons. _Valeur très basse._

### Interface — kiosk & effets CRT (revue 2026-07-10, re-vérifiée 2026-07-12)
Fonctionnalités **livrées et fonctionnelles** (build OK options strictes, auto-tests cœur
verts) — voir `CHANGELOG.md § Frontend`. Les 3 points fonctionnels relevés à la relecture
(ordre `edgeMask` vs persistance, clamp `centerLighting`, touche « collée » page Clavier)
étaient en fait **déjà corrigés dans `0767f66`** (vérifié au code + `git log -S`,
2026-07-12) — il ne reste que du cosmétique :
- **Cosmétique** : membres `srcW_`/`srcH_` morts dans `CrtEffectStack` ; destructeur `= default`
  (fuite GL seulement si l'objet cessait d'être un singleton process-lifetime) ; répétition de
  navigation kiosk : tenir gauche/droite (swap one-shot) bloque la répétition haut/bas.
- **CRT v1 assumé** : en kiosk, baril/vignette encadrent le buffer ST ENTIER (bords courbés
  rognés hors écran en zoom fort) ; contexte GL 2.1 (vieux macOS) → passthrough (pas d'effets).

### Son DMA STE
- ✅ **FIFO 8 octets + avance HBL** (`DmaSnd_FIFO_*`, S2, 2026-07-07) et **compteur d'adresse
  live** (`$FF8909/0B/0D`, `DmaSound::liveCounter` ≙ `DmaSnd_GetFrameCount`, 2026-08-06) : **faits**.
  Reste seulement à confronter la quantification HBL du refill à l'oracle sur un poll serré du
  compteur (cf. `docs/CYCLE_ACCURACY.md` § Son DMA STE). _Effort faible, valeur basse._

### Stockage & contrôleurs
- **SCSI / NCR5380** (MegaSTE/TT) *(gros contrôleur)* — réf. `ncr5380.c`. Non commencé.
- *(SCC : restes faible valeur — timers du BRG / Zero Count, baudrate temporisé, série hôte.)*

### FPU MC68881 (audit 2026-07-12 — différés)
- **Arrondis de conversion SORTANTE bit-exacts** : FMOVE.L/W/B (double arrondi 53 bits via
  extToD, INEX2 jamais levé, NaN→0 au lieu du payload) et FMOVE.S/D (mode FPCR ignoré,
  INEX2/UNFL absents, OVFL silencieux en D) → porter `floatx80_to_int32/float32/float64`
  (softfloat.c). FSGLMUL/FSGLDIV : plage d'exposant ÉTENDUE avec mantisse 24 bits → porter
  `roundSigAndPackFloatx80` (softfloat.c:1502).
- **Packed decimal** : ±inf/NaN → exposant $FFF (pas du BCD invalide), INEX1 sur conversion
  inexacte, OPERR si k>17 (complète le différé « packed decimal bit-exact » existant).
- FMOVECR : précision FPCR non appliquée après la table ; offsets indéfinis → table silicium
  (`fpp_cr_undef`) au lieu de 0.0. FMOD précision < étendu : ré-arrondir a (expDiff<−1).

### Périphériques & profils machine
- **Save-states × GEMDOS HD** : les handles fichiers hôtes ouverts / suivi Pexec de `GemdosHd`
  sont HORS snapshot (bug hunt 2026-07-12, F7) — un état sauvé pendant qu'un programme a des
  fichiers ouverts sur C: donne des handles morts au load (Fread/Fclose du guest échouent).
  Sérialiser la table de handles (chemin + offset + mode) et rouvrir au load ; en attendant,
  documenté ici.
- **ROM TOS MegaSTE** : TOS 2.05/2.06 256 Ko à `$E00000` (choix pays, checksums, fallback EmuTOS
  MegaSTE). Aujourd'hui : EmuTOS 256 Ko par défaut.
- **NVRAM / préférences TOS MegaSTE** (résolution / boot device) si TOS 2.x l'exige.
- **Cartridge port** `$FA0000-$FBFFFF` générique (au-delà du système GEMDOS) — réf. `cart.c`.

### Système de régression (refonte — déclenché par la casse spec512 non détectée, 2026-07-09)

**Constat.** Une régression de palette spec512 (rapport terrain) n'a **PAS** été détectée : l'unique
étalon spec512 est un slice trop étroit (1 disque auto-diapo, **borderless**, ST/tos102uk, 2 trames,
headless). Trois trous : (a) **couverture** — GUI, images bordées/beam-racing, autres résolutions,
res-tricks non testés ; (b) **automatisation** — la suite est manuelle (aucun hook/CI), une régression
ne remonte que si on pense à lancer `run_etalons.py` ; (c) **provenance des réfs** — `compare` préfère
la self-capture `.ppm` à l'oracle `.png` → une réf ré-« blessée » peut figer un bug.

Refonte proposée, en **pyramide à paliers** (chaque test s'auto-verdicte → code de sortie ; les paliers
rapides tournent en secondes et **gardent le commit**) :

- ✅ **P0 — auto-tests logique pure (ms, sans boot) — FAIT (2026-07-09)** : `--spec512-selftest`
  (`Shifter::spec512SelfTest`, headless + `run_etalons.py` type `spec512_selftest`) remplit une RAM
  vidéo synthétique (tous pixels = index 1), injecte des écritures palette datées et **assère la
  couleur pixel octet-exact** contre le modèle `f(kSpec512AlignCyc, géométrie)` — garde aussi la
  constante `-25`. **Contrôle négatif** : c'est cette garde sur la constante qui fait tomber le test
  si l'alignement bouge. (L'ancien `NEOST_ALIGN_OFF=1` n'existe plus dans le code — vérifié
  2026-08-19 : le poser laisse le test à exit 0.)
  ✅ **P0+ FAIT (2026-07-09)** : `--bus-selftest` (`Bus::busSelfTest` : whitelist bus-error — RAM/ROM/cart
  ne fautent pas, $FF0000-$FF7FFF faute, INVARIANT « word ne faute que si TOUS ses octets fautent » sur une
  frontière IO trouvée dynamiquement ; force le superviseur) et `--mfp-selftest` (`Mfp::mfpSelfTest` :
  bits GPIP forcés bit7/5/4, **détection de FRONT** AER/DDR, Timer B event-count fin/début de ligne). Types
  `bus_selftest`/`mfp_selftest` dans `etalons.json` ; dans le palier fast.
- ✅ **P1 — verdicts cartouche (s, déterministe, sans oracle) — FAIT (2026-07-09)** : convention série
  **`NEOST-TEST: <nom> PASS|FAIL <détail>`** (UDR `$FFFA2F`, sink RS-232). `--serial-dump FILE` (capture
  propre), `tools/make_selftest_cart.py` (cartouche **diagnostic** `$FA52235F`, saut `$FA0004` au reset,
  mini-assembleur 68000 à labels ; `--break cpu|timing` pour valider les FAIL), runner
  `tools/run_selftests.py` + `tools/selftests.json` (scanne le série, sort 0/1). Bout-en-bout vert ;
  `--break` → exit 1. ✅ **Verdict FPU migré vers le série** (2026-07-09) : `make_fpu_testrom.py` émet
  `NEOST-TEST: fpu PASS|FAIL` (UDR $FFFA2F) en plus de `D7` (compat trace) ; entrée `fpu_cir` (megaste+fpu).
- ✅ **P1-timing — FAIT (2026-07-09)** : (a) sentinelle **liveness** (`$FF8209` non figé → anti-clock-morte) ;
  (b) **cycle-exact `frame`** : la cartouche diagnostic installe les vecteurs HBL/VBL ($68/$70), **compte les
  HBL par trame** par interruptions et vérifie la bande (262 pré-TOS, déterministe ST/STE/tous TOS) → flague
  une dérive grossière (50 Hz→313, 71 Hz→501, horloge morte→0). `--break frame` valide le FAIL.
  ✅ **latence IPL interne FAIT (2026-07-09)** : test `ipl` — le handler HBL de la ligne 100 fait un délai
  puis capture `$FF8209` (position faisceau = phase d'entrée d'exception, IACK+prologue) → bande calibrée
  224±4, déterministe. `--break ipl` valide le FAIL. ✅ **gate cycle-bench FAIT** : `make_cycle_bench.py
  --cart` (14 corps d'instructions en cartouche, K petit) + `tools/run_cyclebench.py` extrait les périodes
  de boucle (NEOST_TRACE_CYC) et compare au golden `tests/reference/cyclebench.json` (auto-régression du
  modèle de cycle 68000, tolérance 0 ; dérive → exit 1). Dans le palier fast.
- ✅ **P2 — étalons pixel épinglés oracle — durci + élargi (2026-07-09)** : `ref_kind: oracle|snapshot`
  dans `etalons.json` ; `run_etalons.py` compare à l'**oracle `.png`** quand `oracle` (fini la préférence
  silencieuse pour la self-capture `.ppm`), snapshot = self-capture (`.ppm`, repli `.png`).
  `--verify-refs` contrôle la provenance (oracle = `.png` ≥832px, sinon suspect). `compare_screenshot
  --report` = **diff palette PAR LIGNE** (1ᵉʳ écart x/y + pires scanlines → localise un décalage spec512 ;
  utilisé auto en cas d'échec). Élargi : `spectrum512_diapo_ste` (STE) ajouté. ✅ **spec512 bordé couvert**
  (2026-07-09) : `spec512SelfTest` teste aussi le **chemin fenêtré** `renderGlueFrame` (bordures G+D
  ouvertes + palette roulante spec512) — smoke test sans contenu externe (voie armée + couleurs présentes).
  ✅ **Étalon pixel spec512 ST + STE FAIT** : depuis `spectrum_512_auto_diapo.st`, oracle Hatari 832×552 au
  frame 1650 (le plus exigeant) → `spectrum512_diapo2` (ST, puma) + `spectrum512_diapo_ste` (STE, scramble
  FIDÈLE). ⚠ Vérifié : ce disque n'a **aucune** image spec512 à bordures ouvertes (scan ST+STE frames
  100-2500 ; les seules frames « fullscreen » ~300-400 = fond de chargement gris uni, IDENTIQUE Hatari).
- ✅ **P3 — pont config GUI↔headless — FAIT (2026-07-09)** : `--from-cfg neost.cfg` rejoue en headless la
  config EXACTE du GUI (rom/machine/mem/cpu/mono/fastfdc/fpu, supports disk/diskb/cart/gemdos/acsi et
  réseau fujinet*/modem/ethernec — `diskb`, `modem` et `ethernec` ajoutées le 2026-08-17 ; chemins `./../`
  du GUI résolus vers la racine ; les options CLI placées après surchargent). Reproduit « ce que
  l'utilisateur a lancé » → `--from-cfg neost.cfg --frames N --screenshot s.ppm` puis diff Hatari.
- ✅ **Orchestration — FAIT (2026-07-09)** : `tools/run_all.py --tier fast` (P0+P1 et les gardes
  ajoutées depuis, ~3 s → garde de commit)
  et `--tier full` (fast + P2 étalons pixel + `--verify-refs`). `--install-hook` / `--uninstall-hook`
  posent un **hook git pre-push** (opt-in) lançant `--tier fast`.

**Pyramide de test COMPLÈTE (2026-07-09, élargie depuis)** : **P0** `neost-selftest` (logique pure :
chemins hôte, `neost.cfg`) + `--spec512-selftest` (borderless + bordé) + `--bus-selftest` +
`--mfp-selftest` + `--msa-selftest` + `--fuji-selftest` + `--enec-selftest` · **P1** verdicts série
cartouche (cpu/timing/frame/ipl/fpu) + `run_selftests.py` · **cycle-bench** (`run_cyclebench.py`,
golden 68000) · **round-trip save-state** + **contrôle de la disquette livrée**
(`check_disk_assets.py`) · **P2** `ref_kind` oracle + diff par ligne + `--verify-refs` ·
**P3** `--from-cfg` · **orchestration** `run_all.py --tier fast|full` + hook pre-push.
Palier fast complet en ~3 s (mesuré 2026-08-19 ; il a grossi depuis les ~0,3 s d'origine). **Reste (faible priorité)** : gate `trace_diff --periods` vs oracle
Hatari (le cycle-bench actuel est une auto-régression NeoST) ; self-tests P0 supplémentaires (autres Timers,
ACIA) ; si une vraie démo spec512 **overscan** (bordures ouvertes) est rapatriée un jour → l'ajouter en
étalon oracle (l'auto_diapo, lui, est 100 % borderless).

### Outillage / qualité
- **Étalons headless** : calibrer frames + références Cuddly / Union / Troed / Hatari Test Suite ;
  rapatrier Union (planetemu manuel). Infra en place (`tools/run_etalons.py`).
- **Samples GODLIB (chantier « faire tourner le boulot Reservoir Gods »)** : les **15 exemples**
  `GODLIB.SPL/*` compilent (`build.sh <NOM>`) et **s'exécutent sous NeoST** (STE 1 Mo/tos162fr,
  gemdos). Correctifs `build.sh` : détection du `.PRJ` réel (BLITTER1/CLI_TEST/SPRITE1) + pont
  `@__v0printf` (printf vbcc, pour COOKIJAR/JAGPAD/JOY/TRUCOLOR). Assets runtime chargés depuis
  le **cwd** (= racine C:\ à l'autostart #Z ; = dossier au double-clic GEM) : `SPRITE.PI1`,
  `RGLOGO.PI1`, `IMAGE.GOD`, `voice.raw`/`OH_YES.wav` copiés dans `gemdos/etalon/`.
  **HotPot** (jeu complet) : ✅ front-end JOUABLE après le fix `-D` du `.PRJ` (cf. Catalogue).
  ⚠ `build.sh` applique désormais les `-D` actifs du `.PRJ` à toute compilation (module-enable
  GODLIB type `dGODLIB_FADE`) — indispensable, sinon des sous-systèmes sont compilés hors.
- **Comparaison MAME ↔ NeoST** (memory map, bus errors, FDC/MMU FIFO, blitter, SCC).
- **Matrice de compatibilité MegaSTE** : TOS 2.05/06, EmuTOS, 1/2/4 Mo, 8/16 MHz, cache on/off,
  DD/HD, mono/couleur.
- Capturer des **traces Hatari de référence** pour `trace_diff` (Arkanoid & co).

### Réseau (extensions NeoST — base livrée 2026-08-12, cf. `docs/FUJINET.md`)
- **FujiNet — TLS/HTTPS** : brancher mbedTLS (dépendance optionnelle, `NEOST_WITH_TLS`) — v1
  refuse `https://`. Puis POST/headers custom côté N:, UDP, **TNFS**, FTP, imprimante P: (PDF).
- **FujiNet — `FujiHostBridge`** : backend UDP relayant vers le **vrai firmware FujiNet-PC**
  (façon NetSIO, requête de sync qui met l'émulation en pause). Interface `FujiHost` déjà
  pluggable — c'est un simple ajout de backend, hérite de tous les protocoles amont.
- **FujiNet — lib ST** : proposer le dossier `atarist/` en amont à `fujinet-lib` pour cadrer le
  binding ACSI tôt ; device slots 0-7 différenciés ; montage lecteur B.
- **MT-32 (Munt) — paquet macOS** (2026-08-21) : `libmt32emu.dylib` vient de Homebrew ; le `.app`
  livré doit l'embarquer (copie dans Frameworks + `install_name_tool`) ou compiler Munt en
  statique (sous-module `extern/munt`), sinon l'option n'existe que sur une machine avec brew.
- **MIDI OUT Linux/Windows** : `MidiOutMac` est CoreMIDI/AudioToolbox ; ALSA sequencer + FluidSynth
  (Linux) et winmm (Windows) restent à écrire — le MT-32 (Munt), lui, est portable.
- **NetUSBee — périphériques USB hôte** (2026-08-21) : l'ISP1160 (`io/Isp1160`) est un hub racine
  VIDE ; brancher un clavier/souris HID puis un stockage de masse derrière `HcRhPortStatus` (PTD
  ATL → réponses du device). Les pilotes FreeMiNT `netusbee.ucd` + `usb.km` sont le banc d'essai.
- **NetUSBee — fenêtre LSB partagée** : `$FA0000-$FA01FF` = latch ISP1160 ET registre CR NE2000 ;
  NeoST laisse les deux puces voir l'accès faute de schéma. À trancher sur le schéma du NetUSBee
  (hardware.atari.org) ou sur un test matériel, puis ajuster `Bus::read8Slow`.
- **UltraSatan — `US_CONF.TOS` réel** : l'outil de Jookie (ce-atari/ultrasatan/config) compile avec
  Pure C ; le passer sur NeoST (écran de config, lecture FW/horloge/nom) pour valider au-delà du
  programme de test maison. Idem HDDRIVER/ICD PRO sur une image 2 slots.
- **EtherNEC — backend réel** : `SlirpNat` (NAT mode utilisateur, `libslirp` — seul le runtime
  est présent ici, pas le `-dev`) ou pcap/TAP ; puis **valider STinG + `ENEC.STX` sous TOS 1.04**
  (DHCP + ping/GET) et consigner dans `docs/CASE_STUDIES.md`. Livrer les pilotes libres GPL.
- **Modem/STinG** : documenter l'installation STinG (noyau+`sting.inf` dans `AUTO`, modules dans
  `C:\STING`) dans `docs/TEST_SOFTWARE.md` ; banc SLIP bout-en-bout.
- **MIDI ring** : option GUI (saisie du pair) ; test en anneau à 2 nœuds (deux instances NeoST).
- **Sécurité** : liste blanche de domaines optionnelle ; rejouer les scénarios d'évasion GEMDOS
  (chemins/symlinks) contre les écritures de fichiers déclenchées par FujiNet.
