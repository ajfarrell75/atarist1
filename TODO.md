# TODO — NeoST

(c) 2026 VERHILLE Arnaud. **Ce qui reste à faire.** Le fait (implémenté + validé) est dans
`[CHANGELOG.md](CHANGELOG.md)` ; les diagnostics de bugs en cours sont en mémoire projet.

**Sources de vérité à croiser systématiquement :**

- **Hatari** (`extern/hatari/src/*.c`) — comportement ST/STE/MegaSTE éprouvé. La référence.
- **MAME** (`src/mame/atari/atarist.cpp`, `stmmu.cpp`, `stvideo.cpp`, devices `mc68901`,
`wd_fdc`, `6850acia`, `z80scc`, `rp5c15`, `ay8910`, `lmc1992`) — composants séparés.

**Objectif** : émuler proprement un **MegaSTE** (68000 8/16 MHz, 1/2/4 Mo, TOS 2.05/2.06,
STE video/sound/joypads, blitter, RTC, SCC, SCU, ACSI/SCSI, DD/HD) avec un timing assez
fidèle pour jeux, démos et utilitaires.

**Légende** : `lot suivant` = portable, faible risque · `précision cycle` = ordonnanceur daté
(`[docs/CYCLE_ACCURACY.md](docs/CYCLE_ACCURACY.md)`) · `risque élevé` = bus/IRQ éprouvé ·
`gros contrôleur` = puce entière · `faible valeur`.

**Validation** : catalogue logiciels étalon → `[docs/TEST_SOFTWARE.md](docs/TEST_SOFTWARE.md)`.
Ordre affichage : **Spectrum 512 ✅ → Enchanted Land → Cuddly Demos** (scrolling robot + scroller bordure basse).

---

## Catalogue logiciels — bugs en cours

Rapports terrain (2026-06). TOS 1.02fr sauf mention contraire. Chemins sous `disks/st/` (`.st`)
ou `disks/stx/` (`.stx`).

- **Arkanoid (1987)** (`Arkanoid (1987)(Imagine).st`) — plante sur la page de titre
  ```
  (« ARkanoid ») sans jamais arriver au jeu, même avec TOS 1.02fr. Cf. aussi §Précision cycle
  / FDC (gel titre → partie). Oracle : `--keys`/`--joy`, trace IRQ, diff Hatari.
  ```
- **Captain Blood (1988)** (`Captain Blood (1988)(ERE)(ST)[cr 42-Crew][one disk].st`) —
  ```
  arrive au jeu puis plante sur une erreur keyboard et redémarre.
  ```
- **Enchanted Land (1990)** (`Enchanted Land (1990)(Thalion).st`) — logo + gouttes Thalion
  ```
  OK ; son Talion absent (press bouton joystick) ; scrolling saute terriblement (Le personnage qui devrait rester au centre saute 'un endroit à l'autre de l'image. Un probleme de synchro de beam) en jeu (symptôme proche du bug Cuddly / sync-scroll). Cf. §Bordures.
  ```
- **Super Hang-On (1988)** (`Super Hang-On (1988)(Sega).st`) — démarre ; musique abîmée
  ```
  par un bruit blanc de fond anormal ; lignes colorées horribles sur les 3/4 bas de l'écran.
  À corriger (son DMA/PSG ? géométrie vidéo ?). Cf. CHANGELOG (retry secteurs FDC).
  ```
- **Shadow Warriors (2Hot2Handle)** (`ShadowWarriors[2Hot2Handle]-D1/2/3.stx`) — après
  ```
  SPACE : page de titre + musique OK ; appuyer sur un bouton joystick ne lance pas le jeu.
  (Castle Warrior fonctionne parfaitement.)
  ```
- **Rick Dangerous II (1989)** (`Rick Dangerous II (1989)(Core Design)[cr Empire][t +2][a].st`) —
  ```
  SPACE, puis `n`, encore `n` : plante avec 4 bombes.
  ```
- **Stardust (1994)** (`Stardust (1994)(Daze Marketing Ltd.)(Disk 1 of 3)[cr Hardcore][t].st`) —
  ```
  plante sur écran noir.
  ```
