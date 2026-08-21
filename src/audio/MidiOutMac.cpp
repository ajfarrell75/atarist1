// =============================================================================
//  MidiOutMac.cpp — cf. MidiOutMac.hpp. API C d'AudioToolbox/CoreMIDI (pas
//  d'Objective-C) ; AUGraph est déprécié mais reste fonctionnel et suffit ici.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "audio/MidiOutMac.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#include <CoreMIDI/CoreMIDI.h>
#endif

MidiOutMac::MidiOutMac() {
    sysex_.reserve(256);
    worker_ = std::thread([this] { workerLoop(); });
}
MidiOutMac::~MidiOutMac() {
    { std::lock_guard<std::mutex> lk(mtx_); stop_ = true; }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    closeSynth(); closeVirtualPort();
}

// -----------------------------------------------------------------------------
//  Livraison horodatée
// -----------------------------------------------------------------------------
void MidiOutMac::anchor(int64_t cycle, std::chrono::steady_clock::time_point hostTime) {
    std::lock_guard<std::mutex> lk(mtx_);
    anchorCycle_ = cycle; anchorHost_ = hostTime; anchored_ = true;
}

void MidiOutMac::byteAt(uint8_t b, int64_t cycle) {
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

void MidiOutMac::workerLoop() {
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

bool MidiOutMac::available() {
#ifdef __APPLE__
    return true;
#else
    return false;
#endif
}

// -----------------------------------------------------------------------------
//  Synthé GM intégré : DLSMusicDevice → DefaultOutput, via un AUGraph.
// -----------------------------------------------------------------------------
bool MidiOutMac::openSynth() {
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

void MidiOutMac::closeSynth() {
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
bool MidiOutMac::openVirtualPort() {
#ifdef __APPLE__
    if (src_) return true;
    MIDIClientRef client = 0;
    MIDIEndpointRef src = 0;
    if (MIDIClientCreate(CFSTR("NeoST"), nullptr, nullptr, &client) != noErr) return false;
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

void MidiOutMac::closeVirtualPort() {
#ifdef __APPLE__
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

void MidiOutMac::byte(uint8_t b) { parse(b); }

void MidiOutMac::parse(uint8_t b) {
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

void MidiOutMac::emit(const uint8_t* msg, int len) {
#ifdef __APPLE__
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
