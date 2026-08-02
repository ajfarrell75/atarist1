#!/usr/bin/env bash
# =============================================================================
#  neost-bt.sh — Enceinte Bluetooth de la borne : appairage et reconnexion.
#
#  Le Pi 400 n'a PAS de sortie jack : le son sort par HDMI ou par Bluetooth.
#  Ce script gère le second cas.
#
#      sudo neost-bt.sh scan              # 20 s de découverte + liste
#      sudo neost-bt.sh pair AA:BB:CC:DD:EE:FF
#      sudo neost-bt.sh connect           # rebranche l'appareil mémorisé
#      sudo neost-bt.sh status
#
#  `pair` mémorise l'adresse dans /etc/neost-kiosk.conf (NEOST_BT_MAC) ; un
#  timer systemd rappelle ensuite `connect` toutes les 30 s, ce qui rattrape
#  l'enceinte allumée APRÈS la borne — cas normal en exposition.
#
#  POURQUOI PipeWire est indispensable ici : miniaudio ne parle qu'ALSA ou
#  PulseAudio, jamais A2DP directement, et NeoST ouvre UN périphérique au
#  démarrage sans jamais en changer. C'est donc PipeWire qui doit déplacer le
#  flux vers l'enceinte quand elle arrive — d'où `pactl move-sink-input` plus
#  bas, qui rattrape une partie déjà en cours.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -euo pipefail

CONF=/etc/neost-kiosk.conf
[ -r "$CONF" ] && . "$CONF"
KIOSK_USER="${NEOST_KIOSK_USER:-neost}"
BT_MAC="${NEOST_BT_MAC:-}"

[ "$(id -u)" -eq 0 ] || { echo "ERREUR : lancer avec sudo."; exit 1; }

# pactl doit parler au pipewire-pulse de l'UTILISATEUR de la borne, pas à celui
# de root (qui n'en a pas) : d'où le sudo -u + XDG_RUNTIME_DIR explicite.
as_user() {
    local uid; uid="$(id -u "$KIOSK_USER")"
    sudo -u "$KIOSK_USER" XDG_RUNTIME_DIR="/run/user/$uid" "$@"
}

# Mémorise/remplace une clé dans /etc/neost-kiosk.conf.
set_conf() {              # set_conf CLÉ VALEUR
    touch "$CONF"
    if grep -qE "^${1}=" "$CONF"; then
        sed -i "s|^${1}=.*|${1}=\"${2}\"|" "$CONF"
    else
        printf '%s="%s"\n' "$1" "$2" >> "$CONF"
    fi
}

# Nom du puits PipeWire correspondant à l'appareil (bluez_output.<MAC>.<profil>,
# le suffixe de profil varie selon la version de WirePlumber → on cherche).
bt_sink() {
    as_user pactl list short sinks 2>/dev/null \
        | grep -F "bluez_output.${BT_MAC//:/_}" | head -1 | cut -f2
}

case "${1:-status}" in

scan)
    bluetoothctl power on >/dev/null
    echo "[neost-bt] découverte pendant 20 s — allume l'enceinte en mode appairage…"
    bluetoothctl --timeout 20 scan on >/dev/null 2>&1 || true
    echo
    bluetoothctl devices
    echo
    echo "Puis : sudo neost-bt.sh pair <ADRESSE>"
    ;;

pair)
    MAC="${2:-}"
    [ -n "$MAC" ] || { echo "usage : neost-bt.sh pair AA:BB:CC:DD:EE:FF"; exit 1; }
    bluetoothctl power on >/dev/null
    # `trust` AVANT `connect` : sans confiance, BlueZ redemande une autorisation
    # à chaque reconnexion — sur une borne sans clavier, personne ne répondra.
    bluetoothctl pair "$MAC"  || echo "[neost-bt] pair a échoué (déjà appairé ?) — on continue"
    bluetoothctl trust "$MAC" || true
    bluetoothctl connect "$MAC"
    set_conf NEOST_BT_MAC "$MAC"
    systemctl enable --now neost-bt-connect.timer >/dev/null 2>&1 || true
    echo "[neost-bt] $MAC mémorisé. Reconnexion automatique activée."
    exec "$0" connect
    ;;

connect)
    [ -n "$BT_MAC" ] || { echo "[neost-bt] aucun appareil mémorisé (neost-bt.sh pair …)"; exit 0; }
    bluetoothctl power on >/dev/null 2>&1 || true
    if ! bluetoothctl info "$BT_MAC" 2>/dev/null | grep -q "Connected: yes"; then
        bluetoothctl connect "$BT_MAC" >/dev/null 2>&1 || {
            echo "[neost-bt] $BT_MAC injoignable (enceinte éteinte ?)"; exit 0; }
        sleep 2
    fi
    SINK="$(bt_sink || true)"
    [ -n "$SINK" ] || { echo "[neost-bt] connecté, mais aucun puits A2DP (profil HSP ?)"; exit 0; }
    as_user pactl set-default-sink "$SINK" >/dev/null 2>&1 || true
    # Rattrapage : NeoST tourne peut-être déjà et son flux est resté sur l'HDMI.
    while read -r id _; do
        [ -n "$id" ] && as_user pactl move-sink-input "$id" "$SINK" >/dev/null 2>&1 || true
    done < <(as_user pactl list short sink-inputs 2>/dev/null || true)
    echo "[neost-bt] son dirigé vers $SINK"
    ;;

status)
    echo "appareil mémorisé : ${BT_MAC:-<aucun>}"
    [ -n "$BT_MAC" ] && bluetoothctl info "$BT_MAC" 2>/dev/null | grep -E "Name|Connected|Icon" || true
    echo "--- puits PipeWire ---"
    as_user pactl list short sinks 2>/dev/null || echo "(pipewire-pulse injoignable)"
    echo "--- puits par défaut ---"
    as_user pactl get-default-sink 2>/dev/null || true
    ;;

*)
    echo "usage : neost-bt.sh {scan|pair <MAC>|connect|status}"; exit 1 ;;
esac
