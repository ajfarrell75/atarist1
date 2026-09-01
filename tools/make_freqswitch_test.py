#!/usr/bin/env python3
# =============================================================================
#  make_freqswitch_test.py — disquette .ST bootable qui BASCULE 50↔60 Hz EN
#  COURS DE TRAME, pour exercer l'attribution de ligne du chantier V3.
#
#  POURQUOI CE GÉNÉRATEUR EXISTE. Le verrou `NEOST_LINELEN_ATTR` (V3) change la
#  façon dont une écriture de synchro est ATTRIBUÉE à une ligne : à la grille
#  NOMINALE (`frameCycle / 512`) quand il est OFF, à la grille RÉELLE des débuts
#  de ligne quand il est ON. Il est OFF par défaut depuis toujours, et pour une
#  raison qui n'est pas de la prudence mais du VIDE : **aucun étalon du dépôt ne
#  fait la différence**. Le palier `full` est vert avec le verrou armé comme sans
#  — ce qui prouve la non-régression, pas la justesse. Closure a été essayée pour
#  ça le 2026-08-30 et RÉFUTÉE (image bit-identique dans les deux positions :
#  son écran 153 couleurs ne bascule pas la fréquence en cours de trame).
#  Promouvoir le verrou sans exhibiteur serait un pari ; ce disque est
#  l'exhibiteur.
#
#  LE LEVIER, ET POURQUOI IL FAUT UNE LONGUE PLAGE. Une ligne 50 Hz fait 512
#  cycles, une ligne 60 Hz en fait **508**. Chaque ligne passée en 60 Hz décale
#  donc la grille RÉELLE de 4 cycles par rapport à la grille nominale. Tant que
#  ce décalage cumulé reste petit, les deux modèles d'attribution tombent
#  presque toujours sur la même ligne et l'image ne bouge pas — c'est très
#  exactement pourquoi les bascules ponctuelles des démos du dépôt n'exhibent
#  rien. Il faut donc **accumuler au-delà d'une ligne entière** : à partir de
#  128 lignes en 60 Hz (128 × 4 = 512), toute écriture ultérieure est attribuée
#  à une ligne DIFFÉRENTE selon le modèle. D'où la structure ci-dessous :
#
#      · une PLAGE de N lignes en 60 Hz (défaut 140 → 560 cycles de dérive) ;
#      · puis une série de bascules 50/60 alternées, chacune tombant sur une
#        ligne différente selon le modèle → chaque bloc se décale d'une ligne.
#
#  CE QUE ÇA REND À L'ÉCRAN. L'écran est rempli de l'index 1 (palette[1] blanc)
#  sur fond de bordure noire : la LARGEUR affichée de chaque ligne se voit donc
#  directement. Une ligne 60 Hz affiche de 52 à 372, une ligne 50 Hz de 56 à
#  376 — 4 px de décalage à gauche ET à droite. Un bloc de lignes décalé d'un
#  cran par le modèle d'attribution donne 8 px de différence sur chaque ligne
#  de frontière : bien au-delà du bruit, et lisible à l'œil sur la capture.
#
#  ⚠ Les écritures sont posées au MILIEU d'une ligne (cf. PHASE) pour ne PAS
#  déclencher de retrait de bordure : le retrait gauche/droit exige une écriture
#  de fréquence à des cycles bien précis en début/fin de ligne. On veut mesurer
#  l'attribution, pas empiler un second phénomène par-dessus.
#
#  ⚠ L'ANCRE EST LA VBL, ET C'EST LA LEÇON DE LA PREMIÈRE VERSION. Elle se
#  resynchronisait en haut de trame en scrutant le compteur vidéo ($FF8207) —
#  comme make_spec512_test.py, dont c'est le bon choix. Ici c'est FAUX : le
#  compteur vidéo dépend de la fenêtre d'affichage, donc de la fréquence, donc
#  de ce qu'on est en train de mesurer. Mesuré : dès 2 lignes de 60 Hz l'image
#  cessait d'être identique d'une trame à l'autre (2 contenus distincts sur 12
#  trames ; 12 sur 12 avec une plage de 140 lignes) — impossible d'en faire un
#  étalon au pixel. La séquence vit donc dans le HANDLER DE VBL : elle repart de
#  l'interruption de trame, qui reste l'anchor quoi qu'il arrive à la géométrie.
#  Les interruptions MFP sont coupées à la source (IERA/IERB) et le masque passe
#  à IPL 3 pour ne laisser passer que le niveau 4.
#
#  Le 68000 est BIG-ENDIAN : tous les mots sont assemblés octet par octet.
#  Secteur de boot exécutable = somme des 256 mots (mod 65536) == 0x1234.
#
#  Usage : make_freqswitch_test.py [sortie.st] [lignes_60Hz] [blocs]
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


# --- Réglages ----------------------------------------------------------------
# LINES60 : longueur de la plage 60 Hz, en lignes. ≥ 128 pour que la dérive
# cumulée (4 cyc/ligne) dépasse une ligne entière et que les deux modèles
# d'attribution divergent à coup sûr sur tout ce qui suit.
LINES60 = int(sys.argv[2]) if len(sys.argv) > 2 else 140
# BLOCKS : nombre de bascules 50/60 APRÈS la plage. Chacune est une frontière de
# bloc, donc 8 px d'écart entre les deux modèles ; plus il y en a, plus le signal
# est franc.
BLOCKS = int(sys.argv[3]) if len(sys.argv) > 3 else 8
# Une itération de `dbra` prise coûte 10 cycles sur 68000. 51 itérations ≈ 510
# cycles ≈ une ligne. La précision n'a pas à être parfaite : ce qu'on mesure est
# STRUCTUREL (quelle ligne reçoit l'écriture), pas sous-cycle.
ITER_PER_LINE = 51
# Décalage initial pour poser les écritures vers le MILIEU d'une ligne, loin des
# cycles qui déclenchent les retraits de bordure.
PHASE = 25

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
w(0x31FC, 0x0777, 0x8242)         # move.w #$0777,$8242.w → palette[1] = blanc

