#!/usr/bin/env bash
# =============================================================================
#  build_native_pi.sh — Compilation NATIVE de NeoST sur le Raspberry Pi.
#
#  POURQUOI plutôt que l'AppImage `NeoST-*-raspberry-aarch64.AppImage` :
#  l'AppImage est bâtie en aarch64 GÉNÉRIQUE (elle doit tourner du Pi 3 au Pi 5).
#  Ici on compile avec -mcpu=<le cœur exact>, ce qui laisse GCC utiliser le jeu
#  d'instructions et le modèle de coût du cœur réel. La boucle chaude de NeoST
#  est l'interpréteur Moira (un switch géant sur l'opcode) : c'est exactement le
#  genre de code qui profite du bon -mtune. Compter ~10-20 % de trames/s en plus.
#
#  Usage (SUR le Pi, dans une copie du dépôt) :
#      packaging/raspberry/build_native_pi.sh              # build → build-pi/
#      sudo packaging/raspberry/build_native_pi.sh --install   # + installe /opt/neost
#      NEOST_LTO=1 packaging/raspberry/build_native_pi.sh  # + LTO (build long)
#
#  --install copie le binaire ET les données (roms/, disks/, fonts/, gemdos/)
#  dans /opt/neost, la disposition qu'attend le service kiosk :
#      /opt/neost/bin/neost        ← exeDir
#      /opt/neost/roms/…           ← resolveData() essaie exeDir/../<chemin>
#      /opt/neost/neost.cfg        ← cfgPath() = exeDir + "/../neost.cfg"
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

PREFIX="${NEOST_PREFIX:-/opt/neost}"
BUILD_DIR="${NEOST_BUILD_DIR:-build-pi}"
DO_INSTALL=0
[ "${1:-}" = "--install" ] && DO_INSTALL=1

# --- 1. Identifier le cœur ---------------------------------------------------
# Le modèle est dans /proc/device-tree/model ("Raspberry Pi 4 Model B Rev 1.5").
# On ne se fie PAS à -mcpu=native seul : sur certains noyaux 64 bits le MIDR lu
# par GCC est incomplet et la détection retombe sur un générique silencieux.
MODEL="$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || echo inconnu)"
case "$MODEL" in
    *"Raspberry Pi 5"*)            MCPU=cortex-a76 ;;
    *"Raspberry Pi 4"*|*"Pi 400"*) MCPU=cortex-a72 ;;
    *"Raspberry Pi 3"*)            MCPU=cortex-a53 ;;
    *)                             MCPU=native ;;
esac
# Garde-fou : si le compilateur refuse ce -mcpu (GCC trop ancien), on retombe
# sur générique plutôt que d'échouer 20 minutes plus tard sur un .cpp au hasard.
if ! echo 'int main(){}' | ${CXX:-g++} -x c++ -mcpu=$MCPU -o /dev/null - 2>/dev/null; then
    echo "[build_native_pi] AVERTISSEMENT : -mcpu=$MCPU refusé par $(${CXX:-g++} --version | head -1) → générique"
    MCPU=""
fi

ARCH_FLAGS=""
[ -n "$MCPU" ] && ARCH_FLAGS="-mcpu=$MCPU -mtune=$MCPU"

# LTO : encore ~5 % sur Moira mais le lien monte à plusieurs minutes sur un Pi 4
# et demande ~1,5 Go de RAM. Opt-in.
IPO_ARG=()
[ "${NEOST_LTO:-0}" = "1" ] && IPO_ARG=(-DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON)

echo "[build_native_pi] modèle : $MODEL"
echo "[build_native_pi] flags  : ${ARCH_FLAGS:-<génériques>} ${NEOST_LTO:+(+LTO)}"

# --- 2. Compiler -------------------------------------------------------------
# ⚠ On passe par CMAKE_CXX_FLAGS et PAS par CMAKE_CXX_FLAGS_RELEASE : le
# CMakeLists écrase ce dernier sans condition (set(... "-O3 -DNDEBUG")), un
# override en ligne de commande y serait perdu.
cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_C_FLAGS="$ARCH_FLAGS" \
      -DCMAKE_CXX_FLAGS="$ARCH_FLAGS" \
      "${IPO_ARG[@]}"

# Un Pi 4 a 4 cœurs mais souvent 2-4 Go : -j4 sur du C++17 lourd part en OOM-kill
# (symptôme : « c++: fatal error: Killed signal terminated program cc1plus »).
JOBS="${NEOST_JOBS:-}"
if [ -z "$JOBS" ]; then
    MEM_MB=$(($(awk '/MemTotal/{print $2}' /proc/meminfo) / 1024))
    JOBS=$(( MEM_MB / 900 )); [ "$JOBS" -lt 1 ] && JOBS=1
    NPROC=$(nproc); [ "$JOBS" -gt "$NPROC" ] && JOBS=$NPROC
fi
echo "[build_native_pi] compilation avec -j$JOBS"
cmake --build "$BUILD_DIR" -j"$JOBS"

echo "[build_native_pi] OK : $BUILD_DIR/neost"

# --- 3. Installer (optionnel) ------------------------------------------------
if [ "$DO_INSTALL" = "1" ]; then
    [ "$(id -u)" -eq 0 ] || { echo "ERREUR : --install demande root (sudo)"; exit 1; }
    install -d "$PREFIX/bin"
    install -m 755 "$BUILD_DIR/neost" "$BUILD_DIR/neost-headless" "$PREFIX/bin/"
    # Données : on NE remplace pas ce qui existe déjà (les disquettes ajoutées par
    # l'exploitant de la borne survivent à une mise à jour du binaire).
    for d in roms disks fonts gemdos carts; do
        [ -d "$ROOT/$d" ] || continue
        install -d "$PREFIX/$d"
        cp -rn "$ROOT/$d/." "$PREFIX/$d/" 2>/dev/null || true
    done
    echo "[build_native_pi] installé dans $PREFIX (binaire + données)"
    echo "[build_native_pi] étape suivante : packaging/raspberry/install_kiosk.sh"
fi
