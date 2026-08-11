#!/usr/bin/env bash
# SDL2 = couche plateforme du paquet Android (fenêtre, GLES, tactile, manettes,
# audio, cycle de vie). Vendorisé sous extern/SDL2, NON commité (comme Hatari) :
# ~50 Mo de sources qui n'ont rien à faire dans l'historique de NeoST.
set -euo pipefail
cd "$(dirname "$0")/../.."
TAG="release-2.30.9"
if [ -d extern/SDL2/.git ]; then
    echo "SDL2 déjà présent ($(git -C extern/SDL2 describe --tags 2>/dev/null || echo '?'))"
    exit 0
fi
git clone --depth 1 --branch "$TAG" https://github.com/libsdl-org/SDL.git extern/SDL2
echo "OK : SDL2 $TAG dans extern/SDL2"
