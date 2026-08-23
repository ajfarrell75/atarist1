#!/usr/bin/env python3
# =============================================================================
#  midi_simplify.py — Ramène des Standard MIDI Files modernes à ce qu'un
#  séquenceur Atari ST de 1990 (Cubase Lite, Pro-24…) sait avaler.
#
#  Ces logiciels plantent sur des fichiers pourtant valides. Mesuré sur un lot de
#  32 fichiers du domaine « blues/boogie » (tools/midi_scan dans l'historique) :
#    · format 1 avec jusqu'à 41 PISTES — un Lite en gère une poignée ;
#    · division 480 PPQN (valeur de DAW moderne) là où Cubase ST travaille en 384 ;
#    · 1 528 méta-événements de texte, paroles, marqueurs, plus 173 « port » (0x21)
#      et 187 « spécifique constructeur » (0x7F) — 0x21 est POSTÉRIEUR à ces
#      séquenceurs, et tout ce fatras occupe la mémoire d'une machine de 1 Mo ;
#    · des blocs SysEx, souvent mal digérés ;
#    · des noms de fichiers hors 8.3, que le GEMDOS ne sait pas montrer.
#
#  Ce que produit ce script, pour chaque fichier :
#    · SMF **format 0** — une seule piste : le cas le plus universellement lu ;
#    · division **96 PPQN** par défaut (valeur d'époque), rééchelonnée sur les temps
#      ABSOLUS puis re-différenciée, pour ne pas accumuler l'erreur d'arrondi ;
#    · méta-événements réduits au strict utile : tempo (0x51), mesure (0x58),
#      armure (0x59) au tick 0 seulement (plus loin, Cubase Lite perd la piste de
#      notes), fin de piste (0x2F). Tout le reste saute ;
#    · plus aucun SysEx ;
#    · statut courant (running status) conservé : c'est de la norme d'origine, et
#      ça divise la taille par ~1,5 — or la mémoire est le suspect n°1 ;
#    · nom de sortie en **8.3 majuscule**, unique, avec un index NOMS.TXT lisible
#      depuis le ST pour retrouver le titre complet.
#
#  Les ORIGINAUX ne sont jamais modifiés : la sortie va dans un autre dossier.
#
#  Usage : python3 tools/midi_simplify.py SRC_DIR OUT_DIR [--ppqn 96]
#
#  (c) 2026 VERHILLE Arnaud — projet NeoST.
# =============================================================================
from __future__ import annotations

import argparse
import os
import struct
import sys

# Méta-événements CONSERVÉS. Tout ce qui n'est pas là est retiré : textes, paroles,
# marqueurs, nom d'instrument, copyright, canal préférentiel, port, SMPTE offset,
# spécifique constructeur. Aucun n'est nécessaire à la lecture des notes.
META_KEEP = {0x51, 0x58, 0x59}          # tempo, mesure, armure (0x2F ajouté à la fin)


def rd_vlq(b: bytes, i: int) -> tuple[int, int]:
    v = 0
    while True:
        c = b[i]; i += 1
        v = (v << 7) | (c & 0x7F)
        if not c & 0x80:
            return v, i


def wr_vlq(v: int) -> bytes:
    if v < 0:
        v = 0
    out = bytearray([v & 0x7F])
    v >>= 7
    while v:
        out.append((v & 0x7F) | 0x80)
        v >>= 7
    return bytes(reversed(out))


