#!/usr/bin/env bash
# =============================================================================
#  agt-build.sh — bâtit un exemple AGT avec le cross-GCC m68k-atari-mint de NeoST
#  (équivalent, pour AGT, de dev/etalons/build.sh pour GODLIB).
#
#  Usage :  dev/agt/agt-build.sh <exemple> [cibles/vars make...]
#           dev/agt/agt-build.sh hiworld
#           dev/agt/agt-build.sh sprtest clean all
#
#  Prérequis (cf. dev/TOOLCHAIN_M68K_MINT.md) :
#    - cross-GCC dans $CROSSMINT (défaut $HOME/opt/crossmint)
#    - RMAC x86_64 dans dev/agt/bin/Linux/x86_64/rmac (bâti depuis github mwenge/rmac)
#
#  Pourquoi un wrapper : AGT (makedefs.mintgcc) vise /opt/cross-mint + GCC 4.6.4.
#  On redirige vers notre préfixe + GCC 13.4.0, et on injecte -include stdint.h
#  (GCC 13 exige les includes explicites que GCC 4.6.4 tolérait) — SANS patcher AGT.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CROSS="${CROSSMINT:-$HOME/opt/crossmint}"
GCC="$CROSS/usr/bin/m68k-atari-mint-gcc"

EX="${1:?usage: agt-build.sh <exemple> (ex. hiworld) [cibles make...]}"; shift
DIR="$ROOT/examples/$EX"
[ -d "$DIR" ] || { echo "!! exemple introuvable : $DIR" >&2; exit 1; }
[ -x "$GCC" ] || { echo "!! cross-GCC absent : $GCC (cf. dev/TOOLCHAIN_M68K_MINT.md)" >&2; exit 1; }

export PATH="$CROSS/usr/bin:$PATH"
GCCVER="$("$GCC" -dumpversion)"                      # ex. 13.4.0

# RMAC (assembleur des .s AGT) : prébâti x86_64 attendu à bin/Linux/x86_64/rmac
HOST_STUB="$("$ROOT/config.sh")"                     # -> Linux/x86_64
[ -x "$ROOT/bin/$HOST_STUB/rmac" ] || {
    echo "!! rmac absent : $ROOT/bin/$HOST_STUB/rmac" >&2
    echo "   Bâtir : git clone https://github.com/mwenge/rmac && (cd rmac && make) puis copier le binaire ici." >&2
    exit 1; }

echo ">> AGT '$EX'  |  GCC $GCCVER  |  RMAC $HOST_STUB"
make -C "$DIR" \
     TOOLCHAIN_INSTALL="$CROSS/usr" \
     TOOLCHAIN_VER="$GCCVER" \
     TARGETFLAGS="-m68000 -include stdint.h" \
     "$@"

echo ">> OK -> $DIR/build/*.prg"
ls -1 "$DIR"/build/*.prg 2>/dev/null || true
