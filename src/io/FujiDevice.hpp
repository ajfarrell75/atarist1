// =============================================================================
//  FujiDevice.hpp — Périphérique FujiNet virtuel (extension NeoST).
//
//  FujiNet est un périphérique WiFi (ESP32) du monde rétro qui fait du DÉPORT
//  DE PROTOCOLE : la machine n'a pas de pile TCP/IP, elle envoie de petites
//  commandes ({device, commande, aux1, aux2, direction, longueur} + payload) et
//  le périphérique fait le travail (HTTP, TCP, montage d'images…). Aucun
//  FujiNet matériel n'existe pour l'Atari ST : NeoST définit ici le binding ST
//  de référence, sur le port ACSI (opcode vendeur $60) — spécification complète
//  dans docs/EXTENSIONS.md. Hatari n'a AUCUN équivalent (divergence assumée,
//  cf. docs/HATARI_DIVERGENCES.md) ; la fonctionnalité est INACTIVE par défaut.
//
//  Cette classe est la machine à états côté cœur : décodage des CDB, tampons
//  de réponse, slots d'hôtes/d'images. Elle ne touche JAMAIS au réseau — tout
//  passe par l'interface FujiHost (cf. net/FujiHost.hpp), posée par le frontend.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#pragma once
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "core/StateArchive.hpp"
#include "net/FujiHost.hpp"

class FujiDevice {
public:
    static constexpr uint8_t kAcsiOpcode = 0x60;   // opcode vendeur ACSI « FUJI »

    // IDs de périphériques virtuels (octet [2] du CDB — mêmes valeurs que le
    // firmware fujinet : $70 = Fuji, $71-$78 = N1:-N8:, $31-$38 = lecteurs).
    static constexpr uint8_t kDevFuji     = 0x70;
    static constexpr uint8_t kDevNetFirst = 0x71;
    static constexpr uint8_t kDevNetLast  = 0x78;

    static constexpr int kHostSlots   = 8;
    static constexpr int kDeviceSlots = 8;
    static constexpr int kHostSlotLen = 32;   // longueur d'un slot d'hôte (A8)
    static constexpr int kFileLen     = 36;   // nom de fichier d'un device slot (A8)

    // Backend hôte (réseau). nullptr = hors ligne (équivalent FujiHostNull).
    void setHost(FujiHost* h) { host_ = h; }
    FujiHost* host() const { return host_; }

    // Callbacks de montage, câblés par Machine::enableFujiNet : reçoivent un
    // chemin de CACHE LOCAL (image déjà téléchargée par le backend).
    std::function<bool(const std::string& path)> mountFloppyA;
    std::function<bool(const std::string& path)> mountHardDisk;

    // Reset de la commande $FF (le périphérique lui-même — PAS lié au reset de
    // la machine émulée : un FujiNet réel survit au reboot de son hôte).
    void reset();

    // --- Exécution d'un CDB (10 octets, cf. docs/EXTENSIONS.md) -----------------
    // Renvoie le statut ACSI (0 = OK, 2 = erreur). Après l'appel :
    //   dir=1 (device→ST) : dataLen()>0 et readBuffer() prêt (complété à 512) ;
    //   dir=2 (ST→device) : dataLen()>0, l'appelant DOIT livrer via writeData().
    int execute(const uint8_t* cdb);
    int  dataLen() const { return dataLen_; }
    bool isWrite() const { return awaitingWrite_; }
    const uint8_t* readBuffer() const { return buf_.data(); }
    // Livraison du payload ST→device (phase DMA write). Renvoie le statut ACSI.
    int writeData(const uint8_t* src, int len);

    uint8_t lastError() const { return lastError_; }   // dernier code FN_ERR

    // Montage direct d'une URL (CLI/GUI : --fujinet-host, panneau Network).
    // Télécharge puis monte en lecteur A (image disquette) ou disque dur.
    bool mountRemote(const std::string& url);

    // Slots d'hôtes (préfixes d'URL) — CLI/GUI + commandes $F3/$F4.
    void setHostSlot(int slot, const std::string& url);
    std::string hostSlot(int slot) const;

    bool enabled() const { return enabled_; }
    void setEnabled(bool on) { enabled_ = on; }

    // Save-state : état RUNTIME du protocole uniquement. Les canaux réseau du
    // BACKEND ne survivent pas à un load (comme les handles du HD GEMDOS) : le
    // ST devra rouvrir ses canaux (Status → error, puis Open), ce que fait déjà
    // tout client FujiNet robuste après une coupure WiFi.
    void serialize(StateArchive& ar) {
        ar.arr(hostSlots_);
        for (auto& ds : devSlots_) { ar(ds.hostSlot); ar(ds.mode); ar.arr(ds.file); }
        ar(lastError_);
        ar(awaitingWrite_); ar.arr(pendingCdb_);
        ar(dataLen_);
        ar.check(dataLen_ >= 0 && dataLen_ <= kMaxTransfer,
                 "FujiDevice::dataLen_ hors [0,64K]");
        if (ar.loading()) buf_.assign(std::size_t(kMaxTransfer), 0);
    }

private:
    static constexpr int kMaxTransfer = 65536;   // 128 secteurs de 512 octets

    struct DevSlot {
        uint8_t hostSlot = 0;
        uint8_t mode     = 0;
        char    file[kFileLen] = {0};
    };

    FujiHost* host_ = nullptr;
    bool enabled_ = false;

    char    hostSlots_[kHostSlots * kHostSlotLen] = {0};
    DevSlot devSlots_[kDeviceSlots];

    std::vector<uint8_t> buf_;          // tampon de réponse / de réception
    int     dataLen_ = 0;               // longueur DMA (complétée à 512)
    bool    awaitingWrite_ = false;     // dir=2 : payload ST→device attendu
    uint8_t pendingCdb_[10] = {0};      // CDB de la commande en attente de payload
    uint8_t lastError_ = fn_err::OK;

    // Prépare un tampon de réponse de `len` octets utiles (dataLen_ = complété
    // à 512, zéro-rempli au-delà) et renvoie le pointeur d'écriture.
    uint8_t* prepResp(int len);
    static int pad512(int len) { return (len + 511) & ~511; }

    int doFujiCommand(const uint8_t* cdb);           // device $70
    int doNetCommand(int chan, const uint8_t* cdb);  // devices $71-$78
    int finishWrite(const uint8_t* cdb, const uint8_t* payload, int payloadLen);
    int mountImage(int devSlot);
    std::string urlForSlot(const DevSlot& ds) const;
    int ok() { lastError_ = fn_err::OK; return 0; }
    int fail(uint8_t err) { lastError_ = err; return 2; }
    void trace(const char* fmt, ...) const;
};
