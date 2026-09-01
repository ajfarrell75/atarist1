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
#   · compilation en DEUX PASSES guidée par profil (PGO) + LTO. Mesuré sur les
#     mêmes charges (boot TOS 500 trames, Enchanted Land 900 trames), à code
#     identique : -O3 seul → PGO −20 %, PGO+LTO −34 %. C'est le plus gros gain
#     disponible sans toucher une ligne d'émulation, et il est GRATUIT ici :
#     l'entraînement tourne sur le runner, pas sur le Pi. Sorties vérifiées
#     OCTET-IDENTIQUES à celles du binaire -O3 nu (captures de 6801 et 29500
#     trames sur l'étalon overscan No Cooper) — le PGO ne change que la
#     disposition du code, jamais la sémantique.
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
# NEOST_PGO=0 pour retomber sur une passe unique (débogage du script, build rapide).
DO_PGO="${NEOST_PGO:-1}"

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y --no-install-recommends \
    cmake g++ make binutils file ca-certificates libglfw3-dev libgl1-mesa-dev \
    curl desktop-file-utils

echo "[build_in_bookworm_pi] cible : -mcpu=$MCPU"
echo 'int main(){}' | g++ -x c++ -mcpu="$MCPU" -o /dev/null - \
    || { echo "ERREUR : -mcpu=$MCPU refusé par $(g++ --version | head -1)"; exit 1; }

# ⚠ CMAKE_CXX_FLAGS et PAS CMAKE_CXX_FLAGS_RELEASE : le CMakeLists écrase ce
# dernier sans condition, un override en ligne de commande y serait perdu.
# -static-libstdc++/-static-libgcc : ne dépendre QUE de la glibc de bookworm,
# pas du libstdc++ de l'image de build.
ARCH_FLAGS="-mcpu=$MCPU -mtune=$MCPU"
BUILD_DIR=build-borne
PROFDIR="$PWD/build-borne-profile"

configure() {                # configure <drapeaux-supplémentaires> [IPO]
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
        -DNEOST_VERSION_STR="${NEOST_VERSION:-borne}" \
        -DCMAKE_INTERPROCEDURAL_OPTIMIZATION="${2:-OFF}" \
        -DCMAKE_C_FLAGS="$ARCH_FLAGS $1" \
        -DCMAKE_CXX_FLAGS="$ARCH_FLAGS $1" \
        -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc $1"
}

if [ "$DO_PGO" = "1" ]; then
    # ⚠ LES DEUX PASSES DOIVENT PARTAGER LE MÊME RÉPERTOIRE DE BUILD. GCC nomme
    # chaque fichier .gcda d'après le CHEMIN ABSOLU de l'objet compilé : instrumenter
    # dans build-A puis relire depuis build-B ne trouve AUCUN profil, et
    # -Wno-missing-profile rend l'échec parfaitement silencieux (le binaire sort
    # sans le moindre gain, sans le moindre message). D'où le compteur de contrôle
    # plus bas, qui fait ÉCHOUER le build si le profil n'a pas été relu.
    rm -rf "$PROFDIR"; mkdir -p "$PROFDIR"

    echo "[build_in_bookworm_pi] PGO passe 1/2 — binaire instrumenté"
    configure "-fprofile-generate=$PROFDIR"
    cmake --build "$BUILD_DIR" -j"$(nproc)" --target neost-headless

    echo "[build_in_bookworm_pi] PGO — parcours d'entraînement"
    # Seul le headless est entraîné : il partage tout le cœur d'émulation avec le
    # frontend GUI (neost_core), et lui seul tourne sans serveur graphique.
    packaging/raspberry/pgo_train.sh "$BUILD_DIR/neost-headless" "$PWD"

    NGCDA=$(find "$PROFDIR" -name '*.gcda' | wc -l)
    echo "[build_in_bookworm_pi] profils collectés : $NGCDA"
    # Les quatre fichiers qui portent la boucle chaude. S'ils manquent, le parcours
    # d'entraînement n'a rien exécuté d'utile et le PGO serait un placebo.
    for must in Cpu68k Bus Shifter Moira; do
        find "$PROFDIR" -name "*${must}*.gcda" | grep -q . \
            || { echo "ERREUR : aucun profil pour $must — parcours d'entraînement muet"; exit 1; }
    done

    echo "[build_in_bookworm_pi] PGO passe 2/2 — build final (profil + LTO)"
    # -fprofile-partial-training : les objets NON entraînés (main.cpp du GUI, ImGui,
    # miniaudio) sont optimisés normalement au lieu d'être traités comme du code
    # froid — sans lui, le frontend fenêtré sortirait dégradé.
    # -fprofile-correction : les compteurs d'un programme multi-thread peuvent être
    # légèrement incohérents ; on répare au lieu d'échouer.
    configure "-fprofile-use=$PROFDIR -fprofile-correction -fprofile-partial-training -Wno-missing-profile" ON
    cmake --build "$BUILD_DIR" -j"$(nproc)" 2>&1 | tee /tmp/neost-pgo-build.log
    # Contrôle final : si les profils n'avaient PAS été trouvés, GCC l'aurait dit
    # (l'avertissement est neutralisé pour les objets du GUI, jamais pour le cœur —
    # on regarde donc explicitement les sources du cœur).
    if grep -E "src/(core|io)/.*(profile count data file not found|missing-profile)" \
            /tmp/neost-pgo-build.log >/dev/null 2>&1; then
        echo "ERREUR : profil non relu pour une source du cœur (chemins de build désaccordés ?)"
        exit 1
    fi
