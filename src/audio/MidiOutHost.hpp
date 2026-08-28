// =============================================================================
//  MidiOutHost.hpp — Sortie MIDI hôte : le flux MIDI OUT de l'ACIA 6850
//  (MidiAcia::setMidiSink) part vers l'extérieur de l'émulateur.
//
//  Deux destinations, indépendantes :
//    (a) SYNTHÉ GM INTÉGRÉ — macOS seulement (DLSMusicDevice d'Apple) : du son sans
//        rien installer. Aucun équivalent gratuit et embarquable sous Linux/Windows,
//        d'où synthAvailable() qui dit la vérité à l'interface plutôt que de lui
//        laisser proposer une case morte.
//    (b) PORT MIDI VIRTUEL « NeoST MIDI OUT », que n'importe quel synthé logiciel ou
//        matériel peut écouter : CoreMIDI sous macOS, séquenceur ALSA sous Linux.
//        C'est la voie recommandée pour du General MIDI — un FluidSynth ou un Qsynth
//        branché dessus rendra ces fichiers bien mieux qu'un MT-32.
//
//  Les octets arrivent un par un depuis le thread d'émulation ; un petit parseur
//  (running status, SysEx, temps réel) reconstitue les messages. Sur une plateforme
//  sans backend, la classe compile en coquille vide (toujours « indisponible »).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include "core/Pacing.hpp"
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

class MidiOutHost {
public:
    MidiOutHost();
    ~MidiOutHost();
    static bool available();            // au moins une destination utilisable ici
    static bool synthAvailable();       // synthé GM intégré : macOS UNIQUEMENT
    static bool portAvailable();        // port virtuel : CoreMIDI (macOS) / ALSA (Linux)
    static const char* portKindName();  // « CoreMIDI » / « ALSA » / « — », pour l'interface

    // Panique MIDI : All Sound Off (CC 120), Reset All Controllers (CC 121) et All
    // Notes Off (CC 123) sur les 16 canaux. Indispensable dès qu'on coupe une sortie
    // ou qu'on remet la machine à zéro pendant qu'un accord tient : sans ça les notes
    // restent BLOQUÉES dans le synthé, qui n'a aucune raison de les relâcher tout seul.
    void panic();

    bool openSynth();                   // synthé GM intégré (DLSMusicDevice)
    void closeSynth();
    bool synthOpen() const { return synth_ != nullptr; }

    bool openVirtualPort();             // source CoreMIDI « NeoST MIDI OUT »
    void closeVirtualPort();
    bool portOpen() const { return src_ != 0; }

    bool anyOpen() const { return synthOpen() || portOpen(); }

    // Un octet MIDI OUT de l'ACIA, livré IMMÉDIATEMENT (thread d'émulation).
    void byte(uint8_t b);

    // --- Livraison HORODATÉE (celle du GUI) ----------------------------------
    // L'émulation avance par rafales de trames au rythme du GUI : livrer un octet
    // au moment où le 68000 l'écrit fait gigoter le MIDI de ±60 ms (mesuré sur
    // Cubase : σ 28 ms). On ancre chaque trame sur l'heure HÔTE à laquelle elle
    // DOIT commencer (`anchor`, = l'échéance emuNext du GUI) et chaque octet est
    // programmé à anchor + (cycle − cycleAncre)/CPU_HZ + kLeadMs. Un thread le
    // délivre à l'heure dite : latence fixe (inaudible), gigue nulle.
    // A28 : UNE seule définition de l'horloge CPU/bus, dans core/Pacing.hpp.
    static constexpr double kCpuHz = neost::pacing::kCpuHz;
    static constexpr int    kLeadMs = 30;
    void anchor(int64_t cycle, std::chrono::steady_clock::time_point hostTime);
    void byteAt(uint8_t b, int64_t cycle);

private:
    void* graph_ = nullptr;             // AUGraph (macOS)
    void* synth_ = nullptr;             // AudioUnit DLSMusicDevice (macOS)
    uint32_t client_ = 0;               // MIDIClientRef (macOS)
    uint32_t src_ = 0;                  // MIDIEndpointRef / port ALSA + 1 (0 = fermé)
    void* seq_ = nullptr;               // snd_seq_t*        (Linux)
    void* enc_ = nullptr;               // snd_midi_event_t* (Linux)

    // Parseur : statut courant, octets de données attendus/accumulés, SysEx.
    uint8_t status_ = 0;
    int     needed_ = 0;
    uint8_t data_[2] = {0, 0};
    int     got_ = 0;
    bool    inSysex_ = false;
    std::vector<uint8_t> sysex_;

    void emit(const uint8_t* msg, int len);     // message complet → synthé + port
    void parse(uint8_t b);                      // octet → parseur → emit (thread de livraison)

    // File horodatée + thread de livraison.
    struct Pending { std::chrono::steady_clock::time_point when; uint8_t b; };
    std::mutex mtx_;
    std::condition_variable cv_;
    std::deque<Pending> queue_;
    std::thread worker_;
    bool stop_ = false;
    int64_t anchorCycle_ = 0;
    std::chrono::steady_clock::time_point anchorHost_{};
    bool anchored_ = false;
    std::mutex outMtx_;                         // protège synth_/src_ contre close pendant emit
    void workerLoop();
};
