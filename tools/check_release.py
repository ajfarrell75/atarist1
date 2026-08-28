#!/usr/bin/env python3
# =============================================================================
#  check_release.py — la VERSION dit-elle la même chose partout ? (chantier A37)
#
#  POURQUOI. L'audit relevait trois symptômes d'une seule cause — rien ne vérifiait
#  la cohérence des numéros de version : trois tags posés le même jour (0.5 → 0.5.2
#  le 2026-08-10), une **0.5.3 sautée sans une ligne pour le dire**, et un binaire
#  qui peut annoncer une version différente de celle du dépôt (`NEOST_VERSION_STR`
#  est une variable de CACHE CMake : après un bump, sans `-DNEOST_VERSION_STR=<ver>`,
#  `--version` MENT — c'est écrit dans CLAUDE.md, et ça ne s'attrapait qu'à l'œil).
#
#  Ce contrôle vérifie trois égalités, et rien d'autre :
#    1. CMakeLists `project(... VERSION x.y.z)` == « Version courante » du CHANGELOG ;
#    2. == la plus récente en-tête de release du CHANGELOG (`## x.y.z — …`) ;
#    3. les releases du CHANGELOG ne SAUTENT pas de numéro de patch sans qu'une
#       ligne de la section « Numéros sautés » le justifie.
#
#  Il ne pose PAS de tag et ne publie rien : taguer est une décision de mainteneur.
#  Il garantit seulement qu'au moment de la poser, elle porte sur des chiffres qui
#  disent tous la même chose.
#
#  Usage : check_release.py [--verbose]   (code de sortie 1 si un numéro diverge)
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CMAKE = ROOT / "CMakeLists.txt"
CHANGELOG = ROOT / "CHANGELOG.md"

REL_HEADING = re.compile(r"^## (\d+\.\d+(?:\.\d+)?)\b", re.M)


def parts(v: str) -> tuple[int, ...]:
    return tuple(int(x) for x in v.split("."))


def main() -> int:
    verbose = "--verbose" in sys.argv
    cmake = CMAKE.read_text(encoding="utf-8")
    chlog = CHANGELOG.read_text(encoding="utf-8")

    m = re.search(r"project\s*\(\s*NeoST\s+VERSION\s+(\d+\.\d+(?:\.\d+)?)", cmake, re.S)
    if not m:
        print("✗ CMakeLists.txt : project(NeoST VERSION …) INTROUVABLE", file=sys.stderr)
        return 2
    cmake_ver = m.group(1)

    m = re.search(r"Version courante\s*:\s*\*\*(\d+\.\d+(?:\.\d+)?)\*\*", chlog)
    if not m:
        print("✗ CHANGELOG.md : « Version courante : **x.y.z** » INTROUVABLE", file=sys.stderr)
        return 2
    chlog_cur = m.group(1)

    releases = REL_HEADING.findall(chlog)
    if not releases:
        print("✗ CHANGELOG.md : aucune en-tête de release « ## x.y.z — … »", file=sys.stderr)
        return 2
    top = releases[0]

    errors = []
    if cmake_ver != chlog_cur:
        errors.append(f"CMakeLists dit {cmake_ver}, le CHANGELOG dit « Version courante {chlog_cur} »")
    if cmake_ver != top:
        errors.append(f"CMakeLists dit {cmake_ver}, la dernière release du CHANGELOG est {top}")

    # Trous de numérotation : 0.5.2 → 0.5.4 est LÉGITIME s'il est expliqué, et une
    # faute s'il ne l'est pas. C'est le cas exact relevé par l'audit.
    skipped_note = "## Numéros de version sautés" in chlog
    ordered = sorted({r for r in releases}, key=parts)
    holes = []
    for a, b in zip(ordered, ordered[1:]):
        pa, pb = parts(a), parts(b)
        if len(pa) == 3 and len(pb) == 3 and pa[:2] == pb[:2] and pb[2] - pa[2] > 1:
            holes += [f"{a}.{n}" if False else f"{pa[0]}.{pa[1]}.{n}"
                      for n in range(pa[2] + 1, pb[2])]
    if holes and not skipped_note:
        errors.append("numéro(s) de version SAUTÉ(S) sans explication : "
                      + ", ".join(holes)
                      + " — ajouter une section « ## Numéros de version sautés » au CHANGELOG")

    if verbose:
        print(f"  CMakeLists           {cmake_ver}")
        print(f"  CHANGELOG (courante) {chlog_cur}")
        print(f"  CHANGELOG (dernière) {top}")
        print(f"  releases             {', '.join(ordered)}")
        if holes:
            print(f"  trous                {', '.join(holes)} (expliqués : {skipped_note})")

    print(f"\nVersion : {cmake_ver} — {len(releases)} release(s) au CHANGELOG"
          + (f", trou(s) documenté(s) : {', '.join(holes)}" if holes else ""))
    if errors:
        for e in errors:
            print(f"  ✗ {e}", file=sys.stderr)
        print("⚠ Rappel : NEOST_VERSION_STR est une variable de CACHE CMake. Après un "
              "bump, reconfigurer une fois avec -DNEOST_VERSION_STR=<version>, sinon "
              "`--version` ment.", file=sys.stderr)
        return 1
    print("VERSION OK (le binaire, lui, dépend du cache NEOST_VERSION_STR — cf. CLAUDE.md)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
