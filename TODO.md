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

## 🚨 BLOQUANT RELEASE — contenu sous copyright suivi par le dépôt (2026-08-01)

Le dépôt `habib256/neost` est **public** (GPL-3.0, GitHub Pages actif) et `git ls-files`
suit :

| Chemin | Contenu | Volume |
|--------|---------|--------|
| `roms/` | **44 images TOS Atari propriétaires** (`tos100*` → `tos404`, `TOS v1.02 …[MEGA TOS]`) | ~11 Mo |
| `disks/st/`, `disks/stx/` | ~80 images de **jeux commerciaux**, majoritairement CRACKÉS (mentions `[cr Replicants]`, `[cr Elite]`, `[cr Medway Boys]`…) | ~51 Mo |
| `carts/` | cartouches **Atari Field Service** (`ST_Diagnostic_v4.4`, `MegaSTE_Diagnostic_v1.5`, `STE_Test_v1.9`) | |
| `wasm/index.data` | **artefact de build commis** qui ré-embarque les 122 fichiers ci-dessus | 73 Mo |

Conséquences : cloner le dépôt (ou télécharger le tarball GitHub) livre une archive de
logiciels sous copyright. `README.md` affirme par ailleurs que « les TOS Atari d'origine
… **ne sont pas redistribués** ici » — démenti par le contenu, ce qui aggrave la position
plutôt que de la protéger.

✅ **Déjà fait** : `deploy-web.yml` est repassé à `NEOST_WEB_FREE_ONLY=ON`, donc Pages ne
sert plus que EmuTOS + `diskA.st`. Les paquets bureau étaient déjà propres
(`stage_free_data.sh` + gardes `STRAY` dans les 4 jobs).

