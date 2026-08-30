// =============================================================================
//  MidiAcia.hpp — ACIA 6850 MIDI de l'Atari ST ($FFFC04 contrôle/statut,
//  $FFFC06 données), avec BOUCLAGE interne (connecteur MIDI OUT→IN).
//
//  Le port MIDI ST est une seconde ACIA 6850, distincte du clavier. Le diagnostic
//  « M MIDI » émet un octet sur MIDI OUT, active l'interruption de réception (RIE)
//  et attend l'IRQ ACIA (canal 6 du MFP via GPIP4) prouvant qu'il a RELU l'octet
//  sur MIDI IN — ce qui suppose un câble de bouclage OUT→IN branché. NeoST émule
//  ce câble : tout octet écrit sur la donnée TX est ré-injecté dans la file de
//  réception et, si RIE est armé, lève le canal 6 du MFP (comme l'ACIA clavier).
//
//  Côté ÉMISSION, l'ACIA MIDI suit le même modèle que l'ACIA clavier (port de
//  acia.c ACIA_Write_CR/ACIA_UpdateIRQ) : CR bits 5-6 = 01 arme l'IRQ d'émission
//  (TIE) et l'écriture d'une donnée vide TDRE, re-rempli ~1 octet MIDI plus tard
//  (10 bits à 31250 bauds = 2560 cycles, Scheduler::MIDI_TX) — c'est l'IRQ
//  « transmetteur prêt » dont les séquenceurs MIDI cadencent leur sortie.
//  Hors TIE, TDRE reste câblé à 1 (modèle simplifié, comme l'ACIA clavier).
//
//  ⚠ Le câble est DÉBRANCHÉ par défaut (2026-08-21) : Cubase Lite (MROS) avec son
//  MIDI Thru ré-émet tout ce qu'il reçoit — bouclé, chaque octet sorti revenait et
//  repartait, larsen MIDI infini, « gel » au chargement d'un morceau. Sur un vrai ST
//  rien n'est branché par défaut ; la fiche de bouclage se pose comme celle du
//  RS-232 : setLoopback(true) (--loopback en headless, menu Machine dans le GUI).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <vector>

#include "core/Scheduler.hpp"
#include "core/StateArchive.hpp"

class Mfp;

class MidiAcia {
public:
    explicit MidiAcia(Mfp& mfp) : mfp_(mfp) {}

    // Ordonnanceur : date le re-remplissage de TDRE sous TIE (cf. onTxEmpty).
    void setScheduler(Scheduler* s) { sched_ = s; }

    // Pont MIDI RÉSEAU (MIDIMaze en ligne — extension NeoST). Sans sink, l'ACIA
    // boucle OUT→IN sur elle-même (défaut, requis par le diagnostic « M MIDI »).
    // Avec un sink posé, les octets MIDI OUT partent vers le RÉSEAU (l'anneau
    // MIDI) au lieu de reboucler, et les octets de l'anneau reviennent par
    // receiveExternal — exactement le câblage d'un anneau MIDI physique.
    void setMidiSink(std::function<void(uint8_t)> fn) { midiSink_ = std::move(fn); }
    // Variante DATÉE : reçoit aussi le cycle CPU d'émission — pour une sortie hôte qui
    // replace chaque octet à son instant ST réel (cf. audio/MidiOutHost, gigue de trame).
    void setMidiSinkTimed(std::function<void(uint8_t, int64_t)> fn) { midiSinkTimed_ = std::move(fn); }
    // Câble de bouclage MIDI OUT→IN (diagnostics). Config, hors save-state.
    void setLoopback(bool plugged) { loopback_ = plugged; }
    bool loopback() const { return loopback_; }
    bool midiNetworked() const { return static_cast<bool>(midiSink_) || static_cast<bool>(midiSinkTimed_); }
    // Injecte un octet reçu du réseau dans MIDI IN (comme le bouclage, mais depuis
    // l'extérieur). Lève l'IRQ ACIA si RIE est armé.
    void receiveExternal(uint8_t b);

    // --- Source hôte CADENCÉE (appareil MIDI branché sur la machine) --------------
    // `fn(b)` rend true et remplit b s'il y a un octet à faire entrer. L'ACIA
    // l'interroge sur l'horloge SÉRIE (2560 cycles = 10 bits à 31250 bauds), pas une
    // fois par trame : c'est la différence entre 3125 o/s (un vrai câble) et
    // 2 octets/trame (~143 o/s en mono), plafond mesuré de l'ancienne injection.
    // Le débordement redevient alors celui du matériel : si le ST ne lit pas assez
    // vite, c'est le 6850 qui perd l'octet neuf (pushRx), pas l'hôte qui retient.
    void setRxSource(std::function<bool(uint8_t&)> fn);
    bool rxSourced() const { return static_cast<bool>(rxSource_); }
    void onRxPace();                         // échéance MIDI_RX : un octet a fini d'entrer
    // Le 6850 n'a que 2 octets (RDR + registre à décalage) : l'anneau réseau doit
    // n'injecter QUE lorsque la puce a de la place, sinon overrun (jitter buffer
    // côté adaptateur — cf. MidiRing). Vrai = receiveExternal ne débordera pas.
    bool rxCanAccept() const { return rx_.size() < kMidiRxMax; }

