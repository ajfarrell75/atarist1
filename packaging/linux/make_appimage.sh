#!/usr/bin/env bash
# =============================================================================
#  Fabrique dist/NeoST-<version>-[tag-]<arch>.AppImage à partir d'un build
#  existant.
#
#    packaging/linux/make_appimage.sh [dossier-build]     (défaut : build)
#    NEOST_VERSION=1.0.0 packaging/linux/make_appimage.sh
#    NEOST_PKG_TAG=raspberry … → NeoST-<ver>-raspberry-aarch64.AppImage
#
#  Disposition (resolveData cherche exeDir/../roms) :
#    AppDir/usr/bin/neost, neost-headless
#    AppDir/usr/roms/ (EmuTOS seulement), AppDir/usr/disks/diskA.st
#
#  Mécanique reprise de POM1 (packaging/linux/build_appimage.sh) :
#    1. linuxdeploy met en place l'AppDir : libs non-blacklist embarquées
#       (libglfw3…, rpath $ORIGIN/../lib), libGL/glibc EXCLUES (règle AppImage :
#       le pilote graphique vient de l'hôte), AppRun → usr/bin/neost.
#    2. appimagetool d'AppImageKit (l'ANCIEN dépôt) assemble le paquet : son
#       runtime est ELF ET_EXEC + squashfs gzip, accepté par AppImageLauncher.
#       Le nouveau AppImage/appimagetool (et le plugin appimage de linuxdeploy,
#       vérifié ici même) produisent un runtime static-pie ET_DYN rejeté en
#       « type -1 ». En aarch64, même le `continuous` d'AppImageKit est ET_DYN :
#       on épingle le runtime de la release figée 12 (--runtime-file).
#
#  Les outils sont pris dans $NEOST_APPIMAGE_TOOLS_DIR (image bionic gelée),
#  sinon téléchargés/extraits dans build-appimage/tools/ (cache local, pas de
#  FUSE requis). ⚠ neost.cfg (exeDir/../) tombe dans le squashfs lecture
#  seule : la config ne se persiste pas depuis l'AppImage — assumé (kiosk).
# =============================================================================
set -euo pipefail
BUILD_DIR="${1:-build}"
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
ARCH="$(uname -m)"                       # x86_64 | aarch64
VERSION="${NEOST_VERSION:-dev}"
# NEOST_PKG_TAG=raspberry → NeoST-<ver>-raspberry-aarch64.AppImage : distingue
# le paquet Pi (plancher glibc bookworm) de l'arm64 générique bâti sur le runner.
PKG_ARCH="${NEOST_PKG_TAG:+$NEOST_PKG_TAG-}$ARCH"

test -x "$BUILD_DIR/neost" || { echo "ERREUR : $BUILD_DIR/neost absent (build GUI requis)"; exit 1; }
test -x "$BUILD_DIR/neost-headless" || { echo "ERREUR : $BUILD_DIR/neost-headless absent"; exit 1; }

APPDIR="build-appimage/AppDir"
TOOLS="${NEOST_APPIMAGE_TOOLS_DIR:-build-appimage/tools}"
# Nom EXACT, pas un glob : « NeoST-*aarch64.AppImage » emportait aussi le paquet
# « …-raspberry-aarch64.AppImage » d'un empaquetage précédent, sans un mot.
rm -rf "$APPDIR"; rm -f "dist/NeoST-$VERSION-$PKG_ARCH.AppImage"   # (cache tools/ conservé)
mkdir -p "$APPDIR/usr/bin" "$TOOLS" dist

# --- Outils AppImage (extraits → utilisables sans FUSE) ----------------------
fetch_extract() {
    local url="$1" name="$2" appdir="$TOOLS/$2.AppDir"
    [ -d "$appdir" ] && return 0
    echo "[appimage] Téléchargement de $name…"
    curl -fsSL -o "$TOOLS/$name.AppImage" "$url"
    chmod +x "$TOOLS/$name.AppImage"
    (cd "$TOOLS" && "./$name.AppImage" --appimage-extract >/dev/null && mv squashfs-root "$name.AppDir")
}
fetch_extract "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$ARCH.AppImage" linuxdeploy
fetch_extract "https://github.com/AppImage/AppImageKit/releases/download/continuous/appimagetool-$ARCH.AppImage" appimagetool

# Runtime aarch64 : le `continuous` d'AppImageKit est ET_DYN côté ARM ; la
# release 12 est la dernière à publier un runtime-aarch64 ET_EXEC (cf. POM1).
RUNTIME_ARG=()
if [ "$ARCH" = "aarch64" ]; then
    RUNTIME_FILE="$TOOLS/runtime-aarch64-et_exec"
    if [ ! -f "$RUNTIME_FILE" ]; then
        curl -fsSL -o "$RUNTIME_FILE" \
          "https://github.com/AppImage/AppImageKit/releases/download/12/runtime-aarch64"
    fi
    RUNTIME_ARG=(--runtime-file "$RUNTIME_FILE")
fi

# --- Staging AppDir ----------------------------------------------------------
install -m 755 "$BUILD_DIR/neost" "$BUILD_DIR/neost-headless" "$APPDIR/usr/bin/"
packaging/stage_free_data.sh "$APPDIR/usr"

cat > build-appimage/neost.desktop <<'EOF'
[Desktop Entry]
Type=Application
Name=NeoST
Comment=Émulateur Atari ST pédagogique (cycle-exact, Moira)
Exec=neost
Icon=neost
Categories=Game;Emulator;
Terminal=false
EOF

# --- 1. linuxdeploy : libs embarquées + desktop/icône + AppRun ---------------
"$TOOLS/linuxdeploy.AppDir/AppRun" \
    --appdir "$APPDIR" \
    --executable "$APPDIR/usr/bin/neost" \
    --executable "$APPDIR/usr/bin/neost-headless" \
    --desktop-file build-appimage/neost.desktop \
    --icon-file packaging/neost.png

# --- 2. appimagetool (via AppRun : ses libgpgme embarquées, mksquashfs inclus)
OUT="dist/NeoST-$VERSION-$PKG_ARCH.AppImage"
ARCH="$ARCH" VERSION="$VERSION" \
"$TOOLS/appimagetool.AppDir/AppRun" \
    ${RUNTIME_ARG[@]+"${RUNTIME_ARG[@]}"} \
    "$APPDIR" "$OUT"

echo "OK : $OUT"
