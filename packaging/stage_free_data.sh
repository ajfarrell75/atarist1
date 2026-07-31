#!/usr/bin/env bash
# Copie dans $1 les SEULES données librement redistribuables embarquées dans les
# paquets NeoST : EmuTOS (GPL), les échantillons de lecteur et disks/diskA.st
# (générée par tools/make_floppy.py). Les TOS propriétaires et les jeux sous
# copyright présents dans le dépôt de travail ne doivent JAMAIS entrer ici —
# liste explicite, pas de glob sur roms/ ni disks/.
set -euo pipefail
DEST="${1:?usage: stage_free_data.sh <dossier destination>}"
SRC="$(cd "$(dirname "$0")/.." && pwd)"

mkdir -p "$DEST/roms" "$DEST/disks"
for rom in etos192us.img etos192fr.img etos256us.img etos256fr.img; do
    cp "$SRC/roms/$rom" "$DEST/roms/"
done
cp -r "$SRC/roms/drivesound" "$DEST/roms/"
cp "$SRC/disks/diskA.st" "$DEST/disks/"

# Garde-fou : rien d'autre que de l'EmuTOS ne doit se trouver dans le paquet.
# (pas de -printf : find BSD de macOS ne le connaît pas)
STRAY=$(find "$DEST/roms" -maxdepth 1 -name '*.img' ! -name 'etos*')
if [ -n "$STRAY" ]; then
    echo "ERREUR : ROM non libre dans le paquet : $STRAY" >&2
    exit 1
fi
echo "OK : données libres copiées dans $DEST (EmuTOS + drivesound + diskA.st)"
