#!/usr/bin/env bash
# =============================================================================
#  Fabrique dist/NeoST-<version>-macOS-arm64.dmg (build complet inclus).
#
#    NEOST_VERSION=1.0.0 packaging/macos/package_macos.sh
#
#  GLFW est compilé DEPUIS LES SOURCES en statique, pas pris chez Homebrew :
#  la leçon POM1 — le linker bakait le chemin absolu brew (/opt/homebrew/... ou
#  /usr/local/...) dans le binaire et chaque .dmg publié mourait au dyld chez
#  quiconque n'avait pas brew au même endroit. Statique = plus rien à résoudre.
#
#  Disposition .app (resolveData cherche exeDir/../roms) :
#    NeoST.app/Contents/MacOS/neost, neost-headless
#    NeoST.app/Contents/roms/ (EmuTOS seulement), Contents/disks/diskA.st
# =============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
VERSION="${NEOST_VERSION:-dev}"
GLFW_TAG="3.4"

# --- GLFW statique (arm64 natif ; le runner macos-15 est Apple Silicon) ------
if [ ! -f build-deps/glfw/lib/libglfw3.a ]; then
    rm -rf build-deps/glfw-src
    git clone --depth 1 --branch "$GLFW_TAG" https://github.com/glfw/glfw.git build-deps/glfw-src
    cmake -S build-deps/glfw-src -B build-deps/glfw-build \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF \
        -DCMAKE_INSTALL_PREFIX="$ROOT/build-deps/glfw"
    cmake --build build-deps/glfw-build -j"$(sysctl -n hw.ncpu)"
    cmake --install build-deps/glfw-build
fi

# --- Build NeoST contre le GLFW statique -------------------------------------
cmake -B build-macos -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_PREFIX_PATH="$ROOT/build-deps/glfw"
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"
test -x build-macos/neost || { echo "ERREUR : le frontend GUI n'a pas été construit (GLFW introuvable ?)"; exit 1; }

# --- Staging .app ------------------------------------------------------------
APP="dist/NeoST.app"
rm -rf dist
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
install -m 755 build-macos/neost build-macos/neost-headless "$APP/Contents/MacOS/"
packaging/stage_free_data.sh "$APP/Contents"

# Icône .icns depuis packaging/neost.png (sips + iconutil, livrés avec macOS).
ICONSET="build-macos/neost.iconset"
rm -rf "$ICONSET"; mkdir -p "$ICONSET"
for s in 16 32 128 256; do
    sips -z $s $s packaging/neost.png --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
    sips -z $((s*2)) $((s*2)) packaging/neost.png --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/neost.icns"

cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
    <key>CFBundleName</key>            <string>NeoST</string>
    <key>CFBundleDisplayName</key>     <string>NeoST</string>
    <key>CFBundleIdentifier</key>      <string>net.gistlabs.neost</string>
    <key>CFBundleVersion</key>         <string>${VERSION}</string>
    <key>CFBundleShortVersionString</key> <string>${VERSION}</string>
    <key>CFBundleExecutable</key>      <string>neost</string>
    <key>CFBundleIconFile</key>        <string>neost</string>
    <key>CFBundlePackageType</key>     <string>APPL</string>
    <key>NSHighResolutionCapable</key> <true/>
</dict></plist>
EOF

# --- Garde-fou anti-régression POM1 : aucune référence brew/MacPorts ---------
LEAKED=$(otool -L "$APP/Contents/MacOS/neost" | tail -n +2 | awk '{print $1}' \
         | grep -E '^(/usr/local|/opt/homebrew|/opt/local)' || true)
if [ -n "$LEAKED" ]; then
    echo "ERREUR : le .app référence des dylibs hors bundle :"; echo "$LEAKED"; exit 1
fi

# --- DMG ---------------------------------------------------------------------
DMG="dist/NeoST-$VERSION-macOS-arm64.dmg"
hdiutil create -volname "NeoST" -srcfolder "$APP" -ov -format UDZO "$DMG"
echo "OK : $DMG"
