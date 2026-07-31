#!/usr/bin/env bash
# =============================================================================
#  Build + empaquetage Raspberry Pi, à lancer DANS un conteneur debian:bookworm
#  arm64 (le dépôt monté sur /work) — même schéma que le job `raspberry` de
#  POM1 :
#
#    docker run --rm -v "$PWD":/work -w /work -e NEOST_VERSION \
#        debian:bookworm bash packaging/linux/build_in_bookworm_arm64.sh
#
#  Pourquoi bookworm : Raspberry Pi OS EST Debian bookworm, et une AppImage
#  n'embarque jamais la glibc — son plancher est celui de l'image de BUILD.
#  Bâtir sur le runner ubuntu-24.04-arm estampillerait GLIBC_2.39 et le paquet
#  ne démarrerait sur aucun Pi. Bookworm (2.36) couvre Pi OS bookworm+trixie.
#
#  Contrairement à POM1 (GL 3.2 core → tier GLES obligatoire sur Pi), NeoST
#  rend en OpenGL immédiat (profil compat 2.1) que Mesa V3D expose sur Pi 4/5 :
#  pas de build GLES séparé nécessaire.
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y --no-install-recommends \
    cmake g++ make binutils file curl ca-certificates desktop-file-utils \
    libglfw3-dev libgl1-mesa-dev

# -static-libstdc++/-static-libgcc, même raison que le job bionic : ne faire
# dépendre l'AppImage QUE de la glibc (2.36), pas du libstdc++ de la machine.
cmake -B build-pi -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc"
cmake --build build-pi -j"$(nproc)"
test -x build-pi/neost || { echo "ERREUR : frontend GUI non construit (GLFW ?)"; exit 1; }

NEOST_PKG_TAG=raspberry packaging/linux/make_appimage.sh build-pi
