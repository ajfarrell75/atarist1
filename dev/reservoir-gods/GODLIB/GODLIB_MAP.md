# GODLIB — carte AI-friendly

> **Note** : ce fichier est rédigé **par le projet NeoST** (pas par Reservoir Gods) pour
> rendre GODLIB navigable par un agent IA. Le `README.md` amont ne fait que 2 lignes.
> Provenance du code → [`../SOURCES.md`](../SOURCES.md). Chaîne de compilation NeoST
> (produit des `.TOS` étalons) → [`../../etalons/build.sh`](../../etalons/build.sh).

## 1. En une phrase

GODLIB est le **moteur de jeu/démo personnel** de **Mr Pink** (Reservoir Gods, groupe
Atari britannique), ~110 000 lignes de C + asm 68000, **1999 → 2018**, 62 modules.
Un seul auteur principal (`PNK` sur 1663 en-têtes de fonction). Cible **Atari
ST / STE / TT / Falcon** en natif (TOS) **et** PC (Windows Direct3D + SDL) pour itérer
vite. Jeu réel bâti dessus : **HotPot**. 15 exemples compilables dans `../GODLIB.SPL/`.

## 2. Conventions de nommage (À LIRE EN PREMIER — clé pour parser le code)

Hongrois systématique et **régulier** (aide énormément la lecture) :

| Préfixe | Sens | Exemple |
|---|---|---|
| `sXxx` | type **s**truct | `sGraphicCanvas`, `sSprite`, `sVBL` |
| `eXxx` / `eMODULE_*` | **e**num / valeur d'enum | `eSYSTEM_MCH`, `eVIDEO_MODE_4PLANE` |
| `uXxx` | **u**nion | `uU32` (accès octet/mot/long endian) |
| `dXxx` / `dMODULE_*` | **d**éfine (constante/flag) | `dVBL_MAX_CALLS`, `dGODLIB_PLATFORM_ATARI` |
| `gXxx` | **g**lobal | `gVideo`, `gVbl`, `gKernelClass` |
| `mXxx` | **m**embre de struct | `mpVRAM`, `mWidth`, `mUpdatePhysicFlag` |
| `aXxx` | **a**rgument de fonction | `aWidth`, `apPal` |
| `lXxx` | variable **l**ocale | `lRes`, `lSize` |
| `p` (dans le nom) | **p**ointeur | `apArgs`, `mpVRAM`, `lpDst`, `mpNext` |
| `f` (dans le nom) | pointeur de **f**onction | `mfVideoFunc`, `fHashListItemCB` |
| `Module_Fonction()` | toute fonction publique | `Video_SetMode()`, `Screen_Update()` |

Fonctions asm exportées = mêmes noms sans `_` initial spécial. Chaque `.C` a un en-tête
`FUNCTION / ACTION / CREATION : jj.mm.aa PNK`. Indentation **tabulations**.

## 3. Colonne vertébrale des dépendances

```
BASE  (types U8..S32, uU32 endian, macros, sTagString)   ← tout en dépend
  │
  ├─ MEMORY (+ HEAP.C fastbin) ── GEMDOS Malloc/Mxalloc (STRAM/TTRAM)
  ├─ SYSTEM (détection MCH/CPU/FPU/VDO/BLT, cookies, émus) ── COOKIE, MFP, XBIOS
  ├─ VBL (int niv.2 $70, compteur $466) ── MFP (timers 68901)
  │
  ├─ VIDEO (registres shifter $FF82xx) ─┐
  ├─ SCREEN (4 buffers, double-buffer) ─┼─ GRAPHIC (dispatch par mode couleur)
  ├─ BLITTER ($FF8A00) ─────────────────┘   ├─ GRF_4 (4 plans CPU+asm)
  │                                         ├─ GRF_B4 (blitter), GRF_16 (16bpp)
  │                                         └─ CHUNKY (8bpp + C2P)
  │
  ├─ ASSET/PACKAGE ── LINKFILE (archive) ── PACKER (GODPACK/ICE/…) 
  │       └─ RELOCATE (init/relocate post-load extensible)
  │
  ├─ KERNEL (boucle jeu, 4 horloges, rejeu input) ── tout le haut niveau
  └─ PLATFORM (Init/DeInit : orchestre 25+ modules à l'amorçage)
```

