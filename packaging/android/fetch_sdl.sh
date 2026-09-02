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
# REPRISE (2026-09-02) : second téléchargement sans filet du job android, après
# `sdkmanager`. Un clone interrompu laisse un extern/SDL2 PARTIEL que le test
# `-d extern/SDL2/.git` en tête ne rattraperait pas au tour suivant — on purge donc
# avant chaque nouvelle tentative. Même motif que la reprise de release.yml.
ok=0
for try in 1 2 3; do
    if git clone --depth 1 --branch "$TAG" https://github.com/libsdl-org/SDL.git extern/SDL2; then
        ok=1; break
    fi
    echo "clone SDL2 échoué (tentative $try/3) — purge puis nouvelle tentative" >&2
    rm -rf extern/SDL2
    sleep $(( try * 10 ))
done
[ "$ok" = 1 ] || { echo "clone SDL2 échoué 3 fois de suite — ce n'est plus un aléa" >&2; exit 1; }
echo "OK : SDL2 $TAG dans extern/SDL2"
