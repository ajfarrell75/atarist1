#!/usr/bin/env bash
# =============================================================================
#  provision.sh — Premier démarrage de la borne : de la carte SD à l'écran ST.
#
#  Déposé sur la partition de démarrage (/boot/firmware) AVEC son archive
#  `neost-payload.tar.gz` (sources + ROMs + disquettes), il est lancé
#  automatiquement au premier boot par cloud-init (`runcmd:` de `user-data`),
#  dans une unité transitoire `neost-provision` — donc en arrière-plan, sans
#  bloquer la fin de l'initialisation.
#
#      journalctl -u neost-provision -f      # suivre
#      tail -f /var/log/neost-provision.log  # idem, en fichier
#
#  Idempotent : on peut le relancer à la main autant de fois que nécessaire
#      sudo /boot/firmware/neost-provision.sh
#
#  POURQUOI compiler sur place plutôt que livrer l'AppImage :
#  l'AppImage publiée est aarch64 GÉNÉRIQUE (elle doit tourner du Pi 3 au Pi 5).
#  Compiler ici avec -mcpu=cortex-a72 vaut ~10-20 % sur l'interpréteur Moira,
#  c'est-à-dire précisément la marge qui manque quand le son se met à hacher.
#  Compter ~20-40 min sur un Pi 4 — la borne démarre toute seule à la fin.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -euo pipefail

BOOTDIR=/boot/firmware
[ -d "$BOOTDIR" ] || BOOTDIR=/boot
PAYLOAD="$BOOTDIR/neost-payload.tar.gz"
SRCDIR=/usr/local/src/neost
PREFIX=/opt/neost
KIOSK_USER="${NEOST_KIOSK_USER:-neost}"
LOG=/var/log/neost-provision.log

exec > >(tee -a "$LOG") 2>&1
echo "=============================================================="
echo "[provision] début : $(date -Is)"
echo "=============================================================="

[ "$(id -u)" -eq 0 ] || { echo "ERREUR : lancer en root."; exit 1; }
id "$KIOSK_USER" >/dev/null 2>&1 || {
    echo "ERREUR : utilisateur '$KIOSK_USER' inexistant (cloud-init a-t-il fini ?)."
    exit 1; }

# --- 1. Déballer les sources + les données ----------------------------------
if [ -f "$PAYLOAD" ]; then
    echo "[provision] déballage de $PAYLOAD → $SRCDIR"
    mkdir -p "$SRCDIR"
    tar -xzf "$PAYLOAD" -C "$SRCDIR" --strip-components=1
else
    [ -d "$SRCDIR" ] || { echo "ERREUR : ni $PAYLOAD ni $SRCDIR."; exit 1; }
    echo "[provision] pas d'archive, on réutilise $SRCDIR"
fi

# --- 2. Dépendances de compilation ------------------------------------------
# cloud-init les a normalement déjà posées (`packages:` de user-data) ; on
# repasse pour que le script reste utilisable seul.
echo "[provision] dépendances…"
export DEBIAN_FRONTEND=noninteractive
# -o DPkg::Lock::Timeout : au premier démarrage, unattended-upgrades tient souvent
# encore le verrou dpkg — sans attente, l'installation échouerait sèchement.
APT_OPTS=(-o DPkg::Lock::Timeout=900)
apt-get "${APT_OPTS[@]}" update -qq
apt-get "${APT_OPTS[@]}" install -y --no-install-recommends cmake g++ make libglfw3-dev libgl1-mesa-dev

# --- 3. Installer les binaires ----------------------------------------------
# Chemin RAPIDE : un tar.gz de binaires déjà compilés (CI GitHub, runner ARM64
# natif dans un conteneur bookworm, -mcpu=cortex-a72) a été déposé sur la
# partition de démarrage → rien à compiler, la borne est prête en secondes.
# Chemin de SECOURS : compilation sur place (~20-40 min sur Pi 4).
# `neost-pi400-*` est le nom actuel ; `neost-borne-*` est celui d'avant le
# renommage — accepté pour ne pas invalider une carte SD déjà préparée.
PREBUILT="$(ls "$BOOTDIR"/neost-pi*-aarch64.tar.gz "$BOOTDIR"/neost-borne-*-aarch64.tar.gz \
            2>/dev/null | head -1 || true)"
