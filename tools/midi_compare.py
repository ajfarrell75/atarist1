#!/usr/bin/env python3
"""
Compare ce qu'un séquenceur ST a RÉELLEMENT émis (journal `neost-headless --midi-dump`)
au morceau qu'on lui a donné (SMF), ou convertit ce journal en SMF pour l'ouvrir ailleurs.

    midi_compare.py LOG SRC.mid [--tol-ms 5] [--min-notes N]     → verdict PASS/FAIL
    midi_compare.py LOG --to-smf OUT.mid                          → SMF format 0 (1 tick = 0,1 ms)

Le journal est du texte `<cycle 68000> <octet hex>` (8 021 248 cycles/s). On le
reparse en messages (statut courant, SysEx, temps réel), on en tire les notes
(note-on vélocité > 0 … note-off ou note-on vélocité 0) et on compare à celles du
SMF source, dont les ticks sont convertis en secondes via la carte de tempo.

Ce que le verdict vérifie — et pourquoi :
  · même multiset (canal, hauteur) de notes, dans le même ORDRE de départ : une note
    manquante ou en trop = le séquenceur n'a pas tout lu (ou le convertisseur a cassé
    quelque chose) ;
  · vélocités identiques : Pianoteq s'en sert, un ST qui les raboterait s'entendrait ;
  · durées à `--dur-tol-ms` près (plus large : Cubase avance ses note-off d'un tick) ;
  · dérive de tempo : régression linéaire des départs émis sur les départs attendus
    → pente (1,0 = tempo exact) et gigue (écart-type des résidus). Cubase pilote son
    horloge par le Timer A du MFP : une pente fausse = MFP ou ACIA mal cadencés ;
  · pédale (CC64) : mêmes transitions, dans l'ordre.

Le départ absolu est libre (Play a été pressé « quand on a pu ») : on aligne sur la
première note.
"""
import argparse
import re
import struct
import sys

# Horloge CPU du ST. Le dump headless l'ANNONCE dans son en-tête (« cpu_hz=… ») et
# read_log la lit de là : cette valeur n'est qu'un repli pour un fichier sans en-tête.
# Elle ne doit donc jamais diverger de neost::pacing::kCpuHzInt (src/core/Pacing.hpp),
# mais elle ne peut pas non plus l'inclure — c'est du Python.
CPU_HZ = 8021248.0


# ---------------------------------------------------------------------------
#  Journal headless → messages datés
# ---------------------------------------------------------------------------
def read_log(path: str):
    """[(secondes, octets du message)] — canal, SysEx complets, temps réel."""
    msgs, status, pending, t0, sysex = [], 0, bytearray(), 0.0, None
    need = 0
    hz = CPU_HZ
    with open(path, encoding='utf-8', errors='replace') as fh:
        for ln in fh:
            if not ln.strip() or ln[0] == '#':
                # Le producteur date ses octets en CYCLES : c'est son horloge qui
                # convertit, pas la nôtre. Sans ça, changer kCpuHzInt d'un côté
                # décalait silencieusement toutes les mesures de tempo de l'autre.
                m = re.search(r"cpu_hz=(\d+)", ln)
                if m:
                    hz = float(m.group(1))
                continue
            cyc, hx = ln.split()
            t, b = int(cyc) / hz, int(hx, 16)
            if b >= 0xF8:                                   # temps réel : hors-bande
                msgs.append((t, bytes([b])))
                continue
            if sysex is not None:
                sysex.append(b)
                if b == 0xF7:
                    msgs.append((t0, bytes(sysex))); sysex = None
                continue
            if b == 0xF0:
                sysex, t0 = bytearray([b]), t
                continue
            if b & 0x80:
                status, pending, t0 = b, bytearray([b]), t
                hi = b & 0xF0
                need = 1 if hi in (0xC0, 0xD0) else 2 if hi < 0xF0 else {0xF1: 1, 0xF2: 2, 0xF3: 1}.get(b, 0)
                if need == 0:
                    msgs.append((t, bytes(pending))); pending = bytearray()
                continue
            if not pending:                                  # statut courant
                if not status:
                    continue
                pending, t0 = bytearray([status]), t
            pending.append(b)
            if len(pending) == 1 + need:
                msgs.append((t0, bytes(pending))); pending = bytearray()
    return msgs


# ---------------------------------------------------------------------------
#  SMF → messages datés (secondes), carte de tempo appliquée
# ---------------------------------------------------------------------------
def rd_vlq(b: bytes, i: int):
    v = 0
    while True:
        c = b[i]; i += 1
        v = (v << 7) | (c & 0x7F)
        if not c & 0x80:
            return v, i