❌ **Reste à trancher (décision du mainteneur — implique une réécriture d'historique)** :
1. `git rm --cached` sur `roms/tos*`, `disks/st`, `disks/stx`, `carts/`, `wasm/index.*`,
   les ajouter au `.gitignore`, puis **purger l'historique** (`git filter-repo`) — sans
   quoi le contenu reste téléchargeable dans les commits antérieurs.
2. ⚠ **Couplage à traiter EN MÊME TEMPS** : `tools/etalons.json` fait dépendre 8 étalons de
   `roms/tos102uk.img` et `roms/tos162us.img`, et `run_all.py --tier full` garde les deux
   jobs Linux de la release. Retirer les ROMs **casse mécaniquement la CI** → basculer ces
   étalons sur EmuTOS ou les marquer `optional` AVANT le retrait, sinon le correctif
   juridique sera annulé pour « refaire passer le vert ».
3. Rendre l'affirmation de `README.md:192` vraie plutôt que de la retoucher.

Autres points de conformité relevés à la même passe (non bloquants mais à traiter) :
- Les paquets publiés (AppImage, `.dmg`) n'embarquent **aucun texte de licence** :
  `stage_free_data.sh` copie EmuTOS (**GPLv2**) et le binaire NeoST (**GPLv3**) sans
  `LICENSE`, sans COPYING EmuTOS ni offre de source → non-conformité GPL.
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

## Catalogue logiciels — bugs en cours

Rapports terrain. TOS 1.02fr sauf mention contraire. Chemins sous `disks/st/` (`.st`) ou
`disks/stx/` (`.stx`). Pilotage headless : `--keys`/`--joy-at`, trace `--irq`, diff Hatari.

| Jeu | Symptôme | Piste / renvoi |
|-----|----------|----------------|
| **Arkanoid (1987)** (Imagine) | Atteint l'écran-titre mais ne franchit pas la partie (boucle `$31736`/`$26E7`). | Gel FDC `$31736` **résolu** (modèle rotationnel) ; reste une cause distincte (protection ? 2ᵉ chargement ? IRQ ?). 🎯 étalon FDC/protection. |
| **Captain Blood (1988)** (ERE) | ✅ **JOUABLE en TOS US. Cas TOS FR TRANCHÉ (2026-07-09) = FIDÈLE, pas un bug.** « KEYBOARD PROBLEM » en TOS FR : oracle Hatari TOS FR montre EXACTEMENT le même écran (diff visuel identique). Le jeu n'envoie AUCUNE commande IKBD spéciale (flux IKBD US==FR : $08/$0F/$12/$80 init standard) → détection purement logicielle (lecture table clavier TOS) ; NeoST fait tourner la même ROM FR que Hatari → même résultat. La version crackée `[cr 42-Crew]` rejette la table AZERTY. Rien à corriger. | Vérifié à l'oracle (cmd-fifo+SPACE, tos102fr) : Hatari FR == NeoST FR. |
| **Enchanted Land (1990)** (Thalion) | ✅✅✅ **RÉSOLU (5ᵉ passe 2026-07-02) : moteur fullscreen verrouillé 100 %** (12402/12402 écritures freq à la position Hatari-exacte). Le dernier verrou n'était PAS la reconnaissance IPL mais un **double comptage du saut STOP** dans la comptabilité de quantum → `sched.now()`/datation vidéo en avance de δ∈{4..26} sur l'horloge CPU (quand δ≡2 mod 4, le calibrateur $8209 déverrouillait). Micro-sauts éliminés. **Son Thalion OK** (confirmé GUI 2026-07-02 — réparé par la même passe). Jeu complet. | Fix = rebase `quantumStartBus_` au saut STOP (`Cpu68k::run`) → `docs/MOIRA_WINUAE_CONVERGENCE.md` (5ᵉ passe). Les 3 mystères (lock, commit-VBL↔loader, IPLFETCH↔loader) avaient cette même cause. |
| **Lethal Xcess** (`.STX`, TOS 1.62) | ✅ titre stable + **in-game RÉPARÉ (2026-07-12)** : la régression « commit scanline au HBL » (617d030) faisait committer les 29 lignes de bordure haute sur l'ancre sticky `liveStartHBL_=34` (paire 60/50 de calibration ligne 32, qui n'ouvre PAS le haut) → poll $8209 fin de trame faux (+29×160 octets) → écran en jeu déchiré + chargements en échec. Fix `commitAnchor()` (fenêtre Glue LIVE, latchée) → pixel-identique au pré-régression, flux $8209 byte-identique (~3 M lectures). ◑ Titre « buggé à ~8 % » GUI (2026-07-02) : probablement même cause (calibration $8209 chaque trame), à re-vérifier en GUI. | Recette in-game headless : `--disk Disk_1.STX --diskb Disk_2.STX --keys-at 3000 " " --joy-script 16500 "FFFF" --joy-at 21000 0x80` → jeu ≈ trame 21900 (⚠ le jeu NE DÉMARRE qu'avec le disque 2 monté : titre→SPACE→music select→FEU→briefing→FEU tenu). Oracle Hatari : mêmes disques, Xvfb+xdotool, feu=Control_R tenu pendant chargement+briefing. |
| **Beyond the Ice Palace** (D-BUG) | Rapport GUI : écran scramblé en jeu. **NON reproduit en headless** : gameplay PROPRE (ST et STE, 1 Mo, boot AUTO). ⚠ Le PRG exige > 512 Ko (BSS dépack 384 Ko → TPA ~471 Ko) : en 512 Ko le TOS skippe l'AUTO — comportement CORRECT (pas un bug). Le chemin GUI = double-clic bureau GEM (Pexec sous AES) ≠ AUTO — à reproduire avec la config GUI exacte. | Recette headless : copier le disque + `mmd ::AUTO` + `mcopy` ; `--mem 1m --keys-at 4000 "n" --keys-at 6200 " " --keys-at 9600 " " --keys-at 12000 "y" --keys-at 14500 "s" --keys-at 16000 " " --keys-at 19600 "y"` → jeu ≈ trame 21000. |
| **The Cuddly Demos** (TCB) | ✅ **menu robot RÉSOLU (2026-07-03, commit `125388b`)** : clignotement vertical 10-47 % → **0** (250/250 trames verrouillées, mur régulier = Hatari). 1ʳᵉ page OK. | Cause : datations lecture (`−14`) / écriture (`−6`) du compteur vidéo co-calibrées autour d'une « origine −8 » devenue artefact après le fix STOP — ramenées ENSEMBLE aux valeurs fidèles §8 (**read −6, write +2**). Le synchroniseur (pc=f264, sortie sur octet bas `$8209` > `$40` SIGNÉ) lisait 4-6 octets de moins qu'Hatari au même instant → sortie L34→L36 → paire 60/50 une ligne trop tard. ⚠ Ne bouger ces offsets que PAR PAIRE (read seul casse EL). Repro : `--fastfdc --keys-at 3000 " "`, ≈ trame 6000. → `docs/MOIRA_WINUAE_CONVERGENCE.md`. |
| **Shadow Warriors** (2Hot2Handle) | Après SPACE : titre + musique OK ; le bouton joystick ne lance pas le jeu. (Castle Warrior, lui, fonctionne.) | À diff'er Hatari. |
| **Rick Dangerous II (1989)** (Core) | ✅ **JOUABLE** (résolu ; l'ancien plantage « 4 bombes » ne se reproduit plus). | — |
| **Wings of Death** (`.stx`) | Après bouton : titre **corrompu** + son ralenti ; SPACE lance le jeu, qui tourne ensuite très bien. | Corruption titre (vidéo) + son chargement. |
| **Stardust (1994)** (Daze) | ✅ **TRANCHÉ (2026-07-09) : jeu STE-ONLY, « crash ST » = FIDÈLE.** Sur `--machine st` le jeu lit `$FFFF8900` (son DMA **STE**) à PC=$38800 → bus error : **Hatari HALTE aussi** (« double bus/address error => CPU halted ») → pas un bug NeoST. Sur `--machine ste` (tos162fr, 1 Mo) : intro crack HARDCORE + « présente STARDUST » rendus. ⚠ Après l'intro → noir (multi-disque « Disk 1 of 3 » / touche — non creusé). ◑ Divergence secondaire NON creusée : sur ST, NeoST reste en écran noir (CPU tourne) là où Hatari **halte** (double-fault) — la détection double-fault NeoST ne se déclenche pas sur cette séquence ; faible valeur (ne concerne qu'un jeu STE lancé par erreur sur ST). | Lancer sur **STE**. Diff double-fault ST = suivi optionnel. |
| **Spectrum 512 — palettes « foirées » sur STE** (2026-07-09) | ✅ **PAS UN BUG — comportement FIDÈLE** : l'auto-diapo scramblait les palettes sur STE/Mega STE (parfait sur ST). Diag : le viewer spec512 est calibré timing **STF** → sur STE il se désynchronise **sur vrai matériel aussi**. NeoST STE == **oracle Hatari STE byte-exact (0 px, plage f1645-1660)** → NeoST reproduit fidèlement, ce n'est pas sa faute. La vraie lacune était l'**absence de détection** (rien ne disait « fidèle » vs « bug »). | ✅ Étalon `spectrum512_diapo_ste` ajouté (épinglé à l'oracle Hatari STE `tests/reference/spectrum512_diapo_ste.png`). ⚠ `9f0d2bc` (res-tricks) écarté : ne touche que MED_OFFSET ; ST reste 0 px vs oracle. Leçon → « Système de régression » ci-dessous. |
| **Blood Money (1989)** (Psygnosis) | ✅ **PAS UN BUG — manque de RAM (2026-08-07)** : « plante après le cracktro » = écran noir définitif en **512 Ko**, sur les DEUX cracks. **Oracle Hatari : même écran noir** (même ROM/disque/SPACE, jusqu'à 7000 VBL) → fidèle. Trace : recopie emballée à `$E26` avec `D2=$FFCC0484` (compteur négatif ≈ 4,3 G d'itérations) et `A0` déjà à `$34CA84` alors que `phystop` (D5) = `$00080000` — le dépaqueteur du crack calcule une taille bidon faute de RAM ; Hatari logge le même emballement (`Bus Error writing at address $400000, PC=$e28`). En **`--mem 1m`** : ça charge et ça joue. | ✅ Jouable en **1 Mo** avec `[cr Delight][m Superior][t]` (version « file », mono-disquette) : jeu ≈ trame 4800. `[cr Replicants][t]` démarre aussi mais réclame la **disquette 2** (non présente dans `disks/st/`). Recette : `--mem 1m --fastfdc --key-down 900 " " --key-up 906 " " --shot-every 600`. |
| **HotPot (2002)** (Reservoir Gods, notre build `build.sh --game`) | ✅ **RÉSOLU (2026-07-09)** : front-end JOUABLE (menu « DOUBLE JUGGLE », INFO/OPTIONS/PLAY/EXIT) sur STE 1 Mo/tos162fr et tos106uk 4m. Le « noir après l'intro » N'était **PAS** une divergence d'émulation (identique dans Hatari) mais un **bug toolchain** : `build.sh` ignorait les `-D` du `.PRJ`, donc `-DdGODLIB_FADE` jamais défini → `Fade_Init()` (dans `#ifdef dGODLIB_FADE` de PLATFORM.C) compilé hors → callback VBL `Fade_Vbl` jamais installé → **fade-in de palette jamais armé** (palette figée noire alors que le front-end était bien dessiné). | Fix : `build.sh` collecte les `-D` actifs du `.PRJ` (`GAME_DEFS`) → toutes les compilations `vc`. Diag : symboles DRI du `.TOS` + `--dump-at` (`$FFFF8240`, `gFade`, `mfCalls`) + oracle Hatari `--harddrive/--avirecord`. Renvoi mémoire `hotpot-jouable-divergence`. |

