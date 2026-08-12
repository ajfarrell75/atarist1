// =============================================================================
//  FujiDevice.cpp — Périphérique FujiNet virtuel : décodage des commandes.
//
//  Le CDB de 10 octets (opcode vendeur ACSI $60, cf. docs/FUJINET.md) porte
//  l'abstraction de commande UNIVERSELLE du firmware fujinet, identique sur
//  tous ses bus (SIO, SmartPort, RS-232…) :
//      { device, commande, aux1, aux2, direction, longueur } + payload DMA.
//  Les IDs de devices et les octets de commande sont ceux du firmware amont
//  (wiki fujinet-firmware) — on n'invente que le TRANSPORT ACSI.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/FujiDevice.hpp"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace {
// Commandes du device Fuji ($70) — mêmes valeurs que le firmware (SIO).
constexpr uint8_t FUJI_RESET             = 0xFF;
constexpr uint8_t FUJI_GET_SSID          = 0xFE;
constexpr uint8_t FUJI_GET_WIFI_STATUS   = 0xFA;
constexpr uint8_t FUJI_MOUNT_HOST        = 0xF9;
constexpr uint8_t FUJI_READ_HOST_SLOTS   = 0xF4;
constexpr uint8_t FUJI_WRITE_HOST_SLOTS  = 0xF3;
constexpr uint8_t FUJI_READ_DEV_SLOTS    = 0xF2;
constexpr uint8_t FUJI_WRITE_DEV_SLOTS   = 0xF1;
constexpr uint8_t FUJI_MOUNT_IMAGE       = 0xF7;
constexpr uint8_t FUJI_UNMOUNT_IMAGE     = 0xE9;
constexpr uint8_t FUJI_GET_ADAPTER_CFG   = 0xE8;
constexpr uint8_t FUJI_GET_TIME          = 0xD2;

// Commandes des devices N: ($71-$78) — idem firmware.
constexpr uint8_t NET_OPEN        = 'O';
constexpr uint8_t NET_CLOSE       = 'C';
constexpr uint8_t NET_READ        = 'R';
constexpr uint8_t NET_WRITE       = 'W';
constexpr uint8_t NET_STATUS      = 'S';
constexpr uint8_t NET_ERROR       = 'E';
constexpr uint8_t NET_JSON_PARSE  = 'P';
constexpr uint8_t NET_JSON_QUERY  = 'Q';
constexpr uint8_t NET_TRANSLATE   = 'T';

// Directions (octet [6] du CDB).
constexpr uint8_t DIR_NONE  = 0;
constexpr uint8_t DIR_READ  = 1;   // device → ST
constexpr uint8_t DIR_WRITE = 2;   // ST → device

bool hasFloppyExt(const std::string& p) {
    const auto dot = p.find_last_of('.');
    if (dot == std::string::npos) return false;
    std::string e = p.substr(dot + 1);
    for (char& c : e) c = char(tolower(uint8_t(c)));
    return e == "st" || e == "msa" || e == "dim" || e == "stx";
}
} // namespace

void FujiDevice::trace(const char* fmt, ...) const {
    static const bool on = std::getenv("NEOST_FUJI_TRACE") != nullptr;
    if (!on) return;
    va_list ap;
    va_start(ap, fmt);
    std::fprintf(stderr, "[fuji] ");
    std::vfprintf(stderr, fmt, ap);
    std::fprintf(stderr, "\n");
    va_end(ap);
}

void FujiDevice::reset() {
    if (host_) host_->reset();
    dataLen_ = 0;
    awaitingWrite_ = false;
    lastError_ = fn_err::OK;
    trace("reset");
}

uint8_t* FujiDevice::prepResp(int len) {
    dataLen_ = pad512(std::max(len, 1));
    if ((int)buf_.size() < dataLen_) buf_.resize(std::size_t(dataLen_));
    std::memset(buf_.data(), 0, std::size_t(dataLen_));
    return buf_.data();
}

