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
//  Hors diagnostic, aucun logiciel ST courant ne dépend de l'absence de bouclage
//  MIDI ; on garde le câble toujours « branché » par simplicité.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <deque>
#include <vector>

#include "core/Scheduler.hpp"
#include "core/StateArchive.hpp"

class Mfp;

class MidiAcia {
public:
    explicit MidiAcia(Mfp& mfp) : mfp_(mfp) {}

    // Ordonnanceur : date le re-remplissage de TDRE sous TIE (cf. onTxEmpty).
    void setScheduler(Scheduler* s) { sched_ = s; }

    uint8_t read8(uint32_t addr);            // $FFFC04 statut / $FFFC06 données
    void    write8(uint32_t addr, uint8_t v);

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
};
