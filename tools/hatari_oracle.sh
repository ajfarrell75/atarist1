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
# ---------------------------------------------------------------- ÉPINGLAGE --
# Chantier A5. `extern/hatari` est gitignoré et n'est PAS un sous-module : rien ne
# le rapatrie, rien ne fixe SA VERSION, et la recette documentée faisait un
# `git clone --depth 1` — c'est-à-dire « le HEAD du jour ». Deux oracles bâtis à
# deux semaines d'écart pouvaient donc produire des références différentes sans
# que rien ne le signale, alors que TOUTE la méthode du projet repose sur lui.
# On ne bloque pas (un oracle plus récent reste utilisable) : on AVERTIT, pour
# qu'une référence régénérée contre un autre oracle soit visible dans le journal.
HATARI_PIN=f0736b24b32b0439300b52107ba6ab434469ec3c   # v2.6.1-devel, 2026-08-18
if [[ -d "$ROOT/extern/hatari/.git" ]]; then
  HAVE=$(git -C "$ROOT/extern/hatari" rev-parse HEAD 2>/dev/null || echo inconnu)
  if [[ "$HAVE" != "$HATARI_PIN" ]]; then
    echo "⚠ oracle Hatari NON ÉPINGLÉ : ${HAVE:0:12} au lieu de ${HATARI_PIN:0:12}." >&2
    echo "  Les références oracle du dépôt ont été posées avec la version épinglée." >&2
    echo "  Réaligner : tools/setup_hatari.sh   (ou assumer et re-poser les références)" >&2
  fi
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
# ⚠ `--cpu-exact on --compatible on` sont passés EXPLICITEMENT, alors que ce sont
# aujourd'hui les défauts d'Hatari. Mesuré le 2026-08-26 sur l'étalon blitter_timer :
# forcer les deux à `off` déplace la comparaison de 69 px (397 → 328). Une référence
# oracle dépend donc de ces deux réglages, et s'en remettre au DÉFAUT d'un binaire
# qu'on ne contrôle pas serait laisser une référence bouger sans qu'aucune ligne du
# dépôt n'ait changé.
# Chemins UNIQUES par processus : deux --oracle concurrents sur la même machine
# se marchaient dessus via /tmp/hatari_oracle.avi partagé (la frame extraite
# pouvait venir de l'AVI de L'AUTRE étalon, puis être installée en référence).
AVI=/tmp/hatari_oracle.$$.avi
LOG=/tmp/hatari_oracle.$$.log
trap 'rm -f "$AVI" "$LOG"' EXIT
rm -f "$AVI"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOME=/tmp/hatari_home
mkdir -p /tmp/hatari_home
# --avirecord : booléen explicite (« on ») requis sur le build Linux (extern/hatari),
# refusé par le binaire Homebrew macOS (flag nu). On tente « on », repli flag nu.
run_hatari() {
  "$HATARI" --machine "$MACHINE" --tos "$TOS" --monitor rgb \
    --memsize "$MEMSIZE" \
    --cpu-exact on --compatible on \
    --disk-a "$DISK" \
    --sound off --fast-forward on --confirm-quit off --statusbar off \
    --frameskips 0 --alert-level fatal \
    ${FASTFDC[@]+"${FASTFDC[@]}"} \
    --run-vbls "$VBLS" \
    "$@" --avi-vcodec png --avi-file "$AVI" >"$LOG" 2>&1 || true
}
run_hatari --avirecord on
if [[ ! -s "$AVI" ]]; then
  run_hatari --avirecord
fi
if [[ ! -s "$AVI" ]]; then
  echo "hatari n'a produit aucun AVI (crash ? voir le log) :" >&2
  tail -20 "$LOG" >&2 || true
  exit 1
fi
# HATARI_ORACLE_SCAN=N : au lieu d'UNE image, extrait la FENÊTRE [FRAME-N, FRAME+N]
# dans "<OUT sans .png>.scan/f_%05d.png" (l'appelant choisit). Indispensable pour les
# étalons qui BOOTENT UN DISQUE : Hatari fait `Hatari_srand(time(NULL))` (sdl/main_sdl.c)
# et tire au hasard la POSITION ANGULAIRE INITIALE de la disquette
# (fdc.c, IndexPulse_Time = ... - Hatari_rand() % ...), donc la durée du boot — et
# donc la numérotation des trames — CHANGE d'un run à l'autre. Mesuré le 2026-08-19
# sur cuddly_demos : la même trame NeoST tombait sur la trame Hatari n+61 dans un run
# et n-2 dans un autre. Un `frame:` figé ne peut pas être juste par construction ;
# la fenêtre, elle, retrouve l'image.
if [[ -n "${HATARI_ORACLE_SCAN:-}" ]]; then
  SCAN_N="$HATARI_ORACLE_SCAN"
  SCAN_DIR="${OUT%.png}.scan"
  rm -rf "$SCAN_DIR"; mkdir -p "$SCAN_DIR"
  LO=$(( FRAME - SCAN_N )); [[ $LO -lt 0 ]] && LO=0
  HI=$(( FRAME + SCAN_N ))
  ffmpeg -y -loglevel error -i "$AVI" -vf "select='between(n,$LO,$HI)'" \
         -vsync 0 -start_number "$LO" "$SCAN_DIR/f_%05d.png"
  N=$(ls "$SCAN_DIR" | wc -l | tr -d ' ')
  if [[ "$N" -eq 0 ]]; then
    echo "fenêtre $LO-$HI hors de l'AVI (${VBLS} VBL demandées)" >&2; exit 1
  fi
  echo "oracle fenêtre $LO-$HI ($N images) -> $SCAN_DIR ($MACHINE)"
  exit 0
fi

# Extrait la frame demandée (l'AVI a 1 image par VBL avec --frameskips 0).
# ⚠ OUT est supprimé D'ABORD et vérifié APRÈS : si la frame demandée dépasse la
# fin de l'AVI (Hatari sorti court), ffmpeg n'écrit RIEN et sort 0 — un PNG
# périmé d'un run précédent aurait alors été installé comme référence.
rm -f "$OUT"
ffmpeg -y -loglevel error -i "$AVI" -vf "select=eq(n\,$FRAME)" -frames:v 1 -update 1 "$OUT"
if [[ ! -s "$OUT" ]]; then
  echo "frame $FRAME hors de l'AVI (${VBLS} VBL demandées — Hatari sorti court ?)" >&2
  exit 1
fi
echo "oracle frame $FRAME -> $OUT ($MACHINE)"
identify "$OUT" 2>/dev/null || true