# ---- RAM écran : index 1 PARTOUT --------------------------------------------
# Basse rés : 4 mots entrelacés pour 16 pixels (plans 0-3). Plan 0 à $FFFF, les
# trois autres à 0 → tous les pixels portent l'index 1. La zone AFFICHÉE de
# chaque ligne se lit donc directement à l'écran, en blanc sur bordure noire :
# c'est ce qui rend la géométrie visible.
w(0x207C); l(0x00020000)          # movea.l #$00020000,a0
w(0x303C, 32000 // 8 - 1)         # move.w #3999,d0
w(0x223C); l(0xFFFF0000)          # move.l #$FFFF0000,d1
label('fill')
w(0x20C1)                         # move.l d1,(a0)+    → plans 0 et 1
w(0x4298)                         # clr.l (a0)+        → plans 2 et 3
dbra(0, 'fill')

# ---- couper les IRQ MFP, puis ancrer la séquence sur la VBL ------------------
# IERA/IERB à 0 : plus aucune source MFP (niveau 6) ne peut interrompre. Sans ça,
# les timers laissés armés par le TOS tomberaient AU MILIEU de la séquence et en
# décaleraient les écritures d'une quantité variable.
w(0x4238, 0xFA07)                 # clr.b $fffa07.w   (IERA)
w(0x4238, 0xFA09)                 # clr.b $fffa09.w   (IERB)
# Vecteur d'autovecteur niveau 4 (VBL) = $70. L'adresse du handler est prise
# RELATIVEMENT AU PC : un secteur de boot est exécuté là où le TOS l'a chargé, et
# cette adresse n'est pas connue à l'assemblage. Un `lea vbl(pc),a1` s'en moque.
w(0x43FA)                         # lea vbl(pc),a1
fixups.append((len(code), 'vbl')); code.extend(b'\x00\x00')
w(0x21C9, 0x0070)                 # move.l a1,$70.w
w(0x46FC, 0x2300)                 # move.w #$2300,sr  → IPL 3 : seule la VBL passe

label('idle')                     # le programme ne fait plus QUE attendre la VBL
bra_s('idle')

# ---- handler de VBL : toute la séquence, ancrée sur le début de trame --------
label('vbl')
# Phase : caler les écritures vers le milieu d'une ligne (hors cycles de retrait).
w(0x383C, PHASE)                  # move.w #PHASE,d4
label('ph')
dbra(4, 'ph')

# --- la PLAGE 60 Hz : c'est elle qui fait dériver la grille réelle ------------
w(0x11FC, 0x0000, 0x820A)         # move.b #0,$820a.w  → 60 Hz
w(0x303C, LINES60 * ITER_PER_LINE)   # move.w #…,d0
label('hold')
dbra(0, 'hold')

# --- les bascules : chacune tombe sur une ligne DIFFÉRENTE selon le modèle ----
w(0x363C, BLOCKS - 1)             # move.w #BLOCKS-1,d3
label('blk')
w(0x11FC, 0x0002, 0x820A)         # move.b #2,$820a.w  → 50 Hz
w(0x303C, 4 * ITER_PER_LINE)      # 4 lignes
label('b1')
dbra(0, 'b1')
w(0x11FC, 0x0000, 0x820A)         # move.b #0,$820a.w  → 60 Hz
w(0x303C, 4 * ITER_PER_LINE)      # 4 lignes
label('b2')
dbra(0, 'b2')
dbra(3, 'blk')

# Revenir en 50 Hz pour finir la trame proprement (et que la suivante reparte
# d'un état connu — sans quoi le nombre de lignes par trame changerait aussi).
w(0x11FC, 0x0002, 0x820A)         # move.b #2,$820a.w
w(0x4E73)                         # rte

# ---- résolution des déplacements --------------------------------------------
for off, name in fixups:
    target = labels[name]
    op = code[off]
    if op in (0x60, 0x65, 0x64):                  # bra.s / bcs.s / bcc.s : 8 bits
        disp = target - (off + 2)
        assert -128 <= disp <= 127, f"branche courte hors portée {name} : {disp}"
        code[off + 1] = disp & 0xFF
    else:                                          # dbra / lea(pc) : 16 bits, relatif au mot
        disp = target - off
        assert -32768 <= disp <= 32767, f"déplacement hors portée {name} : {disp}"
        struct.pack_into('>h', code, off, disp)

assert len(code) <= 510, f"code trop gros : {len(code)} octets"

# ---- secteur de boot 512 o ---------------------------------------------------
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
out = sys.argv[1] if len(sys.argv) > 1 else "disks/etalons/freq_switch.st"
with open(out, 'wb') as f:
    f.write(img)
print(f"écrit {out} ({len(img)} o) ; code={len(code)} o ; "
      f"plage 60 Hz={LINES60} lignes (dérive {LINES60 * 4} cyc) ; blocs={BLOCKS} ; "
      f"checksum OK (0x1234)")
