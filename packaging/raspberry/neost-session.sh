#!/bin/sh
# =============================================================================
#  neost-session.sh — Session X de la borne : lancé PAR startx, dans le serveur X.
#
#  Rôle : neutraliser tout ce qu'un X nu fait encore de gênant pour une borne
#  (économiseur, DPMS, fond gris moiré), puis DEVENIR NeoST (exec → pas de shell
#  intermédiaire qui traînerait en mémoire, et le code de sortie de NeoST est
#  celui de la session, donc Restart=always de systemd relance vraiment).
#
#  Les paramètres viennent de /etc/neost-kiosk.conf (écrit par install_kiosk.sh).
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -eu

# Réglages de la borne (ROM, disquette, preset CRT, latence audio…).
[ -r /etc/neost-kiosk.conf ] && . /etc/neost-kiosk.conf

NEOST_PREFIX="${NEOST_PREFIX:-/opt/neost}"
NEOST_BIN="${NEOST_BIN:-$NEOST_PREFIX/bin/neost}"
NEOST_ROM="${NEOST_ROM:-roms/tos162uk.img}"
NEOST_DISK="${NEOST_DISK:-}"
NEOST_CRT_PRESET="${NEOST_CRT_PRESET:-}"
# `-` et pas `:-` : une valeur VOLONTAIREMENT vide dans /etc/neost-kiosk.conf
# doit rester vide (elle supprime l'option — seule échappatoire si NEOST_BIN
# pointe vers un binaire antérieur à `--audio-latency`, qui prendrait la valeur
# pour un argument positionnel, donc pour un chemin de ROM).
NEOST_AUDIO_LATENCY="${NEOST_AUDIO_LATENCY-120}"
NEOST_EXTRA_ARGS="${NEOST_EXTRA_ARGS:-}"

# Pas d'extinction d'écran ni d'économiseur : une borne d'expo affiche en continu.
xset s off -dpms s noblank 2>/dev/null || true
# Fond noir : sans ça X affiche son damier gris pendant le chargement du TOS, et
# la moindre bordure non couverte par l'écran ST reste grise.
xsetroot -solid black 2>/dev/null || true

# resolveData() de NeoST cherche d'abord le chemin tel quel (relatif au CWD),
# puis relatif à exeDir. On se place dans le préfixe pour que « roms/x.img »
# et « disks/y.st » de /etc/neost-kiosk.conf soient résolus de façon évidente.
cd "$NEOST_PREFIX"

set -- --kiosk
[ -n "$NEOST_AUDIO_LATENCY" ] && set -- "$@" --audio-latency "$NEOST_AUDIO_LATENCY"
[ -n "$NEOST_CRT_PRESET" ] && set -- "$@" --crt-preset "$NEOST_CRT_PRESET"
# shellcheck disable=SC2086  # NEOST_EXTRA_ARGS est volontairement redécoupé
[ -n "$NEOST_EXTRA_ARGS" ] && set -- "$@" $NEOST_EXTRA_ARGS
set -- "$@" "$NEOST_ROM"
[ -n "$NEOST_DISK" ] && set -- "$@" "$NEOST_DISK"

echo "[neost-session] exec $NEOST_BIN $*"
exec "$NEOST_BIN" "$@"
