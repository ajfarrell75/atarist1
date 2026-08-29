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
    worker_ = std::thread([this] { workerLoop(); });
}
MidiOutHost::~MidiOutHost() {
    { std::lock_guard<std::mutex> lk(mtx_); stop_ = true; }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    closeSynth(); closeDestinations(); closeVirtualPort();
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
//  Ressources partagées entre le port virtuel (b) et la destination matérielle (c)
//
//  macOS : un MIDIClientRef sert les deux (source virtuelle ET port de sortie).
//  ALSA  : les deux passent par UN port séquenceur — la destination matérielle est
//  un ABONNEMENT de ce port (snd_seq_connect_to), exactement ce que fait aconnect.
//  Conséquence assumée : choisir une destination sous Linux fait exister le port
//  « NeoST MIDI OUT » même si la case du port virtuel est décochée. Sans port
//  source, il n'y aurait rien à abonner.
//
//  ⚠ Les fonctions publiques prennent outMtx_ et les helpers le supposent DÉJÀ pris
//  (pas de verrou récursif en C++ sans std::recursive_mutex). panic() passe par
//  emit(), qui verrouille : ne jamais l'appeler le verrou en main.
// -----------------------------------------------------------------------------
bool MidiOutHost::ensurePort_() {
#if defined(NEOST_MIDI_ALSA)
    if (seq_) return true;
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
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
    seq_ = seq; enc_ = enc; src_ = uint32_t(port) + 1;   // +1 : 0 signifie « fermé »
    return true;
#elif defined(__APPLE__)
    if (client_) return true;
    MIDIClientRef client = 0;
    if (const OSStatus st = MIDIClientCreate(CFSTR("NeoST"), nullptr, nullptr, &client); st != noErr) {
        // Muet jusqu'ici : le port « tombait » à 0 dans neost.cfg sans un mot. Cas vu :
        // process sandboxé sans accès au serveur CoreMIDI (MIDIServer) → -10844/… .
        std::fprintf(stderr, "[midi-out] MIDIClientCreate failed (OSStatus %d): no CoreMIDI "
                     "access from this process?\n", int(st));
        return false;
    }
    client_ = client;
    return true;
#else
    return false;
#endif
}

// Ne détruit QUE ce que plus personne n'utilise : le port virtuel et la destination
// se partagent ces ressources, et fermer l'un ne doit pas couper l'autre.
void MidiOutHost::releasePort_() {
#if defined(NEOST_MIDI_ALSA)
    if (userPort_ || !dests_.empty()) return;
    if (enc_) { snd_midi_event_free(static_cast<snd_midi_event_t*>(enc_)); enc_ = nullptr; }
    if (seq_) {
        if (src_) snd_seq_delete_simple_port(static_cast<snd_seq_t*>(seq_), int(src_) - 1);
        snd_seq_close(static_cast<snd_seq_t*>(seq_)); seq_ = nullptr;
    }
    src_ = 0;
#elif defined(__APPLE__)
    if (src_ || !dests_.empty() || outPort_) return;
    if (client_) { MIDIClientDispose(client_); client_ = 0; }
#endif
}

// -----------------------------------------------------------------------------
//  Port MIDI virtuel : les autres applications le voient comme une SOURCE.
// -----------------------------------------------------------------------------
bool MidiOutHost::openVirtualPort() {
#if defined(NEOST_MIDI_ALSA)
    std::lock_guard<std::mutex> lk(outMtx_);
    if (!ensurePort_()) return false;
    userPort_ = true;
    std::fprintf(stderr, "[midi-out] ALSA port \"NeoST MIDI OUT\" open — connect a synth to it\n");
    return true;
#elif defined(__APPLE__)
    std::lock_guard<std::mutex> lk(outMtx_);
    if (src_) { userPort_ = true; return true; }
    if (!ensurePort_()) return false;
    MIDIEndpointRef src = 0;
    if (MIDISourceCreate(client_, CFSTR("NeoST MIDI OUT"), &src) != noErr) {
        releasePort_();
        std::fprintf(stderr, "[midi-out] cannot create the CoreMIDI virtual source\n");
        return false;
    }
    src_ = src;
    userPort_ = true;
    std::fprintf(stderr, "[midi-out] CoreMIDI virtual source \"NeoST MIDI OUT\" created\n");
    return true;
#else
    return false;
#endif
}

void MidiOutHost::closeVirtualPort() {
    std::lock_guard<std::mutex> lk(outMtx_);
    userPort_ = false;
#if defined(NEOST_MIDI_ALSA)
    // Rien à détruire ici : sous ALSA le port EST la ressource partagée, et
    // releasePort_ ne le supprime que si la destination ne s'en sert plus.
#elif defined(__APPLE__)
    if (src_) { MIDIEndpointDispose(src_); src_ = 0; }
#endif
    releasePort_();
}

// -----------------------------------------------------------------------------
//  Destination MATÉRIELLE : le MIDI OUT du ST entre dans l'appareil branché.
// -----------------------------------------------------------------------------
#if defined(__APPLE__) && !defined(NEOST_MIDI_ALSA)
namespace {
// Nom AFFICHÉ (celui d'Audio MIDI Setup, « Circuit Tracks MIDI ») plutôt que le nom
// brut du port : c'est celui que l'utilisateur lit sur sa machine, donc le seul qu'il
// puisse reconnaître dans une liste.
std::string displayName(MIDIObjectRef obj) {
    CFStringRef cf = nullptr;
    if (MIDIObjectGetStringProperty(obj, kMIDIPropertyDisplayName, &cf) != noErr || !cf) return {};
    char buf[256] = {0};
    const bool ok = CFStringGetCString(cf, buf, sizeof buf, kCFStringEncodingUTF8);
    CFRelease(cf);
    return ok ? std::string(buf) : std::string();
}
} // namespace
#endif

std::vector<std::string> MidiOutHost::destinations() {
    std::vector<std::string> out;
#if defined(NEOST_MIDI_ALSA)
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) return out;
    const int self = snd_seq_client_id(seq);
    snd_seq_client_info_t* ci = nullptr; snd_seq_port_info_t* pi = nullptr;
    snd_seq_client_info_malloc(&ci); snd_seq_port_info_malloc(&pi);
    snd_seq_client_info_set_client(ci, -1);
    while (snd_seq_query_next_client(seq, ci) >= 0) {
        const int cid = snd_seq_client_info_get_client(ci);
        if (cid == self || cid == SND_SEQ_CLIENT_SYSTEM) continue;   // pas nous, pas le système
        snd_seq_port_info_set_client(pi, cid);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(seq, pi) >= 0) {
            const unsigned caps = snd_seq_port_info_get_capability(pi);
            if ((caps & (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE))
                != (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE)) continue;
            out.emplace_back(std::string(snd_seq_client_info_get_name(ci)) + ": "
                             + snd_seq_port_info_get_name(pi));
        }
    }
    snd_seq_port_info_free(pi); snd_seq_client_info_free(ci);
    snd_seq_close(seq);