**Point d'entrée** : le jeu fournit `GodLib_Game_Main(argc, argv)`. GODLIB fournit le
vrai `main`/startup (MAIN/GOD_MAIN.C) adapté TOS / SDL / Win32. Séquence type d'un jeu :
`GemDos_Super(0)` → `Platform_Init()` → `Screen_Init()` → boucle
`Screen_Update()`/`IKBD_Update()` → `Platform_DeInit()` (cf. `../GODLIB.SPL/BOX`).

## 4. Motifs transversaux (reviennent dans tout le code)

- **Relocate / Delocate** : quasi chaque système sérialisable (SPRITE, FONT, FE,
  CUTSCENE, ASSET, HASHTREE, TOKENISE, REGISTRY) expose `Xxx_Delocate()` (pointeurs
  absolus → offsets relatifs, pour sauver/charger un bloc) et `Xxx_Relocate()`
  (l'inverse au chargement). C'est LE motif d'assets du moteur.
- **Table de pointeurs de fonction** = polymorphisme. `sGraphicFuncs` (11 ptrs :
  Blit, DrawBox, DrawLine…) est réassignée selon le mode couleur ET selon CPU vs
  blitter (`Graphic_SetBlitterEnable()`). Versions `mpFuncs` (sans clip) / `mpClipFuncs`.
- **Publisher/subscriber** : HASHTREE et REGISTRY — des variables nommées (hash)
  notifient des clients (`OnWrite`/`OnInit`) ; sert au binding data↔UI (GUI, FE, CUTSCENE).
- **Coroutines via `__LINE__`** : THREAD (macros `mTHREAD_WAIT_UNTIL`/`YIELD`) simule
  des continuations avec `switch(mPC)`. CUTSCENE l'utilise pour ses scripts.
- **Wrappers de trap générés** : BIOS/XBIOS/GEMDOS ont des `Module_Call_<sig>()`
  (`_W`, `_L`, `_P`, `_WW`, `_WLP`…) — une par signature d'arguments, ~165 pour XBIOS.
- **Split C / asm 68000** : le chemin chaud est en `.S` (`GRF_4_S.S`, `WIPE_S.S` 100 %
  asm, `MEMORY_S.S`, `MATHS/*`), l'API et la logique froide en `.C`. Un `.S` qui
  exporte un symbole **remplace** le repli C portable homonyme (ex. GRF_4.C vs GRF_4_S.S).
- **Multi-plateforme par `#ifdef`** : `dGODLIB_PLATFORM_ATARI` (86 sites) / `_WIN` (33) ;
  `dGODLIB_SYSTEM_TOS` / `_D3D` (Direct3D) / `_SDL`. Backends `VID_D3D.C` / `VID_SDL.C`
  derrière la même API `sVideo`.

## 5. Index des modules par cluster

Format : **MODULE** — rôle · *API/types clés* · ⇐ dépend de.

### Cœur runtime & système
- **BASE** — types portables + endian · *U8..S32, F32, uU16/uU32, mARRAY_COUNT, mSTRING_TO_U32, Endian_Twiddle* · ⇐ STRING
- **KERNEL** — boucle de jeu, 4 horloges (APP/FE/GAME/LEVEL), enregistrement/rejeu d'input, hooks CLI debug · *sKernelTask, Kernel_Init/Main, Kernel_RequestShutdown* · ⇐ ~17 modules (hub)
- **MAIN** (GOD_MAIN) — vrai `main`/startup selon plateforme · *GodLib_Game_Main, SDL_main* · ⇐ BASE, DEBUG
- **PROGRAM** — chargeur/relocateur de PRG Atari (magic $601A), lance un binaire relocaté · *sProgramHeader, sBasePage, Program_Load/Relocate/Execute* · ⇐ FILE, MEMORY (asm PROG_S.S)
- **PLATFORM** — orchestre l'amorçage (Memory→hardware→Random→…) · *Platform_Init/DeInit/Hardware_Init* · ⇐ 25+ modules (gros `#include`)
- **SYSTEM** — détection matériel très fine (MCH/CPU/FPU/DSP/VDO/MON/BLT) + détection d'émulateurs (Steem/Pacifist/TosBox via cookies) + cache 030/060 · *eSYSTEM_MCH/CPU/VDO, System_GetMCH/GetVDO/GetBLT* · ⇐ COOKIE, MFP, VBL, XBIOS
- **MEMORY** (+ **HEAP.C**) — alloc STRAM/TTRAM (Mxalloc), tracking debug (header/trailer signés), `Memory_Clear` asm (3 chemins) ; HEAP = allocateur fastbin façon ptmalloc **incomplet** (bins large/small = stubs) · *Memory_Alloc/Calloc/ScreenCalloc, mMEMALLOC* · ⇐ GEMDOS, SYSTEM, ASSERT
- **COOKIE** — accès cookie jar Atari ($5A0), no-op hors Atari · *CookieJar_Exists/GetCookieValue* · ⇐ BASE
- **CLOCK** — horloge VBL (H:M:S:µs), tables divmod pré-calculées, arithmétique asm · *sClock, sTime, Clock_Update, Time_ToU32/Add* · ⇐ VBL, SYSTEM
- **FAST** — config de build FastBuild (`.bff`), **pas de code**

### Ponts OS & matériel
- **BIOS** — trap #13 (console, disque, `Rwabs`, `Setexec`) · *Bios_Bconout/Rwabs/Getbpb, sBiosMPB* · ⇐ BASE
- **XBIOS** — trap #14 (~165 fns : son, écran, MFP, DSP 56001) · *Xbios_Vsync/Setscreen/Mfpint, Xbios_Call_\** · ⇐ BASE
- **GEMDOS** — trap #1 (~126 fns : fichiers FAT12, process, mémoire, DTA) · *GemDos_Fopen/Fread/Malloc/Pexec/Super* · ⇐ BASE
- **MFP** — timers 68901 ($FFFFFA00), 4 timers A-D, 200 Hz, 16 vecteurs IRQ · *Mfp_InstallTimerA-D, sMfpTimer* · ⇐ CLOCK
- **IKBD** — clavier/souris/joysticks/Powerpad/TeamTap (ACIA), buffers circulaires, scancodes · *IKBD_Update/GetKeyStatus/GetMouseX, eIKBDSCAN_\** · ⇐ XBIOS, VBL, SYSTEM
- **INPUT** — abstraction unifiée clavier/souris/pads/AI · *sInput, Input_Update/Combine* · ⇐ IKBD
- **VBL** — interruption verticale (vecteur $70, compteur $466), Timer B scanline, ≤64 callbacks, lock tas · *Vbl_AddCall/WaitVbl/SetVideoFunc, sVBL* · ⇐ MFP
- **EXCEPT** — handlers d'exception 68000, écran de crash (dump D0-D7/A0-A7/PC/SR) · *Except_Init/Crash, sExceptInfo* · ⇐ SYSTEM, FONT8X8
- **LINEA** — Line-A ($A000) : pixels/lignes/fonts VDI bas niveau · *LineA_Init→sLineA, LineA_PlotPixel* · ⇐ BASE
- **DRIVE** — répertoires + image disque FAT12 virtuelle, cross-platform · *Drive_CreateDirectory/SetPath, sDiskImage, sBootSector* · ⇐ GEMDOS, FILE

### Graphismes
- **GRAPHIC** — dispatch central par mode couleur (1/2/4/8 plans, 16/24/32 bpp) CPU **ou** blitter · *sGraphicCanvas, sGraphicFuncs (11 ptrs), Graphic_SetBlitterEnable, mLineOffsets[485]* · ⇐ BASE, FONT, BLITTER?, CHUNKY?
- **VIDEO** — registres shifter/palette, palette-split par ligne, save/restore, multi-machine · *Video_SetMode/SetResolution/SetPalST, sVideoPalSplitter* · ⇐ MEMORY, SYSTEM, VBL
- **SCREEN** — 4 buffers (PHYSIC/LOGIC/BACK/MISC), double-buffer (XOR d'index), align 256 o, macros `Screen_Logic_*` · *sScreenClass, Screen_Init/Update, gScreen*Graphic* · ⇐ GRAPHIC, VIDEO, VBL
- **BLITTER** — interface blitter ($FF8A00) : 16 LOP, 4 HOP, sprite/box, `Blitter_Wait` · *sBlitter, sBlitterSprite, gBlitterFlipTable* · ⇐ BASE
- **SPRITE** — sprites multi-formats (plan+masque, TrueColor, RLE, préshiftés ×16), blocs, relocation · *sSprite, sSpriteBlock, Sprite_Create/CreatePreShifted/Delocate* · ⇐ MEMORY
- **CHUNKY** — surface 8 bpp (1 octet/pixel) + conversion C2P/P2C · *ChunkySurface_Blit/To4Plane/From4Plane* · ⇐ GRAPHIC, SPRITE (optionnel `dGODLIB_CHUNKY`)
- **FADE** — fondus palette ST 16 / STE 4096 / Falcon 262144, gamma, VBL async · *Fade_PalST/PalSTE, Fade_StartVblFade* · ⇐ BASE, ASSERT
- **WIPE** — 39 transitions géométriques, **100 % asm**, halftone/LOP (1 VBL) · *Wipe_In_Init/Out_Init/Update* · ⇐ BASE
- **SCRNGRAB** — capture écran DEGAS PI3 par hotkey · *ScreenGrab_Enable/SetKeyIndex/SetDirectory* · ⇐ FILE, VIDEO, DEGAS, IKBD
- **PICTYPES** — I/O images (DEGAS/GIF/TGA/NEO), sCanvas RGBA, quantification (MedianCut, octree, k-means) · *sCanvas, sCanvasIC, Degas_ToCanvas, ColourQuantize_Octree* · ⇐ MEMORY, FILE

### Texte, UI & haut niveau
- **FONT** — polices bitmap (glyphe = sprite), kerning · *Font_Create/GetStringWidth/GetpSprite* · ⇐ SPRITE
- **FONT8X8** — police système 8×8 embarquée (debug) · *Font8x8_Print* · ⇐ BASE
- **GUI** — toolkit widgets (boutons/sliders/fenêtres), events, binding via HashTree, parse externe · *Gui_Init/Update/DataAdd, GuiButton_Select* · ⇐ ASSET, HASHTREE, IKBD, FONT (~84 KB, monolithique)
- **FE** (Fed) — front-end/menus : pages compilées, contrôles, transitions, SFX, variables jeu · *sFedPage, sFedControl, Fed_Init/SetPage* · ⇐ ASSET, FONT, SPRITE, HASHTREE, INPUT, AUDIO (+ parser 51 KB)
- **CUTSCENE** — runtime de cinématiques scriptées (35 opcodes, machine à états type coroutine, word-wrap, fades) · *sCutScene, sCutCommand, sCutSceneThread* · ⇐ ASSET, HASHTREE, FONT, SCREEN, SPRITE, FADE, AUDIO (pas de if/else dans les scripts)
- **ACHIEVE** — succès/scores/stats multi-utilisateur, persistance chiffrée + checksums (anti-triche), UI dédiée · *Achieve_Task_UnLock, Achieve_ScoreTable_SetNewScore, Achieve_Save* · ⇐ CHECKSUM, ENCRYPT, FILE, GRAPHIC, INPUT (9 fichiers, ACH_MAIN.C 57 KB)
- **THREAD** — coroutines coopératives (macros `__LINE__`), header-only · *mTHREAD_BEGIN/WAIT_UNTIL/SPAWN/SEMAPHORE* · ⇐ BASE, ASSERT
- **PROFILER** — échantillonnage d'adresses + résolution de symboles (outil interne) · *Profiler_AddHit/BuildSymbolTable* · ⇐ PROGRAM
- **UNITTEST** — micro-framework de tests (macros + RNG + garde-fous struct) · *GOD_UNIT_TEST, _EXPECT, _RAND* · ⇐ ASSERT, DEBUGLOG, RANDOM

### Audio & données/assets/IO
- **AUDIO** — YM2149 (PSG) + DMA sound STE/Falcon, mixer stéréo (pan law, tables), lecteur SSD embarqué · *Audio_DmaPlaySound, AudioMixer_PlaySample, Audio_SetVolume* · ⇐ MFP, SYSTEM, VBL
- **MUSIC** — lecteurs : SND (tracker legacy, semble mort) + **PINKNOTE** (synth YM2149, DSL 4 octets, 3 voix) · *Snd_GetInfo, PinkNote_PlayNote* · ⇐ MFP, AUDIO
- **ASSET** (PACKAGE/CONTEXT/RELOCATE) — gestion de ressources : chargement async par paquet, contextes nommés, relocation extensible par callbacks · *PackageManager_Load, Context_AssetClient_Add, Relocater_Init, sAssetItem* · ⇐ FILE, LINKFILE, MEMORY
- **PACKER** — décompression multi-format (ICE/Atomic/Auto5/Speed3) + **GODPACK** maison (LZ77B→BWT→MTF→codeur arithmétique) · *Packer_Depack, GodPack_Pack/DePack* · ⇐ MEMORY (asm décodeurs)
- **FILE** — IO fichiers cross-platform (GEMDOS/Win32/POSIX), itérateur DTA, `File_Load`, sélecteur AES · *File_Open/Read/Load/ReadFirst/Selector* · ⇐ GEMDOS, DRIVE, MEMORY
- **LINKFILE** — archive propriétaire (FAT hiérarchique), dépack paresseux, build/dump · *LinkFile_Init/FileLoad/BuildFromDirectory* · ⇐ FILE, PACKER
- **CHECKSUM** — Fletcher (intégrité, non crypto) · *CheckSum_Fletcher_Init/U8/Get* · ⇐ —
- **ENCRYPT** — obfuscation XOR table 64 o (trivial, anti-tamper léger) · *Encrypt_Scramble/DeScramble* · ⇐ —
- **REGISTRY** — config/état arborescent clé-valeur, callbacks, save/load relocatable · *Registry_VarWrite/VarClientRegister, sRegistryNode* · ⇐ MEMORY, ASSERT
- **REFLECT** — introspection/sérialisation générique de types · *sReflectType, Reflect_GetpData/SetData* · ⇐ STRING — **STUB** (parsing float absent)

### Structures de données, maths & débogage
- **STRING** (+ STRLIST/STRPATH) — chaînes statiques/dynamiques (bit MSB = flag dynamique), chemins DOS/Unix · *sString, String_Init/Append, StringPath_GetDirectory* · ⇐ MEMORY (API `String_Str*` marquée « unsafe, à déprécier »)
- **MATHS** — matrices/vecteurs 4×4 **FPU 68881/882** (Falcon), rotations Euler/axe-angle · *FMatrix_BuildRotateX/Mul/ApplyPers, FVector_Add* · ⇐ — (asm ; `BuildAxisAngle` **inachevée**)
- **VECTOR** — vecteurs entiers 16 bits (asm), sqrt itérative sans FPU · *sVector{S16 X,Y,Z}, Vector_Dot/Cross/Normal/SquaredLength* · ⇐ BASE
- **RANDOM** — LCG `s = s*69069 + 41`, germe mixé sur timer HW + vecteurs · *Random_Get/GetClamped, sRandomSeed* · ⇐ BASE
- **HASHLIST** — table de hachage chaînée plate (hash FNV-like, casse normalisée), refcount · *HashList_ItemFind/Register, HashList_BuildHash* · ⇐ MEMORY
- **HASHTREE** — variables hashées + subscribers (`OnWrite`), inline ≤4 o, blocs sérialisables relocatables · *HashTree_Var_Init/VarClient_Init/VarWrite* · ⇐ LINKLIST, MEMORY
- **LINKLIST** — listes chaînées intrusives, **macros pures** (zéro coût) · *GOD_LL_INSERT/REMOVE/FIND/MOVE_FORWARD* · ⇐ —
- **ELFHASH** — hash ELF (quasi identique à HASHLIST → doublon) · *ElfHash_BuildHash* · ⇐ BASE
- **LEXER** — tokeniseur (séparateurs en bitmap 128 bits, suivi de ligne), ASCII · *Lexer_Init/SetSeperators/GetNextToken, sLexerContext* · ⇐ STRING
- **TOKENISE** — parseur structuré + sérialisation (18 types dont fixe 8.8/16.16), args typés · *Tokeniser_Init/Serialise, TokeniserArgs_GetS32/FP32* · ⇐ HASHLIST, STRLIST, LINKLIST
- **CLI** — console debug (commandes, printf), désactivable via `dCLI` · *Cli_CmdInit/PrintfLine/Main* · ⇐ BASE
- **DEBUG** — macro `Debug_Action()` (inerte sans `dDEBUG`) · ⇐ BASE
- **DEBUGLOG** — log fichier/écran/debugger, `DebugLog_Printf0-5` · ⇐ STRING
- **ASSERT** — assertions (dialogue Windows / no-op Atari), `GODLIB_ASSERT` + `__debugbreak` · ⇐ STRING

## 6. Pièges connus (pour qui modifie GODLIB ou débogue un binaire)

1. **`OFFSET` Devpac ≠ vasm** : `OFFSET` sans argument **repart à 0** chez Devpac,
   **continue** le compteur chez vasm → offsets de struct décalés. La chaîne NeoST
   réécrit `OFFSET` → `offset 0` (cf. build.sh).
2. **ABI Pure C** : l'asm écrase d1/d2 sans les sauver. Un compilateur qui croit d2
   « callee-saved » se fait raser ses valeurs → compiler le C en `-d2scratch`.
3. **startup en premier** : un `.TOS` démarre au début du TEXT ; le makefile amont met
   les objets avant le startup (bug latent). Sans startup en tête : pas de `Mshrink`,
   pas de tas libc, `Malloc`/`malloc` renvoient 0.
4. **HEAP incomplet** : bins small/large sont des stubs — n'utiliser que MEMORY/GEMDOS.
5. **REFLECT / MATHS::BuildAxisAngle / MUSIC::SND** : inachevés ou legacy morts.
6. **mLineOffsets[] dépend du mode couleur** : incohérence = corruption écran.
7. **Alignement 256 o** de SCREEN : requis pour blitter/DMA — ne pas contourner.
8. **`Graphic_4BP_DrawLine` est exporté mais vide** dans GRF_4_S.S (ne dessine rien) :
   pour une ligne, passer par le blitter ou coder son propre tracé (cf. étalon
   `../../etalons/cube3d.c` de NeoST).
9. **ELFHASH == HASHLIST** : hash dupliqué, candidat à consolidation.
10. **#include en BACKSLASH** (`<GODLIB\X\Y.H>`) hérités de Pure C : illisibles hors
    Windows ; la chaîne NeoST les convertit `\`→`/`.

## 7. Où regarder pour…

| Besoin | Module(s) |
|---|---|
| Afficher/dessiner | SCREEN → GRAPHIC (→ GRF_4 / CHUNKY), SPRITE, FONT |
| Régler la résolution / palette | VIDEO |
| Timing / boucle / synchro trame | VBL, CLOCK, KERNEL |
| Lire clavier/souris/joystick | IKBD → INPUT |
| Son | AUDIO, MUSIC (PINKNOTE) |
| Charger des données | ASSET → LINKFILE → PACKER, FILE |
| Sauver/charger un état | REGISTRY, HASHTREE, motif Delocate/Relocate |
| Menus / cinématiques / succès | FE, CUTSCENE, ACHIEVE, GUI |
| Détecter la machine | SYSTEM (+ COOKIE) |
| Appel système brut | GEMDOS (#1), BIOS (#13), XBIOS (#14) |
| Maths 3D | MATHS (FPU Falcon), VECTOR (entier ST) |
