// =============================================================================
//  MidiInHost.cpp — cf. MidiInHost.hpp. API C de CoreMIDI / ALSA, pas d'Objective-C.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "audio/MidiInHost.hpp"

#include <cstdio>
#include <cstring>

#if defined(NEOST_MIDI_ALSA)
#include <alsa/asoundlib.h>
#include <poll.h>
#endif

#ifdef __APPLE__
#include <CoreMIDI/CoreMIDI.h>
#endif

MidiInHost::~MidiInHost() { close(); }

bool MidiInHost::available() {
#if defined(__APPLE__) || defined(NEOST_MIDI_ALSA)
    return true;
#else
    return false;
#endif
}

// -----------------------------------------------------------------------------
//  Tampon de gigue
// -----------------------------------------------------------------------------
void MidiInHost::push(const uint8_t* data, std::size_t n) {
    std::lock_guard<std::mutex> lk(mtx_);
    for (std::size_t i = 0; i < n; ++i) {
        // Saturé : c'est le NEUF qui tombe (overrun de 6850, cf. MidiAcia::pushRx).
        if (jitter_.size() >= kMaxJitter) { dropped_.fetch_add(1, std::memory_order_relaxed); continue; }
        jitter_.push_back(data[i]);
    }
    pending_.store(jitter_.size(), std::memory_order_relaxed);
}

bool MidiInHost::tryPop(uint8_t& out) {
    // Chemin rapide sans verrou : l'ACIA appelle 3125 fois par seconde et repart
    // presque toujours les mains vides.
    if (pending_.load(std::memory_order_relaxed) == 0) return false;
    std::lock_guard<std::mutex> lk(mtx_);
    if (jitter_.empty()) return false;
    out = jitter_.front();
    jitter_.pop_front();
    pending_.store(jitter_.size(), std::memory_order_relaxed);
    delivered_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

// -----------------------------------------------------------------------------
//  Énumération des sources
// -----------------------------------------------------------------------------
#if defined(__APPLE__) && !defined(NEOST_MIDI_ALSA)
namespace {
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

std::vector<std::string> MidiInHost::sources() {
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
        if (cid == self || cid == SND_SEQ_CLIENT_SYSTEM) continue;
        snd_seq_port_info_set_client(pi, cid);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(seq, pi) >= 0) {
            const unsigned caps = snd_seq_port_info_get_capability(pi);
            // Une SOURCE se lit et se laisse abonner : READ | SUBS_READ.
            if ((caps & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ))
                != (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) continue;
            out.emplace_back(std::string(snd_seq_client_info_get_name(ci)) + ": "
                             + snd_seq_port_info_get_name(pi));
        }
    }
    snd_seq_port_info_free(pi); snd_seq_client_info_free(ci);
    snd_seq_close(seq);
#elif defined(__APPLE__)
    const ItemCount n = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < n; ++i) {
        std::string nm = displayName(MIDIGetSource(i));
        // « NeoST MIDI OUT » est NOTRE propre source virtuelle : la proposer en
        // entrée offrirait un bouclage OUT→IN déguisé, avec le larsen que la fiche
        // de bouclage débranchée par défaut cherche précisément à éviter.
        if (!nm.empty() && nm.rfind("NeoST", 0) != 0) out.push_back(std::move(nm));
    }
#endif
    return out;
}

// -----------------------------------------------------------------------------
//  Ouverture / fermeture
// -----------------------------------------------------------------------------
#if defined(__APPLE__) && !defined(NEOST_MIDI_ALSA)
// Callback CoreMIDI : thread temps réel du serveur MIDI. On n'y fait QUE recopier
// des octets sous verrou — pas d'allocation hors du deque, pas d'appel bloquant.
void MidiInHost::coreMidiRead(const ::MIDIPacketList* pkts, void* refCon, void* /*srcRef*/) {
    auto* self = static_cast<MidiInHost*>(refCon);
    if (!self || !pkts) return;
    const MIDIPacket* p = &pkts->packet[0];
    for (UInt32 i = 0; i < pkts->numPackets; ++i) {
        self->push(p->data, p->length);
        p = MIDIPacketNext(p);
    }
}
#endif