def parse(data: bytes):
    """(division, [(temps_absolu, ordre, octets_de_l_événement)]) — SysEx exclus."""
    if data[:4] != b'MThd':
        raise ValueError("pas un MThd")
    hlen = struct.unpack('>I', data[4:8])[0]
    _fmt, _ntrks, div = struct.unpack('>HHh', data[8:14])
    if div <= 0:
        raise ValueError("division SMPTE non gérée")
    events, order, i = [], 0, 8 + hlen
    while i < len(data) - 8:
        if data[i:i + 4] != b'MTrk':
            break
        tlen = struct.unpack('>I', data[i + 4:i + 8])[0]
        end = min(i + 8 + tlen, len(data))
        j, t, status = i + 8, 0, 0
        while j < end:
            dt, j = rd_vlq(data, j); t += dt
            b0 = data[j]
            if b0 == 0xFF:                                   # méta
                mt = data[j + 1]
                ln, k = rd_vlq(data, j + 2)
                # Armure (0x59) : gardée au tick 0 SEULEMENT. Mesuré sur Cubase Lite
                # (Mozart K.331 « Alla turca », modulations) : une armure en cours de
                # morceau fait jeter la piste de notes entière à l'import — il ne reste
                # que la piste de tempo. Inoffensif pour la lecture.
                if mt in META_KEEP and not (mt == 0x59 and t > 0):
                    events.append((t, order, bytes([0xFF, mt]) + wr_vlq(ln) + data[k:k + ln]))
                    order += 1
                j = k + ln
            elif b0 in (0xF0, 0xF7):                         # SysEx : jeté
                ln, k = rd_vlq(data, j + 1)
                j = k + ln
            else:                                            # message de canal
                if b0 & 0x80:
                    status = b0; j += 1
                hi = status & 0xF0
                n = 1 if hi in (0xC0, 0xD0) else 2
                events.append((t, order, bytes([status]) + data[j:j + n]))
                order += 1
                j += n
        i = end
    return div, events


def detach_repeats(scaled):
    """Rend le flux de notes « monophonique par hauteur », comme le jouait un pianiste.

    Mesuré sur Cubase Lite (tools/run_midi_sequencer.py) sur des partitions à
    plusieurs voix (Scarlatti K.514, Chopin op. 28 n° 15) :
      · deux note-on de MÊME hauteur au même tick (doublure à l'unisson entre deux
        voix) → Cubase émet un note-off 2 ms après, la note est mangée ;
      · un note-off qui tombe PILE sur le note-on suivant de même hauteur → Cubase
        émet le note-on d'abord, le note-off tue la nouvelle note (2 ms).
    Un vrai pianiste ne peut ni doubler une touche ni la rejouer sans la lâcher : on
    apparie on/off par (canal, hauteur), on FUSIONNE les unissons (vélocité max, fin
    la plus tardive), on TRONQUE un chevauchement au tick précédant la reprise, et on
    SÉPARE d'un tick une note qui finirait sur la suivante. Les autres événements
    (pédale, tempo…) ne bougent pas. Entrée/sortie : (tick, ordre, événement) triés.
    """
    def is_on(ev):  return (ev[0] & 0xF0) == 0x90 and len(ev) > 2 and ev[2] > 0
    def is_off(ev): return (ev[0] & 0xF0) == 0x80 or ((ev[0] & 0xF0) == 0x90 and len(ev) > 2 and ev[2] == 0)
    others, notes, open_ = [], [], {}          # notes : [t_on, t_off, on_ev, off_ev]
    for t, o, ev in scaled:
        if is_on(ev):
            key = (ev[0] & 0x0F, ev[1])
            n = [t, None, ev, None, o]
            open_.setdefault(key, []).append(n); notes.append(n)
        elif is_off(ev):
            key = (ev[0] & 0x0F, ev[1])
            q = open_.get(key)
            if q:
                n = q.pop(0); n[1] = t; n[3] = ev       # FIFO : le plus ancien d'abord
            # note-off orphelin : jeté (rien à éteindre)
        else:
            others.append((t, o, ev))
    for n in notes:                                     # note jamais fermée : 1 tick
        if n[1] is None:
            n[1] = n[0] + 1; n[3] = bytes([0x80 | (n[2][0] & 0x0F), n[2][1], 0x40])
    by_key: dict = {}
    for n in notes:
        by_key.setdefault((n[2][0] & 0x0F, n[2][1]), []).append(n)
    kept = []
    for key, lst in by_key.items():
        lst.sort(key=lambda n: (n[0], n[4]))
        out = []
        for n in lst:
            if out and n[0] == out[-1][0]:              # unisson : fusion
                c = out[-1]
                c[1] = max(c[1], n[1])
                if n[2][2] > c[2][2]:
                    c[2] = n[2]
                continue
            if out and n[0] <= out[-1][1]:              # chevauche ou touche : tronque
                out[-1][1] = max(out[-1][0] + 1, n[0] - 1)
            out.append(n)
        kept += out
    result = list(others)
    for n in kept:
        result.append((n[0], n[4], n[2]))
        result.append((n[1], n[4], n[3]))
    def rank(ev): return 0 if ev[0] == 0xFF else 1 if is_off(ev) else 2
    return sorted(result, key=lambda x: (x[0], rank(x[2]), x[1]))


