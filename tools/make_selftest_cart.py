#!/usr/bin/env python3
# =============================================================================
#  make_selftest_cart.py — Cartouche DIAGNOSTIC (magic $FA52235F, saut $FA0004 au
#  reset) qui exécute des auto-tests et émet un VERDICT sur le port série RS-232.
#
#  Convention de verdict (une ligne par test, scannée par tools/run_selftests.py) :
#      NEOST-TEST: <nom> PASS
#      NEOST-TEST: <nom> FAIL <détail optionnel>
#
#  Avantages du chemin cartouche diagnostic : pré-TOS, sans disque, déterministe,
#  aucun oracle. Le CPU saute à $FA0004 dès le reset (TOS 1.x / EmuTOS le vérifient),
#  masque les IRQ et écrit chaque octet de verdict dans l'UDR $FFFA2F (sink série
#  capturé par neost-headless). Tests inclus :
#    • cpu     — invariants arithmétiques (add.w, moveq #-1/cmpi.l) : garde-fou cœur 68000.
#    • timing  — le compteur vidéo $FF8209 n'est PAS figé (le moteur d'horloge/faisceau
#                tourne) : sentinelle anti-« clock morte » / trame gelée.
#
#  --break cpu|timing force le test correspondant à ÉCHOUER (pour valider que le
#  runner attrape bien les FAIL).
#
#  68000 big-endian. (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
import argparse
import struct
import sys

CART_BASE = 0xFA0000
UDR       = 0x00FFFA2F      # MFP USART data register (émission série)
VIDLO     = 0x00FF8209      # compteur d'adresse vidéo, octet bas (avance avec le faisceau)


