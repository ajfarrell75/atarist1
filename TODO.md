# TODO — NeoST

(c) 2026 VERHILLE Arnaud. **Ce qui reste à faire.**

- Ce qui est fait, par puce → [`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md)
- Titres déjà diagnostiqués (corrigés **ou** jugés fidèles) → [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md)
- Chronologie (le clos détaillé vit là-bas) → [`CHANGELOG.md`](CHANGELOG.md)

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

✅ **Verrous techniques levés** (détail → `CHANGELOG.md`) : Pages ne sert plus que du libre
(`NEOST_WEB_FREE_ONLY=ON`) ; les étalons sont **découplés** des ROM propriétaires — les 4
étalons à disque généré tournent sur EmuTOS (0 px vs TOS propriétaire, réfs inchangées), et
`run_etalons.py` distingue ROM libre (absente = **ÉCHEC**) et ROM Atari (absente = étalon
**sauté et RECENSÉ**, sans faux vert) ; les licences sont dans tous les paquets et gardées par
les 8 jobs de vérification de release.

❌ **Reste à trancher (décision du mainteneur)** :
1. `git rm --cached` sur `roms/tos*`, `disks/st`, `disks/stx`, `carts/`, `wasm/index.*`,
   les ajouter au `.gitignore`, puis **purger l'historique** (`git filter-repo`) — sans
   quoi le contenu reste téléchargeable dans les commits antérieurs. ⚠ GitHub Pages sert
   la branche `main` À LA RACINE : tout ce contenu est donc aussi téléchargeable **depuis
   le web** (habib256.github.io/neost/roms/…), pas seulement depuis git. Le déploiement
   par artefact réglerait ce point ; écarté le 2026-08-22 (le bundle doit rester dans
   l'arbre de travail). **Plus rien ne s'y oppose côté CI** ; c'est une réécriture
   d'historique, donc un choix, pas une tâche.
2. **Les paquets bureau redistribuent DEUX ROM Atari propriétaires** (`tos102uk.img`,
   `tos162uk.img`, profils « 520 ST » / « 1040 STE » — `src/main.cpp:1948-1949`).
   L'interrupteur existe — `NEOST_PACKAGE_NO_ATARI_TOS=1` produit un paquet 100 % libre
   (EmuTOS seul), et la CI l'honore — mais le **défaut reste inchangé** : le basculer est
   une décision.
3. `README.md` dit la vérité (« The packages also carry TOS 1.02 UK and TOS 1.62 UK ») ;
   il reste à les faire figurer au **tableau des composants tiers**, qui ne mentionne
   toujours pas Atari.

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

---

## 🏛 Dette d'architecture

**La revue du 2026-08-25 (A1-A8) est soldée**, à l'exception d'A3 : A1 palier pixel au push,
A2 étalons blitter (`blitter_timer`/`blitter_hog`) + `mfp_poll`, A4 instrument testé
(+ `--key-hold`, `--scancode-at`), A5 oracle épinglé et scripté, A6 barrière de débit
(`run_perfbench.py`), A7 ancres de doc vérifiées en CI, A8 GUI couvert (injection d'entrées,
souris scriptée, job CI `xvfb` bit-exact vs headless). **Détail → `CHANGELOG.md`
(2026-08-26).** Reste :

### A3 ◐ — Le corpus de régression n'est pas livrable

La couverture repose sur ~80 jeux commerciaux crackés et 44 ROM propriétaires. **Le filet de
sécurité ne peut pas être distribué avec le projet**, et un contributeur externe ne peut pas
reproduire la validation. C'est le même dossier que le bloquant release, vu sous l'angle
ingénierie. État : **6 étalons pixel sur 13** survivraient au retrait des TOS Atari
(`etos_ste_boot`, `overscan_top`, `trace_odd`, `scroll_8264`, `scroll_8265`, `blitter_timer`) ;
restent 7 adossés à des ROM propriétaires — le plan de conversion est **A10** ci-dessous.

### Revue architecte du 2026-08-26 — ce qui MANQUE encore pour solder la dette

Constat d'ensemble : la dette restante n'est presque plus de l'émulation — c'est de la
**reproductibilité pour un tiers** et de la **maturité produit**.

#### A9 ⭘ — `src/main.cpp` est un monolithe de ~5 000 lignes

Mesuré le 2026-08-26 : **4 980 lignes** — parsing d'options, chargement/écriture de `neost.cfg`,
profils machine, boucle principale, pages ImGui, injection d'entrées (`--scancode-at`,
`--mouse-at`…) dans un seul fichier. La dette est **confinée au bon endroit** (`neost_core` reste
sans dépendance GUI, le cœur est bien découpé) mais elle est concentrée : chaque nouvelle option
ou page GUI grossit le même fichier, et `tools/check_headless_options.py` protège le
**comportement** de cette surface, pas sa structure. 🎯 À faire, sans big-bang : extraire d'abord
le **parsing d'options** et la **config** vers des unités propres (elles ont déjà leurs tests en
boîte noire, le refactor est donc gardé), le reste peut suivre par opportunité. Ne PAS refondre
la boucle principale en même temps.

#### A10 ⭘ — Convertir les 7 étalons encore adossés à des ROM propriétaires (suite d'A3)

`spectrum512_diapo`, `spectrum512_diapo2`, `spectrum512_diapo_ste`, `cuddly_demos`,
`union_demo`, `nocooper`, `nocooper_greetings`. Deux recettes éprouvées, au choix par étalon :
la **migration EmuTOS** (comme les 4 étalons à disque généré : capture EmuTOS vs TOS
propriétaire = 0 px, contrôlée à l'oracle) quand le contenu le permet, ou un **étalon généré**
équivalent (esprit `tools/make_blitter_test.py`) quand le disque lui-même est le problème
(démos commerciales). Tant que ce n'est pas fait, la purge du § BLOQUANT RELEASE ampute le
filet pixel de plus de moitié — c'est LE verrou qui rend la purge coûteuse, donc l'item qui la
débloque vraiment.

#### A11 ⭘ — L'oracle ne tourne dans aucune CI

Toute la méthode repose sur Hatari, désormais épinglé et scripté (A5) — mais les références
`ref_kind: oracle` ne sont **re-vérifiables que sur un poste où quelqu'un a bâti Hatari**. Rien
ne détecte une réf oracle qui aurait dérivé (mauvaise re-pose, pin bougé, option oubliée).
🎯 À faire : un job **planifié ou manuel** (pas au push — bâtir Hatari coûte) qui clone au pin
via `tools/setup_hatari.sh`, régénère les captures oracle des étalons `oracle` et compare aux
réfs commises. Fermerait la seule boucle de validation encore entièrement manuelle.

#### A12 ⭘ — Aucune cible de livraison n'a été validée sur du matériel réel

L'écart entre « livré » et « vérifié » : **Windows** est livré depuis la 0.5.1 mais n'a jamais
tourné hors CI ; l'**APK Android** n'a jamais touché un appareil réel (QEMU seulement, dit par
`CLAUDE.md` lui-même) ; la **borne Raspberry Pi** est une cible déclarée mais
`tools/run_perfbench.py` ne garde que des **ratios** sur la machine de dev — aucun budget
temps réel (« les 50 trames/s tiennent-elles sur le Pi visé ? ») n'a jamais été mesuré sur la
cible. 🎯 À faire : une passe de validation PAR CIBLE (une session Windows réelle, un appareil
Android, un run perfbench sur Pi), chacune consignée avec sa config — c'est de la mesure, pas
du code.

#### Reliquats suivis ailleurs (rappelés ici pour la vue d'ensemble, ne pas dupliquer)

- **A13** = save-states × GEMDOS HD (F7, handles hôtes hors snapshot — le save-state MENT
  pendant qu'un fichier est ouvert sur C:) → § *Périphériques & profils machine*.
- **A14** = pas de garde lecture-seule pour les balayages de masse (`--disk-ro`, une image de
  jeu a déjà été modifiée dans l'arbre git) → § *Outillage / qualité*.
- **A15** = DSL d'injection sans token « mouvement bouton tenu » (pas de DRAG GEM) → reliquat
  d'A8.
- Signature/notarisation du `.dmg`, tableau des tiers → § BLOQUANT RELEASE.
- Slirp « dernier pas » (DNS→anneau RX) → § *Réseau*, priorité au redémarrage.
- SCSI/NCR5380 + TOS 2.05/2.06 + NVRAM (l'objectif MegaSTE déclaré en tête de ce fichier n'est
  pas atteint sans eux) → § *Stockage & contrôleurs* et § *Périphériques & profils machine*.

### ⚠ Deux erreurs de méthode commises le 2026-08-25, consignées pour ne pas les refaire

- **Un seuil absolu sur une grandeur dépendante de la charge.** `timer IRQ max lateness` avait été
  inscrit ici comme sonde de non-régression « doit rester à **132** ». Faux : 147, 156, 157 et 163
  relevés sur d'autres titres. Corrigé — cette métrique se compare **à charge identique**, jamais
  à un seuil. Un faux garde-fou coûte plus cher qu'aucun garde-fou.
- **Justesse validée, coût ignoré.** BL4 a été validé au pixel et au barème sans **aucune** mesure
  de débit, alors que le changement multiplie les appels au dispatch (corrigé depuis par le
  perfbench : coût mesuré nul).

---

## Catalogue logiciels — bugs OUVERTS

Rapports terrain non expliqués. TOS 1.02fr sauf mention. Chemins sous `disks/st/` (`.st`)
ou `disks/stx/` (`.stx`). Pilotage headless : `--keys`/`--joy-at`, trace `--irq`, diff
Hatari.

| Jeu | Symptôme | Piste / renvoi |
|-----|----------|----------------|
| **Shadow Warriors** (2Hot2Handle) | Après SPACE : titre + musique OK ; le bouton joystick ne lance pas le jeu. (Castle Warrior, lui, fonctionne.) | À diff'er Hatari — le pilotage **joystick** de l'oracle est désormais possible (recette A5 → `docs/HATARI_AUTOMATION.md`) ; égaliser la durée d'appui (`--key-hold`). |

Suivis mineurs laissés ouverts sur des cas par ailleurs tranchés :
- **Lethal Xcess** — titre « buggé à ~8 % » constaté en GUI (2026-07-02), probablement la
  même calibration `$8209` que l'in-game déjà réparé ; à re-vérifier en GUI.
- **Stardust STE** — résidu non élucidé du dossier D-PSG (clos) : l'oracle affichait « INSERT
  DISK 2 IN ANY DRIVE » là où NeoST fond au noir puis poll le lecteur B ; à revoir si les
  disquettes 2/3 (absentes du dépôt) réapparaissent.

> ⚠ **Avant de déclarer un bug : vérifier la RAM, puis la ROM.** Le réflexe et les cas
> qu'il a tranchés → [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md).

> **Déjà expliqués** (corrigés ou jugés fidèles) : Captain Blood, Enchanted Land, Lethal
> Xcess, The Cuddly Demos, Rick Dangerous II, Stardust, Spectrum 512 STE, Blood Money,
> HotPot, Arkanoid, Wings of Death, Beyond the Ice Palace →
> [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md).

---

## 🔬 Divergences Hatari restantes

**Inventaire maître** (sévérité + impact + `fichier:ligne` des deux côtés) :
[`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md). Fidélité globale **très élevée** ;
aucune divergence ne casse un boot EmuTOS/`.ST`. Le terrain **logique** est épuisé (tous les
écarts bornés et vérifiables sans oracle sont corrigés) ; ne restent que les écarts
**cycle-exacts** et quelques cas-limites documentés.

