#!/usr/bin/env bash
# run_neost.sh — lance NeoST automatiquement (build au besoin puis exécution).
#
# Configure l'arbre de build s'il manque, recompile l'incrément (rapide si rien
# n'a changé), puis exécute la cible voulue. Sans argument : GUI avec la dernière
# ROM mémorisée (neost.cfg) ou EmuTOS US par défaut.
#
# Usage :
#   ./run_neost.sh                                   # GUI, ROM/disquette par défaut
#   ./run_neost.sh roms/etos192fr.img disks/diskA.st # GUI, ROM + disquette explicites
#   ./run_neost.sh --headless --frames 50 --screenshot s.ppm   # headless déterministe
#
# Variables d'env :
#   NEOST_BUILD=build   répertoire de build (défaut : build)
set -euo pipefail

# Racine du dépôt = répertoire de ce script.
cd "$(dirname "$0")"

BUILD_DIR="${NEOST_BUILD:-build}"

# Choix de la cible : --headless bascule sur neost-headless.
TARGET=neost
ARGS=()
for arg in "$@"; do
    if [ "$arg" = "--headless" ]; then
        TARGET=neost-headless
    else
        ARGS+=("$arg")
    fi
done

# Première fois ? Pas de build → setup complet (dépendances + sous-modules).
if [ ! -d "$BUILD_DIR" ]; then
    echo "==> Pas de build — lancement de ./setup_neost.sh…"
    ./setup_neost.sh
fi

# Recompile l'incrément (no-op si à jour).
echo "==> Compilation de $TARGET…"
cmake --build "$BUILD_DIR" -j --target "$TARGET"

echo "==> Lancement : $TARGET ${ARGS[*]:-}"
exec "$BUILD_DIR/$TARGET" "${ARGS[@]}"
