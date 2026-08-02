#!/usr/bin/env bash
# =============================================================================
#  build_native_pi.sh — Compilation NATIVE de NeoST sur le Raspberry Pi.
#
#  POURQUOI plutôt que l'AppImage `NeoST-*-raspberry-aarch64.AppImage` :
#  l'AppImage est bâtie en aarch64 GÉNÉRIQUE (elle doit tourner du Pi 3 au Pi 5).
#  Ici on compile avec -mcpu=<le cœur exact>, ce qui laisse GCC utiliser le jeu
#  d'instructions et le modèle de coût du cœur réel. La boucle chaude de NeoST
#  est l'interpréteur Moira (un switch géant sur l'opcode) : c'est exactement le
#  genre de code qui profite du bon -mtune. Compter ~10-20 % de trames/s en plus.
#
#  PLUS EFFICACE ENCORE que -mcpu : la compilation guidée par profil (--pgo).
#  On compile une première fois avec des compteurs, on fait tourner l'émulateur sur
#  un parcours représentatif (pgo_train.sh), puis on recompile en donnant ce profil
#  à GCC. Il sait alors quelle issue de chaque branche est la fréquente et range le
#  code en conséquence : moins de sauts pris, cache d'instructions bien mieux
#  utilisé — ce qui compte double sur un Cortex-A72 (32 Ko de L1i). Mesuré sur les
#  mêmes charges, à code identique : PGO seul −20 %, PGO+LTO −34 %. Sorties
#  vérifiées octet-identiques au binaire -O3 nu.
#
#  Usage (SUR le Pi, dans une copie du dépôt) :
#      packaging/raspberry/build_native_pi.sh              # build → build-pi/
#      packaging/raspberry/build_native_pi.sh --pgo        # 2 passes + LTO (RECOMMANDÉ)
#      sudo packaging/raspberry/build_native_pi.sh --pgo --install
#      NEOST_LTO=1 packaging/raspberry/build_native_pi.sh  # LTO seul (build long)
#
#  --install copie le binaire ET les données (roms/, disks/, fonts/, gemdos/)
#  dans /opt/neost, la disposition qu'attend le service kiosk :
#      /opt/neost/bin/neost        ← exeDir
#      /opt/neost/roms/…           ← resolveData() essaie exeDir/../<chemin>
#      /opt/neost/neost.cfg        ← cfgPath() = exeDir + "/../neost.cfg"
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"

PREFIX="${NEOST_PREFIX:-/opt/neost}"
BUILD_DIR="${NEOST_BUILD_DIR:-build-pi}"
DO_INSTALL=0
DO_PGO=0
for a in "$@"; do
    case "$a" in
        --install) DO_INSTALL=1 ;;
        --pgo)     DO_PGO=1 ;;
        *) echo "Option inconnue : $a  (--pgo, --install)"; exit 1 ;;
    esac
done

# --- 1. Identifier le cœur ---------------------------------------------------
# Le modèle est dans /proc/device-tree/model ("Raspberry Pi 4 Model B Rev 1.5").
# On ne se fie PAS à -mcpu=native seul : sur certains noyaux 64 bits le MIDR lu
# par GCC est incomplet et la détection retombe sur un générique silencieux.
MODEL="$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || echo inconnu)"
case "$MODEL" in
    *"Raspberry Pi 5"*)            MCPU=cortex-a76 ;;
    *"Raspberry Pi 4"*|*"Pi 400"*) MCPU=cortex-a72 ;;
    *"Raspberry Pi 3"*)            MCPU=cortex-a53 ;;
    *)                             MCPU=native ;;
esac
# Garde-fou : si le compilateur refuse ce -mcpu (GCC trop ancien), on retombe
# sur générique plutôt que d'échouer 20 minutes plus tard sur un .cpp au hasard.
if ! echo 'int main(){}' | ${CXX:-g++} -x c++ -mcpu=$MCPU -o /dev/null - 2>/dev/null; then
    echo "[build_native_pi] AVERTISSEMENT : -mcpu=$MCPU refusé par $(${CXX:-g++} --version | head -1) → générique"
    MCPU=""
fi

ARCH_FLAGS=""
[ -n "$MCPU" ] && ARCH_FLAGS="-mcpu=$MCPU -mtune=$MCPU"

# LTO : le lien monte à plusieurs minutes sur un Pi 4 et demande ~1,5 Go de RAM.
# Opt-in seul ; --pgo l'active automatiquement en seconde passe si la RAM suffit.
LTO_ON=OFF
[ "${NEOST_LTO:-0}" = "1" ] && LTO_ON=ON

echo "[build_native_pi] modèle : $MODEL"
echo "[build_native_pi] flags  : ${ARCH_FLAGS:-<génériques>}$( [ "$LTO_ON" = ON ] && echo ' +LTO')$( [ "$DO_PGO" = 1 ] && echo ' +PGO')"

