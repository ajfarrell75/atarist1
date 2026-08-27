#!/usr/bin/env python3
# =============================================================================
#  run_all.py — Orchestrateur des suites de test NeoST par PALIERS.
#
#    --tier fast : P0 (auto-tests logique : glue + spec512) + P1 (verdicts série).
#                  Secondes, sans oracle ni disque externe → garde de commit / hook.
#    --tier full : fast + P2 (tous les étalons pixel + contrôle de provenance des réfs).
#                  Nécessite les disques/oracles présents (les optionnels absents = SKIP).
#
#    --install-hook / --uninstall-hook : (dé)installe un hook git pre-push (opt-in)
#                  qui lance « run_all.py --tier fast » avant chaque push.
#
#  Chaque étape est une sous-suite existante ; run_all agrège les codes de sortie.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
HEADLESS = ROOT / "build" / "neost-headless"
SELFTEST = ROOT / "build" / "neost-selftest"
GUI = ROOT / "build" / "neost"


def selftest_ids() -> str:
    # A19 (audit 2026-08-27) : cette liste était CODÉE EN DUR — et elle avait produit
    # un ORPHELIN : serloop_selftest, présent au manifeste, n'était exécuté par AUCUN
    # palier, ni localement ni en CI, sans qu'aucun garde-fou puisse le voir (le refus
    # d'ID inconnu de run_etalons attrape les IDs SUPPRIMÉS, pas les oubliés).
    # Sélection PAR TYPE depuis etalons.json : un selftest ajouté au manifeste est
    # exécuté d'office.
    man = json.loads((TOOLS / "etalons.json").read_text())
    entries = man["etalons"] if isinstance(man, dict) and "etalons" in man else man
    ids = [e["id"] for e in entries if str(e.get("type", "")).endswith("_selftest")]
    if not ids:
        print("etalons.json : AUCUN *_selftest — manifeste cassé ?", file=sys.stderr)
        sys.exit(2)
    return ",".join(ids)


# Étapes par palier : (label, [argv]). Exécutées dans l'ordre ; tout code ≠ 0 = échec.
FAST = [
    # Logique PURE, sans machine ni ROM (chemins hôte, parseurs, formats) : le seul
    # palier qui exerce la sémantique de chemins WINDOWS depuis un Mac ou une CI
    # Linux. Sans lui, l'issue #37 restait invisible partout où on développe.
    ("P0 auto-test logique pure (hostpath — sémantiques POSIX ET Windows)",
     [str(SELFTEST)]),
    ("P0 auto-tests logique (tous les *_selftest du manifeste etalons.json)",
     [sys.executable, str(TOOLS / "run_etalons.py"), "--only", selftest_ids()]),
    # A20 : WRITE TRACK STX (reinterpretSaveTrack + round-trip .wd1772) sur une image
    # FORGÉE en mémoire — le seul test du parseur Pasti, resté EXCLUDE_FROM_ALL et
    # jamais lancé jusqu'à l'audit du 2026-08-27.
    ("P0 STX WRITE TRACK (image forgée, ré-interprétation + round-trip .wd1772)",
     [str(ROOT / "build" / "neost-stx-test")]),
    # Intégrité des ANCRES de la doc (chantier A7). Instantané, aucune machine : la
    # convention du projet veut que le SYMBOLE fasse foi (les fichier:ligne dérivent),
    # or rien ne vérifiait qu'ils existent encore. Après le renommage
    # Blitter::stallCpu → billCycles, l'inventaire pointait dans le vide.
    ("Ancres de documentation (les symboles cités existent-ils encore ?)",
     [sys.executable, str(TOOLS / "check_doc_anchors.py")]),
    # Auto-tests du HARNAIS lui-même (chantier A4). L'instrument produit TOUTE la
    # preuve du projet, et il avait des pannes silencieuses : trois « bloquants » sur
    # huit venaient de lui lors du balayage du 2026-08-25 (options de pilotage non
    # répétables). Un instrument non testé fabrique des bugs qui n'existent pas.
    ("Harnais headless (options de pilotage : répétabilité, durée d'appui, scancodes)",
     [sys.executable, str(TOOLS / "check_headless_options.py")]),
    ("P1 verdicts série (cartouche diagnostic)",
     [sys.executable, str(TOOLS / "run_selftests.py")]),
    ("Cycle-bench (auto-régression du modèle de cycle 68000)",
     [sys.executable, str(TOOLS / "run_cyclebench.py")]),
    # Round-trip save-state en CONFIG PAR DÉFAUT : la régression v10 (mem_ NE2000
    # vide → tout .state rejeté au chargement, F7 mort) a vécu des semaines sans
    # qu'aucun palier ne la voie — ce verdict la rend impossible à reproduire.
    ("Save-state round-trip (config par défaut)",
     [str(HEADLESS), "roms/etos192us.img", "--frames", "30", "--save-state-test"]),
    # Disquette livrée dans TOUS les paquets : elle avait été écrasée par un test
    # d'écriture secteur et n'était plus lisible sous TOS (issue #38), sans qu'aucun
    # palier ne relise jamais cette image.
    ("Disquette livrée (disks/diskA.st : FAT12 valide, conforme au générateur)",
     [sys.executable, str(TOOLS / "check_disk_assets.py")]),
    # Séquenceur MIDI de bout en bout : Cubase Lite (TOS 1.04, MROS) importe un SMF et
    # le joue ; ce qui sort de l'ACIA est comparé note à note au fichier (tempo, gigue,
    # vélocités, pédale). Couvre ACIA 6850 + Timer A + GEMDOS HD + midi_simplify.py.
    ("Séquenceur MIDI (Cubase Lite joue un SMF → notes/tempo comparés)",
     [sys.executable, str(TOOLS / "run_midi_sequencer.py")]),
]

