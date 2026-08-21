// =============================================================================
//  MidiOutMac.hpp — Sortie MIDI hôte pour le GUI macOS : le flux MIDI OUT de
//  l'ACIA 6850 (MidiAcia::setMidiSink) part vers (a) le synthétiseur General MIDI
//  intégré d'Apple (DLSMusicDevice → sortie audio par défaut : du son sans rien
//  installer) et/ou (b) un port CoreMIDI VIRTUEL « NeoST MIDI OUT » que GarageBand,
//  Logic ou tout synthé logiciel peuvent écouter. Les deux sont indépendants.
//
//  Les octets arrivent un par un depuis le thread d'émulation ; un petit parseur
//  (running status, SysEx, temps réel) reconstitue les messages. Hors macOS la
//  classe compile en coquille vide (toujours « indisponible »).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

class MidiOutMac {
public:
    MidiOutMac();
    ~MidiOutMac();
    static bool available();            // vrai sur macOS

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
    static constexpr double kCpuHz = 8021248.0;
    static constexpr int    kLeadMs = 30;
    void anchor(int64_t cycle, std::chrono::steady_clock::time_point hostTime);
    void byteAt(uint8_t b, int64_t cycle);

private:
    void* graph_ = nullptr;             // AUGraph
    void* synth_ = nullptr;             // AudioUnit DLSMusicDevice
    uint32_t client_ = 0;               // MIDIClientRef
    uint32_t src_ = 0;                  // MIDIEndpointRef (source virtuelle)

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
