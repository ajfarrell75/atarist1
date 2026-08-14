// =============================================================================
//  FujiHostLive.hpp — Backend FujiNet RÉEL : sockets hôte (NEOST_WITH_NET).
//
//  Chaque canal N: ouvert possède son état :
//    · HTTP : un thread de travail télécharge le corps dans le tampon du canal
//      (le ST interroge Status jusqu'à voir des octets — sémantique FujiNet).
//    · TCP  : connexion à l'open (bloquante, timeout 5 s), thread lecteur qui
//      remplit le tampon ; write() envoie directement.
//  Tout l'état partagé est sous mutex par canal — le cœur n'appelle ces
//  méthodes que depuis le thread d'émulation, les threads de travail ne font
//  qu'alimenter les tampons.
//
//  ⚠ Non déterministe par nature (réseau réel) : JAMAIS utilisé par les étalons
//  (--tier fast/full tournent sans réseau ; cf. FujiHostReplay).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "net/FujiHost.hpp"
#include "net/HttpClient.hpp"

class FujiHostLive final : public FujiHost {
public:
    FujiHostLive();
    ~FujiHostLive() override;

    const char* name() const override { return "live"; }
    uint8_t open(int chan, const std::string& spec, uint8_t mode, uint8_t trans) override;
    uint8_t close(int chan) override;
    int     read(int chan, uint8_t* dst, int len) override;
    uint8_t write(int chan, const uint8_t* src, int len) override;
    FujiChanStatus status(int chan) override;
    uint8_t jsonParse(int chan) override;
    uint8_t jsonQuery(int chan, const std::string& query) override;
    void    getTime(uint8_t out[7]) override;
    std::string fetchToFile(const std::string& url) override;
    void reset() override;

private:
    enum class Kind { None, Http, Tcp };

    struct Chan {
        std::mutex mtx;
        Kind kind = Kind::None;
        bool open = false;
        neonet::SocketHandle fd = neonet::kInvalidSocket; // TCP
        std::vector<uint8_t> buf;           // octets prêts pour le ST
        std::string jsonSrc;                // document figé par jsonParse
        std::atomic<bool> workerDone{true};
        std::atomic<bool> stop{false};
        std::thread worker;
        uint8_t lastError = fn_err::OK;
        bool connected = false;

        void joinWorker() {
            stop = true;
            if (worker.joinable()) worker.join();
            stop = false;
        }
    };

    Chan chan_[MAX_CHANNELS];
    std::string cacheDir_;

    Chan* at(int chan) { return (chan >= 0 && chan < MAX_CHANNELS) ? &chan_[chan] : nullptr; }
    void closeLocked(Chan& c);
};
