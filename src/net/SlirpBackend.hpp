// =============================================================================
//  SlirpBackend.hpp — Accès Internet RÉEL pour la NE2000 émulée (NetUSBee /
//  EtherNEC), par NAT en espace utilisateur (libslirp, le routeur virtuel de
//  QEMU — LGPL 2.1+).
//
//  Pourquoi SLIRP et pas un pont/pcap : aucun privilège requis (pas de root, pas
//  d'interface TAP, pas de capture brute), ça marche derrière n'importe quel
//  WiFi, et l'hôte reste protégé — l'ST est derrière un NAT, il ne peut pas être
//  joint depuis l'extérieur. C'est exactement ce que le TODO du projet appelait
//  « EtherNEC — backend réel : SlirpNat ».
//
//  Ce que l'ST voit, côté réseau (mêmes valeurs que QEMU, celles que tous les
//  guides rétro donnent) :
//
//      réseau   10.0.2.0/24        passerelle  10.0.2.2
//      DNS      10.0.2.3           l'ST (DHCP) 10.0.2.15
//
//  Il faut donc, CÔTÉ ST, une pile TCP/IP (STinG + le pilote ENEC.STX, ou
//  MiNTnet) : SLIRP route de l'IP, la NE2000 ne transporte que des trames
//  Ethernet. SLIRP fournit lui-même DHCP, DNS-relais et le routage — rien à
//  configurer côté hôte.
//
//  Sans libslirp au configure (NEOST_WITH_SLIRP absent), la classe compile en
//  coquille vide : `available()` est faux et `open()` échoue proprement.
//
//  ⚠ Thread : tout se passe sur le thread d'émulation (poll() est appelé une
//  fois par trame, comme les autres backends). Aucun thread créé ici.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstddef>
#include <cstdint>
#include <sys/types.h>   // ssize_t (signature de SlirpWriteCb)
#include <deque>
#include <string>
#include <vector>

#include "net/NetBackend.hpp"

class SlirpBackend final : public NetBackend {
public:
    SlirpBackend();
    ~SlirpBackend() override;

    static bool available();                 // compilé avec libslirp ?

    // Démarre le routeur virtuel. `restricted` coupe tout accès sortant (mode
    // bac à sable : seuls DHCP/DNS internes répondent). Renvoie false et
    // renseigne lastError() en cas d'échec.
    bool open(bool restricted = false);
    void close();
    bool isOpen() const { return slirp_ != nullptr; }
    const std::string& lastError() const { return error_; }

    // Adresses distribuées à l'invité (pour l'affichage GUI / la doc).
    static const char* guestIp()    { return "10.0.2.15"; }
    static const char* gatewayIp()  { return "10.0.2.2"; }
    static const char* nameserver() { return "10.0.2.3"; }
    static const char* netmask()    { return "255.255.255.0"; }

    // --- NetBackend ---------------------------------------------------------
    const char* name() const override { return "slirp (user-mode NAT)"; }
    void send(const uint8_t* frame, int len) override;   // ST → Internet
    bool recv(std::vector<uint8_t>& out) override;       // Internet → ST
    void poll() override;                                // une fois par trame

    // Compteurs (diagnostic GUI) : trames vues dans chaque sens.
    uint64_t txFrames() const { return txFrames_; }
    uint64_t rxFrames() const { return rxFrames_; }

private:
    void*       slirp_ = nullptr;            // Slirp* (opaque : pas d'include ici)
    std::string error_;
    uint64_t    txFrames_ = 0, rxFrames_ = 0;

    // File des trames que SLIRP nous rend (callback send_packet → recv()).
    std::deque<std::vector<uint8_t>> rxQueue_;
    static constexpr std::size_t kRxQueueMax = 256;   // au-delà : on jette les plus vieilles

    // Callbacks libslirp (définis dans le .cpp ; `opaque` = this).
    static ssize_t cbSendPacket(const void* buf, size_t len, void* opaque);
    static void    cbGuestError(const char* msg, void* opaque);
    static int64_t cbClockNs(void* opaque);
    static void*   cbTimerNewOpaque(int id, void* cbOpaque, void* opaque);
    static void    cbTimerFree(void* timer, void* opaque);
    static void    cbTimerMod(void* timer, int64_t expireMs, void* opaque);
    static void    cbNotify(void* opaque);

    // Minuteries demandées par SLIRP (retransmissions TCP, baux DHCP…).
    struct Timer { int id; void* cbOpaque; int64_t expireMs; bool armed; };
    std::vector<Timer*> timers_;
    void fireDueTimers();

    // Tampons réutilisés par poll() (zéro allocation en régime établi).
    std::vector<void*> pollFds_;             // pollfd[] (opaque : évite <poll.h> ici)
};
