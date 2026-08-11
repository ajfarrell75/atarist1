#!/usr/bin/env bash
# =============================================================================
#  Copie dans les `assets` du paquet Android les SEULES données redistribuables :
#  EmuTOS (GPL) et la disquette générée par tools/make_floppy.py. Aucun TOS Atari,
#  aucun jeu — la politique du Play Store est plus stricte que celle des paquets
#  de bureau, et l'utilisateur importe ses propres images.
#
#  Le frontend déballe ces fichiers dans le stockage interne au 1er lancement
#  (cf. prepareData(), src/android/main_android.cpp) : les noms doivent donc
#  rester PLATS et coïncider avec la liste kAssets de ce fichier.
# =============================================================================
set -euo pipefail
cd "$(dirname "$0")/../.."
DEST="packaging/android/app/src/main/assets"
mkdir -p "$DEST"
for f in roms/etos192us.img roms/etos192fr.img roms/etos256us.img roms/etos256fr.img; do
    cp "$f" "$DEST/$(basename "$f")"
done
cp disks/diskA.st "$DEST/diskA.st"
# Garde-fou : rien d'autre qu'EmuTOS et diskA ne doit se retrouver dans l'APK.
BAD=$(find "$DEST" -type f ! -name 'etos*' ! -name 'diskA.st' || true)
if [ -n "$BAD" ]; then
    echo "ERREUR : fichier non redistribuable dans les assets :" >&2
    echo "$BAD" >&2
    exit 1
fi
echo "OK : assets = $(ls "$DEST" | tr '\n' ' ')($(du -sh "$DEST" | cut -f1))"