> **L'oracle se bâtit, il n'arrive pas tout seul** : `extern/hatari` est GITIGNORÉ et n'est
> PAS un sous-module — sur une machine fraîche il est ABSENT. `tools/setup_hatari.sh` clone au
> pin (`f0736b2`) et bâtit avec les options macOS obligatoires ; recettes →
> [`docs/HATARI_AUTOMATION.md`](docs/HATARI_AUTOMATION.md).

Restent, par priorité d'impact :

1. **[JOUEUR] Beam-sync** — phase CPU↔faisceau **par-ligne** (overscan vertical). Casse EL /
   Cuddly / SHO en jeu. → `docs/MOIRA_WINUAE_CONVERGENCE.md`, `docs/CYCLE_ACCURACY.md` §4.
2. **[VIDÉO]** V3 géométrie mid-trame (50↔60 Hz) : le restart du compteur est porté
   (`VC_RESTART`), reste l'attribution de ligne fixe (canal `NEOST_LINELEN` hybride).
3. **[SON]** quantification HBL du refill FIFO à confronter à l'oracle sur un poll serré de
   `$FF8909/0B/0D` — validable par dump WAV + trace.
4. **[MFP]** `UpdateTimers` avant lecture IPR/ISR/TBDR en mode bloc (≤ 1 instruction de retard).
5. **[FPU]** arrondi de précision FMOVE/FABS/FNEG selon FPCR — validable par ROM de test étendue.
6. **[BLITTER]** résidu BL5 : ~10 cyc par démarrage de blit + ~3,3 par reprise de tranche,
   **paradoxe de signe non levé** entre les deux instrumentations — aucune correction sans
   3ᵉ mesure indépendante. Détail et hypothèses déjà réfutées (6) → entrée **BL5** de
   `docs/HATARI_DIVERGENCES.md`.

