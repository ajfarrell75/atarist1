#!/usr/bin/env python3
# Test SYNTHÉTIQUE de l'overscan par impulsion res (hi-res) en FIN de ligne, façon
# Enchanted Land en jeu : un handler HBL exécute, sur CHAQUE ligne, un délai calibré
# puis `res=02` (hi-res) `res=00` (lo-res) → retrait bordure droite + gauche ligne
# suivante. Boote dans NeoST ET Hatari SANS input → on compare les ADRESSES vidéo
# per-ligne (Hatari `--trace video_addr` vs NeoST `NEOST_RENDER_ALL`) pour valider la
# gestion du stride/NO_COUNT du res-pulse (cf. docs/MOIRA_WINUAE_CONVERGENCE.md §7).
# L'écran est rempli d'un dégradé (mots incrémentés) → un décalage de stride se voit.
# Arg optionnel : N = nombre d'itérations du délai dbra (positionne l'impulsion ; déf 38).
# 68000 big-endian ; secteur de boot exécutable (somme 256 mots == 0x1234).
import struct, sys
out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/respulse.st"
N   = int(sys.argv[2]) if len(sys.argv) > 2 else 38       # délai HBL → cycle de l'impulsion
code = bytearray(); labels = {}; fixups = []
def w(*xs):
    for x in xs: code.extend(struct.pack('>H', x & 0xFFFF))
def l(x): code.extend(struct.pack('>I', x & 0xFFFFFFFF))
def label(n): labels[n] = len(code)
def bra_s(n): fixups.append((len(code), n, 'bra8')); code.extend(b'\x60\x00')
def dbra(r, n): w(0x51C8 | (r & 7)); fixups.append((len(code), n, 'dbra')); code.extend(b'\x00\x00')
def lea_pc(n, a): w(0x41FA | ((a & 7) << 9)); fixups.append((len(code), n, 'pcrel')); code.extend(b'\x00\x00')

bra_s('code')
while len(code) < 0x1E: code.append(0)
label('code')
w(0x46FC, 0x2700)                 # move.w #$2700,sr  (IPL7)
w(0x4238, 0x8260)                 # clr.b $8260.w  (lo-res)
w(0x11FC, 0x0002, 0x820A)         # move.b #2,$820a.w (50 Hz)
w(0x11FC, 0x0002, 0x8201)         # move.b #2,$8201.w (base $02xxxx)
w(0x4238, 0x8203)                 # clr.b $8203.w  (base = $020000)
# remplit 64 Ko d'écran d'un DÉGRADÉ (mots incrémentés → décalage de stride visible)
w(0x207C); l(0x00020000)          # movea.l #$00020000,a0
w(0x303C, 0x3FFF)                 # move.w #$3FFF,d0   (0x4000 mots)
w(0x4241)                         # clr.w d1
label('fill')
w(0x30C1)                         # move.w d1,(a0)+
w(0x5241)                         # addq.w #1,d1
dbra(0, 'fill')
# masque MFP (IERA/IERB)
w(0x4238, 0xFA07)                 # clr.b $fa07.w
w(0x4238, 0xFA09)                 # clr.b $fa09.w
# vecteurs : VBL (autovec niv4 $70) → rte ; HBL (niv2 $68) → handler
lea_pc('rtestub', 0); w(0x21C8, 0x0070)
lea_pc('hbl', 0);     w(0x21C8, 0x0068)
w(0x46FC, 0x2100)                 # move.w #$2100,sr (IPL1 → niveaux 2+ pris)
label('main'); bra_s('main')      # boucle
label('rtestub'); w(0x4E73)       # rte
label('hbl')
w(0x7000 | (N & 0xFF))            # moveq #N,d0  (délai)
label('delay'); dbra(0, 'delay')  # dbra d0,delay  → ~N*10 cyc
w(0x11FC, 0x0002, 0x8260)         # move.b #2,$8260.w  (HI-RES)
w(0x11FC, 0x0000, 0x8260)         # move.b #0,$8260.w  (LO-RES)
w(0x4E73)                         # rte
for off, name, kind in fixups:
    t = labels[name]
    if kind == 'bra8':
        d = t - (off + 2); assert -128 <= d <= 127, (name, d); code[off+1] = d & 0xFF
    else:
        d = t - off; assert -32768 <= d <= 32767, (name, d); struct.pack_into('>h', code, off, d)
assert len(code) <= 510, len(code)
boot = bytearray(512); boot[0:len(code)] = code
struct.pack_into('<H', boot, 0x0B, 512); boot[0x0D] = 2
struct.pack_into('<H', boot, 0x0E, 1);  boot[0x10] = 2
struct.pack_into('<H', boot, 0x11, 112); struct.pack_into('<H', boot, 0x13, 1440)
boot[0x15] = 0xF9; struct.pack_into('<H', boot, 0x16, 5)
struct.pack_into('<H', boot, 0x18, 9);  struct.pack_into('<H', boot, 0x1A, 2)
def wsum(b): return sum(struct.unpack('>256H', bytes(b))) & 0xFFFF
struct.pack_into('>H', boot, 0x1FE, 0)
struct.pack_into('>H', boot, 0x1FE, (0x1234 - wsum(boot)) & 0xFFFF)
assert wsum(boot) == 0x1234
img = bytearray(1440 * 512); img[0:512] = boot
open(out, 'wb').write(img)
print(f"écrit {out} ; code={len(code)} o ; N={N} (délai HBL) ; checksum OK")
