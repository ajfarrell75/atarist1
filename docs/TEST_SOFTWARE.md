# Logiciels étalons — cas limites d'émulation

> (c) 2026 VERHILLE Arnaud — catalogue des jeux/démos/suites qui poussent un émulateur ST
> dans ses retranchements, classés par **sous-système** matériel.
>
> Complète la **méthode imposée** (`CLAUDE.md`) : on porte d'abord la source de vérité
> (`extern/hatari/src`), PUIS on valide avec du logiciel réel. Chaque cible ci-dessous
> exerce un effet non documenté (affichage détourné, timing au cycle près, astuce de
> synchro HW) qui ne sort que si l'émulation est fidèle. Les chantiers correspondants
> sont dans [`TODO.md`](../TODO.md) ; le rendu attendu se valide à l'oracle Hatari
> ([`HATARI_AUTOMATION.md`](HATARI_AUTOMATION.md)) et l'instant d'IRQ au cycle via
> [`CYCLE_ACCURACY.md`](CYCLE_ACCURACY.md).

⚠ **Redistribution** : seules les démos freeware (Cuddly Demos, Union Demo, suites de test
HW) sont rapatriables via `tools/fetch_disk.py` (domaine public / freeware). Les jeux
commerciaux (Dungeon Master, Xenon 2, Arkanoid, Enchanted Land, Turrican) ne le sont PAS —
tester avec ses propres images.

## ⚠ Configuration : PAL/NTSC et couverture de la suite

**C'est la ROM TOS qui fixe la fréquence de balayage au boot**, et cela change l'image
affichée par les démos :

| ROM | Fréquence |
|-----|-----------|
| suffixes **`uk` / `fr` / `de` / `es`** (`tos102uk`, `tos162uk`, `tos206fr`, `etos192fr`, `etos256fr`…) | **50 Hz — PAL** |
| suffixe **`us`** (`tos102us`, `tos162us`, `tos206us`, `etos192us`, `etos256us`) | **60 Hz — NTSC** |

Vérification en une commande — `./build/neost-headless roms/<rom>.img --frames 120`
affiche `[headless] vidéo : 416x276 @ NN Hz`.

**Conséquence mesurée (2026-08-01)** : les images **Spectrum 512** de
`spectrum_512_auto_diapo.st` sont calculées pour le 50 Hz. Sous une ROM NTSC, les
commutations de palette par ligne ne tombent plus au bon endroit : l'image sort
**déchirée/tramée** au lieu du dégradé 512 couleurs — **sur ST comme sur STE**, et
**à l'identique sous Hatari (0 px des deux côtés)**. C'est donc FIDÈLE, pas un bug ;
le remède est une ROM européenne. Repères d'aire non-noire pour la diapo « coucher de
soleil » : **0,555 = rendu correct (PAL)**, **0,475 = déchiré (NTSC)**.

**Couverture de la suite** (12 étalons « machine », hors auto-tests) : `st` ×8 et
`ste` ×4 ; `tos102uk` ×6 (50 Hz) + `tos162us` ×1 (60 Hz) — **ROM Atari propriétaires** —,
`etos192fr` ×2 (50 Hz) + `etos256us` ×3 (60 Hz) — **ROM libres** ; 512 Ko ×10
et 1 Mo ×2 ; `fastfdc` ×8. Donc **MegaST, MegaSTE, TOS 1.00/1.04/1.06/2.06 et EmuTOS
PAL ne sont couverts par AUCUN étalon**. Une suite verte ne prouve rien hors de ces
configurations : devant un rapport « ça plante dans le GUI », commencer par lire
`neost.cfg` et rejouer la scène avec `--from-cfg neost.cfg --disk <image>`
(+ `--shot-every N PRÉFIXE` pour voir *où* ça casse) avant de soupçonner une
régression. Vérifié le 2026-08-01 : No Cooper, Cuddly Demos, Enchanted Land et
Lethal Xcess « en panne » tournaient en `machine=megast` — tous corrects en `st`.

### ROM libres vs ROM propriétaires (depuis le 2026-08-19)

Un étalon dont la ROM est **absente** ne se comporte plus de la même façon selon la ROM
(`rom_is_free()`, `tools/run_etalons.py`) :

