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

## 🏛 Dette d'architecture — revue du 2026-08-25

Revue transverse faite après le chantier blitter (BL3/BL4) et le balayage des 67 disques. Ce ne
sont pas des bugs mais des **propriétés manquantes du système de développement lui-même** —
classées par risque. Le plan de refonte des paliers de test existe déjà plus bas
(§ *Système de régression*) ; ce qui suit le précise et le complète.

### A1 ✅ — Le palier PIXEL garde désormais la barrière (FAIT le 2026-08-26)

`.github/workflows/tests.yml` — le job qui tourne **à chaque push** — lance `run_all.py
--tier fast`. `--tier full`, le **seul** palier qui compare des pixels, ne tourne que dans
`release.yml`. La protection existe, elle est bonne, et elle se déclenche **après** que le code
est parti.

Deux illustrations mesurées le même jour : `NEOST_SYNC_DISPATCH=1` casse `nocooper_greetings` à
**98,97 %** sans que le `fast` ne bronche ; et BL3/BL4 — une modification de la **base de temps du
cœur** — serait passé la CI au vert (il n'a été validé au pixel que parce qu'un `--tier full` a
été lancé À LA MAIN).
✅ **CORRIGÉ** : nouveau job **`pixel`** dans `.github/workflows/tests.yml`, qui lance
`run_all.py --tier full` **à chaque push et pull_request** (+ `--verify-refs`, + dépôt des
captures d'écart en artefact quand il échoue). Il coûte ~1-2 min de plus que le `fast`.
⚠ Il ne remplace pas les deux autres jobs : leur `--tier fast` couvre autre chose (slirp
compilé, sanitizers Debug+ASan).

### A2 ✅ — Le blitter a enfin un étalon (FAIT le 2026-08-26)

`NEOST_BLIT_TRACE=1` rend **0 blit** sur l'intégralité du corpus pixel : il est tout entier en
`machine=st`, où le blitter n'existe pas. Même trou pour le MFP en mode bloc et le stall FIFO du
FDC (D3). Conséquence directe : la preuve de BL3/BL4 tient à des runs **manuels** de *Lethal
Xcess* sur un `.stx` **non redistribuable**.
✅ **CORRIGÉ** : `tools/make_blitter_test.py` + étalon **`blitter_timer`** (EmuTOS 256 Ko, STE,
512 Ko). Une seule image contraint deux choses : la **datation** (lignes 0-99 = l'octet TADR relu
après chaque blit non-hog, Timer A en mode délai prescaler /200 — 1 tic = 200 cyc, donc insensible
au jitter sous-tic et sensible à la classe BL3) et les **données** (lignes 120-127 = destination
de 100 blits 16×8 mots depuis un motif à pas $3B27 balayant les 4 plans). ~400 tranches non-hog
par run, donc le chemin `Blitter::onSlice` est massivement exercé.
**Dents VÉRIFIÉES, pas supposées** : l'image diffère de **406 px** entre le commit `6bc2ce3`
(AVANT BL3/BL4) et l'état corrigé, **toutes** dans la zone TADR et **zéro** dans la zone de
données — cet étalon aurait attrapé BL3/BL4. Il détecte aussi `NEOST_RAM_SLOT=0` (269 px) et
`NEOST_SYNC_DISPATCH=1` (198 px).
⚠ **Et il a trouvé une divergence dès sa première exécution** : **BL5**, dérive cumulative de
~86 cyc par blit non-hog contre l'oracle (cf. `docs/HATARI_DIVERGENCES.md`). D'où
`ref_kind: snapshot` et non `oracle` — même démarche que `overscan_top`.
✅ **Le chemin HOG est couvert aussi** : étalon jumeau **`blitter_hog`** (même programme,
`ctrl=$C0`), qui emprunte `Blitter::start` au lieu de `Blitter::onSlice`. Il n'était exercé par
AUCUN test ni AUCUN titre (recensement Lethal Xcess : 5764 blits, tous `ctrl=$80`). Contrôle de
non-trivialité : 380 px d'écart avec `blitter_timer`, dont **0 px en zone de données** — même
transfert, seul le partage de bus change. Et il passe en **`ref_kind: oracle` à 0 px**, donc il
prouve la **conformité**, pas seulement la non-régression.
🎯 **Reste ouvert** : le MFP en mode bloc et le stall FIFO du FDC (D3) n'ont toujours pas
d'étalon — ce sont les deux derniers trous de couverture identifiés.

### A3 ◐ — Le corpus de régression n'est pas livrable (AVANCÉ le 2026-08-26)

La couverture repose sur ~80 jeux commerciaux crackés et 44 ROM propriétaires. **Le filet de
sécurité ne peut pas être distribué avec le projet**, et un contributeur externe ne peut pas
reproduire la validation. C'est le même dossier que le bloquant release, vu sous l'angle
ingénierie : chaque étalon **généré** qui remplace un étalon à disque commercial paie deux fois.
◐ **Avancé** : `blitter_timer` est créé **d'emblée sur ROM libre**, donc le corpus qui survivrait
au retrait des TOS Atari passe de **5 à 6 étalons** (`etos_ste_boot`, `overscan_top`, `trace_odd`,
`scroll_8264`, `scroll_8265`, `blitter_timer`) — et la **seule** couverture du blitter est du côté
libre. Restent 7 étalons adossés à des ROM propriétaires : `spectrum512_diapo{,2,_ste}`,
`cuddly_demos`, `union_demo`, `nocooper`, `nocooper_greetings`.

### A4 — L'instrument n'est pas testé (effort **S**)

`neost-headless` **EST** le framework de test, et il a des modes d'échec **silencieux**. Sur la
passe du 2026-08-25, **trois** « bloquants » sur huit venaient de l'instrument et non du système
mesuré (cf. `OUTIL-1`). Le piège résiduel le plus dangereux : `--keys-at` tient la touche
**40 ms** là où `--cmd-fifo` d'Hatari tient **~600 ms**, ce qui invalide silencieusement toute
comparaison à l'oracle — un verdict « confirmé à l'oracle » a été rendu **FAUX** par cet écart.
🎯 `neost-selftest` couvre « la logique pure » : le **parsing d'arguments en est**. Un test qui
vérifie que `--joy-at` deux fois s'applique deux fois aurait attrapé `OUTIL-1` avant qu'il ne
fabrique de faux bugs.

### A5 — L'oracle est une dépendance critique traitée comme un accessoire (effort **S**)

Toute la méthode imposée du projet repose sur Hatari. Or `extern/hatari` est **gitignoré**, n'est
**pas un sous-module**, peut être **absent** sur une machine fraîche, n'est épinglé à **aucun
commit** (l'inventaire note lui-même que ses numéros de ligne ont glissé entre `c9906f1` et
`981f291`), sème son RNG sur `time(NULL)` (d'où le besoin d'`oracle_scan`), et **ne sait pas
injecter de joystick** — ce qui exclut du cross-check toute une classe de jeux d'action.
🎯 L'épingler (sous-module, ou SHA + script de build vérifié), et verser la recette de pilotage
au joystick (`[Joystick1] nJoystickMode = 2` + `kFire = f` + `hatari-event keydown f`) dans
`docs/HATARI_AUTOMATION.md`.

### A6 — Aucun budget de performance (effort **S**)

BL4 ajoute un `syncTo` **par accès bus** du blitter (~370 k appels supplémentaires sur 6000
trames de *Lethal Xcess*). **Personne ne l'a mesuré**, et rien dans la CI ne l'aurait vu. Un
émulateur temps réel dont le **mode borne sur Raspberry Pi** est une cible déclarée devrait avoir
une barrière de **débit** (trames/s sur un boot de référence), pas seulement de justesse.
Le `cycle-bench` existant garde le modèle de cycle, pas le temps mur.

### A7 — La base de connaissance n'a aucun contrôle d'intégrité (effort **XS**)

`docs/HATARI_DIVERGENCES.md` est une base de données maintenue à la main. Constaté le même jour :
deux numérotations **en collision** (`B` sous-système vs `B` sévérité — corrigé en `BL*`), des
ancres pointant vers des **symboles disparus** (`Blitter::stallCpu` après renommage), un canal
décrit à la fois « complet mais OFF » et « ON par défaut ». Le coût réel est la **REDÉCOUVERTE** :
`D4` a été retrouvé comme faux positif par **trois** agents indépendants dans la même passe, et
*Arkanoid* était tranché depuis une passe **sans avoir jamais été versé** dans `CASE_STUDIES`.
🎯 Un contrôle CI qui vérifie que chaque `fichier:symbole` cité existe encore (`grep`) coûte dix
lignes et empêche la dérive silencieuse.

### A8 — Le GUI est un angle mort total (effort **M**)

Aucun test ne couvre le frontend, et c'est **précisément là que vivent les rapports utilisateur
restants** : *Wings of Death* (cœur émulé disculpé, suspect n°1 = underrun de la boucle audio,
`src/audio/Audio.cpp:176-178`), *Beyond the Ice Palace* (chemin double-clic GEM ≠ AUTO), *Lethal
Xcess* titre « à 8 % ». Le cœur est très bien couvert ; **ce que l'utilisateur voit ne l'est pas
du tout**.

### ⚠ Deux erreurs de méthode commises le 2026-08-25, consignées pour ne pas les refaire

- **Un seuil absolu sur une grandeur dépendante de la charge.** `timer IRQ max lateness` avait été
  inscrit ici comme sonde de non-régression « doit rester à **132** ». Faux : 147, 156, 157 et 163
  relevés sur d'autres titres. Corrigé — cette métrique se compare **à charge identique**, jamais
  à un seuil. Un faux garde-fou coûte plus cher qu'aucun garde-fou.
- **Justesse validée, coût ignoré.** BL4 a été validé au pixel et au barème sans **aucune** mesure
  de débit, alors que le changement multiplie les appels au dispatch (cf. A6).

---

## Catalogue logiciels — bugs OUVERTS

Rapports terrain non expliqués. TOS 1.02fr sauf mention. Chemins sous `disks/st/` (`.st`)
ou `disks/stx/` (`.stx`). Pilotage headless : `--keys`/`--joy-at`, trace `--irq`, diff
Hatari.

| Jeu | Symptôme | Piste / renvoi |
|-----|----------|----------------|
| **Beyond the Ice Palace** (D-BUG) | Rapport GUI : écran scramblé en jeu. **NON reproduit en headless** : gameplay PROPRE (ST et STE, 1 Mo, boot AUTO). ⚠ Le PRG exige > 512 Ko (BSS dépack 384 Ko → TPA ~471 Ko) : en 512 Ko le TOS skippe l'AUTO — comportement CORRECT (pas un bug). Le chemin GUI = double-clic bureau GEM (Pexec sous AES) ≠ AUTO — à reproduire avec la config GUI exacte. | Recette headless : copier le disque + `mmd ::AUTO` + `mcopy` ; `--mem 1m --keys-at 4000 "n" --keys-at 6200 " " --keys-at 9600 " " --keys-at 12000 "y" --keys-at 14500 "s" --keys-at 16000 " " --keys-at 19600 "y"` → jeu ≈ trame 21000. |
| **Shadow Warriors** (2Hot2Handle) | Après SPACE : titre + musique OK ; le bouton joystick ne lance pas le jeu. (Castle Warrior, lui, fonctionne.) | À diff'er Hatari. |
| **Wings of Death** (`.stx`) | ~~Après bouton : titre **corrompu**~~ **NON REPRODUIT en headless (2026-08-25)** : logo Thalion puis titre **154 couleurs PROPRES**, en `st/1m/tos102fr` comme en `ste/1m/tos106uk` — vérifié par deux agents indépendamment. Reste à instruire : **le son ralenti SEUL** (non évaluable par capture PPM). | ⚠ Sur cet écran c'est **ESPACE** qui avance, pas le feu. Cœur émulé disculpé le 2026-08-25 (vidéo byte-identique à l'oracle, cadence YM à ±0,1 s sur 26 s) → suspect n°1 = le **frontend** : lancer le GUI sans sandbox et guetter `[Audio] ring underrun … emulation loop: X real frames/s` (`src/audio/Audio.cpp:176-178`), puis `crt=0`, puis `mix_drive=0`. ⚠ `--sound-dump` **exclut** les bruits de lecteur (`AudioMix.hpp:16-19`) alors que l'utilisateur a `drivesound=1`. |
| **Lethal Xcess sur Mega ST** (`.stx`) | ✅ **CORRIGÉ (2026-08-25)** — c'était **BL3** : les cycles de stall du blitter étaient facturés HORS de l'horloge de l'ordonnanceur. `Blitter::onSlice` (tranche non-hog) est le callback de l'échéance `Scheduler::BLITTER`, donc il tourne ENTRE deux `cpu.run()` : ses cycles n'entraient ni dans `ran` ni dans `sched.now()`. La dette (mesurée : 8 tranches × 136 = **1088 cycles bus**) était résorbée d'un coup par le `syncTo` du hook d'IACK juste avant le handler Timer A, ce qui mangeait 2 tics de prescaler → `Mfp::readTimerData` rendait TADR = `$3C` et la garde `$14C2E` du jeu tombait dans son `ILLEGAL`. Corrigé par `Blitter::billCycles` → `Scheduler::addStolenCycles` (port de `Blitter_AddCycles`, `blitter.c:351-352`). ⚠ Le symptôme FDC ci-contre était un **LEURRE** : identique à la milliseconde près en `machine=st`, où le jeu démarre. | Le bug frappait les **trois** machines à blitter, pas seulement Mega ST : `megast` cassait trame 5552, `ste` (`tos106uk`) et `megaste` (`tos206uk`) trame 5523 — écran noir à 1 couleur. Après correctif : aucun `BREAK`, 26-29 couleurs, écran de jeu, sur les trois. Repro : `--machine megast --mem 1m --disk Lethal_Xcess_Disk_1.STX --diskb Lethal_Xcess_Disk_2.STX --keys-at 3000 " " --joy-at 4000 0x80 --frames 6000 --break 14C2E`. Sonde de non-régression : `NEOST_QDELTA_DIAG=1` (le delta d'entrée de quantum doit rester PLAT à 40, jamais d'escalier). Détail → `CHANGELOG.md` (2026-08-25) et divergence **BL3** de `docs/HATARI_DIVERGENCES.md`. ⚠ **Cross-check Hatari toujours BLOQUÉ** : `--cmd-fifo` n'injecte que des scancodes ST, pas de bit joystick — il faut un autre biais pour piloter le tir sous l'oracle. |

