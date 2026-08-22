// =============================================================================
//  HayesModem.hpp — Modem Hayes émulé sur l'USART MFP (RS-232).
//
//  Le grand débloqueur du logiciel d'époque : terminaux, BBS, et les piles
//  TCP/IP historiques (STiK/STinG en SLIP/PPP) parlent toutes à « un modem sur
//  le port série ». On interprète les commandes AT côté hôte et on ouvre de
//  vraies connexions TCP : `ATDT hote:port` → CONNECT, puis pont transparent
//  octets ↔ socket. C'est l'équivalent des modules « WiFi modem » ESP8266
//  vendus pour ST, qui se branchent sur le port série d'origine.
//
//  Câblage (frontend) : Mfp::setSerialSink → onTx (octets émis par l'ST) ;
//  les réponses/le flux entrant reviennent par Mfp::receiveByte (livraison
//  cadencée au débit série + IRQ RxFull — cf. Scheduler::SERIAL_RX). poll()
//  se lance une fois par trame pour pomper le TCP entrant. DCD suit la
//  porteuse (Mfp::setRs232Dcd).
//
//  Échappement « +++ » : trois '+' consécutifs suffisent (sans garde de
//  silence d'une seconde — simplification v1 documentée).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <string>

#include "io/Mfp.hpp"
#include "net/Socket.hpp"

class HayesModem {
public:
    explicit HayesModem(Mfp& mfp) : mfp_(mfp) {}
    ~HayesModem();

    void onTx(uint8_t b);       // octet écrit par l'ST dans l'UDR
    void poll();                // une fois par trame : pompe le TCP entrant
    bool connected() const { return neonet::socketValid(fd_); }
    void hangup(bool notify);

private:
    Mfp& mfp_;
    neonet::SocketHandle fd_ = neonet::kInvalidSocket;
    bool dataMode_ = false;
    bool echo_ = true;
    int  plusCount_ = 0;
    std::string cmd_;           // ligne AT en cours d'accumulation

    void sendStr(const std::string& s);       // → ST (ajoute CRLF)
    void execute(std::string line);
    void dial(const std::string& target);
};
