#!/usr/bin/env python3
# =============================================================================
#  make_fpu_testrom.py — mini-ROM de validation du MC68881 périphérique
#  ($FFFA40, socket Mega STE). Dialogue CIR « façon SFP004 » : écrire le mot
#  de commande dans $FFFA4A, scruter $FFFA40 tant qu'il vaut $8900, transférer
#  les opérandes par $FFFA50. Cf. src/io/Fpu.{hpp,cpp}.
#
#  En-tête : SSP = $00010200 → le mot à l'offset 2 ($0200) sert de « version
#  TOS » 2.00, sinon adjustMachineForTos rétrograde MegaSTE→ST. PC = $E00008.
#
#  Verdict dans la trace headless (--trace --regs) : boucle finale sur
#    PASS : PC stable avec D7 = $0000002A (moveq #42)
#    FAIL : PC stable avec D7 = $FFFFFFxx (moveq #-(numéro du test))
#
#  Usage : python3 tools/make_fpu_testrom.py [sortie.img]
#          ./build/neost-headless sortie.img --machine megaste --fpu \
#              --frames 30 --trace /tmp/fpu.txt --regs
# =============================================================================
import struct
import sys

ROM_BASE = 0xE00000
ROM_SIZE = 256 * 1024

CMD, RESP, OPER = 0xFFFA4A, 0xFFFA40, 0xFFFA50

code = bytearray()
fixups = []   # (offset du mot de déplacement, label, base PC du déplacement)
labels = {}


def w16(v): code.extend(struct.pack(">H", v & 0xFFFF))
def w32(v): code.extend(struct.pack(">I", v & 0xFFFFFFFF))


def movew_imm_absl(imm, addr):          # move.w #imm,addr.l
    w16(0x33FC); w16(imm); w32(addr)


def movel_imm_absl(imm, addr):          # move.l #imm,addr.l
    w16(0x23FC); w32(imm); w32(addr)


def poll():                             # cmpi.w #$8900,RESP.l ; beq.s -10
    w16(0x0C79); w16(0x8900); w32(RESP)
    w16(0x67F6)


def movel_absl_dn(addr, dn):            # move.l addr.l,Dn
    w16(0x2039 | (dn << 9)); w32(addr)


def movew_absl_dn(addr, dn):            # move.w addr.l,Dn
    w16(0x3039 | (dn << 9)); w32(addr)


def cmpil_dn(imm, dn):                  # cmpi.l #imm,Dn
    w16(0x0C80 | dn); w32(imm)


def cmpiw_dn(imm, dn):                  # cmpi.w #imm,Dn
    w16(0x0C40 | dn); w16(imm)


# --- branches/adresses par LABEL (verdict série) ------------------------------
addr_fixups = []   # (offset de l'immédiat 32 bits, label) → adresse absolue ROM


def label(name):
    labels[name] = len(code)


def _branch(op, name):                  # Bcc.w name (disp résolu en 2ᵉ passe)
    w16(op); fixups.append((len(code), name, len(code))); w16(0)


def bra_to(name): _branch(0x6000, name)
def bsr_to(name): _branch(0x6100, name)
def beq_to(name): _branch(0x6700, name)


def movel_label_a3(name):               # move.l #(adresse absolue du label),a3
    w16(0x267C); addr_fixups.append((len(code), name)); w32(0)


def emit_string(name, text):            # chaîne 0-terminée, alignée mot
    label(name)
    b = text.encode("ascii") + b"\x00"
    if len(b) % 2:
        b += b"\x00"
    code.extend(b)


def bne_to(label):                      # bne.w label (fixup différé)
    w16(0x6600)
    fixups.append((len(code), label, len(code)))
    w16(0)


def command(cmd):                       # écrire le Command CIR puis attendre
    movew_imm_absl(cmd, CMD)
    poll()