    uint8_t read8(uint32_t addr);            // $FFFC04 statut / $FFFC06 données
    void    write8(uint32_t addr, uint8_t v);

    // Reset matériel — appelé par les resets CHAUD et FROID, comme chez Hatari
    // (reset.c:111 ACIA_Reset + :124 Midi_Reset, tous deux dans Reset_ST()). Sans
    // lui, un séquenceur qui avait armé RIE/TIE laissait après un Ctrl-Alt-Del un
    // statut périmé sur $FFFC04 et un octet fantôme livrable sur $FFFC06.
    void reset() {
        rx_.clear();
        rdr_ = 1;                            // amorçage 1 (midi.c:110)
        rdrf_ = false;
        control_ = 0;
        txEnableInt_ = false;
        tdre_ = true;                        // ACIA_SR_TX_EMPTY
        if (sched_) sched_->cancel(Scheduler::MIDI_TX);
        // L'horloge de réception, elle, ne s'arrête pas au reset : le câble continue
        // de porter des octets pendant qu'on remet la puce à zéro. On la RÉARME (et on
        // ne la laisse pas éteinte) tant qu'une source hôte est branchée.
        armRxPace();
    }

    // Échéance MIDI_TX : le registre d'émission s'est vidé (~1 octet MIDI après
    // une écriture $FFFC06 sous TIE) → TDRE repasse à 1 et ré-arme l'IRQ TX.
    void    onTxEmpty();

    // Save-state : transfère l'intégralité de l'état runtime (save ET load via la
    // même méthode). Le std::deque rx_ n'a pas d'assistant StateArchive → on le
    // sérialise via un vector<uint8_t> tampon (longueur préfixée par vec()).
    void serialize(StateArchive& ar) {
        std::vector<uint8_t> rxBuf(rx_.begin(), rx_.end());
        ar.vec(rxBuf);
        if (ar.loading()) rx_.assign(rxBuf.begin(), rxBuf.end());
        ar(rdr_);
        ar(rdrf_);
        ar(control_);
        ar(txEnableInt_);
        ar(tdre_);
        // L'horloge de réception est un ÉTAT DU CÂBLAGE, pas de la machine : la
        // source hôte (rxSource_) survit au chargement, mais le Scheduler vient de
        // restaurer SES échéances — celles de l'état sauvé. Un état sauvé SANS
        // appareil MIDI IN, rechargé pendant qu'un clavier est branché, laissait donc
        // MIDI_RX éteint pour toujours : plus rien ne tirait, entrée morte en silence
        // (et la reconnexion à 1 Hz ne la ranimait pas — l'appareil est toujours
        // « ouvert » côté hôte). On réarme si la source existe et que l'échéance ne
        // s'est pas restaurée ; si elle s'est restaurée, on lui laisse sa phase.
        if (ar.loading() && rxSource_ && sched_
            && sched_->cyclesUntil(Scheduler::MIDI_RX) < 0)
            armRxPace();
    }

private:
    void raiseIfReady();                     // lève le canal 6 du MFP si cause RX ou TX

    Mfp&    mfp_;
    Scheduler* sched_ = nullptr;
    static constexpr std::size_t kMidiRxMax = 2;   // RDR + registre à décalage (6850)
    std::deque<uint8_t> rx_;                 // file MIDI IN (alimentée par le bouclage)
    uint8_t rdr_ = 1;                        // dernier octet lu (RDR persistant : relu à vide, cf. 6850) ;
                                             // amorçage 1 comme Hatari (midi.c:110, Midi_Reset)
    bool    rdrf_ = false;                    // Receive Data Register Full — distinct de !rx_.empty()
                                             // (effacé au master reset SANS purger la file, cf. acia.c)
    uint8_t control_ = 0;                    // registre contrôle ACIA (bit7 = RX int enable)
    bool    txEnableInt_ = false;            // IRQ d'émission armée : CR bits5-6 = 01
    bool    tdre_ = true;                    // Transmit Data Register Empty (0 en émission sous TIE)
    // Pont MIDI réseau (cf. setMidiSink). Non sérialisé (liaison frontend).
    std::function<void(uint8_t)> midiSink_;
    std::function<void(uint8_t, int64_t)> midiSinkTimed_;
    bool loopback_ = false;                  // fiche de bouclage OUT→IN branchée ?
    // Source hôte cadencée (cf. setRxSource). Non sérialisée (liaison frontend).
    std::function<bool(uint8_t&)> rxSource_;
    void armRxPace();                        // (ré)arme l'échéance MIDI_RX si source
    void pushRx(uint8_t v);                  // ajoute un octet à MIDI IN (RDR + shift)
};
