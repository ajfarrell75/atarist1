#!/usr/bin/env python3
# =============================================================================
#  make_hd_image.py — Carte SD UltraSatan (ou image ACSI) à partir d'un DOSSIER hôte.
#
#  Produit un disque brut avec table de partitions ATARI (secteur racine, entrées à
#  $1C6) et UNE partition GEM/BGM (FAT16) montée en C: au boot. Le contenu du dossier
#  source est copié à la racine de C:.
#
#  ── AMORÇAGE : POURQUOI UN PILOTE EST OBLIGATOIRE ──────────────────────────────
#  EmuTOS gère l'ACSI nativement et monte un tel disque sans rien de plus. Le TOS
#  Atari, LUI, n'a AUCUN pilote de disque dur en ROM : il exécute le secteur racine
#  si sa somme de contrôle vaut $1234, et c'est ce code qui charge le vrai pilote
#  (HDDRIVER.SYS…) depuis la première partition. Sans ça, sous TOS 1.x/2.x, le
#  bureau n'affiche que A: et B: — exactement comme un UltraSatan sans pilote sur
#  du matériel réel.
#
#  ⚠ LA GREFFE N'EST PAS VÉRIFIÉE DE BOUT EN BOUT, et pas par sa faute : dans l'état
#  actuel de NeoST, AUCUN disque dur ne se monte sous TOS 2.06 — pas même un disque
#  d'origine équipé de HDDRIVER et non touché par cet outil. Mesuré au drapeau
#  _drvbits ($4C2), invariant en 512k/1m/4m et en ST comme en MegaSTE :
#      EmuTOS + carte de données ......... A B C
#      EmuTOS + disque HDDRIVER 3 Go ..... A B C D E F G H I
#      TOS 2.06 + le MÊME disque 3 Go .... A B          ← rien
#  La chaîne d'amorçage s'exécute pourtant (NEOST_ACSI_TRACE=1 montre la lecture du
#  secteur 0, puis du secteur 2, puis deux transferts de 32 secteurs = le pilote qui
#  se charge) : c'est l'INSTALLATION du pilote qui n'aboutit pas. Piège de méthode à
#  ne pas refaire : les icônes C:/D:/E: du bureau 2.06 viennent des lignes #M de
#  NEWDESK.INF et s'affichent SANS lecteur monté — juger sur _drvbits, pas sur elles.
#  Tant que ce point n'est pas tranché contre l'oracle Hatari, ne construire des
#  disques que pour EmuTOS.
#
#  Ce script NE FOURNIT PAS de pilote : HDDRIVER est un logiciel commercial (Uwe
#  Seimet) et rien de tel n'est vendorisable. Il en GREFFE un depuis un disque
#  DONNEUR que vous possédez déjà (--driver-from, ou détection dans hd/) : on
#  recopie sa zone de code d'amorçage, on réécrit la table de partitions pour la
#  nouvelle géométrie, on rétablit la somme $1234, et on copie son fichier pilote
#  à la racine de la partition. --no-driver produit l'image de données d'avant.
#
#  Pourquoi un outil de plus : make_usatan_hd.py fabrique l'image de l'ÉTALON
#  UltraSatan, et son write_fs() (importé de make_usatan_test.py) est câblé en dur
#  sur un unique AUTO\USTEST.PRG — pas d'arborescence, pas de sous-dossiers. Ici on
#  délègue le système de fichiers à mtools, qui sait recopier un arbre entier.
#  La table de partitions Atari, elle, reste écrite à la main : aucun outil hôte ne
#  la connaît (ce n'est PAS un MBR DOS — cf. partitionCountOne, src/io/Acsi.cpp).
#
#  Dépendance : mtools (mformat, mcopy, mdir). Paquet « mtools » sur toutes les distros.
#
#  Usage : python3 tools/make_hd_image.py SRC_DIR OUT.img [--size-mb 16] [--label NOM]
#                                         [--driver-from DISQUE | --no-driver]
#  Puis  : ./build/neost-headless <rom> --ultrasatan --sd1 OUT.img
#
#  68000/TOS big-endian pour la table Atari ; BPB FAT little-endian (DOS).
# =============================================================================
import argparse
import glob
import os
import struct
import shutil
import subprocess
import sys

SECT = 512
PART_START = 2          # la partition commence au secteur 2 (racine + 1 réservé)
SPC = 2                 # clusters de 2 secteurs : la convention AHDI des partitions GEM
NROOT = 128             # entrées de répertoire racine
RES = 1                 # secteurs réservés (le boot sector de la partition)

# Le seuil FAT12/FAT16 est un vrai piège, déjà payé par make_usatan_hd.py : le TOS
# Atari suppose du FAT16 sur tout disque dur, mais EmuTOS applique la règle Microsoft
# (≤ 4084 clusters ⇒ FAT12) et une chaîne de clusters lue en FAT12 casse après le
# premier. Avec SPC=2, il faut donc plus de 4084 × 2 × 512 = 4,2 Mo de partition.
MIN_MB = 6

