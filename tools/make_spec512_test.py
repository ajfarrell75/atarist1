#!/usr/bin/env python3
# =============================================================================
#  make_spec512_test.py — disquette .ST bootable qui change la PALETTE PLUSIEURS
#  FOIS PAR LIGNE (principe du Spectrum 512), pour valider le re-rendu
#  intra-ligne de NeoST contre l'oracle Hatari. Chantier A10.
#
#  POURQUOI CE GÉNÉRATEUR EXISTE. Les trois étalons Spectrum 512 du projet
#  s'appuient sur `disks/etalons/spectrum_512_auto_diapo.st`, qui exige un TOS
#  Atari : ce disque n'a PAS de secteur de boot exécutable, sa diapo est lancée
#  par un dossier AUTO qu'EmuTOS n'exécute pas jusqu'au bout (réfuté à l'oracle
#  le 2026-08-28 — Hatari + EmuTOS retombe sur le bureau, exactement comme NeoST).
#  Le jour de la purge, ces trois étalons deviennent des SKIP recensés et TOUTE la
#  couverture « palette changée en cours de ligne » disparaît. Celui-ci la rend,
#  sans une ligne de code propriétaire.
#
#  CE QU'IL EXERCE, et que rien d'autre n'exerce sans ROM Atari :
#    · la capture des écritures palette DATÉES au cycle (recordColorWrite) ;
#    · la bascule en re-rendu par ligne (spec512Active_) ;
#    · la position HORIZONTALE exacte à laquelle une écriture palette prend effet
#      — c'est le seul endroit du rendu où un cycle de CPU se voit à l'œil.
#
#  COMMENT. Écran basse résolution 50 Hz, RAM écran remplie de l'index 1 partout
#  (plan 0 à 1, plans 1-3 à 0). La boucle principale se resynchronise en haut de
#  trame sur le compteur vidéo ($FF8207), puis martèle palette[1] ($FF8242) avec
#  trois couleurs séparées par un délai FIXE. Le motif est donc identique à chaque
#  trame → image STATIQUE, et la position des bandes dépend du modèle de cycle :
#  c'est précisément la mesure qu'on veut.
#
#  Le 68000 est BIG-ENDIAN : tous les mots sont assemblés octet par octet.
#  Secteur de boot exécutable = somme des 256 mots (mod 65536) == 0x1234.
#
#  Usage : make_spec512_test.py [sortie.st] [cycles_entre_ecritures]
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
import struct
import sys

code = bytearray()
labels = {}
fixups = []


def w(*words):
    for x in words:
        code.extend(struct.pack('>H', x & 0xFFFF))


def l(x):
    code.extend(struct.pack('>I', x & 0xFFFFFFFF))


def label(name):
    labels[name] = len(code)


def bra_s(name):
    fixups.append((len(code), name))
    code.extend(b'\x60\x00')


def bcc_s(op, name):
    fixups.append((len(code), name))
    code.extend(bytes([op, 0x00]))


def dbra(reg, name):          # dbra Dn,label — déplacement 16 bits
    w(0x51C8 | (reg & 7))
    fixups.append((len(code), name))
    code.extend(b'\x00\x00')


# Délai entre deux écritures palette, en itérations de dbra (≈10 cycles chacune).
# 5 → ~64 cycles, soit ~8 changements de couleur par ligne de 512 cycles. Réglable
# en 2ᵉ argument : c'est le bouton qui rend le motif plus ou moins fin.
DELAY = int(sys.argv[2]) if len(sys.argv) > 2 else 5

# ---- secteur de boot : offset 0 = bra.s vers le code (saute le BPB) ----------
bra_s('code')
while len(code) < 0x1E:
    code.append(0x00)

label('code')
w(0x46FC, 0x2700)                 # move.w #$2700,sr   (superviseur, IPL7 : pas d'IRQ)
w(0x4238, 0x8260)                 # clr.b $8260.w      → basse résolution
w(0x11FC, 0x0002, 0x820A)         # move.b #2,$820a.w  → 50 Hz
w(0x11FC, 0x0002, 0x8201)         # move.b #2,$8201.w  → base vidéo $02xxxx
w(0x4238, 0x8203)                 # clr.b $8203.w      → base vidéo = $020000
w(0x4278, 0x8240)                 # clr.w $8240.w      → palette[0] = noir (bordure)