# ---- programme ---------------------------------------------------------------
# Test 1 : FADD.S — 2.5 + 1.25 = 3.75 ($40700000)
fail_n = 1
command(0x4400)                          # FMOVE.S <ea> → FP0
movel_imm_absl(0x40200000, OPER)         # 2.5f
command(0x4422)                          # FADD.S
movel_imm_absl(0x3FA00000, OPER)         # 1.25f
command(0x6400)                          # FMOVE.S FP0 → mem
movel_absl_dn(OPER, 0)
cmpil_dn(0x40700000, 0)                  # 3.75f
bne_to("fail1")

# Test 2 : FDIV.D — 1.0 / 4.0 = 0.25 ($3FD00000_00000000)
command(0x5400)                          # FMOVE.D <ea> → FP0
movel_imm_absl(0x3FF00000, OPER); movel_imm_absl(0, OPER)        # 1.0
command(0x5420)                          # FDIV.D
movel_imm_absl(0x40100000, OPER); movel_imm_absl(0, OPER)        # 4.0
command(0x7400)                          # FMOVE.D FP0 → mem
movel_absl_dn(OPER, 0); cmpil_dn(0x3FD00000, 0); bne_to("fail2")
movel_absl_dn(OPER, 1); cmpil_dn(0x00000000, 1); bne_to("fail2")

# Test 3 : FMOVECR pi → FMOVE.D = $400921FB_54442D18
command(0x5C00)                          # FMOVECR #0 (pi) → FP0
command(0x7400)
movel_absl_dn(OPER, 0); cmpil_dn(0x400921FB, 0); bne_to("fail3")
movel_absl_dn(OPER, 1); cmpil_dn(0x54442D18, 1); bne_to("fail3")

# Test 4 : FSQRT.D — sqrt(2.0) = $3FF6A09E_667F3BCD
command(0x5400)
movel_imm_absl(0x40000000, OPER); movel_imm_absl(0, OPER)        # 2.0
command(0x5404)                          # FSQRT.D (source FP0 rechargée ? non :
movel_imm_absl(0x40000000, OPER); movel_imm_absl(0, OPER)        # <ea> source)
command(0x7400)
movel_absl_dn(OPER, 0); cmpil_dn(0x3FF6A09E, 0); bne_to("fail4")
movel_absl_dn(OPER, 1); cmpil_dn(0x667F3BCD, 1); bne_to("fail4")

# Test 5 : FINTRZ.D + FMOVE.L — trunc(-3.75) = -3
command(0x5403)                          # FINTRZ.D <ea> → FP0
movel_imm_absl(0xC00E0000, OPER); movel_imm_absl(0, OPER)        # -3.75
command(0x6000)                          # FMOVE.L FP0 → mem
movel_absl_dn(OPER, 0); cmpil_dn(0xFFFFFFFD, 0); bne_to("fail5")

# Test 6 : FCMP.S + Condition CIR — 2.5 == 2.5 → prédicat EQ vrai (TF=1)
command(0x4400)                          # FMOVE.S <ea> → FP0
movel_imm_absl(0x40200000, OPER)         # 2.5f
command(0x4438)                          # FCMP.S <ea>,FP0
movel_imm_absl(0x40200000, OPER)         # 2.5f
movew_imm_absl(0x0001, 0xFFFA4E)         # Condition CIR : prédicat EQ
movew_absl_dn(RESP, 0)
cmpiw_dn(0x0803, 0)                      # null : PF=1, TF=1
bne_to("fail6")

# Test 7 : FMOVEM FPCR aller-retour — écrire $10 (mode RZ), relire, remettre 0
command(0x9000)                          # FMOVEM <ea> → FPCR
movel_imm_absl(0x00000010, OPER)
command(0xB000)                          # FMOVEM FPCR → <ea>
movel_absl_dn(OPER, 0); cmpil_dn(0x00000010, 0); bne_to("fail7")
command(0x9000)                          # FPCR ← 0 (ne pas polluer la suite)
movel_imm_absl(0x00000000, OPER)