def build(break_test: str | None) -> bytes:
    # --- Mini-assembleur 2 passes (labels + branches/immédiats résolus) ---------
    items = []   # ('raw',[words]) | ('immL',[prefix],label) | ('br',opword,label) | ('label',name)
    labels = {}

    def raw(*words): items.append(('raw', list(words)))
    def lbl(name):   items.append(('label', name))
    def br(op, name): items.append(('br', op, name))
    def imm_addr(prefix, name):  # move.l #(CART_BASE+label),An : prefix + 32-bit adresse
        items.append(('immL', [prefix], name))

    # move.l #imm32,An : opword 0x2N7C (N = 8+reg pour An en champ dest) — on passe l'opword.
    A3 = 0x267C  # a3
    A4 = 0x287C  # a4
    A5 = 0x2A7C  # a5
    A7 = 0x2E7C  # a7 (sp)

    # Détermine quelle égalité attendue casser (pour --break).
    add_expect = 0x1234 if break_test != 'cpu' else 0x9999   # add.w #$0234,#$1000 → $1234
    # Pour break timing : on échantillonne une adresse ROM STABLE (le magic de la
    # cartouche, $FA0000) au lieu du compteur vidéo → 3 échantillons identiques → FAIL
    # (sans fauter, contrairement aux registres MMIO sensibles).
    vidsrc = VIDLO if break_test != 'timing' else CART_BASE

    def raw32(opword, val):                 # move.l #imm32,An
        raw(opword, (val >> 16) & 0xFFFF, val & 0xFFFF)

    # ---------------- code (entrée = $FA0004) ----------------
    raw32(A7, 0x00008000)                   # move.l #$00008000,a7   (pile)
    raw32(A4, UDR)                          # move.l #$00FFFA2F,a4

    # -- test CPU --------------------------------------------------------------
    raw(0x303C, 0x1000)                     # move.w #$1000,d0
    raw(0x0640, 0x0234)                     # addi.w #$0234,d0
    raw(0x0C40, add_expect)                 # cmpi.w #expect,d0
    br(0x6600, 'cpu_fail')                  # bne cpu_fail
    raw(0x70FF)                             # moveq #-1,d0
    raw(0x0C80, 0xFFFF, 0xFFFF)             # cmpi.l #$FFFFFFFF,d0
    br(0x6600, 'cpu_fail')                  # bne cpu_fail
    imm_addr(A3, 'cpu_pass'); br(0x6100, 'emit')   # move.l #cpu_pass,a3 ; bsr emit
    br(0x6000, 'timing')                    # bra timing
    lbl('cpu_fail')
    imm_addr(A3, 'cpu_failstr'); br(0x6100, 'emit')

    # -- test timing (compteur vidéo non figé) ---------------------------------
    lbl('timing')
    raw32(A5, vidsrc)                       # move.l #vidsrc,a5
    raw(0x1415)                             # move.b (a5),d2   (échantillon 0)
    raw(0x3E3C, 0x2000)                     # move.w #$2000,d7
    lbl('dly1'); br(0x51CF, 'dly1')         # dbra d7,dly1  (boucle sur elle-même = délai)
    raw(0x1615)                             # move.b (a5),d3   (échantillon 1)
    raw(0x3E3C, 0x2000)                     # move.w #$2000,d7
    lbl('dly2'); br(0x51CF, 'dly2')         # dbra d7,dly2
    raw(0x1815)                             # move.b (a5),d4   (échantillon 2)
    raw(0xB602)                             # cmp.b d2,d3
    br(0x6600, 'tim_pass')                  # bne tim_pass  (a changé → vivant)
    raw(0xB802)                             # cmp.b d2,d4
    br(0x6600, 'tim_pass')                  # bne tim_pass
    imm_addr(A3, 'tim_failstr'); br(0x6100, 'emit')
    br(0x6000, 'done')
    lbl('tim_pass')
    imm_addr(A3, 'tim_passstr'); br(0x6100, 'emit')

    lbl('done')
    raw(0x4E72, 0x2700)                     # stop #$2700

    # -- sous-routine emit : a3 = chaîne (0-terminée), écrit sur UDR (a4) --------
    lbl('emit')
    raw(0x121B)                             # move.b (a3)+,d1
    br(0x6700, 'emit_ret')                  # beq emit_ret
    raw(0x1881)                             # move.b d1,(a4)
    br(0x6000, 'emit')                      # bra emit
    lbl('emit_ret')
    raw(0x4E75)                             # rts

    # -- chaînes de verdict -----------------------------------------------------
    def stritem(name, text):
        lbl(name)
        b = text.encode('ascii') + b'\x00'
        if len(b) % 2: b += b'\x00'         # aligne (mots)
        words = [ (b[i] << 8) | b[i+1] for i in range(0, len(b), 2) ]
        raw(*words)
    stritem('cpu_pass',    'NEOST-TEST: cpu PASS\r\n')
    stritem('cpu_failstr', 'NEOST-TEST: cpu FAIL\r\n')
    stritem('tim_passstr', 'NEOST-TEST: timing PASS\r\n')
    stritem('tim_failstr', 'NEOST-TEST: timing FAIL\r\n')

    # --- passe 1 : offsets (le code commence à l'offset 4, après le magic) ------
    off = 4
    for it in items:
        if it[0] == 'label':
            labels[it[1]] = off
        elif it[0] == 'raw':
            off += 2 * len(it[1])
        elif it[0] == 'immL':
            off += 2 * len(it[1]) + 4        # prefix + adresse 32 bits
        elif it[0] == 'br':
            off += 4                         # opword + disp16

    # --- passe 2 : émission -----------------------------------------------------
    words = []
    off = 4
    for it in items:
        if it[0] == 'label':
            continue
        if it[0] == 'raw':
            words += it[1]; off += 2 * len(it[1])
        elif it[0] == 'immL':
            words += it[1]; off += 2 * len(it[1])
            addr = CART_BASE + labels[it[2]]
            words += [(addr >> 16) & 0xFFFF, addr & 0xFFFF]; off += 4
        elif it[0] == 'br':
            op = it[1]
            disp = labels[it[2]] - (off + 2)
            words += [op, disp & 0xFFFF]; off += 4

    data = bytearray()
    data += struct.pack('>I', 0xFA52235F)    # magic diagnostic
    for wd in words:
        data += struct.pack('>H', wd & 0xFFFF)
    if len(data) % 2: data += b'\x00'
    return bytes(data)


def main() -> int:
    ap = argparse.ArgumentParser(description="Cartouche diagnostic auto-test NeoST (verdict série)")
    ap.add_argument("out", help="fichier .bin de sortie")
    ap.add_argument("--break", dest="brk", choices=("cpu", "timing"),
                    help="force ce test à ÉCHOUER (validation du runner)")
    args = ap.parse_args()
    data = build(args.brk)
    with open(args.out, "wb") as f:
        f.write(data)
    print(f"cartouche diagnostic → {args.out} ({len(data)} octets)"
          + (f" [BREAK {args.brk}]" if args.brk else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
