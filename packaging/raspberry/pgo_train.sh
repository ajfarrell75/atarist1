#!/usr/bin/env bash
# =============================================================================
#  pgo_train.sh — Parcours d'ENTRAÎNEMENT pour la compilation guidée par profil.
#
#  Appelé par build_native_pi.sh --pgo entre les deux passes de compilation. Le
#  binaire passé en argument est la version INSTRUMENTÉE (-fprofile-generate) :
#  chaque exécution dépose des compteurs, que la seconde passe relit.
#
#  POURQUOI le PGO compte ici plus qu'ailleurs : la boucle chaude de NeoST est
#  l'interpréteur Moira, c'est-à-dire un branchement indirect sur l'opcode suivi
#  d'un très grand nombre de branches conditionnelles rares. Sans profil, GCC
#  suppose les deux issues équiprobables ; avec, il ordonne les blocs de façon
#  que le cas fréquent tombe en séquence — moins de sauts pris, moins de pression
#  sur le prédicteur, et surtout un cache d'instructions bien mieux utilisé. Sur
#  un Cortex-A72 (32 Ko de L1i, prédicteur modeste face à un cœur x86 de bureau)
#  c'est précisément là que se joue le débit.
#
#  CE QUE LE PARCOURS DOIT COUVRIR — un profil trop étroit est pire qu'aucun
#  profil : il fait déclarer « froid » du code qui ne l'est pas. On balaie donc
#  les grandes familles de charge, pas seulement un boot :
#    · boot TOS et bureau GEM (ST et STE, 50 et 60 Hz)
#    · un jeu en basse résolution avec musique YM et lecteur disquette
#    · une démo à effets de bordure / écritures freq-res (chemin Glue complet)
#    · la haute résolution monochrome (chemin de rendu séparé)
#    · le blitter
#
#  Usage :  pgo_train.sh <chemin/neost-headless> [racine-du-dépôt]
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
set -uo pipefail

HEADLESS="${1:?usage: pgo_train.sh <neost-headless> [racine]}"
ROOT="${2:-$(cd "$(dirname "$0")/../.." && pwd)}"
cd "$ROOT"

[ -x "$HEADLESS" ] || { echo "ERREUR : $HEADLESS introuvable ou non exécutable"; exit 1; }

# Une charge d'entraînement ne DOIT PAS faire échouer la compilation : un jeu
# absent (les images ne sont pas toutes redistribuables) se solde par un profil
# un peu moins riche, pas par un build cassé.
run() {                      # run <libellé> <arguments…>
    local label="$1"; shift
    printf '  [pgo] %-34s' "$label"
    if "$HEADLESS" "$@" >/dev/null 2>&1; then echo "ok"; else echo "ignoré"; fi
}

# Un fichier n'existe pas → on saute la charge qui en dépend (plutôt que de
# compter sur le code d'erreur, qui entraînerait alors les chemins d'ERREUR).
have() { [ -f "$1" ]; }

echo "[pgo_train] parcours d'entraînement — $HEADLESS"

# --- 1. Boot / bureau : le chemin le plus universel -------------------------
have roms/tos162uk.img && run "boot TOS 1.62 PAL (STE)"  roms/tos162uk.img --frames 400
have roms/tos102uk.img && run "boot TOS 1.02 PAL (ST)"   roms/tos102uk.img --machine st --mem 1m --frames 400
have roms/tos162us.img && run "boot TOS 1.62 NTSC 60 Hz" roms/tos162us.img --frames 300
have roms/etos256fr.img && run "boot EmuTOS 256K"        roms/etos256fr.img --frames 300

# --- 2. Haute résolution monochrome : autre chemin de rendu -----------------
have roms/tos162uk.img && run "boot monochrome (640x400)" roms/tos162uk.img --mono --frames 300

# --- 3. Jeu : YM, disquette, basse résolution, blitter ----------------------
NZS="disks/st/New Zealand Story.st"
have "$NZS" && run "jeu (New Zealand Story)" roms/tos162uk.img --disk "$NZS" --fastfdc --frames 700
EL="disks/st/Enchanted Land (1990)(Thalion).st"
have "$EL" && run "démo/jeu à raster (Enchanted Land)" roms/tos162uk.img --disk "$EL" --fastfdc --frames 700

# --- 4. Chemin Glue complet : retraits de bordure, écritures freq/res -------
have disks/etalons/nocooper.msa && \
    run "overscan med-res (No Cooper)" roms/tos102uk.img --machine st --mem 1m \
        --fastfdc --disk disks/etalons/nocooper.msa --keys-at 900 " " --frames 1200

# --- 5. Auto-tests : Glue, Spectrum 512, bus, MFP ---------------------------
# Déterministes, sans dépendance à une image de disquette, et ils exercent des
# chemins (re-rendu palette par pixel, carte de bus error) qu'aucun boot n'atteint.
have roms/tos162uk.img && {
    run "auto-test Glue"        roms/tos162uk.img --glue-selftest    --frames 60
    run "auto-test Spectrum512" roms/tos162uk.img --spec512-selftest --frames 60
    run "auto-test bus error"   roms/tos162uk.img --bus-selftest     --frames 60
    run "auto-test MFP"         roms/tos162uk.img --mfp-selftest     --frames 60
}

echo "[pgo_train] terminé."