**CLOS (2026-08-25)** — *`D-PSG` : Stardust sur STE, gel noir après le menu trainer* :
**CORRIGÉ**. La sélection de lecteur/face écrite dans le port A du PSG est désormais **POUSSÉE**
vers le FDC (`Machine::setPortASink` → `Fdc::refreshDriveSide`, port de `psg.c:419-420` →
`FDC_SetDriveSide`) ; `refreshDriveSide()` est passée publique pour ça. Avant : le FDC ne
RELISAIT le PSG que depuis ses propres accès registre, donc un programme qui écrit sa commande
FDC AVANT de sélectionner le lecteur restait à `driveSel_ = -1` pour toujours. Mesuré après
correctif : `drv=0` au lieu de `drv=-1`, INTRQ levé, **374 294 lignes FDC** au lieu de 4714, et
Stardust STE joue son **intro défilante** puis va chercher la disquette 2 dans le **lecteur B**
(`drv=1`, `idxTime=0` — lecteur vide) — le comportement de l'oracle. ⚠ Les disquettes 2 et 3
étant absentes du dépôt, le titre n'est **pas** jouable pour autant : c'était une correction de
FIDÉLITÉ, comme annoncé. ◑ Résidu non élucidé : l'oracle affichait « INSERT DISK 2 IN ANY
DRIVE » là où NeoST fond au noir puis poll le lecteur B ; à revoir si les disquettes
manquantes réapparaissent. Validé : `--tier full` TOUS LES PALIERS OK, Lethal Xcess `megast`
intact.