else
    echo "[build_in_bookworm_pi] PGO désactivé (NEOST_PGO=0) — passe unique"
    configure "" ON
    cmake --build "$BUILD_DIR" -j"$(nproc)"
fi

test -x "$BUILD_DIR/neost" || { echo "ERREUR : frontend GUI non construit (GLFW ?)"; exit 1; }

# --- Vérifications qui doivent échouer ICI, pas sur la borne -----------------
readelf -h "$BUILD_DIR/neost" | grep -q AArch64 \
    || { echo "ERREUR : pas un binaire AArch64"; exit 1; }
# Plancher glibc : le symbole GLIBC_x.y le plus haut exigé doit rester <= 2.36,
# sinon Pi OS refusera de lancer le binaire (« version `GLIBC_2.39' not found »).
MAX=$(objdump -T "$BUILD_DIR/neost" 2>/dev/null | grep -oE 'GLIBC_[0-9]+\.[0-9]+' | sort -V | tail -1)
echo "[build_in_bookworm_pi] symbole glibc le plus haut : ${MAX:-aucun}"
test "$(printf '%s\nGLIBC_2.36\n' "$MAX" | sort -V | tail -1)" = "GLIBC_2.36" \
    || { echo "ERREUR : le binaire exige $MAX > GLIBC_2.36"; exit 1; }

# --- Nom des paquets ----------------------------------------------------------
# Le paquet porte le MODÈLE DE MACHINE, pas le nom d'un usage : c'est du binaire
# taillé pour un cœur, et il sert aussi bien à la borne sans bureau qu'à un Pi OS
# de bureau. « pi400 » dit à qui il s'adresse ; « borne » ne le disait pas.
case "$MCPU" in
    cortex-a72) PKG_TAG=pi400 ;;      # Pi 4 / Pi 400
    cortex-a76) PKG_TAG=pi5   ;;
    cortex-a53) PKG_TAG=pi3   ;;
    *)          PKG_TAG="$MCPU" ;;
esac

# --- Paquet 1 : tar.gz --------------------------------------------------------
# Disposition = celle qu'attend la borne : $PREFIX/bin/<binaires> + roms/disks/
# fonts (même liste que les AppImage), déballable par `tar -xzf … -C /opt/neost`.
# C'est le format de la borne sans bureau : pas de FUSE, pas de montage, le
# service systemd lance le binaire nu. Sans les ROM, le profil 520 ST / 1040 STE
# et le défaut kiosk (tos162uk) seraient introuvables après déballage.
rm -rf "dist/$PKG_TAG" && mkdir -p "dist/$PKG_TAG/bin" dist
install -m 755 "$BUILD_DIR/neost" "$BUILD_DIR/neost-headless" "dist/$PKG_TAG/bin/"
packaging/stage_free_data.sh "dist/$PKG_TAG"
OUT="dist/neost-${PKG_TAG}-aarch64.tar.gz"
tar -czf "$OUT" -C "dist/$PKG_TAG" .
echo "[build_in_bookworm_pi] OK : $OUT"
ls -lh "$OUT"

# --- Paquet 2 : AppImage ------------------------------------------------------
# MÊME build (aucune recompilation) empaqueté en AppImage : c'est le format utile
# sur Raspberry Pi OS **avec bureau**, où l'on veut un fichier unique cliquable
# plutôt qu'une arborescence dans /opt. Elle embarque les données libres
# (EmuTOS SEUL, polices, disquette de démarrage, démos libres) via stage_free_data.sh.
#
# ⚠ Elle NE REMPLACE PAS l'AppImage de release (`-raspberry-aarch64`, aarch64
# GÉNÉRIQUE, Pi 3 → Pi 5) : celle-ci est compilée pour UN cœur précis. D'où un
# nom distinct — le job `publish` d'une release aplatit tous les artefacts dans
# un même dossier, deux paquets homonymes s'y écraseraient en silence.
echo "[build_in_bookworm_pi] AppImage (tag $PKG_TAG)…"
NEOST_PKG_TAG="$PKG_TAG" NEOST_VERSION="${NEOST_VERSION:-dev}" \
    packaging/linux/make_appimage.sh "$BUILD_DIR"
ls -lh dist/*.AppImage