# Zone de code du secteur racine : de 0 à la table de partitions ($1C2). C'est ce
# qu'on greffe depuis le donneur. Le mot d'AJUSTEMENT de la somme se pose dans le
# rembourrage qui suit le code (les installeurs de pilotes font pareil ; sur le
# donneur observé, code jusqu'à $1A1 puis des zéros).
CODE_END = 0x1C2
SUM_FIX = 0x1BC         # mot libre où l'on rattrape la somme $1234
BOOT_MAGIC = 0x1234

# Noms de pilotes reconnus à la racine de la première partition d'un donneur.
DRIVER_NAMES = ('HDDRIVER.SYS', 'SHDRIVER.SYS', 'AHDI.SYS', 'ICDBOOT.SYS', 'PPDRIVER.SYS')


def word_sum(buf: bytes) -> int:
    return sum(int.from_bytes(buf[i:i + 2], 'big') for i in range(0, len(buf), 2)) & 0xFFFF


def run(cmd, check=True):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if check and r.returncode != 0:
        sys.stderr.write(f"ERREUR: {' '.join(cmd)}\n{r.stdout}{r.stderr}")
        sys.exit(1)
    return r


def part_type(part_size: int) -> bytes:
    """GEM plafonne à 32 Mo (65536 secteurs) ; au-delà le TOS attend BGM."""
    return b'GEM' if part_size < 65536 else b'BGM'


def root_sector(total: int, part_start: int, part_size: int, code: bytes = b'') -> bytes:
    """Secteur racine AHDI. Avec `code`, il devient AMORÇABLE (somme = $1234)."""
    s = bytearray(SECT)
    if code:
        s[0:len(code)] = code[:CODE_END]
    struct.pack_into('>I', s, 0x1C2, total)               # taille du disque
    e = 0x1C6
    s[e] = 0x81 if code else 0x01                         # bit0 existe, bit7 amorçable
    s[e + 1:e + 4] = part_type(part_size)
    struct.pack_into('>I', s, e + 4, part_start)
    struct.pack_into('>I', s, e + 8, part_size)
    # $1F6/$1FA : liste et nombre de secteurs défectueux — aucun.
    if code:
        # Rattrapage de la somme : le TOS n'exécute le secteur QUE si elle vaut $1234.
        struct.pack_into('>H', s, SUM_FIX, 0)
        struct.pack_into('>H', s, SUM_FIX, (BOOT_MAGIC - word_sum(s)) & 0xFFFF)
        assert word_sum(s) == BOOT_MAGIC
    else:
        # Sans pilote : surtout PAS $1234, sinon le TOS exécuterait du vide.
        assert word_sum(s) != BOOT_MAGIC
    return bytes(s)


def first_partition_offset(disk: str):
    """Offset en octets de la 1re partition d'un disque Atari, ou None."""
    with open(disk, 'rb') as f:
        s = f.read(SECT)
    if len(s) < SECT:
        return None
    e = 0x1C6
    if not (s[e] & 0x01) or s[e + 1:e + 4] not in (b'GEM', b'BGM', b'BGM'):
        return None
    return struct.unpack_from('>I', s, e + 4)[0] * SECT


def probe_donor(disk: str):
    """(code d'amorçage, nom du pilote, offset partition) si `disk` est amorçable."""
    try:
        with open(disk, 'rb') as f:
            sec0 = f.read(SECT)
    except OSError:
        return None
    if len(sec0) < SECT or word_sum(sec0) != BOOT_MAGIC:
        return None                                  # pas un secteur racine amorçable
    off = first_partition_offset(disk)
    if off is None:
        return None
    # On INTERROGE mdir fichier par fichier plutôt que de parser son tableau : sa
    # colonne 8.3 sépare nom et extension par une espace (« HDDRIVER SYS »), et
    # chercher « HDDRIVERSYS » dans la sortie ne trouve donc jamais rien.
    for name in DRIVER_NAMES:
        if run(['mdir', '-i', f'{disk}@@{off}', f'::{name}'], check=False).returncode == 0:
            return sec0[:CODE_END], name, off
    return None


