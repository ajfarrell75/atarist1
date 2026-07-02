#!/usr/bin/env python3
# Variante ENTRY-PHASE du poll-test : le handler HBL attend ~254 cycles (dbra)
# AVANT de lire $8209 → la lecture tombe EN PLEIN Display-Enable (compteur en
# marche) et la couleur encode la PHASE D'ENTRÉE du handler au cycle près (le
# poll-test de base lit pendant le blank, compteur figé → insensible à l'entrée).
# C'est L'ORACLE de la latence d'exception HBL (E-clock @ IACK, alignements,
# frontière de reconnaissance IPL) : diff pixel vs Hatari (recette : avirecord
# + ffmpeg, cf. docs/MOIRA_WINUAE_CONVERGENCE.md bloc 2026-07-02).
# Mesuré 2026-07-02 : NeoST 36/180 vs Hatari (structure de quantification par
# période de 5 lignes IDENTIQUE {x,x+4,x+8} ratio 2:2:1, mais latence d'exception
# NeoST = WinUAE − 12 et phase absolue décalée). Les constantes ajoutées au bloc
# IACK sont ABSORBÉES par la boucle main 12 cyc (equilibre auto-verrouillé) →
# le résidu vit dans l'INTERACTION reconnaissance/E-clock/alignement.
import struct, sys
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
w(0x46FC, 0x2700)                 # move.w #$2700,sr  (IPL7 setup)
w(0x4238, 0x8260)                 # clr.b $8260.w  (lo-res)
w(0x11FC, 0x0002, 0x820A)         # move.b #2,$820a.w (50 Hz)
w(0x11FC, 0x0002, 0x8201)         # move.b #2,$8201.w (base vidéo $02xxxx)
w(0x4238, 0x8203)                 # clr.b $8203.w  (base = $020000)
# remplit 64 Ko d'écran avec 0 → tous les pixels = palette[0] (suivra le HBL)
w(0x207C); l(0x00020000)          # movea.l #$00020000,a0
w(0x303C, 0x3FFF)                 # move.w #$3FFF,d0   (0x4000 longs = 64 Ko)
label('fill')
w(0x4298)                         # clr.l (a0)+
dbra(0, 'fill')                   # dbra d0,fill
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
w(0x4240)                         # clr.w d0
w(0x7418)                         # moveq #24,d2 (délai ~254 cyc → lecture EN DE)
label('dly'); dbra(2, 'dly')
w(0x1038, 0x8209)                 # move.b $8209.w,d0  (lit compteur bas, abs.w)
w(0x31C0, 0x8240)                 # move.w d0,$8240.w  (palette[0] = couleur)
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
out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/poll_entry.st"
open(out, 'wb').write(img)
print(f"écrit {out} ; code={len(code)} o ; checksum OK")
