#!/usr/bin/env python3
# =============================================================================
#  make_dmasnd_poll_test.py — Génère une disquette .ST bootable STE qui POLLE
#  SERRÉ le compteur de trame du son DMA ($FF890B/0D) pendant que le DMA joue,
#  et rend le résultat en pixels. Étalon de la QUANTIFICATION HBL du refill FIFO.
#
#  POURQUOI. `TODO.md` § Divergences porte, en n°2 : « [SON] quantification HBL
#  du refill FIFO à confronter à l'oracle sur un poll serré de $FF8909/0B/0D ».
#  Le modèle FIFO (divergence S2) a été porté le 2026-07-07 et validé au WAV
#  (33,3 % d'octets B contre 33,2 % à l'oracle) — mais le WAV mesure ce que le
#  DAC CONSOMME, pas la date à laquelle le DMA FETCHE. Or c'est le fetch que le
#  compteur expose, et c'est lui que les programmes lisent pour se synchroniser.
#  Ce chemin n'avait AUCUN étalon : `make_dmasnd_test.py` module le TAMPON et
#  s'observe à l'oreille, il ne lit jamais $FF8909/0B/0D.
#
#  CE QUE L'IMAGE CONTRAINT. Le compteur montre l'adresse de FETCH, en avance
#  de la FIFO (≤ 8 octets) sur le DAC, et il n'avance QUE lorsque la FIFO est
#  re-remplie — au HBL (`DmaSnd_STE_HBL_Update`), par MOTS. Un poll plus court
#  qu'une ligne doit donc lire PLUSIEURS FOIS LA MÊME VALEUR, puis un SAUT au
#  passage de ligne. Une implémentation qui re-remplirait la FIFO à la lecture
#  du compteur (le défaut corrigé le 2026-08-13 dans `DmaSound::liveCounter`)
#  rendrait une rampe continue : les deux images n'ont rien à voir.
#
#  RÉGLAGES. 50066 Hz STÉRÉO = 2 octets par trame d'échantillon, ~6,4 octets
#  par ligne de 512 cycles : le saut par ligne est petit devant 256, donc le
#  seul octet BAS du compteur suffit à voir la structure, et la boucle de poll
#  (~300 cycles) échantillonne une à deux fois par ligne. On rend le MOT
#  $FF890B:$FF890D (octet médian:bas) pour que le franchissement des 256 octets
#  reste lisible au lieu de replier l'image.
#
#  Le tampon joué est un carré ; son CONTENU n'a aucune importance ici — seule
#  la CADENCE du fetch est mesurée. `repeat` est armé pour que le compteur ne
#  s'arrête jamais pendant les 100 tours.
#
#  Usage  : make_dmasnd_poll_test.py <out.st>
#  Oracle : run_etalons.py --oracle --only dmasnd_poll
#
#  Le 68000 est BIG-ENDIAN : mots assemblés octet par octet. Secteur de boot
#  exécutable = somme des 256 mots (mod 65536) == 0x1234.
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST. Outil de test (domaine public).
# =============================================================================
import struct, sys

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


def dbra(reg, name):
    w(0x51C8 | reg)
    fixups.append((len(code), name))
    code.extend(b'\x00\x00')


out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/dmasnd_poll_test.st"

SCREEN = 0x00020000
BUF    = 0x00030000          # tampon son DMA
BUFLEN = 0x8000              # 32 Ko : ~0,33 s à 100 132 o/s, jamais rebouclé
                             # pendant les 100 tours (le repeat est une ceinture)
END    = BUF + BUFLEN
ITERS  = 100                 # une ligne écran par tour de poll
BPL    = 160

bra_s('code')
while len(code) < 0x1E:
    code.append(0x00)

label('code')
w(0x46FC, 0x2700)                  # move.w #$2700,sr → IRQ masquées (poll pur)

# ---- vidéo : basse résolution, 50 Hz, base écran $020000 -------------------
w(0x4238, 0x8260)                  # clr.b $8260.w      → basse résolution
w(0x11FC, 0x0002, 0x820A)          # move.b #2,$820a.w  → 50 Hz
w(0x11FC, 0x0002, 0x8201)          # move.b #2,$8201.w  → base écran $020000
w(0x4238, 0x8203)
w(0x4238, 0x820D)                  # STE : octet bas de la base écran
w(0x4238, 0x820F)                  # offset de ligne = 0
w(0x4278, 0x8240)                  # palette[0] = noir
w(0x31FC, 0x0777, 0x8242)          # palette[1] = blanc

# ---- écran mis à zéro ------------------------------------------------------
w(0x207C); l(SCREEN)
w(0x303C, 1999)
label('cls')
for _ in range(4):
    w(0x4298)
dbra(0, 'cls')