**CLOS (2026-08-25)** — *Arkanoid (1987) : boucle `$31736`/`$26E7`* : **FIDÈLE**, pas un bug
NeoST. Hatari boucle **identiquement** sous TOS 1.02 (état machine byte-identique) et
**débloque identiquement** sous TOS 1.00 US/UK, où le jeu est jouable dans NeoST. La boucle est
l'attente clavier « 1 ou 2 joueurs » du titre : les pistes « protection / 2ᵉ chargement / IRQ »
sont **rayées** (aucun vecteur $47 pendant le gel). Le marqueur 🎯 « étalon FDC/protection » est
retiré : ce cas n'en est pas un. ⚠ Le disque cité n'est plus dans le dépôt (`45f9a65`).
Preuves et recette → `docs/CASE_STUDIES.md`.

Un suivi mineur laissé ouvert sur un cas par ailleurs tranché :
- **Lethal Xcess** — titre « buggé à ~8 % » constaté en GUI (2026-07-02), probablement la
  même calibration `$8209` que l'in-game déjà réparé ; à re-vérifier en GUI.

**CLOS (2026-08-25)** — *Stardust sur ST : « NeoST reste noir là où Hatari halte »* : **non
reproduit**. Mesure : NeoST **HALTE**, sur la MÊME instruction que Hatari (`$FC5082`,
`move.l A3,-(A7)` avec A7 = `$4E7340E7` impair, après la bus error `$FFFF8900` prise en
`$387FC`) — plus une seule instruction n'est exécutée après la trame ~1826 et l'écran est
noir des deux côtés. La cause de l'observation de 2026-07-09 est **indéterminée** (le
chemin de halt exercé existait déjà à cette date). Seule vraie divergence restante,
corrigée : le halt était **silencieux** — il est désormais journalisé
(`[cpu] 68000 halted: …`, cf. `src/core/Cpu68k.cpp`), comme Hatari
(`gui-sdl/dlgHalt.c:66-71`).

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

