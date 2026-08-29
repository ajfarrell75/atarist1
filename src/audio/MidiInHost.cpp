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
//  Fusion : octets d'une source → messages → file commune
// -----------------------------------------------------------------------------
void MidiInHost::feed(Device& d, const uint8_t* data, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        d.parser.byte(data[i], [this, &d](const uint8_t* msg, int len) {
            emitMessage(d, msg, len);
        });
}

void MidiInHost::emitMessage(Device& d, const uint8_t* msg, int len) {
    if (len <= 0) return;
    uint8_t head = msg[0];
    // CANALISATION : le quartet de canal des messages de VOIE ($80-$EF) est
    // réécrit. Les messages système ($F0-$FF) n'ont pas de canal — on n'y touche
    // pas, sinon on casserait horloge, SysEx et start/stop.
    if (d.forceChannel >= 1 && d.forceChannel <= 16 && head >= 0x80 && head < 0xF0)
        head = uint8_t((head & 0xF0) | uint8_t(d.forceChannel - 1));

    std::lock_guard<std::mutex> lk(mtx_);
    // Running status : on ne réécrit le statut que s'il a CHANGÉ dans le flux
    // fusionné. Une source seule garde donc son running status (le flux ne
    // grossit pas) ; deux sources qui alternent le voient réinséré — sans quoi
    // les données de l'une seraient lues sous le statut de l'autre.
    const bool voice = head >= 0x80 && head < 0xF0;
    const bool omitStatus = voice && head == lastStatus_ && len > 1;
    const std::size_t need = omitStatus ? std::size_t(len - 1) : std::size_t(len);

    // Saturé : c'est le MESSAGE NEUF ENTIER qui tombe. Jeter un fragment
    // laisserait des octets orphelins dans le flux — le pire des deux maux, et
    // c'est aussi la règle du 6850 en overrun (garder l'ancien, cf. MidiAcia).
    if (jitter_.size() + need > kMaxJitter) {
        dropped_.fetch_add(uint64_t(len), std::memory_order_relaxed);
        return;
    }
    if (!omitStatus) jitter_.push_back(head);
    for (int i = 1; i < len; ++i) jitter_.push_back(msg[i]);

    // Suivi du running status du flux : seuls les messages de VOIE l'établissent ;
    // le système commun ($F0-$F7) l'ANNULE ; le temps réel ($F8-$FF) le laisse
    // intact — c'est précisément pour ça qu'il peut tomber n'importe où.
    if (voice)             lastStatus_ = head;
    else if (head < 0xF8)  lastStatus_ = 0;

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

MidiInHost::Device* MidiInHost::deviceForTest(int slot, int forceChannel) {
    while (int(devices_.size()) <= slot) {
        auto d = std::make_unique<Device>();
        d->owner = this;
        d->name = "test:" + std::to_string(devices_.size());
        devices_.push_back(std::move(d));
    }
    devices_[std::size_t(slot)]->forceChannel = forceChannel;
    return devices_[std::size_t(slot)].get();
}

void MidiInHost::pushForTest(int slot, const uint8_t* data, std::size_t n, int forceChannel) {
    feed(*deviceForTest(slot, forceChannel), data, n);
}

std::vector<neost::midi::Endpoint> MidiInHost::openEndpoints() const {
    std::vector<neost::midi::Endpoint> out;
    out.reserve(devices_.size());
    for (const auto& d : devices_) out.push_back({d->name, d->uid});
    return out;
}

std::vector<std::string> MidiInHost::openNames() const {
    std::vector<std::string> out;
    out.reserve(devices_.size());
    for (const auto& d : devices_) out.push_back(d->name);
    return out;
}

// -----------------------------------------------------------------------------
//  Énumération des sources
// -----------------------------------------------------------------------------
#if defined(__APPLE__) && !defined(NEOST_MIDI_ALSA)
namespace {
// kMIDIPropertyUniqueID : STABLE d'un branchement à l'autre. Seul critère qui sépare
// deux claviers du même modèle, qui portent rigoureusement le même nom.
std::string uniqueId(MIDIObjectRef obj) {
    SInt32 uid = 0;
    if (MIDIObjectGetIntegerProperty(obj, kMIDIPropertyUniqueID, &uid) != noErr) return {};
    return std::to_string(long(uid));
}

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

std::vector<neost::midi::Endpoint> MidiInHost::sources() {
    std::vector<neost::midi::Endpoint> out;
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
            // uid vide : ALSA n'a pas d'identifiant stable (cf. MidiEndpoint.hpp).
            out.push_back({std::string(snd_seq_client_info_get_name(ci)) + ": "
                           + snd_seq_port_info_get_name(pi), std::string()});
        }
    }
    snd_seq_port_info_free(pi); snd_seq_client_info_free(ci);
    snd_seq_close(seq);
#elif defined(__APPLE__)
    const ItemCount n = MIDIGetNumberOfSources();
    for (ItemCount i = 0; i < n; ++i) {
        const MIDIEndpointRef e = MIDIGetSource(i);
        std::string nm = displayName(e);
        // « NeoST MIDI OUT » est NOTRE propre source virtuelle : la proposer en entrée
        // offrirait un bouclage OUT→IN déguisé, avec le larsen que la fiche de bouclage
        // débranchée par défaut cherche précisément à éviter.
        if (!nm.empty() && nm.rfind("NeoST", 0) != 0) out.push_back({std::move(nm), uniqueId(e)});
    }
#endif
    return out;
}

