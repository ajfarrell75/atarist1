// =============================================================================
//  MidiInHost.hpp — Entrée MIDI hôte : un appareil branché sur la machine (clavier
//  maître, groovebox, séquenceur matériel) entre dans le MIDI IN du ST.
//
//  C'est le pendant de MidiOutHost : là où celui-ci fait sortir l'ACIA 6850 vers le
//  monde, celui-ci fait ENTRER le monde dans l'ACIA (MidiAcia::receiveExternal).
//  Backends : CoreMIDI (macOS), séquenceur ALSA (Linux). Ailleurs, coquille vide.
//
//  Câblage (frontend) — UNE fois, à l'ouverture ; l'ACIA tire ensuite toute seule :
//    midiIn.open("Circuit Tracks MIDI");
//    machine.midi.setRxSource([&](uint8_t& b) { return midiIn.tryPop(b); });
//
//  POURQUOI un tampon de gigue. CoreMIDI livre ses paquets sur son PROPRE thread,
//  quand ça lui chante, alors que l'émulation avance par tranches. On accumule donc
//  côté hôte, et l'ACIA vient TIRER les octets un par un.
//
//  ⚠ C'est l'ACIA qui fixe la cadence, pas nous (MidiAcia::setRxSource → échéance
//  Scheduler::MIDI_RX, un octet toutes les 2560 cycles = 31250 bauds). La première
//  version poussait les octets une fois par TRAME, ce qui plafonnait l'entrée à
//  2 octets/trame — mesuré 1,76, soit ~143 o/s en mono contre 3125 o/s sur un vrai
//  câble : un accord de dix notes mettait 0,2 s à entrer. Le débit est maintenant
//  celui du câble, et le débordement redevient celui du MATÉRIEL (le 6850 perd
//  l'octet neuf si le ST ne lit pas assez vite) au lieu d'être masqué par une
//  rétention côté hôte.
//
//  Le tampon reste BORNÉ (kMaxJitter) pour le cas où l'hôte livre plus vite que
//  31250 bauds sur une rafale. Au-delà, ce sont les octets NEUFS qui tombent, comme
//  dans un vrai 6850 en overrun : garder l'ancien préserve le début des messages
//  déjà entamés.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifdef __APPLE__
struct MIDIPacketList;   // avancé : évite d'inclure CoreMIDI.h dans tout le GUI
#endif

class MidiInHost {
public:
    MidiInHost() = default;
    ~MidiInHost();
    MidiInHost(const MidiInHost&) = delete;
    MidiInHost& operator=(const MidiInHost&) = delete;

    static bool available();               // un backend d'entrée existe-t-il ici ?
    // Appareils branchés MAINTENANT (à rappeler pour voir un branchement à chaud).
    static std::vector<std::string> sources();

    // Ouvre PAR NOM (les index se renumérotent au débranchement — cf. MidiOutHost).
    // Échec sans bruit si l'appareil n'est pas là : l'appelant garde le nom et
    // re-tente, ce qui rend le branchement à chaud transparent.
    bool open(const std::string& name);
    void close();
    bool isOpen() const { return open_; }
    const std::string& name() const { return name_; }

    // Rend le prochain octet, s'il y en a un. Appelé par l'ACIA sur son horloge
    // série (cf. MidiAcia::setRxSource) — jamais par le frontend.
    bool tryPop(uint8_t& out);

    uint64_t delivered() const { return delivered_.load(std::memory_order_relaxed); }
    uint64_t dropped() const { return dropped_.load(std::memory_order_relaxed); }

    // Injection DIRECTE dans le tampon, sans backend ni appareil : neost-selftest
    // éprouve ainsi le drainage (ordre, non-débordement du 6850, perte du NEUF en
    // saturation) sur une machine où rien n'est branché — donc aussi en CI.
    void pushForTest(const uint8_t* data, std::size_t n) { push(data, n); }

private:
    static constexpr std::size_t kMaxJitter = 1024;     // ~2 trames de MIDI saturé

    void push(const uint8_t* data, std::size_t n);      // appelé par le callback/thread
#ifdef __APPLE__
    // Callback CoreMIDI (thread temps réel du MIDIServer). Membre statique : c'est
    // le seul moyen d'atteindre push() sans l'exposer publiquement.
    static void coreMidiRead(const ::MIDIPacketList* pkts, void* refCon, void* srcRef);
#endif

    mutable std::mutex mtx_;
    std::deque<uint8_t> jitter_;
    // Compte atomique du tampon : l'ACIA interroge tryPop() 3125 fois par seconde,
    // et le cas ÉCRASANT est « rien à prendre ». Ce compteur lui évite de prendre le
    // verrou pour se l'entendre dire.
    std::atomic<std::size_t> pending_{0};
    std::string name_;
    bool open_ = false;
    // Lues par le thread GUI (compteur de la page MIDI), écrites par l'émulation.
    std::atomic<uint64_t> delivered_{0}, dropped_{0};

    uint32_t client_ = 0;        // MIDIClientRef   (macOS)
    uint32_t port_   = 0;        // MIDIPortRef     (macOS) / port ALSA + 1
    uint32_t src_    = 0;        // MIDIEndpointRef (macOS) / (client<<8|port)+1 (ALSA)
    void* seq_ = nullptr;        // snd_seq_t*        (Linux)
    void* dec_ = nullptr;        // snd_midi_event_t* (Linux)
    std::thread reader_;         // boucle de lecture ALSA (CoreMIDI a son callback)
    std::atomic<bool> stop_{false};
    void readerLoop();
};
