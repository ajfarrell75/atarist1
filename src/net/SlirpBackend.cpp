// =============================================================================
//  SlirpBackend.cpp — cf. SlirpBackend.hpp.
//
//  Boucle d'événements : libslirp ne tourne pas tout seul. À chaque trame émulée
//  on lui demande quelles sockets surveiller (slirp_pollfds_fill_socket), on
//  interroge l'OS avec un poll() à timeout NUL (le thread d'émulation ne doit
//  JAMAIS bloquer — une trame ST dure 20 ms et le son est produit derrière), puis
//  on lui rend les résultats (slirp_pollfds_poll). Les trames qu'il veut envoyer
//  à l'invité arrivent par le callback send_packet et attendent dans rxQueue_ que
//  la NE2000 vienne les chercher.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "net/SlirpBackend.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef NEOST_WITH_SLIRP
#include <poll.h>
#include <arpa/inet.h>
#include <libslirp.h>
#endif

namespace {
void strace(const char* what, const std::string& detail = {}) {
    static const bool on = std::getenv("NEOST_SLIRP_TRACE") != nullptr;
    if (on) std::fprintf(stderr, "[slirp] %s %s\n", what, detail.c_str());
}
// Origine des temps : l'horloge rendue à libslirp DOIT partir de ~0.
// Il fixe l'expiration d'une socket avec son `curtime` interne (encore nul quand
// la socket naît, avant tout poll) puis la compare à clock_get_ns()/1e6 : avec le
// temps depuis le démarrage de la machine, toute socket UDP naissait « expirée
// depuis des jours » et était détruite au premier tour — rien ne sortait jamais.
const std::chrono::steady_clock::time_point kEpoch = std::chrono::steady_clock::now();
int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - kEpoch).count();
}
} // namespace

SlirpBackend::SlirpBackend() = default;
SlirpBackend::~SlirpBackend() { close(); }

bool SlirpBackend::available() {
#ifdef NEOST_WITH_SLIRP
    return true;
#else
    return false;
#endif
}

// -----------------------------------------------------------------------------
//  Callbacks libslirp
// -----------------------------------------------------------------------------
ssize_t SlirpBackend::cbSendPacket(const void* buf, size_t len, void* opaque) {
    auto* self = static_cast<SlirpBackend*>(opaque);
    const auto* p = static_cast<const uint8_t*>(buf);
    // File bornée : si l'ST ne vient pas chercher ses trames (pilote arrêté), on
    // jette les PLUS ANCIENNES — c'est ce que fait un vrai tampon de réception,
    // et TCP retransmettra. Sans borne, une session oubliée gonflerait sans fin.
    if (self->rxQueue_.size() >= kRxQueueMax) self->rxQueue_.pop_front();
    self->rxQueue_.emplace_back(p, p + len);
    ++self->rxFrames_;
    strace("slirp->guest", std::to_string(len) + " octets");
    return ssize_t(len);
}

void SlirpBackend::cbGuestError(const char* msg, void* /*opaque*/) {
    // Faute de l'invité (trame malformée…) : jamais fatal, on trace seulement.
    strace("guest error:", msg ? msg : "?");
}

int64_t SlirpBackend::cbClockNs(void* /*opaque*/) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now() - kEpoch).count();   // cf. kEpoch
}

void* SlirpBackend::cbTimerNewOpaque(int id, void* cbOpaque, void* opaque) {
    auto* self = static_cast<SlirpBackend*>(opaque);
    auto* t = new Timer{id, cbOpaque, 0, false};
    self->timers_.push_back(t);
    return t;
}

void SlirpBackend::cbTimerFree(void* timer, void* opaque) {
    auto* self = static_cast<SlirpBackend*>(opaque);
    auto* t = static_cast<Timer*>(timer);
    for (auto it = self->timers_.begin(); it != self->timers_.end(); ++it)
        if (*it == t) { self->timers_.erase(it); break; }
    delete t;
}

void SlirpBackend::cbTimerMod(void* timer, int64_t expireMs, void* /*opaque*/) {
    auto* t = static_cast<Timer*>(timer);
    t->expireMs = expireMs;
    t->armed = true;
}

void SlirpBackend::cbNotify(void* /*opaque*/) {
    // Pas de thread d'I/O séparé : poll() repasse à chaque trame, rien à réveiller.
}

