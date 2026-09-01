#!/usr/bin/env python3
# =============================================================================
#  appimage_ls.py — Lister le CONTENU d'un AppImage sans `unsquashfs`.
#
#  POURQUOI. Le pas 2 du protocole de validation (docs/HW_VALIDATION.md) exige de
#  regarder ce qu'un paquet publié contient RÉELLEMENT — aucune ROM Atari, les trois
#  licences — et il doit se refaire à chaque release. Les quatre AppImage livrés sont
#  la moitié de la surface livrée, et `unsquashfs` n'existe pas sur un macOS nu :
#  sans cet outil, ces quatre cases restent vides ou se remplissent de confiance.
#  Constaté le 2026-09-01 (chantier A12) : le paquet web, lui, n'a passé le contrôle
#  que parce qu'un zip s'ouvre partout.
#
#  COMMENT. Un AppImage type 2 est un ELF suivi d'une image squashfs. On cherche le
#  superbloc (magie `hsqs`) — en écartant les fausses correspondances par la
#  cohérence de ses champs, `version 4.0` et `block_size == 1 << block_log` —, puis on
#  décompresse les blocs de métadonnées de la TABLE DES RÉPERTOIRES, où vivent les
#  noms. On n'extrait rien : ce sont les NOMS qui répondent à la question posée.
#
#  ⚠ Le résultat est une liste de noms, PAS une arborescence : les noms sont lus dans
#  des métadonnées où ils ne sont pas délimités proprement, donc quelques entrées
#  portent un caractère parasite en fin. C'est sans effet sur l'usage — on cherche la
#  PRÉSENCE de `tos*.img` ou l'ABSENCE de `GPL-3.0.txt` — mais il ne faut pas prendre
#  cette sortie pour un inventaire exact.
#
#  Usage : appimage_ls.py <fichier.AppImage> [...]
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
import re
import struct
import sys
import zlib

GZIP = 1   # seule compression rencontrée sur les AppImage produits par le projet


def superblock(d: bytes):
    """Offset du superbloc squashfs 4.0/gzip, ou None."""
    pos = -1
    while True:
        pos = d.find(b'hsqs', pos + 1)
        if pos < 0:
            return None
        if pos + 96 > len(d):
            return None
        bsize, = struct.unpack_from('<I', d, pos + 12)
        comp, blog, _flags, _ids, vmaj, vmin = struct.unpack_from('<HHHHHH', d, pos + 20)
        # Trois conditions ensemble : la magie seule sort des faux positifs dans le
        # code de l'ELF qui précède (vérifié — le premier `hsqs` en est un).
        if vmaj == 4 and vmin == 0 and comp == GZIP and bsize == (1 << blog):
            return pos


def metadata(d: bytes, start: int, end: int) -> bytes:
    """Concatène les blocs de métadonnées squashfs de [start, end).

    Format d'un bloc : un en-tête u16 dont le bit 15 dit « non compressé » et les
    15 bits bas donnent la taille, puis les données.
    """
    out, p = bytearray(), start
    while p < end - 2:
        hdr, = struct.unpack_from('<H', d, p)
        size, compressed = hdr & 0x7FFF, not (hdr & 0x8000)
        if size == 0 or p + 2 + size > end:
            break
        blk = d[p + 2:p + 2 + size]
        try:
            out += zlib.decompress(blk) if compressed else blk
        except zlib.error:
            pass   # un bloc illisible ne doit pas faire perdre les autres
        p += 2 + size
    return bytes(out)


def names_of(path: str):
    d = open(path, 'rb').read()
    sb = superblock(d)
    if sb is None:
        raise SystemExit(f"{path} : aucun superbloc squashfs 4.0/gzip")
    dir_start, = struct.unpack_from('<Q', d, sb + 72)
    frag_start, = struct.unpack_from('<Q', d, sb + 80)
    raw = metadata(d, sb + dir_start, sb + frag_start)
    return sorted({m.decode('utf-8', 'replace')
                   for m in re.findall(rb'[A-Za-z0-9._+-]{3,}', raw)})


def main(argv) -> int:
    if len(argv) < 2:
        print(__doc__ or "Usage : appimage_ls.py <fichier.AppImage> [...]")
        return 1
    for path in argv[1:]:
        if len(argv) > 2:
            print(f"########## {path} ##########")
        for n in names_of(path):
            print(n)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
