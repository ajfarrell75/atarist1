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
#  ── LES DEUX ÉTAGES, ET LEURS DEUX SOMMES DE CONTRÔLE ─────────────────────────
#  L'amorçage HDDRIVER se fait en DEUX étages, chacun gardé par une somme $1234 :
#    · étage 1 = secteur racine du disque. Le TOS ne l'exécute que si ses 256 mots
#      totalisent $1234. Il choisit la partition dont le drapeau, masqué par $F8,
#      vaut $80 (donc « amorçable »), lit son PREMIER secteur, et — désassemblé —
#      refait la même somme dessus : `add.w (a0)+,d0 / dbra / cmp.w #$1234,d0 /
#      bne → abandon` avant `jmp (a4)`.
#    · étage 2 = secteur d'amorçage de la PARTITION, qui sait lire le FAT et charger
#      HDDRIVER.SYS par son nom.
#  D'où les deux pièges, tous deux payés ici :
#    1. mformat écrit un secteur DOS dont la somme est quelconque : l'étage 1 refuse
#       de l'exécuter et le boot s'arrête juste après l'avoir lu (symptôme : la trace
#       ACSI montre les secteurs 0 et 2, puis plus rien). On rétablit $1234 avec un
#       mot d'ajustement en $1FE — là où DOS met sa signature $55AA, et où le disque
#       donneur met déjà le sien.
#    2. le code de l'étage 2 commence à $34, cible du BRA.S de tête, ce qui EMPIÈTE
#       sur les 2 derniers octets du champ « nom de volume ». Le copier à partir de
#       $36 (l'offset « propre » d'après le BPB) laisse ces 2 octets s'exécuter et
#       fait dérailler l'étage 2 en silence.
#  Vérifié : image produite ici, montée en C: sous TOS 1.04 ET TOS 2.06
#  (_drvbits $00000007), pilote chargé depuis la partition comme sur le disque
#  d'origine.
#
#  ⚠ Piège de méthode, à ne pas refaire : `--keys` tape APRÈS le boot, or TOS 2.06
#  attend une touche sur son écran mémoire — utiliser `--keys-at 700 " "`. Et juger
#  sur _drvbits ($4C2) / les vecteurs hdv_* ($472), JAMAIS sur les icônes du bureau :
#  elles viennent des lignes #M de NEWDESK.INF et s'affichent sans lecteur monté.
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
from __future__ import annotations   # `str | None` sous le python 3.9 du système

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
# Entrées de répertoire racine. 128 était BEAUCOUP trop peu : au-delà, mcopy rend
# « No directory slots » et l'image est refusée. 512 est la convention des partitions
# FAT16 de disque dur — c'est aussi ce que porte le disque donneur observé. Coût :
# 32 secteurs, négligeable. ⚠ un nom hors 8.3 consomme PLUSIEURS entrées (VFAT).
NROOT = 512
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
SUM_FIX = 0x1BC         # mot libre où l'on rattrape la somme $1234 (secteur racine)
BOOT_MAGIC = 0x1234
# Secteur d'amorçage de la PARTITION (étage 2). Son code commence à $34 — la cible du
# BRA.S de tête, qui empiète donc sur les 2 derniers octets du champ « nom de volume ».
# Copier à partir de $36 laisse ces 2 octets s'exécuter et fait dérailler l'étage 2.
STAGE2_CODE = 0x34
STAGE2_SUM_FIX = 0x1FE  # là où DOS met $55AA ; le donneur y met son ajustement

# Noms de pilotes reconnus à la racine de la première partition d'un donneur.
DRIVER_NAMES = ('HDDRIVER.SYS', 'SHDRIVER.SYS', 'AHDI.SYS', 'ICDBOOT.SYS', 'PPDRIVER.SYS')


def word_sum(buf: bytes) -> int:
    return sum(int.from_bytes(buf[i:i + 2], 'big') for i in range(0, len(buf), 2)) & 0xFFFF


# Chemin de l'image EN COURS d'écriture. On construit sous un nom temporaire et on
# ne le renomme qu'à la toute fin : un échec en cours de route (mcopy « Disk full »,
# mformat…) laissait sinon à la place du résultat une image parfaitement montable et
# VIDE, qu'on retrouve trois jours plus tard sans se rappeler qu'elle a échoué.
_PARTIAL = None


def _discard_partial():
    global _PARTIAL
    if _PARTIAL and os.path.exists(_PARTIAL):
        try:
            os.remove(_PARTIAL)
        except OSError:
            pass
    _PARTIAL = None


