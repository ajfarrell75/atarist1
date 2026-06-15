#!/usr/bin/env bash
# =============================================================================
#  beamsync_diff.sh — diff cycle-exact NeoST ↔ oracle Hatari de la PHASE
#  CPU↔faisceau : où (cycle/ligne du faisceau) chaque IRQ/exception est prise,
#  et où le CPU échantillonne le compteur vidéo $FF8205/07/09.
#
#  Usage : beamsync_diff.sh <tos> <disk|-> <vbls> [machine]
#  Ex.   : tools/beamsync_diff.sh roms/tos102uk.img - 250 st
#          tools/beamsync_diff.sh roms/tos102fr.img "disks/st/Enchanted Land (1990)(Thalion).st" 1200 st
#
#  Produit /tmp/bs_neo.txt (NeoST) et /tmp/bs_hat.txt (Hatari) + un résumé des
#  exceptions/IRQ et de leur cycle. Sert de garde-fou au chantier beam-sync :
#  l'objectif est que les cycles d'entrée d'IRQ (et leur jitter trame-à-trame)
#  CONVERGENT vers l'oracle. (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -uo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HEADLESS="$ROOT/build/neost-headless"
HATARI="${HATARI:-/opt/homebrew/bin/hatari}"

TOS=${1:?tos}; DISK=${2:-"-"}; VBLS=${3:-250}; MACHINE=${4:-st}
NEO=/tmp/bs_neo.txt; HAT=/tmp/bs_hat.txt

# ---- NeoST : trace instructions + IRQ + lectures compteur vidéo --------------
neo_args=("$ROOT/$TOS" --machine "$MACHINE" --mem 512k --frames "$VBLS"
          --trace "$NEO" --regs --irq --fastfdc)
[ "$DISK" != "-" ] && neo_args+=(--disk "$ROOT/$DISK")
echo "[neost] $HEADLESS ${neo_args[*]}"
NEOST_VC_TRACE=1 NEOST_SYNC_TRACE=1 "$HEADLESS" "${neo_args[@]}" >/tmp/bs_neo.log 2>&1

# ---- Hatari : exceptions + VBL/HBL + cycles vidéo ----------------------------
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy HOME=/tmp/hatari_home
mkdir -p /tmp/hatari_home
hat_args=(--machine "$MACHINE" --tos "$ROOT/$TOS"
          --sound off --fast-forward on --confirm-quit off --statusbar off
          --frameskips 0 --alert-level fatal
          --trace cpu_exception,video_vbl,video_hbl,video_addr
          --trace-file "$HAT" --run-vbls "$VBLS")
[ "$DISK" != "-" ] && hat_args+=(--disk-a "$ROOT/$DISK")
echo "[hatari] $HATARI ${hat_args[*]}"
"$HATARI" "${hat_args[@]}" >/tmp/bs_hat.log 2>&1 || true

# ---- Résumé ------------------------------------------------------------------
echo "=============================================================="
echo "NeoST IRQ pris (échantillon) :"; grep -iE 'irq|interrupt|exception|level' "$NEO" 2>/dev/null | head -8
echo "--------------------------------------------------------------"
echo "Hatari exceptions (échantillon) :"; grep -iE 'exception|autovector|interrupt' "$HAT" 2>/dev/null | head -8
echo "--------------------------------------------------------------"
echo "Hatari video_vbl (cycle d'entrée VBL par trame) :"; grep -iE 'vbl' "$HAT" 2>/dev/null | head -8
echo "=============================================================="
echo "traces complètes : $NEO  /  $HAT  (logs : /tmp/bs_*.log)"
