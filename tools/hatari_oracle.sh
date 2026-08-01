#!/usr/bin/env bash
# Oracle Hatari headless : boot ST + disque → AVI PNG → frame PNG extraite.
# Usage : hatari_oracle.sh <tos> <disk> <run-vbls> <frame-n> <out.png> [machine]
set -euo pipefail
# Racine du dépôt = parent du dossier tools/ (robuste au nom du dossier, ex. neost↔NEOST).
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if [[ -x "$ROOT/extern/hatari/build/src/hatari" ]]; then
  HATARI="$ROOT/extern/hatari/build/src/hatari"
elif command -v hatari >/dev/null; then
  HATARI="$(command -v hatari)"
else
  echo "hatari introuvable (extern/hatari ou PATH)" >&2; exit 1
fi
TOS=${1:?tos}; DISK=${2:?disk}; VBLS=${3:-1400}; FRAME=${4:-1300}; OUT=${5:-/tmp/hatari_frame.png}
MACHINE=${6:-st}
# 8ᵉ arg : taille RAM en Mo au format Hatari (--memsize : 0 = 512 Ko, 1 = 1 Mo…).
# SANS elle, Hatari bootait avec sa taille par défaut alors que NeoST utilise celle de
# l'étalon : timeline de boot différente, donc numéros de trame décalés — c'est ce qui
# rendait l'oracle de spectrum512_diapo inexploitable (image noire).
MEMSIZE=${8:-0}
# 7ᵉ arg optionnel « fastfdc » : aligne la timeline oracle sur un run NeoST --fastfdc
# (sinon FDC vitesse réelle — les numéros de trame ne correspondent PAS entre les deux).
FASTFDC=()
[[ "${7:-}" == "fastfdc" ]] && FASTFDC=(--fastfdc on)
AVI=/tmp/hatari_oracle.avi
rm -f "$AVI"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOME=/tmp/hatari_home
mkdir -p /tmp/hatari_home
# --avirecord : booléen explicite (« on ») requis sur le build Linux (extern/hatari),
# refusé par le binaire Homebrew macOS (flag nu). On tente « on », repli flag nu.
run_hatari() {
  "$HATARI" --machine "$MACHINE" --tos "$TOS" --monitor rgb \
    --memsize "$MEMSIZE" \
    --disk-a "$DISK" \
    --sound off --fast-forward on --confirm-quit off --statusbar off \
    --frameskips 0 --alert-level fatal \
    ${FASTFDC[@]+"${FASTFDC[@]}"} \
    --run-vbls "$VBLS" \
    "$@" --avi-vcodec png --avi-file "$AVI" >/tmp/hatari_oracle.log 2>&1 || true
}
run_hatari --avirecord on
if [[ ! -s "$AVI" ]]; then
  run_hatari --avirecord
fi
# Extrait la frame demandée (l'AVI a 1 image par VBL avec --frameskips 0).
ffmpeg -y -loglevel error -i "$AVI" -vf "select=eq(n\,$FRAME)" -frames:v 1 -update 1 "$OUT"
echo "oracle frame $FRAME -> $OUT ($MACHINE)"
identify "$OUT" 2>/dev/null || true