// -----------------------------------------------------------------------------
//  Cycle de vie
// -----------------------------------------------------------------------------
bool SlirpBackend::open(bool restricted) {
#ifdef NEOST_WITH_SLIRP
    close();
    static const SlirpCb cbs = [] {
        SlirpCb c{};
        c.send_packet      = &SlirpBackend::cbSendPacket;
        c.guest_error      = &SlirpBackend::cbGuestError;
        c.clock_get_ns     = &SlirpBackend::cbClockNs;
        c.timer_free       = &SlirpBackend::cbTimerFree;
        c.timer_mod        = &SlirpBackend::cbTimerMod;
        c.notify           = &SlirpBackend::cbNotify;
        // ⚠ NON optionnels malgré leur statut « deprecated » : libslirp les appelle
        // SANS vérifier qu'ils sont non nuls, dès qu'une socket hôte est créée —
        // c'est-à-dire dès le premier paquet SORTANT (ARP et DHCP, eux, sont servis
        // en interne, d'où un crash qui n'apparaissait qu'en ligne). No-ops : notre
        // poll() réinterroge SLIRP à chaque trame, il n'y a rien à enregistrer.
        c.register_poll_fd   = [](int, void*) {};
        c.unregister_poll_fd = [](int, void*) {};
        c.init_completed     = [](Slirp*, void*) {};
        // timer_new_opaque (version ≥ 4) plutôt que timer_new : c'est la forme
        // recommandée, et elle nous rend l'id à repasser à slirp_handle_timer.
        c.timer_new_opaque = [](SlirpTimerId id, void* cbOpaque, void* opaque) -> void* {
            return SlirpBackend::cbTimerNewOpaque(int(id), cbOpaque, opaque);
        };
        return c;
    }();

    SlirpConfig cfg{};
    cfg.version    = 4;                       // suffisant : timer_new_opaque + init_completed
    cfg.restricted = restricted ? 1 : 0;
    cfg.in_enabled = true;
    inet_pton(AF_INET, "10.0.2.0",     &cfg.vnetwork);
    inet_pton(AF_INET, "255.255.255.0", &cfg.vnetmask);
    inet_pton(AF_INET, "10.0.2.2",     &cfg.vhost);        // passerelle
    inet_pton(AF_INET, "10.0.2.15",    &cfg.vdhcp_start);  // 1re adresse distribuée
    inet_pton(AF_INET, "10.0.2.3",     &cfg.vnameserver);  // DNS relayé
    cfg.vhostname  = "neost";
    cfg.in6_enabled = false;                  // l'ST n'a pas de pile IPv6

    Slirp* s = slirp_new(&cfg, &cbs, this);
    if (!s) {
        error_ = "slirp_new a échoué";
        return false;
    }
    slirp_ = s;
    txFrames_ = rxFrames_ = 0;
    std::fprintf(stderr, "[slirp] NAT user-mode démarré — ST %s/%s, passerelle %s, DNS %s%s\n",
                 guestIp(), netmask(), gatewayIp(), nameserver(),
                 restricted ? " (RESTREINT : pas de sortie)" : "");
    return true;
#else
    (void)restricted;
    error_ = "ce build n'a pas libslirp (NEOST_WITH_SLIRP=OFF)";
    return false;
#endif
}

void SlirpBackend::close() {
#ifdef NEOST_WITH_SLIRP
    if (slirp_) {
        slirp_cleanup(static_cast<Slirp*>(slirp_));
        slirp_ = nullptr;
    }
#endif
    for (Timer* t : timers_) delete t;
    timers_.clear();
    rxQueue_.clear();
}

// -----------------------------------------------------------------------------
//  Trames
// -----------------------------------------------------------------------------
void SlirpBackend::send(const uint8_t* frame, int len) {
#ifdef NEOST_WITH_SLIRP
    if (!slirp_ || len <= 0) return;
    ++txFrames_;
    strace("guest->slirp", std::to_string(len) + " octets, ethertype " +
           std::to_string(len >= 14 ? (frame[12] << 8 | frame[13]) : 0));
    slirp_input(static_cast<Slirp*>(slirp_), frame, len);
#else
    (void)frame; (void)len;
#endif
}

