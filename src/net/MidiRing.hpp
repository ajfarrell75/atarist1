// =============================================================================
//  MidiRing.hpp — Anneau MIDI réseau (MIDIMaze en ligne, extension NeoST).
//
//  MIDI Maze relie les ST en ANNEAU par câbles MIDI : le MIDI OUT de chaque
//  machine va au MIDI IN de la suivante. On transporte cet anneau sur UDP :
//  les octets MIDI OUT partent en datagrammes vers le pair « aval », et les
//  datagrammes reçus (du pair « amont ») entrent dans le MIDI IN local. C'est
//  exactement ce que fait FujiNet côté 8 bits (mozzwald/FujiNet-MIDIMaze).
//
//  Câblage (frontend) :
//    midi.setMidiSink([ring](uint8_t b){ ring->sendByte(b); });
//    // une fois par trame : ring->poll([&](uint8_t b){ midi.receiveExternal(b); });
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <string>

#include "net/HttpClient.hpp"

class MidiRing {
public:
    // `peer` = "hôte:port" du pair AVAL (où envoyer). `listenPort` = port UDP
    // local d'écoute (d'où arrive l'amont). Renvoie false si le socket échoue.
    bool open(const std::string& peer, int listenPort);
    void close();
    ~MidiRing();

    void sendByte(uint8_t b);                    // MIDI OUT → réseau (aval)
    // Reçoit les datagrammes de l'amont dans un tampon de gigue interne, puis en
    // livre autant que `accept` en absorbe (le 6850 ne tient que 2 octets → le
    // reste attend le prochain appel). `accept(b)` renvoie true si l'octet a été
    // pris. À appeler une fois par trame.
    void poll(const std::function<bool(uint8_t)>& accept);

    bool ok() const { return neonet::socketValid(fd_); }

private:
    neonet::SocketHandle fd_ = neonet::kInvalidSocket; // socket UDP
    // Adresse du pair aval (sockaddr_storage opaque pour ne pas fuir <netinet>).
    unsigned char peerAddr_[128] = {0};
    int  peerAddrLen_ = 0;
    std::deque<uint8_t> jitter_;                 // tampon de gigue MIDI IN
};