// -----------------------------------------------------------------------------
//  Ouverture / fermeture
// -----------------------------------------------------------------------------
#if defined(__APPLE__) && !defined(NEOST_MIDI_ALSA)
// Callback CoreMIDI : thread temps réel du serveur MIDI. srcConnRefCon identifie
// LA SOURCE (posé à la connexion) — c'est ce qui permet à chaque appareil d'avoir
// son propre décodeur, donc à la fusion de se faire aux frontières de messages.
void MidiInHost::coreMidiRead(const ::MIDIPacketList* pkts, void* refCon, void* srcRef) {
    auto* self = static_cast<MidiInHost*>(refCon);
    auto* dev  = static_cast<Device*>(srcRef);
    if (!self || !dev || !pkts) return;
    const MIDIPacket* p = &pkts->packet[0];
    for (UInt32 i = 0; i < pkts->numPackets; ++i) {
        self->feed(*dev, p->data, p->length);
        p = MIDIPacketNext(p);
    }
}
#endif

std::size_t MidiInHost::setDevices(const std::vector<Want>& want) {
    close();
    if (want.empty()) return 0;

    // Appariement AVANT ouverture : identifiant d'abord, nom ensuite, et jamais deux
    // fois le même point (cf. MidiEndpoint.hpp). Deux claviers du même modèle sont
    // ainsi ouverts chacun sur le sien, au lieu que le premier soit pris deux fois.
    const auto have = sources();
    std::vector<neost::midi::Wanted> keys;
    keys.reserve(want.size());
    for (const Want& w : want) keys.push_back({w.name, w.uid});
    const std::vector<int> pick = neost::midi::matchEndpoints(keys, have);
#if defined(NEOST_MIDI_ALSA)
    snd_seq_t* seq = nullptr;
    if (snd_seq_open(&seq, "default", SND_SEQ_OPEN_DUPLEX, 0) < 0) {
        std::fprintf(stderr, "[midi-in] ALSA sequencer unavailable\n");
        return 0;
    }
    snd_seq_set_client_name(seq, "NeoST");
    // UN port d'écoute pour toutes les sources : la fusion se fait chez nous, et
    // l'événement ALSA porte son adresse d'origine (ev->source) — de quoi retrouver
    // le décodeur de chaque appareil.
    const int port = snd_seq_create_simple_port(
        seq, "NeoST MIDI IN",
        SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
        SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
    if (port < 0) { snd_seq_close(seq); return 0; }
    snd_midi_event_t* dec = nullptr;
    if (snd_midi_event_new(1024, &dec) < 0) {
        snd_seq_delete_simple_port(seq, port); snd_seq_close(seq);
        return 0;
    }
    // no_status(1) : chaque message ressort avec son statut. La compaction du
    // running status se fait ensuite, sur le flux FUSIONNÉ (cf. emitMessage).
    snd_midi_event_no_status(dec, 1);

    snd_seq_client_info_t* ci = nullptr; snd_seq_port_info_t* pi = nullptr;
    snd_seq_client_info_malloc(&ci); snd_seq_port_info_malloc(&pi);
    for (std::size_t wi = 0; wi < want.size(); ++wi) {
        if (pick[wi] < 0) continue;
        const Want& w = want[wi];
        const std::string target = have[std::size_t(pick[wi])].name;
        int rank = 0, seen = 0;
        for (int e = 0; e < pick[wi]; ++e) if (have[std::size_t(e)].name == target) ++rank;
        int foundC = -1, foundP = -1;
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
                    + snd_seq_port_info_get_name(pi) == target && seen++ == rank) {
                    foundC = cid; foundP = snd_seq_port_info_get_port(pi);
                    break;
                }
            }
        }
        if (foundC < 0 || snd_seq_connect_from(seq, port, foundC, foundP) < 0) continue;
        auto d = std::make_unique<Device>();
        d->owner = this; d->name = target; d->forceChannel = w.forceChannel;
        d->src = (uint32_t(foundC) << 8 | uint32_t(foundP)) + 1;
        devices_.push_back(std::move(d));
        std::fprintf(stderr, "[midi-in] \"%s\" -> MIDI IN (ALSA %d:%d)%s\n",
                     target.c_str(), foundC, foundP,
                     w.forceChannel ? " channelized" : "");
    }
    snd_seq_port_info_free(pi); snd_seq_client_info_free(ci);
    if (devices_.empty()) {
        snd_midi_event_free(dec);
        snd_seq_delete_simple_port(seq, port); snd_seq_close(seq);
        return 0;
    }
    seq_ = seq; dec_ = dec; port_ = uint32_t(port) + 1;
    stop_ = false;
    reader_ = std::thread([this] { readerLoop(); });
    return devices_.size();
