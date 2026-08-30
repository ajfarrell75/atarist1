#!/usr/bin/env bash
# Copie dans $1 les données embarquées dans les paquets NeoST :
#   - EmuTOS (GPL) US/FR 192+256 Ko
#   - échantillons de lecteur + disks/diskA.st (générée par tools/make_floppy.py)
#   - les LICENCES (packaging/licenses/ + LICENSE) : obligatoire, cf. plus bas
# Les TOS propriétaires et les jeux sous copyright ne doivent JAMAIS entrer ici —
# liste explicite, pas de glob sur roms/ ni disks/.
#
# Le paquet est 100 % LIBRE PAR DÉFAUT (EmuTOS seul) depuis la purge du
# 2026-08-30 : les TOS Atari ne sont plus suivis par git, la CI ne les a plus.
# NeoST n'en a PAS besoin — il boote EmuTOS ; tos102uk/tos162uk ne servent
# qu'aux profils « 520 ST » et « 1040 STE ». NEOST_PACKAGE_NO_ATARI_TOS=0 les
# ré-embarque depuis des copies LOCALES (gitignorées, cf. tools/private_assets.sh)
# — décision de mainteneur, pour un paquet personnel, jamais pour une release.
set -euo pipefail
DEST="${1:?usage: stage_free_data.sh <dossier destination>}"
SRC="$(cd "$(dirname "$0")/.." && pwd)"
NO_ATARI="${NEOST_PACKAGE_NO_ATARI_TOS:-1}"

mkdir -p "$DEST/roms" "$DEST/disks"
FREE_ROMS=(etos192us.img etos192fr.img etos256us.img etos256fr.img)
ATARI_ROMS=(tos102uk.img tos162uk.img)
[ "$NO_ATARI" = "1" ] && ATARI_ROMS=()
for rom in "${FREE_ROMS[@]}" ${ATARI_ROMS[@]+"${ATARI_ROMS[@]}"}; do
    cp "$SRC/roms/$rom" "$DEST/roms/"
done
cp -r "$SRC/roms/drivesound" "$DEST/roms/"
cp "$SRC/disks/diskA.st" "$DEST/disks/"

# Polices de l'interface (resolveData cherche exeDir/../fonts) : DejaVu Sans et
# Font Awesome Free, toutes deux librement redistribuables. SANS elles, le GUI
# se replie EN SILENCE sur la police bitmap d'ImGui et TOUS les pictogrammes
# deviennent des carrés vides — dont deux boutons purement iconiques (retrait de
# breakpoint/watchpoint) qui n'ont alors plus aucun libellé. Invisible en CI
# (le smoke est headless), très visible chez l'utilisateur.
cp -r "$SRC/fonts" "$DEST/"
# Photo du clavier ST (fenêtre « Keyboard » : touches cliquables sur l'image).
cp -r "$SRC/pic" "$DEST/"

# LICENCES — obligatoire, pas décoratif. Le paquet embarque du GPLv3 (NeoST) et du
# GPLv2 (EmuTOS) : la GPL exige que le texte de licence ACCOMPAGNE le binaire, et
# qu'une offre de source figure quelque part. Jusqu'au 2026-08-19 les paquets
# publiés ne contenaient AUCUN de ces fichiers — non-conformité pure, invisible en
# CI parce qu'aucun test ne regardait le contenu du paquet.
mkdir -p "$DEST/licenses"
cp "$SRC/LICENSE" "$DEST/licenses/GPL-3.0.txt"
cp "$SRC/packaging/licenses/GPL-2.0.txt" "$DEST/licenses/"
cp "$SRC/packaging/licenses/THIRD-PARTY.txt" "$DEST/licenses/"

# Garde-fou : seules EmuTOS + tos102uk/tos162uk (profils ST/STE) sont autorisées.
# (pas de -printf : find BSD de macOS ne le connaît pas)
STRAY=$(find "$DEST/roms" -maxdepth 1 -name '*.img' \
        ! -name 'etos*' ! -name 'tos102uk.img' ! -name 'tos162uk.img')
if [ -n "$STRAY" ]; then
    echo "ERREUR : ROM non autorisée dans le paquet : $STRAY" >&2
    exit 1
fi
# Garde-fou symétrique : en mode libre, AUCUNE ROM Atari ne doit subsister (un
# répertoire de destination réutilisé d'un build précédent en garderait une).
if [ "$NO_ATARI" = "1" ]; then
    ATARI_STRAY=$(find "$DEST/roms" -maxdepth 1 -name 'tos*.img')
    if [ -n "$ATARI_STRAY" ]; then
        echo "ERREUR : NEOST_PACKAGE_NO_ATARI_TOS=1 mais ROM Atari présente : $ATARI_STRAY" >&2
        exit 1
    fi
fi
# Garde-fou licences : le paquet ne part pas sans elles.
for lic in GPL-3.0.txt GPL-2.0.txt THIRD-PARTY.txt; do
    [ -s "$DEST/licenses/$lic" ] || { echo "ERREUR : licence manquante : $lic" >&2; exit 1; }
done
if [ "$NO_ATARI" = "1" ]; then
    echo "OK : données paquet copiées dans $DEST (EmuTOS SEUL + drivesound + diskA.st + licences)"
else
    echo "OK : données paquet copiées dans $DEST (EmuTOS + tos102uk + tos162uk + drivesound + diskA.st + licences)"
fi