# ---- tampon son : un carré de période 16 octets ----------------------------
# Le CONTENU n'entre pas dans l'image (on ne lit que le compteur) ; on évite le
# silence pur par principe, pour que le chemin DAC travaille comme en vrai.
w(0x207C); l(BUF)
w(0x303C, (BUFLEN // 16) - 1)
label('fill')
for _ in range(2):
    w(0x20FC); l(0x50505050)       # move.l #$50505050,(a0)+
for _ in range(2):
    w(0x20FC); l(0xB0B0B0B0)       # move.l #$b0b0b0b0,(a0)+
dbra(0, 'fill')

# ---- ancrage : VBL + STOP --------------------------------------------------
# ⚠ INDISPENSABLE, et mesuré. La première version démarrait le DMA et pollait
# « en ligne droite », dès la fin du secteur de boot. L'image était alors stable
# d'une trame à l'autre, mais PAS d'un run Hatari à l'autre : le RNG de boot
# (position angulaire de la disquette) décale le démarrage du programme, donc
# l'alignement entre la boucle de poll et la GRILLE HBL — qui est précisément ce
# que l'étalon mesure. Deux runs de la même ligne de commande rendaient 664 px
# d'écart ENTRE EUX (2026-09-02), l'oracle n'était pas re-dérivable.
#
# Remède, identique à celui de make_spec512_test.py (V3, 2026-09-01) : le
# démarrage du DMA **et** la boucle de poll vivent dans le HANDLER DE VBL, et le
# programme l'attend en `stop #$2300`. Une interruption prise depuis STOP a une
# latence FIXE, là où une boucle `bra.s` la prendrait à une frontière
# d'instruction (jusqu'à 10 cycles de jitter). Le fetch DMA démarre donc toujours
# au même cycle de la trame, quelle que soit la durée du boot. IRQ MFP coupées à
# la source (IERA/IERB) ; IPL 3 ne laisse passer que le niveau 4 (VBL), l'HBL
# (niveau 2) reste masquée.
w(0x4238, 0xFA07)                  # clr.b $fffa07.w   (IERA)
w(0x4238, 0xFA09)                  # clr.b $fffa09.w   (IERB)
w(0x43FA)                          # lea vbl(pc),a1
fixups.append((len(code), 'vbl')); code.extend(b'\x00\x00')
w(0x21C9, 0x0070)                  # move.l a1,$70.w   (autovecteur niveau 4)
w(0x46FC, 0x2300)                  # move.w #$2300,sr
label('idle')
w(0x4E72, 0x2300)                  # stop #$2300       → réveil à la VBL, latence fixe
bra_s('idle')

label('vbl')
# ---- son DMA : 50066 Hz stéréo, play + repeat ------------------------------
w(0x11FC, (BUF >> 16) & 0xFF, 0x8903)      # start high
w(0x11FC, (BUF >> 8) & 0xFF, 0x8905)       # start med
w(0x11FC, BUF & 0xFF, 0x8907)              # start low
w(0x11FC, (END >> 16) & 0xFF, 0x890F)      # end high
w(0x11FC, (END >> 8) & 0xFF, 0x8911)       # end med
w(0x11FC, END & 0xFF, 0x8913)              # end low
w(0x11FC, 0x0003, 0x8921)                  # mode : bit7=0 → STÉRÉO, rate 3 → 50066 Hz
w(0x11FC, 0x0003, 0x8901)                  # ctrl : play + repeat

# ---- boucle de poll --------------------------------------------------------
# 100 tours × ~300 cycles ≈ 30 000 cycles, très en deçà d'une trame PAL
# (313 × 512 = 160 256) : la boucle se termine DANS la trame qui l'a lancée, sans
# jamais croiser la VBL suivante.
w(0x227C); l(SCREEN)               # movea.l #SCREEN,a1
w(0x3E3C, ITERS - 1)               # move.w #99,d7

label('loop')
w(0x1038, 0x890B)                  # move.b $ff890b.w,d0  → octet MÉDIAN du compteur
w(0xE148)                          # lsl.w #8,d0          → dans l'octet haut
w(0x1038, 0x890D)                  # move.b $ff890d.w,d0  → octet BAS (move.b garde le haut)
# Le mot médian:bas est RÉPLIQUÉ sur 8 groupes de 16 px (offsets 0, 8, 16 … 56 :
# en basse résolution les mots du plan 0 sont espacés de 8 octets). Même raison
# que dans make_mfp_poll_test.py : un seul mot par ligne ne porterait que 16 px
# sur 114816 et le contrôle de provenance rejetterait la référence comme
# QUASI UNIFORME. Répliquer n'ajoute aucune contrainte, mais rend l'image lisible.
w(0x2049)                          # movea.l a1,a0
w(0x7207)                          # moveq #7,d1
label('rep')
w(0x3080)                          # move.w d0,(a0)
w(0x41E8, 0x0008)                  # lea 8(a0),a0
dbra(1, 'rep')
w(0x43E9, BPL)                     # lea 160(a1),a1
dbra(7, 'loop')

label('halt')
bra_s('halt')                      # image FIGÉE → insensible à la durée de boot

# ---- fixups ----------------------------------------------------------------
for off, name in fixups:
    target = labels[name]
    if code[off] in (0x60, 0x66):
        disp = target - (off + 2)
        assert -128 <= disp <= 127, f"branche hors portée vers {name} ({disp})"
        code[off + 1] = disp & 0xFF
    else:
        struct.pack_into('>h', code, off, target - off)

assert len(code) <= 510, f"code trop gros : {len(code)} octets"

# ---- secteur de boot + image 720K -----------------------------------------
boot = bytearray(512)
boot[0:len(code)] = code
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
assert wsum(boot) == 0x1234, "secteur de boot non exécutable (checksum != $1234)"

img = bytearray(1440 * 512)
img[0:512] = boot
with open(out, 'wb') as f:
    f.write(img)
print(f"écrit {out} ({len(img)} o) ; {ITERS} tours de poll $FF890B/0D ; "
      f"tampon {BUFLEN} o à ${BUF:06X}, 50066 Hz stéréo ; code={len(code)} o")
