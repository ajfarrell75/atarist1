# Automatiser Hatari (oracle de référence)

Hatari est la **source de vérité matérielle** de NeoST (cf. `CLAUDE.md`). Au-delà de la
lecture des sources (`extern/hatari/src`), on peut **exécuter** Hatari de façon
déterministe et **sans affichage** pour comparer son comportement à NeoST (boot, écran,
détection HW, IRQ). Ce doc note la recette vérifiée (Hatari v2.6.1, macOS Silicon, juin 2026).

## Se procurer l'oracle (rien ne le fait à votre place)

⚠ `extern/hatari` est **gitignoré et n'est PAS un sous-module** : `git clone` du dépôt
NeoST ne le ramène pas, `git submodule update` non plus, et **aucun script d'installation
ne s'en occupe**. Sur une machine fraîche (ou après un ménage), il est simplement ABSENT —
c'était le cas ici le 2026-08-19, alors que `CLAUDE.md` et ce document le décrivaient
comme « bâti dans le dépôt ». Le récupérer et le bâtir :

```sh
git clone --depth 1 https://framagit.org/hatari/hatari.git extern/hatari
cmake -S extern/hatari -B extern/hatari/build -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES=arm64 -DENABLE_OSX_BUNDLE=0        # macOS Silicon
cmake --build extern/hatari/build -j                                # → build/src/hatari
```

Les deux options macOS ne sont pas cosmétiques (mesuré le 2026-08-19, Hatari
v2.6.1-devel, macOS 15 / Silicon) :
- **sans `-DCMAKE_OSX_ARCHITECTURES=arm64`**, la configuration vise **x86_64** et l'édition
  de liens échoue sur `_png_write_row… symbol(s) not found for architecture x86_64` (les
  bibliothèques Homebrew sont arm64) ;
- **sans `-DENABLE_OSX_BUNDLE=0`**, la cible est un `Hatari.app` dont l'étape finale lance
  `ibtool` sur un XIB : celui-ci part en `Abort trap: 6` (« A required plugin failed to
  load »), et make **supprime le binaire déjà lié**. Avec `=0`, on obtient exactement le
  chemin qu'attend l'outillage : `extern/hatari/build/src/hatari`.

Sous Linux (CachyOS / Ubuntu) les deux options sont inutiles ; dépendance : `libsdl2-dev`.
`ffmpeg` et `imagemagick` sont requis pour extraire les images (`tools/hatari_oracle.sh`).

Binaire selon la machine :
- **macOS** : `extern/hatari/build/src/hatari` (recette ci-dessus), ou
  `/opt/homebrew/bin/hatari` si le paquet Homebrew est installé — il ne l'est PAS par
  défaut. ⚠ Pas de `timeout` — on s'appuie sur `--run-vbls` qui fait sortir Hatari seul.
