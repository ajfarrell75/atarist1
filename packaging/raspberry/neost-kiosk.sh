#!/bin/sh
# =============================================================================
#  neost-kiosk.sh — Point d'entrée du service systemd : démarre un X NU.
#
#  « X nu » = un serveur X SANS gestionnaire de fenêtres et SANS compositeur.
#  C'est le cœur du gain : sur un bureau (labwc/wayfire/mutter), chaque trame de
#  NeoST est recopiée une fois de plus par le compositeur avant d'atteindre
#  l'écran — sur un Pi 4 cette passe supplémentaire coûte plusieurs ms par trame,
#  soit exactement la marge qui manque et qui fait sous-alimenter l'anneau audio.
#  Sans WM, la fenêtre plein écran de NeoST est page-flippée directement par KMS.
#
#  -keeptty : le service fournit déjà /dev/tty1 (TTYPath=), X ne doit pas en
#             ouvrir un autre — sinon il rate son VT et sort avec « no screens ».
#  -novtswitch : la borne ne doit pas pouvoir basculer sur une console texte.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -eu

[ -r /etc/neost-kiosk.conf ] && . /etc/neost-kiosk.conf
NEOST_PREFIX="${NEOST_PREFIX:-/opt/neost}"

exec /usr/bin/startx "$NEOST_PREFIX/bin/neost-session.sh" -- \
     :0 vt1 -keeptty -novtswitch -nolisten tcp -nocursor
