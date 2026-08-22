#!/usr/bin/env bash
# =============================================================================
#  build.sh — Compile les démos ST des extensions RÉSEAU en .TOS 68000 (vbcc).
#
#  Usage : dev/netdemo/build.sh          → gemdos/DEMOS/{MIDITEST,MODMTEST}.TOS
#
#    · MIDITEST.TOS — anneau MIDI (--midi-net) : émet 10 octets sur MIDI OUT et
#      relit ce qui revient par MIDI IN (round-trip OUT→UDP→pair→IN).
#    · MODMTEST.TOS — modem Hayes (--modem) : commandes AT sur l'USART du MFP.
#
#  Chaîne : vc/vbccm68ks + vasmm68k_mot + vlinkm68k (TOOLS.RG, cf.
#  dev/etalons/build.sh) — cible m68k-atari, ABI int16, startup16.o + libvc16.
#  Aucun GODLIB : ces programmes ne dépendent que de tos.h/libvc.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
RG="$ROOT/dev/reservoir-gods"
TOOLS="$RG/TOOLS.RG/BIN/LINUX"
TARGET="$RG/TOOLS.RG/VBCC/0.9g/vbcc_target_m68k-atari"
SRC="$ROOT/dev/netdemo"
BUILD="$SRC/build"
OUT="$ROOT/gemdos/DEMOS"

export PATH="$TOOLS:$PATH" VBCC="$TARGET"

mkdir -p "$BUILD" "$OUT"
# vbcc résout les #include locaux depuis le répertoire COURANT : on copie les
# sources dans l'arbre de build (noms en minuscules, comme les #include).
cp "$SRC/MIDITEST.C" "$BUILD/miditest.c"
cp "$SRC/MODMTEST.C" "$BUILD/modmtest.c"

cd "$BUILD"
for prog in miditest modmtest; do
    vc "+$TARGET/vc.config" -c -O1 "$prog.c" -o "$prog.o"
    vc "+$TARGET/vc.config" "$prog.o" -o "$(echo "$prog" | tr a-z A-Z).TOS"
    cp "$(echo "$prog" | tr a-z A-Z).TOS" "$OUT/"
    echo "OK -> $OUT/$(echo "$prog" | tr a-z A-Z).TOS"
done
