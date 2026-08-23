// =============================================================================
//  MidiOutHost.cpp — cf. MidiOutHost.hpp. API C d'AudioToolbox/CoreMIDI (pas
//  d'Objective-C) ; AUGraph est déprécié mais reste fonctionnel et suffit ici.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "audio/MidiOutHost.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

#if defined(NEOST_MIDI_ALSA)
#include <alsa/asoundlib.h>
#endif

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreMIDI/CoreMIDI.h>
#endif

MidiOutHost::MidiOutHost() {
    sysex_.reserve(256);
    worker_ = std::thread([this] { workerLoop(); });
}
MidiOutHost::~MidiOutHost() {
    { std::lock_guard<std::mutex> lk(mtx_); stop_ = true; }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    closeSynth(); closeVirtualPort();
}

// -----------------------------------------------------------------------------
//  Livraison horodatée
// -----------------------------------------------------------------------------
void MidiOutHost::anchor(int64_t cycle, std::chrono::steady_clock::time_point hostTime) {
    std::lock_guard<std::mutex> lk(mtx_);
    anchorCycle_ = cycle; anchorHost_ = hostTime; anchored_ = true;
}

void MidiOutHost::byteAt(uint8_t b, int64_t cycle) {
    std::chrono::steady_clock::time_point when;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        if (!anchored_) { when = std::chrono::steady_clock::now(); }
        else {
            const double dt = double(cycle - anchorCycle_) / kCpuHz;
            when = anchorHost_ + std::chrono::microseconds(int64_t(dt * 1e6)) + std::chrono::milliseconds(kLeadMs);
        }
        // L'ordre des octets est SACRÉ (running status, SysEx) : jamais avant le précédent.
        if (!queue_.empty() && when < queue_.back().when) when = queue_.back().when;
        queue_.push_back({when, b});
    }
    cv_.notify_one();
}

void MidiOutHost::workerLoop() {
    std::unique_lock<std::mutex> lk(mtx_);
    while (!stop_) {
        if (queue_.empty()) { cv_.wait(lk); continue; }
        const auto when = queue_.front().when;
        cv_.wait_until(lk, when);
        while (!stop_ && !queue_.empty() && queue_.front().when <= std::chrono::steady_clock::now()) {
            const uint8_t b = queue_.front().b;
            queue_.pop_front();
            lk.unlock();
            parse(b);
            lk.lock();
        }
    }
}

bool MidiOutHost::synthAvailable() {
#ifdef __APPLE__
    return true;                        // DLSMusicDevice : rien d'équivalent ailleurs
#else
    return false;
#endif
}

bool MidiOutHost::portAvailable() {
#if defined(__APPLE__) || defined(NEOST_MIDI_ALSA)
    return true;
#else
    return false;
#endif
}

const char* MidiOutHost::portKindName() {
#if defined(__APPLE__)
    return "CoreMIDI";
#elif defined(NEOST_MIDI_ALSA)
    return "ALSA";
#else
    return "—";
#endif
}

bool MidiOutHost::available() { return synthAvailable() || portAvailable(); }

// -----------------------------------------------------------------------------
//  Synthé GM intégré : DLSMusicDevice → DefaultOutput, via un AUGraph.
// -----------------------------------------------------------------------------
bool MidiOutHost::openSynth() {
#ifdef __APPLE__
    if (synth_) return true;
    AUGraph graph = nullptr;
    if (NewAUGraph(&graph) != noErr || !graph) return false;
    AudioComponentDescription synthDesc{kAudioUnitType_MusicDevice, kAudioUnitSubType_DLSSynth,
                                        kAudioUnitManufacturer_Apple, 0, 0};
    AudioComponentDescription outDesc{kAudioUnitType_Output, kAudioUnitSubType_DefaultOutput,
                                      kAudioUnitManufacturer_Apple, 0, 0};
    AUNode synthNode = 0, outNode = 0;
    AudioUnit synthUnit = nullptr;
    if (AUGraphAddNode(graph, &synthDesc, &synthNode) != noErr ||
        AUGraphAddNode(graph, &outDesc, &outNode) != noErr ||
        AUGraphOpen(graph) != noErr ||
        AUGraphConnectNodeInput(graph, synthNode, 0, outNode, 0) != noErr ||
        AUGraphNodeInfo(graph, synthNode, nullptr, &synthUnit) != noErr ||
        AUGraphInitialize(graph) != noErr ||
        AUGraphStart(graph) != noErr) {
        std::fprintf(stderr, "[midi-out] built-in GM synth unavailable\n");
        DisposeAUGraph(graph);
        return false;
    }
    graph_ = graph;
    synth_ = synthUnit;
    std::fprintf(stderr, "[midi-out] built-in General MIDI synth started\n");
    return true;
#else
    return false;
#endif
}

