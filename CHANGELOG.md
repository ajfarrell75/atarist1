# Changelog — NeoST

(c) 2026 VERHILLE Arnaud. **La chronologie** : releases, puis les chantiers datés dans
l'ordre inverse. Version courante : **0.5.4**.

- « NeoST gère-t-il X ? » → [`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md) (inventaire par puce)
- « Que reste-t-il ? » → [`TODO.md`](TODO.md)

## 0.5.4 — dongles, ports, MIDI vérifié (2026-08-23)

Depuis la 0.5.2 : **clés de protection** (Cubase rouge/noire, Notator/Creator, Leader
Board, Cricket Captain, B.A.T. II, Music Master, Jeanne d'Arc, Multiface, Ultimate
Ripper, DAC Pro Sound) avec page Dongles, `disks/dongles.txt` et oracle de rejeu ;
**page Input par port** (souris / manette / clavier sur chaque DE-9) ; **Cubase Lite
vérifié note à note** en headless, corpus MIDI piano/blues ; port MIDI ALSA sous Linux ;
save-state v16. Détail dans les chantiers datés ci-dessous.

## Le blitter a enfin un étalon — et il trouve une divergence (A1+A2+A3) (2026-08-26)

**A1 — le palier PIXEL garde désormais la barrière.** `tests.yml`, le job qui tourne à chaque
push, ne lançait que `--tier fast`, **qui ne compare aucun pixel**.
`--tier full` n'existait que dans `release.yml`, donc une régression de rendu partait sur `main`
et n'était vue qu'à la publication suivante. Nouveau job **`pixel`** : `--tier full` à chaque
push et pull_request, plus `--verify-refs`, plus le dépôt des captures d'écart en artefact quand
il échoue. Coût ~1-2 min. Deux cas mesurés que le `fast` ne voyait pas :
`NEOST_SYNC_DISPATCH=1` casse `nocooper_greetings` à 98,97 %, et le chantier BL3/BL4 (base de
temps du blitter) serait passé au vert.

**A2 — `tools/make_blitter_test.py` + étalon `blitter_timer`.** Le trou mesuré la veille :
`NEOST_BLIT_TRACE=1` rendait **0 blit** sur l'intégralité du corpus pixel, entièrement en
`machine=st`. Le nouvel étalon (EmuTOS 256 Ko, STE, 512 Ko) contraint **deux** propriétés dans une
seule image : la **datation** — lignes 0-99, l'octet TADR relu après chaque blit non-hog, Timer A
en mode délai prescaler /200, donc 1 tic = 200 cyc : insensible au jitter sous-tic, sensible à la
classe BL3 (1088 cyc ≈ 5 tics) — et les **données** — lignes 120-127, destination de 100 blits
16×8 mots depuis un motif à pas `$3B27` qui balaie les 4 plans. ~400 tranches non-hog par run.

**Dents vérifiées, pas supposées.** L'image diffère de **406 px** entre le commit `6bc2ce3`
(AVANT BL3/BL4) et l'état corrigé — **toutes** dans la zone TADR, **zéro** dans la zone de
données, soit la signature exacte d'un écart de datation sans écart de données. Cet étalon aurait
donc attrapé BL3/BL4. Il détecte aussi `NEOST_RAM_SLOT=0` (269 px) et `NEOST_SYNC_DISPATCH=1`
(198 px), et ignore `NEOST_IACK_SYNC=0` (0 px) — ce qui est **correct** depuis BL4, la dette
étant soldée par accès et non plus au point d'IACK.

**Et il a trouvé une divergence dès sa première exécution — BL5.** NeoST vs oracle Hatari :
**397 px / 114816**. Le chemin de **données est byte-identique** (0 px sur la destination) ;
l'écart est **entièrement de datation**, 99 lignes TADR sur 100, avec une dérive qui
**s'accumule** de +1 à +43 tics sur 100 blits, soit ≈ **86 cycles par blit non-hog**. L'oracle est
stable (3 runs Hatari = 0 px entre eux), ce n'est donc pas un artefact de boot. **BL3/BL4 avaient
rapproché** NeoST de l'oracle sans fermer l'écart : 463 px avant → 397 px après. Piste inscrite
en BL5 : NeoST facture l'arbitration une fois par **blit**, Hatari une fois par **tranche**.

**A3 — le corpus livrable progresse.** `blitter_timer` est créé d'emblée sur ROM **libre** : le
corpus qui survivrait au retrait des TOS Atari propriétaires passe de **5 à 6 étalons**, et la
**seule** couverture du blitter est du côté libre.

**Le chemin HOG aussi — et il localise BL5.** Étalon jumeau **`blitter_hog`** (même programme,
`ctrl=$C0`, donc `Blitter::start` au lieu de `Blitter::onSlice`) : ce chemin n'était exercé par
aucun test ni aucun titre. Contrôle de non-trivialité : 380 px d'écart avec `blitter_timer`, dont
**0 px en zone de données**. Et il passe **`ref_kind: oracle` à 0 px** — conformité, pas seulement
non-régression. Conséquence directe : **BL5 est spécifique au NON-HOG**, donc à la fenêtre CPU
entre deux tranches, et non à la facturation du blit (identique dans les deux modes).

⚠ **Deux hypothèses posées puis réfutées le jour même** sur BL5, consignées pour ne pas les
rouvrir : (1) « NeoST facture l'arbitration par blit, Hatari par tranche » — faux,
`Blitter_Start` est ré-appelé à chaque tranche ; (2) « l'oracle tourne le chemin non-CE et son
forfait de 256 cyc » — faux aussi, le défaut d'Hatari ici EST le cycle-exact (mesuré : forcer
`--cpu-exact off --compatible off` change l'image, 397 → 328 px). ⚠ Ces deux options déplacent la
comparaison de 69 px et `hatari_oracle.sh` n'en passe aucune : toute reprise doit les épingler.

⚠ Restent non couverts : le MFP en mode bloc et le stall FIFO du FDC (D3).

## La sélection de lecteur du PSG atteint enfin le FDC (`D-PSG`) (2026-08-25)

**Ce qui n'allait pas.** Le FDC ne **relisait** le port A du PSG (R14 : lecteur A/B, face) que
depuis ses propres accès registre — `Fdc::refreshDriveSide()` n'avait que trois appelants, tous
des accès CPU aux registres FDC — et le sink port A du YM2149 n'avait qu'un abonné, qui ne
faisait que le bouclage RS-232. **Le chemin PSG→FDC n'existait pas.** Hatari, lui, **pousse** :
`psg.c:419-420` appelle `FDC_SetDriveSide` à *chaque* écriture du port A. Conséquence : tout
programme qui écrit sa commande FDC **avant** de sélectionner le lecteur restait bloqué avec
`driveSel_ = -1` — commande partie sans lecteur, `indexTime_` à 0, INTRQ/GPIP5 jamais levé.

**La mesure.** *Stardust* sur STE : écran noir définitif après le menu trainer, le CPU tournant
dans `tst.b D5 / bne $261e` en attente d'une IRQ FDC qui ne venait plus. Trace :
`[fdc-st] cmd=1 state=2 drv=-1 idxTime=0 str=c5` puis **silence FDC total** sur 90 s de temps ST
(4714 lignes en tout).

**Ce qui a changé.** `Machine::setPortASink` appelle désormais `Fdc::refreshDriveSide()`
(passée publique) à chaque écriture du port A, avant le bouclage RS-232 — port direct du
comportement d'Hatari. La fonction est idempotente : elle ne ré-ancre l'index que si le lecteur
a effectivement changé.

**Résultat mesuré.** `drv=0` au lieu de `drv=-1`, INTRQ levé, **374 294** lignes FDC au lieu de
4714, et Stardust STE joue son **intro défilante** (14 couleurs) puis fond au noir et va chercher
la disquette 2 dans le **lecteur B** (`drv=1`, `idxTime=0`, lecteur vide) — le comportement de
l'oracle. `--tier full` : tous les paliers OK ; Lethal Xcess `megast` inchangé (132 cyc).
⚠ Les disquettes 2 et 3 sont **absentes du dépôt** : le titre n'est pas jouable pour autant.
C'est une correction de **fidélité**. ◑ Résidu : l'oracle affichait « INSERT DISK 2 IN ANY
DRIVE » là où NeoST fond au noir puis poll le lecteur B.

## Headless : `--joy-at`, `--joy-script` et `--mouse-at` deviennent répétables (`OUTIL-1`) (2026-08-25)

Ces trois options étaient des **scalaires** là où `--keys-at` / `--key-down` / `--key-up` sont
des vecteurs : la dernière occurrence de la ligne de commande écrasait les précédentes **sans le
moindre avertissement**. Ce n'est pas un détail — le balayage du catalogue du même jour a montré
que ce seul piège fabriquait des « bugs » qui n'existaient pas : *Xenon 2*, *Flood* et *Dynamite
Dux* étaient tous rapportés bloqués, tous jouables une fois la repro corrigée. Les trois passent
en `std::vector`, l'aide porte « (repeatable) », et les sites d'application bouclent sur les
listes (chevauchement : le dernier de la ligne gagne sur les trames communes ; des scripts
disjoints jouent tous). Vérifié : `--joy-at 10 0x80 --joy-at 30 0x08` produit désormais **deux**
lignes « joystick applied », une seule avant.

⚠ Piège de mise en œuvre, consigné : `--joy-at` consomme **deux** arguments, donc un
`emplace_back(next(a), next(a))` aurait un ordre d'évaluation **non spécifié** et inverserait la
trame et la valeur au gré du compilateur — les temporaires nommés sont obligatoires.

Reste ouvert (cf. `TODO.md`) : `--keys-at` ne tient la touche que **2 trames ≈ 40 ms**, ce qui
fausse toute A/B contre un `--cmd-fifo` d'Hatari (~600 ms) — un verdict « confirmé à l'oracle » a
déjà été rendu FAUX par cet écart ; et `stScancode()` ne mappe pas le pavé numérique.

## Bug hunt : les 67 disques du dépôt passés au headless (2026-08-25)

**Couverture intégrale, catalogue sain.** Les 67 images de `disks/st` et `disks/stx` ont été
bootées en headless. **66 sans divergence démontrée.** Le brut donnait 8 anomalies ; **7 tombent**
en réfutation adversariale, dont 5 à l'oracle Hatari ou au réflexe RAM. Les sept sont versées
dans `docs/CASE_STUDIES.md` — New Zealand Story (manque de RAM : `$FC0BD8` est la routine
d'affichage des **bombes** du TOS, pas une boucle mystérieuse), Xenon 2, Flood, Dynamite Dux,
disquettes STX de données seules en A:, Great Giana Sisters, Pipe Dream. *Arkanoid*, tranché
FIDÈLE lors de la passe précédente mais jamais versé, y est également fermé et retiré du
catalogue des bugs ouverts.

**Un seul bug d'émulation survit — `D-PSG`** (fiche complète dans `TODO.md`) : la sélection de
lecteur/face écrite dans le **port A du PSG** n'est jamais poussée vers le FDC. Hatari pousse à
chaque écriture (`psg.c:419-420` → `FDC_SetDriveSide`) ; côté NeoST `Fdc::refreshDriveSide()`
n'est appelé que depuis des accès CPU aux registres FDC — **le chemin PSG→FDC n'existe pas**.
Tout programme qui écrit sa commande FDC *avant* de sélectionner le lecteur reste bloqué avec
`drv=-1`. Symptôme : Stardust sur STE, écran noir définitif après le menu trainer. Oracle :
Hatari atteint « INSERT DISK 2 IN ANY DRIVE ».

**Aucune régression** du chantier blitter du même jour : le seul échec STE-only du catalogue est
`D-PSG`, dont la trace montre un FDC arrêté et non un décalage de base de temps ; Lethal Xcess,
Wings of Death et `faster_atari_ste` passent sur machine à blitter.

**Le principal fabricant de faux positifs est un défaut d'OUTILLAGE**, pas d'émulation :
`--joy-at`, `--joy-script` et `--mouse-at` ne sont **pas répétables** (scalaires, dernière
occurrence gagnante, sans avertissement) — à eux seuls ils ont produit **trois** des huit
« bloquants ». Et `--keys-at` ne tient la touche que **2 trames ≈ 40 ms**, ce qui rend toute A/B
contre un `--cmd-fifo` d'Hatari (~600 ms) **faussée** : un verdict « confirmé à l'oracle » a été
rendu FAUX par cet écart. Recensé `OUTIL-1` dans `TODO.md`.

**Corrections d'inventaire.** *V3* : « CyclesPerVBL ±4 » **retiré, faux positif prouvé** — chez
Hatari ce compteur n'est vivant qu'en mode VDI, la VBL normale vient de la chaîne
`ShifterLines[].StartCycle`, que NeoST porte déjà. *D3* : **confirmé et chiffré** (Hatari 4127
cyc/flush, NeoST 4173, modèle sans stall 4096) avec un avertissement neuf — **à ne pas appliquer
seul**, D3 isolé porterait l'erreur de +45 à +77 cyc, et sous `--fastfdc` l'écart actuel est de
**+14 %**, pas 1,1 %. *MFP* : écart confirmé et exhibé, mais le patch proposé est **rejeté** (il
réactiverait le dispatch sync-driven déjà réfuté, ~1590 fois par trame) ; la borne annoncée
« ≤ 1 instruction » est corrigée en **157 cycles** mesurés.

⚠ **Leçon d'outillage** : un balayage de masse laisse des images de disque **modifiées dans
l'arbre git** (le jeu écrit sur sa disquette). Une image a dû être restaurée. Il manque un
`--disk-ro`.

## Les cycles volés par le blitter avancent enfin l'horloge des timers (2026-08-25)

**Ce qui n'allait pas.** NeoST avait DEUX bases de temps dès que le blitter volait le bus
en dehors d'un quantum CPU. `Blitter::billCycles` → `Cpu68k::addBusWaitCycles` n'avance que
l'horloge du cœur Moira ; or `Blitter::onSlice` (tranche non-hog) est le **callback de
l'échéance `Scheduler::BLITTER`** : il tourne dans `Scheduler::runTo`, donc **entre** deux
`cpu.run()`. Ces cycles n'étaient donc ni dans `ran` (retour de `Cpu68k::run`, mesuré
depuis un `quantumStartBus_` réancré sur l'horloge CPU à chaque entrée) ni dans
`sched.now()` : **perdus** pour l'ordonnanceur. Chez Hatari le problème n'existe pas —
`Blitter_AddCycles` (`blitter.c:351-352`) écrit dans `nCyclesMainCounter` /
`CyclesGlobalClockCounter`, les compteurs mêmes que le CPU incrémente et que `CycInt` lit
pour dater ses échéances (`cycInt.h`, `CycInt_Process`).

**La mesure.** Sonde `NEOST_QDELTA_DIAG` (delta = horloge CPU − `sched.now()` à l'entrée du
quantum) sur *Lethal Xcess* en `megast`, 6000 trames : delta plat à **40** (décalage de
reset, sans rapport) pendant ~7 millions d'entrées de `run()`, puis **escalier de 136 en
136 sur les 21 dernières** — 8 tranches non-hog × 136 cyc = **1088 cycles bus** de dette,
résorbée d'un seul coup par le `syncTo` du hook d'IACK juste avant le handler Timer A. Ce
saut mangeait les 2 tics de prescaler de marge : `Mfp::readTimerData` rendait TADR = `$3C`
au lieu de `$3D`/`$3E`, et la garde `$14C2E` du jeu tombait dans son `ILLEGAL`. Le
symptôme FDC noté au TODO le 2026-08-19 (`cmd=d0` puis silence) était un **leurre** :
identique en `machine=st`, où le jeu démarre.

**Ce qui a changé.** `Blitter::billCycles` crédite désormais l'ordonnanceur
(`Scheduler::addStolenCycles`) **en plus** de l'horloge Moira, mais **uniquement** hors
quantum (discriminant `Cpu68k::inRun` — dans le quantum, le mode HOG est déjà capté par
`ran` puis reversé par le `runTo(now + ran)` de `Machine::runFrame` ; créditer les deux
double-compterait). La facturation est **par accès**, pas en lot : `Blitter::billCycles`
(port de `Blitter_AddCycles` + `Blitter_FlushCycles`) est appelé par `readWord`/`writeWord`
pour leurs 4 cycles et à chaque **arbitration**, à sa position réelle — prise du bus AVANT
le transfert, restitution APRÈS. `addStolenCycles` **dispatche** (`syncTo`) au lieu
d'avancer `now_` en silence, si bien qu'une échéance tombant au milieu d'une tranche est
servie **au cycle où elle tombe**. C'est le modèle d'Hatari, dont `CycInt_Process`
(`cycInt.h:85-88`) est ré-entré depuis le handler `INTERRUPT_BLITTER` lui-même. La
ré-entrance de `Scheduler::runTo` est acquise : `fired`/`minAll` étaient déjà des locales,
seul `firingDue_` — l'ancre anti-dérive de l'événement en cours — manquait, il est
désormais sauvegardé/restauré par un garde RAII. **Aucun état persistant ajouté, format
de save-state inchangé.**

**Résultat.** *Lethal Xcess* : plus aucun `BREAK $14C2E`, et le jeu va **en jeu** sur les
**trois** machines à blitter — `megast` (trame 5552 avant), `ste` et `megaste` (trame 5523
avant, écran noir à 1 couleur ; désormais 26-29 couleurs, écran de jeu). `machine=st` :
capture 26000 trames **bit-identique** et métriques au cycle près (le patch y est inerte,
pas de blitter). Boot EmuTOS `megast` nu : capture bit-identique et `timer IRQ max
lateness` **3334 → 161 cyc**. `--tier full` : tous les paliers OK.

**Sur la métrique `timer IRQ max lateness`.** Une étape intermédiaire de ce chantier
créditait l'ordonnanceur en **fin de tranche** : elle réparait le jeu mais faisait monter ce
compteur de 132 à ~265 cyc, un retard intra-tranche borné (136 cyc, 264 sur tranche pleine)
qui était le prix du crédit groupé. Le passage au dispatch **par accès** (ci-dessus) l'a
supprimé : le compteur **redescend à 132** sur `megast`, `ste` et `megaste` — soit la valeur
ST, mais cette fois sans plus rien masquer, là où AVANT tout le chantier il cachait un saut
cumulatif de 1088. Divergences recensées **BL3** et **BL4**, toutes deux ✅, dans
`docs/HATARI_DIVERGENCES.md`.

**Validation de la ré-entrance.** Build à assertions ACTIVES (`-UNDEBUG` — attention,
`RelWithDebInfo` réinjecte `-DNDEBUG` APRÈS `CMAKE_CXX_FLAGS`, il faut passer par
`CMAKE_CXX_FLAGS_RELWITHDEBINFO`) : *Lethal Xcess* `megast` 26000 trames, ~1,6 M
préemptions, des milliers de tranches à 64 dispatches imbriqués chacune — **aucune**
assertion de `runTo` déclenchée (`armedInvariant`, `nextDue_ == scanNextDue()`). Idem
`ste`/`megaste`.

## Le halt du 68000 s'annonce (double faute de bus/adresse) (2026-08-25)

Un `cpuDidHalt()` était disponible côté Moira mais non surchargé : NeoST **arrêtait le
CPU en silence** sur une double faute. Écran figé, aucune explication — c'est ce qui avait
fait ouvrir le faux bug « Stardust sur ST : NeoST reste noir là où Hatari halte ».
**Vérification (2026-08-25) : NeoST HALTE déjà, sur la MÊME instruction que Hatari**
(`$FC5082`, `A7=$4E7340E7` impair, après la bus error `$FFFF8900` d'un jeu STE lancé sur
ST) ; 0 instruction exécutée après la trame ~1826, écran noir des deux côtés. Ajouté :
le message `[cpu] 68000 halted: double bus/address error while taking exception vector N
(SSP=$…)` (vecteur latché par les délégués d'exception de groupe 0 — `reg.pc0` a déjà
été avancé par le prefetch, il ne désigne PAS la faute, donc aucun PC n'est affiché,
comme Hatari `gui-sdl/dlgHalt.c:66-71`), le cas distinct « reset vector fetch failed »
(`Moira::reset`), l'accesseur `Cpu68k::halted()` et une ligne de bilan headless.
**Aucun changement de comportement émulé** ; suivis TODO/CASE_STUDIES clos.

## Page Input : ce qu'on branche dans les deux ports joystick (2026-08-23)

Vue **par port** au lieu d'une liste de manettes : « Port 0 (mouse port) » = souris
(défaut) / auto (2ᵉ manette) / clavier / telle manette ; « Port 1 » = auto (1ʳᵉ manette) /
clavier / telle manette. S'exprime sur le mécanisme existant (rôles par GUID `joymap=`,
émulation clavier, nouveau `port0=mouse|auto`) : la fenêtre Joystick et le menu borne
restent cohérents. **Changement de défaut** : une 2ᵉ manette ne prend plus le port souris
toute seule (`port0=mouse`) — on l'y branche explicitement, ou `port0=auto` pour
l'ancien comportement. Un joystick qui occupe le port 0 **débranche la souris hôte**
(`g_port0Joystick`), comme sur un vrai ST ; les clés de protection du port (page Dongles)
s'affichent à côté.

## Dongles, 2e passe : oracle de rejeu, un périphérique par port, port cartouche abstrait (2026-08-23)

Revue d'architecture du chantier dongles, six manques corrigés :
1. **Oracle** — format de trace de référence (`R3`/`R4`/`U`, `docs/EXTENSIONS.md`),
   `--key-log FILE` l'écrit, `--key-replay FILE` rejoue une capture (matérielle ou NeoST)
   contre la machine d'état sans machine et sort 0/1 ; recette de capture documentée.
2. **Un périphérique par port** — `PortDevices` remplace l'enum unique « à la Steem » :
   `joy0=`/`joy1=`/`rs232=`/`printer=`/`cartbutton=`, `--plug PORT=DEVICE`, connecteurs
   vérifiés (une clé joystick entre dans les deux DE-9, le GUI signale le mauvais port),
   coexistence. `disks/dongles.txt` branche la clé d'un jeu au montage (emplacements vides
   seulement), GUI, borne et headless.
3. **Port cartouche abstrait** — `core/CartDevice.hpp` : les périphériques s'abonnent aux
   signaux /ROM3, /ROM4, /UDS ; le `Bus` boucle sur la liste (plusieurs clés possibles),
   plus de `dongleUds` spécial dans le wrapper CPU. `CubaseDongle` → `CartridgeKey`.
4. **Observabilité** — page Dongles : sondages, dernier octet, état, armement.
5. **Save-state v16** — `PortDevices` sérialisé (oscillateur, date, périphériques branchés).
6. **DAC Pro Sound** — bloc DC propre amorcé au branchement (plus de clic), fader `mix_dac=`
   page Sound.
Non couvert : les frontends WASM/Android n'exposent toujours pas les dongles.

## Page « Dongles » et adaptateurs de port : `PortDongle` (2026-08-23)

Après les clés Steinberg, **les autres** : recherche sur les dongles ST (Steem SSE, WinUAE,
MiSTery, forums — inventaire dans `docs/EXTENSIONS.md`), puis transcription des onze
adaptateurs que Steem émule. `--adapter NAME`, `adapter=`, nouvelle page **Dongles** de la
configuration (la clé Steinberg y déménage depuis la page MIDI). Joystick : **Leader Board /
10th Frame** (haut+bas), **Cricket Captain / Rugby Coach / Multi Player Soccer Manager**
(oscillateur `%1100`/`%1101`). RS-232 : **B.A.T. II** (CTS à 0), **Music Master** (DTR → DCD
retardé de 200 cycles), **Jeanne d'Arc** (DCD sur décroissance de RTS|DTR). Parallèle :
**Pro Sound Designer**, DAC 8 bits sur le port imprimante (Wings of Death, Lethal Xcess sur
STF) — R15 horodaté et rejoué par le YM avant son HPF. Boutons **Multiface ST** (GPIP7) et
**Ultimate Ripper** (RI) : page Dongles ou `--button-at N`, relâchés à la VBL. Crochets :
`Mfp::setGpipReadHook`/`setMonitorButton`, `YM2149::setPortBDac`, sonde joystick IKBD, abonné
port A. **OFF par défaut**, étalons inchangés (`--tier full`), auto-tests par protocole.
**Clé C-Lab Notator / Creator** (`--dongle notator`) : les équations de l'EP600, publiées
par TPH en octobre 2025 et transcrites en C dans le firmware SidecarTridge `md-notator`,
sont portées dans `CubaseDongle` — bascule d'armement `FEEDB1` sur /ROM4 (`$FA00EA`),
8 bascules D cadencées par UDS (désarmée) ou par la **descente** de /ROM3 (armée), resets
asynchrones D8/D9 ; nouveau crochet `rom4Read` dans le `Bus`, save-state **v15**.
Non émulés faute de relevé public : Log 3 (EP330), Pro-24, Avalon, Zodiac.

## Cubase Lite joue un SMF en headless, et on le vérifie note à note (2026-08-23)

La chaîne « classiques du piano → Cubase Lite → Pianoteq » tenait déjà (sortie CoreMIDI
horodatée du 21/08), mais rien ne la **prouvait** sans oreille. Désormais :
`neost-headless --midi-dump FILE` journalise chaque octet MIDI OUT daté de son cycle
68000 ; `tools/midi_compare.py` le confronte au SMF donné au séquenceur (notes, ordre,
vélocités, durées, pédale CC64, pente de tempo, gigue) ou le convertit en SMF (`--to-smf`) ;
`tools/run_midi_sequencer.py` rejoue tout le scénario — boot TOS 1.04 sur un lecteur GEMDOS
temporaire, **auto-lancement de Cubase** par la ligne `#Z` de `DESKTOP.INF`, *File → Import…*
à la souris relative (`--mouse-at`), nom de fichier tapé au clavier **AZERTY** (nouveau
`--azerty` : sur TOS FR, `M` tombait en virgule), Enter du pavé (`|`) = Play — et tranche :
**PASS**, 200 notes, pente 1,001, gigue σ 0,4 ms. Étalon du palier `fast` (≈3 s).
Corpus **piano classique**, un dossier 8.3 par compositeur (`disks/midi/BACH`, `MOZART`,
`BEETHOVE`, `CHOPIN`, `HAYDN`, `SCARLATT` — 159 pièces : inventions, sinfonies et Clavier bien
tempéré I de Bach, 17 sonates de Mozart, sonates de Beethoven/Haydn, préludes et mazurkas de
Chopin ; partitions Humdrum de C. S. Sapp, CC BY-NC-SA, rendues par music21, sources et `.krn`
sous `disks/midi/sources/`) ; le blues déménage en `BLUES/`. Un canal, une piste de notes par
fichier — Pianoteq ne voit plus de note-on en double.
Trois quirks **de Cubase Lite** trouvés par l'étalon et absorbés par `midi_simplify.py` :
une armure (méta 0x59) en cours de morceau lui fait **jeter la piste de notes** à l'import
(gardée au tick 0 seulement) ; doublure à l'unisson ou note répétée sans trou → note coupée à
2 ms (`--detach` : fusion, troncature, tick de séparation) ; note-off un tick interne en
avance (tolérance de durée à part). `disks/midi/README.md` : provenance, recette Pianoteq.

## Clé Steinberg sur le port cartouche : `CubaseDongle` (2026-08-23)

`--dongle cubase3|cubase2|auto`, page MIDI, `dongle=` — **OFF par défaut**. Clé **rouge**
(Cubase 3.10 / Score / Audio : EPLD 5C060, 16 bascules T, A8 → D8, cadencée par /ROM3) et clé
**noire** (Cubase 2.01 : PAL16R8, A1-A8 → D8-D15, cadencée par **chaque** front /UDS du CPU —
crochet `udsDone` dans `NeostMoira`, un test de bool par accès). Banque `/ROM3 $FB0000-$FBFFFF`
(pas `$FA0000` : le TOS ne sonde que /ROM4, la clé cohabite avec le HD GEMDOS). Équations
transcrites de MiSTery (`cubase2_dongle.v`, `cubase3_dongle.v`) ; Hatari n'émule aucune clé.
Save-state **v14** (état de la clé + drapeau). ⚠ Fidèle aux équations, **pas encore confrontée
à un Cubase à clé** (aucun dans le dépôt) — cf. `docs/EXTENSIONS.md`, `TODO.md`.

## Warnings de build éliminés (2026-08-23)

`Ne2000.cpp` (constantes CR inutilisées), `SlirpBackend.cpp` (`register_poll_fd` dépréciés
sous libslirp ≥ 4.8 : pragma + `SlirpCb` remplie en place, le constructeur de déplacement
implicite déclenchait l'avertissement hors de portée du pragma), `CMakeLists.txt`
(`neost_core` lié deux fois via `neost_net` PUBLIC → `ld: duplicate libraries`).

## Pages servi par la CI : `wasm/` sort du dépôt (2026-08-23)

**Le bundle WebAssembly n'est plus commité.** Le job `wasm` de `release.yml` le dépose
en **artefact Pages** et un job `pages` le publie (`build_type=workflow`). Une seule
construction Emscripten par push — le job existant sert les deux usages, l'archive de
release et la démo en ligne. `wasm/` est gitignoré ; `cmake --build build-web --target
neost-web` le régénère pour qui veut la démo hors ligne.

**Ce que ça règle.** En `build_type=legacy`, Pages servait la branche : le dépôt était
publié **en entier** sur habib256.github.io/neost/, ROM Atari et jeux compris — une
contrepartie qui avait été assumée faute de mieux (cf. `TODO.md` § contenu propriétaire).
Le déploiement par artefact ne publie QUE le bundle. Et la CI ne pose plus de commit de
bot sur `main` à chaque push, ce que faisait la recommission du bundle.

**L'URL change** : la démo passe de `/neost/wasm/` à la **racine** `/neost/`. Le lien du
README suit.

Troisième et dernier état d'un aller-retour assumé : garde de fraîcheur
(`tools/wasm_stamp.sh`, rouge sans réparer) → recommission par la CI (réparait, mais
commit de bot systématique) → déploiement par artefact.

## Cartes SD UltraSatan amorçables : `tools/make_hd_image.py` (2026-08-22)

Un dossier hôte devient une carte SD que le **vrai TOS** monte en C:, pilote compris.
`make_hd_image.py SRC OUT.img` écrit la table de partitions Atari AHDI à la main
(aucun outil hôte ne la connaît : ce n'est pas un MBR DOS), délègue le FAT16 à mtools,
et **greffe automatiquement le pilote** depuis un disque donneur détecté dans `hd/`.
Aucun pilote n'est embarqué dans le dépôt — HDDRIVER est commercial ; on reprend celui
que l'utilisateur possède déjà. Vérifié `drvbits=$00000007` (A B C) sous **TOS 1.04,
TOS 2.06 et EmuTOS**.

**L'amorçage HDDRIVER est en deux étages, chacun gardé par une somme `$1234`** — et
c'est là que tout se joue. L'étage 1 (secteur racine, que le TOS n'exécute que si sa
somme vaut `$1234`) choisit la partition dont le drapeau masqué par `$F8` vaut `$80`,
lit son premier secteur, **refait la même somme dessus** (`cmp.w #$1234,d0` / `bne` →
abandon) et seulement alors saute dedans. Deux conséquences, toutes deux constatées en
échec avant d'être comprises au désassembleur :

- `mformat` écrit un secteur DOS de somme quelconque ⇒ l'étage 1 refuse de l'exécuter,
  et la trace ACSI s'arrête pile après les secteurs 0 et 2. On rétablit `$1234` par un
  mot d'ajustement en `$1FE`, là où DOS met `$55AA`.
- le code de l'étage 2 commence à **`$34`**, cible du `BRA.S` de tête, donc il empiète
  sur les 2 derniers octets du champ « nom de volume ». Le copier depuis `$36` laisse
  ces 2 octets s'exécuter et fait dérailler l'étage 2 **en silence**.

Le BPB de mformat (FAT16, 512 o/secteur) convient tel quel : l'étage 2 sait le lire,
il n'y a pas à reproduire la géométrie du donneur.

**Piège de méthode consigné** : `--keys` tape APRÈS le boot, or TOS 2.06 attend une
touche sur son écran mémoire — utiliser `--keys-at 700 " "`, faute de quoi aucun disque
dur ne se monte et l'on croit à une divergence d'émulation. Et juger sur `_drvbits`
(`$4C2`) et les vecteurs `hdv_*` (`$472`), jamais sur les icônes du bureau : elles
viennent des lignes `#M` de `NEWDESK.INF` et s'affichent sans lecteur monté.

## libmt32emu vendorisé — le MT-32 cesse d'être une option de machine (2026-08-22)

**Munt (`libmt32emu` 2.8.3) passe de dépendance système à copie vendorisée**, dans
`extern/mt32emu`, liée en **statique**. Le `find_package(MT32Emu CONFIG QUIET)` exigeait
`brew install mt32emu` / `libmt32emu-dev` : aucun runner de CI ni aucune image de release
ne l'installait, donc le Roland MT-32/CM-32L n'existait **dans aucun binaire livré** — et
son absence ne se signalait que par une ligne de configuration. Le TODO du 2026-08-21
(« embarquer le `.dylib` ou compiler Munt en statique ») est fermé par cette seconde voie.

Code d'amont **intact** ; seul le `CMakeLists.txt` du dossier est de NeoST — il remplace
les 578 lignes d'amont (installation d'en-têtes, export de paquet CMake, `.pc`) par ce que
NeoST utilise : API C++ seule, statique, rééchantillonneur interne, aucune dépendance
externe. Les en-têtes sont recopiés dans `build/generated/mt32emu/mt32emu/` — les sources
d'amont s'incluent à plat (`"Synth.h"`) alors que NeoST écrit `<mt32emu/mt32emu.h>` —
même schéma que Moira. `add_subdirectory` est placé **après** le garde-fou GLFW : seul le
GUI s'en sert, inutile de le compiler pour les cibles headless, Android et WASM.

Licence : LGPL 2.1+, compatible avec la GPL 3 de NeoST (§ 3 de la LGPL), et l'édition de
liens statique est couverte puisque NeoST publie ses sources, cette copie comprise.
Détails et marche à suivre pour une mise à jour : `extern/mt32emu/NEOST_VENDOR.md`.
⚠ Les ROM Roland ne sont toujours pas incluses (`roms/mt32/`, à fournir).

## FujiNet retiré : NeoST n'émule plus que du matériel qui a existé (2026-08-22)

**Le FujiNet virtuel est supprimé du projet.** Il n'a jamais existé de FujiNet pour
l'Atari ST : c'était un *binding de référence* inventé ici, greffé sur l'opcode vendeur
ACSI `$60`. Un émulateur d'Atari ST n'a pas à porter du matériel imaginaire — et ce
sous-système était par ailleurs le moins tenu du dépôt (`FujiHostLive` : **0 %** de
couverture sous la suite complète).

Retiré : `io/FujiDevice.{cpp,hpp}`, `net/FujiHost.hpp`, `net/FujiHostLive.{cpp,hpp}`,
`net/FujiHostReplay.{cpp,hpp}`, `net/MiniJson.{cpp,hpp}`, `net/HttpClient.{cpp,hpp}`,
les fixtures `tests/fixtures/fuji/`, `dev/fujinet/`, `gemdos/DEMOS/NWGET.TOS`, les
drapeaux `--fujinet*` / `--fuji-selftest`, les clés `fujinet*=` de `neost.cfg`, l'étalon
`fuji_selftest` et la section FujiNet de `docs/EXTENSIONS.md`.

**Ce qui reste et ne bouge pas.** Le modem Hayes, l'anneau MIDI, EtherNEC, NetUSBee et
l'UltraSatan sont intacts — tous correspondent à du matériel réel. La couche socket
partagée qui vivait dans `HttpClient` (namespace `neonet` : `tcpConnect`, `sockSend`,
`sockRecv`…) est extraite dans **`net/Socket.{hpp,cpp}`**, dont c'est désormais le seul
rôle ; le modem et l'anneau MIDI y sont repointés. `NEOST_FUJI_TRACE` devient
`NEOST_NET_TRACE`. Les programmes ST `MIDITEST.C` / `MODMTEST.C`, qui testent le MIDI
ring et le modem, migrent de `dev/fujinet/` vers **`dev/netdemo/`** avec leur `build.sh`.

**Effet de bord bienvenu sur l'ACSI** : l'opcode `$60` était routé vers le périphérique
virtuel sur une cible ; il retrouve le **rejet strict sur toutes les cibles**, exactement
comme le port de `hdc.c`. NeoST est donc *plus* fidèle qu'avant sur ce point.

**Save-states : v12 → v13.** `FujiDevice` et `Acsi::fujiPending_` quittent le flux, et
les bits de drapeau d'en-tête se décalent (bit1 = EtherNEC, bit2 = UltraSatan, bit3 =
NetUSBee). Les `.state` antérieurs sont refusés avec un message explicite.

**Vérification** : `--tier full` vert — 19 étalons, dont les 11 comparaisons pixel, **0
pixel de différence**. Le retrait est neutre sur tout ce que la suite couvre.

## `docs/FUJINET.md` renommé, et un backend Internet réel pour la NE2000 (2026-08-22)

**Renommage.** `docs/FUJINET.md` devient **`docs/EXTENSIONS.md`** : le fichier documentait
depuis longtemps *toutes* les extensions NeoST — UltraSatan (stockage), NetUSBee, EtherNEC,
modem Hayes, anneau MIDI — alors que son nom n'annonçait que le FujiNet, **qui n'a jamais
existé sur Atari ST** (c'est le seul binding « de référence » du lot, sans matériel
correspondant). Un tableau en tête distingue désormais ce qui a réellement existé de ce qui
est une invention NeoST. `git mv` (l'historique suit), 17 fichiers de références mis à jour.

**`NetBackendSlirp` (en cours).** Le TODO « EtherNEC — backend réel » est attaqué :
`src/net/SlirpBackend.{hpp,cpp}` donne à la NE2000 émulée un accès Internet par **NAT en
espace utilisateur** (libslirp, le routeur de QEMU) — aucun privilège, pas de pcap ni de TAP.
L'ST reçoit 10.0.2.15/24, passerelle 10.0.2.2, DNS 10.0.2.3, DHCP servi par SLIRP. Option
CMake `NEOST_WITH_SLIRP` (pkg-config), drapeaux `--slirp` / `--slirp-restricted`, auto-test
`--slirp-selftest`. **3 vérifications sur 4 passent** (ARP, DHCP, compteurs) ; la sortie
réelle (DNS, opt-in `NEOST_SLIRP_ONLINE=1`) reste à finir — état détaillé, pièges déjà
résolus et pistes dans `TODO.md`, entrée en tête de liste.

Trois pièges de libslirp valent d'être retenus : ses callbacks `register_poll_fd` sont
*deprecated* mais appelés **sans test de nullité** (SIGSEGV à la première socket sortante) ;
`clock_get_ns` doit partir de zéro, sinon toute socket UDP naît « expirée » et meurt au
premier tour ; et SLIRP **ARPe l'invité** avant de lui livrer un paquet — sur un vrai ST
c'est la pile TCP/IP qui répond.

## La CI reconstruit ET recommite le bundle `wasm/` — plus de garde, plus de rouge (2026-08-22)

La démo en ligne est le dossier `wasm/` **commité** (Pages sert la branche). Conséquence :
toute modification de `src/**` le périmait, et la garde `tools/wasm_stamp.sh --check`
rendait la CI **rouge** jusqu'à une reconstruction manuelle — le 2026-08-19, puis le
2026-08-21 (chantier UltraSatan/NetUSBee). La garde faisait son travail (sans elle, la
démo restait périmée en silence) mais elle **signalait** au lieu de **réparer**.

Désormais, sur push de `main`, le job `wasm` de `release.yml` recommite le bundle qu'il
vient de construire : `[skip ci]` pour ne pas boucler, rien de commité s'il est identique
(emcc est déterministe à version d'emsdk égale), et en cas de push concurrent il repart du
nouveau sommet en y redéposant le bundle. Le dossier reste donc dans l'arbre de travail
(`git pull` après le run) et la démo suit les sources sans intervention.
`tools/wasm_stamp.sh` est supprimé.

Essai intermédiaire écarté : déployer Pages depuis l'**artefact** de CI (`build_type=workflow`,
`upload-pages-artifact` → `deploy-pages`). Ça marchait — démo à jour par construction, et le
dépôt n'était plus publié — mais le bundle disparaissait de l'arbre de travail, ce que le
mainteneur ne veut pas. Pages reste donc en `build_type=legacy` (`main/(root)`), avec sa
contrepartie assumée : le dépôt est publié en entier, ROM comprises (cf. `TODO.md`).

## Page Sound : mixeur par source + choix MT-32 / CM-32L (2026-08-21)

Configuration → Sound : **faders** YM2149, son DMA (STE), bruits du lecteur, MT-32 (0-200 %,
100 % = matériel), appliqués à chaud et persistés (`mix_ym=`, `mix_dma=`, `mix_drive=`,
`mix_mt32=`). YM et DMA sont dosés **en amont du LMC1992** (`neost::mixEmulatedFrame` reçoit
deux gains optionnels ; headless et web restent neutres, l'image sonore de référence ne
bouge pas — la voie DMA est rendue seule, dosée, puis le YM rejoint le mix). Même page :
sorties MIDI (GM, CoreMIDI, MT-32) et **modèle Roland Auto / MT-32 / CM-32L** (`mt32_model=`,
réouverture de Munt à chaud ; le chargeur ne retient que les ROM complètes et préfère
CM-32L 1.02, jamais la variante CM-32LN).

## Sorties MIDI hôte : synthé GM, port CoreMIDI, Roland MT-32/CM-32L (Munt) — livraison sans gigue (2026-08-21)

Le MIDI OUT de l'ACIA a enfin des oreilles côté hôte (`src/audio/MidiOutMac.*`, macOS) :
**synthé General MIDI intégré** (DLSMusicDevice) et **port CoreMIDI virtuel « NeoST MIDI OUT »**
(GarageBand, Logic, synthés logiciels). Mesure sur Cubase Lite : tempo exact (pente hôte/ST
1,0007) mais livraison au fil de l'exécution = **gigue ±60 ms** (σ 28 ms), le « bizarre » —
l'émulation avance par rafales de trames. Correctif : **livraison horodatée** — chaque octet est
daté de son cycle 68000 (`MidiAcia::setMidiSinkTimed`), ancré sur l'heure réelle de sa trame
(`emuNext`) + 30 ms d'avance fixe, et délivré par un thread à l'heure dite.
Et l'expandeur que visaient les morceaux d'époque : **Roland MT-32 / CM-32L** via
**libmt32emu (Munt, LGPL 2.1)**, optionnel au configure (`brew install mt32emu`), ROM Roland à
déposer dans `roms/mt32/`. Rendu DANS la sortie audio de NeoST, événements datés à
l'échantillon (`Synth::playMsg(msg, timestamp)`) : l'horloge est l'audio, gigue nulle par
construction (`src/audio/Mt32Synth.*`, `Audio::produceFrame(frameCycles, frameEndCycle)`).
Menu *Machine → MIDI OUT → …* ; `neost.cfg` : `midi_out_gm=`, `midi_out_port=`, `midi_out_mt32=`,
`mt32_roms=`. Traces : `NEOST_MIDI_TRACE` (cycle ST par octet), `NEOST_MIDIOUT_TRACE` (heure hôte).

## Câble de bouclage MIDI débranché par défaut — Cubase Lite charge ses morceaux (2026-08-21)

L'ACIA MIDI rebouclait en permanence OUT→IN (câble de test du diagnostic Atari). Avec le
MIDI Thru de Cubase/MROS, chaque octet émis revenait et repartait : larsen infini, « gel » au
chargement de `JAMMER.ALL` (73 IRQ RX pour 73 TX dans le save-state du gel). La fiche devient
**optionnelle et débranchée par défaut** (`MidiAcia::setLoopback`, `--loopback` branche RS-232
ET MIDI, menu *Machine → MIDI loopback cable*, `midi_loopback=`). Cas détaillé dans
`docs/CASE_STUDIES.md`. Le test « M MIDI » du diagnostic exige donc `--loopback`.

## Fenêtre « Keyboard » : le clavier ST en photo, toutes ses touches cliquables (2026-08-21)

Menu *Windows → Keyboard* (`showKbd=` dans `neost.cfg`) : la photo `pic/Black_Keyboard_AtariST.jpeg`
(décodée par `stb_image`, nouveau `extern/stb`, domaine public) avec une zone par touche →
scancode IKBD (`Ikbd::keyEvent`) : appui tenu tant que la souris l'est, Shift/Control/
Alternate/CapsLock collants (armés d'un clic, relâchés après la touche suivante), surbrillance
et info-bulle `nom ($scancode)`. Les libellés fantaisistes de la photo sont mappés à leur
position sur un vrai ST (grande touche à gauche de Z = Shift gauche, « Splift » = Shift droit,
« Ne » = Insert, « Rel » = Clr/Home, « PgDn » = Delete, vierge sous Tab = `~, pavé
« Num Lock / × ÷ » = ( ) / *). `src/gui/KeyboardWindow.cpp` ; `pic/` est livré par
`stage_free_data.sh`.

## Ctrl+Alt+F : bascule bureau ⇄ borne au clavier (2026-08-21)

Même action que `F8`, sous forme de chord hôte (discipline de `Ctrl+Alt+G` : `F` seul va
toujours à l'IKBD, le relâchement du chord est absorbé). `onKey` dans `src/main.cpp`.

## UltraSatan + NetUSBee : le vrai couple stockage/réseau du ST (2026-08-21)

Le FujiNet virtuel est un binding de référence sans matériel ; l'écosystème ST, lui, a tranché
autrement : **UltraSatan** (SD sur ACSI) et **NetUSBee** (Ethernet + USB sur le port cartouche).
Les deux sont maintenant émulés, OFF par défaut, sans effet sur les étalons.

- **UltraSatan** (`src/io/UltraSatan.*`, hooks dans `Acsi`) : 2 slots = cibles ACSI 0-1 (ID
  réglable en headless), INQUIRY `JOOKIE  UltraSatan` (RMB, n° de slot, v1.20), slot vide =
  NOT READY / medium not present, horloge propre sur les cycles émulés, et les **paquets ICD
  `$20 'US…'`** portés du firmware v1.20 (atarijookie/ce-atari) : `CurntFW`, `RdCl`/`WrCl`
  (magie `RTC`), `RdINQRN`/`WrINQRN`, `RdSt`/`WrSt` (magie `$83 $03 $17`), `RdLog` ; `RdFW`/`WrFW`
  refusés (pas de dataflash émulée). Garde-fou : les paquets `'US'` ne sont routés que sur les
  slots — toute autre cible reste byte-identique à `hdc.c`. **EmuTOS monte C:** depuis une image
  SD générée (`tools/make_usatan_hd.py`, partition GEM FAT16) sans un octet de pilote.
- **NetUSBee** (`src/io/Isp1160.*` + `io/Ne2000` inchangé) : contrôleur hôte USB ISP1160 décodé
  aux adresses du pilote FreeMiNT (`isp116x.h`) — latch `$FA0000`, lecture `$FA8000`, données
  `$FB8000`, commande `$FBC000`, accès MOT avec effets de bord une fois par accès CPU. ID `$6120`,
  reset logiciel, registres OHCI/ISP, hub racine **vide** (2 ports), ATL achevée en
  `DeviceNotResponding`. ⚠ Fenêtre LSB partagée avec le CR de la NE2000, consigné.
- **Tests** : `--usatan-selftest` (15 checks, séquence **LongRW** exacte de `US_CONF` — la première
  version lisait le statut APRÈS la bascule R/W, que le matériel efface : le harnais avait tort,
  pas le cœur) et `--netusbee-selftest` (11 checks, primitives **raw** du pilote — idem : le
  harnais utilisait la variante swappée que le pilote n'emploie pas pour les registres), tous
  deux au palier `fast`. Verdict série **`usatan_netusbee`** : une carte SD générée (16 Mo, GEM
  FAT16) qu'EmuTOS monte en C: et dont il lance `AUTO\USTEST.PRG` — programme 68000 relogeable
  qui pilote les deux cartes comme les logiciels d'époque. Trois règles EmuTOS apprises en chemin
  (`bios/blkdev.c`, `bios/disk.c`) : disque dur présent ⇒ la disquette n'est plus amorcée ; un
  secteur racine exécutable n'est lancé que sur un disque SANS partition reconnue ; FAT12/16 par
  le nombre de clusters (> 4084 ⇒ FAT16) — une partition de 2 Mo était lue en FAT12 et la chaîne
  du PRG cassait. (Et Rwabs ne revient jamais depuis un secteur de boot : Floprd.) Save-states **v12**.
- GUI : case UltraSatan + slot 2 (page Hard Disks), case NetUSBee exclusive d'EtherNEC (page
  Network) ; `neost.cfg` : `ultrasatan=`, `sd2=`, `netusbee=`. Headless : `--ultrasatan`,
  `--ultrasatan-id N`, `--sd1/--sd2 IMG`, `--netusbee`.

## Cuddly Demos ré-activé : l'oracle Hatari n'est pas déterministe (2026-08-19)

L'étalon `cuddly_demos` était **désactivé depuis le 2026-08-01** au motif que « l'animation
est à une PHASE différente » (au mieux 16023 px d'écart, jamais 0). Ce verdict est **réfuté**,
et le coupable n'était pas NeoST.

- **NeoST rend cette démo BYTE-IDENTIQUEMENT à Hatari** : 0 px / 114766 sur **220 trames
  consécutives** (3300-3519 côté NeoST, 3361-3580 côté oracle).
- **Ce qui manquait était la bonne trame oracle.** Hatari fait `Hatari_srand(time(NULL))`
  (`sdl/main_sdl.c`) et tire avec ce RNG la **position angulaire initiale de la disquette**
  (`fdc.c`) : la durée du boot — donc la numérotation des trames de l'AVI — **change d'un run
  à l'autre**. Mesuré : la même trame NeoST tombait sur la trame Hatari **n+61** dans un run
  et **n−2** dans un autre ; deux runs lancés à quelques secondes d'intervalle, eux, sont
  identiques. Un balayage à `frame:` figé ne pouvait pas converger — il cherchait au mauvais
  endroit, pas trop court.
- **Outillage : `oracle_scan: N`.** `hatari_oracle.sh` accepte `HATARI_ORACLE_SCAN` (extrait
  la fenêtre au lieu d'une image) et `run_etalons.py --oracle` retient la trame **identique**
  à la capture NeoST — jamais la « moins pire » : installer une image simplement proche
  figerait un écart au lieu de le signaler. Aucune correspondance ⇒ échec bruyant, et c'est
  alors une vraie divergence. Les **7 étalons qui bootent un disque** en sont équipés.
  La capture NeoST passe donc AVANT l'oracle dans `run_one` (elle sert de clé de recherche).
- Résultat : `cuddly_demos` repasse **actif, `ref_kind: oracle`, `max_diff_px: 0`** — la
  trame retenue par le scan est à **+60** de la nominale et à **0 px**. Plus aucun étalon
  désactivé dans la suite ; `--tier full` vert.

## Découplage juridique de la suite d'étalons + licences dans les paquets (2026-08-19)

Le seul point 🚨 du `TODO.md` — 79 Mo de contenu sous copyright suivi par un dépôt public —
était bloqué par un **couplage technique** : 12 étalons dépendaient de deux ROM Atari, donc
les retirer « cassait mécaniquement la CI ». Ce couplage est levé, sans perdre un pixel de
couverture. Paliers `fast` et `full` verts avant comme après.

- **4 étalons migrés sur EmuTOS, à 0 px près.** `overscan_top`, `trace_odd`, `scroll_8264`
  et `scroll_8265` tournent sur `etos192fr`/`etos256us` au lieu de `tos102uk`/`tos162us`.
  Leur programme est un **secteur de boot autonome** (il pose lui-même résolution, palette,
  base écran) : le TOS ne fait que le charger. Vérifié plutôt que supposé — capture EmuTOS
  vs capture TOS propriétaire = **0 px / 114816**, références `tests/reference/` **inchangées**,
  et l'**oracle Hatari lui-même est byte-identique entre les deux ROM**.
- **ROM absente : deux cas, plus un seul.** `run_etalons.py` sépare ROM libre et ROM
  propriétaire (`rom_is_free()`) : une `etos*` manquante reste un ÉCHEC (dépôt cassé), une
  `tos*` manquante SAUTE l'étalon et le **recense** dans un bloc dédié. Vérifié bout-en-bout
  en retirant les deux ROM : suite **verte** avec 6 étalons déclarés non exécutés, au lieu de
  8 échecs. Reste alors 7 auto-tests + 5 étalons machine (ST ×2, STE ×3).
- **Ce que la confrontation à l'oracle a révélé au passage** : `scroll_8264`/`scroll_8265`
  sont à **0 px** de Hatari → leurs références sont promues `ref_kind: oracle` (PNG 832×552
  commis, `.ppm` supprimés). Mais `overscan_top` est à **194 px (0,17 %)** et `trace_odd` à
  **22 px (0,02 %)** de l'oracle, **sur les premières lignes de trame** — écarts que leurs
  références en self-capture ne pouvaient pas voir. Indépendants de la ROM (mesurés
  identiques sous les deux). Consignés en 11ᵉ passe de `docs/HATARI_DIVERGENCES.md` et dans
  `etalons.json` ; références laissées en `snapshot`, car ni les promouvoir ni les
  re-baseliner ne serait honnête tant que l'écart n'est pas expliqué.
- **Les paquets partaient sans aucune licence.** `stage_free_data.sh` copie désormais
  `licenses/GPL-3.0.txt` (NeoST), `licenses/GPL-2.0.txt` (EmuTOS) et un `THIRD-PARTY.txt`
  qui nomme chaque composant, sa licence et **l'offre de source** ; idem dans l'APK. Les
  **8 vérifications de paquet** de `release.yml`/`pi-borne.yml` échouent maintenant si une
  licence manque — c'est l'absence de cette garde qui rendait la non-conformité invisible.
- **`NEOST_PACKAGE_NO_ATARI_TOS=1`** construit un paquet 100 % libre (EmuTOS seul), gardes
  CI comprises. Le défaut est INCHANGÉ : les paquets bureau continuent d'embarquer
  `tos102uk`/`tos162uk`. Le `TODO.md` prétendait le contraire (« les paquets bureau étaient
  déjà propres ») — corrigé, la décision revient au mainteneur.
- **L'oracle Hatari n'était nulle part.** `extern/hatari` est gitignoré, n'est pas un
  sous-module, et aucun script ne le rapatrie : sur cette machine il était **absent**, alors
  que `CLAUDE.md` et `HATARI_AUTOMATION.md` le décrivaient comme « bâti dans le dépôt ».
  Cloné, bâti (v2.6.1-devel) et la recette écrite — avec les deux options macOS sans
  lesquelles le build échoue : `-DCMAKE_OSX_ARCHITECTURES=arm64` (sinon édition de liens
  x86_64 contre des bibliothèques arm64) et `-DENABLE_OSX_BUNDLE=0` (sinon `ibtool` part en
  `Abort trap: 6` et make **supprime le binaire déjà lié**).

## Passe de cohérence doc ↔ code, 3ᵉ tour (2026-08-19)

Dernier tour, sur les docs restées hors périmètre (CLOSURE_CHANTIER, MOIRA_WINUAE_CONVERGENCE,
HATARI_AUTOMATION, empaquetage Raspberry) et sur les affirmations chiffrées, qui vieillissent
sans prévenir. Palier `fast` vert.

- **La feuille de route disait encore d'attendre l'oracle.** `TODO.md` gardait une section
  « à reprendre une fois l'oracle Hatari bâti » : l'oracle EST bâti, et **cinq de ses six items
  sont faits** grâce à lui (V1, V2, S2, S3, M1). La section dit maintenant ce qui a été traité et
  ce qui reste — beam-sync par-ligne, résidus V3, D3, `UpdateTimers`, arrondi FPU.
- **Le contrôle négatif mort traînait à un deuxième endroit** : `TODO.md` affirmait aussi
  « Détection prouvée : `NEOST_ALIGN_OFF=1` → exit 1 ». Corrigé comme dans `TEST_SOFTWARE.md`.
- **Chiffres re-mesurés plutôt que recopiés** : le palier `fast` était annoncé à ~0,1 s / ~0,3 s
  dans deux docs — il fait **~3 s** aujourd'hui (il a absorbé le round-trip save-state, le
  contrôle de la disquette livrée et quatre auto-tests de plus). La liste des auto-tests P0 et
  celle des clés relues par `--from-cfg` ont été alignées sur le code au même endroit.
- **Deux sondes d'instrumentation citées comme utilisables n'existent plus** : `NEOST_EXC_DIAG`
  (NeoST) et `NEOST_HAT_IPLDIAG` (Hatari patché). `MOIRA_WINUAE_CONVERGENCE.md` le dit désormais
  en tête — les mesures restent valables, les commandes non. Les 27 autres `NEOST_*` du doc ont
  été vérifiées vivantes une par une.
- **Dernières ancres ré-ancrées** : `updateGlueState` (Shifter, +600 lignes de dérive),
  `recordSyncWrite`, `restartVideoCounter`, `dacTable`, les quatre sites `envFlag` de
  `CLOSURE_CHANTIER.md`, `lineLenOn_`, le latch de bordure gauche dans `TODO.md`. Après quoi le
  balayage automatique ne remonte plus que des faux positifs (symboles Hatari, homonymes).

## Passe de cohérence doc ↔ code, 2ᵉ tour (2026-08-19)

Suite de la passe du jour, sur ce qu'elle avait explicitement laissé de côté : le reste des
ancres `fichier:ligne`, et les docs qui n'avaient pas encore été confrontées au code (KIOSK,
FUJINET, TEST_SOFTWARE, TODO, empaquetage). Palier `fast` vert avant comme après.

- **Cinq divergences CORRIGÉES étaient encore décrites comme des défauts ouverts** dans
  l'inventaire maître — dont une **HAUTE** et deux **ÉLEVÉES**. `HATARI_DIVERGENCES.md`
  annonçait leur correction dans le chapeau de la 3ᵉ passe, mais les entrées détaillées, elles,
  n'avaient jamais été retouchées : SCU jamais réinitialisé (`Scu::reset(bool cold)` existe et
  est appelé), propagation des NaN FPU (l'opérande réel est renvoyé quiété), SNaN→SNAN
  (`flag_signaling` distinct → `EXC_SNAN`), INQUIRY ACSI `buf[4]` (valeur fixe 31), masques
  FPCR/FPSR (`&0xFFF0` / `&0x0FFFFFF8`). Idem plus bas pour **S3** (gain LMC ×2) et **S4**
  (table DAC mesurée), que le tableau des priorités donnait pourtant ✅. Toutes marquées, avec
  le *avant* conservé et l'ancre du correctif. Un inventaire qui liste comme à faire ce qui est
  fait coûte une deuxième fois le travail.
- **Le compteur `$FF8909/0B/0D` « au cycle » était porté depuis le 2026-08-06** (`liveCounter`
  ≙ `DmaSnd_GetFrameCount`) mais restait en « Reste » dans `CYCLE_ACCURACY.md` et en TODO. Seule
  la confrontation à l'oracle de la quantification HBL du refill reste ouverte — c'est ce qui est
  écrit maintenant.
- **Un contrôle négatif qui ne contrôlait plus rien** : `TEST_SOFTWARE.md` donnait
  `NEOST_ALIGN_OFF=1 … --spec512-selftest doit échouer`. La variable a disparu du code —
  vérifié : le test sort **0**. Le vrai garde-fou est la première vérification du test, qui
  ÉPINGLE `kSpec512AlignCyc` à −25 ; c'est elle qui est documentée.
- **Inventaire des étalons refait** : la doc en listait une poignée et disait Union Demo « à
  rapatrier » alors que `etalons.json` en compte **19** (7 auto-tests + 12 étalons machine,
  `union_demo` présent mais `optional`). La liste des auto-tests P0 ignorait `--msa-selftest`,
  `--fuji-selftest`, `--enec-selftest` et `neost-selftest`.
- **`--fuji-selftest` fait 17 vérifications, pas 11** (`EXTENSIONS.md`) — les cas-limites ACSI
  (`count=0` ⇒ 256, plafond MODE SENSE, reset) ont été ajoutés sans que le compte suive.
- **L'APK Android a une interface depuis le 2026-08-11** (menu décalqué de la borne,
  `src/android/AndroidMenu.cpp`) ; `CLAUDE.md` disait encore « pas d'interface ». Ce qui reste
  vrai, et qui est conservé : il n'a jamais tourné sur un appareil réel.
- **Frontends recomptés** : l'en-tête de `CMakeLists.txt` parlait de « deux frontends » et
  `IMPLEMENTED.md` de `neost` + `neost-headless` — il y en a quatre (plus `neost_net` et
  `neost-selftest`). Le sélecteur `--cpu musashi|moira` y était encore annoncé comme un choix,
  dix lignes sous le paragraphe qui explique le retrait de Musashi.
- **Reste des ancres `fichier:ligne` ré-ancrées** (Scc, Shifter, StxImage, GemdosHd, SoftFloatX80,
  MidiAcia, DmaSound, Fpu, Acsi…) : plusieurs pointaient à des centaines de lignes de leur sujet.
- Commentaire du menu borne dans `main.cpp` : « index action 0..4 » pour six actions bouclées
  modulo 6, et une liste d'actions qui s'était arrêtée à trois.

## Passe de cohérence doc ↔ code (2026-08-19)

Relecture croisée de la documentation et du code, sans changement de comportement
d'émulation. Méthode : extraction mécanique de ce que la doc AFFIRME (chemins, cibles,
options, symboles, variables d'environnement, `fichier:ligne`) puis confrontation à
l'arbre. Le palier `fast` est vert avant comme après.

- **La release livre 8 paquets, la doc en annonçait 7.** `release.yml` construit et
  attache l'APK Android depuis le 2026-08-11 (son job `publish` COMPTE 8 et échoue
  sinon), mais `README.md` et `DEV.md` étaient restés à 7 — le tableau public des paquets
  ne mentionnait même pas l'APK. Ligne ajoutée, avec sa réserve (« pas d'interface »).
- **L'arborescence `src/` de `DEV.md` datait d'avant le découpage de `main.cpp`.** Elle
  ignorait `gui/`, `util/`, `net/`, `android/`, `tests/`, et une dizaine de composants
  `io/` (Acsi, Fpu, GemdosHd, Ne2000, Scc, StxImage, MediaScan) — c'est-à-dire la carte
  qu'on donne à lire à quelqu'un qui arrive. Refaite sur les listes de sources réelles,
  `neost_net` incluse.
- **`neost-selftest` n'était listé nulle part** alors que `run_all.py --tier fast` en
  fait son PREMIER pas : la ligne de build de `CLAUDE.md` et le « pas de tests unitaires »
  de `DEV.md` le rendaient invisible.
- **Commentaires de code qui contredisaient le code.** Les cinq signalés par l'audit
  `HATARI_MAPPING` du 2026-07-08 — jamais traités depuis — sont corrigés : « préemption
  DORMANTE » (fausse dans le modèle BLOC, qui est le DÉFAUT ; dormante seulement sous
  `NEOST_SYNC_DISPATCH`), en-tête `Scheduler.hpp` « Phase 1 : 3 sources, quantum ligne »
  (une vingtaine de sources, quantum à l'événement), « le blitter est ABSENT : NeoST ne
  l'émule pas » dans `Bus.cpp` (il l'émule, la plage est dé-fautée selon
  `machineHasBlitter`), l'intro `Mfp.hpp` « strict nécessaire : Timer C » (le MFP est
  quasi 1:1 avec `mfp.c`), et le `regs_[7]=0xFF` de `YM2149.hpp` qui laissait croire
  qu'Hatari fait pareil (micro-écart désormais écrit noir sur blanc). Un commentaire faux
  coûte plus cher qu'une absence de commentaire : il fait chercher au mauvais endroit.
- **Huit options du headless étaient analysées mais absentes du `--help`** — `--mono`,
  `--shot-every`, `--shot-from`, `--keys-at`, `--joy-at`, `--joy-script`, `--mouse-at`,
  `--version` : documentées dans `DEV.md`, utilisées par l'outillage, invisibles pour qui
  lance le binaire. Ajoutées (et le commentaire de `--from-cfg`, qui listait les clés
  relues, remis d'accord avec la boucle qui les lit).
- **Renvois morts.** `disks/utils/` (c'est `disks/etalons/`), `docs/SOUND_HATARI_DIFF.md`
  cité comme existant alors qu'il a été supprimé, `NEOST_ALIGN_OFF` retiré du code,
  `DmaSound::onFrameEnd` et `kioskAreSiblings` qui n'existent pas (`fifoRefill` et
  `neost::areSiblingImages`), `gemdos/BUILD`, un lien Markdown cassé.
- **`fichier:ligne` ré-ancrés** dans `CYCLE_ACCURACY.md` et `HATARI_DIVERGENCES.md` là où
  ils tombaient sur du code sans rapport (Blitter, Bus, Cpu68k, Fdc, Mfp, YM2149,
  Machine). Ces ancres dérivent à chaque édition : `HATARI_DIVERGENCES.md` le dit
  maintenant en tête — **c'est le symbole cité qui fait foi**, pas le numéro.

## Chasse aux bugs : auto-test EtherNEC dépendant du compilateur, `--from-cfg` amnésique (2026-08-17)

Campagne de recherche de défauts. Deux angles : **ASan+UBSan** sur toute la suite
(auto-tests, étalons, **51 jeux du corpus × 900 trames**) et **fuzzing** des parseurs
d'entrée non fiable (4000 mutations de `.stx`, 120 de `.st`/`.msa`/`.dim`). Résultat côté
mémoire : **aucun défaut** — le cœur d'émulation et les parseurs durcis tiennent. Les
trois défauts trouvés sont ailleurs, tous du même genre : une règle écrite **deux fois**,
ou une expression dont le résultat dépend du compilateur.

- **Auto-test EtherNEC : verdict décidé par le compilateur.** La lecture de l'en-tête de
  paquet tenait en `rd(0x10) | (rd(0x10) << 8)` — or les opérandes de `|` ne sont **pas
  séquencés** en C++17 et chaque `rd(0x10)` fait AVANCER le Remote DMA. Les deux octets
  de longueur pouvaient donc être pris à l'envers : l'auto-test lisait `0x2400` au lieu
  de 36. Vert en Release, **ROUGE** sur la même source compilée avec sanitizers — c'est
  ainsi qu'il est sorti. Un gate dont le verdict dépend du choix du compilateur ne garde
  rien : il serait passé rouge sur le premier portage ou la première montée de toolchain.
  Une lecture par instruction.
- **`--from-cfg` perdait le lecteur B, le modem et l'EtherNEC.** Le headless a son PROPRE
  lecteur de `neost.cfg` (dette déjà notée le même jour, ci-dessous) et il ignorait
  `diskb=`, `modem=` et `ethernec=` — trois clés que le GUI écrit, les deux dernières
  juste à côté des `fujinet_*` toutes relues, elles. Rejouer une config dans le headless
  démarrait donc **sans** disquette B ni réseau, sans un mot, alors que `--diskb`,
  `--modem` et `--ethernec` existent et fonctionnent.
- **Le rognage de ligne avait divergé.** Le lecteur GUI retire `\r` **et** les blancs de
  queue (correctif CRLF de la 0.5.2) ; la recopie côté headless ne retirait que le `\r`.
  Un `machine=st ` suivi d'une espace y retombait donc en silence sur la machine par
  défaut — exactement le défaut que le rognage GUI corrige, resté vivant dans le second
  lecteur. La règle devient `appconfig::trimConfigLine`, **une** définition pour deux
  appelants, verrouillée par 8 cas dans `neost-selftest` (**84 → 92**).

Reste, inchangé : le headless garde son propre *aiguillage* de clés (il résout en plus
les chemins relatifs au dossier du cfg). Seule la règle de rognage est désormais partagée.

## Chemins hôte unifiés, logique pure testable, `main.cpp` dégrossi (2026-08-17)

Chantier d'architecture né du constat des issues #37/#38 : la discipline du projet
s'arrête à la frontière de l'émulation. Tout ce qui est *autour* — couche hôte, données
livrées, frontend — n'avait ni module, ni test, ni gate.

- **`src/util/HostPath` : UNE définition de « chemin absolu ».** Il y en avait quatre,
  toutes écrites à la main sur la règle Unix « ça commence par `/` » : `GemdosHd`
  (corrigé la veille), les deux résolveurs de `main.cpp`, et le lambda `resolve` de
  `--from-cfg` côté headless — où le défaut de #37 était **toujours vivant**, clé
  `gemdos=` comprise. Le style de chemin (`Posix` / `Windows`) est un **paramètre
  d'exécution**, pas un `#ifdef` : c'est ce qui rend la sémantique Windows exerçable
  depuis un Mac ou une CI Linux. Personne ne pouvait le faire, donc personne ne l'a fait,
  et le lecteur C: est resté mort sur tous les paquets Windows depuis la 0.5.1.
- **Nouvelle cible `neost-selftest`** (`tests/selftest_logic.cpp`, **84 cas**) pour la
  logique qui n'a besoin ni de machine ni de ROM, et que le headless ne peut donc pas
  couvrir. Câblée au palier `fast` — un test qu'aucun script n'exécute pourrit.
- **`main.cpp` : 4981 → ~4250 lignes**, en trois unités qui n'avaient rien à faire là.
  `gui/AppConfig` (structure `Config`, analyse, écriture atomique, profils) entre dans
  `neost_core` : sans dépendance GUI, il devient **testable**, d'où les nouveaux cas
  d'aller-retour `parseConfigLine` ↔ `writeConfigKeys` (une clé écrite mais jamais relue
  est un réglage muet, invisible au démarrage), de bornage des valeurs hostiles et de
  neutralisation des noms de profils. `gui/UiCommon` (pictogrammes + `IconButton`) et
  `gui/MediaPages` (pages Disquettes / Cartouche / Disque dur / Réseau) sortent côté GUI ;
  elles s'y prêtaient sans réécriture, ayant déjà la discipline « une page ne fait rien,
  elle pose des requêtes ».

Reste identifié, non fait : le headless garde **son propre parseur** de `neost.cfg`
(`--from-cfg`), deuxième lecture du même format — à faire converger sur `gui/AppConfig`.

## La CI teste enfin le PAQUET, pas seulement son binaire (2026-08-17)

Les deux seuls défauts jamais signalés sur un paquet publié (issues #37 et #38, la veille)
avaient **la même racine** : le smoke-launch de la release amorçait le binaire — `--version`,
500 trames d'EmuTOS, capture non uniforme — et s'arrêtait là. Il n'ouvrait ni la **disquette
livrée** ni le **disque dur GEMDOS**, c'est-à-dire précisément les deux données que le paquet
embarque. Un lecteur C: mort sur tout Windows et une `diskA.st` sans système de fichiers ont
donc traversé plusieurs releases sans qu'une seule ligne rouge apparaisse.

Nouveau `tools/smoke_package.sh`, appelé par les **6 jobs** qui exécutent le paquet
(`linux-bionic`, `linux-arm64`, `raspberry`, `pi400`, `macos`, `windows`) — les 15 lignes
dupliquées dans chacun disparaissent au passage. Quatre phases :

1. version annoncée = version du paquet (contrôle existant) ;
2. boot EmuTOS 500 trames + capture non uniforme (existant, via
   `tools/check_ppm_nonuniform.py`) ;
3. **disquette livrée** : structure FAT12 de la copie DU PAQUET
   (`check_disk_assets.py --image`, nouvelle option) **et** montage réel par l'émulateur
   du paquet (le FDC doit journaliser la géométrie) ;
4. **HD GEMDOS sur chemin ABSOLU au format natif de l'hôte** (`cygpath -w` sous MSYS2) :
   le montage doit aboutir en C: **et** aucun accès ne doit être refusé par le bac à sable.
   C'est exactement la forme `D:\a\…` qui échouait sous Windows.

Vérifié sur les deux régressions : avec l'ancienne `diskA.st` écrasée le smoke sort en
échec à la phase 3, avec un montage GEMDOS qui rate il sort en échec à la phase 4.


## Retours 0.5.2 : HD GEMDOS sous Windows, disquette livrée (2026-08-16)

Deux défauts signalés par Christian Zietz (EmuTOS) sur la **0.5.2 Windows** —
issues [#37](https://github.com/habib256/neost/issues/37) et
[#38](https://github.com/habib256/neost/issues/38).

- **HD GEMDOS : tout chemin Windows était traité comme RELATIF.** `makeAbsoluteName`
  (port de `File_MakeAbsoluteName`) ne connaissait que la règle Unix « absolu = commence
  par `/` ». Sous Windows un chemin absolu commence par une **lettre de lecteur**
  (`C:\Temp\atari`) ou une racine **UNC** (`\\serveur\partage`) : le dossier glissé sur
  la fenêtre était donc préfixé du répertoire courant → `C:\…\NeoST-0.5.2\C:\Temp\atari`,
  « GEMDOS folder not found » sur un dossier pourtant valide. Ajout de
  `isAbsoluteHostPath` (lettre de lecteur + UNC) et normalisation `\` → `/` des chemins
  hôte à l'entrée (`makeAbsoluteName`, et le retour de `getcwd`, lui aussi en `\`), le
  reste du fichier comparant déjà en `/` (comme `physicalCanon`). `cleanFileName` ne
  rabote plus « `C:/` » en « `C:` » (racine du lecteur ≠ dossier courant du lecteur).
  Aucun effet sur Unix/macOS (montages absolus, relatifs et `../` revérifiés).
- **`disks/diskA.st` livrée n'était plus une disquette.** Écrasée par un test d'écriture
  secteur (commit `828bc87`, juin 2026), elle partait dans **tous** les paquets avec un
  motif binaire à la place du système de fichiers : BPB absurde, aucun secteur de boot,
  inutilisable sous TOS. Regénérée par `tools/make_floppy.py` (FAT12 720 Ko, 9x2,
  `LISEZMOI.TXT` + `NEOST.TXT` + dossier `PROGS`), avec un **numéro de série** non nul
  (détection de changement de disquette par le TOS). Vérifiée de bout en bout : sur une
  copie de travail munie d'un `EMUDESK.INF` qui ouvre `A:\*.*` au boot, le bureau EmuTOS
  liste bien le contenu (« 497 bytes used in 4 items »).
  Nouveau gate `tools/check_disk_assets.py` au palier **fast** (BPB + chaînes FAT +
  égalité bit à bit avec le générateur) : l'image ne peut plus être écrasée en silence.

## Capture souris sans bouton central (2026-08-15)

Le clic molette reste la bascule principale, avec **Ctrl+Alt+G** comme raccourci de
secours dans les frontends bureau et Web pour les trackpads et souris à deux boutons.
Seul le chord **Ctrl+Alt+G** est réservé à l'hôte ; `G` seul reste transmis normalement
à l'IKBD.

## Super Hang-On : phase MFP sans dérive (2026-08-14)

Les timers MFP A/B/C/D conservent désormais leur échéance en unités de **1/256 de
cycle CPU**, comme `CYCINT_SHIFT` dans Hatari. Auparavant chaque recharge tronquait
séparément la conversion MFP→CPU (par exemple 40106 au lieu de 40106,238 cycles pour
le Timer C 200 Hz) : la phase dérivait progressivement par rapport au faisceau et aux
écritures YM, terrain commun aux rares lignes raster transitoires et aux événements
musicaux manqués de *Super Hang-On*. L'auto-test MFP couvre l'accumulation sur 25
périodes. La phase fractionnaire est sérialisée ; save-state **v11**.

Deux défauts de présentation amplifiaient les symptômes : le bureau désactivait la
**VSync**, ce qui pouvait couper une image à une ligne aléatoire (tearing hôte), et un
seul underrun audio imposait ensuite **85 ms de silence** avant de reprendre. La VSync
est réactivée avec la boucle de rattrapage à échéance absolue (elle maintient le temps
émulé même si un swap bloque), et la ré-amorce après underrun descend à environ 20 ms,
au moins la taille d'un bloc CoreAudio. Le premier amorçage conserve la latence choisie.

## Réseau : FujiNet + modem Hayes + EtherNEC + anneau MIDI (2026-08-12)

Première ouverture de la machine émulée sur le réseau — **quatre extensions NeoST**,
toutes **OFF par défaut**, sans équivalent Hatari (`docs/HATARI_DIVERGENCES.md` §
Extensions), **sans effet sur les étalons** (réseau jamais ouvert par `run_all.py`).
Réf. complète : [`docs/EXTENSIONS.md`](docs/EXTENSIONS.md). Principe : `neost_core` reste sans
socket ni thread ; une nouvelle lib **`neost_net`** (frontends) fait l'I/O (option CMake
`NEOST_WITH_NET`, forcée OFF sur WASM/Android).

- **FujiNet virtuel** sur le bus ACSI (opcode vendeur $60, devices Fuji $70 + N1:-N8:).
  Déport de protocole HTTP/TCP/JSON et **montage d'images distantes** (démarrer une
  disquette HTTP sans un octet de code ST — validé **0 px** vs montage local). Backends
  live (sockets) / rejeu (déterministe) / hors ligne. Lib 68000 + `NWGET.TOS`
  (`dev/fujinet/`). Panneau GUI **Network**, clés `neost.cfg`. Save-state **v10**.
- **Modem Hayes** RS-232 (`--modem`) → pont TCP réel pour STiK/STinG, terminaux, BBS ;
  a nécessité `Mfp::receiveByte` (injection RX cadencée, `Scheduler::SERIAL_RX`).
- **EtherNEC** : NE2000 émulée sur le port cartouche (`--ethernec`), pour les pilotes
  STinG/MiNTnet/MagiCNet historiques ; exclusive d'une cartouche montée.
- **Anneau MIDI réseau** (`--midi-net`) : MIDIMaze jouable en ligne.

Auto-tests fil déterministes ajoutés au palier `fast` : `--fuji-selftest` (11/11),
`--enec-selftest` (5/5). Tier **full vert** (pixels inchangés). Save-state v10 :
`FujiDevice` + `Ne2000` + file RX MFP sérialisés, flags d'en-tête GEMDOS/FujiNet/EtherNEC.

## 0.5.2 — la 0.5.1, mais réellement livrée (2026-08-10)

**Aucun changement fonctionnel par rapport à la 0.5.1** : mêmes 7 paquets, même cœur
d'émulation. La 0.5.1 n'a jamais reçu ses binaires — son job `wasm` échouait, donc
`publish` (qui attend les 7 paquets) restait `skipped` et le tag pointait sur une
Release vide. Cette version reprend le même contenu sur une CI verte.

Ce qui a été corrigé pour y arriver est décrit au chantier du 2026-08-10 ci-dessous :
bundle WebAssembly reconstruit (il servait encore l'ancien défaut Mega STE + TOS Atari)
et empreinte de fraîcheur rendue reproductible d'une machine à l'autre.

## 0.5.1 — Windows, et le vrai paquet Pi 400 (2026-08-10)

Deux paquets s'ajoutent aux cinq de la 0.5 ; le cœur d'émulation est inchangé.

**Windows x64.** `NeoST-<ver>-windows-x86_64.zip` : on déballe, on lance `neost.exe`,
il n'y a rien à installer. Bâti en **MinGW-w64** (MSYS2/MINGW64) et non MSVC — le code
est écrit pour GCC/Clang et Moira exige C++20, donc la chaîne se réutilise telle quelle
au lieu de porter les options et les dépendances vers vcpkg. **Tout est lié en
statique** (libgcc, libstdc++, winpthread, GLFW) : `packaging/windows/build_mingw.sh`
inspecte les imports du binaire et REFUSE le paquet s'il dépend d'une DLL non système,
parce qu'une DLL manquante est invisible en CI (MSYS2 les a dans son `PATH`) et fatale
chez l'utilisateur. Le paquet n'est **pas signé** : SmartScreen prévient au premier
lancement. Le job CI tourne sur `windows-latest` et **exécute réellement** le binaire
produit (`--version`, boot EmuTOS 500 trames, capture non uniforme).

Portage nécessaire, quatre points — aucun changement de comportement sur POSIX :
`main.cpp` résout le dossier de l'exécutable par `GetModuleFileNameW` (la variante W :
un chemin accentué ressort en mojibake avec la version ANSI, et plus rien n'est trouvé) ;
`Acsi.cpp` et `Fdc.cpp` abandonnent `<sys/stat.h>` pour `std::filesystem` (la
write-protection reste la permission d'écriture du propriétaire, ce que Windows dérive
de son attribut « lecture seule » — même sémantique qu'Hatari) ; `GemdosHd.cpp` remplace
`realpath()` par `std::filesystem::canonical` et `statvfs()` par `GetDiskFreeSpaceExW`.
⚠ La canonicalisation Windows renormalise les séparateurs en `/` : tout le fichier
compare avec `PATHSEP`, et un retour en `\` aurait fait échouer le test de préfixe du
bac à sable GEMDOS — donc rabattu chaque accès sur la racine, en silence.

**Paquet Pi 4 / Pi 400.** `NeoST-<ver>-pi400-aarch64.AppImage`, compilé
`-mcpu=cortex-a72` (~10-20 % sur Moira). Il existait déjà, mais seulement dans
`pi-borne.yml`, un workflow manuel dont les artefacts n'étaient attachés à aucune
Release. `raspberry-aarch64` reste ce qu'il a toujours été et ce qu'il doit être :
aarch64 **générique**, du Pi 3 au Pi 5. En cas de doute, c'est le générique.

La release passe donc de 5 à **7 paquets** ; le job `publish` compte 7 et échoue sinon.

## 0.5 « Newborn » — première release taguée (2026-08-10)

Premier tag du projet. « Newborn » parce que c'est la première fois que NeoST sort du
dépôt sous forme de paquets : la machine est complète et se tient debout toute seule,
mais elle vient de naître. Ce qui suit récapitule l'état livré ; le détail par
sous-système est dans les sections datées ci-dessous.

**Interface et journaux en anglais.** Toute l'UI (barre de menus, fenêtre
`Configuration` et ses onze pages, débogueur, joystick, effets CRT, mode borne, barre
d'état, infobulles, messages transitoires) et **tous les messages de journal** —
`neost-headless --help` inclus — sont désormais en anglais. Les commentaires du code et
la documentation restent en français. Les étiquettes de journal (`[FDC]`, `[headless]`,
`[Bus]`…), les noms d'options et les champs de trace sont inchangés : rien de ce qui
analyse cette sortie ne bouge. Unités normalisées `Ko/Mo` → `KB/MB`.

**Le matériel émulé.** Quatre profils — ST, Mega ST, STE, Mega STE — de 256 Ko à 4 Mo de
ST-RAM, avec le matériel optionnel présent ou absent selon le modèle (68000 8/16 MHz +
cache et socket **FPU MC68881** sur Mega STE). Cœur 68000 **Moira** cycle-exact.
Shifter + Glue (bordures, overscan, tricks de résolution, **Spectrum 512**, scroll fin
STE), MFP 68901, ACIA 6850 + IKBD, FDC WD1772 (`.st`/`.msa`/`.dim` inscriptibles, `.stx`
Pasti), DMA/ACSI, blitter, YM2149 + son DMA STE + Microwire/LMC1992, RTC.

**Ce qu'on peut en faire.** Disque dur **GEMDOS** (dossier hôte monté en C:) et image
**ACSI**, **save-states** complets (v9, empreinte de config vérifiée), **débogueur**
(breakpoints, watchpoints, symboles, pas-à-pas instruction, désassemblage, hexa,
registres), **mode borne** plein écran pilotable à la manette, **effets CRT** opt-in, et
un `neost-headless` déterministe (traces façon MAME, captures PPM, dumps RAM/audio/série)
qui sert d'outil de diagnostic et de banc de non-régression.

**Validation.** `tools/run_all.py --tier full` passe intégralement : auto-tests logique
(glue 36, spec512 15, bus 12, MFP 16, MSA 51 — 0 échec), verdicts série de la cartouche
de diagnostic, cycle-bench, provenance des références, et les étalons pixel comparés à
**0 pixel d'écart** (dont `nocooper` / `nocooper_greetings` en overscan med-res et les
diaporamas Spectrum 512, référencés sur l'**oracle Hatari**).

Deux étalons ne gardent rien, ce qui ne dit RIEN du logiciel qu'ils visent :
`union_demo` est ignoré faute de disque optionnel, et `cuddly_demos` est désactivé faute
de **référence** — il n'y a aucune image dans `tests/reference/` pour la trame 3400
(animation centrale), donc rien à comparer. **The Cuddly Demos tourne bien** en
512 Ko ST + TOS 1.02 UK (`--machine st --mem 512k roms/tos102uk.img`) : écran-titre
complet et conforme. C'est l'outillage de non-régression qui manque, pas l'émulation.

**Paquets.** Cinq artefacts à la 0.5 (sept depuis la 0.5.1), avec leurs sommes SHA-256 : AppImage Linux
x86_64 (plancher glibc 2.27), AppImage Linux aarch64, **deux** paquets Raspberry —
`raspberry-aarch64` GÉNÉRIQUE (Pi 3 → Pi 5, aucun `-mcpu`, PGO+LTO) et `pi400-aarch64`
taillé pour le Cortex-A72 du Pi 4/400 (~10-20 % sur Moira, mais il ne démarre pas sur un
cœur plus ancien) —, `.dmg` macOS Universal 2, bundle WebAssembly. Pas de paquet Windows :
les cibles sont macOS Silicon et Linux. Contenu embarqué : liste explicite tenue
par `packaging/stage_free_data.sh` (EmuTOS, `tos102uk`/`tos162uk` des profils 520 ST /
1040 STE, `diskA.st`, polices, échantillons de lecteur) — un garde-fou refuse toute
autre ROM. ⚠ `tos102uk`/`tos162uk` sont des **ROM Atari sous copyright** : leur
redistribution est un choix assumé du projet, pas une donnée libre.

## Closure : l'image rejoint l'oracle — scroll hardware STF 4 px / stab med (2026-08-12)

Suite (et fin du volet visuel) du chantier Closure : après le crash au boot
(chantier précédent), la démo tournait mais **hachée** — logo en damier à
marches, cartons texte et photo aux couleurs striées. L'enquête (dossier
[`docs/CLOSURE_CHANTIER.md`](docs/CLOSURE_CHANTIER.md) § Cycle 6) a exonéré une
à une toutes les données (bitmap RAM, adresses par ligne, écritures palette —
11 306/11 314 identiques à l'oracle à une constante près) grâce à deux outils
neufs : un **oracle instrumenté** (`[LADDR]` dans le Hatari du dépôt : adresse
du raster à chaque ligne, deltas invariants à l'ancrage) et le **re-rendu
modèle en boucle fermée** — un pipeline Python nourri des données d'Hatari
lui-même, qui produisait le damier là où l'image d'Hatari est lisse : preuve
que l'ingrédient manquant du modèle était aussi celui du renderer.

Cet ingrédient : **le scroll hardware 4 px du STF** (`video.c:3946-3990`, qui
cite nommément « 'Closure' demo Troed/Sync »). Le retrait de bordure gauche
par bascule hi→med→lo déplace chaque ligne de 1 à 13 px selon le cycle de la
bascule retour — le « X-DISTING » de la démo — et le matériel réalise ce
déplacement en **désalignant les plans** : l'offset d'origine en octets fait
charger les bitplanes dans les mauvais registres. Port dans `renderGlueFrame` :
table par ligne (offset source, shift effectif) relative au repère calibré —
13→(+4,+5), 9→(+2,+1), 5→(0,−3), 1→(−2,−7), stab 0→(−2,−8) — l'offset
s'applique en octets à la source (la permutation de plans émerge du décodage,
comme le chemin med l'avait établi).

Deuxième pièce : **kSnapLead** — 8 octets de garde en tête des captures
par-ligne (`lineSnap_`), pour que les offsets sources négatifs restent dans le
slot. Le premier essai (repli RAM) tuait le logo d'intro SYNC : cet écran
single-buffer dessine et efface son logo en course avec le faisceau, la RAM de
fin de trame est déjà vide — précisément l'artefact que la capture au faisceau
prévient.

Résultat : logo d'intro net, grand logo lisse (les 5 discontinuités restantes
sont mesurées à l'identique sur l'oracle : bords de lettres, résidu NUL),
cartons texte et photo impeccables — **parité visuelle**. Leçons de méthode au
dossier, dont une majeure : **Hatari est non-déterministe run-à-run sur cette
démo** (ancrage de boot) — les canaux d'animation ne se comparent JAMAIS par
VBL absolu, seulement par invariants (deltas par ligne, séries de motifs,
cohérence interne). Diag `NEOST_WATCH=hex` ajouté (watch d'écriture bus daté,
coût nul désarmé). Validation : `--tier full` 39/39 vert, menu Cuddly,
Enchanted Land, Super Hang-On et Lethal Xcess pixel-identiques. ⚠ Closure se
joue en ST + tos102uk + 1 Mo (512 Ko : refus fidèle ; chemin STE non porté —
FIXME assumé chez Hatari aussi).

## Closure (Sync) boote : datation des écritures freq/res par parité d'accès (2026-08-12)

La démo **Closure** de Sync (`disks/etalons/closure.msa`, ST 1 Mo + TOS 1.02 UK)
crashait au boot (opcode illégal `$19C0` auto-généré) là où Hatari l'exécute. Un
travail d'oracle en cinq cycles (dossier complet :
[`docs/CLOSURE_CHANTIER.md`](docs/CLOSURE_CHANTIER.md)) a remonté la chaîne causale
au cycle près : la démo classe le *wakestate* de la machine en mesurant le compteur
vidéo à travers des bascules 60/50 Hz beam-racées, et NeoST datait l'écriture du
retour 50 Hz de la ligne 64 à 56 (> `Line_Set_Pal` 55, Freq_match refusé) là où
Hatari mesure 54 — ligne restée à 508 cycles, grille −4, right-off de la ligne 65
manqué, 44 octets perdus, delta `$A2` au lieu de `$CE`, verdict 0, file d'épreuves
corrompue, crash.

**Le correctif** (`Shifter::recordSyncWrite`) : la datation `fcRaw + 2` constante
devient une datation par **parité de la position de l'accès dans l'instruction** —
`+2` quand `cyclesIntoInstr() ≡ 2 (mod 4)` (la classe historique `move Dn,(An)`/abs,
tout le parc calibré inchangé), `+0` quand `≡ 0` (la classe `move An,(An)` du
classificateur de Closure, que Moira place 2 cycles après WinUAE). C'est la
transposition de la loi Hatari CE (`Cycles_GetInternalCycleOnWriteAccess` :
position de l'accès + 4). ⚠ Un premier essai « début d'instruction + 4 » uniforme
cassait nocooper de 19 361 px (les `move` vers abs.w exigent start+8) — la parité
réconcilie tout. `NEOST_SYNC_MODE=0` restaure l'ancienne datation pour l'A/B.
Validation : `--tier full` 39/39 vert (nocooper oracle et les 3 diapos spec512
compris), menu Cuddly trame 3400 pixel-identique, A/B pixel-identique sur
Enchanted Land, Super Hang-On et Lethal Xcess (2600 trames chacun).

S'ajoutent, issus de la même enquête (fidélité Hatari, étalons verts à chaque pas) :

- **Timer B positionné par ligne réelle** (`Machine::onTimerB`,
  `Shifter::timerBPosForLine`/`timerBFrameCycleForLine`) : la position du tir suit
  le DE réel de la ligne (Glue live, `(DE_start|DE_end) + 24` comme
  `Video_TimerB_GetPosFromDE`) au lieu d'une position fixe ; re-check du callback
  sur l'ÉCHÉANCE planifiée (`tbScheduledAt_`, robuste au service quantifié par
  STOP). Mesuré : 580/763 tirs à la cible Glue sur le balayage per-line de Closure.
- **MFP fidèle au reset** (`Mfp.cpp`) : GPIP initialisé à `0x00` (Hatari
  `mfp.c:523`) et bits 6 (RI) / 3 (GPU idle) au repos BAS dans `gpipInput()` —
  la table d'identité machine de Closure (`$2E22F`) converge à l'octet près.
- **Diags d'enquête** (zéro coût hors env) : `NEOST_COL_DIAG` (datation des
  écritures palette, appariable au `--trace video_color` d'Hatari),
  `NEOST_NO_SNAP` (neutralise la capture par-ligne), `[GLUP]`/`[VC]`/`[render]`
  enrichis, `NEOST_WRITE_DIAG`/`NEOST_TB_TRACE`.

Reste ouvert (consigné au dossier § Cycle 5) : le logo animé de l'effet 2 est
haché chez NeoST (interférence bitmap×palette : le remplisseur de listes de
couleurs de la démo publie sa vague plus tard dans la trame que chez Hatari —
chantier d'ordonnancement CPU intra-trame, suspects Timer B fallback / latences
IRQ / e-clock, toutes les mesures archivées).

## CI verte : bundle web reconstruit, et une empreinte qui ne dépend plus de la machine (2026-08-10)

Le job `wasm` de `release.yml` bloquait la 0.5.1 : sa garde de fraîcheur refusait le
dossier `wasm/` commité, et le job `publish` (qui attend les 7 paquets) restait donc
`skipped` — **aucune Release n'était attachée au tag**.

**La garde avait raison.** Le commit « démo par défaut en ST 1 Mo / EmuTOS » avait changé
`src/web/main_web.cpp` sans reconstruire le bundle : `wasm/index.wasm` contenait encore
l'ancien défaut Mega STE + TOS Atari. Le bundle est reconstruit
(`-DNEOST_WEB_FREE_ONLY=ON`, 9,0 Mo au total) et vérifié dans un Chromium headless — la
démo boote bien sur `Atari ST · 1 MB` / `etos192us.img`, disquette A montée, sans erreur
de page.

**Mais l'empreinte, elle, avait tort aussi.** Elle ne retombait sur la même valeur qu'à
machine identique, ce qui aurait fait rougir la CI *sans* qu'aucune source bouge :
`find | sort` classe selon la **locale** (le poste macOS en `en_US.UTF-8`, le runner en
`C`, et un `/` ou un `_` suffit à départager deux chemins dans l'ordre inverse), `find`
compte les fichiers **non suivis** traînant dans `src/`, et `sha256sum` n'existe pas sur
un macOS sans coreutils. `tools/wasm_stamp.sh` liste désormais par `git ls-files` (tri
par octets, fichiers suivis seulement), recompose lui-même chaque ligne
« empreinte + chemin » et accepte `sha256sum`, `shasum` ou `openssl` — vérifié
identique entre les trois.

**Et l'artefact part avant la garde** (`if: always()`) : quand elle se déclenche, le zip
que le job vient de construire EST le bundle à recommiter, donc on le récupère depuis la
CI sans installer emsdk. Marche à suivre dans `DEV.md` § *Builds spécialisés*, qui
pointait encore le `deploy-web.yml` supprimé.

## CI : 8ᵉ paquet — l'APK Android entre dans release.yml (2026-08-11)

Nouveau job `android` dans `release.yml`, sur le modèle des sept autres : chaque
push/PR vérifie que l'APK se construit, un tag l'attache à la Release
(`NeoST-<ver>-android-arm64-debug.apk` — signé clé de DEBUG, installable tel quel ;
le projet n'a pas de clé de store et n'en aura pas dans le dépôt). Le job `publish`
compte désormais **8** paquets et ramasse aussi les `.apk`.

Le job rejoue exactement la recette locale : JDK 17 **complet** via setup-java (un
JRE n'a pas `jlink`, dont AGP a besoin — piège documenté), composants SDK épinglés
aux versions du README (NDK 27, CMake 3.22, API 34) sur le SDK préinstallé du
runner, `fetch_sdl.sh`, `build_apk.sh debug` (garde-fous bibliothèques/assets
intégrés), puis vérification STATIQUE du manifeste (`aapt dump badging` : paquet,
activité lançable, arm64-v8a) — pas d'appareil ni de KVM sur les runners ; le cœur
arm64 est validé par ailleurs (bit-exact sous qemu, cf. packaging/android/README.md).

Avant de pousser, le chemin CI a été **répété dans un clone frais** du dépôt
(checkout vierge → sous-module imgui → fetch_sdl → build_apk) : c'est ce test qui
prouve que le dépôt commité contient tout — le poste de dev, lui, avait déjà tout
sous la main.

Au passage, le job `wasm` du push précédent était tombé au rouge sur sa garde de
fraîcheur — à raison : `SOURCE_STAMP` avait été écrit AVANT le `git add` complet,
et l'empreinte (assise sur `git ls-files src/**`) a changé quand les nouveaux
fichiers Android sont entrés dans l'index. Le bundle, lui, était bon : reconstruit
pour vérifier, il ressort **identique au bit près** au commité (même md5) — emcc
est reproductible à version fixe sur la même machine. Empreinte réécrite ; leçon :
`--write` toujours APRÈS le stage complet.

## Plein écran WASM : zoom adaptatif (2026-08-11)

Le plein écran de la démo web montrait le CADRE COMPLET, bordures comprises — l'image
utile flottait, petite, au milieu des bandes. Il applique désormais le **zoom
adaptatif** du mode borne : cadré sur la ZONE ACTIVE (rectangle matériel, jamais au
pixel → zéro saccade), et **buffer entier dès qu'une démo ouvre les bordures**
(hystérésis ~0,6 s). En fenêtré, rien ne change : le « moniteur » de la page montre
les bordures, c'est son charme.

- **Calcul partagé, pas recopié** : `stContentRegion` (latches d'hystérésis compris)
  quitte `main.cpp` pour `core/Framing.cpp` — bureau, kiosk et WASM appellent la même
  fonction. C'était la prochaine copie divergente en puissance, après `AudioMix` (son)
  et `MediaScan` (ludothèque) cette même semaine.
- Le shell signale `fullscreenchange` au cœur (`neost_set_fullscreen`) — l'écouteur
  couvre aussi la sortie par Échap, que le bouton ne voit pas.
- **Piège mesuré, pas supposé** : en plein écran, le port GLFW d'Emscripten
  redimensionne LUI-MÊME le canvas à la taille de l'écran (640×400 demandés, 800×600
  imposés) — compter sur la taille intrinsèque du canvas pour le ratio aurait ÉTIRÉ
  l'image. Le letterbox est donc fait au viewport GL (recette de `drawStKiosk` et du
  frontend Android), et le canvas est rendu à Emscripten pendant le plein écran ;
  `syncCanvasSize` compare à la taille RÉELLE du canvas pour reposer le ratio fenêtré
  à la sortie.

Vérifié dans Chrome headless (Puppeteer, clic de confiance sur le vrai bouton) :
l'image plein écran mesure un ratio de **1,605** (zone active = 1,600 attendu ; le
cadre complet ferait 1,507, l'étirement écran 1,333), et la sortie de plein écran
restaure le canvas 832×552. Le repli overscan n'a pas été rejoué en navigateur : sa
logique est le code du bureau, déplacé tel quel.

## Paquet Android : premier APK qui tourne (2026-08-11)

Quatrième plateforme. `packaging/android/build_apk.sh` produit un **APK arm64-v8a**
(Android 5.0+) : la machine démarre sur EmuTOS, l'image et le son sont là, la souris se
pilote au doigt et une manette physique tient le port joystick 1.

**Le portage n'est pas celui du GUI, c'est celui du WEB** — et ce n'est pas un choix
esthétique : `src/main.cpp` rend en OpenGL mode immédiat (`glBegin`/`GL_QUADS`) et
pilote son interface avec `imgui_impl_opengl2`, deux choses qui n'existent pas sur
Android. Le frontend web, lui, rendait déjà en **GLES 2** et produisait son son au
modèle « push ». `src/android/main_android.cpp` en est la transposition : même shader,
même chaîne audio partagée (`core/AudioMix.cpp`, extraite la veille), même cadence sur
le **temps émulé** — un tour de boucle exécute 0, 1 ou 2 trames selon ce que le temps
réel réclame, jamais « une trame par image écran ».

Le cœur est repris **tel quel** : aucune ligne de `neost_core` n'a bougé. C'est le
dividende du découplage « le cœur ne dépend pas du GUI ».

- **SDL2** fournit ce que GLFW ne sait pas faire ici (fenêtre, contexte GLES, cycle de
  vie, tactile, manettes, audio). Vendorisé **non commité** dans `extern/SDL2` comme
  Hatari — `packaging/android/fetch_sdl.sh` le récupère.
- **Branche `if(ANDROID)` du CMakeLists racine**, sur le modèle exact de `if(EMSCRIPTEN)` :
  on ne construit que le cœur et le frontend de la plateforme. Gradle appelle ce
  CMakeLists — pas de définition dupliquée de `neost_core`.
- **Données embarquées : EmuTOS + `diskA.st` seulement** (~1 Mo), avec un garde-fou qui
  refuse tout autre fichier. Aucun TOS Atari, aucun jeu : le Play Store est plus strict
  que nos paquets de bureau. Déballage dans le stockage interne au 1er lancement.
- **Entrées v1** : glissé = souris relative (le bureau GEM se pilote ainsi), appui bref
  = clic gauche, deux doigts = clic droit, manette SDL → port 1.

**Validation sans appareil.** Ni `/dev/kvm` ni téléphone ici, donc l'APK est vérifié
statiquement (bibliothèques natives, assets, manifeste, classes SDL dans le dex,
`SDL_main` exporté) — et le **cœur** est validé sur l'architecture cible autrement :
compilé pour ARM64 Linux et lancé sous `qemu-aarch64`, il rend une image et un son
**identiques au bit près** au x86-64 (`cmp` sur PPM et WAV). Perf : 1000 trames ST en
7,1 s sous QEMU, qui coûte lui-même un facteur 5 à 10.

Trois pièges consignés dans `packaging/android/README.md` : `jlink` absent des JRE
headless (le plugin Android en a besoin, et le message ne le dit pas), Gradle 8.1 du
gabarit SDL2 qui refuse le JDK 21 (d'où Gradle 8.9 + AGP 8.5.2), et
`-DNEOST_ANDROID_APP=OFF` pour bâtir le cœur seul sans SDL.

**Interface : le menu borne, décalqué (même jour).** Plutôt qu'inventer une interface
mobile, on reprend la grammaire du **menu borne** — elle a été pensée pour être lue à
distance et pilotée sans clavier, ce qui est exactement la contrainte d'un téléphone :
voile sombre et machine EN PAUSE, ludothèque en rangées énormes avec le disque inséré
en vert et les **suites** du jeu en cours teintées et remontées en tête, **insérer ne
redémarre pas** (seul `RESTART` relance), et une page **clavier** ancrée en bas où la
machine continue de TOURNER — c'est ce qui permet de répondre à un « PRESS SPACE ».

Le tri de la ludothèque n'est pas recopié : `kioskScanDisks` est extrait en
**`io/MediaScan`**, partagé par la borne et Android (scan borné, détection des suites
par préfixe/suffixe communs, ordre de proximité). Vérifié : monté sur un *Blood Money*,
l'autre version du même jeu ressort en 2ᵉ position.

Deux écarts assumés avec la borne, dictés par le support : les rangées sont **tapables**
(pas seulement navigables au curseur), et les actions passent sur une **rangée
horizontale** — empilées comme sur un téléviseur, elles ne laissaient que deux jeux
visibles sur un écran de téléphone en paysage.

**`neost-menu-preview`** : le menu ne dépendant que d'ImGui et de `io/MediaScan`, une
cible de bureau (`EXCLUDE_FROM_ALL`) le dessine dans une fenêtre au format d'un
téléphone. C'est elle qui a rattrapé l'erreur du premier jet — des tailles en pixels
multipliées par l'échelle alors que la police l'était déjà : rangées deux fois trop
hautes, deux jeux visibles, actions hors cadre. Sans appareil sous la main, dessiner
une interface à l'aveugle n'est pas une option.

**Chasse aux bugs (même jour), 8 corrections** — presque toutes trouvées en comparant
mon code à ce que la borne fait DÉJÀ, la recette exacte étant à trois écrans de la
table de touches que j'avais recopiée :

- **injection touche/clic** : la borne MAINTIENT 4 trames puis relâche, et refuse
  toute nouvelle injection pendant le maintien. Mon premier jet faisait le clic
  down+up dans la même trame (ratable par un jeu qui scrute chaque VBL) et laissait
  un 2ᵉ tap rapide écraser le relâchement en attente — touche « collée » côté ST,
  le bug même que le commentaire kiosk décrit ;
- **clic fantôme** : taper le bouton MENU envoyait aussi un clic gauche au ST
  (gate `WantCaptureMouse`), et un FINGERUP avalé par l'interface désynchronisait
  le compteur de doigts (le tap suivant passait pour un clic droit à deux doigts) ;
- **boucle libre** : depuis que le menu est redessiné à chaque itération, swap
  immédiat + `Delay(1)` ≈ 50 rendus par trame émulée — vsync ON (repli si refusé),
  la cadence d'émulation restant sur le temps émulé ;
- **data race** : `g_primed` (thread audio) était écrit par le thread principal au
  retour de veille — retiré, l'anneau se gère seul ; underruns passés en atomique
  et JOURNALISÉS (~1 msg/5 s, comme le natif) ;
- **contexte EGL perdu en veille** : texture/programme/VBO recréés au retour au
  premier plan (écran noir muet sinon, sur les appareils qui ne préservent pas le
  contexte) ; troncation UTF-8 du pied de page calée sur un bord de point de code.

**Ce n'est pas fini** : pas de stick virtuel, pas d'import de disquettes (SAF), pas de
sélecteur de ROM ni de réglages, pas de sauvegarde d'état, pas d'effets CRT — et rien
n'a tourné sur un appareil réel.

## Son de la démo WASM : les samples redeviennent audibles (2026-08-11)

Symptôme rapporté : dans le navigateur, « les samples ne s'entendent presque pas ».
La mélodie passait, la batterie non.

**Cause.** La chaîne de mixage vivait en TROIS copies : `Audio::produceFrame` (GUI),
le dump `--sound-dump` du headless — dont le commentaire disait déjà « même chaîne
que » — et le frontend web. Cette troisième copie était restée sur l'ANCIENNE API :
la page TIRAIT des échantillons quand son `ScriptProcessorNode` réclamait un bloc
(~43 ms) et le cœur synthétisait alors en lisant les registres du YM **en direct**.

Or tout ce qui fait un sample sur ST module le son SOUS la trame : un digidrum écrit
le registre de volume à plusieurs kHz, le sync-buzzer réarme l'enveloppe en rafale,
les bruitages DMA durent quelques millisecondes. Échantillonner ça une fois par bloc,
c'est n'en garder qu'un point sur mille : la modulation disparaît, et il ne reste que
ce qui varie lentement — la mélodie. Le son n'était pas « trop faible », il était
**aplati**.

**Correctif.** La chaîne devient une unité du cœur, `core/AudioMix.cpp`, appelée par
les trois frontends : YM horodaté (`synthesizeFrame`), DMA STE horodaté (`mixStereo`),
HPF, gains et tonalité LMC1992, dans cet ordre — celui qui a été calé contre les WAV
oracles. Le web produit désormais le son **par trame émulée**, juste après `runFrame`,
comme le natif ; la page ne fait plus que mettre en file et sortir.

Détails qui comptent :

- **`setCycleClock` armé côté web** (PSG et son DMA). C'est la ligne sans laquelle
  rien ne marche : `synthesizeFrame` rend le jeu de registres « audio », que SEULS les
  événements horodatés mettent à jour — sans horloge, la machine est muette. Vérifié
  en le manquant : la sortie tombait à zéro absolu.
- **Sortie stéréo** (elle était mono) : le DMA STE est stéréo et le LMC1992 panoramique.
- **AudioWorklet** quand le navigateur le sait, repli automatique sur
  `ScriptProcessorNode` (déprécié) sinon. Le mixage vit alors sur le thread audio : un
  à-coup du thread principal — qui porte l'émulation ET le rendu — ne coupe plus le son.
- **File d'attente avec coussin de 90 ms**, amorçage, et **asservissement de débit** :
  la page renvoie sa profondeur de file, le cœur ajuste de ±8 échantillons par trame
  (≤ 0,8 % de hauteur, inaudible) pour absorber la dérive entre l'horloge de
  l'AudioContext et celle de la machine. Garde-fou des deux côtés : une sortie qui ne
  consomme pas (contexte suspendu avant le geste utilisateur) ne fait plus enfler la
  file de ~380 Ko/s.
- **Curseur de volume** dans la page, appliqué par le cœur en rampe anti-clic.

**Nouvel étalon : `tools/make_digidrum_test.py`.** Une disquette bootable qui joue un
digidrum — mixeur YM à $3F (ni tonalité ni bruit, seul le DAC de volume sort) et Timer A
à 7 979 Hz écrivant une table de 8 points → carré de ~997 Hz. C'est le test qui
DISCRIMINE : une synthèse « en direct » ne peut pas le rendre. Le disque `make_dmasnd_test`,
lui, joue un flux continu et sortait au même niveau AVANT comme APRÈS — il mesure le
niveau, pas la fidélité temporelle.

Mesures (Chrome headless, analyse spectrale de la sortie réelle du navigateur) :

| digidrum ~997 Hz | avant | après | référence native |
|---|---|---|---|
| raie dominante | 211 Hz (fantôme) | **1008 Hz** (1 case de FFT) | 996 Hz |
| saillance | ×6 | **×28** | ×104 |
| niveau | −19,8 dBFS | **−13,7 dBFS** (volume 80 %) | −12,7 dBFS |

Non-régression du natif prouvée au bit près : `--sound-dump` avant/après l'extraction
donne des WAV **identiques** (`cmp`), sur le test DMA STE et sur une démo ST.

## Profils de réglages nommés (2026-08-10)

`neost.cfg` **était** déjà écrit tout seul à chaque changement — mais il n'y a qu'UNE
configuration courante, et l'émulateur sert des attelages incompatibles : une démo
Spectrum 512 veut 512 Ko + TOS européen (50 Hz), un crack veut 1 Mo, une image `.stx`
veut son lecteur B. Refaire la manœuvre à chaque fois, c'est exactement ce que la barre
d'état du 2026-08-07 a montré comme source n°1 de faux rapports de bug.

**Page `Profiles`** (fenêtre Configuration, ou `Machine → Settings profiles…`) :
on nomme la configuration EN VIGUEUR, on la retrouve d'un clic. Un fichier
`profiles/<nom>.cfg` par profil, à côté de `neost.cfg` et **au même format** —
lisible, éditable, copiable d'une machine à l'autre. `Load` / `Overwrite` / `Delete`
(en deux temps : le bouton devient `Delete?` + `Cancel`).

Un profil enregistre les réglages, pas l'état de la machine : modèle, RAM, FPU, ROM,
supports montés (A, B, cartouche, GEMDOS, ACSI), moniteur, CRT, son, entrées. Il laisse
volontairement dehors l'**horloge** (`rtc=`, état machine), les **dossiers ROM de la
borne** (`kiosk_romdir=`, propre à l'installation) et la **disposition de l'interface**
(`dock=`, `showXxx=`, `uiVersion=` — cousins d'`imgui.ini`) : rappeler un profil ne doit
pas déplacer les fenêtres de l'utilisateur.

Trois points de mise en œuvre, tous conséquences de choix déjà faits dans le fichier :

- **Un seul format, deux lecteurs.** `parseConfigLine` / `writeConfigKeys` /
  `writeConfigAtomic` sont extraits de `loadConfig`/`saveConfig`. Charger un profil, c'est
  partir de la config courante et lui appliquer les lignes du fichier : ce qu'un profil
  **ne dit pas** ne change pas, sans liste de recopie champ par champ à tenir à jour.
  L'écriture atomique (tmp + rename, échec = ancien fichier intact) profite aux profils.
- **Application par les requêtes existantes.** Charger pose `reqRebuild` (`applyConfig`
  refait déjà modèle/RAM/FPU/ROM/cartouche/HD/moniteur/FDC en une reconstruction) et,
  pour les lecteurs — qu'`applyConfig` conserve délibérément —, les requêtes normales de
  montage, qui valident l'image avant d'écrire quoi que ce soit.
- **Nom de fichier assaini.** Le champ est libre : séparateurs de chemin, caractères de
  contrôle et réservés Windows sont retirés, points et espaces de bord rognés, nom vide
  refusé (`../../evil` → `evil.cfg`, dans `profiles/`). Les accents, eux, passent.

**En borne, rien ne s'écrit** : les profils restent consultables et chargeables, mais
`Save`/`Overwrite`/`Delete` sont grisés et doublés d'une garde côté boucle — l'invariant
« la borne repart identique » vaut aussi pour ce dossier.

**Au passage : le son du lecteur était un réglage sans mémoire.** La case « Floppy drive
sound » se cochait, se décochait… et repartait à ON au lancement suivant : `drivesound=`
n'existait pas. Clé ajoutée. Le câblage (brancher `DriveSound` sur l'`Audio`, armer le
sink `FdcSound`) suit désormais la **disponibilité** des échantillons et non le réglage,
sinon démarrer son coupé rendait la case sans effet pour toute la session ; et la case est
grisée si `roms/drivesound/` manque, au lieu de mentir.

La rangée de préréglages matériels en haut de la fenêtre s'appelle maintenant `Presets:`
(520 ST / 1040 STE / Mega STE) — elle ne garnit que les champs « en attente », là où un
profil est une configuration complète de l'utilisateur.

## Interface : une fenêtre « Configuration » unique + barre d'état (2026-08-07)

Réorganisation de la GUI. Le diagnostic tenait en trois points : **trois idiomes pour la
même action** (une cartouche se montait par le menu *et* par une fenêtre, un disque dur en
tapant un chemin *ou* par une fenêtre, une disquette par une fenêtre seulement), un menu
`Machine` **fourre-tout** (actions + configuration matérielle + réglage d'émulation +
Quitter), et surtout **aucun affichage de l'état courant** — or les deux « bugs » signalés
ce jour-là (démo déchirée, jeu qui plante) étaient des faits de configuration invisibles :
ROM `us` en 60 Hz NTSC, et 512 Ko là où le crack veut 1 Mo.

**Fenêtre `Configuration`** (⚙ dans la barre d'outils, `Machine → Configuration…`),
ancrable et non modale : colonne de navigation à gauche (Machine · Mémoire · ROM/TOS ·
Disquettes · Disques durs · Cartouche · Écran · Son · Entrées · Émulation · Borne), page à
droite, rangée de profils en haut (520 ST / 1040 STE / Mega STE). Elle **absorbe** six
sous-menus et les **trois fenêtres-bibliothèques** (Disk/Cart/Hard Disks, supprimées) : il
n'y a désormais qu'**une** façon de monter un support. Elle ne fait rien elle-même — tout
sort en requêtes consommées en fin de trame, la discipline des anciennes bibliothèques.
La page ROM affiche **50 Hz PAL / 60 Hz NTSC** (en orange) à côté de chaque image, d'après
le suffixe pays.

**« Appliquer et redémarrer »** : modèle, RAM, FPU et ROM ne relancent plus la machine à
chaque clic. Ils sont mis **en attente**, le pied de page les compte (« 3 réglages
matériels en attente ») et un seul bouton reconstruit une fois — avec le rattrapage TOS
≥ 2.06 du Mega STE (`pickTosForMachine`) au passage. Les **montages** restent immédiats :
monter est une action, pas un réglage.

**Barre d'état permanente** : `Mega STE | 4 Mo | tos206fr | 50 Hz PAL | A: … | B: … |
C: gemdos/ | 50,1 fps`. Chaque segment est cliquable et ouvre SA page. C'est le remède
direct aux faux rapports de bug ci-dessus.

**Lecteur B en GUI.** Le cœur le gérait depuis toujours (`Fdc::loadImage(path,1)`,
`--diskb` du headless) ; seule l'interface l'ignorait — alors que Lethal Xcess ne DÉMARRE
qu'avec son disque 2 monté. Chaque ligne de la ludothèque a maintenant deux boutons
`[A] [B]` ; mémorisé (`diskb=`).

**Glisser-déposer** sur la fenêtre : un DOSSIER se monte en C: (GEMDOS), une image
`.st/.msa/.dim/.stx` va dans le lecteur A, une image de disque dur en ACSI, un TOS devient
la ROM. `.img` étant ambigu (roms/, carts/ et hd/ en sont tous pleins), l'arbitrage se fait
sur la **taille et l'en-tête** (BRA.S `$602E` + ≤ 512 Ko = TOS ; ≤ 128 Ko = cartouche ;
au-delà = disque dur), pas sur l'extension. Ignoré en borne (config figée).

**Barre de menus ramenée à quatre entrées** — Machine (actions + états + borne + Quitter),
Affichage (moniteur, zoom, CRT, ancrage), Fenêtres (**inspection seulement** : hex, CPU,
joystick, débogueur), Aide (**liste des raccourcis**, jusqu'ici nulle part). La barre
d'outils ne porte plus que des verbes (⚙ ⟳ ⏻ ◐ + volume) : ses bascules de fenêtres
faisaient doublon avec le menu.

**Dossier `hd/`** (+ `hd/README.md`, contenu gitignoré) : un **dossier** dedans = un lecteur
GEMDOS, un **fichier** image = un disque ACSI. Le scan des images n'est pas récursif, sinon
les `.img` rangés dans un lecteur GEMDOS seraient proposés comme disques durs.

Migration : `neost.cfg` gagne `diskb=`, `showCfg=`, `uiVersion=` (les `showDisk=/showCart=/
showHd=` d'avant sont ignorés) ; `uiVersion` resème **une fois** la disposition ancrée,
sans quoi un `imgui.ini` existant garderait des nœuds pour des fenêtres disparues et
laisserait la fenêtre Configuration flotter au-dessus de l'écran ST.

Validé en GUI (captures à l'appui) : montage GEMDOS → barre d'état `C: gemdos/` ; profil
Mega STE → « 3 réglages en attente » → Appliquer → `[Bus] TOS chargé : tos206fr` et barre
d'état `Mega STE | 4 Mo | tos206fr` ; lecteur B → `B: diskA.st` ; page ROM avec ses badges
50/60 Hz ; **mode borne intact** (`--kiosk` : plein écran, aucun chrome). ⚠ Non faits, et
c'est ce que la colonne de gauche est faite pour accueillir : pause/avance rapide,
capture d'écran, protection en écriture, imprimante/RS-232, plein écran hors borne.

## Effets CRT : version GLSL choisie à l'exécution (débloque le Raspberry Pi) (2026-08-07)

Sur la borne Pi, activer les effets CRT échouait avec « **shader indisponible : GLSL 1.50
is not supported. Supported versions are: 1.10, 1.20, 1.30, 1.40, 1.00 ES, 3.00 ES** » : le
préambule `#version 150` était **codé en dur** dans `OpenGLShader.cpp` alors que le V3D des
Raspberry Pi (Mesa) plafonne à **GLSL 1.40**. Le corps des shaders CRT, lui, n'utilise que
des constructions **GLSL 1.30** (`in`/`out`, `texture()`, `fwidth()`) — il n'y avait rien à
réécrire, seulement à cesser d'exiger 1.50.

Le dialecte est maintenant déduit de `GL_SHADING_LANGUAGE_VERSION` puis essayé **en cascade
150 → 140 → 130** (`#version 300 es` si le contexte est GLES natif — Pi en KMS/Wayland,
Emscripten). La cascade est un filet et pas une coquetterie : un pilote peut annoncer une
version et la refuser dans *ce* contexte, seule la compilation réelle tranche. Les échecs
des tentatives intermédiaires sont silencieux et `errorOut` est vidé en cas de succès —
sinon le panneau afficherait « shader indisponible » alors que la pile est prête.

Diagnostic : une ligne au démarrage dit ce qui a été retenu **et** ce que le pilote annonce
— `[CRT] GLSL 140 (pilote : 1.40)`.

Validé bout en bout sous Mesa llvmpipe avec la version forcée (`MESA_GL_VERSION_OVERRIDE` /
`MESA_GLSL_VERSION_OVERRIDE` — ⚠ le pilote propriétaire NVIDIA les ignore, il faut
`LIBGL_ALWAYS_SOFTWARE=1 __GLX_VENDOR_LIBRARY_NAME=mesa`) : pilote 4.60 → 150, **1.40 → 140
(cas Pi)**, 1.30 → 130, chaque fois « pile d'effets CRT prête » et, capture de fenêtre à
l'appui en 1.30, l'image bien rendue à travers la pile. ⚠ **Pas encore rejoué sur le Pi
lui-même** ; la ligne `[CRT] GLSL …` le confirmera. Une pile limitée à GLSL 1.20/ES 1.00
échouerait encore (il faudrait repasser en `attribute`/`varying`/`texture2D`/
`gl_FragColor`) — ce n'est pas le cas du Pi 4. Le reste de la pile (FBO `GL_RGBA8`, VAO)
passe sans retouche sur V3D.

## IACK MFP +4 cyc (raster Super Hang-On verrouillé à l'oracle) + chaîne son STE fidèle (2026-08-06/07)

**IACK MFP vectorisé : 12 → 16 cycles** (`Cpu68k.cpp`, `g_iackMfp`). Mesuré à l'oracle
Hatari **instrumenté** sur Super Hang-On EN JEU (banc souris déterministe + `--trace
video_color` + diag `[HEXC]` étendu à la position vidéo) : la chaîne fixe « exception
Timer B → handler → `stop #$2100` → HBL pendante prise au stop » fait 144 cycles chez
Hatari, 140 chez NeoST — le `CPU_IACK_CYCLES_MFP_CE=12` d'Hatari (« not measured ») ne
compte pas le cycle bus d'IACK lui-même. Après correction, l'histogramme des écritures
palette du raster in-game est **verrouillé à ±1 point** sur ~2 500 trames (1re écriture
de paire {104..128}, réveil STOP 40/40/20 inchangé et déjà exact). C'était la cause des
« lignes transitoires » de SHO — aucun des candidats de la 5ᵉ passe. Nouveaux outils :
`NEOST_PAL_TRACE_ALL` (trace palette cumulative par trame), `NEOST_RAISE_DIAG`/
`NEOST_RAISE_WINDOW` (fenêtre de différé ipl_fetch, opt-in — mesuré : les frontières de
prise d'IRQ d'Hatari CE correspondent au commit SANS différé), événements fifo
`leftdown`/`leftup` ajoutés à l'oracle. Étalon `nocooper` recalé d'une trame (méthode de
sa note, 0 px bit-identique). Détail complet → `docs/HATARI_DIVERGENCES.md` § 10ᵉ passe.

**Son STE — chaîne de sortie alignée sur Hatari** : signe DMA **×−1** (le LMC1992 inverse
le canal DMA — phase relative YM↔DMA) ; **HPF sous-sonique déplacé sur le MIX YM+DMA** en
STE (le YM entre brut dans le mix, comme sound.c/dmaSnd.c — DC du DMA filtré), GUI +
`--sound-dump` + WASM ; correcteur LMC1992 en **plateaux 1er ordre Savinkoff**
(118.2763/8438.756 Hz, port exact — remplace le RBJ 2e ordre 200/8000) ; horloge YM
**250 663 Hz** réels (MCLK/128 — l'ancien 250 000 jouait ~4,6 cents bas). Validation :
selftests + tier full verts, étalon `make_dmasnd_test` (fetch au faisceau) inchangé.

**Événements échus dispatchés au point d'IACK** (`NEOST_IACK_SYNC`, défaut ON) — port du
`CycInt_Process()` que Hatari appelle juste avant la séquence d'IACK (newcpu.c:2938-2946) :
un timer expirant dans la fenêtre « frontière d'instruction → IACK » n'avait pas posé son
bit IPR quand le vecteur MFP était élu. Mesuré : un événement est échu à **5,7-7 % des
IACK**. Le dispatch **rebase le quantum** au préalable (`Cpu68k::rebaseQuantumAndSync`),
comme le saut STOP : sans ce rebase le temps est compté deux fois et tout le raster glisse
de ~16 cycles — régression attrapée par le banc SHO avant commit.

**Bug hunt (workflow 6 chasseurs + vérification adversariale) — 12 correctifs**, dont :
niveau DMA STE **−6 dB** (le ÷4 d'Hatari pré-compense AUSSI le ×2 des gains LMC —
`kDmaGain` −0.1875) ; `reconfigure` à chaud qui perdait le placement du HPF (STE↔ST) ;
chaîne LMC non gatée et `adjustMachineForTos` absent côté **WASM** (YM +6 dB sur ST,
TOS incompatible = écran figé) ; **scroll fin STE par ligne** dans le re-rendu fenêtré
(`renderGlueFrame` appliquait la valeur de fin de trame à tout l'écran → save-state
**v9**, capture `lineScrollSnap_`) ; bit `LOOPING` forgeable dans un save-state
(pointeur-membre nul en Release) ; $FFFA31-3F void (0xFF, sans wait-state) au lieu de
RAM cachée ; $FF8900/8920 fidèles ; `--load-state` qui écrasait `--joy` ; défauts de
config annoncés. Détail → `docs/HATARI_DIVERGENCES.md` § 10ᵉ passe (bug hunt).

## Performance du cœur : ~2,4× sur la même machine, à sortie octet-identique (2026-08-02)

Campagne menée **au callgrind**, sur un profil de boot TOS et un profil en jeu (les deux
ont la même forme : les points chauds du cœur ne dépendent pas du logiciel émulé).
Méthode, mesures ligne à ligne, fausses pistes et pièges → **[`docs/PERFORMANCE.md`](docs/PERFORMANCE.md)**.
**Aucune valeur émulée ne change** : les 15 étalons pixel-exacts sont restés à 0 pixel
d'écart après chaque étape, et les variantes de compilation ont été vérifiées
octet-identiques sur des captures de 6801 et 29500 trames.

**Bus — le décodage MMU était refait à chaque octet.** `mmuTranslate()` relisait la
config `$FF8001`, retraversait deux `switch` de taille de banque, divisait pour obtenir
la taille de RAM posée, puis rejouait le remappage RAS/CAS : 12,8 M d'appels pour
300 trames de boot, ~39 instructions pièce, ~20 % du programme avec ses appelants. Le
résultat ne dépend pourtant que de deux entrées (l'octet de config, la taille de `ram[]`)
— il est désormais mémorisé et *revalidé par comparaison de ces deux entrées*, ce qui
rend impossible l'oubli d'un site d'invalidation. Le cache ne retient qu'une chose : la
longueur du préfixe où la traduction est l'**identité** (le cas dès qu'une banque est
annoncée à sa taille réelle, ce que fait tout TOS après son sizing) — démonstration en
commentaire dans `Bus::rebuildMmuCache`, plus un contrôle en debug contre le décodage
complet. `read8`/`read16`/`write8` passent dans l'en-tête et s'inlinent chez l'appelant.
⚠ La première version ne couvrait que la RAM et ne rendait que −4,4 % : **le code du TOS
s'exécute depuis la ROM**, donc chaque mot d'opcode repassait par le chemin lent. La
fenêtre ROM ajoutée au chemin rapide a porté le gain à −20 %.

**Ordonnanceur — le balayage chaud n'était pas celui qu'on croyait.** Le profil
l'attribuait à `Machine::runFrame` : l'appelant est `sched.nextDue()`, une fois par bloc
CPU. Plutôt que d'accélérer le balayage, on l'a supprimé — `runTo` calcule le minimum des
échéances *pendant* sa passe de dispatch (qui parcourt déjà les mêmes sources), et
`schedule()` tient `nextDue_` **exact** au lieu de simplement minorant, si bien que
`nextDue()` répond en O(1). Un `assert` compare le cache au balayage complet en debug.
Une variante intermédiaire — minimum sans branche sur tableau plein — a été **essayée
puis retirée** : elle coûtait +1,4 % d'instructions (parcourir 19 entrées coûte plus que
d'en sauter 5).

**Shifter — deux tables au lieu de deux boucles.** Le dé-entrelacement des bitplanes se
faisait bit à bit (4 décalages + 4 masques par pixel) : remplacé par une table de 256
entrées qui éclate un octet de plan en 8 octets, les quatre plans se composant en **une
opération 64 bits**, 8 pixels d'un coup (indépendant du boutisme : chaque octet valant 0
ou 1, les décalages de 1-3 restent confinés). Et `stColorToArgb` était appelée pour
*chacun* des 320 à 640 pixels d'une ligne alors que la palette ne peut pas changer
pendant l'émission — 16 conversions par ligne désormais.

**Deux appels gratuits sur le chemin chaud** : `busFaultN` court-circuite la RAM ordinaire
et la lecture en ROM, qui ne fautent jamais ; et `busDiag`, diagnostic éteint, franchissait
la garde d'un statique local **et** évaluait `getClock()` à chaque accès bus.

**Compilation guidée par profil (PGO) + LTO — le plus gros gain unitaire, sans toucher au
code.** La boucle chaude est l'interpréteur Moira : un branchement indirect sur l'opcode
puis beaucoup de branches rarement prises. Avec le profil, GCC range les blocs pour que le
cas fréquent tombe en séquence — ce qui compte double sur un Cortex-A72 (32 Ko de L1i).
`build_native_pi.sh --pgo`, `pgo_train.sh` (parcours volontairement large : ST et STE,
50 et 60 Hz, mono, un jeu, une démo à retraits de bordure, les auto-tests — un profil
étroit fait déclarer « froid » du code qui ne l'est pas) et la CI `pi-borne.yml`, qui
entraîne sur le runner ARM64 pour que le Pi n'en paie rien. ⚠ **Piège qui coûte tout le
gain en silence** : GCC nomme les `.gcda` d'après le chemin absolu de l'objet, donc
instrumenter dans un répertoire et relire depuis un autre ne trouve **aucun** profil — et
`-Wno-missing-profile`, indispensable pour les objets du GUI non entraînés, rend l'échec
totalement muet. Une première mesure annonçait ainsi « PGO = −4 % », qui n'était que du
bruit. Les deux passes partagent désormais le même répertoire, et les scripts **échouent**
si aucun profil n'a été collecté pour `Cpu68k`, `Bus`, `Shifter` et `Moira`.

## Borne Raspberry Pi : démarrage direct + latence audio réglable (2026-08-02)

**`--audio-latency MS`** (persisté `audio_latency_ms=` dans `neost.cfg`) : le coussin
d'amorçage de l'anneau audio était figé à 85 ms dans `Audio::start`. Il est maintenant
réglable et borné à `[20, 250]` ms par `Audio::setLatencyMs` — au-delà, le coussin
s'approcherait de la capacité de `SampleRing{32768}` (341 ms à 48 kHz stéréo) et le
producteur jetterait des échantillons à chaque trame. Vérifié bout en bout : `--audio-latency
130` → `coussin 6240 frames` = 48000 × 130/1000, et `latence ~130 ms` au démarrage.

**`packaging/raspberry/`** — déploiement d'une borne qui démarre *directement* sur
l'émulateur, sans bureau. `install_kiosk.sh` (idempotent, `--uninstall`) monte un X **nu**
(ni gestionnaire de fenêtres ni compositeur) sur le VT 1 via une unité systemd modèle,
purge les serveurs de son (miniaudio → ALSA en direct), passe le gouverneur en
`performance`, épingle les IRQ sur le cœur 0, coupe Wi-Fi/BT/swap et le boot bavard.
`build_native_pi.sh` compile avec le `-mcpu` du cœur réel (l'AppImage livrée est aarch64
générique), et `pi-borne.yml` fait le même travail en CI sur runner ARM64 natif dans un
conteneur bookworm (plancher glibc ≤ 2.36 vérifié) pour éviter les 20-40 min de
compilation sur le Pi.

**Son : HDMI ou Bluetooth — le Pi 400 n'a pas de jack.** Par défaut, aucun serveur de
son et détection de la sortie HDMI *réellement branchée* via l'ELD (le Pi 400 a deux
ports). `--bluetooth-audio` installe PipeWire, seul chemin vers l'A2DP : miniaudio ne
sait pas parler Bluetooth. Ce qui rend le mode possible **sans toucher au code**, c'est
que miniaudio classe PulseAudio AVANT ALSA (`ma_backend` est ordonné par priorité) —
NeoST se branche donc sur `pipewire-pulse`, qui déplace le flux vers l'enceinte quand
elle se connecte, même en pleine partie ; sans cela `Audio::start` ouvre UN périphérique
au démarrage et n'en change jamais. Réglé pour ne pas coûter cher : 48 kHz verrouillé
(NeoST sort déjà en 48 kHz → aucun rééchantillonnage), quantum 1024, et profils HSP/HFP
coupés (une enceinte qui bascule en HSP passe en 8 kHz mono avec le micro ouvert —
la panne Bluetooth la plus fréquente). `neost-bt.sh` + un timer de 30 s rattrapent
l'enceinte allumée APRÈS la borne. ⚠ l'A2DP ajoute 150-250 ms irréductibles : pour
jouer, l'HDMI reste très supérieur.

Deux pièges refermés dans le script plutôt que dans un ticket : miniaudio
demande `SCHED_FIFO` pour son thread ALSA et **échoue silencieusement** sans
`LimitRTPRIO=` (le thread audio reste préemptible → underruns), et le `libglfw3` de
bookworm est **X11 uniquement**, ce qui exclut un kiosk Wayland (`cage`) sans recompiler
GLFW. ⚠ Scripts **non encore exécutés sur un Pi réel** — cf. leur README.

## Relecture adversariale pré-release (2026-08-01)

Cinq audits parallèles (zone chaude des 8 derniers commits, sécurité des entrées non
fiables, mémoire/UB, fidélité vs Hatari, préparation de release), chaque constat
re-vérifié dans le code avant correction. Suite `--tier full` verte avant ET après
(14 étalons, étalons pixel byte-identiques) ; 51 images disque corrompues et
**240 save-states forgés à CRC valide** rejoués sous ASan/UBSan.

**Save-states — 5 corruptions mémoire refermées.** Le chargement d'un `.state` forgé
restait une frontière de confiance trouée :
- `Shifter::liveGlueLine_` n'était pas borné et sert d'index à `startHBL` : le
  `borderMask |= …` devenait un read-modify-write à un offset **négatif arbitraire** du
  tas, répété sur toute la plage rattrapée. Garde de bornes dans `startHBL` + invariant.
- `Shifter::colorWrites_[].index` (registre palette) alimente `pal[index] = …` sur un
  `array<uint16_t,16>` **de pile** : jusqu'à 510 octets écrits au-delà, offset ET valeur
  choisis. Borné à la relecture comme `YM2149::RegEvent::reg`, et re-testé à l'écriture.
- `Fdc::lsnOffset` calculait l'offset image en **uint32 qui rebouclait** : un secteur 0
  donnait `$FFFFFE00`, et la garde « `off + 512 <= image.size()` » se calculant elle aussi
  en uint32 valait `0 <= size` — elle PASSAIT, et l'accès indexait ~4 Go plus loin (en
  lecture *et* en écriture, `writeBack` allant jusqu'à `seekp` dans le `.st` de
  l'utilisateur). Passé en 64 bits de bout en bout.
- `Fdc::bufferReadByte/Timing/BytePos` déréférençaient `buf_[bufPos_]` sans borne ; un
  état forgé entre directement dans un `TRANSFER_LOOP` sans passer par le `TRANSFER_START`
  qui teste le tampon vide (`buf_` vide → déréférencement nul).
- Gels à 100 % de CPU : `vcLineY_` et `renderLine_/tbLine_/hblLine_` n'étaient bornés
  qu'en bas (ou pas du tout) alors qu'ils pilotent des boucles de rattrapage.

**Comportements indéfinis (trouvés par fuzzing sous UBSan).** Un `bool` restauré à 63 et
une énumération `MouseMode` à 173 : dans les deux cas le chargement lui-même est un UB (un
`bool` non-0/1 peut rendre `if (b)` et `if (!b)` vrais tous les deux). Les booléens sont
désormais **normalisés dans `StateArchive`** — donc pour tous les composants d'un coup — et
`mouseMode_` transite par son type sous-jacent. Format de fichier **inchangé**.
`StateArchive::check()` prend en outre une étiquette : un état refusé dit maintenant PAR
QUOI (sans elle, une garde trop stricte est indiscernable d'un fichier corrompu — c'est ce
qui a permis de rattraper une des gardes de cette passe, qui refusait l'overscan légitime).

**GUI.** La bascule F8 vers le mode borne persistait les préférences de la séance, puis
n'importe quel `saveConfig(force)` ultérieur de la borne les écrasait avec la config du
LANCEMENT (`g_cfgPristine` figé au démarrage) — y compris sur simple auto-purge d'un
dossier ROM disparu. `drawCartLibrary` a reçu les deux durcissements de son jumeau
`drawDiskLibrary` : retour anticipé sur `Begin()` faux, et itération manuelle du dossier
(le range-for lève `filesystem_error` non rattrapée → `std::terminate`).

**CI de release — le chemin de publication ne pouvait pas aboutir.** Le job `linux-arm64`
compilait sans `-DNEOST_VERSION_STR` (les trois autres le posent), donc son binaire
annonçait le `project(VERSION)` figé `0.1.0` tandis que la garde exigeait la version du
paquet : job rouge à tous les coups, et `publish` en dépendant, **aucune release n'aurait
pu sortir**. `ffmpeg`, dépendance non déclarée de `compare_screenshot.py`, est désormais
installé par les deux jobs Linux (sans lui les 5 étalons à référence PNG échouent).

**Le garde-fou de vacuité des références échouait « en mode ça passe »** : sans ffmpeg il
imprimait un simple ⚠ et sortait 0, laissant 6 références non contrôlées — dont les trois
oracles spec512, précisément l'étalon dont la référence a été noire deux fois. Un contrôle
non concluant est maintenant un ÉCHEC.

**Diffusion.** Le workflow GitHub Pages construisait le bundle WASM avec
`NEOST_WEB_FREE_ONLY=OFF`, c'est-à-dire tout `roms/` et `disks/` embarqués — sous une
condition écrite dans ce même fichier (« dépôt à garder privé ») qui **n'est pas remplie**.
Basculé sur `ON` (EmuTOS + `diskA.st`). ⚠ Le dépôt lui-même suit toujours ce contenu :
cf. `TODO.md`.

**`.MSA`/`.DIM` inscriptibles — port de `MSA_WriteDisk`/`DIM_WriteDisk`.** Ces images
étaient montées en lecture seule, et le drapeau ne bloquait pas que la recopie hôte : il
pilotait le **bit WPRT du WD1772** vu par le programme. Sauvegardes en jeu, high-scores,
écritures depuis le bureau TOS et protections « écrit puis relit » échouaient donc
« disque protégé » sur toute `.msa`/`.dim`, alors que la même disquette en `.st`
fonctionnait. Hatari, lui, ne dérive WPRT que du réglage et de `stat()` — jamais du
format (`floppy.c:205-225`). `writeProtect` ne vient plus que de `stat()` ; `writeBack`
dispatche désormais sur le conteneur (`FloppyDisk::imgFormat`) : écriture partielle in
situ pour le `.ST`, idem décalée de 32 o pour le `.DIM` (en-tête préservé, comme
`dim.c:134-149`), et ré-encodage RLE complet **atomique** (tmp + rename) pour le `.MSA` —
reconstruire tout le fichier, une coupure en cours laisserait sinon une disquette
illisible. Le refus d'écrire ne subsiste que là où l'on ne SAIT PAS ré-encoder (STX, ou
en-tête `.msa`/`.dim` reconnu mais indécodable) : y écrire détruirait le fichier.
Nouvel auto-test `neost-headless --msa-selftest` (étalon `msa_selftest`, palier *fast*) :
44 cas — aller-retour byte-exact sur 6 géométries × 7 motifs (dont `$E5` isolé, qui doit
être échappé même seul, et un motif incompressible qui force la branche « piste stockée
brute »), plus deux cas de bout en bout montage → écriture → remontage sur fichiers `.msa`
et `.dim` réels. Vérifié aussi qu'aucun disque d'étalon suivi par git n'est modifié par
un run complet, et que les `.msa` tronquées restent en lecture seule.

**Documentation.** `HATARI_DIVERGENCES.md` affirmait que `.MSA`/`.DIM` étaient « conformes
(vérifiés ligne à ligne) » : c'est faux et cela masquait un écart réel (montage en lecture
seule + bit WPRT présenté au programme, là où Hatari ne dérive WPRT que de `stat()`) —
consigné en D0. L'écart `$FFFA01` GPIP bits 3/6 vs Hatari est consigné, attendu, et à
connaître avant toute chasse différentielle.

## Bug hunt passes 1-3 + CI de release (2026-07-31)

**Sécurité / crashs.** Le pont GEMDOS laissait s'ÉCHAPPER du dossier monté : `/` n'était
pas reconnu comme séparateur Atari (seul `\` l'était), donc un `.TOS` hostile lisait,
écrivait et listait hors du bac à sable avec les droits de l'utilisateur (Hatari a le même
trou — durcissement assumé). Le blitter pouvait faire PLANTER l'émulateur : un blit visant
son propre registre de contrôle se relançait à l'infini (SIGSEGV). Les deux sont prouvés
par repro et re-testés fermés.

**Le filet de test lui-même était troué** — `run_selftests.py` rendait VERT un émulateur
qui segfaute (code de retour ignoré + dump série périmé relu) ; `--only <ID inconnu>`
exécutait zéro test en annonçant « TOUS OK » ; une référence absente comptait comme une
réussite. La CI de release ne lançait AUCUN palier de validation : elle lance désormais
`run_all.py --tier fast`.

**Émulation.** FPU : `normalizeSubnormal` portait la branche x87 au lieu de la branche
68881 → tout opérande dénormal ressortait ×2 ; FSGLMUL/FSGLDIV rabattaient à tort la plage
d'exposant et tronquaient avant les cas spéciaux (fuzz différentiel contre `softfloat.c` :
0 écart). L'instruction 68000 **RESET** ne réinitialisait aucune puce (port de
`customreset()` : IKBD, Glue, PSG, FDC + IRQ latchées ; `MFP_Reset_All` reste à faire).
`Machine::liveNow()` comptait le temps DEUX FOIS pendant le dispatch du saut STOP (1208
dispatches sur 15780, δ ≤ 112 cyc → 0). Save-states **v6 → v7** (empreinte GEMDOS +
cartouche, `bus.cart` sérialisée). STX : les écritures faites après un formatage de piste
étaient perdues au remontage.

**Paquets.** CI de release : 5 artefacts (AppImage x86_64 glibc 2.27, AppImage aarch64,
AppImage Raspberry Pi, `.dmg` macOS Universal 2, bundle WebAssembly) + sommes SHA-256.

## Bug hunt multi-agents (2026-07-29)

Chasse à 6 lentilles parallèles (diff non commité, cœur CPU/Bus/état, vidéo, I/O disque,
périphériques, audio & concurrence), chaque lot passé à un sceptique mandaté pour RÉFUTER :
26 findings bruts → **22 confirmés, 4 réfutés**, tous corrigés. Étalons `--tier full` verts
avant/après (Cuddly & Enchanted Land 0 px), build ASan/UBSan sans rapport.

- **Sécurité mémoire — 3 critiques, toutes reproduites puis refermées :**
  - *Save-state / framebuffer* : `curW_`/`curH_` étaient restaurés sans invariant les liant à
    la taille de `frame_`, et le court-circuit « même w/h » de `resizeFor()` empêchait la
    réallocation → écriture hors du tas dès le 1ᵉʳ `renderLine` (repro : *stack smashing* +
    core dump). Invariants `ar.check` posés APRÈS `podVec(frame_)` (géométrie, mode, aires
    actives) + taille du tampon intégrée au test de `resizeFor`. `Shifter.hpp/.cpp`.
  - *Parseur STX* : `TrackImageSize` (16 bits venus du fichier) n'était jamais confronté à la
    fin du tampon → `readTrackStx` lisait jusqu'à ~64 Ko hors du tas sur une `.stx` tronquée
    (mesuré : 65535 annoncés pour 6 octets disponibles). Plafonné sur le reste réel, dans
    l'esprit du clamp de `msa.c:205`. `StxImage.cpp`.
  - *Save-state PSG* : `RegEvent::reg` relu brut (0..255) indexait `audioRegs_[16]` → écrasait
    les pointeurs de `events_` et les `std::function` voisines. `ar.check(reg < 14)` (borne de
    `write8`) + masque par événement. `YM2149.hpp/.cpp`.
- **Autres invariants de save-state** (même classe, tous « fichier forgé passé le CRC ») :
  `envPos_ < 96` (table d'enveloppes), `fifoPos_ < 8 && fifoNb_ <= 8` + masque dans `fifoPull`
  (FIFO DMA son), `mwSteps_ ∈ [0,16]` (compteur de décalage Microwire), et la borne FDC
  `fifoSize_ <= 16` corrigée en `< 16` — `fifoPush` écrit `fifo_[fifoSize_]`, donc 16 était la
  seule valeur toxique que l'invariant laissait passer.
- **`.msa` illisible ne détruit plus l'image source** : une longueur de run RLE non plafonnée
  (cas nommément prévu par `msa.c:205-210`) faisait échouer tout le décodage, et le repli
  montait les octets COMPRESSÉS comme `.st` **brut et inscriptible** — le premier `write sector`
  de l'invité écrasait le `.msa` de l'utilisateur. Clamp porté + repli en LECTURE SEULE dès que
  l'en-tête ressemble vraiment à une `.msa` (`looksLikeMsaHeader`). `Fdc.cpp`.
- **Vidéo — trois consommateurs manquants, alignés sur `video.c` :**
  - `V_OVERSCAN_NO_DE` était détecté fidèlement mais consommé NULLE PART : une trame dont le DE
    vertical n'est jamais activé s'affichait normalement au lieu de sortir à l'index couleur 0,
    et le compteur vidéo avançait à tort. Branché sur ses trois consommateurs Hatari — rendu
    (`video.c:3988`), stride du compteur (raster non avancé) et Timer B en event-count
    (`video.c:3649`) — avec un cas d'auto-test Glue dédié (détection + contre-épreuve).
  - `videoCounter()` ignorait `BORDERMASK_LEFT_OFF_2_STE`/`_MED` (+20 o, `video.c:1514-1517`) :
    la ligne valait 180 o pour l'accumulation inter-lignes mais 160 pour l'offset intra-ligne,
    et le gel de fin de ligne tombait 40 cycles trop tôt.
  - Fenêtre verticale plus COURTE que `curAH_` (`VO_BOTTOM_SHORT_50`, −29 lignes) : le compteur
    avançait sur les lignes non affichées. Borné dans `glueLineBytes` plutôt que dans la boucle
    de commit, pour ne pas toucher à la cadence de capture `lineSnap_`.
- **Verrou `NEOST_LINELEN` à sémantique inversée** : `Machine` lit la VALEUR (défaut ON, `0` =
  OFF), les 3 sites `Shifter` testaient la seule PRÉSENCE de la variable — `NEOST_LINELEN=0`
  désactivait donc une moitié et ACTIVAIT l'autre, et l'A/B documenté mesurait un hybride
  jamais validé. Lecture unifiée (`envFlag`), défaut de chaque site INCHANGÉ (c'est l'hybride
  validé au pixel), docs corrigées.
- **FPU (68881)** : infini GÉNÉRÉ empaqueté avec la mantisse `$8000…` au lieu de la forme
  canonique 0 (`floatx80_default_infinity_low`) → deux motifs binaires différents pour +∞ selon
  qu'il est chargé ou calculé (`INF_SIG` séparé d'`INF_LOW`) ; `FGETEXP` d'un NaN rendait
  l'opérande BRUT, sans quiéter le SNaN ni lever `FPSR.SNAN` (délégué à `propagateNaN1`, comme
  `FGETMAN`) ; `FMOD` empruntait le court-circuit de `FREM` sur `expDiff < 0`, sautant arrondi
  de précision et `UNFL` (`softfloat.c:3048` vs `2941`).
- **Blitter** : les accès bus CPU étaient datés dans l'horloge du CŒUR alors que les fenêtres
  du blitter sont armées dans celle de l'ORDONNANCEUR — 40 cycles d'écart (les cycles de
  `Moira::reset()` avant l'ancrage de la 1ʳᵉ trame), donc fenêtre PRE_START ratée et tranche
  reprogrammée trop tard. Datation unifiée sur `Scheduler::liveNow()`.
- **Kiosk (travail en cours de `main.cpp`)** : le gel de la configuration était définitivement
  rompu par un aller-retour F8 (la borne réécrivait `neost.cfg` avec la session du visiteur) —
  `g_kioskLaunched` distingue désormais l'invariant de DÉPLOIEMENT de l'état courant ;
  l'émulation joystick-clavier restait armée en revenant au bureau depuis une session lancée en
  `--kiosk` (capture de lambda évaluée après `g_kbdJoy = g_kiosk`), avalant flèches et Ctrl
  droit sans rien afficher ; la fenêtre était replacée en (0,0) faute de géométrie fenêtrée
  jamais observée (drapeau `g_winGeomValid` + centrage sur la zone de travail du moniteur).
- **MIDI** : la file de bouclage OUT→IN croissait sans borne (~11 Mo/heure pour un séquenceur
  qui n'a aucune raison de lire MIDI IN, recopiée dans chaque save-state, `RDRF` collé donc IRQ
  ACIA permanente sous RIE). Bornée à la profondeur physique d'un 6850 (RDR + registre à
  décalage), ce qui modélise en prime l'overrun.
- **UB** : `mwData_ << 16` débordait un `int` signé dès que le bit 15 de `$FF8922` était posé
  (atteignable par du code invité, sans save-state) → arithmétique non signée.

---

## Inventaire par sous-système

Le détail de **ce qui est implémenté, puce par puce** — Cœur & boot, machines & mémoire,
Vidéo/Shifter, MFP, IKBD/ACIA, FDC & DMA, GEMDOS HD, Audio, Bus error & cartouches de
diagnostic, Frontend & outillage — a été déplacé dans
**[`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md)**. Ce fichier-ci reste la chronologie ;
celui-là répond à « NeoST gère-t-il X ? ».