// -----------------------------------------------------------------------------
//  Point d'entrée : CDB complet reçu par la cible ACSI.
// -----------------------------------------------------------------------------
int FujiDevice::execute(const uint8_t* cdb) {
    dataLen_ = 0;
    awaitingWrite_ = false;

    const uint8_t device = cdb[2];
    const uint8_t dir    = cdb[6];
    const int     len    = (cdb[7] << 8) | cdb[8];

    trace("cdb dev=$%02X cmd=$%02X ('%c') aux=%02X/%02X dir=%d len=%d",
          device, cdb[3], (cdb[3] >= 0x20 && cdb[3] < 0x7F) ? cdb[3] : '.',
          cdb[4], cdb[5], dir, len);

    if (dir > DIR_WRITE || len > kMaxTransfer) return fail(fn_err::BAD_CMD);
    if (dir != DIR_NONE && len == 0)           return fail(fn_err::BAD_CMD);

    // dir=2 : on retient le CDB, l'action se joue à la livraison du payload
    // (writeData) — le statut lisible ENTRE les deux phases reste OK.
    if (dir == DIR_WRITE) {
        std::memcpy(pendingCdb_, cdb, 10);
        dataLen_ = pad512(len);
        if ((int)buf_.size() < dataLen_) buf_.resize(std::size_t(dataLen_));
        awaitingWrite_ = true;
        return 0;
    }

    if (device == kDevFuji)                                    return doFujiCommand(cdb);
    if (device >= kDevNetFirst && device <= kDevNetLast)       return doNetCommand(device - kDevNetFirst, cdb);
    return fail(fn_err::NO_DEVICE);
}

// Phase DMA write terminée : exécute la commande retenue avec son payload.
int FujiDevice::writeData(const uint8_t* src, int len) {
    awaitingWrite_ = false;
    const int useful = std::min(len, (pendingCdb_[7] << 8) | pendingCdb_[8]);
    return finishWrite(pendingCdb_, src, useful);
}

// -----------------------------------------------------------------------------
//  Device Fuji ($70)
// -----------------------------------------------------------------------------
int FujiDevice::doFujiCommand(const uint8_t* cdb) {
    const uint8_t cmd = cdb[3];
    switch (cmd) {
    case FUJI_RESET:
        reset();
        return ok();

    case FUJI_GET_WIFI_STATUS: {                 // 1 octet : 3 = connecté, 6 = déconnecté
        uint8_t* b = prepResp(1);
        b[0] = host_ ? 3 : 6;
        return ok();
    }

    case FUJI_GET_SSID: {                        // struct { char ssid[33]; char password[64]; }
        uint8_t* b = prepResp(97);
        std::snprintf(reinterpret_cast<char*>(b), 33, "%s", host_ ? host_->name() : "");
        return ok();
    }

    case FUJI_GET_ADAPTER_CFG: {                 // AdapterConfig (140 octets, cf. docs/FUJINET.md)
        uint8_t* b = prepResp(140);
        std::snprintf(reinterpret_cast<char*>(b), 32, "%s", host_ ? host_->name() : "");
        std::snprintf(reinterpret_cast<char*>(b + 32), 64, "neost");
        b[96] = 127; b[97] = 0; b[98] = 0; b[99] = 1;          // localIP
        b[112] = 0x02; b[113] = 0x4E; b[114] = 0x53; b[115] = 0x54;  // MAC « NST »
        std::snprintf(reinterpret_cast<char*>(b + 124), 15, "NeoST");
        return ok();
    }

    case FUJI_READ_HOST_SLOTS: {                 // 8 × 32 octets
        uint8_t* b = prepResp(sizeof(hostSlots_));
        std::memcpy(b, hostSlots_, sizeof(hostSlots_));
        return ok();
    }

    case FUJI_READ_DEV_SLOTS: {                  // 8 × 38 octets {hostSlot, mode, file[36]}
        uint8_t* b = prepResp(kDeviceSlots * (2 + kFileLen));
        for (int i = 0; i < kDeviceSlots; ++i) {
            uint8_t* p = b + i * (2 + kFileLen);
            p[0] = devSlots_[i].hostSlot;
            p[1] = devSlots_[i].mode;
            std::memcpy(p + 2, devSlots_[i].file, kFileLen);
        }
        return ok();
    }

    case FUJI_MOUNT_HOST:                        // aux1 = slot d'hôte — validation différée
        if (cdb[4] >= kHostSlots) return fail(fn_err::BAD_CMD);
        return ok();

    case FUJI_MOUNT_IMAGE:                       // aux1 = device slot, aux2 = mode
        return mountImage(cdb[4]);

    case FUJI_UNMOUNT_IMAGE:
        if (cdb[4] >= kDeviceSlots) return fail(fn_err::BAD_CMD);
        devSlots_[cdb[4]] = DevSlot{};
        return ok();

    case FUJI_GET_TIME: {                        // 7 octets {aH,aB,mois,jour,h,m,s}
        uint8_t* b = prepResp(7);
        if (host_) host_->getTime(b);
        return ok();
    }

    default:
        trace("fuji cmd $%02X not implemented", cmd);
        return fail(fn_err::BAD_CMD);
    }
}

