#!/usr/bin/env bash
# =============================================================================
#  Fabrique dist/NeoST-<version>-macOS-universal2.dmg (build complet inclus).
#
#    NEOST_VERSION=1.0.0 packaging/macos/package_macos.sh
#
#  UN SEUL .dmg Universal 2 (arm64 + x86_64) — natif sur Apple Silicon ET
#  Intel, aucun Rosetta. On tourne sur l'image arm64 (macos-15) ; la tranche
#  x86_64 est cross-compilée, ce que la toolchain Apple fait nativement.
#
#  GLFW est compilé DEPUIS LES SOURCES en statique universel, pas pris chez
#  Homebrew — la double leçon POM1 : brew est mono-arch (DMG x86_64-only), et
#  pire, le linker bakait le chemin absolu brew (/opt/homebrew/... ou
#  /usr/local/...) dans le binaire → chaque .dmg publié mourait au dyld chez
#  quiconque n'avait pas brew au même endroit. Statique = plus rien à résoudre.
#
#  Disposition .app (resolveData cherche exeDir/../roms) :
#    NeoST.app/Contents/MacOS/neost, neost-headless
#    NeoST.app/Contents/roms/ (EmuTOS + tos102uk + tos162uk), Contents/disks/diskA.st
# =============================================================================
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
VERSION="${NEOST_VERSION:-dev}"
GLFW_TAG="3.4"
ARCHS="arm64;x86_64"                     # Universal 2
# Plancher macOS. SANS lui, clang estampille le binaire avec la version de l'OS du
# runner (macOS 15) : le .dmg refusait alors de se lancer sur Ventura/Sonoma et sur
# TOUS les Mac Intel d'avant 2019 — c'est-à-dire précisément le public que la tranche
# x86_64 existe pour servir. C'est le pendant du plancher glibc si soigneusement
# contrôlé côté Linux. Doit être posé sur GLFW *et* sur NeoST (sinon le lien avertit).
MACOS_MIN="11.0"

# Les deux tranches doivent être là, sinon la moitié du parc reçoit un paquet
# qui ne s'exécute pas du tout.
assert_universal() {
    lipo -info "$1"
    for a in arm64 x86_64; do
        lipo -info "$1" | grep -qw "$a" \
            || { echo "ERREUR : $1 n'a pas la tranche $a"; exit 1; }
    done
}

# --- GLFW statique universel (cache invalidé s'il n'est pas universel) -------
if [ -f build-deps/glfw/lib/libglfw3.a ] \
   && ! lipo -info build-deps/glfw/lib/libglfw3.a | grep -qw x86_64; then
    echo "[macos] cache GLFW mono-arch — reconstruction en universel"
    rm -rf build-deps
fi
if [ ! -f build-deps/glfw/lib/libglfw3.a ]; then
    rm -rf build-deps/glfw-src
    git clone --depth 1 --branch "$GLFW_TAG" https://github.com/glfw/glfw.git build-deps/glfw-src
    cmake -S build-deps/glfw-src -B build-deps/glfw-build \
        -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
        -DCMAKE_OSX_ARCHITECTURES="$ARCHS" -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_MIN" \
        -DGLFW_BUILD_EXAMPLES=OFF -DGLFW_BUILD_TESTS=OFF -DGLFW_BUILD_DOCS=OFF \
        -DCMAKE_INSTALL_PREFIX="$ROOT/build-deps/glfw"
    cmake --build build-deps/glfw-build -j"$(sysctl -n hw.ncpu)"
    cmake --install build-deps/glfw-build
fi
assert_universal build-deps/glfw/lib/libglfw3.a

# --- Build NeoST universel contre le GLFW statique ---------------------------
cmake -B build-macos -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_OSX_ARCHITECTURES="$ARCHS" -DCMAKE_OSX_DEPLOYMENT_TARGET="$MACOS_MIN" \
      -DNEOST_VERSION_STR="$VERSION" \
      -DCMAKE_PREFIX_PATH="$ROOT/build-deps/glfw"
cmake --build build-macos -j"$(sysctl -n hw.ncpu)"
test -x build-macos/neost || { echo "ERREUR : le frontend GUI n'a pas été construit (GLFW introuvable ?)"; exit 1; }
assert_universal build-macos/neost
assert_universal build-macos/neost-headless

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

# CFBundleVersion n'accepte qu'une suite de nombres séparés par des points : hors tag,
# VERSION vaut « dev-a1b2c3d » et produirait un plist non conforme.
PLIST_VER=$(printf '%s' "$VERSION" | grep -oE '^[0-9]+(\.[0-9]+)*' || true)
[ -n "$PLIST_VER" ] || PLIST_VER="0.0.0"
cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
    <key>CFBundleName</key>            <string>NeoST</string>
    <key>CFBundleDisplayName</key>     <string>NeoST</string>
    <key>CFBundleIdentifier</key>      <string>net.gistlabs.neost</string>
    <key>CFBundleVersion</key>         <string>${PLIST_VER}</string>
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
DMG="dist/NeoST-$VERSION-macOS-universal2.dmg"
hdiutil create -volname "NeoST" -srcfolder "$APP" -ov -format UDZO "$DMG"
echo "OK : $DMG"
