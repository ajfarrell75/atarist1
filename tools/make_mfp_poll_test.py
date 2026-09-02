#!/usr/bin/env python3
# =============================================================================
#  make_mfp_poll_test.py — Génère une disquette .ST bootable qui POLLE le registre
#  de pending du MFP (IPRA) pendant qu'un Timer A tourne, et rend le résultat en
#  pixels. Étalon de la DATATION des registres MFP en mode bloc.
#
#  POURQUOI (chantier A2, second trou de couverture). L'inventaire porte depuis
#  longtemps : « pas de MFP_UpdateTimers avant lecture IPR/ISR/TBDR en mode bloc —
#  un timer expirant PENDANT l'instruction qui polle est vu en retard, jusqu'à
#  157 cycles mesurés ». Cet écart n'avait AUCUN étalon : il n'était exhibé que
#  par un comptage manuel sur Super Hang-On, un jeu commercial non redistribuable.
#  Ici il devient une image, sur ROM LIBRE.
#
#  ⚠ CE QUI EST VISÉ, ET CE QUI NE L'EST PAS. L'écart porte sur les registres de
#  PENDING (IPR/ISR), pas sur les data-registers : ceux-là sont déjà compensés par
#  `Mfp::readTimerData`, qui reconstruit le compteur vivant depuis `due − liveNow`.
#  L'IRQ elle-même n'est pas affectée non plus (antidatage + commit à la
#  frontière). C'est donc bien IPRA que la boucle lit.
#
#  LE MOTIF. Chaque ligne écran porte deux octets : IPRA relu, puis TADR. Le Timer
#  A tourne avec une période VOISINE de celle de la boucle : la phase dérive d'un
#  tour à l'autre (battement), si bien que l'expiration tombe successivement à
#  tous les endroits de l'instruction de poll. C'est ce battement qui rend l'étalon
#  sensible — une période très courte ou très longue donnerait un motif uniforme
#  qui ne contraindrait rien.
#
#  Chaque tour ACQUITTE le pending (`clr.b $fa0b`) : sans ça le bit resterait posé
#  après la première expiration et les 99 lignes suivantes seraient identiques.
#
#  Usage  : make_mfp_poll_test.py <out.st>
#  Oracle : run_etalons.py --oracle --only mfp_poll
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


out = sys.argv[1] if len(sys.argv) > 1 else "/tmp/mfp_poll_test.st"

SCREEN = 0x00020000
ITERS  = 100          # une ligne écran par tour de poll
BPL    = 160
# Timer A, mode DÉLAI, prescaler /4 (TACR=1), TDR=13 → 52 cycles MFP ≈ 169 cycles
# CPU, pour une boucle de poll d'environ 60 cycles : l'expiration se déplace donc
# d'un tour à l'autre au lieu de retomber toujours au même endroit.
TACR = 1
TADR_INIT = 13

bra_s('code')
while len(code) < 0x1E:
    code.append(0x00)

label('code')
# IRQ MASQUÉES (IPL 7) : l'étalon mesure l'ÉTAT DES REGISTRES — le bit pending d'IPRA
# et le compteur vivant TADR — et non la LIVRAISON de l'interruption. Ouvrir les IRQ y
# ferait tourner un handler Timer A toutes les ~169 cycles : la boucle de poll serrée,
# qui est tout l'instrument, serait détruite, et l'étalon mesurerait le dispatch.
# ⚠ COROLLAIRE À CONNAÎTRE AVANT DE LUI FAIRE DIRE TROP : cet étalon est par
# construction AVEUGLE à tout ce qui touche la datation ou la livraison des IRQ. Vérifié
# le 2026-09-02 — décaler de 40 cycles la datation des écritures registre
# (NEOST_MFP_WRITE_END=40) le laisse à 0 px, alors que le même décalage déplace
# 24 773 px sur Super Hang-On. C'est ce qui l'a fait rejeter à tort le port de
# MFP_UpdateTimers le matin du 2026-09-02 (cf. CHANGELOG).
w(0x46FC, 0x2700)                  # move.w #$2700,sr