def _cmd_str(cmd) -> str:
    """Ligne de commande lisible : mcopy en reçoit des milliers, l'afficher en
    entier rendait le message d'erreur illisible (972 Ko mesurés sur 9000 fichiers)."""
    if len(cmd) <= 12:
        return ' '.join(cmd)
    return f"{' '.join(cmd[:8])} … (+{len(cmd) - 9} arguments) {cmd[-1]}"


def run(cmd, check=True):
    r = subprocess.run(cmd, capture_output=True, text=True)
    if check and r.returncode != 0:
        sys.stderr.write(f"ERREUR: {_cmd_str(cmd)}\n{r.stdout}{r.stderr}")
        _discard_partial()
        sys.exit(1)
    return r


# Plafond de longueur d'une ligne de commande mcopy. ARG_MAX vaut 2 Mio sur ce Linux
# mais bien moins ailleurs (macOS ~256 Kio utiles) : une ludothèque de quelques
# milliers de fichiers aux noms longs partait droit sur E2BIG. On découpe.
ARG_CHUNK = 96 * 1024


def run_batched(prefix, items, suffix):
    """Exécute `prefix + lot + suffix` en autant de lots qu'il faut."""
    batch, size = [], 0
    for it in items:
        if batch and size + len(it) + 1 > ARG_CHUNK:
            run(prefix + batch + suffix)
            batch, size = [], 0
        batch.append(it); size += len(it) + 1
    if batch:
        run(prefix + batch + suffix)


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
    with open(disk, 'rb') as f:                  # étage 2 : secteur d'amorçage de la partition
        f.seek(off)
        sec2 = f.read(SECT)
    if len(sec2) < SECT or word_sum(sec2) != BOOT_MAGIC:
        return None                              # étage 2 absent ou non exécutable
    for name in DRIVER_NAMES:
        if run(['mdir', '-i', f'{disk}@@{off}', f'::{name}'], check=False).returncode == 0:
            return sec0[:CODE_END], sec2, name, off
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

    # Défaut = greffe du pilote (le disque produit s'amorce sous vrai TOS) ; --no-driver
    # retombe sur une image de données, que seul EmuTOS montera.
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

    # Contrôle de capacité AVANT de travailler : sinon l'échec n'arrive qu'au mcopy, sous
    # la forme d'un « Disk full » de mtools qui ne dit ni combien il manque, ni pourquoi.
    src_bytes = sum(os.path.getsize(os.path.join(d, f))
                    for d, _, fs in os.walk(args.src) for f in fs)
    usable = (part_size - RES - 2 * ((part_size // SPC) * 2 // SECT + 1)) * SECT
    if donor:
        usable -= 64 * 1024                      # le pilote greffé occupe la racine
    # Le nombre d'entrées RACINE est borné par le BPB, indépendamment de la place :
    # sans ce contrôle, l'échec n'arrivait qu'au mcopy, en « No directory slots ».
    n_root = len(os.listdir(args.src)) + (1 if donor else 0)   # +1 : le pilote greffé
    if n_root > NROOT:
        sys.stderr.write(f"ERREUR: {n_root} entrées à la racine de C:, le maximum est "
                         f"{NROOT} — les regrouper dans des sous-dossiers.\n")
        return 1
    if src_bytes > usable:
        sys.stderr.write(f"ERREUR: le contenu fait {src_bytes / 1048576:.1f} Mo, la partition "
                         f"n'en offre que ~{max(0, usable) / 1048576:.1f} — augmenter --size-mb.\n")
        return 1

    global _PARTIAL
    work = args.out + '.part'                    # construction sous un nom temporaire
    _PARTIAL = work

    # 1) Le disque brut + sa table de partitions Atari (amorçable si greffe).
    code = donor[1] if donor else b''      # étage 1 (secteur racine)
    with open(work, 'wb') as f:
        f.write(root_sector(total, PART_START, part_size, code))
        f.truncate(total * SECT)

    # 2) La partition : FAT16 posée à l'octet PART_START*SECT dans le fichier.
    #    -N : n° de série FIXE. ⚠ Ça ne suffit PAS à rendre l'image reproductible au
    #    bit près : mtools horodate la CRÉATION de chaque entrée de répertoire à
    #    l'heure courante, et -m ne préserve que la date de MODIFICATION (premier
    #    écart mesuré à l'octet 67087, 1re entrée racine). Contrairement à
    #    make_usatan_hd.py, cet outil produit du média utilisateur, pas un étalon :
    #    le déterminisme n'est pas requis ici, seulement la stabilité du contenu.
    at = f"{work}@@{PART_START * SECT}"
    # ⚠ 9 caractères, pas 11, quand on greffe : le code de l'étage 2 commence à $34 et
    # écrase les 2 derniers octets du champ « nom de volume » du BPB. Un nom plus long
    # y était SILENCIEUSEMENT tronqué — et mdir n'y voyait rien, parce qu'il lit le nom
    # dans l'entrée de répertoire racine, pas dans le BPB : les deux divergeaient.
    label_max = 9 if donor else 11
    label = args.label[:label_max].upper()
    if len(args.label) > label_max:
        sys.stderr.write(f"ATTENTION: nom de volume tronqué à {label_max} caractères "
                         f"« {label} » (le code d'amorçage occupe la fin du champ).\n")
    run(['mformat', '-i', at, '-T', str(part_size), '-c', str(SPC),
         '-r', str(NROOT), '-R', str(RES), '-M', str(SECT),
         '-N', '4e454f53', '-v', label, '::'])

    # 3) Le pilote À LA RACINE — le code d'amorçage l'y cherche par son nom.
    if donor:
        donor_path, _, stage2, drv, doff = donor
        # -m des DEUX côtés : sans lui le pilote recopié porte l'heure courante, et
        # deux exécutions ne rendent plus la même image (constaté : premier écart à
        # l'octet 67087, dans le répertoire racine).
        tmp = work + '.drv.tmp'
        run(['mcopy', '-i', f'{donor_path}@@{doff}', '-m', '-o', f'::{drv}', tmp])
        run(['mcopy', '-i', at, '-m', '-o', tmp, f'::{drv}'])
        os.remove(tmp)

    # 4) Le contenu. On copie les ENTRÉES du dossier, pas le dossier lui-même.
    #    ⚠ PAS de `mcopy -s` : il recopie chaque dossier dans l'ordre readdir de
    #    l'HÔTE (non déterministe — hachage APFS…), or l'ordre des entrées d'un
    #    répertoire est un CONTRAT côté TOS : \AUTO s'exécute dans l'ordre du
    #    répertoire (STING.PRG doit précéder son client, etc.). On parcourt donc
    #    l'arbre nous-mêmes, trié, dossier par dossier puis fichier par fichier —
    #    l'image devient aussi reproductible octet à octet à contenu identique.
    for here, dirs, files in os.walk(args.src):
        rel = os.path.relpath(here, args.src)
        dst = '::' if rel == '.' else '::' + rel.replace(os.sep, '/')
        dirs.sort()
        for d in dirs:
            run(['mmd', '-i', at, f'{dst}/{d}'.replace('::/', '::')])
        batch = [os.path.join(here, f) for f in sorted(files)]
        if batch:
            run_batched(['mcopy', '-i', at, '-Q', '-m'], batch, [dst])

    # 5) L'ÉTAGE 2. L'étage 1 lit ce secteur puis EXIGE que ses 256 mots totalisent
    #    $1234 avant de l'exécuter (cmp.w #$1234,d0 / bne → abandon) : sans ce
    #    rattrapage il refuse, et le boot s'arrête juste après la lecture du secteur.
    #    On garde NOTRE BPB ($03..$33) — l'étage 2 sait lire un FAT16 ordinaire — et on
    #    greffe le saut de tête plus le code à partir de STAGE2_CODE.
    if donor:
        with open(work, 'r+b') as f:
            f.seek(PART_START * SECT)
            boot = bytearray(f.read(SECT))
            boot[0:3] = stage2[0:3]
            boot[STAGE2_CODE:STAGE2_SUM_FIX] = stage2[STAGE2_CODE:STAGE2_SUM_FIX]
            boot[STAGE2_SUM_FIX:STAGE2_SUM_FIX + 2] = b'\x00\x00'
            fix = (BOOT_MAGIC - word_sum(boot)) & 0xFFFF
            boot[STAGE2_SUM_FIX:STAGE2_SUM_FIX + 2] = fix.to_bytes(2, 'big')
            assert word_sum(boot) == BOOT_MAGIC
            f.seek(PART_START * SECT)
            f.write(boot)

    os.replace(work, args.out)                   # publication ATOMIQUE du résultat
    _PARTIAL = None

    kind = (f"AMORÇABLE — pilote {donor[3]} greffé depuis {os.path.basename(donor[0])}"
            if donor else "image de données : EmuTOS la monte, un vrai TOS non "
                          "(--driver-from pour greffer un pilote)")
    print(f"{args.out} : {args.size_mb} Mo, 1 partition {part_type(part_size).decode()} "
          f"FAT16, {len(os.listdir(args.src))} entrée(s) à la racine de C: — {kind}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
