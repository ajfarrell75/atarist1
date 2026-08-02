#!/usr/bin/env bash
# =============================================================================
#  install_kiosk.sh — Transforme un Raspberry Pi OS Lite en borne NeoST.
#
#  Résultat : mise sous tension → écran Atari ST, sans bureau, sans gestionnaire
#  de connexion, sans serveur de son. Le Pi ne fait plus QUE de l'émulation.
#
#  Ce que le script fait, et POURQUOI (dans l'ordre des gains mesurables) :
#
#   1. X NU (pas de WM, pas de compositeur) sur le VT 1, lancé par systemd.
#      → supprime une recopie plein écran par trame (le compositeur du bureau).
#   2. Aucun serveur de son : miniaudio parle à ALSA en direct (sortie HDMI).
#      → supprime resampling + graphe PipeWire, 1re cause de micro-coupures.
#      EXCEPTION : --bluetooth-audio réinstalle PipeWire, seul moyen de sortir
#      en A2DP (miniaudio ne sait pas parler Bluetooth). Cf. § 2 du script.
#   3. Gouverneur `performance` sur les 4 cœurs.
#      → Pi OS met `ondemand` : les rampes de fréquence produisent exactement le
#        hachage périodique « ça rame par à-coups ».
#   4. IRQ matérielles épinglées sur le cœur 0 (irqaffinity=0).
#      → la boucle d'émulation ne partage plus son cœur avec l'USB/Ethernet.
#   5. Bluetooth désactivé au niveau du device-tree (sauf --bluetooth-audio),
#      Wi-Fi aussi SI un câble Ethernet est branché (sinon on se couperait de
#      SSH — cf. le garde-fou).
#      → les interruptions brcmfmac sont une source de gigue documentée.
#   6. Swap coupé, services inutiles coupés, boot silencieux.
#
#  Usage (SUR le Pi, en root) :
#      sudo packaging/raspberry/install_kiosk.sh                 # utilisateur `pi`
#      sudo packaging/raspberry/install_kiosk.sh --user borne
#      sudo packaging/raspberry/install_kiosk.sh --keep-wifi     # borne en réseau
#      sudo packaging/raspberry/install_kiosk.sh --bluetooth-audio   # enceinte BT
#      sudo packaging/raspberry/install_kiosk.sh --uninstall     # tout défaire
#
#  PRÉ-REQUIS : le binaire doit déjà être en place, cf. build_native_pi.sh
#  --install (ou une AppImage pointée par NEOST_BIN dans /etc/neost-kiosk.conf).
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
PREFIX="${NEOST_PREFIX:-/opt/neost}"
KIOSK_USER="pi"
KEEP_WIFI=0
KEEP_AUDIO_SERVER=0
BT_AUDIO=0
UNINSTALL=0
ALSA_CARD=""
FORCE=0

while [ $# -gt 0 ]; do
    case "$1" in
        --user)              KIOSK_USER="$2"; shift 2 ;;
        --keep-wifi)         KEEP_WIFI=1; shift ;;
        --keep-audio-server) KEEP_AUDIO_SERVER=1; shift ;;
        # Le son Bluetooth EXIGE un serveur de son (A2DP n'existe pas en ALSA nu)
        # et, évidemment, que le Bluetooth ne soit pas désactivé plus bas.
        --bluetooth-audio)   BT_AUDIO=1; KEEP_AUDIO_SERVER=1; shift ;;
        --alsa-card)         ALSA_CARD="$2"; shift 2 ;;
        --uninstall)         UNINSTALL=1; shift ;;
        --force)             FORCE=1; shift ;;
        *) echo "Option inconnue : $1"; exit 1 ;;
    esac
done

[ "$(id -u)" -eq 0 ] || { echo "ERREUR : lancer avec sudo."; exit 1; }

MODEL="$(tr -d '\0' < /proc/device-tree/model 2>/dev/null || echo inconnu)"
case "$MODEL" in
    *"Raspberry Pi"*) ;;
    *) [ "$FORCE" = 1 ] || { echo "ERREUR : « $MODEL » n'est pas un Raspberry Pi (--force pour passer outre)."; exit 1; } ;;
esac

