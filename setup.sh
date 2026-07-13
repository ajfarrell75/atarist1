#!/usr/bin/env bash
# setup.sh — installe TOUTES les dépendances de NeoST puis compile.
#
# Étapes :
#   1) paquets système : GLFW3 + OpenGL + CMake + toolchain C++17  (brew/apt/pacman/dnf)
#   2) sous-modules     : extern/imgui, extern/miniaudio
#   3) configuration CMake (Release par défaut) + compilation des 3 cibles
#
# Le cœur Moira (extern/moira) n'est PAS un sous-module : il est vendorisé
# (copié dans le dépôt, avec le patch NEOST_IPLFETCH) — cf. extern/moira/NEOST_VENDOR.md.
# Il arrive donc déjà avec le clone NeoST, rien à télécharger.
#
# Usage :
#   ./setup.sh            # build Release complet
#   ./setup.sh --debug    # build Debug
#   ./setup.sh --no-deps  # saute l'installation des paquets système
set -euo pipefail

# Racine du dépôt = répertoire de ce script (marche depuis n'importe où).
cd "$(dirname "$0")"

BUILD_TYPE=Release
INSTALL_DEPS=1
for arg in "$@"; do
    case "$arg" in
        --debug)   BUILD_TYPE=Debug ;;
        --release) BUILD_TYPE=Release ;;
        --no-deps) INSTALL_DEPS=0 ;;
        -h|--help) grep '^#' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
        *) echo "Option inconnue : $arg" >&2; exit 2 ;;
    esac
done

say()  { printf '\033[1;36m==>\033[0m %s\n' "$*"; }
warn() { printf '\033[1;33m/!\\\033[0m %s\n' "$*" >&2; }

# ---------------------------------------------------------------------------
# 1) Dépendances système (GLFW3 + OpenGL + CMake + compilateur C++17).
# ---------------------------------------------------------------------------
if [ "$INSTALL_DEPS" -eq 1 ]; then
    say "Installation des dépendances système (GLFW3, CMake, toolchain)…"
    if command -v brew >/dev/null 2>&1; then
        brew list glfw  >/dev/null 2>&1 || brew install glfw
        brew list cmake >/dev/null 2>&1 || brew install cmake
    elif command -v pacman >/dev/null 2>&1; then
        sudo pacman -S --needed --noconfirm glfw cmake base-devel
    elif command -v apt-get >/dev/null 2>&1; then
        sudo apt-get update
        sudo apt-get install -y libglfw3-dev libgl1-mesa-dev cmake build-essential git
    elif command -v dnf >/dev/null 2>&1; then
        sudo dnf install -y glfw-devel mesa-libGL-devel cmake gcc-c++ git
    else
        warn "Gestionnaire de paquets non reconnu — installez GLFW3 + OpenGL + CMake à la main."
    fi
fi

# ---------------------------------------------------------------------------
# 2) Sous-modules : imgui + miniaudio. (Moira est vendorisé, pas un sous-module.)
# ---------------------------------------------------------------------------
say "Initialisation des sous-modules imgui + miniaudio…"
git submodule update --init extern/imgui extern/miniaudio

# Moira est vendorisé : il doit déjà être là. Sinon, le dépôt est incomplet.
if [ ! -f extern/moira/Moira/Moira.cpp ]; then
    warn "extern/moira/Moira/Moira.cpp absent — Moira est vendorisé, restaurez-le :"
    warn "  git checkout -- extern/moira   (ou réclonez le dépôt NeoST)."
fi

# ---------------------------------------------------------------------------
# 3) Configuration + compilation des 3 cibles (neost, neost-headless, lib).
# ---------------------------------------------------------------------------
say "Configuration CMake ($BUILD_TYPE)…"
cmake -B build -DCMAKE_BUILD_TYPE="$BUILD_TYPE"

say "Compilation (neost, neost-headless, neost_core)…"
cmake --build build -j

say "Terminé. Lance l'émulateur avec : ./run.sh"
