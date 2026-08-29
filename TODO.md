# TODO — NeoST

(c) 2026 VERHILLE Arnaud. **Ce qui reste à faire — uniquement l'OUVERT.**

- Ce qui est fait, par puce → [`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md)
- Titres déjà diagnostiqués (corrigés **ou** jugés fidèles) → [`docs/CASE_STUDIES.md`](docs/CASE_STUDIES.md)
- Chronologie (le clos détaillé vit là-bas, y compris tout ce qui a été retiré d'ici) → [`CHANGELOG.md`](CHANGELOG.md)

**Objectif** : émuler proprement un **MegaSTE** (68000 8/16 MHz, 1/2/4 Mo, TOS 2.05/2.06, STE
vidéo/son/joypads, blitter, RTC, SCC, SCU, ACSI — le disque interne d'époque est un pont
ACSI-SCSI, PAS un NCR5380, DD/HD) avec un timing assez fidèle pour jeux, démos et utilitaires.
**Atteint et GARDÉ** (2026-08-27) : `tools/run_megaste_diag.py` rejoue la suite Q du
diagnostic Field Service (12/12) à chaque palier `full`. Ce qui suit affine, il ne
conditionne plus l'objectif.

**Sources de vérité à croiser systématiquement** (cf. [`CLAUDE.md`](CLAUDE.md)) :
- **Hatari** (`extern/hatari/src/*.c`) — comportement ST/STE/MegaSTE éprouvé. La référence.
- **MAME** (`src/mame/atari/atarist.cpp`, `stmmu.cpp`, `stvideo.cpp`, devices `mc68901`,
  `wd_fdc`, `6850acia`, `z80scc`, `rp5c15`, `ay8910`, `lmc1992`) — composants séparés.

**Documentation connexe** :
- Précision cycle (modèle, acquis, restant) → [`docs/CYCLE_ACCURACY.md`](docs/CYCLE_ACCURACY.md)
- Beam-sync (chantier CLOS — journal, « ÉTAT COURANT » en tête) → [`docs/MOIRA_WINUAE_CONVERGENCE.md`](docs/MOIRA_WINUAE_CONVERGENCE.md)
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
| `disks/midi/CUBLITE/` | **33 fichiers de Cubase Lite** (Steinberg 1996) — logiciel **commercial**, moteur du seul étalon MIDI | ~0,5 Mo |
| `dev/` | tiers commis : `dev/agt` (« aucun fichier LICENSE explicite »), `dev/reservoir-gods/` sans licence avec `.exe` précompilés et une `license.txt` UnRAR (non libre) | ~50 Mo |

Conséquences : cloner le dépôt (ou télécharger le tarball GitHub) livre une archive de
logiciels sous copyright — et **GitHub Pages sert `main` à la racine**, donc tout est aussi
téléchargeable depuis le web. Verrous techniques levés (Pages libre-seulement, étalons
découplés, `rom_is_free()`, licences gardées par 8 jobs) → détail au `CHANGELOG.md`.
Les chiffres du tableau sont gardés par `tools/check_doc_claims.py`.

**Séquencement de la purge** (l'ordre casse la circularité « purger ampute le filet ») :

1. **A10 d'abord** — convertir les étalons pixel encore adossés à des ROM/disques
   propriétaires (§ Dette d'architecture). C'est LE verrou qui rend la purge coûteuse.
   **3 sur 7 migrés le 2026-08-28** ; les 3 spec512 ne migreront pas (réfuté à l'oracle)
   et deviendront des SKIP recensés le jour de la purge — décision à assumer, ou à
   racheter par un étalon spec512 GÉNÉRÉ.
2. ✅ **FAIT le 2026-08-28 — le palier `fast` ne dépend plus d'aucun fichier
   propriétaire.** `run_selftests.py` (`diag_cart`) et `run_cyclebench.py` sont passés sur
   `etos192fr` : leurs deux programmes prennent la main **avant le TOS** (cartouche
   $FA52235F, cartouche bench), la ROM ne sert qu'à construire la machine — vérifié, pas
   supposé (dump série identique octet pour octet sous `tos102uk` / `etos192fr` /
   `etos192us` ; golden `cyclebench.json` posé sous `tos102uk` passe tel quel, tolérance
   0 cycle). `run_midi_sequencer.py` ne peut PAS migrer (auto-lancement `#Z` du
   DESKTOP.INF non honoré par EmuTOS — mesuré : on reste sur le bureau, 0 octet MIDI ;
   et Cubase Lite resterait propriétaire de toute façon) : il applique désormais la
   politique de **SKIP recensé**, comme `run_megaste_diag.py`. Convention posée :
   **code de sortie 77** = « sauté, recensé », et `run_all.py` liste ces étapes dans son
   bilan de fin (« TOUS LES PALIERS OK — COUVERTURE AMPUTÉE ») au lieu de les engloutir
   dans un vert plein.
3. **La purge elle-même** (décision du mainteneur — réécriture d'historique) :
   `git rm --cached` sur `roms/tos*`, `disks/st`, `disks/stx`, `carts/`,
   `dev/reservoir-gods`, `dev/agt`, ajout au `.gitignore`, puis `git filter-repo` — sans
   quoi le contenu reste téléchargeable dans les commits antérieurs.
4. **Basculer le défaut des paquets** : `NEOST_PACKAGE_NO_ATARI_TOS=1` (l'interrupteur
   existe et la CI l'honore ; aujourd'hui les paquets bureau redistribuent `tos102uk.img`
   et `tos162uk.img` par défaut).
5. ✅ **FAIT le 2026-08-28 — les composants tiers sont tous nommés, avec leur licence.**
   README **et** `packaging/licenses/THIRD-PARTY.txt` (celui qui accompagne les paquets)
   listent désormais libmt32emu (LGPL 2.1+, **lié statiquement** — la note explique
   pourquoi la GPL 3 du dépôt satisfait la condition de relien), stb_image, SDL2 2.30.9
   (paquet Android), la mention ⚠ des TOS Atari livrés par défaut, et **GLFW** — qui
   n'était nommé NULLE PART alors qu'il est statique dans tous les paquets de bureau ;
   c'est l'omission que le pas 5 n'avait pas vue. `libslirp` est documenté comme
   dépendance de compilation **optionnelle et non livrée** (les builds de release sont
   faits sans elle), et sa licence corrigée dans le `CMakeLists.txt` : **BSD-3-Clause**,
   pas « LGPL 2.1+ » comme l'annonçait le commentaire. Verrou : `tools/check_licenses.py`
   (palier `fast`) exige que tout composant livré soit nommé, avec une licence, dans les
   DEUX documents — fil-piège vérifié en le déclenchant. Reste hors de sa portée : les
   bibliothèques que `linuxdeploy` embarque seule dans l'AppImage.

Conformité annexe (non bloquante) :
- `packaging/linux/make_appimage.sh` tire `linuxdeploy`/`appimagetool` depuis le tag mouvant
  `continuous` sans somme de contrôle pour arm64, alors que le `Dockerfile.bionic` épingle
  par SHA256.
- `.dmg` macOS ni signé ni notarisé (Gatekeeper : « NeoST est endommagé ») ; `.zip` Windows
  non signé. À traiter **après** la purge (A37) — signer un paquet contenant des ROM Atari
  n'aurait pas de sens.

---

## 🏛 Dette d'architecture — items ouverts

Issus de l'audit quatre dimensions du 2026-08-27 et des revues antérieures. Le soldé
(A1-A8, A16-A27, les leçons de méthode) vit au `CHANGELOG.md` — la numérotation A*n*
est continue, les trous sont du travail fait.

### Reproductibilité & maturité produit (hérités)

- **A3 ◐ — Le corpus de régression n'est pas livrable.** La couverture repose sur les 68
  jeux crackés et 37 ROM propriétaires du § BLOQUANT. Recompté 2026-08-28 (A10) : **12 étalons
  pixel sur 16** survivraient au retrait des TOS Atari (`etos_ste_boot`, `overscan_top`,
  `trace_odd`, `scroll_8264`, `scroll_8265`, `blitter_timer`, `blitter_hog`, `mfp_poll`,
  plus `cuddly_demos`, `nocooper`, `nocooper_greetings` migrés sur EmuTOS, et
  `spec512_bands` — l'étalon GÉNÉRÉ qui rend la couverture « palette en cours de ligne ») ;
  les 4 restants sont le reliquat d'**A10**.
- **A9 ⭘ — Découper `main()`** (`src/main.cpp`, mesuré 2026-08-29 : **5 013 lignes**, dont
  `main()` ≈ 2 490 avec une boucle de ~1 530 lignes et **84 globaux `g_*`** — chiffre gardé
  par `check_doc_claims.py`). ⚠ +199 lignes depuis le 2026-08-27, dont l'essentiel est la
  page MIDI (matrice d'aiguillage, liste de fusion) : chaque fonctionnalité d'interface
  atterrit ici faute de découpage, ce qui rend ce chantier un peu plus cher à chaque fois. Le filet préalable existe (boot GUI dans `run_all.py`, mode
  harnais sans trace — cf. `CHANGELOG.md`) ; reste le découpage : une `struct App`
  absorbant les globaux, `main()` sous 300 lignes, en généralisant le pattern de requêtes
  de `MediaPages`. Ne PAS refondre la boucle en même temps qu'autre chose.
- **A10 ◐ — Étalons adossés à des ROM propriétaires : 3 migrés, 4 restants.**
  Migrés sur `etos192fr` le 2026-08-28, référence commise INCHANGÉE (0 px / 114816,
  crop `buffer_noled`) : `cuddly_demos` (--frames 3500 → 3655), `nocooper` (6802 → 6932),
  `nocooper_greetings` (29500 → 29700). Ces démos bootent depuis le disque : le TOS ne
  fait que les charger, SEULE la durée du boot change. Recette (à réutiliser) — balayer
  les trames à **pas 1** autour de la cible et retenir CELLE qui est à 0 px, jamais la
  moins pire ; sur `cuddly_demos` la trame voisine est déjà à 7 548 px, sur `nocooper` à
  19 069 px. Détail et preuves dans le `rom_note` de chaque entrée d'`etalons.json`.
  Restent :
  - ✅ **La couverture spec512 est RENDUE, sans ROM Atari** (2026-08-28) :
    `spec512_bands`, étalon **généré** (`tools/make_spec512_test.py`) — secteur de boot
    autonome qui martèle `palette[1]` trois fois par ~100 cycles sur un écran entièrement
    à l'index 1. La position horizontale de chaque bascule dépend du cycle exact de
    l'écriture : c'est le seul endroit du rendu où un cycle de CPU se voit à l'œil.
    **0 px contre l'oracle Hatari** sur ROM libre. Les trois étalons ci-dessous restent
    donc des SKIP recensés le jour de la purge, mais plus rien d'essentiel ne part avec eux.
  - **`spectrum512_diapo`, `spectrum512_diapo2`, `spectrum512_diapo_ste` — migration
    EmuTOS RÉFUTÉE, ne pas retenter.** Ce disque n'a pas de secteur de boot exécutable
    (somme $FB35) : la diapo est lancée par le dossier `AUTO`, et sous EmuTOS le
    programme démarre puis abandonne (bureau figé dès la trame 600). Ce n'est pas un bug
    NeoST — **l'oracle Hatari + EmuTOS rend le même bureau** (écart entièrement dans la
    bande de la LED disquette d'Hatari) ; `etos256fr` échoue pareil. Seule voie restante :
    l'**étalon généré** (esprit `tools/make_overscan_test.py`) — un secteur de boot
    autonome qui écrit la palette en cours de ligne, calé à l'oracle.
  - **`union_demo`** : disque absent du dépôt (fetch planetemu manuel), donc non testable
    — appliquer la recette ci-dessus le jour où il revient.
  - **Prix de la migration, mesuré au repos** : le palier pixel passe de **46 s à 50 s**
    (`run_etalons.py` complet, 2 runs de chaque côté en alternance). Le mur reste
    `nocooper_greetings` : **41,3 → 45,1 s**, dont 0,7 % de trames ajoutées et ~8 % de
    cœur — NeoST émule cette démo un peu plus lentement sous EmuTOS, à image identique.
    ⚠ Mesuré d'abord SOUS CHARGE, ces mêmes écarts sortaient à 46→90 s et 43→63 s : une
    durée sans description de la charge n'est pas une mesure (leçon du 2026-08-25,
    re-jouée).
  - Bonus NON acquis : raccourcir `nocooper_greetings` (il borne à lui seul le mur du
    palier pixel). Trois tentatives mesurées le 2026-08-28 — espaces resserrés à 2 000
    puis à 600 trames d'intervalle, puis AUCUN espace : l'écran greetings n'est jamais
    atteint (au mieux 24 508 px). Et décaler les 5 espaces de +141 trames ne change rien
    à l'arrivée (greetings toujours à 29 610) : la durée de la dernière partie ne dépend
    pas d'eux. La démo joue ses parties à son rythme (la trame change sans touche à
    2 000, 2 800, 3 700…) — un espace anticipé n'est pas pris. Trancher demande de savoir
    QUAND la démo relit le clavier, pas de re-tirer au hasard un calendrier.
- **A11 ⭘ — L'oracle ne tourne dans aucune CI.** Job planifié ou manuel (pas au push) qui
  clone Hatari au pin via `tools/setup_hatari.sh`, régénère les captures des étalons
  `ref_kind: oracle` et compare aux réfs commises. Fermerait la dernière boucle de
  validation entièrement manuelle. ✅ Le volet « statuer sur les deux étalons `snapshot` à
  écart oracle inexpliqué » est **SOLDÉ le 2026-08-29** (détail : `CHANGELOG.md`,
  `docs/HATARI_DIVERGENCES.md` 11ᵉ passe) — `trace_odd` était un **artefact de mesure**
  (masque LED d'un pixel trop étroit) et est promu `ref_kind: oracle` à 0 px ; il reste de
  cet item l'**écart réel d'`overscan_top`**, ci-dessous.
- **A40 ⭘ — `overscan_top` : 144 px d'écart oracle RÉEL, localisé, cause non établie.**
  Sorti d'A11 le 2026-08-29. Stable sur les 61 trames de la fenêtre (structurel, pas de
  phase). Les 144 px sont sur les **5 premières lignes de trame**, celles qu'ouvre le
  retrait de bordure haute ; **au-delà les deux images sont identiques au pixel**, donc le
  retrait de bordure haute lui-même est CONFORME. Ce qui diverge est la **bordure gauche**
  sur ces lignes de transition : NeoST garde une fenêtre de 320 px décalée de −4
  (`glue::LEFT_OFF`, `default: −4` de la table `shEff`, `Shifter.cpp`), Hatari rend une
  fenêtre de **336 px** dont les 16 px de tête et de queue portent des index ≠ 0/15.
  ⚠ Ce chemin est calibré à 0 px contre No Cooper, Closure et Cuddly : **ne rien régler sur
  la foi de ce seul étalon** — lire `video.c` d'abord (méthode imposée), et la référence
  reste une self-capture tant que ce n'est pas compris.
- **A12 ⭘ — Aucune cible de livraison validée sur du matériel réel.** Windows jamais lancé
  hors CI, APK Android jamais posé sur un appareil (QEMU seul), aucun budget temps réel
  mesuré sur le Raspberry Pi visé (le perfbench ne garde que des ratios sur le poste de
  dev). Une passe de validation PAR CIBLE, consignée avec sa config — de la mesure, pas
  du code.
- **A13** = save-states × GEMDOS HD → § *Périphériques & profils machine*.
- **A15** = DSL d'injection sans token « mouvement bouton tenu » (pas de DRAG GEM).

### Consolidation

*(vide — A16b, A28, A29 et A30 sont soldés le 2026-08-28 ; détail au `CHANGELOG.md`.)*

### Chantiers structurels (UN à la fois, jamais combinés)

- **A32 ◐ — Découper `Shifter` : le fichier et les NOMS sont faits, les CLASSES
  restent.** Fait le 2026-08-28 (détail au `CHANGELOG.md`) : `Shifter.cpp` passe de
  **2 917 à 1 331 lignes**, la machine à états des bordures vit dans `VideoGlue.cpp`
  (1 032 l.) et le compteur vidéo dans `VideoCounter.cpp` (441 l.) ; le vrai GLUE
  vidéo a son en-tête (`VideoGlue.hpp`) et le stub MMU son vrai nom (`MmuGlue.hpp`) ;
  les **4 `const_cast`** de `videoCounter()` ont disparu (la méthode n'est plus
  `const` — elle avançait la machine Glue).
  **Ce qui reste, et pourquoi ça n'a pas été fait d'un bloc** : les trois rôles sont
  toujours des MÉTHODES DE `Shifter`, parce qu'ils partagent son état par-ligne
  (`glueLines_`, `glueLineStart_`, `liveGlue*`, `vc*`). En faire trois classes
  demande d'abord de trancher **qui possède cet état** — et une réponse bâclée y
  ajouterait des accesseurs croisés, c'est-à-dire le même couplage avec plus de
  cérémonie. Prochain pas concret : extraire `VideoCounter` en objet membre (ses
  champs `vc*` sont les moins partagés — `vcFrameBase_`, `vcLineBase_`, `vcLineY_`,
  `vcRestart*`), mesurer ce que ça casse, et seulement ensuite regarder la Glue.
- **A33 ◐ — Mono-instance CPU LEVÉ ; l'état est par instance, le parallélisme reste.**
  Fait le 2026-08-28 (détail au `CHANGELOG.md`) : le
  `throw std::logic_error("Cpu68k supports only one live instance")` a disparu, les
  **25 globaux d'état** sont devenus une `struct CpuState` possédée par `Cpu68k`, et
  ses **135 accès internes** passent par `state_` (son propre état) et non plus par le
  pointeur d'instance active. Prouvé, pas supposé : `selftest_logic.cpp` construit
  DEUX `Cpu68k` sur deux `Bus`, vérifie que chacun prend le vecteur de reset de SON
  bus, et qu'en faire tourner un ne bouge ni l'horloge ni le PC de l'autre. Les
  **23 verrous de configuration de processus** restent globaux — les rendre
  par-instance serait faux (cf. `tools/env_locks.json`).
  **Reste** : le vrai PARALLÉLISME. Le modèle est « à tour de rôle » — les callbacks
  Moira et les fonctions libres du fichier n'ont pas de `this` et passent par `g_cur`,
  posé à l'entrée de `run()`/`reset()`. Deux CPU dans DEUX THREADS demanderaient de
  supprimer ces 94 accès restants (passer le contexte aux callbacks, ou un
  `thread_local`) — ce n'est pas ce qu'A33 promettait, et aucune fonctionnalité ne le
  réclame aujourd'hui.
- **A35 ◐ — Le fork Moira : le pin est RETROUVÉ, le `Cputester` reste à évaluer.**
  Fait le 2026-08-28 : `extern/moira/NEOST_VENDOR.md` porte désormais le **pin de
  départ** — `1efd69467ca13b27b2fb40febd5cb31dbecdea5f`, l'amont
  `dirkwhoffmann/Moira` au premier commit d'intégration (2026-06-01) — retrouvé dans
  l'historique NeoST lui-même : le gitlink du sous-module y est encore, et la commande
  qui le déterre est écrite dans le fichier. Le tableau donne aussi les trois commits
  du fork local disparu, dont le tip (`a1e52ec`) dont le CONTENU est le code vendorisé.
  La recette de rebase et la commande de `diff` contre l'amont sont écrites.
  **Reste** : (1) vérifier d'un `git ls-remote` que `1efd6946` existe toujours chez
  l'amont — non fait, aucun accès réseau utilisé pour établir ce qui précède ; (2) le
  `Cputester`. Sa réintégration DANS le dépôt est écartée (≈713 Mo de corpus ADF, dans
  un dépôt public qu'on cherche justement à alléger) : la voie est la même que pour
  l'oracle Hatari — clone hors arbre, gitignoré, recette documentée. Ses dépendances
  propres n'ont pas été évaluées.
- **A37 ◐ — Discipline de release : la procédure est écrite et gardée, le TAG est
  une décision de mainteneur.** Fait le 2026-08-28 : [`docs/RELEASE.md`](docs/RELEASE.md)
  écrit la procédure en sept pas (dont le piège du cache `NEOST_VERSION_STR`, celui
  qu'on saute) ; `tools/check_release.py` (palier `fast`) exige que les TROIS numéros
  disent la même chose — `CMakeLists`, « Version courante » du CHANGELOG, dernière
  en-tête de release — et refuse un numéro sauté en silence ; le saut de **0.5.3** est
  désormais consigné au `CHANGELOG.md` (§ *Numéros de version sautés*) avec ce qu'on
  sait : elle n'a jamais existé, le bump `dec5929` est passé de 0.5.2 à 0.5.4, et la
  raison n'est pas reconstituable.
  **Reste, et ce sont des décisions du mainteneur, pas du travail technique en
  attente** : (1) **poser le tag** — 128 commits depuis la 0.5.4 du 2026-08-23, dont le
  MegaSTE 12/12, CAB/theoldnet, l'audit et le plan A16-A37, puis A10/A14/A16b/A28-A36 et
  les pas 2 et 5 de la purge : le numéro et le moment appartiennent au mainteneur ;
  (2) **signer / notariser** le `.dmg` et le `.zip`, à faire APRÈS la purge (signer un
  paquet contenant des ROM Atari n'aurait pas de sens).

### Issus de l'évaluation d'architecture du 2026-08-28

- **A38 ⭘ — Le palier `fast` lance 17 outils : un garde-fou qui ne mord jamais devient
  du bruit.** Quatre ont été ajoutés le 2026-08-28 seul (`check_licenses`,
  `check_env_locks`, `check_release`, le fuzz des parseurs). Chacun se justifie et
  chacun a attrapé quelque chose de réel *le jour de sa pose* — mais rien ne le
  vérifiera plus jamais. Le risque n'est pas le coût (~12 s) : c'est qu'un contrôle
  cesse d'attraper quoi que ce soit, que personne ne le sache, et qu'on finisse par
  désarmer le lot en bloc.
  À faire : (1) tenir, pour chaque contrôle, la **trace de son dernier
  déclenchement RÉEL** — un fil-piège volontaire ne compte pas ; (2) décider d'une
  règle de retrait (un contrôle qui n'a rien attrapé en N mois se justifie à nouveau ou
  s'en va) ; (3) re-mesurer le mur du palier `fast` à chaque ajout, et le dire.
  ⚠ Ne PAS traiter en ajoutant un 18ᵉ outil qui surveille les 17 autres.

- **A39 ◐ — Trois modules importants n'ont JAMAIS été audités ; `GemdosHd` l'a été.**
  Premier passage fait le 2026-08-28 sur `io/GemdosHd.cpp` (détail au `CHANGELOG.md`) :
  chaque opération sur le système de fichiers hôte a été remontée jusqu'à la
  provenance de son chemin (**toutes** passent par `createHostFileName` →
  `clampToSandbox`), les contrôles mémoire à taille variable ont été vérifiés contre
  le débordement, et le bac à sable a reçu **son premier test** (13 assertions,
  `--gemdos-selftest`, palier `fast`). Verdict : le module est **plus solide que sa
  réputation** — durci à plusieurs reprises, chaque durcissement portant le récit de
  l'évasion qu'il ferme. Ce qui manquait n'était pas la robustesse mais la GARDE.
  **Restent** :
  - ✅ `io/Ikbd.cpp` (1 189 l.) **audité et pincé le 2026-08-29** : sa table de
    longueurs de commande est comparée ligne à ligne à Hatari `KeyboardCommands[]`
    (39 opcodes), et le protocole a sa table de vérité — accumulation multi-octets,
    opcode inconnu = NOP, PAUSE OUTPUT levée par TOUTE commande valide, forme des
    paquets `$FD`/`$F7`. Une seule anomalie trouvée, d'étiquette : `$19` était
    commenté « SetJoystickFireDuration » (le nom que Hatari donne à `$18`) alors que
    c'est le mode keycode manette. **Restent** dans ce module : les modes souris
    absolus sous mise à l'échelle, l'horloge, et le code 6301 custom (`Execute`) ;
  - la pile réseau (`src/net`, ~1 030 lignes) — pas d'audit de lecture, mais elle
    est la MOINS démunie des trois : `--slirp-selftest`, `enec_selftest`,
    `netusbee_selftest` et un job de CI dédié la couvrent déjà par le comportement ;
  - dans `GemdosHd` : la table de handles et le suivi Pexec (c'est **A13**), les codes
    d'erreur face à `gemdos.c`, et le comportement de `Fsfirst`/DTA sous réutilisation
    d'index — non couverts par le test du bac à sable ;
  - **un test par COUCHE** du bac à sable. Le test posé garantit la PROPRIÉTÉ (rien ne
    sort) et pas les couches : mesuré par mutation, il faut retirer LES DEUX défenses
    pour le faire rougir. Il ne signalera donc pas la perte silencieuse d'une seule.
    Le corriger demande de tester des états intermédiaires, donc de coupler le test à
    l'implémentation : à décider, pas à bâcler.

- ⚠ **Et le GUI n'a jamais été exercé INTERACTIVEMENT dans une passe de validation.**
  Il est jugé sur sa forme (A9 : 4 814 lignes, `main()` 2 430, 82 globaux) et couvert
  par une capture au boot plus trois assertions d'arguments — jamais par un usage.
  C'est la même famille qu'**A12** (aucune cible de livraison validée à la main) et ça
  se traite avec : une passe d'usage réel, consignée.

### Garde-fous du plan (à NE PAS faire)

- **Croire que le plan se vide** : l'audit du 2026-08-27 a produit 22 items ; la journée
  du 2026-08-28 en a soldé 4 entièrement et avancé 5 — et en a ajouté 2 (A38, A39). La
  méthode **VOIT mieux qu'elle ne RÉPARE**, et c'est structurel : auditer coûte une
  journée, réparer coûte des semaines. Ce n'est pas un défaut tant qu'on le sait ;
  ça le devient si l'on planifie comme si la liste allait se refermer toute seule.
  Arbitrer explicitement entre « ouvrir » et « fermer » à chaque session.

- **Rouvrir BL5 sans concevoir une 3ᵉ mesure indépendante** : le paradoxe de signe entre
  les deux instrumentations existantes est documenté (`docs/HATARI_DIVERGENCES.md` § BL5,
  6 hypothèses réfutées) — re-mesurer avec les mêmes sondes ne tranchera rien.
- **Combiner A9 + A31 + A32 en un « grand refactor »** : chaque chantier structurel
  séparément, filet de test posé AVANT (le boot GUI l'est ; A29 pour le cœur).
- ~~Supprimer un des deux modèles d'exécution sans la mesure d'A34.~~ **La mesure a
  été prise le 2026-08-28 et le perdant est supprimé** (cf. `CHANGELOG.md`) : il n'y
  a plus qu'un modèle, le BLOC. Le garde-fou reste utile comme précédent — on ne
  supprime pas une branche d'exécution sur une intuition.

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

## 🔬 Divergences Hatari & précision cycle — restes

**Inventaire maître** (sévérité + impact + `fichier:ligne` des deux côtés) :
[`docs/HATARI_DIVERGENCES.md`](docs/HATARI_DIVERGENCES.md). **Aucune divergence de
sévérité haute n'est ouverte** (vérifié entrée par entrée le 2026-08-27) ; la
convergence instruction Moira↔WinUAE est complète et le beam-sync joueur est **clos**
(→ « ÉTAT COURANT » de `docs/MOIRA_WINUAE_CONVERGENCE.md`). Le restant, à rendement
décroissant, par priorité d'impact :

1. **[VIDÉO]** V3 géométrie mid-trame (50↔60 Hz) : le restart du compteur est porté
   (`VC_RESTART`), reste l'**attribution de ligne** — verrou dédié `NEOST_LINELEN_ATTR`
   (toujours OFF par défaut). **A16b est soldé le 2026-08-28** : le segfault qui bloquait
   le chantier est corrigé (invariant `glueLineStart_.size() == glueLines_.size()` rompu
   par `replayGlue`), et le palier `full` est vert **avec le verrou armé** — mais c'est
   une NON-RÉGRESSION, pas une preuve : **aucun étalon n'exerce la géométrie mi-trame
   50↔60 Hz que V3 vise**. Prochain pas réel : un étalon (généré ou oracle) qui bascule
   la fréquence EN COURS DE TRAME, sans quoi promouvoir le verrou serait un pari.
2. **[SON]** quantification HBL du refill FIFO à confronter à l'oracle sur un poll serré de
   `$FF8909/0B/0D` — validable par dump WAV + trace.
3. **[MFP]** `UpdateTimers` avant lecture IPR/ISR/TBDR en mode bloc — retard **mesuré à
   157 cycles** dans le pire cas observé. Le correctif évident (dispatch sync-driven) est
   **réfuté**, et A34 a SUPPRIMÉ ce modèle le 2026-08-28 : il ne reste plus rien à
   attendre de ce côté. Toute correction devra se faire DANS le modèle BLOC — par
   exemple en rafraîchissant les timers à la lecture du registre plutôt qu'à la
   frontière de bloc.
4. **[FPU]** arrondis de conversion sortante et précision FPCR (détail § Roadmap / FPU).
5. **[BLITTER]** résidu BL5 : ~10 cyc par démarrage de blit + ~3,3 par reprise de tranche,
   **paradoxe de signe non levé** — cf. Garde-fous (aucune correction sans 3ᵉ mesure).
6. **[VIDÉO, P3]** wakeup-state WS3 sous-pixel, mode 336 px STE (`bSteBorderFlag`), rendu
   live du retrait bas, interfoliage blitter → `docs/CYCLE_ACCURACY.md` §4.

**Faisables sans oracle** : FPU packed decimal bit-exact ; GEMDOS recomposition Unicode NFD→NFC
(cible macOS) — détaillés dans `docs/HATARI_DIVERGENCES.md`.

**Décisions actées (NE PAS « corriger » vers Hatari)** : SCC `WR14` bit4 loopback (datasheet
Zilog, NeoST plus fidèle) ; WRITE/READ TRACK STX réinterprétés (NeoST rend la piste lisible) ;
densité HD/ED STX (NeoST plus cohérent) ; RTC en temps émulé (déterminisme headless).

> **L'oracle se bâtit, il n'arrive pas tout seul** : `extern/hatari` est GITIGNORÉ et n'est
> PAS un sous-module — sur une machine fraîche il est ABSENT. `tools/setup_hatari.sh` clone au
> pin (`f0736b2`) et bâtit avec les options macOS obligatoires ; recettes →
> [`docs/HATARI_AUTOMATION.md`](docs/HATARI_AUTOMATION.md).

---

## Roadmap par sous-système — items ouverts

> Le reste (Bus/MMU, FDC, YM2149, GEMDOS, ACSI, SCC, FPU, imprimante, MegaSTE 8/16 MHz + cache…)
> est **fait et validé** — voir `CHANGELOG.md`. Ci-dessous, uniquement ce qui reste ouvert.

### Vidéo / Shifter
- Raffinements cycle-exact → § Divergences ci-dessus.
- **Résidu du latch couleur bordure gauche** : 16 px (cols 45-60) = la **position
  horizontale exacte** où l'écriture palette prend effet (Hatari bascule ~16 px après le
  début nominal de l'aire active = latence pipeline ; NeoST bascule pile à `activeX_`).
  Invisible aux étalons. _Valeur très basse._

### Interface — kiosk & effets CRT
- ⭘ **Souris ABSOLUE pour GEM/bureau** — la souris ST n'est pilotée qu'en mode
  capturé/relatif (`g_mouseCaptured`, `GLFW_RAW_MOUSE_MOTION`), pensé pour les jeux.
  🎯 Un mode absolu (position curseur hôte → curseur ST, sans capture) pour l'usage
  GEM/desktop/navigateur — confort, pas bug.
- ⭘ **Trace clavier permanente `NEOST_KBD_TRACE`** (comme `NEOST_ENEC_TRACE`) — éviterait
  un cycle rebuild/revert au prochain doute clavier. _Valeur faible, coût nul._
- Cosmétique : membres `srcW_`/`srcH_` morts dans `CrtEffectStack` ; répétition de
  navigation kiosk (tenir gauche/droite bloque la répétition haut/bas). Limitations CRT v1
  assumées (baril/vignette sur le buffer entier en kiosk ; GL 2.1 → passthrough).

### Stockage & contrôleurs
- **SCSI / NCR5380** — TT/Falcon **uniquement** (le MegaSTE n'en a pas). Hors périmètre,
  non commencé. Réf. `ncr5380.c`.
- SCC : restes faible valeur — timers du BRG / Zero Count, baudrate temporisé, série hôte.
- ⭘ **Test F (disquette) de la cartouche STE_Test v1.9 : « Cannot write drive A/B »,
  drives vus SS** (pré-existant au chantier MegaSTE ; le test F du diagnostic MegaSTE,
  lui, PASSE avec le même FDC émulé — la cartouche STE détecte les faces/l'écriture
  autrement). Trace façon FDC + Hatari en oracle sur la même cartouche. _Valeur moyenne._

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
(La pyramide, ses paliers et ses chiffres → `CLAUDE.md` et `DEV.md`.)
- gate `trace_diff --periods` vs oracle Hatari (le cycle-bench actuel est une auto-régression
  NeoST) ;
- self-tests P0 supplémentaires (autres Timers, ACIA) ;
- si une vraie démo spec512 **overscan** (bordures ouvertes) libre est rapatriée un jour →
  l'ajouter en étalon oracle (l'auto_diapo est 100 % borderless).

### Outillage / qualité
- **Étalons headless** : calibrer frames + références Cuddly / Union / Troed / Hatari Test
  Suite ; rapatrier Union (planetemu manuel). Infra en place (`tools/run_etalons.py`).
- **Comparaison MAME ↔ NeoST** (memory map, bus errors, FDC/MMU FIFO, blitter, SCC).
- **Matrice MegaSTE — restes** : combinaisons DD/HD × cache par balayage systématique si un
  jour un titre l'exige.
- Capturer des **traces Hatari de référence** pour `trace_diff` (Arkanoid & co).
- ⭘ **Hygiène FujiNet — décision de mainteneur** : le code est retiré (2026-08-22), restent
  deux mentions historiques (commentaire de version save-state dans `src/core/Machine.cpp`,
  entrées `CHANGELOG.md`). Reformuler ou assumer — un changelog garde normalement la trace
  de ce qu'il a supprimé.

### Station MIDI — le ST comme séquenceur d'un studio moderne (relevé 2026-08-29)

> Cas d'usage visé : un Mac porte les AU/VST et les claviers USB, le ST porte Cubase ou
> Notator et mène la séance. La **sortie** est déjà au niveau : gigue σ 0,4-1,7 ms et
> pente de tempo 1,001 mesurées sur 200 notes (`run_midi_sequencer.py`). Ce qui suit est
> ce qui manque pour y brancher un vrai studio plutôt qu'un seul appareil.

- ~~**Débit d'entrée — plafond à 4,5 % d'un câble MIDI.**~~ **LIVRÉ le 2026-08-29.**
  L'injection hôte→ACIA se faisait une fois par trame et le 6850 n'accepte que 2 octets :
  plafond 2 octets/trame, ~143 o/s en mono contre 3 125 o/s sur un vrai câble. C'est
  désormais l'ACIA qui TIRE, sur `Scheduler::MIDI_RX` (2 560 cycles/octet), comme
  `IKBD_RX` le fait pour l'ACIA clavier. **Mesuré en temps réel : 40,4 octets/trame,
  soit ~2 885 o/s — 92 % d'un câble, ×20.** Le débordement redevient celui du matériel
  (le 6850 perd l'octet neuf) au lieu d'être masqué par une rétention côté hôte.
- ~~**Un seul appareil de chaque côté.**~~ **LIVRÉ le 2026-08-29.** Entrée : boîtier de
  **fusion** (N sources → l'unique ACIA, entrelacement aux frontières de MESSAGES, un
  décodeur par source) avec **canalisation** par source, sans quoi deux claviers émettant
  tous deux sur le canal 1 seraient inséparables pour le séquenceur. Sortie : **aiguillage
  par canal** (chaque destination reçoit le masque de canaux qu'on lui donne ; les
  messages système vont à toutes). Reste ouvert ci-dessous : le multi-PORTS, qui est un
  autre sujet — l'aiguillage répartit 16 canaux, il n'en crée pas 32.
- **Latence de jeu : ~30-35 ms aller-retour** (arithmétique, non mesurée) — la part
  « entrée » est tombée avec le point ci-dessus (l'octet entre à son cycle, plus au
  début de la trame) ; restent surtout les 30 ms d'avance fixe en sortie
  (`MidiOutHost::kLeadMs`, le prix payé pour tuer la gigue). Un pianiste le sent. Rendre
  cette avance **réglable** : c'est un arbitrage gigue/latence qui appartient à
  l'utilisateur. Le premier point ci-dessus supprime la part « entrée ».
- **16 canaux — pas d'interface multi-ports.** (L'aiguillage livré répartit les 16 canaux
  du ST entre plusieurs appareils ; il n'en ajoute pas.) Le ST n'a qu'un MIDI OUT ; les **C-Lab
  Unitor** et **Steinberg Midex** en ajoutaient (32/64 canaux, SMPTE). Curiosité : la
  *clé* de l'Unitor-N est **déjà émulée** (c'est la même que celle de Notator), mais pas
  le boîtier. `CartDevice.hpp` sait déjà que MIDEX et Combiner empilaient des clés.
- **Notator n'a jamais tourné.** Il est cité comme séquenceur voulu au même titre que
  Cubase, mais aucun étalon ne l'exerce et sa clé garde **deux incertitudes** non
  tranchées faute de Notator SL original (cf. le point « Clé Notator » plus bas). Tant
  que le logiciel n'a pas démarré une fois, c'est une promesse, pas une capacité.
- **Profils d'appareil.** L'aiguillage se règle canal par canal ; pour un appareil connu
  (les deux synthés + les 4 drums d'un Circuit Tracks, par exemple) un profil nommé
  poserait le masque d'un clic. ⚠ Suppose un plan de canaux **vérifié** sur l'appareil,
  pas deviné — c'est un réglage de la machine, pas une constante universelle.
- **Canal forcé par source : pas de « splitter » clavier.** Un vrai boîtier de fusion sait
  aussi couper un clavier en zones (grave → canal 1, aigu → canal 2). Ici une source = un
  canal.
- **Pas de synchro SMPTE/MTC.** L'horloge MIDI traverse (ce ne sont que des octets), donc
  le ST peut mener un DAW. Le code de temps SMPTE de l'Unitor, non.
- **Rien sous Windows** : cf. « MIDI OUT Windows » ci-dessous (winmm à écrire).

### Réseau (extensions NeoST — base livrée 2026-08-12, cf. `docs/EXTENSIONS.md`)

> Les chantiers **clos** de ce front (Slirp 5/5, fenêtres EtherNEC ROM3/ROM4, CAB affiche
> theoldnet.com) → `CHANGELOG.md` (2026-08-27), recettes incluses.

- **MIDI OUT Windows** : `MidiOutHost` couvre CoreMIDI (macOS) et ALSA (Linux) ; winmm
  reste à écrire — le MT-32 (Munt), lui, est portable.
- **Périphériques des ports — validation** : `PortDevices` transcrit Steem/WinUAE sans
  logiciel à clé sous la main. À exercer : Leader Board / 10th Frame (dump ST), B.A.T. II,
  Music Master, et l'option « Pro Sound » de Wings of Death / Lethal Xcess (présents en
  STX) pour entendre le DAC. **Clé Notator** (`--dongle notator`) : à confronter à un
  Notator SL original — deux incertitudes à trancher sur le vrai matériel (front de /ROM4
  cadençant FEEDB1 ; ordre UDS↔/ROM4 à l'armement). Restent sans relevé public : Log 3
  (EP330), Pro-24 (GAL16V8), Avalon / Synthworks, Zodiac, DynaBlaster. L'outil pour
  trancher existe : capture matérielle `R3`/`R4`/`U` + `--key-replay`
  (recette → `docs/EXTENSIONS.md`).
- **Dongles — frontends WASM/Android** : `PortDevices`/`CartridgeKey` ne sont exposés que
  par le GUI et le headless ; le menu Android et la démo web n'ont pas de page Dongles.
- **Clé Steinberg — validation** : `CartridgeKey` (rouge/noire, équations MiSTery) n'a
  jamais vu un Cubase 3.10 / Score / 2.01 réel — il faut une disquette originale (non
  crackée). (La « destination CoreMIDI au lieu de la seule source virtuelle » qui figurait
  ici est **livrée** le 2026-08-29, dans les deux sens — cf. `docs/EXTENSIONS.md`.)
- **NetUSBee — périphériques USB hôte** : l'ISP1160 (`io/Isp1160`) est un hub racine
  VIDE ; brancher un clavier/souris HID puis un stockage de masse derrière
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
