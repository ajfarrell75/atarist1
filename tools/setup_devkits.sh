#!/usr/bin/env bash
# =============================================================================
#  setup_devkits.sh — Rapatrie les devkits Atari (GODLIB + AGT) AUX VERSIONS
#  ÉPINGLÉES, dans des dossiers gitignorés.
#
#  POURQUOI (purge copyright 2026-08-30). `dev/reservoir-gods` et `dev/agt`
#  étaient vendorisés dans le dépôt — sans licence explicite ni droit de
#  redistribution établi (cf. TODO § BLOQUANT RELEASE). La purge les retire de
#  l'historique public ; ce script les remet en place sur une machine fraîche,
#  exactement comme `setup_hatari.sh` le fait pour l'oracle. Même contrat :
#  gitignoré, épinglé au commit, idempotent.
#
#  Les pins reprennent ceux des notes de vendorisation d'avant purge
#  (dev/reservoir-gods/SOURCES.md et dev/agt/NEOST_VENDOR.md, clones du
#  2026-07-08) : ce sont les versions avec lesquelles les étalons et les tests
#  du dépôt ont été construits.
#
#  ⚠ Usage NeoST = étude / test d'émulation. AGT n'a « aucun fichier LICENSE
#  explicite » et les dépôts Reservoir Gods n'en ont pas non plus : vérifier
#  les conditions des auteurs avant TOUTE redistribution — c'est précisément
#  pourquoi ils ne sont plus suivis par git.
#
#  Usage : tools/setup_devkits.sh
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# repo|dossier cible|pin (commit vérifié d'avant purge)
DEVKITS=(
  "https://github.com/ReservoirGods/GODLIB.git|dev/reservoir-gods/GODLIB|ae600aae192f35c8fe8a7e8164ec0e4caaa7e6e9"
  "https://github.com/ReservoirGods/GODLIB.SPL.git|dev/reservoir-gods/GODLIB.SPL|80f62d45c12550936f6513d3e2178b4b49b278b1"
  "https://github.com/ReservoirGods/TOOLS.RG.git|dev/reservoir-gods/TOOLS.RG|6e1830ae927741043240bedd46f8034092d8f396"
  "https://github.com/ReservoirGods/GAMES.RG.git|dev/reservoir-gods/GAMES.RG|4dcd5689f41208dfd621a30c39b0069f1c1a199d"
  "https://bitbucket.org/d_m_l/agtools.git|dev/agt|1139b0993c0afda2b4bb8f24df26520c2fcddad6"
)

for entry in "${DEVKITS[@]}"; do
  url="${entry%%|*}"; rest="${entry#*|}"
  dir="$ROOT/${rest%%|*}"; pin="${rest#*|}"
  if [[ -d "$dir/.git" ]]; then
    have=$(git -C "$dir" rev-parse HEAD)
    if [[ "$have" == "$pin" ]]; then
      echo "OK       ${dir#$ROOT/} (déjà au pin ${pin:0:8})"
      continue
    fi
    echo "PRESENT  ${dir#$ROOT/} à $have — checkout du pin ${pin:0:8}"
    git -C "$dir" fetch --quiet origin
  else
    echo "CLONE    ${dir#$ROOT/} ← $url"
    git clone --quiet "$url" "$dir"
  fi
  git -C "$dir" checkout --quiet "$pin"
done

echo "Devkits en place. (Gitignorés : rien de tout ceci ne doit revenir dans l'index.)"