MAX_BPM = 250   # plafond de tempo de Cubase Lite (au-delà il reste à 120 : morceau 2,5× trop lent)


def clamp_tempo(div_in: int, events):
    """Ramène tout tempo > MAX_BPM dans la plage du séquenceur en doublant les durées.

    Mesuré sur Cubase Lite (Mozart K.457 III, *MM300 dans la partition) : un tempo
    au-delà de ~250 bpm n'est pas importé, le morceau se joue au 120 par défaut (pente
    2,5 à l'étalon). Remède sans rien changer à la musique : µs/noire ×2 et durées en
    ticks ÷2 (l'ancienne noire devient une croche) — sur TOUT le fichier, par cohérence.
    """
    bpm_max = 0.0
    for _t, _o, ev in events:
        if ev[0] == 0xFF and ev[1] == 0x51:
            us = int.from_bytes(ev[-3:], 'big') or 500000
            bpm_max = max(bpm_max, 60e6 / us)
    k = 1
    while bpm_max / k > MAX_BPM:
        k *= 2
    if k == 1:
        return div_in, events
    out = []
    for t, o, ev in events:
        if ev[0] == 0xFF and ev[1] == 0x51:
            us = min(int.from_bytes(ev[-3:], 'big') * k, 0xFFFFFF)
            ev = ev[:-3] + us.to_bytes(3, 'big')
        out.append((t, o, ev))
    # µs/noire ×k ⇒ pour garder les mêmes durées réelles, chaque événement doit
    # tomber k fois plus tôt en ticks : division ×k (le rééchelonnage aval fait
    # t * ppqn / div_in). L'ancienne noire devient une croche — musique inchangée.
    return div_in * k, out


def build(div_in: int, events, ppqn: int, detach: bool = False) -> bytes:
    """SMF format 0, division `ppqn`, statut courant, une seule piste."""
    # Rééchelonnage sur les temps ABSOLUS : le faire sur les deltas ferait dériver le
    # morceau, chaque arrondi s'ajoutant au précédent.
    div_in, events = clamp_tempo(div_in, events)
    scaled = sorted((int(round(t * ppqn / div_in)), o, ev) for t, o, ev in events)
    if detach:
        scaled = detach_repeats(scaled)
    trk, prev_t, running = bytearray(), 0, None
    for t, _o, ev in scaled:
        trk += wr_vlq(t - prev_t); prev_t = t
        if ev[0] == 0xFF:
            trk += ev; running = None                        # un méta casse le statut courant
        elif ev[0] == running:
            trk += ev[1:]                                    # statut courant : octet économisé
        else:
            trk += ev; running = ev[0]
    trk += wr_vlq(0) + b'\xFF\x2F\x00'                        # fin de piste
    return (b'MThd' + struct.pack('>IHHh', 6, 0, 1, ppqn)
            + b'MTrk' + struct.pack('>I', len(trk)) + bytes(trk))


def build_per_channel(div_in: int, events, ppqn: int, detach: bool = False) -> bytes:
    """SMF format 1, une piste PAR CANAL MIDI (+ une piste de tempo).

    Pourquoi cette variante : le format 0 fusionne tout, donc les statuts alternent et
    le statut courant ne compresse plus rien — mesuré, cinq fichiers GROSSISSAIENT
    (HONKYTON : 66 -> 73 ko), alors que la charge utile est à 80 % des Note-On, c'est-
    à-dire précisément ce que le statut courant sait comprimer. Une piste par canal
    rend chaque piste homogène : on retrouve la taille d'origine, et le morceau reste
    lisible par instrument. Le nombre de pistes reste borné par 16 canaux + 1.
    """
    div_in, events = clamp_tempo(div_in, events)
    scaled = sorted((int(round(t * ppqn / div_in)), o, ev) for t, o, ev in events)
    if detach:
        scaled = detach_repeats(scaled)
    metas = [(t, ev) for t, _o, ev in scaled if ev[0] == 0xFF]
    by_ch: dict[int, list] = {}
    for t, _o, ev in scaled:
        if ev[0] != 0xFF:
            by_ch.setdefault(ev[0] & 0x0F, []).append((t, ev))

    def mtrk(items, with_running: bool) -> bytes:
        trk, prev, running = bytearray(), 0, None
        for t, ev in items:
            trk += wr_vlq(t - prev); prev = t
            if not with_running or ev[0] == 0xFF:
                trk += ev; running = None
            elif ev[0] == running:
                trk += ev[1:]
            else:
                trk += ev; running = ev[0]
        trk += wr_vlq(0) + b'\xFF\x2F\x00'
        return b'MTrk' + struct.pack('>I', len(trk)) + bytes(trk)

    chunks = [mtrk(metas, False)] + [mtrk(by_ch[c], True) for c in sorted(by_ch)]
    return (b'MThd' + struct.pack('>IHHh', 6, 1, len(chunks), ppqn) + b''.join(chunks))


