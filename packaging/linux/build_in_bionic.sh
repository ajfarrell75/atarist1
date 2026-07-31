#!/usr/bin/env bash
# =============================================================================
#  Release Linux x86_64 : COMPILER + EMPAQUETER, à lancer DANS l'image gelée
#  neost-bionic-builder (le dépôt monté sur /work) — même schéma que le
#  build_in_bionic.sh de POM1 :
#
#    docker run --rm -v "$PWD":/work -w /work -e NEOST_VERSION \
#        ghcr.io/<owner>/neost-bionic-builder:bionic \
#        bash /work/packaging/linux/build_in_bionic.sh
#
#  Rien ici ne touche apt ni ne télécharge de toolchain : gcc-11, CMake,
#  GLFW 3.3 et les outils AppImage sont CUITS dans l'image (Dockerfile.bionic),
#  donc la release ne dépend plus des miroirs EOL de bionic.
#
#  POURQUOI `docker run` ET PAS une clé `container:` : les actions node
#  (checkout, upload-artifact) exigent une glibc >= 2.28 — un cran au-dessus
#  du 2.27 de bionic — et ne peuvent pas tourner dedans. Les actions restent
#  sur l'hôte ; ce script tourne dans le conteneur.
# =============================================================================
set -euxo pipefail
cd "$(dirname "$0")/../.."

# Le dépôt bind-mounté appartient à l'uid de l'hôte, pas à root.
git config --global --add safe.directory '*'

# -static-libstdc++/-static-libgcc : le SEUL plancher libc que l'AppImage
# impose doit être la glibc (2.27) — les symboles GLIBCXX/libgcc de gcc-11
# relèveraient sinon le plancher au-dessus de ce que Mint 19 embarque.
cmake -B build-bionic -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
cmake --build build-bionic -j"$(nproc)"
test -x build-bionic/neost || { echo "ERREUR : frontend GUI non construit (GLFW ?)"; exit 1; }

# Les outils AppImage viennent de l'image (NEOST_APPIMAGE_TOOLS_DIR).
packaging/linux/make_appimage.sh build-bionic
