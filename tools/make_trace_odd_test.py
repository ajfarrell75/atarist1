#!/usr/bin/env python3
# Banc « trace_odd » : vecteur TRACE ($24) IMPAIR + bit T → le 68000 doit prendre
# une ADDRESS ERROR (vecteur 3, groupe 0), pas planter l'émulateur. Le handler
# d'address error pose palette[0]=vert et boucle : écran vert = OK, crash = bug.
import struct, sys
code = bytearray(); labels = {}; fixups = []
def w(*xs):
    for x in xs: code.extend(struct.pack('>H', x & 0xFFFF))
def l(x): code.extend(struct.pack('>I', x & 0xFFFFFFFF))
def label(n): labels[n] = len(code)
def bra_s(n): fixups.append((len(code), n, 'bra8')); code.extend(b'\x60\x00')
def lea_pc(n, a): w(0x41FA | ((a & 7) << 9)); fixups.append((len(code), n, 'pcrel')); code.extend(b'\x00\x00')

bra_s('code')
while len(code) < 0x1E: code.append(0)
label('code')
w(0x46FC, 0x2700)                 # move.w #$2700,sr
w(0x4238, 0x8260)                 # clr.b $8260.w (lo-res)
w(0x31FC, 0x0F00, 0x8240)        # move.w #$0F00,$8240.w (palette[0]=ROUGE avant test)
# vecteur 3 (address error, $0C) → handler 'ae'
lea_pc('ae', 0); w(0x21C8, 0x000C)
# vecteur TRACE ($24) → adresse IMPAIRE $00000101
w(0x21FC); l(0x00000101); w(0x0024) # move.l #$101,$24.w
w(0x46FC, 0xA700)                 # move.w #$A700,sr (T1+S+IPL7)
w(0x4E71)                         # nop → TRACE → vecteur impair → ADDRESS ERROR
label('hang'); bra_s('hang')      # ne devrait jamais s'exécuter (trace re-fire)
label('ae')
w(0x31FC, 0x0070, 0x8240)        # move.w #$0070,$8240.w (palette[0]=VERT = succès)
label('ok'); bra_s('ok')
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
out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/trace_odd.st"
open(out, 'wb').write(img)
print(f"écrit {out} ; code={len(code)} o ; checksum OK")