def read_smf(path: str):
    data = open(path, 'rb').read()
    if data[:4] != b'MThd':
        raise ValueError(f"{path}: pas un MThd")
    hlen = struct.unpack('>I', data[4:8])[0]
    _fmt, _n, div = struct.unpack('>HHh', data[8:14])
    if div <= 0:
        raise ValueError("division SMPTE non gérée")
    raw, tempos, i = [], [], 8 + hlen            # raw: (tick, ordre, octets)
    order = 0
    while i + 8 <= len(data) and data[i:i + 4] == b'MTrk':
        tlen = struct.unpack('>I', data[i + 4:i + 8])[0]
        end, j, t, status = min(i + 8 + tlen, len(data)), i + 8, 0, 0
        while j < end:
            dt, j = rd_vlq(data, j); t += dt
            b0 = data[j]
            if b0 == 0xFF:
                mt = data[j + 1]; ln, k = rd_vlq(data, j + 2)
                if mt == 0x51:
                    tempos.append((t, int.from_bytes(data[k:k + 3], 'big')))
                j = k + ln
            elif b0 in (0xF0, 0xF7):
                ln, k = rd_vlq(data, j + 1); j = k + ln
            else:
                if b0 & 0x80:
                    status = b0; j += 1
                n = 1 if (status & 0xF0) in (0xC0, 0xD0) else 2
                raw.append((t, order, bytes([status]) + data[j:j + n])); order += 1
                j += n
        i = end
    raw.sort()
    # ticks → secondes, tempo par morceaux (défaut 120 bpm = 500 000 µs/noire)
    tempos.sort()
    out, cur_tick, cur_sec, cur_us, ti = [], 0, 0.0, 500000, 0
    for tick, _o, msg in raw:
        while ti < len(tempos) and tempos[ti][0] <= tick:
            cur_sec += (tempos[ti][0] - cur_tick) * cur_us / 1e6 / div
            cur_tick, cur_us = tempos[ti]; ti += 1
        out.append((cur_sec + (tick - cur_tick) * cur_us / 1e6 / div, msg))
    return out


# ---------------------------------------------------------------------------
#  Messages → notes / pédale
# ---------------------------------------------------------------------------
def notes_of(msgs):
    """[(t_on, canal, note, vel, durée)] dans l'ordre des note-on ; CC64 à part."""
    notes, open_, pedal = [], {}, []
    for t, m in msgs:
        hi, ch = m[0] & 0xF0, m[0] & 0x0F
        if hi == 0x90 and m[2] > 0:
            key = (ch, m[1])
            if key in open_:                                  # re-déclenchée : clôt l'ancienne
                idx = open_.pop(key); notes[idx][4] = t - notes[idx][0]
            open_[key] = len(notes); notes.append([t, ch, m[1], m[2], None])
        elif hi == 0x80 or (hi == 0x90 and m[2] == 0):
            idx = open_.pop((ch, m[1]), None)
            if idx is not None:
                notes[idx][4] = t - notes[idx][0]
        elif hi == 0xB0 and m[1] == 64:
            pedal.append((t, ch, 1 if m[2] >= 64 else 0))
    return [tuple(n) for n in notes], pedal


# ---------------------------------------------------------------------------
#  Journal → SMF format 0 (1 tick = 0,1 ms : tempo 1 s/noire, 10 000 ticks/noire)
# ---------------------------------------------------------------------------
def wr_vlq(v: int) -> bytes:
    out = bytearray([v & 0x7F]); v >>= 7
    while v:
        out.append((v & 0x7F) | 0x80); v >>= 7
    return bytes(reversed(out))


def to_smf(msgs, path: str):
    trk = bytearray(b'\x00\xFF\x51\x03' + (1000000).to_bytes(3, 'big'))
    last = 0
    for t, m in msgs:
        if m[0] >= 0xF8:
            continue                                          # temps réel : pas dans un SMF
        tick = int(round(t * 10000))
        trk += wr_vlq(tick - last); last = tick
        if m[0] == 0xF0:
            trk += b'\xF0' + wr_vlq(len(m) - 1) + m[1:]
        else:
            trk += m
    trk += b'\x00\xFF\x2F\x00'
    with open(path, 'wb') as fh:
        fh.write(b'MThd' + struct.pack('>IHHH', 6, 0, 1, 10000))
        fh.write(b'MTrk' + struct.pack('>I', len(trk)) + trk)


# ---------------------------------------------------------------------------
#  Verdict
# ---------------------------------------------------------------------------
def linfit(xs, ys):
    n = len(xs)
    if n < 2:
        return 1.0, 0.0, 0.0
    mx, my = sum(xs) / n, sum(ys) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    slope = sum((x - mx) * (y - my) for x, y in zip(xs, ys)) / sxx if sxx else 1.0
    icpt = my - slope * mx
    res = [y - (slope * x + icpt) for x, y in zip(xs, ys)]
    return slope, icpt, (sum(r * r for r in res) / n) ** 0.5