**Trou de couverture MESURÉ : aucun étalon n'exerce le blitter (2026-08-25).** `NEOST_BLIT_TRACE=1`
rend **0 blit** sur `scroll_8264`, `scroll_8265` et `etos_ste_boot` (les étalons STE) ; tous les
autres sont `machine=st`, où le blitter n'existe pas. Conséquence : un `--tier full` **vert ne
prouve rien** sur le blitter — il prouve la non-régression de la base de temps, pas la correction
**BL3** ni **BL4**. Toute leur preuve tient à des runs *Lethal Xcess* lancés à la main, sur un
`.stx` non redistribuable. 🎯 **À faire** : un étalon **généré** (esprit
`tools/make_scroll_test.py`) — secteur de boot STE autonome qui enchaîne des blits **non-hog**
pendant qu'un timer MFP tourne, capture pixel + contrôle que le timer n'a pas dérivé. C'est
l'étalon qui couvrirait les DEUX : BL3 (pas de dérive cumulée) et BL4 (le timer sert à l'heure
même quand son échéance tombe au MILIEU d'une tranche). Sans lui, la prochaine refonte de
l'ordonnanceur les re-cassera sans qu'aucun palier ne bronche. Palliatifs immédiats : la sonde
`NEOST_QDELTA_DIAG=1` (le delta doit rester **plat**, jamais d'escalier) et la métrique
`timer IRQ max lateness` — c'est elle qui avait signé le crédit groupé (~265) avant BL4.
⚠ **Cette métrique dépend de la CHARGE, ce n'est pas une constante** : la valeur **132** vaut
pour la repro *Lethal Xcess* `megast`/`ste`/`megaste` (celle qui sert d'A/B), mais un balayage
du 2026-08-25 relève **147, 156, 157 et 163** sur d'autres titres en `machine=st`. À comparer
donc **à charge identique**, jamais à un seuil absolu — sans quoi elle produit de fausses
alertes. Voir aussi : le chemin
**HOG** du blitter n'est exercé par **aucun** titre testé — recensement sur *Lethal Xcess*
`megast`, 6000 trames : 5764 blits, **tous** `ctrl=$80`, le bit HOG (`$40`) n'est jamais posé.

### Système de régression (refonte — déclenché par la casse spec512 non détectée, 2026-07-09)

**Constat.** Une régression de palette spec512 (rapport terrain) n'a **PAS** été détectée : l'unique
étalon spec512 est un slice trop étroit (1 disque auto-diapo, **borderless**, ST/tos102uk, 2 trames,
headless). Trois trous : (a) **couverture** — GUI, images bordées/beam-racing, autres résolutions,
res-tricks non testés (⚠ et **aucun étalon n'exerce le blitter**, mesuré le 2026-08-25 → A2) ;
(b) **automatisation** — ⚠ **partiellement périmé** : la CI existe désormais
(`.github/workflows/tests.yml`) mais elle ne garde le push qu'avec `--tier fast`, qui ne compare
**aucun pixel** ; `--tier full` n'est lancé que par `release.yml`, donc **après** le commit
(→ **A1**, correctif XS) ; (c) **provenance des réfs** — `compare` préfère
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
  réseau modem/ethernec — `diskb`, `modem` et `ethernec` ajoutées le 2026-08-17 ; chemins `./../`
  du GUI résolus vers la racine ; les options CLI placées après surchargent). Reproduit « ce que
  l'utilisateur a lancé » → `--from-cfg neost.cfg --frames N --screenshot s.ppm` puis diff Hatari.
- ✅ **Orchestration — FAIT (2026-07-09)** : `tools/run_all.py --tier fast` (P0+P1 et les gardes
  ajoutées depuis, ~3 s → garde de commit)
  et `--tier full` (fast + P2 étalons pixel + `--verify-refs`). `--install-hook` / `--uninstall-hook`
  posent un **hook git pre-push** (opt-in) lançant `--tier fast`.

**Pyramide de test COMPLÈTE (2026-07-09, élargie depuis)** : **P0** `neost-selftest` (logique pure :
chemins hôte, `neost.cfg`) + `--spec512-selftest` (borderless + bordé) + `--bus-selftest` +
`--mfp-selftest` + `--msa-selftest` + `--enec-selftest` · **P1** verdicts série
cartouche (cpu/timing/frame/ipl/fpu) + `run_selftests.py` · **cycle-bench** (`run_cyclebench.py`,
golden 68000) · **round-trip save-state** + **contrôle de la disquette livrée**
(`check_disk_assets.py`) · **P2** `ref_kind` oracle + diff par ligne + `--verify-refs` ·
**P3** `--from-cfg` · **orchestration** `run_all.py --tier fast|full` + hook pre-push.
Palier fast complet en ~3 s (mesuré 2026-08-19 ; il a grossi depuis les ~0,3 s d'origine). **Reste (faible priorité)** : gate `trace_diff --periods` vs oracle
Hatari (le cycle-bench actuel est une auto-régression NeoST) ; self-tests P0 supplémentaires (autres Timers,
ACIA) ; si une vraie démo spec512 **overscan** (bordures ouvertes) est rapatriée un jour → l'ajouter en
étalon oracle (l'auto_diapo, lui, est 100 % borderless).

### Outillage / qualité
- ✅ **OUTIL-1 — `--joy-at`, `--joy-script` et `--mouse-at` RENDUS RÉPÉTABLES (2026-08-25)**.
  Les trois sont passés en `std::vector` comme `--keys-at`, l'aide porte « (repeatable) », et les
  trois sites d'application bouclent sur les listes (règle de chevauchement : le dernier de la
  ligne de commande gagne sur les trames communes ; des scripts disjoints jouent tous). Vérifié :
  `--joy-at 10 0x80 --joy-at 30 0x08` produit désormais **deux** lignes « joystick applied »
  (une seule avant). ⚠ Piège de mise en œuvre à ne pas reproduire ailleurs : `--joy-at` consomme
  DEUX arguments, donc `emplace_back(next(a), next(a))` aurait un ordre d'évaluation **non
  spécifié** — les temporaires nommés sont obligatoires. **Reste ouvert** de ce dossier : c'étaient des **scalaires**
  là où `--keys-at` / `--key-down` / `--key-up` sont des **vecteurs** (`:1134-1136`) : la dernière
  occurrence gagne, **sans le moindre avertissement**. C'est le **principal fabricant de faux
  positifs du projet** — à lui seul il a produit **trois des huit « bloquants »** de la passe du
  2026-08-25 (Xenon 2, Flood, Dynamite Dux, tous jouables avec un script unique). Deux corollaires
  du même dossier :
  · `--joy-script` appelle `setJoystick(0, st)` à **chaque** trame (`:1696`) → il remet le port 0
    à zéro en silence, et `--joy 0x80,0x80` ne peut pas tenir avec lui ;
  · `--keys-at` ne tient la touche que **2 trames ≈ 40 ms** (make +0 / break +2, `:1620-1631`) —
    trop court pour certaines cracktros, et surtout **incomparable** à un `--cmd-fifo`
    `keydown`/`keyup` d'Hatari (~600 ms). ⚠ **Toute A/B contre l'oracle doit égaliser la DURÉE
    d'appui** : un verdict « confirmé à l'oracle » a déjà été rendu FAUX par cet écart.
  Reste à faire : une option de **durée d'appui** pour `--keys-at` (les 2 trames sont câblées en
  dur), et le pavé numérique — `stScancode()` ne mappe pas le **pavé
  numérique** (seuls `.` 0x71 et Enter 0x72), d'où des menus de compilation (Automation)
  impilotables — Hatari étant tout aussi insensible, il n'y a **aucun bug d'émulation** derrière.
- **Balayage de masse : monter les disques en LECTURE SEULE.** Un balayage des 67 images le
  2026-08-25 a laissé `disks/st/Eliminator-Nebulus (19xx)(A-Ha).st` **modifié dans l'arbre git**
  (le jeu écrit sur sa disquette, l'émulateur écrit dans le fichier). Restauré par
  `git checkout --`, mais il manque un garde-fou : une option `--disk-ro` (ou un `git status`
  systématique en fin de campagne) éviterait de commettre une image altérée par accident.
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

### Réseau (extensions NeoST — base livrée 2026-08-12, cf. `docs/EXTENSIONS.md`)
- 🔴 **PRIORITÉ AU REDÉMARRAGE — `NetBackendSlirp` : finir le dernier pas** (2026-08-22).
  Le backend Internet réel de la NE2000 (NetUSBee/EtherNEC) est **écrit, compilé, câblé et
  aux trois quarts prouvé** : `src/net/SlirpBackend.{hpp,cpp}`, option CMake `NEOST_WITH_SLIRP`
  (pkg-config `slirp` ; libslirp 4.9.3 présente sur le poste), drapeaux headless `--slirp` /
  `--slirp-restricted`, auto-test `--slirp-selftest`.

  **État : 3 vérifications sur 4 passent.**
  ```
  ARP: la passerelle 10.0.2.2 repond        OK
  DHCP: OFFER attribue 10.0.2.15            OK
  compteurs TX/RX du backend                OK
  SORTIE REELLE : DNS resout theoldnet.com  FAIL   <- reste a finir
  ```
  Les trois premières sont **déterministes et hors ligne** (servies par SLIRP lui-même) : ce
  sont elles qui iront en CI. La quatrième est **opt-in** (`NEOST_SLIRP_ONLINE=1`), la règle
  du projet interdisant qu'un étalon dépende du réseau.

  **Trois pièges déjà trouvés ET corrigés** (ne pas les re-chercher) :
  1. `register_poll_fd`/`unregister_poll_fd` sont marqués *deprecated* mais libslirp les
     appelle **sans tester leur nullité**, dès la première socket sortante -> SIGSEGV qui
     n'apparaissait qu'en ligne. Des no-ops suffisent.
  2. `clock_get_ns` doit partir de **~0**. libslirp fixe l'expiration d'une socket avec son
     `curtime` interne (encore nul avant le premier poll) puis la compare à cette horloge :
     avec le temps depuis le démarrage de la machine, toute socket UDP naissait « expirée »
     et était détruite au premier tour -> rien ne sortait jamais. Corrigé par `kEpoch`.
  3. SLIRP **ARPe l'invité** avant de livrer un paquet entrant (« qui a 10.0.2.15 ? »). Sur
     un vrai ST c'est STinG qui répond ; l'auto-test doit le faire lui-même. La réponse ARP
     est écrite dans `slirpSelfTest`.

  **Ce qui reste à diagnostiquer** : le datagramme sortant part bien — PROUVÉ, un serveur UDP
  local visé via 10.0.2.2 a reçu la charge utile et la socket hôte s'est liée — et SLIRP nous
  ARPe, mais la réponse DNS n'atteint pas encore l'anneau de réception. Pistes, dans l'ordre :
  a) vérifier que la réponse ARP fabriquée par l'auto-test est bien formée/acceptée ;
  b) `NEOST_SLIRP_TRACE=1` pour voir si `slirp->guest` porte enfin un IPv4/UDP ;
  c) sinon, regarder le filtre MAC de `Ne2000::deliverFrame` et l'anneau — l'auto-test
     n'avance JAMAIS `BNRY`, donc au-delà de ~58 pages la carte refuse les trames.
  Un banc minimal hors NeoST isole libslirp du reste (`scratchpad/slirptest.c`, non versionné,
  à recréer : ~80 lignes, il lit une trame en hexa et boucle sur fill/poll).

  **Ensuite seulement** : câbler `--slirp` dans le GUI (page Network), documenter dans
  `docs/EXTENSIONS.md` § NetUSBee, puis vérifier de bout en bout avec **STinG + ENEC.STX**
  côté ST (freeware, à récupérer) et un navigateur (CAB) sur theoldnet.com.