# Test 8 : FDIV.X 1.0/3.0 relu en FMOVE.X — prouve la MANTISSE 64 BITS réelle.
# Étendu exact = $3FFD AAAAAAAA_AAAAAAAB (arrondi au plus près) ; un calcul en
# `double` (53 bits) donnerait $3FFD AAAAAAAA_AAAAA800 → l'octet final $AB tranche.
command(0x4800)                          # FMOVE.X <ea> → FP0
movel_imm_absl(0x3FFF0000, OPER); movel_imm_absl(0x80000000, OPER); movel_imm_absl(0x00000000, OPER)  # 1.0
command(0x4820)                          # FDIV.X
movel_imm_absl(0x40000000, OPER); movel_imm_absl(0xC0000000, OPER); movel_imm_absl(0x00000000, OPER)  # 3.0
command(0x6800)                          # FMOVE.X FP0 → mem (12 octets)
movel_absl_dn(OPER, 0); cmpil_dn(0x3FFD0000, 0); bne_to("fail8")   # signe/exposant
movel_absl_dn(OPER, 1); cmpil_dn(0xAAAAAAAA, 1); bne_to("fail8")   # mantisse 63..32
movel_absl_dn(OPER, 2); cmpil_dn(0xAAAAAAAB, 2); bne_to("fail8")   # mantisse 31..0 (64 bits !)

# Test 10 : DOUBLE ARRONDI de la conversion sortante — le cas qui tranche.
# FP0 = 0,5 + 2^-64, soit l'étendu IMMÉDIATEMENT SUPÉRIEUR à 0,5 :
#   $3FFE 80000000_00000001  (exposant -1, mantisse 1 + 2^-63)
# FMOVE.L au plus près doit rendre 1 : la valeur est STRICTEMENT au-dessus de 0,5,
# il n'y a pas d'égalité à départager. Mais un émulateur qui passe d'abord par un
# `double` (53 bits de mantisse) perd le bit 2^-64, retombe sur 0,5 EXACTEMENT,
# puis applique la règle du pair le plus proche → 0. C'est le double arrondi.
# Aucun des tests 1-9 ne le voyait : le test 8 sort en FMOVE.X, qui recopie la
# mantisse sans conversion, et les autres tiennent tous en 53 bits.
command(0x4800)                          # FMOVE.X <ea> → FP0
movel_imm_absl(0x3FFE0000, OPER); movel_imm_absl(0x80000000, OPER); movel_imm_absl(0x00000001, OPER)
command(0x6000)                          # FMOVE.L FP0 → mem
movel_absl_dn(OPER, 0); cmpil_dn(0x00000001, 0); bne_to("fail10")

# Test 11 : INEX2 sur conversion entière inexacte. FMOVE.L de 1,5 rend 2 (pair le
# plus proche) ET DOIT armer INEX2 (FPSR bit 9) + le bit accumulé INEX (bit 3).
# Le 68881 lève INEX2 dès qu'une conversion perd de l'information ; du code qui
# teste l'exactitude d'un arrondi le lit.
command(0x8800)                          # FMOVEM <ea> → FPSR (efface les drapeaux)
movel_imm_absl(0x00000000, OPER)
command(0x4800)                          # FMOVE.X 1,5 → FP0  ($3FFF C0000000_00000000)
movel_imm_absl(0x3FFF0000, OPER); movel_imm_absl(0xC0000000, OPER); movel_imm_absl(0x00000000, OPER)
command(0x6000)                          # FMOVE.L FP0 → mem
movel_absl_dn(OPER, 0); cmpil_dn(0x00000002, 0); bne_to("fail11")
command(0xA800)                          # FMOVEM FPSR → <ea>
movel_absl_dn(OPER, 0)
w16(0x0280); w32(0x00000208)             # andi.l #$208,d0   → INEX2 (bit9) | INEX (bit3)
cmpil_dn(0x00000208, 0); bne_to("fail11")

