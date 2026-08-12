#!/usr/bin/env bash
# =============================================================================
#  build.sh — Compile la fujinet-lib ST + les démos en .TOS 68000 (vbcc).
#
#  Usage : dev/fujinet/build.sh          → gemdos/DEMOS/NWGET.TOS
#
#  Chaîne : vc/vbccm68ks + vasmm68k_mot + vlinkm68k (TOOLS.RG, cf.
#  dev/etalons/build.sh) — cible m68k-atari, ABI int16, startup16.o + libvc16.
#  Aucun GODLIB : la lib ne dépend que de tos.h/libvc.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RG="$ROOT/dev/reservoir-gods"
TOOLS="$RG/TOOLS.RG/BIN/LINUX"
TARGET="$RG/TOOLS.RG/VBCC/0.9g/vbcc_target_m68k-atari"
SRC="$ROOT/dev/fujinet"
BUILD="$SRC/build"
OUT="$ROOT/gemdos/DEMOS"

export PATH="$TOOLS:$PATH" VBCC="$TARGET"

mkdir -p "$BUILD" "$OUT"
# vbcc résout les #include locaux depuis le répertoire COURANT : on copie les
# sources dans l'arbre de build (noms en minuscules, comme les #include).
cp "$SRC/FUJINET.H" "$BUILD/fujinet.h"
cp "$SRC/FUJINET.C" "$BUILD/fujinet.c"
cp "$SRC/NWGET.C"   "$BUILD/nwget.c"

cd "$BUILD"
vc "+$TARGET/vc.config" -c -O1 fujinet.c -o fujinet.o
vc "+$TARGET/vc.config" -c -O1 nwget.c   -o nwget.o
vc "+$TARGET/vc.config" fujinet.o nwget.o -o NWGET.TOS
cp NWGET.TOS "$OUT/NWGET.TOS"
echo "OK -> $OUT/NWGET.TOS"
