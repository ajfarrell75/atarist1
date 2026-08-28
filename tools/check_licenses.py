#!/usr/bin/env python3
# =============================================================================
#  check_licenses.py — Le tableau des composants tiers est-il COMPLET ?
#
#  Huit jobs de CI vérifient déjà que les FICHIERS de licence accompagnent chaque
#  paquet (GPL-3.0.txt, GPL-2.0.txt, THIRD-PARTY.txt). Aucun ne regardait leur
#  CONTENU — et c'est là que l'omission arrive : le 2026-08-28, GLFW (lié en
#  statique dans TOUS les paquets de bureau) ne figurait ni au README ni au
#  THIRD-PARTY, et libmt32emu (LGPL 2.1+, statique) n'était arrivé au second que
#  par un appendice ajouté après coup. Une bibliothèque qu'on livre sans la nommer
#  n'est pas une négligence de forme : c'est la seule chose que sa licence demande.
#
#  Ce que ce contrôle vérifie :
#    1. tout composant tiers COMPILÉ (répertoire d'`extern/` cité par un fichier de
#       build) est nommé dans README.md ET dans packaging/licenses/THIRD-PARTY.txt ;
#    2. les composants LIÉS mais non vendorisés (GLFW) y sont aussi ;
#    3. au moins une des lignes qui le nomment porte une licence.
#
#  Ce qu'il ne peut PAS vérifier, et qu'il faut donc relire à la main quand on
#  ajoute une dépendance système à un paquet : les bibliothèques que `linuxdeploy`
#  embarque toute seule dans l'AppImage. Un fil-piège, pas une preuve.
#
#  Usage : check_licenses.py [--verbose]   (code de sortie 1 si un composant manque)
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
README = ROOT / "README.md"
THIRD_PARTY = ROOT / "packaging" / "licenses" / "THIRD-PARTY.txt"

# Fichiers de build fouillés pour trouver « extern/<nom> ». Le paquet Android a le
# sien : SDL2 n'apparaît nulle part dans le CMakeLists racine, et c'est pourtant un
# .so livré dans l'APK.
BUILD_FILES = [ROOT / "CMakeLists.txt"] + sorted(
    (ROOT / "packaging" / "android").rglob("*.sh")
) + sorted((ROOT / "packaging" / "android").rglob("CMakeLists.txt"))

# `extern/hatari` n'est NI compilé NI redistribué : il est lu comme source de vérité
# matérielle et exécuté comme oracle pendant le développement (cf. CLAUDE.md). Il est
# gitignoré, absent des paquets — le citer parmi les composants livrés serait faux.
NOT_SHIPPED = {"hatari"}

# Liés mais pas vendorisés : rien sous extern/ ne les trahit, il faut les nommer ici.
# GLFW est STATIQUE sur macOS (tag 3.4 compilé par packaging/macos/package_macos.sh)
# et sur Windows (MinGW-w64), et embarqué en .so dans l'AppImage Linux — il est donc
# dans tous les paquets de bureau.
LINKED_NOT_VENDORED = ["glfw"]

# Un composant est « cité » si son nom apparaît (insensible à la casse) : « imgui »
# attrape « Dear ImGui », « stb » attrape « stb_image », « mt32emu » attrape
# « libmt32emu ». Volontairement permissif — le but est d'attraper l'OMISSION.
LICENCE_WORDS = re.compile(
    r"MIT|GPL|LGPL|BSD|zlib|public domain|Unlicense|CC BY|SIL OFL|"
    r"Bitstream|free licence|libpng",
    re.I,
)


def compiled_components() -> list[str]:
    names = set()
    for f in BUILD_FILES:
        if not f.exists():
            continue
        for m in re.finditer(r"extern/([A-Za-z0-9_]+)", f.read_text(errors="replace")):
            names.add(m.group(1))
    return sorted(names - NOT_SHIPPED)


def cited_lines(text: str, name: str) -> list[str]:
    """Toutes les lignes qui nomment le composant.

    TOUTES, et pas la première : un README parle d'un composant plusieurs fois (« le
    cœur 68000 est Moira », « installez GLFW3 »…) et la ligne du TABLEAU, seule à
    porter la licence, n'est pas la première. Ne retenir que la première produisait
    deux faux positifs — un contrôle qui crie à tort finit désarmé.
    """
    return [l for l in text.splitlines() if name.lower() in l.lower()]


def main() -> int:
    verbose = "--verbose" in sys.argv
    for f in (README, THIRD_PARTY):
        if not f.exists():
            print(f"✗ absent : {f.relative_to(ROOT)}", file=sys.stderr)
            return 2
    readme = README.read_text(errors="replace")
    third = THIRD_PARTY.read_text(errors="replace")

    wanted = compiled_components() + LINKED_NOT_VENDORED
    errors = []
    for name in wanted:
        for label, text in (("README.md", readme),
                            ("packaging/licenses/THIRD-PARTY.txt", third)):
            lines = cited_lines(text, name)
            if not lines:
                errors.append(f"{label} — composant livré NON CITÉ : « {name} »")
            elif not any(LICENCE_WORDS.search(l) for l in lines):
                errors.append(f"{label} — « {name} » est cité ({len(lines)} fois) mais "
                              "AUCUNE de ces lignes ne porte de licence")
            elif verbose:
                print(f"  · {name:12} ✓ {label}")

    print(f"\nComposants tiers livrés : {len(wanted)} vérifiés "
          f"({', '.join(wanted)}), {len(errors)} manquant(s).")
    if errors:
        for e in errors:
            print(f"  ✗ {e}", file=sys.stderr)
        print("Une bibliothèque livrée doit être nommée avec sa licence dans les DEUX "
              "documents (README + le THIRD-PARTY.txt qui accompagne les paquets).",
              file=sys.stderr)
        return 1
    print("LICENCES OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
