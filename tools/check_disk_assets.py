#!/usr/bin/env python3
# =============================================================================
#  check_disk_assets.py — Gate : la disquette LIVRÉE est-elle encore une FAT12 ?
#
#  disks/diskA.st est la SEULE image embarquée dans les paquets (AppImage, .app,
#  .zip Windows, APK, WASM). Elle a été ÉCRASÉE par un test d'écriture secteur
#  (commit 828bc87, juin 2026) : les archives 0.5.x ont livré pendant des mois un
#  motif binaire sans système de fichiers, inutilisable sous TOS (issue #38). Rien
#  ne le voyait — aucun palier ne relit cette image.
#
#  Deux vérifications, toutes deux sans émulateur ni oracle (→ palier « fast ») :
#    1. BPB sain (720 Ko, 2 FAT identiques, racine lisible, chaînes cohérentes) ;
#    2. l'image est BIT POUR BIT celle que produit tools/make_floppy.py — le
#       générateur est donc la source de vérité, et toute écriture accidentelle
#       (ou toute dérive du générateur) fait échouer le gate.
#
#  Usage :
#    python3 tools/check_disk_assets.py            # gate (exit 0/1)
#    python3 tools/check_disk_assets.py --update   # regénère disks/diskA.st
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
from __future__ import annotations

import struct
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GEN = ROOT / "tools" / "make_floppy.py"
DISK = ROOT / "disks" / "diskA.st"


def check_fat12(data: bytes) -> list[str]:
    """Contrôles structurels d'une disquette FAT12 720 Ko (offsets DOS = Atari)."""
    errs: list[str] = []
    if len(data) != 737280:
        return [f"taille {len(data)} octets (attendu 737280 = 80x2x9x512)"]
    bps   = struct.unpack_from("<H", data, 0x0B)[0]
    spc   = data[0x0D]
    res   = struct.unpack_from("<H", data, 0x0E)[0]
    nfat  = data[0x10]
    ndir  = struct.unpack_from("<H", data, 0x11)[0]
    total = struct.unpack_from("<H", data, 0x13)[0]
    spf   = struct.unpack_from("<H", data, 0x16)[0]
    spt   = struct.unpack_from("<H", data, 0x18)[0]
    sides = struct.unpack_from("<H", data, 0x1A)[0]
    if bps != 512:                errs.append(f"octets/secteur = {bps} (attendu 512)")
    if spc not in (1, 2):         errs.append(f"secteurs/cluster = {spc}")
    if nfat != 2:                 errs.append(f"nb de FAT = {nfat} (attendu 2)")
    if total * 512 != len(data):  errs.append(f"total secteurs = {total} (attendu 1440)")
    if (spt, sides) != (9, 2):    errs.append(f"géométrie {spt}x{sides} (attendu 9 secteurs x 2 faces)")
    if not 1 <= res:              errs.append("aucun secteur réservé (pas de boot sector)")
    if errs:
        return errs
    # Une disquette NON amorçable doit le rester : le TOS n'exécute le secteur de
    # boot que si la somme de ses 256 mots (big-endian) vaut $1234.
    if sum(struct.unpack_from(">256H", data, 0)) & 0xFFFF == 0x1234:
        errs.append("secteur de boot marqué EXÉCUTABLE (somme = $1234) sans code de boot")
    fat1 = data[res * 512: (res + spf) * 512]
    fat2 = data[(res + spf) * 512: (res + 2 * spf) * 512]
    if fat1 != fat2:
        errs.append("FAT1 et FAT2 divergent")
    if fat1[0] != 0xF9 or fat1[1] != 0xFF or fat1[2] != 0xFF:
        errs.append(f"début de FAT {fat1[:3].hex()} (attendu f9ffff : média 720 Ko)")

    def fat_entry(idx: int) -> int:
        off = idx * 3 // 2
        val = fat1[off] | (fat1[off + 1] << 8)
        return (val >> 4) if (idx & 1) else (val & 0xFFF)

    root = (res + nfat * spf) * 512
    rootsects = (ndir * 32 + 511) // 512
    nclusters = (total - (root // 512 + rootsects)) // spc
    entries = 0
    for slot in range(ndir):
        e = data[root + slot * 32: root + slot * 32 + 32]
        if e[0] == 0x00:
            break
        if e[0] == 0xE5:
            continue
        entries += 1
        name = e[0:11].decode("latin1")
        if any(c < 0x20 for c in e[0:11]):
            errs.append(f"entrée racine {slot} : nom non imprimable ({e[0:11].hex()})")
            continue
        clus, size = struct.unpack_from("<H", e, 26)[0], struct.unpack_from("<I", e, 28)[0]
        chain, cur = [], clus
        while 2 <= cur < 0xFF8 and len(chain) < nclusters + 1:
            chain.append(cur)
            cur = fat_entry(cur)
        if cur < 0xFF8:
            errs.append(f"« {name} » : chaîne FAT non terminée (boucle ?)")
        if any(c >= nclusters + 2 for c in chain):
            errs.append(f"« {name} » : cluster hors de la zone de données")
        want = 1 if e[11] & 0x10 else max(1, (size + spc * 512 - 1) // (spc * 512))
        if len(chain) != want:
            errs.append(f"« {name} » : {len(chain)} clusters pour {size} octets (attendu {want})")
    if entries == 0:
        errs.append("racine vide (aucune entrée)")
    return errs


def main() -> int:
    update = "--update" in sys.argv[1:]
    if update:
        rc = subprocess.run([sys.executable, str(GEN), str(DISK)], cwd=ROOT).returncode
        if rc != 0:
            return rc
    if not DISK.exists():
        print(f"ÉCHEC : {DISK} absent (le regénérer : python3 tools/make_floppy.py)")
        return 1
    data = DISK.read_bytes()

    errs = check_fat12(data)
    for e in errs:
        print(f"  ✗ FAT12 : {e}")

    with tempfile.TemporaryDirectory() as tmp:
        ref = Path(tmp) / "diskA.st"
        rc = subprocess.run([sys.executable, str(GEN), str(ref)], cwd=ROOT,
                            stdout=subprocess.DEVNULL).returncode
        if rc != 0:
            print("  ✗ make_floppy.py a échoué")
            return 1
        if ref.read_bytes() != data:
            errs.append("écart avec make_floppy.py")
            print("  ✗ disks/diskA.st ≠ sortie de tools/make_floppy.py "
                  "(image écrasée ? générateur modifié ?)\n"
                  "     → la régénérer : python3 tools/check_disk_assets.py --update")

    if errs:
        print(f"ÉCHEC : disks/diskA.st ({len(errs)} problème(s))")
        return 1
    print("OK : disks/diskA.st est une FAT12 720 Ko valide, conforme à make_floppy.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
