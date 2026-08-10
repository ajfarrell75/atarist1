#!/usr/bin/env bash
# =============================================================================
#  Paquet Windows x86_64 — bâti dans un shell MSYS2/MINGW64.
#
#  Pourquoi MinGW-w64 et pas MSVC : le code est écrit pour GCC/Clang (attributs,
#  extensions, options -W…), Moira exige C++20 et le reste du dépôt se compile
#  partout ailleurs avec GCC. Passer par MSVC voudrait dire porter les options de
#  compilation ET la chaîne de dépendances (vcpkg) ; MinGW réutilise tout tel quel.
#
#  TOUT EST LIÉ EN STATIQUE (-static) : libgcc, libstdc++, winpthread et GLFW.
#  Un utilisateur Windows qui déballe un zip ne va pas installer des runtimes,
#  et une DLL oubliée ne se voit pas ici (le smoke tourne dans MSYS2, qui a les
#  DLL dans son PATH) — elle se voit chez lui, par une boîte « libstdc++-6.dll
#  introuvable ». Statique = le zip contient tout, ou il ne se construit pas.
#
#  Sortie : dist/NeoST-<version>-windows-x86_64.zip
#           (neost.exe + neost-headless.exe + roms/ disks/ fonts/ + LISEZMOI)
#
#  Usage (dans un shell MINGW64) :
#      NEOST_VERSION=0.5.1 packaging/windows/build_mingw.sh
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."

VERSION="${NEOST_VERSION:-dev}"
BUILD_DIR="build-win"
STAGE="dist/NeoST-$VERSION-windows-x86_64"
OUT="dist/NeoST-$VERSION-windows-x86_64.zip"

rm -rf "$BUILD_DIR" "$STAGE" "$OUT"
mkdir -p dist

# -- Compilation -------------------------------------------------------------
# -static : voir l'en-tête. GLFW_USE_STATIC : la config CMake de mingw-w64-glfw
# expose la cible `glfw` en statique quand on ne demande pas la DLL.
cmake -B "$BUILD_DIR" -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DNEOST_VERSION_STR="$VERSION" \
      -DCMAKE_EXE_LINKER_FLAGS="-static -static-libgcc -static-libstdc++"
cmake --build "$BUILD_DIR" -j"$(nproc)"

test -f "$BUILD_DIR/neost.exe" \
  || { echo "ERREUR : neost.exe absent — GLFW/OpenGL n'ont pas été trouvés, le GUI n'a pas été construit" >&2; exit 1; }
test -f "$BUILD_DIR/neost-headless.exe" || { echo "ERREUR : neost-headless.exe absent" >&2; exit 1; }

# -- Garde ANTI-DLL ----------------------------------------------------------
# La raison d'être du -static ci-dessus. objdump liste les DLL importées : seules
# celles FOURNIES PAR WINDOWS sont tolérées. Toute libgcc/libstdc++/libwinpthread/
# glfw3 dans cette liste = un zip qui ne démarre pas sur une machine nue, et le
# smoke de ce script ne le verrait pas (MSYS2 a ces DLL dans son PATH).
check_dlls() {
    local exe="$1" bad=""
    while read -r dll; do
        case "${dll,,}" in
            kernel32.dll|user32.dll|gdi32.dll|shell32.dll|advapi32.dll|ole32.dll|\
            oleaut32.dll|winmm.dll|imm32.dll|version.dll|setupapi.dll|cfgmgr32.dll|\
            msvcrt.dll|opengl32.dll|dwmapi.dll|shlwapi.dll|ws2_32.dll|bcrypt.dll|\
            avrt.dll|mmdevapi.dll|api-ms-win-*|ext-ms-*) ;;
            *) bad="$bad $dll" ;;
        esac
    done < <(objdump -p "$exe" | awk '/DLL Name:/ {print $3}')
    if [ -n "$bad" ]; then
        echo "ERREUR : $exe dépend de DLL non système :$bad" >&2
        echo "         (le zip ne démarrerait pas sur une machine sans MSYS2)" >&2
        exit 1
    fi
    echo "OK : $(basename "$exe") n'importe que des DLL Windows"
}
check_dlls "$BUILD_DIR/neost.exe"
check_dlls "$BUILD_DIR/neost-headless.exe"

# -- Contenu du zip ----------------------------------------------------------
mkdir -p "$STAGE"
cp "$BUILD_DIR/neost.exe" "$BUILD_DIR/neost-headless.exe" "$STAGE/"
# Même liste autorisée que tous les autres paquets — EmuTOS + les deux TOS UK des
# profils + diskA + polices. Jamais les autres TOS ni les jeux du dépôt.
packaging/stage_free_data.sh "$STAGE"

# resolveData() cherche les données à côté de l'exécutable PUIS un cran au-dessus :
# ici tout est à plat dans le dossier déballé, donc le premier candidat suffit.
cat > "$STAGE/LISEZMOI.txt" <<EOF
NeoST $VERSION — émulateur Atari ST
=======================================

Double-cliquez neost.exe. Rien à installer : tout est dans ce dossier, gardez-le
d'un seul tenant (l'émulateur cherche roms\\ et disks\\ à côté de l'exécutable).

  neost.exe            interface graphique
  neost-headless.exe   version console, déterministe (--help pour les options)
  roms\\                EmuTOS + TOS 1.02/1.62 UK
  disks\\               une disquette de démonstration
  neost.cfg            créé au premier lancement, à côté de l'exécutable

L'interface et les messages sont en anglais.

SmartScreen peut afficher « Windows a protégé votre ordinateur » : le paquet
n'est pas signé (pas de certificat de signature de code pour ce projet).
« Informations complémentaires » puis « Exécuter quand même ».

Licence GNU GPL v3. Code source et rapports de bug :
https://github.com/habib256/neost
EOF

# -- Zip ---------------------------------------------------------------------
( cd dist && zip -qr "$(basename "$OUT")" "$(basename "$STAGE")" )
rm -rf "$STAGE"
ls -lh "$OUT"
echo "OK : $OUT"
