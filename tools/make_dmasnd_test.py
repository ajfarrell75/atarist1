#!/usr/bin/env python3
# =============================================================================
#  make_dmasnd_test.py — Génère une disquette .ST bootable STE qui joue un
#  échantillon DMA en boucle TOUT EN MODIFIANT le tampon pendant la lecture,
#  pour valider le modèle FIFO « fetch au faisceau » (divergence S2, port de
#  Hatari DmaSnd_FIFO_* / DmaSnd_STE_HBL_Update) contre l'oracle.
#
#  Scénario (le cas « Mental Hangover / Power Up Plus » distillé) :
#    • tampon de 1248 octets à $030000, rempli d'un CARRÉ A (±80, période 8
#      octets → ~782 Hz à 6258 Hz), joué en mono 6258 Hz, repeat ON ;
#    • le handler VBL écrit un PLATEAU B (+112 constant) sur la PREMIÈRE MOITIÉ
#      du tampon, attend ~70 000 cycles, puis RESTAURE le carré A — la moitié
#      basse du tampon ne contient donc B que pendant ~[4k..78k] de la trame,
#      et TOUJOURS A à la frontière de trame.
#
#  Discrimination : un émulateur qui relit la RAM en FIN de trame (ancien
#  modèle NeoST) ne peut jamais entendre B (moyenne des blocs ≈ 0 partout) ;
#  le vrai STE (et le modèle FIFO : fetch par mots à chaque HBL) capture des
#  octets B quand le fetch tombe dans la fenêtre → blocs à moyenne FORTEMENT
#  POSITIVE quand le DMA traverse la moitié basse du tampon.
#
#  Usage : make_dmasnd_test.py <out.st>
#  Mesure : neost-headless --machine ste --sound-dump t.wav, puis moyenne par
#  bloc de 5 ms — cf. docs/HATARI_DIVERGENCES.md § S2.
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

out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/dmasnd_test.st"

BUF   = 0x030000                  # tampon échantillon (hors écran TOS)
BLEN  = 1248                      # 156 périodes de 8 octets (pair, multiple de 8)
HALF  = BLEN // 2                 # moitié basse réécrite par le handler VBL

# bra.s code (saute le BPB)
bra_s('code')
while len(code) < 0x1E:
    code.append(0x00)

label('code')
w(0x46FC, 0x2700)                 # move.w #$2700,sr

# ---- remplissage : carré A (±80) période 8 octets, 1248 octets ----
w(0x207C); l(BUF)                 # movea.l #BUF,a0
w(0x303C, BLEN // 8 - 1)          # move.w #périodes-1,d0
label('fillA')
w(0x20FC); l(0x50505050)          # move.l #$50505050,(a0)+   (4 × +80)
w(0x20FC); l(0xB0B0B0B0)          # move.l #$B0B0B0B0,(a0)+   (4 × −80)
w(0x51C8); fixups.append((len(code), 'fillA')); code.extend(b'\x00\x00')   # dbra d0,fillA

# ---- registres DMA son : start/end/mode, puis PLAY+REPEAT ----
w(0x11FC, (BUF >> 16) & 0xFF, 0x8903)          # move.b #hi,$ff8903.w  (start)
w(0x11FC, (BUF >> 8) & 0xFF, 0x8905)           # move.b #med,$ff8905.w
w(0x11FC, BUF & 0xFF, 0x8907)                  # move.b #lo,$ff8907.w
END = BUF + BLEN
w(0x11FC, (END >> 16) & 0xFF, 0x890F)          # move.b #hi,$ff890f.w  (end)
w(0x11FC, (END >> 8) & 0xFF, 0x8911)           # move.b #med,$ff8911.w
w(0x11FC, END & 0xFF, 0x8913)                  # move.b #lo,$ff8913.w
w(0x11FC, 0x0080, 0x8921)                      # move.b #$80,$ff8921.w → mono, 6258 Hz
w(0x11FC, 0x0003, 0x8901)                      # move.b #3,$ff8901.w   → play + repeat

# ---- handler VBL installé, VBL démasquée, idle ----
w(0x21FC); fixups.append((len(code), 'vbl_abs')); l(0)   # move.l #vbl,$70.w
w(0x0070)
w(0x46FC, 0x2300)                 # move.w #$2300,sr (VBL niveau 4 passe)
label('idle')
bra_s('idle')

# ---- handler VBL : B sur la moitié basse, ~70k cycles, retour à A ----
label('vbl')
w(0x207C); l(BUF)                 # movea.l #BUF,a0
w(0x303C, HALF // 4 - 1)          # move.w #longs-1,d0
w(0x223C); l(0x70707070)          # move.l #$70707070,d1      (plateau B = +112)
label('fillB')
w(0x20C1)                         # move.l d1,(a0)+
w(0x51C8); fixups.append((len(code), 'fillB')); code.extend(b'\x00\x00')   # dbra d0,fillB
w(0x303C, 6999)                   # move.w #6999,d0           (~70 000 cycles)
label('wait')
w(0x51C8); fixups.append((len(code), 'wait')); code.extend(b'\x00\x00')    # dbra d0,wait
w(0x207C); l(BUF)                 # movea.l #BUF,a0
w(0x303C, HALF // 8 - 1)          # move.w #périodes-1,d0
label('restA')
w(0x20FC); l(0x50505050)          # move.l #$50505050,(a0)+
w(0x20FC); l(0xB0B0B0B0)          # move.l #$B0B0B0B0,(a0)+
w(0x51C8); fixups.append((len(code), 'restA')); code.extend(b'\x00\x00')   # dbra d0,restA
w(0x4E73)                         # rte

# ---- fixups ----
for off, name in fixups:
    if name == 'vbl_abs':                           # adresse ABSOLUE du handler
        # le boot sector est chargé/exécuté par le TOS dans un tampon : on ne
        # connaît pas son adresse → le code se RECOPIE ? Non : le TOS exécute le
        # secteur EN PLACE. On résout via PC-relatif : remplacé plus bas.
        continue
    target = labels[name]
    op = code[off]
    if op == 0x60:                                  # bra.s : disp 8 bits
        disp = target - (off + 2)
        assert -128 <= disp <= 127
        code[off + 1] = disp & 0xFF
    else:                                           # dbra : disp 16 bits
        disp = target - off
        struct.pack_into('>h', code, off, disp)

# L'adresse du handler VBL n'est connue qu'à l'exécution (le TOS charge le boot
# sector où il veut) → on la calcule en tête de code : lea vbl(pc),a1 puis
# move.l a1,$70.w. On PATCHE ici le « move.l #imm,$70.w » posé plus haut (8 o :
# opcode + imm32 + ext abs.w) en séquence équivalente de MÊME taille (8 o) :
# lea disp(pc),a1 (4 o) + move.l a1,$70.w (4 o).
for off, name in fixups:
    if name != 'vbl_abs':
        continue
    # remplace 21FC <imm32> 0070 (8 octets) par 43FA <disp16> 21C9 0070
    disp = labels['vbl'] - off                      # lea vbl(pc),a1 : base = mot d'extension
    struct.pack_into('>HhHH', code, off - 2, 0x43FA, disp, 0x21C9, 0x0070)

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
print(f"écrit {out} ({len(img)} o) ; tampon {BLEN} o @ ${BUF:06X}, code={len(code)} o")
