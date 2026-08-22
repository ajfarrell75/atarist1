#!/usr/bin/env python3
# =============================================================================
#  make_net_test.py — Démonstration « NeoST parle à Internet » : un programme
#  68000 qui compose un numéro sur le MODEM HAYES émulé (--modem : les commandes
#  AT ouvrent de vraies connexions TCP côté hôte) et envoie une requête HTTP à la
#  main, en affichant tout ce qui revient à l'écran de l'ST.
#
#  C'est le chemin qui marche SANS pile TCP/IP côté ST : le modem est un pont
#  transparent octets ↔ socket, donc un simple terminal suffit — exactement ce
#  que faisaient les BBS, et ce que fait UNITERM.PRG interactivement.
#  (Le NetUSBee, lui, exige une pile TCP/IP sur l'ST — STinG — ET un backend
#  réseau réel côté NeoST : cf. TODO.md « EtherNEC — backend réel ».)
#
#  Tout passe par le BIOS (trap #13) : Bconout/Bconstat/Bconin sur le port AUX
#  (device 1 = RS-232) et la console (device 2 = écran). Aucun accès direct au
#  MFP : le TOS a déjà configuré l'USART à 9600 8N1 au boot.
#
#  L'attente est comptée en TICS DE 200 Hz (_hz_200, $4BA) et non en boucles
#  vides : en headless le CPU émulé tourne bien plus vite que le temps réel, et
#  c'est le temps ÉMULÉ qui décide du nombre de trames — donc du nombre de fois
#  où le frontend pompe la socket. Une boucle de comptage aurait expiré avant
#  que l'hôte n'ait fini son connect().
#
#  Usage : python3 tools/make_net_test.py OUT.st [hôte] [chemin]
#          (défaut : theoldnet.com / "/")  → disquette avec AUTO\NETTEST.PRG
#
#  68000 big-endian. (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
from __future__ import annotations

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_usatan_test import Asm, build_floppy   # noqa: E402

HZ200 = 0x000004BA          # compteur 200 Hz du TOS (temps ÉMULÉ)
AUX, CON = 1, 2             # devices BIOS : 1 = RS-232, 2 = écran