| ROM | Absente ⇒ | Pourquoi |
|-----|-----------|----------|
| `roms/etos*.img` (EmuTOS, libre) | **ÉCHEC** | elle est livrée avec le dépôt : son absence est une casse. |
| toute autre (`tos*.img`, Atari) | **SKIP recensé** | non redistribuable : le dépôt ne peut pas garantir sa présence. |

Le SKIP n'est jamais silencieux — la suite imprime un bloc
« ⚠ NON EXÉCUTÉS — ROM propriétaire absente (N) : … » et le code de sortie reste 0.
Sans les deux ROM Atari, il reste **7 auto-tests + 5 étalons machine** (ST ×2, STE ×3).

Les **4 étalons à disque généré** (`overscan_top`, `trace_odd`, `scroll_8264`,
`scroll_8265`) ont été migrés sur EmuTOS le 2026-08-19 : leur programme est un secteur de
boot autonome (il pose lui-même résolution, palette et base écran), donc le TOS ne fait que
le charger. Vérifié plutôt que supposé — capture EmuTOS vs capture TOS propriétaire = 0 px,
oracle Hatari EmuTOS vs oracle Hatari TOS = 0 px, références inchangées.

⚠ Les **3 étalons Spectrum 512 ne sont PAS migrables** : sous EmuTOS le disque
`spectrum_512_auto_diapo.st` ne prend pas la main (on retombe sur le bureau GEM). Ils
restent donc sur `tos102uk` / `tos162us`, et se sautent proprement sans ces ROM.

## Ordre de débogage affichage conseillé

Du plus simple au plus violent, chaque étape suppose la précédente acquise :

1. **Spectrum 512** — ✅ **RÉSOLU** (100 % pixel-identique à Hatari, sans flicker) : stabilité
   de la palette par ligne (synchro CPU↔faisceau de base).
2. **The Cuddly Demos** — suppression des 4 bordures (timings HSYNC/VSYNC).
3. **Enchanted Land** — sync-scroll horizontal (bascule 50/60 Hz au cycle exact).

---

## 1. Shifter / synchro CPU-vidéo

Le ST n'a ni scrolling matériel, ni split-screen, ni copper : tout passe par la
réécriture des registres Shifter (fréquence 50/60/71 Hz, résolution, palette) à des
cycles précis du 68000 PENDANT le balayage. → réf. NeoST : `Shifter`, `Machine::runFrame` ;
Hatari : `video.c`, `spec512.c`.