**Faisables sans oracle** : FPU packed decimal bit-exact ; GEMDOS recomposition Unicode NFD→NFC
(cible macOS) — détaillés dans `docs/HATARI_DIVERGENCES.md`.

**Décisions actées (NE PAS « corriger » vers Hatari)** : SCC `WR14` bit4 loopback (datasheet
Zilog, NeoST plus fidèle) ; WRITE/READ TRACK STX réinterprétés (NeoST rend la piste lisible) ;
densité HD/ED STX (NeoST plus cohérent) ; RTC en temps émulé (déterminisme headless).

---

## 🎯 Précision cycle

> **Plan, acquis et inventaire priorisé du restant** → [`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md).
> **Front actif (beam-sync, convergence Moira↔WinUAE)** → [`docs/MOIRA_WINUAE_CONVERGENCE.md`](docs/MOIRA_WINUAE_CONVERGENCE.md).

Convergence **instruction** Moira↔WinUAE : complète. Reste la **phase d'entrée d'IRQ** et la
**géométrie par-ligne** (beam-sync, P1) : la dérive *moyenne* du faisceau correspond à Hatari
(+78/ligne) mais la **phase absolue par-ligne** diffère (NeoST culmine cyc 476-492 vs Hatari
500-508) → le retrait haut d'Enchanted Land en jeu ne « tient » pas. Fermeture = tracking
cycle-exact du handler **par ligne** (alternance 76/80 d'Hatari), pas un offset constant.
Inventaire priorisé P1-P3 (beam-sync, res-tricks, géométrie mid-trame, wakeup states, unité
interne ×256…) → `docs/CYCLE_ACCURACY.md` §4.

---

## Roadmap par sous-système — items ouverts

> Le reste (Bus/MMU, FDC, YM2149, GEMDOS, ACSI, SCC, FPU, imprimante, MegaSTE 8/16 MHz + cache…)
> est **fait et validé** — voir `CHANGELOG.md`. Ci-dessous, uniquement ce qui reste ouvert.

### Vidéo / Shifter
- **Raffinements cycle-exact** (→ `docs/CYCLE_ACCURACY.md` §4) : beam-sync par-ligne, tricks par
  changement de résolution (V2 hi/med/lo, overscan med-res), géométrie mid-trame (V3), rendu live
  du retrait **bas** (scroller Cuddly) + lignes EMPTY/BLANK/NO_DE, mode 336 px STE
  (`bSteBorderFlag`), wakeup-state WS3 (sous-pixel).
- **Résidu du latch couleur bordure gauche** (le latch lui-même est corrigé, 2026-07-09) :
  16 px (cols 45-60) = la **position horizontale exacte** où l'écriture palette prend effet
  (Hatari bascule ~16 px après le début nominal de l'aire active = latence pipeline ; NeoST
  bascule pile à `activeX_`). Invisible aux étalons. _Valeur très basse._

### Interface — kiosk & effets CRT
Fonctionnalités livrées et fonctionnelles (→ `CHANGELOG.md` § Frontend). Restent :
- **Cosmétique** : membres `srcW_`/`srcH_` morts dans `CrtEffectStack` ; destructeur `= default`
  (fuite GL seulement si l'objet cessait d'être un singleton process-lifetime) ; répétition de
  navigation kiosk : tenir gauche/droite (swap one-shot) bloque la répétition haut/bas.
- **CRT v1 assumé** : en kiosk, baril/vignette encadrent le buffer ST ENTIER (bords courbés
  rognés hors écran en zoom fort) ; contexte GL 2.1 (vieux macOS) → passthrough (pas d'effets).

### Son DMA STE
- Confronter la **quantification HBL du refill FIFO** à l'oracle sur un poll serré du compteur
  `$FF8909/0B/0D` (le reste — FIFO 8 octets, compteur live, gains LMC — est fait).
  _Effort faible, valeur basse._

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

### Système de régression — restes (faible priorité)
La pyramide P0-P3 est **en place** (palier fast ~3 s garde le commit, palier full = pixels ;
détail → `DEV.md` et `CHANGELOG.md`). Restent :
- gate `trace_diff --periods` vs oracle Hatari (le cycle-bench actuel est une auto-régression
  NeoST) ;
- self-tests P0 supplémentaires (autres Timers, ACIA) ;
- si une vraie démo spec512 **overscan** (bordures ouvertes) libre est rapatriée un jour →
  l'ajouter en étalon oracle (l'auto_diapo est 100 % borderless).

### Outillage / qualité
- **Balayage de masse : monter les disques en LECTURE SEULE.** Un balayage des 67 images le
  2026-08-25 a laissé `disks/st/Eliminator-Nebulus (19xx)(A-Ha).st` **modifié dans l'arbre git**
  (le jeu écrit sur sa disquette, l'émulateur écrit dans le fichier). Restauré par
  `git checkout --`, mais il manque un garde-fou : une option `--disk-ro` (ou un `git status`
  systématique en fin de campagne) éviterait de commettre une image altérée par accident.
- **Étalons headless** : calibrer frames + références Cuddly / Union / Troed / Hatari Test Suite ;
  rapatrier Union (planetemu manuel). Infra en place (`tools/run_etalons.py`).
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
