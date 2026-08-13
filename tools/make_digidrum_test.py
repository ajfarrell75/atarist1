#!/usr/bin/env python3
# =============================================================================
#  make_digidrum_test.py — Génère une disquette .ST bootable qui joue un
#  DIGIDRUM : du son produit en écrivant le registre de VOLUME du YM2149 à
#  plusieurs kHz, tonalités coupées. C'est la technique de toutes les musiques
#  ST « avec batterie » (Hippel, Mad Max, Lotus…), et c'est le cas qui distingue
#  une synthèse HORODATÉE d'une synthèse qui lit les registres « en direct ».
#
#  Scénario :
#    • mixeur YM = $3F (ni tonalité ni bruit) → chaque canal sort en continu le
#      niveau de son DAC de volume : plus rien n'est audible SANS écriture ;
#    • Timer A du MFP à ~7 979 Hz (préscaleur /4, data 77) ;
#    • le handler écrit le point suivant d'une table de 8 valeurs (4 × 15 puis
#      4 × 0) dans le registre 8 → carré de 7979/8 ≈ 997 Hz sur le canal A.
#
#  Discrimination : un frontend qui synthétise en lisant les registres au moment
#  où sa sortie audio réclame un bloc (~43 ms) n'échantillonne le volume qu'une
#  fois par bloc — il rend une quasi-CONSTANTE et la raie à 997 Hz DISPARAÎT.
#  Le modèle « push » horodaté, lui, rejoue chaque écriture à son cycle et rend
#  le carré. C'est exactement ce qui rendait les samples inaudibles côté WASM.
#
#  Usage  : make_digidrum_test.py <out.st>
#  Mesure : neost-headless roms/tos102uk.img --disk out.st --frames 400 \
#             --fastfdc --sound-dump d.wav
#           puis chercher la raie ~997 Hz (tools/wav_tone.py, ou tout FFT).
#
#  Le 68000 est BIG-ENDIAN : mots assemblés octet par octet. Secteur de boot
#  exécutable = somme des 256 mots (mod 65536) == 0x1234.
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


def ym(reg, val):
    """Écrit `val` dans le registre `reg` du YM2149 (sélection $FF8800, donnée $FF8802)."""
    w(0x11FC, reg & 0xFF, 0x8800)      # move.b #reg,$ff8800.w
    w(0x11FC, val & 0xFF, 0x8802)      # move.b #val,$ff8802.w


out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/digidrum_test.st"

TBL  = 0x030000      # table des 8 points (hors zone TOS, comme make_dmasnd_test)
IDX  = 0x030010      # index courant (mot)
TADR = 77            # Timer A : 2 457 600 / 4 / 77 ≈ 7 979 Hz
VEC  = 0x0134        # vecteur Timer A du MFP (VR $40 | canal 13) × 4

# bra.s code (saute le BPB)
bra_s('code')
while len(code) < 0x1E:
    code.append(0x00)

label('code')
w(0x46FC, 0x2700)                      # move.w #$2700,sr — au calme pendant l'installation

# ---- table des 8 points : 4 × volume 15, 4 × volume 0 ----
w(0x207C); l(TBL)                      # movea.l #TBL,a0
w(0x20FC); l(0x0F0F0F0F)               # move.l #$0f0f0f0f,(a0)+
w(0x4298)                              # clr.l (a0)+
w(0x4279); l(IDX)                      # clr.w IDX

# ---- YM : tonalités ET bruit coupés, canaux B/C muets ----
ym(7, 0x3F)                            # mixeur : rien ne passe → seul le DAC de volume sort
ym(8, 0x00)                            # canal A : volume 0 au départ (le handler le module)
ym(9, 0x00)                            # canal B muet
ym(10, 0x00)                           # canal C muet

# ---- vecteur Timer A : adresse résolue en PC-relatif (le TOS charge le
#      secteur où il veut), même astuce que make_dmasnd_test.py ----
w(0x21FC); fixups.append((len(code), 'irq_abs')); l(0)   # move.l #irq,$134.w
w(VEC)

# ---- MFP : Timer A en mode retard, préscaleur /4, IRQ activée et démasquée ----
w(0x11FC, 0x0000, 0xFA19)              # move.b #0,$fffa19.w    (TACR : arrêt)
w(0x11FC, TADR,   0xFA1F)              # move.b #77,$fffa1f.w   (TADR)
w(0x11FC, 0x0001, 0xFA19)              # move.b #1,$fffa19.w    (délai, /4)
w(0x0038, 0x0020, 0xFA07)              # ori.b #$20,$fffa07.w   (IERA bit5 = Timer A)
w(0x0038, 0x0020, 0xFA13)              # ori.b #$20,$fffa13.w   (IMRA bit5)

w(0x46FC, 0x2300)                      # move.w #$2300,sr — le MFP est en niveau 6 : il passe
label('idle')
bra_s('idle')

# ---- handler Timer A : point suivant de la table → registre 8 ----
label('irq')
w(0x48E7, 0xC080)                      # movem.l d0-d1/a0,-(sp)
w(0x207C); l(TBL)                      # movea.l #TBL,a0
w(0x3039); l(IDX)                      # move.w IDX,d0
w(0x5240)                              # addq.w #1,d0
w(0x0240, 0x0007)                      # andi.w #7,d0
w(0x33C0); l(IDX)                      # move.w d0,IDX
w(0x1230, 0x0000)                      # move.b (0,a0,d0.w),d1
w(0x11FC, 0x0008, 0x8800)              # move.b #8,$ff8800.w    (registre volume A)
w(0x11C1, 0x8802)                      # move.b d1,$ff8802.w
w(0x11FC, 0x00DF, 0xFA0F)              # move.b #$df,$fffa0f.w  (ISRA : efface le bit Timer A)
w(0x4CDF, 0x0103)                      # movem.l (sp)+,d0-d1/a0
w(0x4E73)                              # rte

# ---- fixups ----
for off, name in fixups:
    if name == 'irq_abs':
        continue                                    # traité juste après (séquence PC-relative)
    target = labels[name]
    op = code[off]
    if op == 0x60:                                  # bra.s : déplacement 8 bits
        disp = target - (off + 2)
        assert -128 <= disp <= 127
        code[off + 1] = disp & 0xFF
    else:                                           # dbra : déplacement 16 bits
        disp = target - off
        struct.pack_into('>h', code, off, disp)

# « move.l #imm,$134.w » (8 o) → « lea irq(pc),a1 » + « move.l a1,$134.w » (8 o).
for off, name in fixups:
    if name != 'irq_abs':
        continue
    disp = labels['irq'] - off                      # base du lea = mot d'extension
    struct.pack_into('>HhHH', code, off - 2, 0x43FA, disp, 0x21C9, VEC)

assert len(code) <= 510, f"code trop gros : {len(code)} octets"

# ---- secteur de boot + image 720K ----
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
assert wsum(boot) == 0x1234

img = bytearray(1440 * 512)
img[0:512] = boot
with open(out, 'wb') as f:
    f.write(img)
print(f"écrit {out} ({len(img)} o) ; digidrum ~997 Hz via Timer A {2457600 // 4 // TADR} Hz, "
      f"code={len(code)} o")