#elif defined(__APPLE__)
    MIDIClientRef client = 0;
    if (const OSStatus st = MIDIClientCreate(CFSTR("NeoST IN"), nullptr, nullptr, &client); st != noErr) {
        std::fprintf(stderr, "[midi-in] MIDIClientCreate failed (OSStatus %d): no CoreMIDI "
                     "access from this process?\n", int(st));
        return 0;
    }
    MIDIPortRef port = 0;
    if (MIDIInputPortCreate(client, CFSTR("NeoST IN"), &MidiInHost::coreMidiRead, this, &port) != noErr) {
        MIDIClientDispose(client);
        std::fprintf(stderr, "[midi-in] cannot create the CoreMIDI input port\n");
        return 0;
    }
    client_ = client; port_ = port;
    const ItemCount nsrc = MIDIGetNumberOfSources();
    for (std::size_t wi = 0; wi < want.size(); ++wi) {
        if (pick[wi] < 0) continue;        // débranché : l'appelant re-tentera
        const Want& w = want[wi];
        const auto& ep = have[std::size_t(pick[wi])];
        // Retrouvé par son IDENTIFIANT : c'est le seul lien fiable entre la liste
        // rendue par sources() (qui saute les noms vides et les nôtres) et CoreMIDI.
        MIDIEndpointRef found = 0;
        for (ItemCount i = 0; i < nsrc; ++i) {
            const MIDIEndpointRef e = MIDIGetSource(i);
            if (uniqueId(e) == ep.uid && displayName(e) == ep.name) { found = e; break; }
        }
        if (!found) continue;
        auto d = std::make_unique<Device>();
        d->owner = this; d->name = ep.name; d->uid = ep.uid;
        d->forceChannel = w.forceChannel; d->src = found;
        // refCon de la CONNEXION = l'appareil : le callback saura de qui vient
        // chaque paquet, donc quel décodeur alimenter.
        if (MIDIPortConnectSource(port, found, d.get()) != noErr) {
            std::fprintf(stderr, "[midi-in] cannot connect \"%s\"\n", w.name.c_str());
            continue;
        }
        std::fprintf(stderr, "[midi-in] \"%s\" -> MIDI IN (CoreMIDI uid %s)%s\n",
                     ep.name.c_str(), ep.uid.c_str(),
                     w.forceChannel ? " channelized" : "");
        devices_.push_back(std::move(d));
    }
    if (devices_.empty()) { closeBackend(); return 0; }
    return devices_.size();
#else
    (void)want;
    return 0;
#endif
}

void MidiInHost::closeBackend() {
#if defined(NEOST_MIDI_ALSA)
    if (reader_.joinable()) { stop_ = true; reader_.join(); }
    if (seq_) {
        snd_seq_t* seq = static_cast<snd_seq_t*>(seq_);
        for (const auto& d : devices_)
            if (port_ && d->src)
                snd_seq_disconnect_from(seq, int(port_) - 1,
                                        int((d->src - 1) >> 8), int((d->src - 1) & 0xFF));
        if (port_) snd_seq_delete_simple_port(seq, int(port_) - 1);
        snd_seq_close(seq); seq_ = nullptr;
    }
    if (dec_) { snd_midi_event_free(static_cast<snd_midi_event_t*>(dec_)); dec_ = nullptr; }
#elif defined(__APPLE__)
    if (port_) {
        for (const auto& d : devices_)
            if (d->src) MIDIPortDisconnectSource(port_, d->src);
        MIDIPortDispose(port_);
    }
    if (client_) MIDIClientDispose(client_);
#endif
    client_ = port_ = 0;
}

void MidiInHost::close() {
    closeBackend();
    devices_.clear();
    std::lock_guard<std::mutex> lk(mtx_);
    jitter_.clear();     // un appareil débranché ne doit pas rejouer son passé
    lastStatus_ = 0;
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
        // Timeout de 100 ms : c'est ce qui rend `stop_` efficace — un
        // snd_seq_event_input bloquant ne s'interrompt pas proprement.
        if (::poll(pfds.data(), nfds_t(pfds.size()), 100) <= 0) continue;
        snd_seq_event_t* ev = nullptr;
        while (!stop_ && snd_seq_event_input(seq, &ev) >= 0 && ev) {
            // Retrouve l'appareil par l'adresse d'origine de l'événement : chaque
            // source a son décodeur, sans quoi la fusion mélangerait des octets.
            const uint32_t key = (uint32_t(ev->source.client) << 8 | ev->source.port) + 1;
            Device* dev = nullptr;
            for (const auto& d : devices_) if (d->src == key) { dev = d.get(); break; }
            if (dev) {
                uint8_t buf[256];
                const long n = snd_midi_event_decode(dec, buf, sizeof buf, ev);
                if (n > 0) feed(*dev, buf, std::size_t(n));
            }
            if (snd_seq_event_input_pending(seq, 0) <= 0) break;
        }
    }
#endif
}
