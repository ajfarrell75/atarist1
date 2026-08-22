// =============================================================================
//  Socket.hpp — Aides sockets TCP/UDP bloquantes, partagées par les extensions
//  réseau (modem Hayes sur l'USART du MFP, anneau MIDI UDP).
//
//  Volontairement minimal : un handle portable, une connexion à timeout, et de
//  quoi émettre/recevoir sans bloquer indéfiniment. Aucun protocole applicatif
//  ici — c'est la seule couche que le reste du projet partage.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <limits>
#include <string>

namespace neonet {

#ifdef _WIN32
using SocketHandle = std::uintptr_t;
inline constexpr SocketHandle kInvalidSocket = std::numeric_limits<SocketHandle>::max();
#else
using SocketHandle = int;
inline constexpr SocketHandle kInvalidSocket = -1;
#endif

inline constexpr bool socketValid(SocketHandle fd) { return fd != kInvalidSocket; }

// Renvoie un handle valide ou kInvalidSocket. `timeoutMs` borne la connexion
// (la résolution DNS synchrone reste soumise au résolveur du système).
SocketHandle tcpConnect(const std::string& host, int port, int timeoutMs, std::string& err);
void sockClose(SocketHandle fd);
void sockShutdown(SocketHandle fd);                               // débloque recv/send SANS libérer le fd
int  sockSend(SocketHandle fd, const uint8_t* p, int n);          // -1 = erreur
int  sockRecv(SocketHandle fd, uint8_t* p, int n, int timeoutMs); // 0 = fermé, -1 = erreur, -2 = timeout
bool sockHasData(SocketHandle fd);                                // données prêtes (poll 0 ms)
void netInitOnce();                                               // WSAStartup sous Windows

} // namespace neonet