# Un Pi 4 a 4 cœurs mais souvent 2-4 Go : -j4 sur du C++17 lourd part en OOM-kill
# (symptôme : « c++: fatal error: Killed signal terminated program cc1plus »).
JOBS="${NEOST_JOBS:-}"
MEM_MB=$(($(awk '/MemTotal/{print $2}' /proc/meminfo) / 1024))
if [ -z "$JOBS" ]; then
    JOBS=$(( MEM_MB / 900 )); [ "$JOBS" -lt 1 ] && JOBS=1
    NPROC=$(nproc); [ "$JOBS" -gt "$NPROC" ] && JOBS=$NPROC
fi

# --- 2. Compiler -------------------------------------------------------------
# ⚠ On passe par CMAKE_CXX_FLAGS et PAS par CMAKE_CXX_FLAGS_RELEASE : le
# CMakeLists écrase ce dernier sans condition (set(... "-O3 -DNDEBUG")), un
# override en ligne de commande y serait perdu.
configure() {                 # configure <drapeaux-supplémentaires> <IPO ON/OFF>
    cmake -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INTERPROCEDURAL_OPTIMIZATION="$2" \
          -DCMAKE_C_FLAGS="$ARCH_FLAGS $1" \
          -DCMAKE_CXX_FLAGS="$ARCH_FLAGS $1" \
          -DCMAKE_EXE_LINKER_FLAGS="$1"
}

if [ "$DO_PGO" = "1" ]; then
    PROFDIR="$ROOT/$BUILD_DIR-profile"
    # ⚠ LES DEUX PASSES PARTAGENT LE MÊME RÉPERTOIRE DE BUILD : GCC nomme les
    # fichiers .gcda d'après le chemin ABSOLU de l'objet compilé. Instrumenter dans
    # un répertoire et relire depuis un autre ne trouve aucun profil — et le fait
    # SILENCIEUSEMENT si -Wno-missing-profile est actif. D'où le contrôle plus bas.
    rm -rf "$PROFDIR"; mkdir -p "$PROFDIR"

    echo "[build_native_pi] PGO passe 1/2 : binaire instrumenté"
    configure "-fprofile-generate=$PROFDIR" OFF
    cmake --build "$BUILD_DIR" -j"$JOBS" --target neost-headless

    echo "[build_native_pi] PGO : parcours d'entraînement (quelques minutes)"
    "$ROOT/packaging/raspberry/pgo_train.sh" "$BUILD_DIR/neost-headless" "$ROOT"
    for must in Cpu68k Bus Shifter Moira; do
        find "$PROFDIR" -name "*${must}*.gcda" | grep -q . \
            || { echo "ERREUR : aucun profil pour $must — l'entraînement n'a rien exécuté"; exit 1; }
    done

    # LTO en seconde passe seulement si la machine a de quoi : le lien LTO de NeoST
    # demande ~1,5 Go. Sur un Pi à 1 Go on garde le PGO seul (l'essentiel du gain).
    PGO_IPO=ON
    [ "$MEM_MB" -lt 2000 ] && { PGO_IPO=OFF; echo "[build_native_pi] < 2 Go de RAM → LTO désactivé"; }
    echo "[build_native_pi] PGO passe 2/2 : build final (profil${PGO_IPO:+ + LTO})"
    configure "-fprofile-use=$PROFDIR -fprofile-correction -fprofile-partial-training -Wno-missing-profile" "$PGO_IPO"
    cmake --build "$BUILD_DIR" -j"$JOBS"
else
    echo "[build_native_pi] compilation avec -j$JOBS"
    configure "" "$LTO_ON"
    cmake --build "$BUILD_DIR" -j"$JOBS"
fi

echo "[build_native_pi] OK : $BUILD_DIR/neost"

# --- 3. Installer (optionnel) ------------------------------------------------
if [ "$DO_INSTALL" = "1" ]; then
    [ "$(id -u)" -eq 0 ] || { echo "ERREUR : --install demande root (sudo)"; exit 1; }
    install -d "$PREFIX/bin"
    install -m 755 "$BUILD_DIR/neost" "$BUILD_DIR/neost-headless" "$PREFIX/bin/"
    # Données : on NE remplace pas ce qui existe déjà (les disquettes ajoutées par
    # l'exploitant de la borne survivent à une mise à jour du binaire).
    for d in roms disks fonts gemdos carts; do
        [ -d "$ROOT/$d" ] || continue
        install -d "$PREFIX/$d"
        cp -rn "$ROOT/$d/." "$PREFIX/$d/" 2>/dev/null || true
    done
    echo "[build_native_pi] installé dans $PREFIX (binaire + données)"
    echo "[build_native_pi] étape suivante : packaging/raspberry/install_kiosk.sh"
fi