| Étalon | Éditeur | Mécanisme | Cas limite testé | TODO NeoST |
|--------|---------|-----------|------------------|------------|
| **Spectrum 512** | Inshape | Réécrit la palette plusieurs fois par ligne → >512 couleurs affichées | Synchro cycle-près `MOVE.W` ↔ position du faisceau. Défaut : couleurs décalées verticalement, bandes/flicker | ✅ **RÉSOLU** — diaporama étalon **0 px vs Hatari** (4 images), flicker éliminé. Reste : scroll fin mi-ligne |
| **Enchanted Land** | Thalion | Scrolling horizontal pixel-près SANS Blitter : bascule 50↔60 Hz en fin de ligne pour tromper le compteur d'adresse interne du Shifter et décaler l'adresse de base | 1 cycle d'erreur → écran qui saute/déchire ou plante. Exige la géométrie variable EN COURS de trame | « Suppression de bordures » (géométrie mi-trame) |
| **No Cooper** | 1984, 1989 | Écran **greetings** : page med-res 4 bordures ouvertes (chaque ligne : hi@0/lo@12/**MED@20** + 60/50@376/384 + stab@444/456) ; écran principal : nappe raster bord à bord + scrolltext 2 px/trame | Cas d'école `Video_WriteToGlueRes` — **✅ V2 VALIDÉ 0 px vs oracle (2026-07-08)** | Étalons `nocooper` (écran principal, ~6800) et `nocooper_greetings` (**référencé sur l'oracle**, 5 espaces datés, ~29500) — fetch fujiology auto |
| **The Cuddly Demos** | The Carebears (TCB), 1989 | 1ʳᵉ démo à ouvrir les **4 bordures** (haut/bas/gauche/droite) simultanément : boucles de NOP calibrées qui commutent la fréquence au moment où le canon atteint les limites de l'affichage standard. Le robot du menu est dessiné puis **effacé en course avec le faisceau** (un seul buffer) : seul un rendu échantillonné PAR LIGNE le voit (cf. `lineSnap_`, commit 08b58e1) | Précision des timings de génération HSYNC/VSYNC + tampons internes Shifter | « Suppression de bordures » (BORDERMASK_*) |

### Quirk connu — PAS un bug d'émulation

- **Spectrum 512 sous ROM NTSC** : images déchirées/tramées au lieu du dégradé 512
  couleurs, parce que la ROM `…us` démarre la machine en 60 Hz (cf. § *Configuration :
  PAL/NTSC* en tête de document). Identique sous Hatari (0 px), sur ST **et** sur STE.
  Lancer ces disques avec `tos102uk`/`tos102fr`/`tos162uk`…

- **Captain Blood** (ERE, crack 42-Crew anglais) : au chargement, le jeu scanne la ROM
  TOS à la recherche de la chaîne `AZER` (table clavier AZERTY, code en RAM `$1d69a`).
  TOS **français** détecté → affiche « KEYBOARD PROBLEM » (Cconws VT52) puis attend une
  touche et **reboote**. Comportement IDENTIQUE sous Hatari (oracle vérifié trame à
  trame) : ce n'est pas l'IKBD/ACIA. **Lancer ce crack avec un TOS US/UK** (`tos102uk`)
  → le jeu démarre (écran planète) et se joue normalement.

## 2. MFP 68901 / interruptions

Timers et IRQ poussés à fréquences extrêmes (rasters, musique, digidrums). → réf. NeoST :
`Mfp`, `Scheduler` ; Hatari : `mfp.c`, `cycInt.c`.

| Étalon | Mécanisme | Cas limite testé | TODO NeoST |
|--------|-----------|------------------|------------|
| **The Union Demo** (menu) | Timer B = IRQ par ligne de balayage (rasters/lignes de couleur) **+** Timer A = musique en fond | Priorité des IRQ, latence de prise en compte de l'exception 68000, **réentrance** (IRQ pendant IRQ). Défaut : blocage CPU (Line F / Bus Error) | Timer B/A faits ; reste latence IRQ au cycle (cf. `cycle-accuracy`) |

## 3. YM2149 PSG / digidrums (PCM)

Le YM2149 ne sait pas jouer d'échantillons : les jeux émulent une voix numérique en
modulant le **volume** des canaux à très haute fréquence, cadencé par le **Timer A** du
MFP (souvent >8 kHz). → réf. NeoST : `YM2149`, `Mfp::timerA_*` ; Hatari : `psg.c`, `sound.c`.

| Étalon | Mécanisme | Cas limite testé | TODO NeoST |
|--------|-----------|------------------|------------|
| **Xenon 2: Megablast**, **Turrican** (musiques J. Hippel / D. Whittaker) | Volume des canaux YM réécrit à chaque IRQ Timer A → voix/percussions PCM | Synchro MFP↔YM2149 parfaite. Défaut : aliasing sévère, craquements, dérive de la hauteur tonale | Timer A event-count fait ; reste « décodage son sur l'horloge d'émulation » (précision cycle) |

## 4. Contrôleur disquette WD1772 / protections

Les protections (souvent Rob Northen) exploitent des caractéristiques PHYSIQUES non
standards du flux magnétique, qu'une émulation WD1772 « haut niveau » (logique) ne
reproduit pas. → réf. NeoST : `Fdc` ; Hatari : `fdc.c`, `floppies/stx.c`.

| Étalon | Mécanisme | Cas limite testé | TODO NeoST |
|--------|-----------|------------------|------------|
| **Dungeon Master** | FTL | « **Fuzzy bits** » : flux affaibli volontairement → le WD1772 lit alternativement 0 ou 1 à chaque passage. + secteurs de tailles exotiques (8192 o) | Fidélité au flux physique (format `.STX`/`.IPF`) : timing de rotation exact + registres d'erreur du contrôleur. Une émulation logique échoue à lancer le jeu | « Support STX (Pasti) » + « Timing réel » (FDC cycle-exact) |

## 4 bis. Séquenceurs MIDI — Cubase Lite joue un SMF (2026-08-23)

Le séquenceur exerce d'un coup l'ACIA 6850 (`$FFFC04/06`, TDRE/TIE à 31 250 bauds),
le Timer A du MFP (horloge MROS), le GEMDOS HD (`Pexec`, `Fopen/Fread`) et le
convertisseur `tools/midi_simplify.py`. → réf. NeoST : `MidiAcia`, `Mfp`, `GemdosHd` ;
Hatari : `acia.c`, `midi.c`, `gemdos.c`.

| Étalon | Configuration | Ce qui est vérifié | Recette |
|--------|---------------|--------------------|---------|
| **Cubase Lite** (Steinberg 1996, MROS, sans clé) + `disks/midi/BLUES/ALBERTAM.MID` | **TOS 1.04 FR**, Mega ST, 1 Mo, mono, `--gemdos disks/midi` (EmuTOS : MROS panique, incompatibilité connue) | 200 notes : hauteur, ordre, **vélocité**, durée (±12 ms + 0,2 %), **pédale CC64**, **pente de tempo** (1,001, tolérance ±0,5 %), **gigue σ < 5 ms** (mesuré 0,4-1,7 ms) | `python3 tools/run_midi_sequencer.py` (palier `fast`, ≈3 s) ; `--song disks/midi/CHOPIN/RAINDROP.MID` pour une autre pièce ; `--keep` garde `midi.log` + la capture |

Mécanique : `DESKTOP.INF` auto-lance `CB_LITE.PRG` (ligne `#Z`, TOS 1.04) ; souris
relative `--mouse-at` vers *File → Import…* ; `--azerty` pour taper `SONG.MID` dans le
sélecteur (TOS FR = AZERTY, sinon le M se perd) ; `|` = Enter du pavé = Play ;
`--midi-dump` journalise chaque octet MIDI OUT daté du cycle 68000 ;
`tools/midi_compare.py` compare (ou convertit le journal en SMF : `--to-smf`).

Quirks **de Cubase Lite** mesurés ainsi (pas des bugs NeoST, cf. `disks/midi/README.md`) :
note-off émis un tick interne en avance (−1,6 ms à 150 bpm, −5 ms à 55 bpm) ;
doublure à l'unisson ou note répétée sans trou → note coupée à 2 ms (`--detach` du
convertisseur) ; **armure (méta 0x59) en cours de morceau → piste de notes jetée à
l'import** (le convertisseur ne garde que celle du tick 0).

## Suite headless NeoST (`tools/run_etalons.py`)

Infra de non-régression par captures PPM (déterministe, sans GUI) :

```sh
# 1. Rapatrier les disques freeware listés dans tools/etalons.json
python3 tools/fetch_etalons.py

# 2. Générer les captures de référence (1ʳᵉ fois ou après correctif validé)
python3 tools/run_etalons.py --update-ref

# 3. Régression (compare NeoST vs tests/reference/*.ppm)
python3 tools/run_etalons.py

# Sous-ensemble + oracle Hatari pour une nouvelle référence externe
python3 tools/run_etalons.py --only spectrum512_diapo --oracle
python3 tools/compare_screenshot.py tests/out/foo_neost.ppm tests/reference/foo.png --crop active
```

Étalons intégrés aujourd'hui : **glue_selftest**, **spec512_selftest**, **bus_selftest**,
**mfp_selftest**, **msa_selftest**, **enec_selftest** (P0, logique pure),
**EmuTOS STE boot**, **spectrum512_diapo** + **spectrum512_diapo2** (ST) +
**spectrum512_diapo_ste** (STE, scramble FIDÈLE == oracle Hatari STE), **overscan_top**,
**trace_odd**, **scroll_8264** / **scroll_8265** (scroll fin STE), **nocooper** et
**nocooper_greetings** (V2, réf. oracle archivée) ; fetch auto : **Cuddly Demos**
(`disks/etalons/cuddly_demos.msa`), **No Cooper** (`disks/etalons/nocooper.msa`),
**union_demo** (`optional` : SKIP tant que la disquette n'est pas rapatriée).
Soit **21 entrées** dans `tools/etalons.json` — 9 auto-tests + **12 étalons machine**, dont
le `_comment` du fichier rappelle la couverture réelle (ni MegaST, ni MegaSTE, ni TOS
1.00/1.04/1.06/2.06, ni EmuTOS PAL).

### Auto-tests logique pure (P0 — ms, sans boot ni oracle)

```sh
./build/neost-selftest                                        # logique PURE : chemins hôte, neost.cfg
./build/neost-headless roms/etos256us.img --glue-selftest      # machine Glue (bordures)
./build/neost-headless roms/etos256us.img --spec512-selftest   # re-rendu Spectrum 512 (borderless + bordé)
./build/neost-headless roms/etos256us.img --bus-selftest       # whitelist bus error (par octet)
./build/neost-headless roms/etos256us.img --mfp-selftest       # GPIP forcé / fronts AER-DDR / Timer B
./build/neost-headless roms/etos256us.img --msa-selftest       # ré-encodage .msa (aller-retour)
./build/neost-headless roms/etos256us.img --enec-selftest      # NE2000/EtherNEC (bouclage)
./build/neost-headless roms/etos256us.img --usatan-selftest    # UltraSatan (fil ACSI : INQUIRY, paquets 'US', RTC)
./build/neost-headless roms/etos256us.img --netusbee-selftest  # NetUSBee (ISP1160 + NE2000 en coexistence)
python3 tools/run_cyclebench.py [--update]                    # golden du modèle de cycle 68000
```

`--spec512-selftest` construit une RAM vidéo synthétique (tous pixels = index 1), injecte des
écritures palette datées et vérifie **octet-exact** la couleur de chaque pixel contre le modèle
`f(kSpec512AlignCyc, géométrie)`. Toute dérive d'alignement palette↔pixel (la cause du « scramble
spec512 ») décale les frontières → exit 1. La constante elle-même est ÉPINGLÉE par la première
vérification du test (`chk("kSpec512AlignCyc", …, -25)`) : la changer suffit à faire tomber le
test — c'est le contrôle négatif. (L'ancien `NEOST_ALIGN_OFF` n'existe plus : le poser ne fait
plus rien, le test reste vert.) Intégré à la suite via `run_etalons.py` (type `spec512_selftest`).

### Auto-tests à verdict série (P1 — s, déterministe, sans oracle)

Convention : une ROM écrit sur le port série RS-232 (UDR `$FFFA2F`, capturé par
`--serial-dump FILE`) une ligne par test :

```
NEOST-TEST: <nom> PASS
NEOST-TEST: <nom> FAIL <détail>
```

```sh
python3 tools/make_selftest_cart.py disks/etalons/selftest_cart.bin   # cartouche diagnostic $FA52235F
python3 tools/run_selftests.py                                        # génère si absent, lance, scanne, exit 0/1
python3 tools/run_selftests.py --list
```

`tools/make_selftest_cart.py` produit une **cartouche diagnostic** (magic `$FA52235F` → le TOS saute
à `$FA0004` au reset, pré-TOS, sans disque) qui teste :
- **`cpu`** — invariants arithmétiques (garde-fou cœur 68000) ;
- **`timing`** — sentinelle liveness : le compteur vidéo `$FF8209` n'est pas figé (anti-clock-morte) ;
- **`frame`** — cycle-exact : installe les vecteurs HBL/VBL (`$68`/`$70`) et **compte les HBL par trame**
  par interruptions (262 pré-TOS, déterministe) → flague une dérive grossière (50 Hz→313, 71 Hz→501) ;
- **`ipl`** — latence d'exception : le handler HBL de la ligne 100 fait un délai puis lit `$FF8209`
  (position faisceau = phase d'entrée IACK+prologue, 224±4 déterministe).

`--break cpu|timing|frame` force le FAIL correspondant (valide que le runner l'attrape). Le verdict
**`fpu`** vient de `make_fpu_testrom.py` (Mega STE + `--fpu`, 9 tests MC68881), qui émet désormais
`NEOST-TEST: fpu PASS|FAIL` sur le série (en plus de `D7` pour la trace). Le runner
`tools/run_selftests.py` (manifeste `tools/selftests.json`) lance le headless avec `--cart`/rom +
`--serial-dump`, scanne les verdicts et sort 0/1.

- **`usatan_netusbee`** (2026-08-21) — carte SD `disks/etalons/usatan_sd.img` (générée par
  `tools/make_usatan_hd.py` : 16 Mo, partition GEM FAT16 portant `AUTO\USTEST.PRG`), EmuTOS
  192 Ko, `--ultrasatan --sd1 … --netusbee`. EmuTOS amorce sur la carte, monte C: et lance le
  PRG (`tools/make_usatan_test.py`, relogeable, `Super()`), qui parle à l'UltraSatan **comme
  `US_CONF.TOS`** (séquence LongRW, attente IRQ GPIP5) et au NetUSBee **comme le pilote FreeMiNT**
  (lectures MOT, primitives raw). ⚠ Règles EmuTOS apprises ici : disque dur présent ⇒ disquette
  non amorcée ; secteur racine exécuté seulement sans partition reconnue ; FAT12/16 par le
  nombre de clusters (> 4084 ⇒ FAT16), d'où 16 Mo. Variante disquette (`A:\AUTO`, même PRG) :
  `tools/make_usatan_test.py OUT.st`, à utiliser SANS carte SD.
  **Contrôle négatif Hatari** (vérifié 2026-08-21) : la même carte sous l'oracle —
  `hatari --machine st --tos roms/etos192us.img --acsi disks/etalons/usatan_sd.img --rs232-in
  /dev/null --rs232-out OUT --run-vbls 900` — monte C: et lance le PRG (`uscdrv PASS`) mais rend
  **FAIL** sur les six verdicts matériels : Hatari n'a ni UltraSatan ni NetUSBee. Le test
  discrimine donc bien l'émulation des cartes, il ne passe pas « par construction ».

### Orchestration par paliers + hook pre-push

```sh
python3 tools/run_all.py --tier fast      # P0 (logique pure + 7 auto-tests) + P1 (verdicts série)
                                          # + cycle-bench + round-trip save-state + disquette
                                          # livrée — ~5 s (mesuré 2026-08-27)
python3 tools/run_all.py --tier full      # fast + P2 (étalons pixel + --verify-refs)
python3 tools/run_all.py --install-hook    # hook git pre-push (opt-in) → lance --tier fast
python3 tools/run_all.py --uninstall-hook
```

### Étalons qui BOOTENT un disque : `oracle_scan` (2026-08-19)

Hatari sème son RNG sur `time(NULL)` et s'en sert pour la position angulaire initiale de
la disquette : **la numérotation des trames de son AVI change d'un run à l'autre**. Un
`frame:` figé ne peut donc pas servir de référence oracle pour un étalon qui boote un
disque. Les 7 entrées concernées portent `oracle_scan: N` : `--oracle` extrait la fenêtre
`[frame−N, frame+N]` et retient la trame **identique** à la capture NeoST (jamais la moins
pire ; aucune correspondance ⇒ échec bruyant = vraie divergence). Détail et mesures →
`docs/HATARI_AUTOMATION.md`.

### Provenance des références & diff par ligne (P2)

Chaque étalon pixel déclare `ref_kind` dans `etalons.json` :
- **`oracle`** → comparé à l'**oracle Hatari** `tests/reference/<id>.png` (jamais une self-capture).
- **`snapshot`** → comparé à la self-capture NeoST `<id>.ppm` (non-régression ; repli `.png`).

```sh
python3 tools/run_etalons.py --verify-refs      # contrôle la provenance (oracle = .png ≥832px)
python3 tools/compare_screenshot.py A.ppm B.png --crop active --report   # diff PAR SCANLINE
```

`--report` affiche le 1ᵉʳ pixel divergent (x/y) et les pires scanlines — un décalage vertical spec512
apparaît en bande contiguë, un décalage horizontal en petit compte réparti sur beaucoup de lignes.
`run_etalons.py` passe `--report` automatiquement (diagnostic en cas d'échec).

### Pont config GUI↔headless (P3)

Les bugs « seulement en GUI » viennent d'une config ≠ headless. `--from-cfg neost.cfg` rejoue la config
EXACTE du GUI (machine/TOS/mem/cpu/disque/cart/mono/fastfdc/fpu/gemdos/acsi) ; les chemins `./../` du
GUI sont résolus vers la racine. Les options CLI placées **après** surchargent :

```sh
# Reproduire headless ce que le GUI a lancé, capturer, diff Hatari
./build/neost-headless --from-cfg neost.cfg --frames 1651 --screenshot s.ppm
python3 tools/compare_screenshot.py s.ppm tests/reference/<oracle>.png --crop active --report
```

---

## 5. Suites de test automatisées

Micro-tests formalisés par la communauté (Hatari, Steem), à exécuter au headless plutôt
que jeu par jeu.

| Suite | Cible de validation | Réf. |
|-------|---------------------|------|
| **Hatari Test Suite** | Micro-tests 68000 (instructions/exceptions non documentées) + Shifter | dépôt Hatari (`tests/`) |
| **ST-STE Hardware Test** (Troed / Sync) | Timings fins du Shifter, détection des modes de rémanence, variations de cycles mémoire (accès RAM asynchrones CPU/vidéo) | scène Atari |
| **Anatool / Discovery Cartridge** | Lignes de statut WD1772 lors du formatage / lecture de pistes corrompues | utilitaire bas niveau |