if [ -n "$PREBUILT" ]; then
    echo "[provision] binaires pré-compilés : $PREBUILT"
    install -d "$PREFIX"
    tar -xzf "$PREBUILT" -C "$PREFIX"          # → $PREFIX/bin/neost{,-headless}
    chmod 755 "$PREFIX/bin/neost" "$PREFIX/bin/neost-headless"
    # Contrôle : un binaire d'une autre architecture ou d'un plancher glibc trop
    # haut se voit tout de suite ici, pas par un écran noir au démarrage.
    "$PREFIX/bin/neost-headless" --version || {
        echo "ERREUR : les binaires pré-compilés ne s'exécutent pas ici."
        echo "         Retirer $PREBUILT et relancer pour compiler sur place."; exit 1; }
    # Les données (roms/, disks/…) que build_native_pi.sh --install aurait copiées.
    # -n : ce que l'exploitant a ajouté sur la borne n'est jamais écrasé.
    for d in roms disks fonts gemdos carts; do
        [ -d "$SRCDIR/$d" ] || continue
        install -d "$PREFIX/$d"
        cp -rn "$SRCDIR/$d/." "$PREFIX/$d/" 2>/dev/null || true
    done
else
    echo "[provision] pas de binaires pré-compilés → compilation native (~20-40 min)…"
    "$SRCDIR/packaging/raspberry/build_native_pi.sh" --install
fi

# --- 3bis. Réglages de la borne AVANT install_kiosk.sh ----------------------
# install_kiosk.sh ne crée /etc/neost-kiosk.conf que s'il est absent : en
# l'écrivant ici, on choisit la ROM et la disquette de départ sans toucher au
# script générique. ⚠ tos162uk = PAL 50 Hz (un suffixe `us` donnerait du 60 Hz
# NTSC, qui déchire les démos et les images Spectrum 512 — fidèlement).
if [ ! -f /etc/neost-kiosk.conf ]; then
    echo "[provision] /etc/neost-kiosk.conf"
    cat > /etc/neost-kiosk.conf <<EOF
# Réglages de la borne NeoST — lus par neost-kiosk.sh / neost-session.sh.
# Après modification : sudo systemctl restart neost-kiosk@${KIOSK_USER}
NEOST_PREFIX="$PREFIX"
NEOST_BIN="$PREFIX/bin/neost"
NEOST_ROM="roms/tos162uk.img"
# ⚠ TOUJOURS entre guillemets : ce fichier est SOURCÉ par un shell, et les noms
# de disquettes contiennent des espaces.
NEOST_DISK="disks/st/New Zealand Story.st"
NEOST_AUDIO_LATENCY="120"
NEOST_CRT_PRESET=""
NEOST_EXTRA_ARGS=""
EOF
fi

# --- 4. Monter la borne ------------------------------------------------------
# --keep-wifi : le Wi-Fi est ici le SEUL lien réseau (configuré dans l'Imager) ;
# le couper reviendrait à se priver de SSH sur une machine sans écran de service.
# --bluetooth-audio : le Pi 400 n'a pas de jack — HDMI ou Bluetooth. Ce mode
# installe PipeWire (seul chemin vers l'A2DP) tout en laissant l'HDMI marcher :
# WirePlumber bascule sur l'enceinte quand elle se connecte, et revient à l'HDMI
# quand elle s'éteint. Retirer l'option = HDMI seul, ALSA en direct, zéro serveur.
echo "[provision] installation du kiosk…"
"$SRCDIR/packaging/raspberry/install_kiosk.sh" \
    --user "$KIOSK_USER" --keep-wifi --bluetooth-audio

# --- 5. Configuration de départ de la borne ---------------------------------
# neost.cfg porte ce que la ligne de commande ne porte PAS : modèle de machine,
# RAM, lecteur rapide, effets CRT. En mode --kiosk il est FIGÉ (saveConfig sort
# immédiatement) : ce qu'on écrit ici est ce que la borne aura à chaque
# démarrage, quoi qu'il se passe pendant l'exposition.
if [ ! -f "$PREFIX/neost.cfg" ]; then
    echo "[provision] neost.cfg initial (STE 4 Mo, lecteur rapide, CRT éteint)"
    cat > "$PREFIX/neost.cfg" <<'EOF'
rom=roms/tos162uk.img
disk=
cart=
gemdos=
acsi=
mono=0
cpu=moira
machine=ste
mem=4m
fpu=0
joyport=1
joymap=
joydeadzone=0.3
fastfdc=1
volume=0.85
audio_latency_ms=120
showDisk=1
showCart=0
showHex=0
showCpu=0
showJoy=0
dock=1
autozoom=1
crt=0
EOF
fi
chown -R "$KIOSK_USER":"$KIOSK_USER" "$PREFIX"

echo
echo "=============================================================="
echo "[provision] TERMINÉ : $(date -Is)"
echo "  Borne      : systemctl status neost-kiosk@$KIOSK_USER"
echo "  Réglages   : /etc/neost-kiosk.conf   (ROM, disquette, latence audio, CRT)"
echo "  Journal    : journalctl -u neost-kiosk@$KIOSK_USER -f"
echo "=============================================================="

# Démarrage immédiat : pas besoin d'attendre le redémarrage pour voir l'écran ST.
# (config.txt/cmdline.txt, eux, ne prendront effet qu'au prochain boot.)
systemctl start "neost-kiosk@${KIOSK_USER}.service" || true
