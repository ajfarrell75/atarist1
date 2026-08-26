#!/usr/bin/env python3
# =============================================================================
#  check_doc_anchors.py — Contrôle d'intégrité des ANCRES de la documentation.
#
#  POURQUOI (chantier A7). `docs/HATARI_DIVERGENCES.md` est une base de données
#  maintenue à la main, et la convention du projet est explicite : les
#  `fichier:ligne` DÉRIVENT à chaque édition, c'est le SYMBOLE cité qui fait foi.
#  Sauf que rien ne vérifiait que ces symboles existent encore. Constaté le
#  2026-08-25 : après le renommage `Blitter::stallCpu` → `Blitter::billCycles`,
#  l'inventaire pointait vers un symbole disparu — 22 renvois à corriger à la
#  main, et un oublié malgré tout. Le coût réel de cette dérive n'est pas
#  cosmétique, c'est la REDÉCOUVERTE : `D4` a été retrouvé comme faux positif par
#  trois agents indépendants, et un cas tranché (Arkanoid) est resté ouvert au
#  catalogue faute d'avoir été versé.
#
#  CE QUI EST VÉRIFIÉ : tout `Classe::méthode` et tout `Fichier.cpp:symbole` cité
#  entre backticks dans la doc doit exister dans `src/` (ou dans `extern/moira`,
#  qui est vendorisé). Le contrôle est volontairement SYNTAXIQUE et grossier — il
#  ne compile rien, il cherche le lexème — parce qu'un contrôle exact coûterait un
#  index clang pour un bénéfice nul : ce qu'on veut attraper, ce sont les
#  renommages, pas les subtilités de surcharge.
#
#  DEUX NIVEAUX, et c'est délibéré :
#    · DOCUMENTS VIVANTS (TODO.md, DEV.md, docs/*.md) → ERREUR. Ils décrivent
#      l'état ACTUEL du code ; une ancre morte y est un mensonge.
#    · CHANGELOG.md → AVERTISSEMENT seulement. C'est un registre DATÉ : une entrée
#      de juillet a le droit de citer un symbole renommé depuis, et la réécrire
#      falsifierait l'histoire.
#
#  Les symboles cités À DESSEIN comme inexistants (une phrase qui parle justement
#  d'une ancre morte) vont dans ALLOWLIST, avec leur raison.
#
#  Usage : check_doc_anchors.py [--verbose]   (code de sortie 1 si ancre morte)
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Documents décrivant l'état ACTUEL du code : une ancre morte y est une erreur.
LIVING = ["TODO.md", "DEV.md", "README.md"] + sorted(
    str(p.relative_to(ROOT)) for p in (ROOT / "docs").glob("*.md")
)
# Registre daté : avertissement seulement (cf. en-tête).
HISTORICAL = ["CHANGELOG.md"]

# Symboles cités VOLONTAIREMENT alors qu'ils n'existent pas — la phrase parle
# justement de leur disparition. Toute entrée doit porter sa raison.
ALLOWLIST = {
    "Blitter::stallCpu":
        "TODO.md § A7 : cité comme EXEMPLE d'ancre morte après le renommage en billCycles.",
    "DmaSound::onFrameEnd":
        "CHANGELOG.md : cité dans une phrase qui dit explicitement « qui n'existent pas ».",
}

ANCHOR_RE = re.compile(
    r"`(?:([A-Z][A-Za-z0-9_]*)::([A-Za-z_][A-Za-z0-9_]*)"          # Classe::méthode
    r"|([A-Za-z_][A-Za-z0-9_]*\.(?:cpp|hpp)):([A-Za-z_][A-Za-z0-9_]*))`"  # Fichier:symbole
)

# Espaces de noms externes : cités en référence, pas définis par NeoST.
SKIP_SCOPES = {"std", "moira", "ImGui", "SDL", "MFP", "FDC", "PSG"}


def lexemes(*globs) -> set:
    """Tous les identifiants présents dans les sources (grep, pas de compilation)."""
    args = ["grep", "-rhoE", "[A-Za-z_][A-Za-z0-9_]*"]
    args += [f"--include={g}" for g in globs[0]]
    args += list(globs[1])
    out = subprocess.run(args, cwd=ROOT, capture_output=True, text=True).stdout
    return set(out.split())


def main() -> int:
    verbose = "--verbose" in sys.argv

    known = lexemes(("*.cpp", "*.hpp", "*.h"), ("src",))
    known |= lexemes(("*.cpp", "*.hpp", "*.h"), ("extern/moira",))
    known |= lexemes(("*.py",), ("tools",))          # les docs citent aussi des outils

    errors, warnings, checked = [], [], 0
    for rel in LIVING + HISTORICAL:
        path = ROOT / rel
        if not path.exists():
            continue
        historical = rel in HISTORICAL
        for n, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
            for m in ANCHOR_RE.finditer(line):
                scope, meth, fname, sym = m.groups()
                name = f"{scope}::{meth}" if scope else f"{fname}:{sym}"
                target = meth if scope else sym
                if scope in SKIP_SCOPES:
                    continue
                checked += 1
                if target in known or name in ALLOWLIST:
                    continue
                (warnings if historical else errors).append((rel, n, name))

    for rel, n, name in warnings:
        print(f"  ⚠ {rel}:{n} — `{name}` introuvable dans src/ "
              f"(registre daté : avertissement, pas une erreur)")
    for rel, n, name in errors:
        print(f"  ✗ {rel}:{n} — `{name}` INTROUVABLE dans src/ : ancre morte "
              f"(renommage ? suppression ?)")

    print(f"\nAncres de documentation : {checked} vérifiées, "
          f"{len(errors)} morte(s), {len(warnings)} avertissement(s), "
          f"{len(ALLOWLIST)} exemptée(s).")
    if verbose and ALLOWLIST:
        for k, why in ALLOWLIST.items():
            print(f"  · exemptée `{k}` — {why}")
    if errors:
        print("\nUne ancre morte n'est pas cosmétique : la convention du projet est que "
              "le SYMBOLE fait foi (les fichier:ligne dérivent). Corriger le renvoi, ou "
              "ajouter le symbole à ALLOWLIST avec sa raison s'il est cité à dessein.")
        return 1
    print("ANCRES OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
