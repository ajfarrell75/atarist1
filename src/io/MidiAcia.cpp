// =============================================================================
//  MidiAcia.cpp — ACIA 6850 MIDI avec bouclage OUT→IN (cf. MidiAcia.hpp).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/MidiAcia.hpp"

#include <cstdio>
#include <cstdlib>
#include "io/Mfp.hpp"

// Bits du registre de statut ACIA 6850.
enum : uint8_t {
    ACIA_RDRF = 0x01,   // Receive Data Register Full
    ACIA_TDRE = 0x02,   // Transmit Data Register Empty
    ACIA_IRQ  = 0x80,   // ligne d'interruption (vers GPIP4 du MFP)
};

// Temps série d'un octet MIDI (trame de 10 bits à 31250 bauds) en cycles 68000 à
// 8 MHz : 8 000 000 × 10 / 31250 = 2560. C'est la cadence du re-remplissage de
// TDRE sous TIE (l'horloge MIDI est /16 d'un quartz 500 kHz, cf. Hatari midi.c).
static constexpr int64_t kMidiTxByteCycles = 2560;

uint8_t MidiAcia::read8(uint32_t addr) {
    if ((addr & 2) == 0) {
        // $FFFC04 : statut. TDRE = 1 au repos (0 pendant l'émission sous TIE) ;
        // RX plein si le bouclage a livré un octet ; IRQ si une cause RX ou TX
        // est active (cf. acia.c ACIA_UpdateIRQ).
        uint8_t s = tdre_ ? ACIA_TDRE : 0;
        if (rdrf_) s |= ACIA_RDRF;
        if ((rdrf_ && (control_ & 0x80)) || (txEnableInt_ && tdre_))
            s |= ACIA_IRQ;
        return s;
    }
    // $FFFC06 : donnée reçue → consomme un octet (efface RDRF). À vide, le 6850
    // renvoie le DERNIER octet reçu (RDR persistant, cf. acia.c ACIA_Read_RDR), pas 0.
    if (rx_.empty()) return rdr_;
    rdr_ = rx_.front();
    rx_.pop_front();
    rdrf_ = !rx_.empty();            // RDRF ne reste vrai que s'il reste un octet en file
    raiseIfReady();                  // octet suivant éventuel → ré-arme l'IRQ
    return rdr_;
}

void MidiAcia::write8(uint32_t addr, uint8_t v) {
    if ((addr & 2) == 0) {
        // $FFFC04 : contrôle. Bits 5-6 = contrôle émetteur (01 → TIE, cf. ACIA_Write_CR) ;
        // bits 0-1 = 11 → master reset.
        control_ = v;
        txEnableInt_ = ((v & 0x60) == 0x20);
        if (!txEnableInt_) {                 // TIE coupé → émetteur réputé prêt
            tdre_ = true;
            if (sched_) sched_->cancel(Scheduler::MIDI_TX);
        }
        // Master reset (bits 0-1 = 11) : efface RDRF (SR → TDRE seul, cf. acia.c
        // ACIA_MasterReset) mais NE PURGE PAS la file RX — le 6850 ne perd pas
        // l'octet en transit (note ikbd.c « don't clear bytes in transit »). RDRF
        // distinct de la file → on peut l'effacer tout en gardant l'octet relisible.
        if ((v & 0x03) == 0x03) rdrf_ = false;
        raiseIfReady();
        return;
    }
    // $FFFC06 : octet émis sur MIDI OUT. Sous TIE, TDRE tombe (transmetteur
    // occupé) et se re-remplit ~1 octet MIDI plus tard (Scheduler::MIDI_TX →
    // onTxEmpty) : c'est l'IRQ qui cadence la sortie des séquenceurs. L'octet
    // est bouclé aussitôt sur MIDI IN (câble OUT→IN) — NeoST ne perd jamais
    // l'octet émis, TDRE ne sert qu'au statut et à l'IRQ TX.
    if (txEnableInt_ && sched_) {
        tdre_ = false;
        sched_->schedule(Scheduler::MIDI_TX, sched_->now() + kMidiTxByteCycles);
    }
    // Pont MIDI RÉSEAU actif : l'octet part vers l'anneau au lieu de reboucler
    // (sinon on s'entendrait soi-même). Les octets de l'anneau reviennent par
    // receiveExternal (cf. MidiRing). Sans sink : bouclage interne SI la fiche est
    // branchée (setLoopback) — débranchée par défaut, comme sur un vrai ST.
    // NEOST_MIDI_TRACE=1 : chaque octet émis, daté au cycle (diagnostic tempo/gigue).
    static const bool traceOut = std::getenv("NEOST_MIDI_TRACE") != nullptr;
    if (traceOut) std::fprintf(stderr, "[midi] %lld %02X\n",
                               static_cast<long long>(sched_ ? sched_->now() : 0), v);
    if (midiSinkTimed_) { midiSinkTimed_(v, sched_ ? sched_->now() : 0); return; }
    if (midiSink_) { midiSink_(v); return; }
    if (loopback_) pushRx(v);        // bouclage OUT→IN (câble physique, optionnel)
}

void MidiAcia::pushRx(uint8_t v) {
    // Profondeur physique d'un 6850 : RDR + registre à décalage. Au-delà, l'octet le
    // plus ancien tombe — ce qui modélise en prime l'overrun réel du composant (le
    // bit OVRN reste non modélisé, cf. HATARI_DIVERGENCES.md).
    if (rx_.size() >= kMidiRxMax) rx_.pop_front();
    rx_.push_back(v);
    rdrf_ = true;                    // un octet disponible (RDRF)
    raiseIfReady();
}

void MidiAcia::receiveExternal(uint8_t b) {
    pushRx(b);                       // octet de l'anneau MIDI → MIDI IN
}

void MidiAcia::onTxEmpty() {
    // Le registre d'émission s'est vidé (octet « parti » sur MIDI OUT) : TDRE
    // repasse à 1 → re-lève l'IRQ « transmetteur prêt » tant que TIE est armé.
    tdre_ = true;
    raiseIfReady();
}

void MidiAcia::raiseIfReady() {
    // L'ACIA active sa ligne d'IRQ dès qu'une cause RX (octet dispo + RIE) ou TX
    // (TDRE + TIE) est active → canal 6 du MFP via GPIP4 (cf. ACIA_UpdateIRQ).
    const bool active = (rdrf_ && (control_ & 0x80)) || (txEnableInt_ && tdre_);
    // Le canal 6 est levé par le détecteur de FRONT du setter (wire-OR avec le
    // clavier : si GPIP4 est déjà bas, pas de nouveau front → pas d'IRQ, comme
    // sur le vrai MFP). Pas de raise() manuel.
    mfp_.setAciaLineMidi(active);
}