# Boot GUI (A9a, audit 2026-08-27) : build/neost était à 0 % de couverture LOCALE —
# seul le job CI xvfb le lançait. 400 trames EmuTOS + capture non uniforme, en
# harnais (--run-frames, qui depuis A9a ne réécrit PAS neost.cfg : un test ne doit
# laisser aucune trace dans l'état utilisateur). Sauté ET DIT quand il n'y a pas
# d'affichage (CI sans xvfb) ou pas de cible GUI — jamais un faux vert silencieux.
_GUI_SHOT = ROOT / "tests" / "out" / "gui_boot.ppm"
GUI_STEPS = [
    ("Boot GUI (400 trames EmuTOS, capture du framebuffer)",
     [str(GUI), "roms/etos192us.img", "--run-frames", "400", "--shot", str(_GUI_SHOT)]),
    ("Boot GUI — la capture montre quelque chose",
     [sys.executable, str(TOOLS / "check_ppm_nonuniform.py"), str(_GUI_SHOT)]),
]


def gui_available() -> str | None:
    """None si le boot GUI peut tourner, sinon la raison du SKIP (recensée)."""
    if not GUI.exists():
        return "cible GUI non bâtie (cmake --build build)"
    if sys.platform != "darwin" and not os.environ.get("DISPLAY") \
            and not os.environ.get("WAYLAND_DISPLAY"):
        return "pas d'affichage (DISPLAY/WAYLAND_DISPLAY absents — xvfb-run pour forcer)"
    return None
FULL = FAST + [
    # Barrière de DÉBIT (chantier A6). Dans le palier FULL et non FAST : il mesure du
    # temps mur, donc il coûte quelques secondes et il est le seul verdict du dépôt
    # sensible à la charge de la machine. Il garde des RATIOS entre charges mesurées
    # dans le MÊME run (le boot nu sert d'étalon de vitesse), ce qui le rend
    # indépendant de la vitesse du runner — un seuil absolu en trames/s serait
    # instable sur une CI partagée, donc désarmé au bout de trois faux rouges.
    ("Banc de débit (coût relatif des chemins blitter et MFP)",
     [sys.executable, str(TOOLS / "run_perfbench.py")]),
    ("P2 provenance des références",
     [sys.executable, str(TOOLS / "run_etalons.py"), "--verify-refs"]),
    ("P2 étalons pixel (oracle + snapshots)",
     [sys.executable, str(TOOLS / "run_etalons.py")]),
]

HOOK_BODY = ("#!/bin/sh\n"
             "# Hook NeoST (opt-in via tools/run_all.py --install-hook) : garde de push.\n"
             "exec python3 tools/run_all.py --tier fast\n")


def git_hooks_dir() -> Path | None:
    try:
        out = subprocess.run(["git", "rev-parse", "--git-path", "hooks"],
                             cwd=ROOT, capture_output=True, text=True, check=True)
        d = (ROOT / out.stdout.strip()).resolve()
        d.mkdir(parents=True, exist_ok=True)
        return d
    except Exception as e:
        print(f"git introuvable / hors dépôt : {e}", file=sys.stderr)
        return None


def install_hook(remove: bool) -> int:
    d = git_hooks_dir()
    if not d:
        return 2
    hook = d / "pre-push"
    if remove:
        if hook.exists() and "run_all.py --tier fast" in hook.read_text(errors="ignore"):
            hook.unlink()
            print(f"hook pre-push retiré : {hook}")
        else:
            print("aucun hook NeoST à retirer.")
        return 0
    if hook.exists() and "run_all.py --tier fast" not in hook.read_text(errors="ignore"):
        print(f"⚠ un pre-push existe déjà (non-NeoST) : {hook} — non écrasé.", file=sys.stderr)
        return 1
    hook.write_text(HOOK_BODY)
    hook.chmod(0o755)
    print(f"hook pre-push installé : {hook}\n  → lance « run_all.py --tier fast » avant chaque push.")
    return 0


def run_tier(steps) -> int:
    ok = True
    for label, cmd in steps:
        print(f"\n########## {label} ##########")
        rc = subprocess.run(cmd, cwd=ROOT).returncode
        if rc != 0:
            ok = False
            print(f"  → ÉCHEC ({label})")
    print("\n" + ("=" * 60) + "\n" + ("TOUS LES PALIERS OK" if ok else "ÉCHEC — voir ci-dessus"))
    return 0 if ok else 1


def main() -> int:
    ap = argparse.ArgumentParser(description="Orchestrateur des tests NeoST par paliers")
    ap.add_argument("--tier", choices=("fast", "full"), default="fast")
    ap.add_argument("--install-hook", action="store_true", help="installe le hook git pre-push")
    ap.add_argument("--uninstall-hook", action="store_true")
    args = ap.parse_args()

    if args.install_hook or args.uninstall_hook:
        return install_hook(args.uninstall_hook)

    for binary in (HEADLESS, SELFTEST, ROOT / "build" / "neost-stx-test"):
        if not binary.exists():
            print(f"Build requis : cmake --build build  ({binary} absent)", file=sys.stderr)
            return 2

    steps = list(FAST if args.tier == "fast" else FULL)
    skip = gui_available()
    if skip is None:
        steps += GUI_STEPS
    else:
        print(f"⚠ Boot GUI SAUTÉ (recensé) : {skip}")
    return run_tier(steps)


if __name__ == "__main__":
    sys.exit(main())
