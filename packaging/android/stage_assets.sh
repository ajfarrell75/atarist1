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
# LICENCES : l'APK embarque EmuTOS (GPLv2) et NeoST (GPLv3) — leur texte doit
# ACCOMPAGNER le binaire. Ces fichiers ne figurent PAS dans kAssets
# (src/android/main_android.cpp) : ils voyagent dans l'APK sans être déballés,
# ce qui suffit à la licence et n'ajoute rien au stockage interne.
cp LICENSE "$DEST/GPL-3.0.txt"
cp packaging/licenses/GPL-2.0.txt "$DEST/GPL-2.0.txt"
cp packaging/licenses/THIRD-PARTY.txt "$DEST/THIRD-PARTY.txt"
# Garde-fou : rien d'autre qu'EmuTOS, diskA et les licences ne doit se retrouver
# dans l'APK.
BAD=$(find "$DEST" -type f ! -name 'etos*' ! -name 'diskA.st' \
       ! -name 'GPL-*.txt' ! -name 'THIRD-PARTY.txt' || true)
if [ -n "$BAD" ]; then
    echo "ERREUR : fichier non redistribuable dans les assets :" >&2
    echo "$BAD" >&2
    exit 1
fi
echo "OK : assets = $(ls "$DEST" | tr '\n' ' ')($(du -sh "$DEST" | cut -f1))"