// -----------------------------------------------------------------------------
//  Devices N: ($71-$78)
// -----------------------------------------------------------------------------
int FujiDevice::doNetCommand(int chan, const uint8_t* cdb) {
    const uint8_t cmd = cdb[3];
    const int     len = (cdb[7] << 8) | cdb[8];
    if (!host_) {
        // Hors ligne : Status répond proprement (canal mort), le reste échoue.
        if (cmd == NET_STATUS) {
            uint8_t* b = prepResp(4);
            b[0] = b[1] = b[2] = 0; b[3] = fn_err::OFFLINE;
            return ok();
        }
        return fail(fn_err::OFFLINE);
    }

    switch (cmd) {
    case NET_CLOSE: {
        const uint8_t e = host_->close(chan);
        return e == fn_err::OK ? ok() : fail(e);
    }

    case NET_READ: {
        uint8_t* b = prepResp(len);
        const int n = host_->read(chan, b, len);
        if (n < 0) return fail(fn_err::IO_ERROR);
        // Contrat FujiNet : on lit ≤ ce que Status annonce. Une lecture plus
        // longue que le disponible est une erreur (le client doit re-Status).
        if (n < len) { trace("chan %d short read %d/%d", chan, n, len); return fail(fn_err::IO_ERROR); }
        return ok();
    }

    case NET_STATUS: {                           // 4 octets {availB, availH, connected, error}
        const FujiChanStatus st = host_->status(chan);
        uint8_t* b = prepResp(4);
        b[0] = uint8_t(st.avail & 0xFF);
        b[1] = uint8_t(st.avail >> 8);
        b[2] = st.connected;
        b[3] = st.error;
        return ok();
    }

    case NET_ERROR: {                            // 1 octet : dernier FN_ERR du canal
        uint8_t* b = prepResp(1);
        b[0] = host_->status(chan).error;
        return ok();
    }

    case NET_JSON_PARSE: {
        const uint8_t e = host_->jsonParse(chan);
        return e == fn_err::OK ? ok() : fail(e);
    }

    case NET_TRANSLATE:                          // aux1 = mode — accepté, sans effet v1
        return ok();

    default:
        trace("net chan %d cmd $%02X not implemented", chan, cmd);
        return fail(fn_err::BAD_CMD);
    }
}

// dir=2 : payload livré — Open (devicespec), Write (données), JSON Query,
// écriture des slots.
int FujiDevice::finishWrite(const uint8_t* cdb, const uint8_t* payload, int payloadLen) {
    const uint8_t device = cdb[2];
    const uint8_t cmd    = cdb[3];

    if (device == kDevFuji) {
        switch (cmd) {
        case FUJI_WRITE_HOST_SLOTS:
            if (payloadLen < (int)sizeof(hostSlots_)) return fail(fn_err::BAD_CMD);
            std::memcpy(hostSlots_, payload, sizeof(hostSlots_));
            return ok();
        case FUJI_WRITE_DEV_SLOTS:
            if (payloadLen < kDeviceSlots * (2 + kFileLen)) return fail(fn_err::BAD_CMD);
            for (int i = 0; i < kDeviceSlots; ++i) {
                const uint8_t* p = payload + i * (2 + kFileLen);
                devSlots_[i].hostSlot = p[0];
                devSlots_[i].mode     = p[1];
                std::memcpy(devSlots_[i].file, p + 2, kFileLen);
                devSlots_[i].file[kFileLen - 1] = 0;
            }
            return ok();
        default:
            return fail(fn_err::BAD_CMD);
        }
    }

    if (device >= kDevNetFirst && device <= kDevNetLast) {
        const int chan = device - kDevNetFirst;
        if (!host_) return fail(fn_err::OFFLINE);
        switch (cmd) {
        case NET_OPEN: {
            // Payload = devicespec ASCIIZ, avec ou sans préfixe « Nx: ».
            std::string spec(reinterpret_cast<const char*>(payload),
                             strnlen(reinterpret_cast<const char*>(payload), std::size_t(payloadLen)));
            if (spec.size() >= 2 && (spec[0] == 'N' || spec[0] == 'n')) {
                const auto colon = spec.find(':');
                if (colon != std::string::npos && colon <= 2) spec.erase(0, colon + 1);
            }
            trace("chan %d open \"%s\" mode=%d trans=%d", chan, spec.c_str(), cdb[4], cdb[5]);
            const uint8_t e = host_->open(chan, spec, cdb[4], cdb[5]);
            return e == fn_err::OK ? ok() : fail(e);
        }
        case NET_WRITE: {
            const uint8_t e = host_->write(chan, payload, payloadLen);
            return e == fn_err::OK ? ok() : fail(e);
        }
        case NET_JSON_QUERY: {
            std::string q(reinterpret_cast<const char*>(payload),
                          strnlen(reinterpret_cast<const char*>(payload), std::size_t(payloadLen)));
            const uint8_t e = host_->jsonQuery(chan, q);
            return e == fn_err::OK ? ok() : fail(e);
        }
        default:
            return fail(fn_err::BAD_CMD);
        }
    }
    return fail(fn_err::NO_DEVICE);
}