# ---- vidéo : basse résolution, 50 Hz, base écran $020000 -------------------
w(0x4238, 0x8260)                  # clr.b $8260.w
w(0x11FC, 0x0002, 0x820A)          # move.b #2,$820a.w  → 50 Hz
w(0x11FC, 0x0002, 0x8201)          # move.b #2,$8201.w  → base $020000
w(0x4238, 0x8203)
w(0x4238, 0x820D)
w(0x4238, 0x820F)
w(0x4278, 0x8240)                  # palette[0] = noir
w(0x31FC, 0x0777, 0x8242)          # palette[1] = blanc

# ---- écran mis à zéro ------------------------------------------------------
w(0x207C); l(SCREEN)
w(0x303C, 1999)
label('cls')
for _ in range(4):
    w(0x4298)
dbra(0, 'cls')

# ---- Timer A en mode délai -------------------------------------------------
# IERA bit 5 armé : sans lui le MFP ne poserait JAMAIS le bit de pending, et
# l'étalon lirait des zéros — il ne contraindrait alors plus rien.
w(0x11FC, 0x0020, 0xFA07)          # move.b #$20,$fa07.w  → IERA, Timer A
w(0x11FC, 0x0020, 0xFA13)          # move.b #$20,$fa13.w  → IMRA, Timer A
                                   # ⚠ CORRECTION (2026-09-02) : ce commentaire affirmait
                                   # qu'avec IMRA=0 « le bit de pending ne se pose jamais
                                   # et IPRA lit 0 sur les 100 lignes ». C'est FAUX, et
                                   # vérifié : une variante à IMRA=0 rend les 100 octets
                                   # IPRA *et* TADR IDENTIQUES. Le bit pending ne dépend
                                   # que d'IER — `if (*pEnableReg & Bit) *pPendingReg |=
                                   # Bit` (MFP_InputOnChannel, mfp.c:1099-1114), dont
                                   # Mfp::raiseAt est le port 1:1 ; IMR ne gouverne que
                                   # l'élection de l'IRQ (Pending_Time_Min). IMRA reste
                                   # armé ici — le changer imposerait de re-poser la
                                   # référence pour un gain nul — mais il n'est PAS ce
                                   # qui rend l'étalon non vide.
w(0x11FC, TADR_INIT, 0xFA1F)       # move.b #13,$fa1f.w   → TADR
w(0x11FC, TACR, 0xFA19)            # move.b #1,$fa19.w    → TACR = /4 (démarre le timer)

# ---- boucle de poll --------------------------------------------------------
w(0x227C); l(SCREEN)               # movea.l #SCREEN,a1
w(0x3E3C, ITERS - 1)               # move.w #99,d7

label('loop')
w(0x1038, 0xFA0B)                  # move.b $fa0b.w,d0   → IPRA (le registre visé)
w(0xE148)                          # lsl.w #8,d0         → IPRA dans l'octet HAUT
w(0x1038, 0xFA1F)                  # move.b $fa1f.w,d0   → TADR dans l'octet bas (move.b
                                   #                       préserve le haut) : d0 = IPRA:TADR
w(0x4238, 0xFA0B)                  # clr.b $fa0b.w       → acquitte le pending (cf. en-tête)
# Le mot IPRA:TADR est RÉPLIQUÉ sur 8 groupes de 16 px (offsets 0, 8, 16 … 56 : en
# basse résolution, les mots du plan 0 sont espacés de 8 octets).
# ⚠ Ce n'est pas cosmétique : avec un seul mot par ligne, l'image ne portait que
# 16 px sur 114816 et le contrôle de provenance la REJETAIT — « référence QUASI
# UNIFORME, elle ne valide aucun pixel ». Répliquer n'ajoute aucune contrainte
# nouvelle, mais rend la référence recevable ET lisible à l'œil. L'alternative
# (poser uniform_ok) aurait désactivé un garde-fou utile.
w(0x2049)                          # movea.l a1,a0
w(0x7207)                          # moveq #7,d1
label('rep')
w(0x3080)                          # move.w d0,(a0)
w(0x41E8, 0x0008)                  # lea 8(a0),a0
dbra(1, 'rep')
w(0x43E9, BPL)                     # lea 160(a1),a1
dbra(7, 'loop')

label('halt')
bra_s('halt')

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
print(f"écrit {out} ({len(img)} o) ; {ITERS} tours de poll IPRA ; "
      f"Timer A /{ (0,4,10,16,50,64,100,200)[TACR] } TDR={TADR_INIT} ; code={len(code)} o")
