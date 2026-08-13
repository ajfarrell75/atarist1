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
import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
HEADLESS = ROOT / "build" / "neost-headless"

# Étapes par palier : (label, [argv]). Exécutées dans l'ordre ; tout code ≠ 0 = échec.
FAST = [
    ("P0 auto-tests logique (glue + spec512 + bus + mfp + msa + fuji + enec)",
     [sys.executable, str(TOOLS / "run_etalons.py"), "--only",
      "glue_selftest,spec512_selftest,bus_selftest,mfp_selftest,msa_selftest,fuji_selftest,enec_selftest"]),
    ("P1 verdicts série (cartouche diagnostic)",
     [sys.executable, str(TOOLS / "run_selftests.py")]),
    ("Cycle-bench (auto-régression du modèle de cycle 68000)",
     [sys.executable, str(TOOLS / "run_cyclebench.py")]),
    # Round-trip save-state en CONFIG PAR DÉFAUT : la régression v10 (mem_ NE2000
    # vide → tout .state rejeté au chargement, F7 mort) a vécu des semaines sans
    # qu'aucun palier ne la voie — ce verdict la rend impossible à reproduire.
    ("Save-state round-trip (config par défaut)",
     [str(HEADLESS), "roms/etos192us.img", "--frames", "30", "--save-state-test"]),
]
FULL = FAST + [
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

    if not HEADLESS.exists():
        print(f"Build requis : cmake --build build  ({HEADLESS} absent)", file=sys.stderr)
        return 2

    return run_tier(FAST if args.tier == "fast" else FULL)


if __name__ == "__main__":
    sys.exit(main())