// -----------------------------------------------------------------------------
//  Montage d'images
// -----------------------------------------------------------------------------
std::string FujiDevice::urlForSlot(const DevSlot& ds) const {
    std::string file(ds.file, strnlen(ds.file, kFileLen));
    if (file.find("://") != std::string::npos) return file;    // URL absolue
    std::string base = hostSlot(ds.hostSlot);
    if (base.empty()) return file;
    if (!base.empty() && base.back() != '/' && !file.empty() && file.front() != '/')
        base += '/';
    return base + file;
}

int FujiDevice::mountImage(int devSlot) {
    if (devSlot < 0 || devSlot >= kDeviceSlots) return fail(fn_err::BAD_CMD);
    if (!host_) return fail(fn_err::OFFLINE);
    const std::string url = urlForSlot(devSlots_[devSlot]);
    if (url.empty()) return fail(fn_err::BAD_CMD);
    const std::string local = host_->fetchToFile(url);
    if (local.empty()) { trace("mount slot %d: fetch failed (%s)", devSlot, url.c_str()); return fail(fn_err::IO_ERROR); }
    const bool isFloppy = hasFloppyExt(url) || hasFloppyExt(local);
    bool mounted = false;
    if (isFloppy && mountFloppyA)       mounted = mountFloppyA(local);
    else if (!isFloppy && mountHardDisk) mounted = mountHardDisk(local);
    trace("mount slot %d: %s -> %s (%s) %s", devSlot, url.c_str(), local.c_str(),
          isFloppy ? "floppy A" : "hard disk", mounted ? "OK" : "FAILED");
    return mounted ? ok() : fail(fn_err::IO_ERROR);
}

bool FujiDevice::mountRemote(const std::string& url) {
    if (!host_) return false;
    setHostSlot(0, url.substr(0, url.find_last_of('/')));
    DevSlot& ds = devSlots_[0];
    ds.hostSlot = 0;
    ds.mode = 0;
    std::snprintf(ds.file, kFileLen, "%s", url.c_str());   // URL absolue (tronquée si longue)
    // Le nom peut dépasser kFileLen : on passe DIRECTEMENT par l'URL complète.
    const std::string local = host_->fetchToFile(url);
    if (local.empty()) return false;
    const bool isFloppy = hasFloppyExt(url) || hasFloppyExt(local);
    if (isFloppy && mountFloppyA)  return mountFloppyA(local);
    if (!isFloppy && mountHardDisk) return mountHardDisk(local);
    return false;
}

void FujiDevice::setHostSlot(int slot, const std::string& url) {
    if (slot < 0 || slot >= kHostSlots) return;
    char* p = hostSlots_ + slot * kHostSlotLen;
    std::memset(p, 0, kHostSlotLen);
    std::snprintf(p, kHostSlotLen, "%s", url.c_str());
}

std::string FujiDevice::hostSlot(int slot) const {
    if (slot < 0 || slot >= kHostSlots) return {};
    const char* p = hostSlots_ + slot * kHostSlotLen;
    return std::string(p, strnlen(p, kHostSlotLen));
}
