#!/usr/bin/env python3
# =============================================================================
#  check_ppm_nonuniform.py — La capture montre-t-elle QUELQUE CHOSE ?
#
#  Contrôle minimal partagé par les smoke-tests de paquet : un écran d'une seule
#  couleur = boot raté (machine figée, écran noir, palette morte). Volontairement
#  grossier — la comparaison PIXEL, elle, est le métier des étalons
#  (tools/run_etalons.py) et exige un oracle.
#
#  Usage : python3 tools/check_ppm_nonuniform.py <capture.ppm>
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage : check_ppm_nonuniform.py <capture.ppm>", file=sys.stderr)
        return 2
    path = Path(sys.argv[1])
    if not path.is_file():
        print(f"ERREUR : {path} absent", file=sys.stderr)
        return 1
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        print(f"ERREUR : {path} n'est pas un PPM binaire (P6)", file=sys.stderr)
        return 1
    body = data.split(b"\n", 3)[3]              # P6 : 3 lignes d'en-tête
    distinct = len(set(body))
    if distinct <= 1:
        print(f"ERREUR : {path} est uniforme — le boot a échoué", file=sys.stderr)
        return 1
    print(f"OK : {path} — {len(body)} octets, {distinct} valeurs distinctes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