bool SlirpBackend::recv(std::vector<uint8_t>& out) {
    if (rxQueue_.empty()) return false;
    out = std::move(rxQueue_.front());
    rxQueue_.pop_front();
    return true;
}

void SlirpBackend::fireDueTimers() {
#ifdef NEOST_WITH_SLIRP
    if (!slirp_) return;
    const int64_t now = nowMs();
    // Copie : le callback peut créer/détruire des minuteries pendant l'itération.
    std::vector<Timer*> due;
    for (Timer* t : timers_)
        if (t->armed && t->expireMs <= now) due.push_back(t);
    for (Timer* t : due) {
        t->armed = false;
        slirp_handle_timer(static_cast<Slirp*>(slirp_), SlirpTimerId(t->id), t->cbOpaque);
    }
#endif
}

void SlirpBackend::poll() {
#ifdef NEOST_WITH_SLIRP
    if (!slirp_) return;
    auto* slirp = static_cast<Slirp*>(slirp_);

    // 1) SLIRP énumère ses sockets. On les accumule dans un tableau de pollfd ;
    //    l'index rendu à SLIRP est la position dans ce tableau.
    static std::vector<struct pollfd> fds;    // statique : réutilisé d'une trame à l'autre
    fds.clear();
    uint32_t timeout = 0;                     // on N'ATTEND PAS (cf. en-tête du fichier)
    // Deux API pour la MÊME chose : slirp_pollfds_fill_socket (libslirp >= 4.8,
    // SLIRP_CONFIG_VERSION_MAX >= 5) prend un slirp_os_socket, qui n'existe que
    // pour porter les SOCKET Windows — sur POSIX c'est un int, donc les deux
    // variantes sont identiques ici. On garde la classique en repli : sans elle,
    // NeoST ne compilait pas du tout sur les distributions en libslirp 4.7
    // (Debian 12 bookworm = Raspberry Pi OS, Ubuntu 24.04).
#if defined(SLIRP_CONFIG_VERSION_MAX) && SLIRP_CONFIG_VERSION_MAX >= 5
    using SlirpFd = slirp_os_socket;
    #define NEOST_SLIRP_FILL slirp_pollfds_fill_socket
#else
    using SlirpFd = int;
    #define NEOST_SLIRP_FILL slirp_pollfds_fill
#endif
    NEOST_SLIRP_FILL(slirp, &timeout,
        [](SlirpFd fd, int events, void* /*opaque*/) -> int {
            short ev = 0;
            if (events & SLIRP_POLL_IN)  ev |= POLLIN;
            if (events & SLIRP_POLL_OUT) ev |= POLLOUT;
            if (events & SLIRP_POLL_PRI) ev |= POLLPRI;
            strace("fill fd", std::to_string(int(fd)) + " events=" + std::to_string(events));
            fds.push_back(pollfd{int(fd), ev, 0});
            return int(fds.size()) - 1;
        }, this);
#undef NEOST_SLIRP_FILL

    if (!fds.empty()) strace("sockets surveillees:", std::to_string(fds.size()));
    // 2) Interrogation NON BLOQUANTE de l'OS.
    const int rc = fds.empty() ? 0 : ::poll(fds.data(), nfds_t(fds.size()), 0);
    if (rc > 0) strace("poll rc", std::to_string(rc) + " revents0=" + std::to_string(fds[0].revents));

    // 3) On rend les résultats. `select_error` non nul dit à SLIRP d'ignorer les
    //    revents (poll a échoué) — sans ça il lirait des drapeaux non initialisés.
    slirp_pollfds_poll(slirp, rc < 0 ? 1 : 0,
        [](int idx, void* /*opaque*/) -> int {
            if (idx < 0 || idx >= int(fds.size())) return 0;
            const short re = fds[std::size_t(idx)].revents;
            int events = 0;
            if (re & POLLIN)  events |= SLIRP_POLL_IN;
            if (re & POLLOUT) events |= SLIRP_POLL_OUT;
            if (re & POLLPRI) events |= SLIRP_POLL_PRI;
            if (re & POLLERR) events |= SLIRP_POLL_ERR;
            if (re & POLLHUP) events |= SLIRP_POLL_HUP;
            return events;
        }, this);

    // 4) Minuteries (retransmissions TCP, baux DHCP…) : SLIRP compte sur nous.
    fireDueTimers();
#endif
}