def compare(log_path, smf_path, tol_ms, min_notes, max_notes, dur_tol_ms):
    got_msgs, want_msgs = read_log(log_path), read_smf(smf_path)
    got, got_ped = notes_of(got_msgs)
    want, want_ped = notes_of(want_msgs)
    if max_notes:
        want = want[:max_notes]; got = got[:max_notes]
    fails = []
    print(f"source : {len(want)} notes, {len(want_ped)} pédale | émis : {len(got)} notes, "
          f"{len(got_ped)} pédale, {sum(1 for _, m in got_msgs if m[0] == 0xF0)} SysEx")
    if len(got) < min_notes:
        fails.append(f"moins de {min_notes} notes émises ({len(got)})")
    n = min(len(got), len(want))
    if len(got) != len(want) and not max_notes:
        fails.append(f"nombre de notes : attendu {len(want)}, émis {len(got)}")
    pitch_bad = [(i, w, g) for i, (w, g) in enumerate(zip(want, got)) if (w[1], w[2]) != (g[1], g[2])]
    if pitch_bad:
        i, w, g = pitch_bad[0]
        fails.append(f"{len(pitch_bad)} notes hors ordre/hauteur ; 1re à #{i} : attendu "
                     f"ch{w[1]} n{w[2]}, émis ch{g[1]} n{g[2]}")
    vel_bad = sum(1 for w, g in zip(want, got) if w[3] != g[3])
    if vel_bad:
        fails.append(f"{vel_bad} vélocités différentes")
    if n >= 2:
        slope, _icpt, jitter = linfit([w[0] for w in want[:n]], [g[0] for g in got[:n]])
        print(f"tempo  : pente {slope:.5f} (1 = exact), gigue σ {jitter * 1000:.2f} ms")
        if abs(slope - 1.0) > 0.005:
            fails.append(f"pente de tempo {slope:.4f} (tolérance ±0,5 %)")
        if jitter * 1000 > tol_ms:
            fails.append(f"gigue σ {jitter * 1000:.2f} ms > {tol_ms} ms")
        # Durées : tolérance À PART, plus large. Cubase Lite émet ses note-off avec un
        # tick interne d'avance (mesuré : −1,6 ms à 150 bpm, −5 ms à 55 bpm, constant par
        # morceau) — c'est le séquenceur, pas l'ACIA : les note-on, eux, tiennent la gigue.
        dur_bad = [(w, g) for w, g in zip(want[:n], got[:n])
                   if w[4] is not None and g[4] is not None
                   and abs(w[4] - g[4]) * 1000 > dur_tol_ms + 2.0 * w[4]]   # + 0,2 % (pente de tempo)
        if dur_bad:
            w, g = dur_bad[0]
            fails.append(f"{len(dur_bad)} durées hors tolérance ; ex. n{w[2]} attendu "
                         f"{w[4] * 1000:.1f} ms, émis {g[4] * 1000:.1f} ms")
    # Pédale : bornée à la fenêtre [1re note, dernière note comparée] de chaque côté.
    # Avant la 1re note, Cubase envoie son « reset » de Play (CC64=0, CC121… sur les
    # 16 canaux) — ce n'est pas le morceau, on l'ignore.
    if n:
        w0, w1 = want[0][0], want[n - 1][0]
        g0, g1 = got[0][0], got[n - 1][0]
        wp = [(c, v) for t, c, v in want_ped if w0 - 1e-3 <= t <= w1 + 1e-3]
        gp = [(c, v) for t, c, v in got_ped if g0 - 1e-3 <= t <= g1 + 1e-3]
        if wp != gp:
            k = next((i for i, (a, b) in enumerate(zip(wp, gp)) if a != b), min(len(wp), len(gp)))
            fails.append(f"pédale CC64 : {len(wp)} transitions attendues, {len(gp)} émises, "
                         f"1re différence à #{k}")
    for f in fails:
        print("FAIL:", f)
    print("PASS" if not fails else "FAIL")
    return 0 if not fails else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[1])
    ap.add_argument('log')
    ap.add_argument('src', nargs='?', help="SMF donné au séquenceur")
    ap.add_argument('--to-smf', metavar='OUT', help="écrit le journal en SMF et sort")
    ap.add_argument('--tol-ms', type=float, default=5.0, help="tolérance de gigue des note-on (défaut 5)")
    ap.add_argument('--dur-tol-ms', type=float, default=12.0,
                    help="tolérance sur les durées (défaut 12 : Cubase avance ses note-off d'un tick)")
    ap.add_argument('--min-notes', type=int, default=1)
    ap.add_argument('--max-notes', type=int, default=0,
                    help="ne comparer que les N premières notes (lecture tronquée)")
    a = ap.parse_args()
    if a.to_smf:
        msgs = read_log(a.log); to_smf(msgs, a.to_smf)
        print(f"{len(msgs)} messages -> {a.to_smf}")
        return 0
    if not a.src:
        ap.error("SRC.mid requis sans --to-smf")
    return compare(a.log, a.src, a.tol_ms, a.min_notes, a.max_notes, a.dur_tol_ms)


if __name__ == '__main__':
    sys.exit(main())
