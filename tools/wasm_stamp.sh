#!/usr/bin/env bash
# =============================================================================
#  Empreinte des SOURCES qui déterminent le bundle WebAssembly.
#
#  Pourquoi une empreinte de sources et pas un diff du bundle : depuis que
#  GitHub Pages est repassé en « deploy from a branch », la démo en ligne EST le
#  dossier wasm/ commité. Un oubli de reconstruction met donc en ligne une démo
#  périmée, en silence — c'est exactement ce qui avait laissé wasm/index.html
#  sur une coque française pendant des semaines. Il faut donc une garde.
#
#  Mais comparer le bundle OCTET À OCTET ne marche pas : emcc n'est pas
#  reproductible d'une version d'emsdk à l'autre, et la CI n'a pas forcément la
#  même que le poste de dev — la garde échouerait en permanence, pour rien.
#  On compare donc ce qui, lui, est déterministe : le contenu des sources.
#
#  Usage :
#      tools/wasm_stamp.sh            # imprime l'empreinte
#      tools/wasm_stamp.sh --write    # l'écrit dans wasm/SOURCE_STAMP
#      tools/wasm_stamp.sh --check    # compare, sort ≠ 0 si périmé
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

STAMP_FILE="wasm/SOURCE_STAMP"

# Tout ce qui entre dans le bundle : le cœur, le frontend web, la coque HTML et
# les options de build. PAS les ROM/disquettes embarquées (elles changent
# rarement et alourdiraient inutilement le calcul).
compute() {
    {
        find src -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' \) -print0 | sort -z | xargs -0 sha256sum
        sha256sum web/shell.html CMakeLists.txt
    } | sha256sum | cut -d' ' -f1
}

CUR="$(compute)"

case "${1:-}" in
    --write)
        mkdir -p wasm
        printf '%s\n' "$CUR" > "$STAMP_FILE"
        echo "empreinte écrite : $CUR"
        ;;
    --check)
        if [ ! -f "$STAMP_FILE" ]; then
            echo "ERREUR : $STAMP_FILE absent — le bundle wasm/ commité n'est pas traçable." >&2
            echo "         Reconstruire puis : tools/wasm_stamp.sh --write" >&2
            exit 1
        fi
        OLD="$(cat "$STAMP_FILE")"
        if [ "$CUR" != "$OLD" ]; then
            cat >&2 <<EOF
ERREUR : le bundle wasm/ commité est PÉRIMÉ.
         empreinte des sources : $CUR
         empreinte de wasm/    : $OLD

GitHub Pages sert le dossier wasm/ DU DÉPÔT (build_type=legacy, main/(root)) :
tant que ce dossier n'est pas reconstruit, la démo en ligne reste l'ancienne.

  source /chemin/vers/emsdk/emsdk_env.sh
  emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release -DNEOST_WEB_FREE_ONLY=ON
  cmake --build build-web -j --target neost-web
  tools/wasm_stamp.sh --write
  git add wasm/ && git commit
EOF
            exit 1
        fi
        echo "OK : bundle wasm/ à jour ($CUR)"
        ;;
    *)
        printf '%s\n' "$CUR"
        ;;
esac