void MidiOutHost::closeSynth() {
#ifdef __APPLE__
    std::lock_guard<std::mutex> lk(outMtx_);
    if (!graph_) return;
    AUGraph graph = static_cast<AUGraph>(graph_);
    AUGraphStop(graph);
    AUGraphUninitialize(graph);
    DisposeAUGraph(graph);
    graph_ = nullptr;
    synth_ = nullptr;
#endif
}

// -----------------------------------------------------------------------------
//  Port CoreMIDI virtuel : les autres applications le voient comme une SOURCE.
// -----------------------------------------------------------------------------
bool MidiOutHost::openVirtualPort() {
#if defined(NEOST_MIDI_ALSA)
    if (seq_) return true;
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_OUTPUT, 0) < 0) {
        std::fprintf(stderr, "[midi-out] ALSA sequencer unavailable\n");
        return false;
    }
    snd_seq_set_client_name(seq, "NeoST");
    // Port en LECTURE et souscriptible : c'est une SOURCE, les synthés s'y abonnent
    // (aconnect, qjackctl, l'onglet MIDI de Qsynth…). Type APPLICATION pour que les
    // gestionnaires de connexions le rangent au bon endroit.
    const int port = snd_seq_create_simple_port(
        seq, "NeoST MIDI OUT",
        SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (port < 0) {
        snd_seq_close(seq);
        std::fprintf(stderr, "[midi-out] cannot create the ALSA port\n");
        return false;
    }
    // L'encodeur convertit le FLUX d'octets en événements du séquenceur — il gère
    // running status et SysEx, qu'on n'a donc pas à réimplémenter ici. 4 Ko : de quoi
    // contenir un dump de patch (les SysEx du parseur sont bornés plus haut).
    snd_midi_event_t* enc = nullptr;
    if (snd_midi_event_new(4096, &enc) < 0) {
        snd_seq_delete_simple_port(seq, port); snd_seq_close(seq);
        return false;
    }
    snd_midi_event_no_status(enc, 1);   // pas de running status en sortie : chaque
                                        // événement du séquenceur est autonome
    std::lock_guard<std::mutex> lk(outMtx_);
    seq_ = seq; enc_ = enc; src_ = uint32_t(port) + 1;   // +1 : 0 signifie « fermé »
    std::fprintf(stderr, "[midi-out] ALSA port \"NeoST MIDI OUT\" open — connect a synth to it\n");
    return true;
#elif defined(__APPLE__)
    if (src_) return true;
    MIDIClientRef client = 0;
    MIDIEndpointRef src = 0;
    if (const OSStatus st = MIDIClientCreate(CFSTR("NeoST"), nullptr, nullptr, &client); st != noErr) {
        // Muet jusqu'ici : le port « tombait » à 0 dans neost.cfg sans un mot. Cas vu :
        // process sandboxé sans accès au serveur CoreMIDI (MIDIServer) → -10844/… .
        std::fprintf(stderr, "[midi-out] MIDIClientCreate failed (OSStatus %d): no CoreMIDI "
                     "access from this process?\n", int(st));
        return false;
    }
    if (MIDISourceCreate(client, CFSTR("NeoST MIDI OUT"), &src) != noErr) {
        MIDIClientDispose(client);
        std::fprintf(stderr, "[midi-out] cannot create the CoreMIDI virtual source\n");
        return false;
    }
    client_ = client;
    src_ = src;
    std::fprintf(stderr, "[midi-out] CoreMIDI virtual source \"NeoST MIDI OUT\" created\n");
    return true;
#else
    return false;
#endif
}

void MidiOutHost::closeVirtualPort() {
#if defined(NEOST_MIDI_ALSA)
    std::lock_guard<std::mutex> lk(outMtx_);
    if (enc_) { snd_midi_event_free(static_cast<snd_midi_event_t*>(enc_)); enc_ = nullptr; }
    if (seq_) {
        if (src_) snd_seq_delete_simple_port(static_cast<snd_seq_t*>(seq_), int(src_) - 1);
        snd_seq_close(static_cast<snd_seq_t*>(seq_)); seq_ = nullptr;
    }
    src_ = 0;
#elif defined(__APPLE__)
    std::lock_guard<std::mutex> lk(outMtx_);
    if (src_)    { MIDIEndpointDispose(src_); src_ = 0; }
    if (client_) { MIDIClientDispose(client_); client_ = 0; }
#endif
}

// -----------------------------------------------------------------------------
//  Parseur d'octets → messages
// -----------------------------------------------------------------------------
namespace {
int dataBytesFor(uint8_t status) {
    switch (status & 0xF0) {
    case 0xC0: case 0xD0: return 1;                 // Program Change, Channel Pressure
    case 0x80: case 0x90: case 0xA0: case 0xB0: case 0xE0: return 2;
    default: break;
    }
    switch (status) {                                // système commun
    case 0xF1: case 0xF3: return 1;
    case 0xF2: return 2;
    default: return 0;
    }
}
} // namespace