bool MidiInHost::open(const std::string& name) {
    if (name.empty()) { close(); return true; }
    if (open_ && name_ == name) return true;
    close();
#if defined(NEOST_MIDI_ALSA)
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
        std::fprintf(stderr, "[midi-in] ALSA sequencer unavailable\n");
        return false;
    }
    snd_seq_set_client_name(seq, "NeoST");
    const int port = snd_seq_create_simple_port(
        seq, "NeoST MIDI IN",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (port < 0) { snd_seq_close(seq); return false; }

    // Retrouve client:port derrière le nom (cf. MidiOutHost::openDestination).
    int foundC = -1, foundP = -1;
    snd_seq_client_info_t* ci = nullptr; snd_seq_port_info_t* pi = nullptr;
    snd_seq_client_info_malloc(&ci); snd_seq_port_info_malloc(&pi);
    snd_seq_client_info_set_client(ci, -1);
    while (foundC < 0 && snd_seq_query_next_client(seq, ci) >= 0) {
        const int cid = snd_seq_client_info_get_client(ci);
        if (cid == snd_seq_client_id(seq) || cid == SND_SEQ_CLIENT_SYSTEM) continue;
        snd_seq_port_info_set_client(pi, cid);
        snd_seq_port_info_set_port(pi, -1);
        while (snd_seq_query_next_port(seq, pi) >= 0) {
            const unsigned caps = snd_seq_port_info_get_capability(pi);
            if ((caps & (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ))
                != (SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ)) continue;
            if (std::string(snd_seq_client_info_get_name(ci)) + ": "
                + snd_seq_port_info_get_name(pi) == name) {
                foundC = cid; foundP = snd_seq_port_info_get_port(pi);
                break;
            }
        }
    }
    snd_seq_port_info_free(pi); snd_seq_client_info_free(ci);
    if (foundC < 0 || snd_seq_connect_from(seq, port, foundC, foundP) < 0) {
        snd_seq_delete_simple_port(seq, port); snd_seq_close(seq);
        return false;
    }
    // Décodeur événement → octets MIDI bruts. no_status(1) : chaque message ressort
    // avec son octet de statut, sans running status — le ST reçoit un flux explicite.
    snd_midi_event_t* dec = nullptr;
    if (snd_midi_event_new(1024, &dec) < 0) {
        snd_seq_disconnect_from(seq, port, foundC, foundP);
        snd_seq_delete_simple_port(seq, port); snd_seq_close(seq);
        return false;
    }
    snd_midi_event_no_status(dec, 1);
    seq_ = seq; dec_ = dec;
    port_ = uint32_t(port) + 1;
    src_  = (uint32_t(foundC) << 8 | uint32_t(foundP)) + 1;
    name_ = name; open_ = true; stop_ = false;
    reader_ = std::thread([this] { readerLoop(); });
    std::fprintf(stderr, "[midi-in] \"%s\" -> MIDI IN (ALSA %d:%d)\n", name.c_str(), foundC, foundP);
    return true;
#elif defined(__APPLE__)
    MIDIEndpointRef found = 0;
    const ItemCount n = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < n; ++i) {
        const MIDIEndpointRef e = MIDIGetSource(i);
        if (displayName(e) == name) { found = e; break; }
    }
    if (!found) return false;              // débranché : l'appelant re-tentera
    MIDIClientRef client = 0;
    if (const OSStatus st = MIDIClientCreate(CFSTR("NeoST IN"), nullptr, nullptr, &client); st != noErr) {
        std::fprintf(stderr, "[midi-in] MIDIClientCreate failed (OSStatus %d): no CoreMIDI "
                     "access from this process?\n", int(st));
        return false;
    }
    MIDIPortRef port = 0;
    if (MIDIInputPortCreate(client, CFSTR("NeoST IN"), &MidiInHost::coreMidiRead, this, &port) != noErr) {
        MIDIClientDispose(client);
        std::fprintf(stderr, "[midi-in] cannot create the CoreMIDI input port\n");
        return false;
    }
    if (MIDIPortConnectSource(port, found, nullptr) != noErr) {
        MIDIPortDispose(port); MIDIClientDispose(client);
        std::fprintf(stderr, "[midi-in] cannot connect \"%s\"\n", name.c_str());
        return false;
    }
    client_ = client; port_ = port; src_ = found;
    name_ = name; open_ = true;
    std::fprintf(stderr, "[midi-in] \"%s\" -> MIDI IN (CoreMIDI source)\n", name.c_str());
    return true;
#else
    (void)name;
    return false;
#endif
}

void MidiInHost::close() {
#if defined(NEOST_MIDI_ALSA)
    if (reader_.joinable()) { stop_ = true; reader_.join(); }
    if (seq_) {
        snd_seq_t* seq = static_cast<snd_seq_t*>(seq_);
        if (port_ && src_)
            snd_seq_disconnect_from(seq, int(port_) - 1, int((src_ - 1) >> 8), int((src_ - 1) & 0xFF));
        if (port_) snd_seq_delete_simple_port(seq, int(port_) - 1);
        snd_seq_close(seq); seq_ = nullptr;
    }
    if (dec_) { snd_midi_event_free(static_cast<snd_midi_event_t*>(dec_)); dec_ = nullptr; }
#elif defined(__APPLE__)
    if (port_) {
        if (src_) MIDIPortDisconnectSource(port_, src_);
        MIDIPortDispose(port_);
    }
    if (client_) MIDIClientDispose(client_);
#endif
    client_ = port_ = src_ = 0;
    open_ = false;
    name_.clear();
    std::lock_guard<std::mutex> lk(mtx_);
    jitter_.clear();     // un appareil débranché ne doit pas rejouer son passé
    pending_.store(0, std::memory_order_relaxed);
}

// -----------------------------------------------------------------------------
//  Boucle de lecture ALSA (CoreMIDI n'en a pas besoin : il appelle notre callback)
// -----------------------------------------------------------------------------
void MidiInHost::readerLoop() {
#if defined(NEOST_MIDI_ALSA)
    snd_seq_t* seq = static_cast<snd_seq_t*>(seq_);
    auto* dec = static_cast<snd_midi_event_t*>(dec_);
    const int nfds = snd_seq_poll_descriptors_count(seq, POLLIN);
    std::vector<pollfd> pfds(nfds > 0 ? std::size_t(nfds) : 1);
    while (!stop_) {
        snd_seq_poll_descriptors(seq, pfds.data(), unsigned(pfds.size()), POLLIN);
        // Timeout de 100 ms : c'est ce qui rend `stop_` efficace — un snd_seq_event_input
        // bloquant ne se laisse pas interrompre proprement à la fermeture.
        if (::poll(pfds.data(), nfds_t(pfds.size()), 100) <= 0) continue;
        snd_seq_event_t* ev = nullptr;
        while (!stop_ && snd_seq_event_input(seq, &ev) >= 0 && ev) {
            uint8_t buf[256];
            const long n = snd_midi_event_decode(dec, buf, sizeof buf, ev);
            if (n > 0) push(buf, std::size_t(n));
            if (snd_seq_event_input_pending(seq, 0) <= 0) break;
        }
    }
#endif
}