- **Linux (CachyOS / Ubuntu)** : `extern/hatari/build/src/hatari` (v2.6.1, aligné sur la
  source de vérité du repo, plus récent que le `hatari` d'apt en 2.4.1).
  ⚠ Un ancien symlink `~/.local/bin/hatari` a pu rester CASSÉ (chemin `src/NeoST/…`, casse
  différente) : invoquer le binaire **par son chemin**, ou le réparer avec
  `ln -sf "$PWD/extern/hatari/build/src/hatari" ~/.local/bin/hatari`.
  `timeout` y est disponible et conseillé en plus de `--run-vbls`.
  💡 `tools/hatari_oracle.sh` fait cette découverte de binaire toute seule — le préférer
  aux invocations manuelles.

## ⚠ Hatari n'est PAS déterministe d'un run à l'autre (et ça change la méthode)

`sdl/main_sdl.c` fait `Hatari_srand(time(NULL))`. Ce RNG alimente notamment :

| Site Hatari | Ce qui est tiré au hasard |
|-------------|---------------------------|
| `fdc.c` (`IndexPulse_Time = … - Hatari_rand() % FdcCyclesPerRev`) | **position angulaire initiale de la disquette** au démarrage |
| `video.c:1155` | wakeup state MMU/GLUE **si** `--video-timing random` (défaut : `ws3`, donc figé) |
| `mfp.c:1392` | jitter de timer (« for wod2 ») |
| `ikbd.c` | délai de réponse de l'IKBD à l'ACIA |

Conséquence pratique, mesurée le 2026-08-19 sur `cuddly_demos` : **deux runs avec la
MÊME ligne de commande ne donnent pas la même image au même numéro de trame** dès que le
programme boote d'une disquette — la durée du boot varie, et tout le film glisse. Mesure :
la trame NeoST 3499 tombait sur la trame Hatari **3560** dans un run et **3497** dans un
autre (deux runs lancés à quelques secondes d'écart sont, eux, identiques : le sel est
l'horloge en secondes). C'est ce qui avait fait conclure à tort, le 2026-08-01, que
NeoST rendait Cuddly « à une phase d'animation différente » : le balayage à `frame:` figé
ne pouvait pas converger. En réalité NeoST rend cette démo **byte-identique** à Hatari
(0 px sur 220 trames consécutives).

**Règle** : pour tout étalon qui boote un disque, ne JAMAIS épingler un numéro de trame
oracle. Utiliser `oracle_scan: N` dans `tools/etalons.json` — `run_etalons.py --oracle`
extrait alors la fenêtre `[frame-N, frame+N]` (via `HATARI_ORACLE_SCAN` que comprend
`hatari_oracle.sh`) et retient la trame **identique** à la capture NeoST, jamais la moins
pire ; s'il n'y en a aucune, il le dit et échoue — c'est alors une vraie divergence.
Côté NeoST rien de tout ceci ne se pose : l'émulation est déterministe, et une référence
une fois commise se compare de façon reproductible.

## Recette headless : boot → image PNG

```sh
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy   # aucune fenêtre / audio (CI, headless)
HATARI=extern/hatari/build/src/hatari                # cf. ci-dessus : PAS sur le PATH
"$HATARI" --machine megaste --tos roms/etos256us.img --monitor rgb \
       --sound off --fast-forward on --confirm-quit off --statusbar off \
       --alert-level fatal \
       --run-vbls 400 \
       --avirecord --avi-vcodec png --avi-file /tmp/h.avi
# Extraire une frame (ffmpeg dispo via Homebrew). N = n° de frame ; ~60 fps dans l'AVI.
ffmpeg -y -i /tmp/h.avi -vf "select=eq(n\,300)" -frames:v 1 -update 1 /tmp/h.png
```

- **`--avirecord` capture l'écran ÉMULÉ** (pas la fenêtre hôte) → marche avec
  `SDL_VIDEODRIVER=dummy`. C'est la clé du headless : pas besoin d'un display ni d'envoyer
  la touche screenshot. `--avi-vcodec png` = frames PNG sans perte dans un conteneur AVI.
- Choisir une frame **du milieu** (pas la dernière : les toutes dernières frames peuvent
  être noires/transition de sortie). L'AVI fait la taille de l'overscan (ex. 832×552) avec
  de larges bordures noires ; le contenu utile est au centre.
- `--alert-level fatal` : **indispensable** — sinon Hatari ouvre des **dialogues GUI
  bloquants** (ex. l'avertissement TOS≤1.4, cf. piège ci-dessous) qui figent l'exécution.
- `--run-vbls N` : exécute N VBL (≈ N/50 s de temps ST en PAL) puis quitte proprement.
  `--fast-forward on` accélère (ne change pas le nombre de VBL émulées).

## Autres signaux (sans image)

- `--conout 2` : redirige la **console EmuTOS/VT-52** vers stdout — utile pour suivre le
  boot (messages, panics) sans image.
- `--trace <flags>` (`--trace help` pour la liste) + `--trace-file FILE` : trace CPU /
  IRQ / vidéo… façon MAME. Comparable aux traces NeoST headless (`trace_diff.py`).
- `--parse FILE` : exécute des commandes du **débogueur** intégré (points d'arrêt, dump
  mémoire/registres après N cycles) → introspection scriptée.
- `--log-file FILE`, `--log-level info|warn|...`.

### Injection d'entrée headless (`--cmd-fifo`) — oracle des MENUS / IN-GAME

Le build local (v2.6.1) supporte `--cmd-fifo <path>` : Hatari **crée** la fifo et lit des
commandes runtime, dont **`hatari-event keypress <scancode>`** (SPACE=57, ENTER=28). Ça
débloque l'oracle des scènes qui exigent une touche (menu Cuddly, démos) — jadis noté
« impossible » dans le TODO. Pièges vérifiés :
- Hatari **bloque** à l'ouverture de la fifo en lecture jusqu'à ce qu'un writer s'y connecte
  → ouvrir le writer (`exec 3>fifo`) AVANT que Hatari ne tourne.
- `--cmd-fifo` **désactive le fast-forward** → Hatari tourne en TEMPS RÉEL (~50 vbl/s). Donc
  une touche au « titre » (vbl ~1500) s'envoie à **~30 s** réelles, pas après quelques sleeps.
- Joystick : pas d'event direct ; passer par `--joystick <port>` (touches curseur) + une
  touche de tir, ou injecter les scancodes.
- ⚠ **`keypress` (make+break instantanés) peut être IGNORÉ** par un poll clavier de démo
  (vérifié : menu Cuddly insensible au `keypress 57`). Recette fiable = appui TENU :
  `keydown 57`, sleep 0.5, `keyup 57`. Tester la livraison de la fifo avec une commande
  invalide (`keypress zz`) → l'ERROR dans le log prouve la réception.

```sh
FIFO=/tmp/h.fifo; rm -f "$FIFO"
hatari ... --cmd-fifo "$FIFO" --run-vbls 3200 --avirecord --avi-file out.avi &
while [ ! -p "$FIFO" ]; do sleep 0.05; done
exec 3>"$FIFO"                                  # débloque Hatari
sleep 30; for k in $(seq 12); do echo "hatari-event keypress 57" >&3; sleep 0.3; done
exec 3>&- ; wait
```

## Options machine utiles

| Option | Effet |
|--------|-------|
| `--machine st\|megast\|ste\|megaste\|tt\|falcon` | profil matériel |
| `--tos <file>` | image TOS/EmuTOS |
| `--cpulevel <0..>` | type 680x0 (EmuTOS/TOS 2.06 seulement) |
| `--monitor mono\|rgb\|vga\|tv` | type moniteur (mono = haute rés) |
| `--country <x>` | code pays pour EmuTOS multi-langue |
| `--fast-boot on` | patche TOS/memvalid pour booter plus vite |

## Pièges vérifiés

- **TOS ≤ 1.4 → forcé en mode ST.** Hatari lit la version dans l'en-tête TOS ; un EmuTOS
  **192 Ko** (`etos192*.img`) se présente en **« TOS 1.4 / Atari ST »** et Hatari **refuse**
  de le lancer en MegaSTE/TT (« TOS versions <= 1.4 work only in ST mode », bascule auto en
  ST). Pour MegaSTE il faut un **EmuTOS 256 Ko** (`etos256us/fr.img`, qui se présente
  « Atari Mega STe ») ou un TOS 2.05/2.06. C'est ainsi qu'on a tranché la question du SCU
  (cf. `CHANGELOG` : EmuTOS 256K **programme** le SCU comme TOS 2.06).
- **`--avirecord` peut exiger un booléen explicite** : sur le build Linux du sous-module
  (v2.6.1-devel), la forme drapeau `--avirecord` échoue (« Usage: … ») — écrire
  `--avirecord on`. La forme drapeau passe sur le binaire Homebrew macOS.
- **Oracle AUDIO** : l'AVI embarque la piste son → `ffmpeg -i h.avi -vn -acodec pcm_s16le h.wav`
  donne le WAV de référence (48 kHz via `--sound 48000`). ⚠ Hatari applique un HPF sous-sonique
  + IIR LMC au mix : les métriques à composante continue ne sont PAS comparables à NeoST (pas
  de HPF sur le canal DMA) — comparer des ratios de contenu (fenêtres, spectres), ou mieux :
  `--trace dmasound` logge chaque fetch FIFO (« DMA snd fifo refill adr=… ») à diff-er contre
  `NEOST_DMASND_TRACE=1` côté NeoST (cf. `DEV.md`).
- **`--avirecord` exige `--avi-file`** ; sans `--avi-vcodec png` le défaut peut être un
  codec moins pratique à décoder.
- Au **premier lancement** Hatari crée `~/Library/Application Support/Hatari/` (config,
  NVRAM). Un `INFO : NVRAM not found` au boot est normal.

## Récupérer un EmuTOS récent (libre, GPL)

```sh
curl -sL -o /tmp/e.zip "https://downloads.sourceforge.net/project/emutos/emutos/1.4/emutos-256k-1.4.zip"
unzip -o /tmp/e.zip -d /tmp/e && cp /tmp/e/emutos-256k-1.4/etos256us.img roms/
```
Le paquet `256k` contient toutes les langues (`etos256us.img`, `etos256fr.img`, …). Le
paquet `192k` est pour ST/STE (TOS 1.x), le `256k` pour Mega ST/STE/TT/Falcon (TOS 2.x/3.x).