# Test 12 : le MODE D'ARRONDI DU FPCR s'applique à la conversion SORTANTE simple.
# FPCR ← $10 (RZ, vers zéro), FP0 ← 1/3 étendu ($3FFD AAAAAAAA_AAAAAAAB), puis
# FMOVE.S. En RZ la mantisse simple TRONQUE : $3EAAAAAA. Au plus près (le mode de
# l'HÔTE, qu'un `float(double)` applique quoi qu'en dise le FPCR) on obtiendrait
# $3EAAAAAB — un ulp au-dessus. C'est le test qui distingue les deux.
command(0x9000)                          # FMOVEM <ea> → FPCR
movel_imm_absl(0x00000010, OPER)         # RZ
command(0x4800)                          # FMOVE.X 1/3 → FP0
movel_imm_absl(0x3FFD0000, OPER); movel_imm_absl(0xAAAAAAAA, OPER); movel_imm_absl(0xAAAAAAAB, OPER)
command(0x6400)                          # FMOVE.S FP0 → mem
movel_absl_dn(OPER, 0); cmpil_dn(0x3EAAAAAA, 0); bne_to("fail12")
command(0x9000)                          # FPCR ← 0 (ne pas polluer la suite)
movel_imm_absl(0x00000000, OPER)

# Test 9 : livraison d'exception FP. On ACTIVE DZ dans le FPCR (bit10), puis FDIV
# par 0 → le Response CIR doit livrer « Take Pre-Instruction Exception » (CA=0,
# vecteur DZ $32 en octet bas) = $7032 au lieu du null $0802.
command(0x9000)                          # FMOVEM <ea> → FPCR
movel_imm_absl(0x00000400, OPER)         # enable DZ (bit 10)
command(0x4400)                          # FMOVE.S 1.0 → FP0
movel_imm_absl(0x3F800000, OPER)         # 1.0f
command(0x4420)                          # FDIV.S (poll sort sur $9501)
movel_imm_absl(0x00000000, OPER)         # ÷ 0.0f → DZ activée → response = take-exc
movew_absl_dn(RESP, 0)
cmpiw_dn(0x7032, 0)                      # take-pre-instruction-exc, vecteur DZ $32
bne_to("fail9")

# PASS : D7 = 42 (compat trace) + verdict série « fpu PASS », boucle stable.
w16(0x7E2A)                              # moveq #42,d7
movel_label_a3("pass_str")              # move.l #pass_str,a3
bsr_to("emit")
w16(0x60FE)                             # bra.s *

for n in range(1, 13):                   # FAILn : D7 = -n (compat trace) → verdict commun
    labels[f"fail{n}"] = len(code)
    w16(0x7E00 | ((-n) & 0xFF))          # moveq #-n,d7
    bra_to("fail_common")
label("fail_common")                     # verdict série « fpu FAIL », boucle stable
movel_label_a3("fail_str")
bsr_to("emit")
w16(0x60FE)                             # bra.s *

# Sous-routine emit : a3 = chaîne 0-terminée → écrit chaque octet dans l'UDR $FFFA2F.
label("emit")
w16(0x121B)                              # move.b (a3)+,d1
beq_to("emit_ret")                       # beq.w emit_ret
w16(0x13C1); w32(0x00FFFA2F)             # move.b d1,$FFFA2F.l
bra_to("emit")
label("emit_ret")
w16(0x4E75)                              # rts
emit_string("pass_str", "NEOST-TEST: fpu PASS\r\n")
emit_string("fail_str", "NEOST-TEST: fpu FAIL\r\n")

for off, lname, base in fixups:          # résoudre les bne.w / bra / bsr / beq
    disp = labels[lname] - base
    code[off:off + 2] = struct.pack(">h", disp)
for off, lname in addr_fixups:           # résoudre les adresses absolues de chaînes
    code[off:off + 4] = struct.pack(">I", ROM_BASE + 8 + labels[lname])

# ---- image ROM ----------------------------------------------------------------
rom = bytearray(b"\xFF" * ROM_SIZE)
rom[0:4] = struct.pack(">I", 0x00010200)         # SSP (+ « version TOS 2.00 »)
rom[4:8] = struct.pack(">I", ROM_BASE + 8)       # PC initial
rom[8:8 + len(code)] = code

out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/fpu_testrom.img"
with open(out, "wb") as f:
    f.write(rom)
print(f"{out} : {len(code)} octets de code, ROM {ROM_SIZE // 1024} Ko @ ${ROM_BASE:06X}")
