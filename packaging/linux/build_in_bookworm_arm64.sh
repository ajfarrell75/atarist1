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

#  L'ARCHITECTURE RESTE GÉNÉRIQUE (pas de -mcpu) : cette AppImage-là doit tourner
#  du Pi 3 au Pi 5, c'est sa raison d'être. En revanche elle est compilée en DEUX
#  PASSES GUIDÉES PAR PROFIL (PGO) puis avec LTO — une optimisation qui ne dépend
#  d'aucun modèle de processeur et vaut, mesurée sur les charges d'étalons, −34 %
#  de temps d'émulation à code identique. Sortie vérifiée octet-identique.
#  Le paquet taillé pour UN cœur (Pi 4/400) est produit à part, par
#  packaging/raspberry/. Détails et pièges : docs/PERFORMANCE.md § 5.
# =============================================================================
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y --no-install-recommends \
    cmake g++ make binutils file curl ca-certificates desktop-file-utils \
    libglfw3-dev libgl1-mesa-dev

BUILD_DIR=build-pi
PROFDIR="$PWD/build-pi-profile"
# NEOST_PGO=0 : passe unique (build de secours, ~3× plus rapide).
DO_PGO="${NEOST_PGO:-1}"

# -static-libstdc++/-static-libgcc, même raison que le job bionic : ne faire
# dépendre l'AppImage QUE de la glibc (2.36), pas du libstdc++ de la machine.
configure() {                # configure <drapeaux-supplémentaires> <IPO ON/OFF>
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
        -DNEOST_VERSION_STR="${NEOST_VERSION:-dev}" \
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION="$2" \
        -DCMAKE_C_FLAGS="$1" \
        -DCMAKE_CXX_FLAGS="$1" \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc $1"
}

if [ "$DO_PGO" = "1" ]; then
    # ⚠ LES DEUX PASSES PARTAGENT LE MÊME RÉPERTOIRE DE BUILD : GCC nomme les
    # .gcda d'après le chemin ABSOLU de l'objet. Deux répertoires distincts ne
    # trouvent aucun profil, et -Wno-missing-profile rend l'échec MUET (binaire
    # sans le moindre gain, sans le moindre message) — d'où le contrôle plus bas.
    rm -rf "$PROFDIR"; mkdir -p "$PROFDIR"

    echo "[build_in_bookworm_arm64] PGO passe 1/2 — binaire instrumenté"
    configure "-fprofile-generate=$PROFDIR" OFF
    cmake --build "$BUILD_DIR" -j"$(nproc)" --target neost-headless

    echo "[build_in_bookworm_arm64] PGO — parcours d'entraînement"
    packaging/raspberry/pgo_train.sh "$BUILD_DIR/neost-headless" "$PWD"
    for must in Cpu68k Bus Shifter Moira; do
        find "$PROFDIR" -name "*${must}*.gcda" | grep -q . \
            || { echo "ERREUR : aucun profil pour $must — entraînement muet"; exit 1; }
    done

    echo "[build_in_bookworm_arm64] PGO passe 2/2 — build final (profil + LTO)"
    configure "-fprofile-use=$PROFDIR -fprofile-correction -fprofile-partial-training -Wno-missing-profile" ON
    cmake --build "$BUILD_DIR" -j"$(nproc)"
else
    echo "[build_in_bookworm_arm64] PGO désactivé (NEOST_PGO=0) — passe unique"
    configure "" ON
    cmake --build "$BUILD_DIR" -j"$(nproc)"
fi

test -x "$BUILD_DIR/neost" || { echo "ERREUR : frontend GUI non construit (GLFW ?)"; exit 1; }

NEOST_PKG_TAG=raspberry packaging/linux/make_appimage.sh "$BUILD_DIR"
