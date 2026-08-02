#!/usr/bin/env bash
# =============================================================================
#  build_in_bookworm_pi.sh — Binaires de la BORNE, à lancer DANS un conteneur
#  debian:bookworm arm64 (dépôt monté sur /work) :
#
#    docker run --rm -v "$PWD":/work -w /work -e NEOST_MCPU=cortex-a72 \
#        debian:bookworm bash /work/packaging/raspberry/build_in_bookworm_pi.sh
#
#  Différences avec `packaging/linux/build_in_bookworm_arm64.sh` (l'AppImage de
#  release), et pourquoi ce second script existe :
#
#   · -mcpu=<cœur exact> au lieu d'aarch64 générique. L'AppImage publiée doit
#     tourner du Pi 3 au Pi 5 ; la borne, elle, ne tourne QUE sur son Pi. La
#     boucle chaude de NeoST est l'interpréteur Moira — c'est exactement le
#     genre de code qui profite du bon modèle de coût (~10-20 %).
#   · pas d'AppImage : un simple tar.gz de binaires. Une AppImage v2 réclame
#     libfuse2, absent de Pi OS Lite bookworm — la borne devrait l'extraire à
#     chaque démarrage pour rien.
#
#  Pourquoi bookworm : Raspberry Pi OS EST Debian bookworm (glibc 2.36). Bâtir
#  sur le runner ubuntu-24.04-arm estampillerait GLIBC_2.39 et le binaire ne
#  démarrerait sur aucun Pi.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

MCPU="${NEOST_MCPU:-cortex-a72}"     # cortex-a72 = Pi 4 / Pi 400 ; cortex-a76 = Pi 5

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y --no-install-recommends \
    cmake g++ make binutils file ca-certificates libglfw3-dev libgl1-mesa-dev

echo "[build_in_bookworm_pi] cible : -mcpu=$MCPU"
echo 'int main(){}' | g++ -x c++ -mcpu="$MCPU" -o /dev/null - \
    || { echo "ERREUR : -mcpu=$MCPU refusé par $(g++ --version | head -1)"; exit 1; }

# ⚠ CMAKE_CXX_FLAGS et PAS CMAKE_CXX_FLAGS_RELEASE : le CMakeLists écrase ce
# dernier sans condition, un override en ligne de commande y serait perdu.
# -static-libstdc++/-static-libgcc : ne dépendre QUE de la glibc de bookworm,
# pas du libstdc++ de l'image de build.
cmake -B build-borne -DCMAKE_BUILD_TYPE=Release \
    -DNEOST_VERSION_STR="${NEOST_VERSION:-borne}" \
    -DCMAKE_C_FLAGS="-mcpu=$MCPU -mtune=$MCPU" \
    -DCMAKE_CXX_FLAGS="-mcpu=$MCPU -mtune=$MCPU" \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
cmake --build build-borne -j"$(nproc)"

test -x build-borne/neost || { echo "ERREUR : frontend GUI non construit (GLFW ?)"; exit 1; }

# --- Vérifications qui doivent échouer ICI, pas sur la borne -----------------
readelf -h build-borne/neost | grep -q AArch64 \
    || { echo "ERREUR : pas un binaire AArch64"; exit 1; }
# Plancher glibc : le symbole GLIBC_x.y le plus haut exigé doit rester <= 2.36,
# sinon Pi OS refusera de lancer le binaire (« version `GLIBC_2.39' not found »).
MAX=$(objdump -T build-borne/neost 2>/dev/null | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sort -V | tail -1)
echo "[build_in_bookworm_pi] symbole glibc le plus haut : ${MAX:-aucun}"
test "$(printf '%s\nGLIBC_2.36\n' "$MAX" | sort -V | tail -1)" = "GLIBC_2.36" \
    || { echo "ERREUR : le binaire exige $MAX > GLIBC_2.36"; exit 1; }

# --- Paquet ------------------------------------------------------------------
# Disposition = celle qu'attend la borne : $PREFIX/bin/<binaires>, déballable
# directement par `tar -xzf … -C /opt/neost`.
rm -rf dist/borne && mkdir -p dist/borne/bin dist
install -m 755 build-borne/neost build-borne/neost-headless dist/borne/bin/
OUT="dist/neost-borne-${MCPU}-aarch64.tar.gz"
tar -czf "$OUT" -C dist/borne bin
echo "[build_in_bookworm_pi] OK : $OUT"
ls -lh "$OUT"