> ⚠ **Avant de déclarer un bug : vérifier la RAM (et la ROM).** Beaucoup de titres 1989+ et
> la plupart des cracks/dépaqueteurs exigent **1 Mo** ; en 512 Ko ils ne râlent pas, ils
> partent en vrille (compteur de recopie négatif, écran noir figé, ou l'AUTO simplement
> skippé par le TOS). Symptôme typique = « ça plante juste après le cracktro / l'intro ».
> Cas déjà tranchés ainsi : **Blood Money**, **Beyond the Ice Palace** (BSS 384 Ko),
> **Stardust** (STE-only). Réflexe : rejouer en `--mem 1m` **avant** de tracer, puis
> confirmer à l'oracle Hatari (mêmes ROM/disque/touches) — dans ces trois cas Hatari
> plantait **à l'identique**, la config était en cause, pas l'émulation. Second réflexe :
> la ROM fixe 50/60 Hz (suffixe `us` = NTSC, cf. `CLAUDE.md`).

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
  `Shifter.cpp:585-593` : bord gauche[N] = palette[0] de la ligne N−1 (`leftBorderPal0_`, réamorcé
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
- **FIFO 8 octets + avance HBL** (`DmaSnd_FIFO_*`, S2) + **compteur d'adresse live**
  (`$FF8909/0B/0D` au cycle) — refinement sous-perceptuel (la timeline horodatée capte déjà les
  modifs intra-trame à la granularité trame). _Effort moyen, valeur basse._

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
  constante `-25`. **Détection prouvée** : `NEOST_ALIGN_OFF=1` (dérive 1 cyc) → exit 1 ; propre → exit 0.
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
  config EXACTE du GUI (rom/machine/mem/cpu/disque/cart/mono/fastfdc/fpu/gemdos/acsi ; chemins `./../`
  du GUI résolus vers la racine ; les options CLI placées après surchargent). Reproduit « ce que
  l'utilisateur a lancé » → `--from-cfg neost.cfg --frames N --screenshot s.ppm` puis diff Hatari.
- ✅ **Orchestration — FAIT (2026-07-09)** : `tools/run_all.py --tier fast` (P0+P1, ~0,1 s → garde de commit)
  et `--tier full` (fast + P2 étalons pixel + `--verify-refs`). `--install-hook` / `--uninstall-hook`
  posent un **hook git pre-push** (opt-in) lançant `--tier fast`.

**Pyramide de test COMPLÈTE (2026-07-09)** : **P0** `--spec512-selftest` (borderless + bordé) +
`--bus-selftest` + `--mfp-selftest` · **P1** verdicts série cartouche (cpu/timing/frame/ipl/fpu) +
`run_selftests.py` · **cycle-bench** (`run_cyclebench.py`, golden 68000) · **P2** `ref_kind` oracle + diff
par ligne + `--verify-refs` · **P3** `--from-cfg` · **orchestration** `run_all.py --tier fast|full` + hook
pre-push. Palier fast complet en ~0,3 s. **Reste (faible priorité)** : gate `trace_diff --periods` vs oracle
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
