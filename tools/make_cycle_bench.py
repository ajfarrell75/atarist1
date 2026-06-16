#!/usr/bin/env python3
# Banc de cycles différentiel Moira↔WinUAE : secteur de boot exécutable qui masque
# les IRQ (IPL7) et exécute une série de BOUCLES d'instructions (chacune `moveq #K,d7 ;
# Lx: <corps> ; dbra d7,Lx`), puis boucle. Chaque corps est un motif d'instruction à
# comparer cycle-à-cycle entre les deux cœurs (cf. tools/trace_diff.py --periods +
# docs/MOIRA_WINUAE_CONVERGENCE.md). 68000 big-endian ; somme 256 mots == 0x1234.
import struct, sys
code = bytearray(); labels = {}; fixups = []
def w(*xs):
    for x in xs: code.extend(struct.pack('>H', x & 0xFFFF))
def l(x): code.extend(struct.pack('>I', x & 0xFFFFFFFF))
def label(n): labels[n] = len(code)
def bra_s(n): fixups.append((len(code), n, 'bra8')); code.extend(b'\x60\x00')
def dbra7(n): w(0x51CF); fixups.append((len(code), n, 'dbra')); code.extend(b'\x00\x00')

K = 150
# Chaque test = (nom, [mots du corps]). Corps n'utilise PAS d7 (compteur) ni a7.
# a0=$00020000 (RAM), a1=$00FFFA01 (MMIO MFP GPIP), d0=0, d1=0, d2=1.
TESTS = [
    ("nop",       [0x4E71]),               # interne pur
    ("mov_rr",    [0x3200]),               # move.w d0,d1
    ("mov_ramR",  [0x3210]),               # move.w (a0),d1   (lecture RAM, CHIP16)
    ("mov_ramW",  [0x3081]),               # move.w d1,(a0)   (écriture RAM)
    ("mov_ramL",  [0x2210]),               # move.l (a0),d1   (lecture long RAM)
    ("mov_mmio",  [0x1211]),               # move.b (a1),d1   (lecture MMIO, FAST)
    ("add_rr",    [0xD240]),               # add.w d0,d1
    ("addq",      [0x5241]),               # addq.w #1,d1
    ("addi",      [0x0641, 0x1234]),       # add.w #$1234,d1  (mot d'extension)
    ("lsl4",      [0xE949]),               # lsl.w #4,d1
    ("lsr_r",     [0xE469]),               # lsr.w d2,d1      (d2=1 → 1 décalage)
    ("tst",       [0x4A41]),               # tst.w d1
    ("clr_ram",   [0x4250]),               # clr.w (a0)
    ("cmp_imm",   [0x0C41, 0x0001]),       # cmpi.w #1,d1
]

bra_s('code')
while len(code) < 0x1E: code.append(0)
label('code')
w(0x46FC, 0x2700)                 # move.w #$2700,sr  (IPL7 : aucune IRQ → périodes propres)
w(0x207C); l(0x00020000)          # movea.l #$00020000,a0
w(0x227C); l(0x00FFFA01)          # movea.l #$00FFFA01,a1
w(0x7000)                         # moveq #0,d0
w(0x7200)                         # moveq #0,d1
w(0x7402)                         # moveq #2,d2  (>0 pour lsr.w d2,d1)
label('main')
for name, body in TESTS:
    w(0x7E00 | (K & 0xFF))         # moveq #K,d7
    label('L_' + name)
    for word in body: w(word)
    dbra7('L_' + name)
bra_s('main')

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
out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/cyclebench.st"
open(out, 'wb').write(img)
# Carte des labels (offset relatif au début du code = base de chargement + 0x1E + ...).
print(f"écrit {out} ; code={len(code)} o ; checksum OK")
print("offsets (depuis début du secteur) :")
for name, _ in TESTS:
    print(f"  L_{name:9} = +0x{labels['L_'+name]:03x}")