def short_name(stem: str, taken: set[str]) -> str:
    """Nom 8.3 majuscule, unique — le GEMDOS ne montre rien d'autre.

    Un « préfixe explicite » — la partie avant le premier `_`, si elle tient en 8
    caractères alphanumériques — devient le nom ST tel quel : `wtc1p01_bwv846.mid` →
    `WTC1P01.MID` (sans lui, le rognage à 8 caractères donnerait `WTC1P01B`). Le reste
    du nom long ne sert qu'à l'index NOMS.TXT.
    """
    head = stem.split('_', 1)[0] if '_' in stem else ''
    head = ''.join(c for c in head.upper() if c.isalnum())
    base = head if 0 < len(head) <= 8 else (''.join(c for c in stem.upper() if c.isalnum())[:8] or 'MIDI')
    name = base
    n = 1
    while name + '.MID' in taken:
        suffix = str(n); name = base[:8 - len(suffix)] + suffix; n += 1
    taken.add(name + '.MID')
    return name + '.MID'


def main() -> int:
    ap = argparse.ArgumentParser(description="SMF modernes → SMF lisibles par un séquenceur ST")
    ap.add_argument('src'); ap.add_argument('out')
    ap.add_argument('--ppqn', type=int, default=96, help="division de sortie (défaut 96)")
    ap.add_argument('--per-channel', action='store_true',
                    help="format 1 avec une piste par canal (plus compact et plus lisible "
                         "par instrument) au lieu du format 0 fusionné (plus universel)")
    ap.add_argument('--detach', action='store_true',
                    help="corpus piano : fusionne les unissons, tronque les chevauchements "
                         "de même hauteur, sépare d'un tick les notes répétées — sinon "
                         "Cubase coupe ces notes au bout de 2 ms")
    a = ap.parse_args()
    if not os.path.isdir(a.src):
        sys.stderr.write(f"ERREUR: {a.src} n'est pas un dossier\n"); return 2
    os.makedirs(a.out, exist_ok=True)

    taken: set[str] = set()
    index, tot_in, tot_out, fails = [], 0, 0, 0
    for f in sorted(os.listdir(a.src)):
        if not f.lower().endswith(('.mid', '.midi')):
            continue
        src = os.path.join(a.src, f)
        try:
            div, ev = parse(open(src, 'rb').read())
            blob = (build_per_channel(div, ev, a.ppqn, a.detach) if a.per_channel
                    else build(div, ev, a.ppqn, a.detach))
        except Exception as e:                                # fichier illisible : on le DIT
            sys.stderr.write(f"  ✗ {f} : {e}\n"); fails += 1; continue
        dst_name = short_name(os.path.splitext(f)[0], taken)
        open(os.path.join(a.out, dst_name), 'wb').write(blob)
        sz_in = os.path.getsize(src); tot_in += sz_in; tot_out += len(blob)
        index.append((dst_name, f, sz_in, len(blob)))
        print(f"  {dst_name:12} ← {f:34} {sz_in // 1024:3} ko → {len(blob) // 1024:3} ko")

    # Index lisible DEPUIS le ST (CRLF, 8.3) : sans lui, « BLUESFOR.MID » ne dit plus rien.
    with open(os.path.join(a.out, 'NOMS.TXT'), 'w', newline='\r\n') as fh:
        fh.write("Fichiers MIDI simplifies pour sequenceur ST\r\n")
        fh.write(f"format {1 if a.per_channel else 0}, {a.ppqn} PPQN, "
                 f"sans SysEx ni textes\r\n\r\n")
        for s, orig, _i, _o in index:
            fh.write(f"{s:12} {orig}\r\n")

    print(f"\n{len(index)} fichier(s) écrits dans {a.out} "
          f"({tot_in // 1024} ko → {tot_out // 1024} ko)"
          + (f", {fails} échec(s)" if fails else ""))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
