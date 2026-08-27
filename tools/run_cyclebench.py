#!/usr/bin/env python3
# =============================================================================
#  run_cyclebench.py — Gate d'AUTO-RÉGRESSION cycle (sans oracle Hatari).
#
#  Exécute la CARTOUCHE bench de cycles (make_cycle_bench.py --cart : mêmes corps de
#  boucle que le secteur de boot, mais pré-TOS → fiable en headless), trace au cycle
#  (NEOST_TRACE_CYC=1) et extrait, pour chaque boucle d'instruction, sa PÉRIODE stable
#  (cycles/itération = coût de l'instruction + dbra). Compare ces périodes à un golden
#  committé (tests/reference/cyclebench.json). Déterministe → tolérance 0 ; toute
#  dérive du modèle de cycle du cœur 68000 (Moira + wait states bus) fait échouer.
#
#  Usage :
#    python3 tools/run_cyclebench.py            # gate (exit 0/1)
#    python3 tools/run_cyclebench.py --update    # (re)génère le golden
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import collections
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = Path(__file__).resolve().parent
HEADLESS = ROOT / "build" / "neost-headless"
ROM = ROOT / "roms" / "tos102uk.img"
OUT = ROOT / "tests" / "out"
GOLDEN = ROOT / "tests" / "reference" / "cyclebench.json"
CART = OUT / "cyclebench_cart.bin"
TRACE = OUT / "cyclebench.trace"

# Garde-fou A18 (audit 2026-08-27) : AUCUN appel à l'émulateur n'avait de timeout —
# un 68000 qui boucle (la régression même que ces tests cherchent) consommait les
# 45 min du job CI sans diagnostic. Verdict 124 (convention de timeout(1)) pour les
# appels à code de retour ; sortie 2 immédiate pour les appels check=True (leur
# échec est de toute façon fatal au run).
def run_timed(cmd, limit_s, **kw):
    try:
        return subprocess.run(cmd, timeout=limit_s, **kw)
    except subprocess.TimeoutExpired:
        print(f"  TIMEOUT après {limit_s}s : " + " ".join(map(str, cmd)),
              file=sys.stderr, flush=True)
        if kw.get("check"):
            sys.exit(2)
        return subprocess.CompletedProcess(cmd, 124)

FRAMES = 3
MIN_ITERS = 200          # une boucle doit itérer au moins ça pour être fiable

sys.path.insert(0, str(TOOLS))
import trace_diff as td  # noqa: E402


def measure() -> dict:
    OUT.mkdir(parents=True, exist_ok=True)
    run_timed([sys.executable, str(TOOLS / "make_cycle_bench.py"), "--cart", str(CART)],
              120, cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    labels = json.loads((Path(str(CART) + ".labels.json")).read_text())
    run_timed([str(HEADLESS), str(ROM), "--cart", str(CART),
               "--frames", str(FRAMES), "--trace", str(TRACE)],
              300, cwd=ROOT, check=True, env={**__import__("os").environ, "NEOST_TRACE_CYC": "1"},
              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    ins, _ = td.parse(str(TRACE))
    periods = {}
    for name, pchex in labels.items():
        deltas = td.cycle_periods(ins, int(pchex, 16))
        if not deltas:
            periods[name] = None
            continue
        mode, count = collections.Counter(deltas).most_common(1)[0]
        periods[name] = mode if count >= MIN_ITERS else None
    return periods


def main() -> int:
    if not HEADLESS.exists():
        print(f"Build requis : cmake --build build  ({HEADLESS} absent)", file=sys.stderr)
        return 2
    periods = measure()

    if "--update" in sys.argv:
        # Refus d'un golden à trous : une boucle mesurée None (< MIN_ITERS itérations)
        # écrite dans le golden donnait ensuite « OK name None » pour toujours —
        # une ligne verte en permanence qui ne valide rien (None == None).
        holes = [n for n, p in periods.items() if p is None]
        if holes:
            print("mesure incomplète (< MIN_ITERS itérations) pour : "
                  + ", ".join(holes) + " — golden NON écrit", file=sys.stderr)
            return 2
        GOLDEN.write_text(json.dumps(periods, indent=1) + "\n")
        print(f"golden cycle écrit : {GOLDEN.relative_to(ROOT)}")
        for n, p in periods.items():
            print(f"  {n:12} {p} cyc/itér")
        return 0

    if not GOLDEN.exists():
        print(f"golden absent ({GOLDEN.relative_to(ROOT)}) — lancer --update", file=sys.stderr)
        return 2
    golden = json.loads(GOLDEN.read_text())

    ok = True
    for name, want in golden.items():
        got = periods.get(name)
        # Un golden null (ancien --update permissif) ou une mesure None ne peuvent
        # pas compter OK : None == None validerait zéro cycle.
        if want is None or got is None or got != want:
            ok = False
            print(f"  FAIL {name:12} got={got} want={want}")
        else:
            print(f"  OK   {name:12} {got} cyc/itér")
    # boucles nouvelles / disparues
    for name in periods:
        if name not in golden:
            print(f"  ⚠ boucle non golden : {name} = {periods[name]}")
    print("\n" + ("CYCLE-BENCH OK" if ok else "CYCLE-BENCH : DÉRIVE détectée"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