#elif defined(__APPLE__)
    const ItemCount n = MIDIGetNumberOfDestinations();
    for (ItemCount i = 0; i < n; ++i) {
        std::string nm = displayName(MIDIGetDestination(i));
        if (!nm.empty()) out.push_back(std::move(nm));
    }
#endif
    return out;
}

std::size_t MidiOutHost::setDestinations(const std::vector<Dest>& want) {
    closeDestinations();
    if (want.empty()) return 0;
    std::lock_guard<std::mutex> lk(outMtx_);
    if (!ensurePort_()) return 0;
#if defined(NEOST_MIDI_ALSA)
    snd_seq_t* seq = static_cast<snd_seq_t*>(seq_);
    snd_seq_client_info_t* ci = nullptr; snd_seq_port_info_t* pi = nullptr;
    snd_seq_client_info_malloc(&ci); snd_seq_port_info_malloc(&pi);
    for (const Dest& w : want) {
        // Retrouve le couple client:port derrière le NOM (l'énumération et l'ouverture
        // sont deux instants distincts : pas d'index mémorisable entre les deux).
        int foundC = -1, foundP = -1;
        snd_seq_client_info_set_client(ci, -1);
        while (foundC < 0 && snd_seq_query_next_client(seq, ci) >= 0) {
            const int cid = snd_seq_client_info_get_client(ci);
            if (cid == snd_seq_client_id(seq) || cid == SND_SEQ_CLIENT_SYSTEM) continue;
            snd_seq_port_info_set_client(pi, cid);
            snd_seq_port_info_set_port(pi, -1);
            while (snd_seq_query_next_port(seq, pi) >= 0) {
                const unsigned caps = snd_seq_port_info_get_capability(pi);
                if ((caps & (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE))
                    != (SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE)) continue;
                if (std::string(snd_seq_client_info_get_name(ci)) + ": "
                    + snd_seq_port_info_get_name(pi) == w.name) {
                    foundC = cid; foundP = snd_seq_port_info_get_port(pi);
                    break;
                }
            }
        }
        if (foundC < 0) continue;                 // débranché : l'appelant re-tentera
        dests_.push_back({w.name, w.channels, (uint32_t(foundC) << 8 | uint32_t(foundP)) + 1});
        std::fprintf(stderr, "[midi-out] MIDI OUT -> \"%s\" (ALSA %d:%d) channels $%04X\n",
                     w.name.c_str(), foundC, foundP, unsigned(w.channels));
    }
    snd_seq_port_info_free(pi); snd_seq_client_info_free(ci);
#elif defined(__APPLE__)
    if (!outPort_) {
        MIDIPortRef port = 0;
        if (MIDIOutputPortCreate(client_, CFSTR("NeoST OUT"), &port) != noErr) {
            releasePort_();
            std::fprintf(stderr, "[midi-out] cannot create the CoreMIDI output port\n");
            return 0;
        }
        outPort_ = port;
    }
    for (const Dest& w : want) {
        MIDIEndpointRef found = 0;
        const ItemCount n = MIDIGetNumberOfDestinations();
        for (ItemCount i = 0; i < n; ++i) {
            const MIDIEndpointRef e = MIDIGetDestination(i);
            if (displayName(e) == w.name) { found = e; break; }
        }
        if (!found) continue;                     // débranché : l'appelant re-tentera
        dests_.push_back({w.name, w.channels, found});
        std::fprintf(stderr, "[midi-out] MIDI OUT -> \"%s\" (CoreMIDI) channels $%04X\n",
                     w.name.c_str(), unsigned(w.channels));
    }
#else
    (void)want;
#endif
    if (dests_.empty()) releasePort_();
    return dests_.size();
}