def find_donor(explicit: str | None):
    """Donneur explicite, sinon premier disque amorçable trouvé dans hd/."""
    if explicit:
        d = probe_donor(explicit)
        if not d:
            sys.stderr.write(
                f"ERREUR: {explicit} n'est pas un donneur utilisable — il lui faut un\n"
                f"        secteur racine amorçable (somme $1234) ET un pilote parmi\n"
                f"        {', '.join(DRIVER_NAMES)} à la racine de sa 1re partition.\n")
            sys.exit(1)
        return (explicit,) + d
    here = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    cands = []
    for ext in ('img', 'hd', 'acsi', 'vhd', 'raw'):
        cands += sorted(glob.glob(os.path.join(here, 'hd', f'*.{ext}')))
    for c in cands:
        d = probe_donor(c)
        if d:
            return (c,) + d
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description="Carte SD UltraSatan depuis un dossier hôte")
    ap.add_argument('src', help="dossier dont le CONTENU va à la racine de C:")
    ap.add_argument('out', help="image de sortie (.img)")
    ap.add_argument('--size-mb', type=int, default=16, help="taille du disque en Mo (défaut 16)")
    ap.add_argument('--label', default='NEOST', help="nom de volume (11 car. max)")
    ap.add_argument('--driver-from', metavar='DISQUE',
                    help="disque donneur d'où greffer l'amorçage + le pilote TOS "
                         "(défaut : premier disque amorçable trouvé dans hd/)")
    ap.add_argument('--no-driver', action='store_true',
                    help="image de DONNÉES non amorçable (EmuTOS la monte quand même)")
    args = ap.parse_args()

    if not os.path.isdir(args.src):
        sys.stderr.write(f"ERREUR: {args.src} n'est pas un dossier\n")
        return 2
    for tool in ('mformat', 'mcopy', 'mdir'):
        if not shutil.which(tool):
            sys.stderr.write(f"ERREUR: {tool} introuvable — installer le paquet « mtools ».\n")
            return 2
    if args.size_mb < MIN_MB:
        sys.stderr.write(f"ERREUR: {args.size_mb} Mo est sous le plancher FAT16 "
                         f"({MIN_MB} Mo) — EmuTOS lirait la partition en FAT12.\n")
        return 2

    donor = None if args.no_driver else find_donor(args.driver_from)
    if donor is None and not args.no_driver:
        sys.stderr.write(
            "ERREUR: aucun disque donneur amorçable trouvé dans hd/.\n"
            "        Le TOS Atari n'a pas de pilote de disque dur en ROM : sans greffe,\n"
            "        seul EmuTOS montera l'image. Options : --driver-from <disque>, ou\n"
            "        --no-driver pour assumer une image de données.\n")
        return 1

    total = args.size_mb * 1024 * 1024 // SECT
    part_size = total - PART_START

    # 1) Le disque brut + sa table de partitions Atari (amorçable si greffe).
    code = donor[1] if donor else b''
    with open(args.out, 'wb') as f:
        f.write(root_sector(total, PART_START, part_size, code))
        f.truncate(total * SECT)

    # 2) La partition : FAT16 posée à l'octet PART_START*SECT dans le fichier.
    #    -N : n° de série FIXE. ⚠ Ça ne suffit PAS à rendre l'image reproductible au
    #    bit près : mtools horodate la CRÉATION de chaque entrée de répertoire à
    #    l'heure courante, et -m ne préserve que la date de MODIFICATION (premier
    #    écart mesuré à l'octet 67087, 1re entrée racine). Contrairement à
    #    make_usatan_hd.py, cet outil produit du média utilisateur, pas un étalon :
    #    le déterminisme n'est pas requis ici, seulement la stabilité du contenu.
    at = f"{args.out}@@{PART_START * SECT}"
    run(['mformat', '-i', at, '-T', str(part_size), '-c', str(SPC),
         '-r', str(NROOT), '-R', str(RES), '-M', str(SECT),
         '-N', '4e454f53', '-v', args.label[:11].upper(), '::'])

    # 3) Le pilote À LA RACINE — le code d'amorçage l'y cherche par son nom.
    if donor:
        donor_path, _, drv, doff = donor
        # -m des DEUX côtés : sans lui le pilote recopié porte l'heure courante, et
        # deux exécutions ne rendent plus la même image (constaté : premier écart à
        # l'octet 67087, dans le répertoire racine).
        tmp = args.out + '.drv.tmp'
        run(['mcopy', '-i', f'{donor_path}@@{doff}', '-m', '-o', f'::{drv}', tmp])
        run(['mcopy', '-i', at, '-m', '-o', tmp, f'::{drv}'])
        os.remove(tmp)

    # 4) Le contenu. On copie les ENTRÉES du dossier, pas le dossier lui-même.
    entries = sorted(os.listdir(args.src))
    if entries:
        run(['mcopy', '-i', at, '-s', '-Q', '-m']
            + [os.path.join(args.src, e) for e in entries] + ['::'])

    kind = (f"amorçable (pilote {donor[2]} greffé depuis {os.path.basename(donor[0])})"
            if donor else "données, NON amorçable (EmuTOS seulement)")
    print(f"{args.out} : {args.size_mb} Mo, 1 partition {part_type(part_size).decode()} "
          f"FAT16, {len(entries)} entrée(s) à la racine de C: — {kind}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