- **MIDI OUT Windows** : `MidiOutHost` couvre CoreMIDI (macOS) et ALSA (Linux) ; winmm reste à
  écrire — le MT-32 (Munt), lui, est portable.
- **Périphériques des ports — validation** (2026-08-23) : `PortDevices` transcrit Steem/WinUAE sans
  logiciel à clé sous la main. À exercer : Leader Board / 10th Frame (dump ST), B.A.T. II, Music
  Master, et l'option « Pro Sound » du menu de Wings of Death / Lethal Xcess (présents en STX) pour
  entendre le DAC. **Clé Notator** (`--dongle notator`, équations TPH) : à confronter à un Notator
  SL original (non cracké) — deux incertitudes à trancher sur le vrai matériel : le front de /ROM4
  qui cadence FEEDB1 (fin d'accès supposée) et l'ordre UDS↔/ROM4 à l'armement (données remises à 0
  supposées). Restent sans relevé public : Log 3 (EP330), Pro-24 (GAL16V8), Avalon / Synthworks
  (clé noire, équations ≠ Cubase 2), Zodiac, DynaBlaster. L'outil pour trancher existe : une capture
  matérielle au format `R3`/`R4`/`U` + `--key-replay` (recette dans `docs/EXTENSIONS.md`).
- **Dongles — frontends WASM/Android** : `PortDevices`/`CartridgeKey` ne sont exposés que par le GUI
  et le headless ; le menu Android (décalqué de la borne) et la démo web n'ont pas de page Dongles.
- **Clé Steinberg — validation** (2026-08-23) : `CartridgeKey` (rouge/noire, équations MiSTery) n'a
  jamais vu un Cubase 3.10 / Score / 2.01 réel. Il faut une disquette originale (non crackée) pour
  trancher ; la noire dépend en plus du motif bus exact de Moira. Option de confort : choisir une
  **destination** CoreMIDI (`MIDIGetNumberOfDestinations`) au lieu de la seule source virtuelle.
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
- **Sécurité** : liste blanche de domaines optionnelle pour les backends sortants.