# ---- RAM écran : index 1 PARTOUT ------------------------------------------
# Basse rés : 4 mots entrelacés pour 16 pixels (plan 0, 1, 2, 3). Plan 0 à $FFFF
# et les trois autres à 0 → tous les pixels portent l'index 1, donc TOUS suivent
# palette[1]. Un seul registre de palette à marteler, et l'écran entier réagit.
w(0x207C); l(0x00020000)          # movea.l #$00020000,a0
w(0x303C, 32000 // 8 - 1)         # move.w #3999,d0    (4 000 groupes de 8 octets)
w(0x223C); l(0xFFFF0000)          # move.l #$FFFF0000,d1
label('fill')
w(0x20C1)                         # move.l d1,(a0)+    → plans 0 et 1
w(0x4298)                         # clr.l (a0)+        → plans 2 et 3
dbra(0, 'fill')

# ---- boucle principale ----------------------------------------------------
label('sync')
# Attendre d'être bien DANS l'affichage (octet médian du compteur vidéo grand),
# puis attendre sa remise à zéro : on repart au même point de chaque trame, donc
# le motif est identique d'une trame à l'autre → image statique.
label('s1')
w(0x1438, 0x8207)                 # move.b $8207.w,d2
w(0x0C02, 0x0040)                 # cmpi.b #$40,d2
bcc_s(0x65, 's1')                 # bcs.s s1  (encore trop tôt)
label('s2')
w(0x1438, 0x8207)                 # move.b $8207.w,d2
w(0x0C02, 0x0004)                 # cmpi.b #$04,d2
bcc_s(0x64, 's2')                 # bcc.s s2  (pas encore le haut de trame)

# Nombre d'itérations : chacune coûte ~300 cycles (3 écritures + 3 délais), mesuré
# à la trace NEOST_PAL_TRACE. Une trame PAL fait 313×512 = 160 256 cycles, donc ~500
# itérations la couvrent en laissant de quoi se resynchroniser. Trop peu → le bas de
# l'écran garde la dernière couleur ; trop → la boucle déborde sur la trame suivante
# et le resync ne voit qu'une trame sur deux (constaté avec 2 600).
w(0x363C, 500)                    # move.w #500,d3
label('band')
w(0x31FC, 0x0700, 0x8242)         # move.w #$0700,$8242.w   → palette[1] = ROUGE
w(0x383C, DELAY)                  # move.w #DELAY,d4
label('w1')
dbra(4, 'w1')
w(0x31FC, 0x0070, 0x8242)         # palette[1] = VERT
w(0x383C, DELAY)
label('w2')
dbra(4, 'w2')
w(0x31FC, 0x0007, 0x8242)         # palette[1] = BLEU
w(0x383C, DELAY)
label('w3')
dbra(4, 'w3')
dbra(3, 'band')
bra_s('sync')

# ---- résolution des déplacements -------------------------------------------
for off, name in fixups:
    target = labels[name]
    op = code[off]
    if op in (0x60, 0x65, 0x64):                  # bra.s / bcs.s / bcc.s : 8 bits
        disp = target - (off + 2)
        assert -128 <= disp <= 127, f"branche courte hors portée {name} : {disp}"
        code[off + 1] = disp & 0xFF
    else:                                          # dbra : 16 bits, relatif au mot
        disp = target - off
        assert -32768 <= disp <= 32767, f"dbra hors portée {name} : {disp}"
        struct.pack_into('>h', code, off, disp)

assert len(code) <= 510, f"code trop gros : {len(code)} octets"

# ---- secteur de boot 512 o --------------------------------------------------
boot = bytearray(512)
boot[0:len(code)] = code
# BPB minimal (720 Ko, 9 secteurs/piste, 2 faces, 80 pistes), champs little-endian :
# le TOS doit pouvoir lire la disquette sans erreur avant d'exécuter le secteur.
struct.pack_into('<H', boot, 0x0B, 512)
boot[0x0D] = 2
struct.pack_into('<H', boot, 0x0E, 1)
boot[0x10] = 2
struct.pack_into('<H', boot, 0x11, 112)
struct.pack_into('<H', boot, 0x13, 1440)
boot[0x15] = 0xF9
struct.pack_into('<H', boot, 0x16, 5)
struct.pack_into('<H', boot, 0x18, 9)
struct.pack_into('<H', boot, 0x1A, 2)


def wsum(b):
    return sum(struct.unpack('>256H', bytes(b))) & 0xFFFF


struct.pack_into('>H', boot, 0x1FE, 0)
struct.pack_into('>H', boot, 0x1FE, (0x1234 - wsum(boot)) & 0xFFFF)
assert wsum(boot) == 0x1234, "secteur de boot non exécutable (somme ≠ $1234)"

img = bytearray(1440 * 512)
img[0:512] = boot
out = sys.argv[1] if len(sys.argv) > 1 else "disks/etalons/spec512_bands.st"
with open(out, 'wb') as f:
    f.write(img)
print(f"écrit {out} ({len(img)} o) ; code={len(code)} o ; délai={DELAY} ; checksum OK (0x1234)")
