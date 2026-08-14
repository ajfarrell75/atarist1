// =============================================================================
//  HttpClient.hpp — Client HTTP 1.1 minimal (GET/POST) pour le backend FujiNet.
//
//  Volontairement petit : sockets bloquants avec timeout, Connection: close,
//  décodage chunked, redirections 301/302/307/308 suivies (5 max). PAS de TLS —
//  https:// est refusé avec un message clair (mbedTLS optionnel viendra plus
//  tard, cf. docs/FUJINET.md § Limites).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <atomic>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace neonet {

#ifdef _WIN32
using SocketHandle = std::uintptr_t;
inline constexpr SocketHandle kInvalidSocket = std::numeric_limits<SocketHandle>::max();
#else
using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocket = -1;
#endif

inline constexpr bool socketValid(SocketHandle fd) { return fd != kInvalidSocket; }

struct Url {
    std::string scheme;   // "http", "tcp", "udp", …
    std::string host;
    int         port = 0;
    std::string path;     // avec le '/' initial (HTTP)
};

// Découpe "PROTO://host[:port]/chemin". Renvoie false si inanalysable.
bool parseUrl(const std::string& url, Url& out);

struct HttpResult {
    int status = 0;                  // code HTTP (0 = échec transport)
    std::vector<uint8_t> body;
    std::string error;               // message d'erreur transport (anglais, logs)
};

// GET/POST bloquant (timeout total ~30 s). `postBody` nul → GET.
// `cancel` non nul : abandon coopératif (vérifié ~1 s), sans corps partiel.
HttpResult httpFetch(const std::string& url,
                     const std::string* postBody = nullptr,
                     const std::vector<std::string>* headers = nullptr,
                     const std::atomic<bool>* cancel = nullptr);

// --- Aide sockets partagée (TCP) — utilisée aussi par FujiHostLive -----------
// Renvoie un handle valide ou kInvalidSocket. `timeoutMs` borne la connexion
// (la résolution DNS synchrone reste soumise au résolveur du système).
SocketHandle tcpConnect(const std::string& host, int port, int timeoutMs, std::string& err);
void sockClose(SocketHandle fd);
void sockShutdown(SocketHandle fd);                               // débloque recv/send SANS libérer le fd
int  sockSend(SocketHandle fd, const uint8_t* p, int n);          // -1 = erreur
int  sockRecv(SocketHandle fd, uint8_t* p, int n, int timeoutMs); // 0 = fermé, -1 = erreur, -2 = timeout
bool sockHasData(SocketHandle fd);                                // données prêtes (poll 0 ms)
void netInitOnce();                                      // WSAStartup sous Windows

} // namespace neonet
