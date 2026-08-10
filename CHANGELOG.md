# Changelog — NeoST

(c) 2026 VERHILLE Arnaud. **La chronologie** : releases, puis les chantiers datés dans
l'ordre inverse. Version courante : **0.5.1**.

- « NeoST gère-t-il X ? » → [`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md) (inventaire par puce)
- « Que reste-t-il ? » → [`TODO.md`](TODO.md)

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
