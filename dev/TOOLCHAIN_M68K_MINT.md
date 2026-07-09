# Toolchain m68k-atari-mint (cross-GCC) — pour AGT / SGDL / libcmini

GODLIB (Reservoir Gods) se compile avec **vbcc** (`dev/etalons/build.sh`, chaîne
vendorisée dans `dev/reservoir-gods/TOOLS.RG`). Les **autres** libs Atari du projet
— **AGT** (`dev/agt`), **libcmini** (`dev/libcmini`), **SGDL** (OrionSoft, payant)
— utilisent au contraire un **GCC cross `m68k-atari-mint`** (a.out classique).

Ce cross-compilateur est fourni en binaires prêts à l'emploi par **Thomas Otto** :
👉 <https://tho-otto.de/crossmint.php>

## ⚠ Non vendorisé dans le dépôt

Le toolchain (~120 Mo extrait) est **spécifique à l'hôte** (binaires x86_64 Linux
ici) et **ne fonctionnerait pas** sur macOS Silicon ou une autre arch. On ne le
committe donc **pas** : chacun l'installe pour sa machine via les étapes ci-dessous
(binaires macOS / Windows / Linux dispo sur la même page).

## Installation (Linux x86_64, sans root — préfixe local)

Version installée/validée : **binutils 2.45 + GCC 13.4.0 + MiNTLib 0.60.1 + fdlibm**,
variante `-mint-` (a.out `m68k-atari-mint-*`, = préfixe attendu par AGT/libcmini).

```sh
PREFIX="$HOME/opt/crossmint"; mkdir -p "$PREFIX"
BASE=https://tho-otto.de/download/mint
for f in binutils-2.45-mint-20250812-bin-linux64.tar.xz \
         gcc-13.4.0-mint-20250702-bin-linux64.tar.xz \
         mintlib-0.60.1-mint-20240718-dev.tar.xz \
         fdlibm-20240425-mint-dev.tar.xz ; do
    curl -sSL "$BASE/$f" | tar -xJ -C "$PREFIX"       # extrait usr/… dans le préfixe
done
export PATH="$PREFIX/usr/bin:$PATH"                   # à ajouter au shell profile
m68k-atari-mint-gcc --version                         # -> 13.4.0
```

Les binaires de Thomas Otto sont **relocalisables** : GCC trouve son sysroot/libs
relativement à son propre chemin, donc le préfixe local marche sans `--sysroot`.

> **macOS** : prendre les paquets `*-macos*` de la page (ou Homebrew), même prefixe
> `m68k-atari-mint-`. **Autres GCC** : 15.2.0 / 14.3.0 / … dispo aussi ; 13.4.0
> retenu ici (stable, récent). La variante **`mintelf`** (ELF) existe mais AGT/SGDL
> ciblent le **a.out classique** → prendre `-mint-`, pas `-mintelf-`.

## Validé

- Compile+lie un `hello.c` → exécutable **Atari ST M68K** (a.out GEMDOS).
- Compile **libcmini v0.54** → `build/**/libcmini.a` + `crt0.o` (68000, 020-60,
  ColdFire, `mshort`, `mfastcall`).

## Piège GCC 13 ↔ libcmini v0.54

GCC 13 durcit certains warnings en erreurs (`-Werror=infinite-recursion` sur
l'idiome ctype de `isblank.c`, etc.). libcmini v0.54 (2020) est antérieur. Réglé
**sans toucher la source** par `dev/libcmini/Make.config.local` (fichier d'override
que le `Makefile` `-include` déjà) :

```make
CFLAGS += -Wno-error
```

## Construire libcmini

```sh
export PATH="$HOME/opt/crossmint/usr/bin:$PATH"
cd dev/libcmini && make          # -> build/**/libcmini.a, build/crt0.o
```

## Build AGT (branché & validé ✅)

AGT (`dev/agt`) est **make-driven** et attend `/opt/cross-mint` + GCC 4.6.4 ; on le
redirige vers ce toolchain via le wrapper **`dev/agt/agt-build.sh`** (l'équivalent AGT
de `build.sh` pour GODLIB) :

```sh
dev/agt/agt-build.sh hiworld          # -> dev/agt/examples/hiworld/build/hiworld.prg
```

Le wrapper : `export PATH=$CROSSMINT/usr/bin` + `make TOOLCHAIN_INSTALL=… TOOLCHAIN_VER=…`
+ **`TARGETFLAGS="-m68000 -include stdint.h"`** (GCC 13 exige les includes explicites que
GCC 4.6.4 tolérait — ici `uint32_t` dans `compress.h`), sans patcher AGT.

**RMAC** (assembleur des 47 `.s` du moteur AGT) : 3ᵉ-party **absent** des sources AGT
(seuls prebuilts `bin/Linux/aarch64` fournis). Bâti pour x86_64 et placé à
`dev/agt/bin/Linux/x86_64/rmac` (= convention AGT `bin/<hoststub>/`) :
```sh
git clone https://github.com/mwenge/rmac && (cd rmac && make)   # ELF x86_64, ~900 Ko
cp rmac/rmac dev/agt/bin/Linux/x86_64/rmac
```

**Validé** : `hiworld` compile (moteur GCC 13 + asm RMAC + tiny-CRT `LINK_MINIMAL`) →
`hiworld.prg`, qui **tourne dans NeoST** (`--machine ste --gemdos <dir> --frames 1800`,
autostart `#Z C:\HIWORLD.PRG` → écran « [A]tari [G]ame [T]ools / Hi World! »).

## Outils d'assets (graphismes) — bâtis & validés ✅

Les exemples avec graphismes (sprtest, bganim, parallax…) ont un `assets_com.sh` qui
invoque les **outils hôte AGT** (conversion PNG→sprites, compression). Prebuilts fournis
= **aarch64 seulement** → bâtis pour x86_64 (chacun s'auto-installe dans
`bin/Linux/x86_64/` via son `make`) :

| Outil | Source | Rôle |
|-------|--------|------|
| `agtcut`   | `tools/agtcut` (`make`)       | PNG → tuiles/sprites (.cct/.ems/.emx/.emh/.slab) |
| `packwrap` | `tools/packwrap` (`make`)     | enrobe/identifie les assets compressés |
| `pcs2agi`  | `tools/pcs2agi` (`make`)       | format PCS → AGI |
| `lz77`,`zx0` | `tools/3rdparty/{lz77,zx0}` (`make`) | compresseurs |
| **`lzsa`** | github **`emmanuel-marty/lzsa`** (`make CC=gcc`) | LZSA-2 (mode `lzsa`/`lzs2` de `pack.sh`) — **absent** même des prebuilts, à récupérer |

**Validé** : `sprtest` compile (agtcut convertit 6 PNG → EMS/EMX/EMH/SLAB, lzsa comprime)
→ `sprtest.prg` + assets. **Tourne dans NeoST** : charge le tileset (117 tuiles 16×16),
map 24×38, spritesheets ; menu interactif + **sprites rendus** (blitter STE) + barre raster
de perf. Config : `--machine ste --gemdos <dir : SPRTEST.PRG + assets à la racine>`,
autostart `#Z`, touches via `--keys-at` (espace = « ready », 1-4 = toggle sprites).

## Reste à faire

- **SGDL** : payant (cf. analyse) — à vendoriser dans `dev/sgdl` seulement après achat.