# Bookworm a déplacé la partition de démarrage de /boot vers /boot/firmware.
BOOTDIR=/boot/firmware
[ -f "$BOOTDIR/config.txt" ] || BOOTDIR=/boot
[ -f "$BOOTDIR/config.txt" ] || { echo "ERREUR : config.txt introuvable (ni /boot/firmware ni /boot)."; exit 1; }

MARK_BEGIN="# >>> NeoST kiosk (install_kiosk.sh) — ne pas éditer à la main"
MARK_END="# <<< NeoST kiosk"

log() { echo "[install_kiosk] $*"; }

# ---------------------------------------------------------------------------
#  Édition idempotente : bloc délimité par des marqueurs, remplacé s'il existe.
# ---------------------------------------------------------------------------
write_block() {           # write_block <fichier> <contenu…>
    local file="$1"; shift
    local tmp; tmp="$(mktemp)"
    [ -f "$file" ] && sed "/^${MARK_BEGIN//\//\\/}$/,/^${MARK_END}$/d" "$file" > "$tmp" || true
    if [ $# -gt 0 ]; then
        { echo "$MARK_BEGIN"; printf '%s\n' "$@"; echo "$MARK_END"; } >> "$tmp"
    fi
    # Sauvegarde unique de l'ORIGINAL : une seconde exécution ne doit pas écraser
    # la sauvegarde par une version déjà modifiée (sinon --uninstall ne rend rien).
    [ -f "$file" ] && [ ! -f "$file.neost-orig" ] && cp -a "$file" "$file.neost-orig"
    cat "$tmp" > "$file"
    rm -f "$tmp"
}

# cmdline.txt est une LIGNE UNIQUE : pas de bloc, on ajoute/retire des jetons.
cmdline_add() {           # cmdline_add <jeton…>
    local file="$BOOTDIR/cmdline.txt" line tok key
    [ -f "$file" ] || return 0
    [ -f "$file.neost-orig" ] || cp -a "$file" "$file.neost-orig"
    line="$(tr -d '\n' < "$file")"
    for tok in "$@"; do
        key="${tok%%=*}"
        # On retire d'abord toute occurrence de la CLÉ (une valeur différente d'un
        # passage précédent doit être remplacée, pas dupliquée : le noyau prendrait
        # la dernière et le fichier deviendrait illisible).
        line="$(echo " $line " | sed -E "s/ ${key}(=[^ ]*)? / /g")"
        line="$line $tok"
    done
    echo "$(echo "$line" | tr -s ' ' | sed 's/^ //;s/ $//')" > "$file"
}

cmdline_restore() {
    [ -f "$BOOTDIR/cmdline.txt.neost-orig" ] && \
        mv "$BOOTDIR/cmdline.txt.neost-orig" "$BOOTDIR/cmdline.txt" || true
}

# ---------------------------------------------------------------------------
#  Désinstallation
# ---------------------------------------------------------------------------
if [ "$UNINSTALL" = 1 ]; then
    log "désinstallation…"
    systemctl disable --now "neost-kiosk@${KIOSK_USER}.service" 2>/dev/null || true
    systemctl disable --now neost-perf.service 2>/dev/null || true
    systemctl disable --now neost-bt-connect.timer 2>/dev/null || true
    rm -f /etc/systemd/system/neost-bt-connect.{service,timer}
    rm -f /etc/systemd/system/neost-kiosk@.service /etc/systemd/system/neost-perf.service
    systemctl enable getty@tty1.service 2>/dev/null || true
    write_block "$BOOTDIR/config.txt"
    cmdline_restore
    systemctl daemon-reload
    log "fait. Le service kiosk et les réglages de démarrage sont retirés."
    log "NOTE : /opt/neost, /etc/neost-kiosk.conf et les paquets installés restent en place."
    exit 0
fi

id "$KIOSK_USER" >/dev/null 2>&1 || { echo "ERREUR : utilisateur '$KIOSK_USER' inexistant."; exit 1; }

# GARDE-FOU : couper le Wi-Fi sur une machine dont c'est le SEUL lien réseau, c'est
# se couper de SSH sur une borne qui n'a peut-être ni clavier ni écran de service.
# On ne le fait donc QUE si un lien Ethernet est effectivement branché.
if [ "$KEEP_WIFI" = 0 ]; then
    if ! grep -qs 1 /sys/class/net/eth0/carrier; then
        log "ATTENTION : pas de lien Ethernet détecté → le Wi-Fi est CONSERVÉ."
        log "            (relancer avec --force pour le couper quand même)"
        [ "$FORCE" = 1 ] || KEEP_WIFI=1
    fi
fi

# ---------------------------------------------------------------------------
#  1. Paquets : le strict minimum pour un X sans bureau
# ---------------------------------------------------------------------------
log "installation des paquets (X nu + ALSA)…"
export DEBIAN_FRONTEND=noninteractive
# -o DPkg::Lock::Timeout : au premier démarrage, unattended-upgrades tient souvent
# encore le verrou dpkg — sans attente, l'installation échouerait sèchement.
APT_OPTS=(-o DPkg::Lock::Timeout=900)
apt-get "${APT_OPTS[@]}" update -qq
# --no-install-recommends est ESSENTIEL : sans lui, xserver-xorg-core tire une
# partie du bureau (et donc un serveur de son) par recommandation.
apt-get "${APT_OPTS[@]}" install -y --no-install-recommends \
    xserver-xorg-core xserver-xorg-legacy xserver-xorg-input-libinput \
    xinit x11-xserver-utils alsa-utils libglfw3 libgl1-mesa-dri

# Xwrapper : par défaut Debian n'autorise startx qu'depuis une console de
# connexion. Le service systemd n'en est pas une → « Only console users are
# allowed to run the X server ».
install -d /etc/X11
cat > /etc/X11/Xwrapper.config <<'EOF'
# Écrit par NeoST install_kiosk.sh — X est lancé par un service systemd, pas
# par une session de connexion interactive.
allowed_users=anybody
needs_root_rights=no
EOF

# Accès direct au KMS, aux périphériques d'entrée et au VT sans passer par root.
usermod -aG video,input,tty,audio,render "$KIOSK_USER" || true

# ---------------------------------------------------------------------------
#  2. La sortie son
#
#  Le Pi 400 n'a PAS de jack : c'est HDMI ou Bluetooth. Deux modes, exclusifs :
#
#  · défaut          — aucun serveur de son, miniaudio parle à ALSA en direct
#                      (HDMI). Le moins de latence et de gigue possible.
#  · --bluetooth-audio — PipeWire + WirePlumber + BlueZ. A2DP n'existe QUE via
#                      un serveur de son : miniaudio ne sait pas le parler. On
#                      réintroduit donc la couche qu'on avait justement retirée,
#                      mais réglée pour ne pas coûter cher (48 kHz fixe = aucun
#                      rééchantillonnage puisque NeoST sort déjà en 48 kHz, et
#                      quantum large = peu de réveils). L'HDMI reste disponible
#                      dans ce mode : PipeWire bascule tout seul.
#
#  Détail qui rend le mode Bluetooth possible SANS toucher au code de NeoST :
#  miniaudio classe PulseAudio AVANT ALSA (`ma_backend` est ordonné par
#  priorité), donc NeoST se branche sur pipewire-pulse, qui déplace le flux vers
#  l'enceinte quand elle se connecte — même en pleine partie.
# ---------------------------------------------------------------------------
if [ "$BT_AUDIO" = 1 ]; then
    log "audio Bluetooth : PipeWire + WirePlumber + BlueZ…"
    apt-get "${APT_OPTS[@]}" install -y --no-install-recommends \
        pipewire pipewire-pulse pipewire-alsa wireplumber libspa-0.2-bluetooth \
        bluez bluez-tools pulseaudio-utils

    install -d /etc/pipewire/pipewire.conf.d /etc/pipewire/pipewire-pulse.conf.d
    # 48 kHz VERROUILLÉ : NeoST synthétise en 48 kHz (Audio::start). Toute autre
    # fréquence rebrancherait le rééchantillonneur pour rien. Quantum large :
    # sur un Pi 400 déjà à pleine charge, mieux vaut peu de réveils gros que
    # beaucoup de petits — la latence perçue est de toute façon dominée par
    # l'A2DP (150-250 ms), pas par le quantum.
    cat > /etc/pipewire/pipewire.conf.d/99-neost.conf <<'EOF'
# Écrit par NeoST install_kiosk.sh
context.properties = {
    default.clock.rate          = 48000
    default.clock.allowed-rates = [ 48000 ]
    default.clock.quantum       = 1024
    default.clock.min-quantum   = 1024
    default.clock.max-quantum   = 2048
}
EOF
    cat > /etc/pipewire/pipewire-pulse.conf.d/99-neost.conf <<'EOF'
# Écrit par NeoST install_kiosk.sh — c'est par CE chemin que NeoST sort
# (miniaudio préfère le backend PulseAudio à ALSA).
pulse.properties = {
    pulse.min.req      = 1024/48000
    pulse.default.req  = 2048/48000
    pulse.min.quantum  = 1024/48000
}
EOF
    # WirePlumber 0.4 (bookworm) se configure en Lua. On coupe les profils
    # casque (HSP/HFP) : une enceinte qui bascule en HSP passe en 8-16 kHz mono
    # avec le micro ouvert — le son devient un talkie-walkie. A2DP seulement.
    install -d /etc/wireplumber/bluetooth.lua.d
    cat > /etc/wireplumber/bluetooth.lua.d/99-neost.lua <<'EOF'
-- Écrit par NeoST install_kiosk.sh
bluez_monitor.properties = {
  ["bluez5.roles"]            = "[ a2dp_sink ]",
  ["bluez5.enable-sbc-xq"]    = true,
  ["bluez5.enable-msbc"]      = false,
  ["bluez5.enable-hw-volume"] = true,
  ["bluez5.autoswitch-profile"] = false,
}
EOF
    # Les services PipeWire sont des services UTILISATEUR : sans « linger », ils
    # ne démarrent qu'à l'ouverture d'une session interactive — or la borne n'en
    # ouvre jamais, elle démarre par systemd. Sans ça : aucun son du tout.
    loginctl enable-linger "$KIOSK_USER" || true
    UID_K="$(id -u "$KIOSK_USER")"
    sudo -u "$KIOSK_USER" XDG_RUNTIME_DIR="/run/user/$UID_K" \
        systemctl --user enable pipewire.socket pipewire-pulse.socket wireplumber.service \
        2>/dev/null || true
    usermod -aG bluetooth "$KIOSK_USER" 2>/dev/null || true

    install -m 755 "$HERE/neost-bt.sh" "$PREFIX/bin/" 2>/dev/null || {
        install -d "$PREFIX/bin"; install -m 755 "$HERE/neost-bt.sh" "$PREFIX/bin/"; }
    install -m 644 "$HERE/neost-bt-connect.service" "$HERE/neost-bt-connect.timer" \
        /etc/systemd/system/
    systemctl daemon-reload
    systemctl enable neost-bt-connect.timer >/dev/null 2>&1 || true

elif [ "$KEEP_AUDIO_SERVER" = 0 ]; then
    log "suppression des serveurs de son (miniaudio → ALSA en direct)…"
    for p in pipewire pipewire-pulse pipewire-alsa wireplumber pulseaudio pulseaudio-utils; do
        dpkg-query -W -f='${Status}' "$p" 2>/dev/null | grep -q "install ok installed" \
            && apt-get "${APT_OPTS[@]}" purge -y "$p" || true
    done
    # Les unités utilisateur survivent à la purge si elles ont été copiées.
    sudo -u "$KIOSK_USER" systemctl --user mask pipewire.socket pipewire-pulse.socket \
        pulseaudio.socket 2>/dev/null || true
else
    log "serveur de son conservé (--keep-audio-server) — attention aux micro-coupures."
fi

# --- Carte de sortie ALSA (mode direct uniquement) --------------------------
# En mode PipeWire, c'est WirePlumber qui choisit et bascule : on ne fige rien.
if [ "$BT_AUDIO" = 0 ]; then
    if [ -z "$ALSA_CARD" ]; then
        # Le Pi 400 a DEUX sorties HDMI (vc4hdmi0/1). On prend celle où un écran
        # est réellement branché : l'ELD n'est valide que si un moniteur répond.
        for eld in /proc/asound/card*/eld#*; do
            [ -r "$eld" ] || continue
            grep -q "eld_valid.*1" "$eld" 2>/dev/null || continue
            cand="${eld#/proc/asound/card}"; cand="${cand%%/*}"
            grep -qi "vc4hdmi" "/proc/asound/card$cand/id" 2>/dev/null || continue
            ALSA_CARD="$cand"; log "sortie HDMI détectée : carte $cand ($(cat /proc/asound/card$cand/id))"
            break
        done
    fi
    if [ -n "$ALSA_CARD" ]; then
        log "carte ALSA par défaut → $ALSA_CARD"
        cat > /etc/asound.conf <<EOF
# Écrit par NeoST install_kiosk.sh — carte de sortie de la borne.
defaults.pcm.card $ALSA_CARD
defaults.ctl.card $ALSA_CARD
EOF
    else
        log "AUCUNE sortie HDMI détectée — brancher l'écran puis relancer, ou --alsa-card N"
    fi
fi

# ---------------------------------------------------------------------------
#  3. Gouverneur `performance` — le réglage au meilleur rapport gain/effort
# ---------------------------------------------------------------------------
log "gouverneur CPU → performance"
cat > /etc/systemd/system/neost-perf.service <<'EOF'
[Unit]
Description=NeoST — gouverneur CPU performance (borne)
# La borne tourne à pleine charge en permanence : `ondemand` ne fait que
# rajouter de la latence de montée en fréquence, jamais d'économie utile.
[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/bin/sh -c 'for g in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor; do echo performance > "$g" || true; done'
[Install]
WantedBy=multi-user.target
EOF
systemctl enable neost-perf.service >/dev/null

# ---------------------------------------------------------------------------
#  4. Démarrage : config.txt / cmdline.txt
# ---------------------------------------------------------------------------
log "réglages de démarrage dans $BOOTDIR"
CFG_LINES=(
    "disable_splash=1"        # pas d'arc-en-ciel Broadcom au démarrage
    "boot_delay=0"
    "arm_boost=1"             # Pi 4/400 : 1,5 → 1,8 GHz (officiel, sans surcadençage)
)
# Le Bluetooth ne peut évidemment pas être coupé si c'est lui qui porte le son.
[ "$BT_AUDIO" = 0 ] && CFG_LINES+=("dtoverlay=disable-bt")
[ "$KEEP_WIFI" = 0 ] && CFG_LINES+=("dtoverlay=disable-wifi")
CFG_LINES+=(
    ""
    "# Surcadençage : à activer SEULEMENT avec un vrai dissipateur et une"
    "# alimentation officielle. Vérifier ensuite `vcgencmd get_throttled` == 0x0."
    "#over_voltage=6"
    "#arm_freq=2000"
)
write_block "$BOOTDIR/config.txt" "${CFG_LINES[@]}"

# irqaffinity=0     : IRQ matérielles sur le cœur 0 → cœurs 1-3 pour l'émulation.
# consoleblank=0    : la console ne s'éteint jamais (écran noir en expo).
# quiet/logo.nologo : démarrage sans texte ni logo framboise.
cmdline_add irqaffinity=0 consoleblank=0 quiet loglevel=3 logo.nologo vt.global_cursor_default=0

# ---------------------------------------------------------------------------
#  5. Services inutiles à une borne hors ligne
# ---------------------------------------------------------------------------
log "extinction des services inutiles…"
# avahi-daemon est VOLONTAIREMENT conservé : c'est lui qui fait répondre
# `<hôte>.local`, seule façon commode de reprendre la main sur une borne sans
# écran. Son coût à l'exécution est négligeable devant l'émulation.
SERVICES_OFF="ModemManager triggerhappy cups cups-browsed lightdm gdm3
              apt-daily.timer apt-daily-upgrade.timer man-db.timer"
[ "$BT_AUDIO" = 0 ] && SERVICES_OFF="$SERVICES_OFF bluetooth hciuart"
for s in $SERVICES_OFF; do
    systemctl disable --now "$s" 2>/dev/null || true
done
[ "$KEEP_WIFI" = 0 ] && systemctl disable --now wpa_supplicant 2>/dev/null || true
# L'attente de réseau ajoute des secondes de boot pour rien sur une borne.
systemctl mask NetworkManager-wait-online.service systemd-networkd-wait-online.service 2>/dev/null || true
# Swap : sur SD, un swap-in au milieu d'une trame = un trou audio garanti.
if [ -x /usr/sbin/dphys-swapfile ]; then
    dphys-swapfile swapoff 2>/dev/null || true
    systemctl disable --now dphys-swapfile 2>/dev/null || true
fi
systemctl set-default multi-user.target >/dev/null

# ---------------------------------------------------------------------------
#  6. Scripts de session + configuration de la borne
# ---------------------------------------------------------------------------
install -d "$PREFIX/bin"
install -m 755 "$HERE/neost-kiosk.sh" "$HERE/neost-session.sh" "$PREFIX/bin/"

# Créé UNE SEULE FOIS : une réinstallation ne doit pas écraser le choix de ROM
# et de disquette de l'exploitant.
if [ ! -f /etc/neost-kiosk.conf ]; then
    log "création de /etc/neost-kiosk.conf (réglages de la borne)"
    cat > /etc/neost-kiosk.conf <<EOF
# Réglages de la borne NeoST — lus par neost-kiosk.sh / neost-session.sh.
# Après modification : sudo systemctl restart neost-kiosk@${KIOSK_USER}

NEOST_PREFIX="$PREFIX"
# Binaire : soit la compilation native (recommandé), soit une AppImage.
NEOST_BIN="$PREFIX/bin/neost"

# ROM et disquette, relatives à NEOST_PREFIX.
# ⚠ Le suffixe de la ROM FIXE la fréquence de balayage : `us` → 60 Hz NTSC,
# `uk`/`fr`/`de`/`es` → 50 Hz PAL. Les démos et les images Spectrum 512 sont
# calculées pour le 50 Hz : une ROM `us` les affiche déchirées (fidèlement).
NEOST_ROM="roms/tos162uk.img"
NEOST_DISK=""          # ⚠ entre guillemets : ce fichier est SOURCÉ (noms à espaces)

# Coussin audio en ms (--audio-latency, défaut NeoST 85, borné [20,250]).
# Sur Pi 4 : 120 est un bon point de départ. Monter à 150 si le journal montre
# encore des « underrun anneau » ; descendre à 85 si le son paraît en retard.
NEOST_AUDIO_LATENCY="120"

# Effets CRT : off | leger | arcade | phosphor (vide = ce que dit neost.cfg).
# ⚠ C'est un shader plein écran : sur Pi 4 c'est le premier poste à couper si
# la boucle d'émulation ne tient pas 50 trames/s.
NEOST_CRT_PRESET=""

# Arguments supplémentaires passés tels quels (ex. --kiosk-monitor 1).
NEOST_EXTRA_ARGS=""
EOF
fi

# ---------------------------------------------------------------------------
#  7. Le service
# ---------------------------------------------------------------------------
install -m 644 "$HERE/neost-kiosk@.service" /etc/systemd/system/
systemctl daemon-reload
systemctl enable "neost-kiosk@${KIOSK_USER}.service" >/dev/null

# ---------------------------------------------------------------------------
#  Bilan
# ---------------------------------------------------------------------------
echo
log "installation terminée."
echo
if [ ! -x "$PREFIX/bin/neost" ]; then
    echo "  ⚠ $PREFIX/bin/neost est ABSENT. Compiler et installer d'abord :"
    echo "      sudo packaging/raspberry/build_native_pi.sh --install"
    echo "    (ou pointer NEOST_BIN vers une AppImage dans /etc/neost-kiosk.conf)"
    echo
fi
echo "  Réglages de la borne : /etc/neost-kiosk.conf"
echo "  Sortie audio dispo   :"; aplay -l 2>/dev/null | sed 's/^/      /' || true
echo
if [ "$BT_AUDIO" = 1 ]; then
    echo "  Enceinte Bluetooth   :  sudo $PREFIX/bin/neost-bt.sh scan"
    echo "                          sudo $PREFIX/bin/neost-bt.sh pair <ADRESSE>"
    echo "  ⚠ l'A2DP ajoute 150-250 ms de retard son/image, inhérents au Bluetooth."
    echo
fi
echo "  Redémarrer pour appliquer config.txt/cmdline.txt :  sudo reboot"
echo "  Sans redémarrer, tester le service               :  sudo systemctl start neost-kiosk@${KIOSK_USER}"
echo "  Journal (dont les diagnostics [Audio])           :  journalctl -u neost-kiosk@${KIOSK_USER} -f"
echo "  Tout défaire                                     :  sudo $0 --uninstall --user ${KIOSK_USER}"