void MidiOutHost::byte(uint8_t b) { parse(b); }

void MidiOutHost::parse(uint8_t b) {
    // NEOST_MIDIOUT_TRACE=1 : horodatage HÔTE (ms) de chaque octet livré — à croiser
    // avec NEOST_MIDI_TRACE (cycles ST) pour juger la cadence de livraison.
    static const bool trace = std::getenv("NEOST_MIDIOUT_TRACE") != nullptr;
    if (trace) {
        static const auto t0 = std::chrono::steady_clock::now();
        std::fprintf(stderr, "[midi-out] %.1f %02X\n",
                     std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count(), b);
    }
    if (b >= 0xF8) { emit(&b, 1); return; }          // temps réel : passe-droit, même en SysEx
    if (inSysex_) {
        if (b == 0xF7) {                             // fin de SysEx
            sysex_.push_back(b);
            emit(sysex_.data(), int(sysex_.size()));
            sysex_.clear(); inSysex_ = false;
        } else if (b & 0x80) {                       // statut : SysEx interrompu
            sysex_.clear(); inSysex_ = false;
            byte(b);
        } else if (sysex_.size() < 4096) {
            sysex_.push_back(b);
        }
        return;
    }
    if (b & 0x80) {
        if (b == 0xF0) { inSysex_ = true; sysex_.assign(1, b); status_ = 0; return; }
        status_ = b; needed_ = dataBytesFor(b); got_ = 0;
        if (needed_ == 0) { emit(&b, 1); status_ = 0; }
        return;
    }
    if (!status_) return;                            // donnée orpheline
    data_[got_++] = b;
    if (got_ >= needed_) {
        uint8_t msg[3] = {status_, data_[0], data_[1]};
        emit(msg, 1 + needed_);
        got_ = 0;                                    // running status : le statut reste armé
        if (status_ >= 0xF0) status_ = 0;            // pas de running status pour le système commun
    }
}

// -----------------------------------------------------------------------------
//  Panique MIDI
// -----------------------------------------------------------------------------
// Un synthé ne relâche JAMAIS une note de lui-même : si l'on coupe la sortie, qu'on
// remet la machine à zéro ou que le programme ST plante pendant un accord, les notes
// tiennent indéfiniment. On envoie donc la séquence standard sur les 16 canaux.
// Ordre voulu : All Sound Off coupe même les résonances, Reset All Controllers remet
// molette et pédale à zéro (une pédale de sustain restée enfoncée retiendrait les
// notes malgré All Notes Off), All Notes Off termine.
void MidiOutHost::panic() {
    for (int ch = 0; ch < 16; ++ch) {
        const uint8_t st = uint8_t(0xB0 | ch);
        const uint8_t allSoundOff[3]   = {st, 120, 0};
        const uint8_t resetCtrl[3]     = {st, 121, 0};
        const uint8_t allNotesOff[3]   = {st, 123, 0};
        emit(allSoundOff, 3);
        emit(resetCtrl, 3);
        emit(allNotesOff, 3);
    }
    // Le parseur d'octets est repris à zéro : un SysEx interrompu par la panique
    // laisserait sinon l'analyse au milieu d'un message.
    status_ = 0; needed_ = 0; got_ = 0; inSysex_ = false; sysex_.clear();
}

void MidiOutHost::emit(const uint8_t* msg, int len) {
#if defined(NEOST_MIDI_ALSA)
    std::lock_guard<std::mutex> lk(outMtx_);
    if (!seq_ || !enc_ || !src_) return;
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, int(src_) - 1);
    snd_seq_ev_set_subs(&ev);            // vers TOUS les abonnés du port
    snd_seq_ev_set_direct(&ev);          // pas de file : on est déjà horodaté en amont
    if (snd_midi_event_encode(static_cast<snd_midi_event_t*>(enc_), msg, len, &ev) > 0
        && ev.type != SND_SEQ_EVENT_NONE)
        snd_seq_event_output_direct(static_cast<snd_seq_t*>(seq_), &ev);
#elif defined(__APPLE__)
    std::lock_guard<std::mutex> lk(outMtx_);
    if (synth_ && len <= 3 && msg[0] < 0xF0)
        MusicDeviceMIDIEvent(static_cast<AudioUnit>(synth_), msg[0], len > 1 ? msg[1] : 0,
                             len > 2 ? msg[2] : 0, 0);
    if (src_) {
        alignas(MIDIPacketList) uint8_t buf[4096 + 64];
        MIDIPacketList* list = reinterpret_cast<MIDIPacketList*>(buf);
        MIDIPacket* pkt = MIDIPacketListInit(list);
        pkt = MIDIPacketListAdd(list, sizeof buf, pkt, 0, ByteCount(len), msg);
        if (pkt) MIDIReceived(src_, list);
    }
#else
    (void)msg; (void)len;
#endif
}