void MidiOutHost::closeDestinations() {
    // Une note tenue au moment du débranchement resterait tenue POUR TOUJOURS dans un
    // appareil matériel : il n'a aucune raison de la relâcher. On panique d'abord —
    // hors verrou, panic() passant par emit() qui le prend.
    if (!dests_.empty()) panic();
    std::lock_guard<std::mutex> lk(outMtx_);
    dests_.clear();
#if defined(__APPLE__) && !defined(NEOST_MIDI_ALSA)
    if (outPort_) { MIDIPortDispose(outPort_); outPort_ = 0; }
#endif
    releasePort_();
}

std::vector<MidiOutHost::Dest> MidiOutHost::openDestinations() const {
    std::vector<Dest> out;
    out.reserve(dests_.size());
    for (const auto& d : dests_) out.push_back({d.name, d.channels});
    return out;
}

// -----------------------------------------------------------------------------
//  Octets → messages (décodeur partagé, cf. MidiMessageParser.hpp)
// -----------------------------------------------------------------------------
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
    parser_.byte(b, [this](const uint8_t* msg, int len) { emit(msg, len); });
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
    // Le décodeur est repris à zéro : un SysEx interrompu par la panique laisserait
    // sinon l'analyse au milieu d'un message.
    parser_.reset();
}

void MidiOutHost::sendTo(const OpenDest& d, const uint8_t* msg, int len) {
#if defined(NEOST_MIDI_ALSA)
    if (!seq_ || !enc_ || !src_) return;
    snd_seq_event_t ev;
    snd_seq_ev_clear(&ev);
    snd_seq_ev_set_source(&ev, int(src_) - 1);
    // Adressage EXPLICITE (pas set_subs) : un abonné recevrait tout le flux, ce qui
    // interdirait le filtrage par canal qui fait tout l'intérêt de l'aiguillage.
    snd_seq_ev_set_dest(&ev, int((d.ep - 1) >> 8), int((d.ep - 1) & 0xFF));
    snd_seq_ev_set_direct(&ev);          // pas de file : on est déjà horodaté en amont
    if (snd_midi_event_encode(static_cast<snd_midi_event_t*>(enc_), msg, len, &ev) > 0
        && ev.type != SND_SEQ_EVENT_NONE)
        snd_seq_event_output_direct(static_cast<snd_seq_t*>(seq_), &ev);
#elif defined(__APPLE__)
    if (!outPort_) return;
    alignas(MIDIPacketList) uint8_t buf[4096 + 64];
    MIDIPacketList* list = reinterpret_cast<MIDIPacketList*>(buf);
    MIDIPacket* pkt = MIDIPacketListInit(list);
    pkt = MIDIPacketListAdd(list, sizeof buf, pkt, 0, ByteCount(len), msg);
    if (pkt) MIDISend(outPort_, d.ep, list);
#else
    (void)d; (void)msg; (void)len;
#endif
}

void MidiOutHost::emit(const uint8_t* msg, int len) {
    if (len <= 0) return;
    std::lock_guard<std::mutex> lk(outMtx_);

    // AIGUILLAGE. Un message de VOIE ($80-$EF) porte son canal dans le quartet bas et
    // ne part que vers les destinations qui l'écoutent. Les messages SYSTÈME
    // ($F0-$FF : horloge, start/stop, SysEx) n'ont pas de canal et vont à TOUTES :
    // les filtrer désynchroniserait le studio.
    const bool voice = msg[0] >= 0x80 && msg[0] < 0xF0;
    const int  ch    = voice ? (msg[0] & 0x0F) : -1;
    for (const auto& d : dests_)
        if (ch < 0 || ((d.channels >> ch) & 1)) sendTo(d, msg, len);

    // Le synthé intégré et le port virtuel reçoivent le flux ENTIER : ce ne sont pas
    // des appareils d'un studio à aiguiller, mais des sorties générales (un DAW
    // abonné au port virtuel fait son propre tri).
#if defined(NEOST_MIDI_ALSA)
    if (seq_ && enc_ && src_ && userPort_) {
        snd_seq_event_t ev;
        snd_seq_ev_clear(&ev);
        snd_seq_ev_set_source(&ev, int(src_) - 1);
        snd_seq_ev_set_subs(&ev);        // vers les abonnés du port virtuel
        snd_seq_ev_set_direct(&ev);
        if (snd_midi_event_encode(static_cast<snd_midi_event_t*>(enc_), msg, len, &ev) > 0
            && ev.type != SND_SEQ_EVENT_NONE)
            snd_seq_event_output_direct(static_cast<snd_seq_t*>(seq_), &ev);
    }
#elif defined(__APPLE__)
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
#endif
}
