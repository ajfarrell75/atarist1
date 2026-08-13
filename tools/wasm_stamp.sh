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
#  « Déterministe » impose trois précautions, apprises d'une CI rouge alors que
#  RIEN n'avait bougé — l'empreinte écrite sur le poste macOS ne retombait pas
#  sur celle recalculée par le runner Linux :
#    · la liste des fichiers vient de `git ls-files`, PAS de `find` : git ne
#      voit que le SUIVI (un brouillon oublié dans src/ ne décale plus rien) et
#      trie par octets, indépendamment de la locale — `sort` en en_US.UTF-8
#      (macOS) et en C (CI) ne classent PAS pareil dès qu'un « / » ou un « _ »
#      départage deux chemins ;
#    · l'empreinte de chaque fichier est recomposée ICI (« hash<espace>chemin »)
#      au lieu de recopier la sortie de l'outil : sha256sum, shasum et openssl
#      la formatent chacun à leur façon ;
#    · l'outil de hachage est choisi parmi les trois, macOS n'ayant pas
#      sha256sum sans coreutils.
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

# Outil de hachage, choisi une fois pour toutes.
if command -v sha256sum >/dev/null 2>&1; then
    SHA_CMD=(sha256sum)
elif command -v shasum >/dev/null 2>&1; then
    SHA_CMD=(shasum -a 256)
elif command -v openssl >/dev/null 2>&1; then
    SHA_CMD=(openssl dgst -sha256 -r)
else
    echo "ERREUR : aucun outil sha256 disponible (sha256sum, shasum, openssl)." >&2
    exit 1
fi

# sha256 de l'entrée standard, en hexadécimal nu. Les trois outils impriment
# « empreinte <séparateur> nom » : on ne garde que le premier champ.
sha256_stdin() {
    "${SHA_CMD[@]}" | cut -d' ' -f1
}

# Tout ce qui entre dans le bundle : le cœur, le frontend web, la coque HTML et
# les options de build. PAS les ROM/disquettes embarquées (elles changent
# rarement et alourdiraient inutilement le calcul).
compute() {
    if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        echo "ERREUR : hors d'un dépôt git — la liste des sources vient de git ls-files." >&2
        exit 1
    fi
    while IFS= read -r -d '' f; do
        case "$f" in
            src/*.cpp|src/*.hpp|src/*.h|web/shell.html|CMakeLists.txt)
                printf '%s %s\n' "$(sha256_stdin < "$f")" "$f" ;;
        esac
    done < <(git ls-files -z -- src web/shell.html CMakeLists.txt) | sha256_stdin
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

Sans emsdk : le job « wasm » de la CI téléverse le bundle qu'il vient de construire
(artefact NeoST-web-wasm) MÊME quand ce contrôle échoue — dézipper les quatre
fichiers index.* dans wasm/, puis --write.
EOF
            exit 1
        fi
        echo "OK : bundle wasm/ à jour ($CUR)"
        ;;
    *)
        printf '%s\n' "$CUR"
        ;;
esac