- **Lethal Xcess** (`Lethal_Xcess_Disk_1.STX`) — ~~écran noir~~ **DÉMARRE (2026-06-14)** :
  fix = wait-state +2 (valeur d'abord) sur la lecture `$FF8209`, validé oracle sur LX **et**
  Enchanted Land (cf. CHANGELOG §Vidéo). *Reste* le beam-sync en jeu (image qui saute, voir
  ci-dessous — commun à EL/Cuddly).
- **Stardust Bloodhouse** (`stardust_bloodhouse_a/b/c.STX`) — plante au démarrage
  ```
  (écran noir).
  ```
- **Wings of Death** (`Wings_Of_Death_Disk_1/2.stx`) — après bouton : page de titre
  ```
  avec forte corruption graphique ; chargement avec son ralenti/bizarre ; SPACE lance le jeu
  qui fonctionne très bien ensuite.
  ```
- **The Cuddly Demos** (`disks/etalons/cuddly_demos.msa`) — première page OK mais son de
  ```
  mauvaise qualité ; après une touche, menu de sélection (robot) : scrolling complètement
  bugué qui saute. Cf. §Bordures (items 5-6).
  ```

---

## 🎯 Précision cycle

> Plan : `[docs/CYCLE_ACCURACY.md](docs/CYCLE_ACCURACY.md)` · Inventaire :
> `[docs/CYCLE_EXACT_INVENTORY.md](docs/CYCLE_EXACT_INVENTORY.md)`.
>
> Phases 1-6, latch palette Spec512, alignement bus shifter + wait states PSG/MFP/ACIA,
> machine Glue live, VDE_On live, Spec512 pixel-perfect, bordures haut/bas/gauche/droite :
> **FAIT** (cf. CHANGELOG).

- **Contention DMA vidéo sur la RAM** *(précision cycle, reporté)* — modèle MAME
  ```
  (`stmmu.cpp::bus_contention`), **non porté depuis Hatari** (qui ne le modélise pas) ;
  divergerait de l'oracle pixel. À ne traiter que si besoin matériel réel hors Hatari.
  ```
- **Arkanoid** — page de titre « ARkanoid » atteinte (FDC rotationnel, cf. CHANGELOG),
  ```
  **mais plante / ne franchit jamais la partie** (même TOS 1.02fr — protection ? second
  chargement ? IRQ ?). Détail terrain → §Catalogue logiciels. À diff'er contre Hatari
  (`--keys`/`--joy`, trace IRQ). 🎯 étalon suite FDC/protection.
  ```

## Bus / memory map / MMU

- ~~Zone void `[fin RAM, $400000)` : lire le dernier mot du bus~~ → **FAIT**
(`Bus::cpuDb` latché par les overrides Moira, cf. `CHANGELOG.md`).

## Vidéo / Shifter

- **Bordures — raffinements** *(précision cycle, faible priorité)* :
  ```
  (1) wakeup-state WS3 (+1 cyc, sous-pixel) ; (2) med-res overscan ; (3) blank lines /
  NO_SYNC ; (4) pixel-perfect L/D end-to-end ; (5) **BEAM-SYNC : l'image SAUTE trame à
  trame** — bug COMMUN (rapport utilisateur 2026-06) à **Cuddly Demos** (scrolling robot),
  **Enchanted Land** (en jeu) et **Lethal Xcess** (en jeu, après le fix wait-state) : les
  lignes du beam ne sont pas synchronisées → décalage erratique trame après trame. Cœur =
  rendu cycle-exact de la géométrie PER-LIGNE sous écritures sync-raster (`$820a/$8260`/
  palette datées au cycle). Le wait-state `$FF8209` (cf. §FDC) corrige le FEEDBACK compteur
  aux jeux mais PAS le rendu lui-même. 🎯 reproduire les 3 ; (6) **scroller bordure BASSE**
  du menu Cuddly non rendu.
  🎯 étalons : `make_overscan_test.py` / `make_overscan_lr.py` (✅), **The Cuddly Demos**.
  **ROOT-CAUSE (6) TROUVÉE (2026-06-14, oracle cmd-fifo) — mésattribution de ligne :**
  Oracle du menu DÉSORMAIS POSSIBLE (le build local a `--cmd-fifo` → `hatari-event
  keypress 57`, cf. `docs/HATARI_AUTOMATION.md` ; l'ancienne note « impossible » est
  PÉRIMÉE). Diff : Hatari ouvre la bordure basse (`nEndHBL=310`, gros scroller « OUR
  DAY! ») ; NeoST la garde fermée (`end=263`, scroller écrasé) la PLUPART des trames
  (intermittent → l'image saute). Cause : le menu écrit à la ligne 262 `60Hz@cyc~440`
  PUIS un `50Hz`. Hatari date ce 50Hz à la **ligne 263 cyc 16** (ligne SUIVANTE) → la
  décision bordure-basse de la ligne 262 reste « retirée ». NeoST le date à la **ligne
  262 cyc 492** (≤502) → `updateGlueState` RE-FERME (l.819, comme Hatari `Video_EndHBL`
  2973, mais Hatari ne voit pas ce write sur la ligne 262). MÉSATTRIBUTION car NeoST date
  avec `cyclesPerLine=512` FIXE alors qu'une **ligne 60Hz fait 508 cyc** → un write près
  de la frontière bascule de ligne. → exige la **longueur de ligne 50/60Hz VARIABLE** dans
  la datation (chantier §Précision-cycle « géométrie par ligne »). (5) le tearing du mur =
  même cause (datation per-ligne). Outils : `NEOST_SYNC_TRACE=1` (NeoST), `--trace
  video_sync` (Hatari). Réf. : menu robot Hatari /tmp/cudh_3150.png, NeoST /tmp/cud_02900.png.
  (7) **Lethal Xcess (STX) — écran noir** : la calibration fullscreen (`$14ef6` poll
  `$FF8209`, exige avance `0xbe`=190) deadlocke à cause du TIMING de la lecture compteur
  (pas la géométrie ; l'ancienne analyse 144-vs-190 / `syncWrites_` vide était une fausse
  piste invalidée par l'oracle). Fix candidat (`syncCpuBus` align) fait converger LX mais
  RÉGRESSE le sync-scroll d'Enchanted Land (même famille, ce point !) → opt-in
  `NEOST_VC_SYNC`. La VRAIE solution doit satisfaire LX **et** EL. Détail → §FDC « écran noir ».
  ```

## FDC WD1772 + DMA disquette

- ~~Lecteur HD MegaSTE~~ → **FAIT** : densité DD/HD/ED auto (géométrie), débit MFM
  ```
  ÷ facteur, porte `$FF860E` Mega STE, images 1,44 Mo (cf. `CHANGELOG.md`).
  ```
- ~~WRITE TRACK (format) sur `.ST`~~ → **FAIT** : extraction des secteurs si géométrie
  ```
  standard, sinon `LOST_DATA` tout-ou-rien (limite Hatari). Reformatage non
  standard = images flux (STX/SCP), hors périmètre `.ST`.
  ```
- ~~FIFO DMA/MMU vs MAME `stmmu.cpp`~~ → **TRANCHÉ** (recherche MAME master) : le modèle
  ```
  NeoST (= Hatari) est fidèle ; les écarts MAME (double FIFO 2×16 o, bit2 DRQ live,
  dernier bloc 8 mots) sont des choix que Hatari assume sans impact observable sur ST.
  Seule différence de fond : le chemin ACSI court-circuite le FIFO et `dmaSectorCount_`
  (transfert bloc piloté par le CDB) — à ne corriger QUE si un diagnostic qui
  désaligne sector-count DMA et longueur CDB échoue un jour.
  ```
- ~~STX HD/densité (`nextSectorIDStx`/`MFM_BIT` en cellules DD)~~ → **FAIT** : conversion
  bit/octet→cycles à la densité du média (DD inchangé). Cf. `CHANGELOG.md`.
- ~~Ré-interprétation en LECTURE d'une piste réécrite par WRITE TRACK~~ → **FAIT**
  (au-delà de Hatari) : `StxImage::reinterpretSaveTrack` parse le flux en secteurs lus à
  la place de l'original ; round-trip `.wd1772`. Cf. `CHANGELOG.md` + `tests/stx_writetrack_test.cpp`.
- ~~`Rick Dangerous.stx` « plante après titre »~~ → **rapport périmé, FONCTIONNE**
  (titre + jeu ; le test headless n'injectait pas d'entrée). Cf. `CHANGELOG.md`.
- **« écran noir » Lethal Xcess / Stardust / onslaught — DIAGNOSTIQUÉ : PAS un bug STX**,
  ```
  mais le MÊME chantier sync-raster que §Bordures (Enchanted Land / Cuddly). Le loader STX
  finit sa 1ʳᵉ salve (Lethal Xcess : pistes 0-35 face 0, TOUTES standard, dernier secteur
  lu+INTRQ OK) ; le jeu installe alors un afficheur fullscreen piloté par un état VBL
  (pointeur `$604`, phases `$139e8`/`$1499a`/`$149d4` qui incrémentent `$13a16`,
  vs `$149dc` qui ne l'incrémente PAS et joue un script de splits `$FF820A`/`$FF8260`
  synchronisé en pollant `$FF8209`). Boucle de calage `$14940` (attend que `$14ce8`,
  maj par le handler Timer B event-count `$14cc4`, reste STABLE 20 trames) → puis
  `$604=$149dc` + `bsr $13a18` (`clr $13a16` / `tst` / `beq`) : avec `$604=$149dc` rien
  n'incrémente `$13a16` → **deadlock**. Le calage diverge de la machine réelle car
  notre compteur vidéo `$FF8209` ne se comporte pas au cycle près pendant le poll
  (cf. `Video_RestartVideoCounter` NON porté + géométrie verrouillée par trame,
  `Shifter::videoCounter`). → à reprendre avec le chantier « géométrie par ligne /
  bascule 50-60 Hz + compteur vidéo cycle-exact » (§Bordures, §Précision cycle), PAS ici.
  ⚠ ORACLE DISPO (2026-06-14) : Hatari tourne en headless **sous Linux** aussi (binaire
  `extern/hatari/build/src/hatari`, cf. `docs/HATARI_AUTOMATION.md`). Référence visuelle
  obtenue (écran-titre OK sous Hatari STE/TOS 1.62 vs noir sous NeoST). Vérité-terrain
  cycle-exact du poll dispo via `--trace video_addr` (Hatari) et `NEOST_VC_TRACE=1` (NeoST,
  même format : `base/addr/line/X/start/cpl/liveStart/sync/pc` à chaque lecture
  $FF8205/07/09).
  ```
  **✅ RÉSOLU (2026-06-14, diff oracle) — wait-state +2 (valeur d'abord) sur la lecture `$FF8209` :**
  *(fix dans `Shifter::read8`, validé sur LX ET Enchanted Land + étalons `--max 0`. Reste
  le beam-sync en jeu, cf. §Bordures item (5).)*
  ```
  Diff Hatari↔NeoST de la calibration fullscreen (TOS 1.62us, Disk 1). Le code jeu :
    $14ef6: move.b $8209,d0 / beq $14ef6   ; ATTEND octet bas != 0
    $14f0e: move.b $8209,$14715            ; SAVE START (octet bas)
            ... script splits $820a/$8260 ...
    $14a26: move.b $8209,$14714            ; SAVE END
    $14a32: add.b #$be,d0 / cmp / bne      ; exige (END-START)&0xff == 0xbe=190
  Calibration : le jeu décale son script de 2 octets/trame et attend que l'avance mesurée
  monte LINÉAIREMENT jusqu'à 190 pile. HATARI : START toujours @(ligne63,X=284), rampe
  PROPRE 110,112,…,190 → converge → passe à la boucle de jeu $30142. NeoST : START JITTER
  @X=282/284 ; la sortie de boucle $14ef6 alterne (X=62,low=2) / (X=72,low=8) — un JITTER
  DE PHASE CPU↔faisceau d'~10 cyc au début de trame → deltas erratiques (jamais 190) →
  spin infini dans $14ef6 → JAMAIS $30142 → écran noir. La géométrie est CORRECTE
  (`cpl=512,start=56,liveStart=63` stables, = Hatari) : le repli `liveStartHBL=63` /
  `syncWrites_` vide N'EST PAS la cause (hypothèse précédente INVALIDÉE par l'oracle).
  CAUSE FINALE : la boucle sort à `E mod T` où T = durée d'itération `move.b $8209,d0/beq`.
  T était 2 cyc TROP COURT car la lecture `$FF8209` n'avait pas son WAIT-STATE. Mesuré à
  l'oracle (consécutifs `fc` au même PC) : LX `$14ef6` T=24 cyc chez Hatari vs 22 NeoST ;
  EL `$ee78` T=20 vs 18 → **+2 cyc bus FIXE** sur les DEUX. FIX (retenu) : échantillonner
  la VALEUR au cycle d'accès PUIS `addBusWaitCycles(2)` (ordre crucial : valeur intacte →
  étalons `--max 0` OK ; CPU retardé → T=24/20 = Hatari). ⚠ Un align-4 (`syncCpuBus`) au
  lieu d'un +2 FIXE jitterait et casserait EL — c'est pourquoi le 1ᵉʳ candidat (align)
  régressait EL. Résultat : LX converge ($14ef6 ~94k iters, delta 190, $30142, titre) ET
  EL atteint son jeu, AUCUNE régression étalon. Cf. CHANGELOG §Vidéo. Outil : `NEOST_VC_TRACE=1`.
  ```

## YM2149 PSG

- Données port B Centronics + front strobe (bit5) non émulés en sortie *(faible valeur)* —
  ```
  réf. `psg.c:PSG_Set_DataRegister`
  ```
- Filtre passe-bas STF alternatif (`LowPassFilter`) + table 16³ interpolée
  ```
  (`interpolate_volumetable`) en option _(faible valeur)_.
  ```
- Read-latch `regReadData_`, `$FF8801/03` → 0xFF *(faible valeur, risque word-read)*.

## Son DMA STE + Microwire/LMC1992

- FIFO 8 octets du DMA son remplie sur HBL (`DmaSnd_FIFO_Refill/PullByte`,
  ```
  dmaSnd.c:343-410) _(refinement résiduel : timing ±8 octets des débuts/fins de
  trame et drain post-PLAY ; le gros de la Phase C est fait)_.
  ```

## CPU : IRQ, Moira, MegaSTE

- ~~MC68881 — arithmétique flottante~~ → **FAIT** (cf. `CHANGELOG.md`,
`src/io/Fpu.{hpp,cpp}`) : FP0-7 étendu 80 bits, formats B/W/L/S/D/X/P, dialogue
Command/Response/Operand/Condition/Save/Restore complet, FMOVECR bit-exact ;
validé mini-ROM SFP004 (`tools/make_fpu_testrom.py`, 7/7) + diag MegaSTE
« FPU idle ». *Reste (faible priorité) : mantisse 64 bits réelle (softfloat —
les calculs passent par le double hôte, 53 bits) ; IRQ d'exception FP non
câblée (le socket se scrute, la glue SFP004 n'en a pas besoin).*

## Stockage & contrôleurs

- **GEMDOS HD** : monter un dossier hôte comme lecteur C: (`--gemdos DIR` /
`NEOST_GEMDOS_DIR`) — port complet de `gemdos.c` (cf. CHANGELOG).
- **ACSI complet** (jusqu'à 8 cibles, boot disque dur TOS depuis une image, R/W,
détection de partitions) — port de `hdc.c` (`io/Acsi`, `--acsi`/`--hd`, cf. CHANGELOG).
- **SCC Z85C30 MegaSTE** : canaux A/B, registres WR/RR, IRQ niv5 vectorisée (SCU),
  reset, TX→RX bouclage — port fonctionnel de `scc.c` (`io/Scc`, cf. CHANGELOG).
  _Reste (faible valeur) : timers du BRG (Zero Count), baudrate temporisé, série hôte._
- **SCSI / NCR5380** (MegaSTE/TT) *(gros contrôleur)* — réf. `ncr5380.c`
- **Imprimante/Centronics** : port B YM, strobe PSG port A bit5, busy MFP I0 — réf.
  ```
  `printer.c`
  ```

## Périphériques & profils machine

- **ROM TOS MegaSTE** : TOS 2.05/2.06 256 Ko à `$E00000`, choix pays, checksums, fallback
  ```
  EmuTOS MegaSTE.
  ```
- **NVRAM / préférences TOS MegaSTE** (résolution/boot device) si TOS 2.x l'exige.
- **Cartridge port** `$FA0000-$FBFFFF` générique — réf. `cart.c`

## Outillage / qualité

- **Étalons headless** — infra en place (cf. CHANGELOG) ; reste : calibrer frames +
  ```
  références Cuddly/Union/Troed/Hatari Test Suite ; rapatrier Union (planetemu manuel).
  ```
- **Comparaison MAME ↔ NeoST** (memory map, bus errors, FDC/MMU FIFO, blitter, SCC).
- Capturer la **trace Hatari de référence** pour `trace_diff` (Arkanoid & co).
- **Matrice de compatibilité MegaSTE** : TOS 2.05/06, EmuTOS, 1/2/4 Mo, 8/16 MHz, cache
  ```
  on/off, DD/HD, mono/couleur.
  ```
- Tests de non-régression (screenshots de référence EmuTOS/TOS 1.02).