def build_code(host: str, path: str) -> Asm:
    a = Asm(0)

    # --- helpers ------------------------------------------------------------
    # bconout(d1 = caractère, d2 = device)
    def bconout():
        a.w(0x3F01)                                  # move.w d1,-(sp)   caractère
        a.w(0x3F02)                                  # move.w d2,-(sp)   device
        a.w(0x3F3C, 0x0003)                          # move.w #3,-(sp)   Bconout
        a.w(0x4E4D)                                  # trap #13
        a.w(0xDEFC, 0x0006)                          # adda.w #6,sp

    # a3 = chaîne 0-terminée, d2 = device → tout envoyer
    a.lbl('puts')
    a.w(0x121B)                                      # move.b (a3)+,d1
    a.br(0x6700, 'puts_ret')                         # beq ret
    a.w(0x4881)                                      # ext.w d1
    bconout()
    a.br(0x6000, 'puts')
    a.lbl('puts_ret'); a.w(0x4E75)

    # wait_echo : recopie AUX → écran pendant d6 tics de 200 Hz
    a.lbl('wait_echo')
    a.w(0x2A39); a.l32(HZ200)                        # move.l $4BA,d5
    a.w(0xDA86)                                      # add.l d6,d5        échéance
    a.lbl('we_loop')
    a.w(0x3F3C, 0x0001)                              # move.w #1,-(sp)    AUX
    a.w(0x3F3C, 0x0001)                              # move.w #1,-(sp)    Bconstat
    a.w(0x4E4D)                                      # trap #13
    a.w(0x588F)                                      # addq.l #4,sp   (deux mots empilés)
    a.w(0x4A40)                                      # tst.w d0
    a.br(0x6700, 'we_tick')                          # rien à lire
    a.w(0x3F3C, 0x0001)                              # move.w #1,-(sp)    AUX
    a.w(0x3F3C, 0x0002)                              # move.w #2,-(sp)    Bconin
    a.w(0x4E4D)                                      # trap #13
    a.w(0x588F)                                      # addq.l #4,sp   (deux mots empilés)
    a.w(0x3200)                                      # move.w d0,d1       caractère reçu
    a.w(0x0241, 0x00FF)                              # andi.w #$FF,d1
    # Plafond d'affichage (d4) : on veut voir le DÉBUT de la réponse (en-têtes
    # HTTP + premières lignes), pas la fin après défilement. Au-delà, on continue
    # de vider la file série sans imprimer.
    a.w(0x0C84); a.l32(760)                          # cmpi.l #760,d4
    a.br(0x6400, 'we_tick')                          # bcc : assez affiché
    a.w(0x5284)                                      # addq.l #1,d4
    a.w(0x343C, 0x0002)                              # move.w #2,d2       écran
    bconout()
    # Repli de ligne à 78 colonnes (d3) : la console ST n'enroule pas, elle ÉCRASE
    # la 80ᵉ colonne — sans ça, une ligne HTML longue ne laissait voir que son
    # dernier caractère (une colonne de « > » sur le bord droit).
    a.w(0x0C41, 0x000D)                              # cmpi.w #13,d1      CR ?
    a.br(0x6600, 'we_lf')
    a.w(0x4283)                                      # clr.l d3           colonne = 0
    a.br(0x6000, 'we_tick')
    # LF SEUL (le HTML n'a pas toujours de CR) : la console ST descend d'une ligne
    # sans revenir à gauche — le texte partait alors en escalier. On ajoute le CR.
    a.lbl('we_lf')
    a.w(0x0C41, 0x000A)                              # cmpi.w #10,d1      LF ?
    a.br(0x6600, 'we_col')
    a.w(0x323C, 0x000D)                              # move.w #13,d1      CR
    a.w(0x343C, 0x0002)
    bconout()
    a.w(0x4283)                                      # clr.l d3
    a.br(0x6000, 'we_tick')
    a.lbl('we_col')
    a.w(0x5283)                                      # addq.l #1,d3
    a.w(0x0C83); a.l32(78)                           # cmpi.l #78,d3
    a.br(0x6500, 'we_tick')                          # bcs : encore de la place
    a.w(0x323C, 0x000D); a.w(0x343C, 0x0002); 
    bconout()
    a.w(0x323C, 0x000A); a.w(0x343C, 0x0002); 
    bconout()
    a.w(0x4283)                                      # clr.l d3
    a.lbl('we_tick')
    a.w(0x2039); a.l32(HZ200)                        # move.l $4BA,d0
    a.w(0xB085)                                      # cmp.l d5,d0
    a.br(0x6500, 'we_loop')                          # bcs : pas encore l'heure
    a.w(0x4E75)

    # --- programme ----------------------------------------------------------
    a.lbl('start')
    # Super(0) : le compteur 200 Hz ($4BA) vit dans la mémoire BASSE, protégée sur
    # ST — y toucher en mode utilisateur lève un bus error (constaté : « Panic: Bus
    # Error addr=000004ba sr=0300 » sous EmuTOS, bombes fugaces sous TOS 1.04).
    # Un PRG démarre en mode utilisateur : on passe superviseur pour toute la durée.
    a.w(0x42A7)                                      # clr.l -(sp)
    a.w(0x3F3C, 0x0020)                              # move.w #$20,-(sp)  Super
    a.w(0x4E41)                                      # trap #1
    a.w(0x5C8F)                                      # addq.l #6,sp
    a.w(0x4283)                                      # clr.l d3   colonne courante
    a.w(0x4284)                                      # clr.l d4   caractères affichés
    a.lea_lbl('s_banner', 3); a.w(0x343C, 0x0002); a.br(0x6100, 'puts')   # bannière écran
    a.lea_lbl('s_dial', 3);   a.w(0x343C, 0x0002); a.br(0x6100, 'puts')   # « composition… »
    a.lea_lbl('s_atdt', 3);   a.w(0x343C, 0x0001); a.br(0x6100, 'puts')   # ATDT → modem
    a.w(0x2C3C); a.l32(1000); a.br(0x6100, 'wait_echo')                   # 5 s : CONNECT
    a.lea_lbl('s_get', 3);    a.w(0x343C, 0x0001); a.br(0x6100, 'puts')   # requête HTTP
    a.w(0x2C3C); a.l32(4000); a.br(0x6100, 'wait_echo')                   # 20 s : réponse
    a.lea_lbl('s_done', 3);   a.w(0x343C, 0x0002); a.br(0x6100, 'puts')
    a.lbl('freeze')
    a.br(0x6000, 'freeze')                           # boucle : l'écran reste affiché pour la capture
                                                     # (stop serait PRIVILÉGIÉ — un PRG est en mode utilisateur)

    # --- données ------------------------------------------------------------
    a.string('s_banner', '\r\n--- NeoST: dialing the Internet through the Hayes modem ---\r\n')
    a.string('s_dial',   'ATDT %s:80\r\n' % host)
    a.string('s_atdt',   'ATDT %s:80\r' % host)
    a.string('s_get',    'GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: NeoST/AtariST\r\n\r\n' % (path, host))
    a.string('s_done',   '\r\n--- end ---\r\n')
    return a


def build_prg(host: str, path: str) -> bytes:
    a = build_code(host, path)
    # L'exécution commence au PREMIER octet du TEXT : 'puts'/'wait_echo' sont des
    # sous-programmes, il faut donc sauter par-dessus. On préfixe un bra vers 'start'.
    pre = Asm(0)
    pre.br(0x6000, 'start')
    merged = Asm(0)
    merged.items = pre.items + a.items
    text = merged.assemble()
    reloc = bytearray()
    if merged.relocs:
        reloc += struct.pack('>I', merged.relocs[0])
        prev = merged.relocs[0]
        for r in merged.relocs[1:]:
            d = r - prev
            while d > 254:
                reloc.append(1); d -= 254
            reloc.append(d); prev = r
    reloc.append(0)
    hdr = struct.pack('>HIIIIIIH', 0x601A, len(text), 0, 0, 0, 0, 0, 0)
    return hdr + text + bytes(reloc)


def main() -> int:
    if len(sys.argv) < 2:
        print("usage: make_net_test.py OUT.st [hôte] [chemin]", file=sys.stderr)
        return 2
    host = sys.argv[2] if len(sys.argv) > 2 else "theoldnet.com"
    path = sys.argv[3] if len(sys.argv) > 3 else "/"
    prg = build_prg(host, path)
    data = build_floppy(prg, 'NETTEST') if sys.argv[1].lower().endswith('.st') else prg
    with open(sys.argv[1], 'wb') as f:
        f.write(data)
    print(f"NETTEST ({host}{path}) -> {sys.argv[1]} ({len(prg)} octets de PRG)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
