// =============================================================================
//  FujiHostReplay.hpp — Backend FujiNet DÉTERMINISTE : rejoue un dossier de
//  fixtures. C'est le backend des étalons (tools/run_fujinet_tests.py) : aucune
//  E/S réseau, résultats identiques à chaque exécution.
//
//  Convention de fixtures : un fichier par devicespec, nommé par assainissement
//  (tout caractère hors [A-Za-z0-9._-] → '_'). Exemple :
//      N:HTTP://host/dir/file.txt  →  <dir>/HTTP___host_dir_file.txt
//  open() charge le fichier dans le tampon du canal ; read() le consomme ;
//  write() est accepté et journalisé (NEOST_FUJI_TRACE). fetchToFile applique
//  le même assainissement et renvoie le chemin du fichier de fixture lui-même.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <string>
#include <vector>

#include "net/FujiHost.hpp"

class FujiHostReplay final : public FujiHost {
public:
    explicit FujiHostReplay(std::string fixtureDir) : dir_(std::move(fixtureDir)) {}

    const char* name() const override { return "replay"; }
    uint8_t open(int chan, const std::string& spec, uint8_t mode, uint8_t trans) override;
    uint8_t close(int chan) override;
    int     read(int chan, uint8_t* dst, int len) override;
    uint8_t write(int chan, const uint8_t* src, int len) override;
    FujiChanStatus status(int chan) override;
    uint8_t jsonParse(int chan) override;
    uint8_t jsonQuery(int chan, const std::string& query) override;
    std::string fetchToFile(const std::string& url) override;
    void reset() override;

    static std::string sanitize(const std::string& spec);

private:
    struct Chan {
        bool open = false;
        std::vector<uint8_t> buf;    // données restantes (consommées par read)
        std::string jsonSrc;         // document chargé par jsonParse
        uint8_t lastError = fn_err::OK;
    };
    std::string dir_;
    Chan chan_[MAX_CHANNELS];

    Chan* at(int chan) { return (chan >= 0 && chan < MAX_CHANNELS) ? &chan_[chan] : nullptr; }
};
