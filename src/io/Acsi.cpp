// =============================================================================
//  Acsi.cpp — Contrôleur de disque dur ACSI (port de Hatari hdc.c).
//
//  Port fidèle de la logique « disque » de hdc.c : réception des paquets de
//  commande ACSI/SCSI, dispatch des commandes, accès à l'image hôte, tampon de
//  réponse. Le transfert DMA RAM↔image est orchestré par Fdc (qui possède
//  l'adresse/le mode DMA et le plan mémoire).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Acsi.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <cstdint>

namespace {
// Opcodes (hdc.h)
constexpr uint8_t HD_TEST_UNIT_RDY = 0x00, HD_REQ_SENSE = 0x03, HD_FORMAT_DRIVE = 0x04,
    HD_READ_SECTOR = 0x08, HD_WRITE_SECTOR = 0x0A, HD_SEEK = 0x0B, HD_INQUIRY = 0x12,
    HD_MODESELECT = 0x15, HD_MODESENSE = 0x1A, HD_SHIP = 0x1B, HD_READ_CAPACITY1 = 0x25,
    HD_READ_SECTOR1 = 0x28, HD_WRITE_SECTOR1 = 0x2A, HD_REPORT_LUNS = 0xa0;
// Statuts
constexpr int HD_STATUS_OK = 0x00, HD_STATUS_ERROR = 0x02;
// Codes REQUEST SENSE
constexpr uint8_t HD_REQSENS_OK = 0x00, HD_REQSENS_NOSECTOR = 0x01, HD_REQSENS_WRITEERR = 0x03,
    HD_REQSENS_OPCODE = 0x20, HD_REQSENS_INVADDR = 0x21, HD_REQSENS_INVARG = 0x24,
    HD_REQSENS_INVLUN = 0x25;

const uint8_t inquiry_bytes[] = {
    0, 0, 1, 0, 31, 0, 0, 0,
    'N','e','o','S','T',' ',' ',' ',          // Vendor ID
    'E','m','u','l','a','t','e','d',          // Product ID 1
    'H','a','r','d','d','i','s','k',          // Product ID 2
    '0','1','8','0',                          // Revision
};

inline uint32_t rdInt24(const uint8_t* a, int i) {
    return (uint32_t(a[i]) << 16) | (uint32_t(a[i + 1]) << 8) | a[i + 2];
}
inline uint32_t rdInt32(const uint8_t* a, int i) {
    return (uint32_t(a[i]) << 24) | (uint32_t(a[i + 1]) << 16) | (uint32_t(a[i + 2]) << 8) | a[i + 3];
}
} // namespace

Acsi::~Acsi() { unmountAll(); }

// -----------------------------------------------------------------------------
//  Montage / cycle de vie
// -----------------------------------------------------------------------------
bool Acsi::mount(int target, const std::string& path) {
    if (target < 0 || target >= MAX_DEVS) return false;
    Dev& d = devs_[target];
    if (d.fp) { fclose(d.fp); d.fp = nullptr; }
    d.enabled = false;

    // std::filesystem et non stat() : <sys/stat.h> manque hors POSIX, et la
    // surcharge à error_code ne LANCE pas sur un chemin illisible.
    std::error_code ec;
    const std::uintmax_t sz = std::filesystem::file_size(path, ec);
    if (ec || sz == 0 || (sz & 511)) {
        std::fprintf(stderr, "[acsi] invalid image (zero size or not a multiple of 512): %s\n",
                     path.c_str());
        return false;
    }
    bool ro = false;
    FILE* fp = fopen(path.c_str(), "rb+");
    if (!fp) { fp = fopen(path.c_str(), "rb"); ro = true; }
    if (!fp) {
        std::fprintf(stderr, "[acsi] cannot open: %s\n", path.c_str());
        return false;
    }
    d.fp = fp;
    d.readOnly = ro;
    d.blockSize = 512;
    d.hdSize = uint32_t(sz / 512);
    d.scsiVersion = 1;
    d.lastError = HD_REQSENS_OK;
    d.enabled = true;
    d.path = path;
    std::fprintf(stderr, "[acsi] ACSI %d <-> %s (%u sectors, %.1f MB%s)\n",
                 target, path.c_str(), d.hdSize, d.hdSize / 2048.0, ro ? ", read-only" : "");
    return true;
}

void Acsi::unmountAll() {
    for (auto& d : devs_) {
        if (d.fp) fclose(d.fp);
        d.fp = nullptr;
        d.enabled = false;
    }
}

void Acsi::reset() { status_ = 0; byteCount_ = 0; }

bool Acsi::anyEnabled() const {
    for (const auto& d : devs_) if (d.enabled) return true;
    return false;
}
bool Acsi::targetEnabled() const {
    return target_ >= 0 && target_ < MAX_DEVS && devs_[target_].enabled;
}

// -----------------------------------------------------------------------------
//  Helpers de commande
// -----------------------------------------------------------------------------
uint32_t Acsi::lba() const {
    if (opcode_ < 0x20) return rdInt24(command_, 1) & 0x1FFFFF;   // classe 0
    return rdInt32(command_, 2);                                  // classe 1
}
int Acsi::count() const {
    if (opcode_ < 0x20) return command_[4];                       // classe 0
    return (command_[7] << 8) | command_[8];                     // classe 1
}
uint8_t* Acsi::prepRespBuf(int size) {
    dataLen_ = size;
    if ((int)buf_.size() < size) buf_.resize(size);
    return buf_.data();
}

// -----------------------------------------------------------------------------
//  Commandes (port des HDC_Cmd_*)
// -----------------------------------------------------------------------------
void Acsi::cmdTestUnitReady() { status_ = HD_STATUS_OK; }

void Acsi::cmdInquiry() {
    Dev& dev = devs_[target_];
    int n = count();
    // Longueur d'allocation 0 possible (count() == 0) : Hatari écrit buf[0] sans
    // condition dans un tampon persistant jamais rétréci. On garantit ici au moins
    // 1 octet de stockage, puis on retronque dataLen_ (octets renvoyés au DMA) à
    // la longueur demandée par l'invité.
    uint8_t* buf = prepRespBuf(std::max(n, 1));
    dataLen_ = n;
    if (n > (int)sizeof(inquiry_bytes)) {
        memset(buf + sizeof(inquiry_bytes), 0, n - sizeof(inquiry_bytes));
        n = sizeof(inquiry_bytes);
    }
    memcpy(buf, inquiry_bytes, n);
    buf[0] = lun() == 0 ? 0 : 0x7F;        // LUN non géré → Peripheral Qualifier 0x7F
    if (n > 2) buf[2] = dev.scsiVersion;
    // buf[4] (« Additional Length ») reste la valeur fixe 31 d'inquiry_bytes[4] (copiée
    // par memcpy), comme Hatari (HDC_Cmd_Inquiry n'écrit jamais cet octet).
    status_ = HD_STATUS_OK;
    dev.lastError = HD_REQSENS_OK;
    dev.setLastBlockAddr = false;
}

void Acsi::cmdRequestSense() {
    Dev& dev = devs_[target_];
    int n = count();
    if (n == 0 && dev.scsiVersion == 1) n = 4;
    else if (n > 22) n = 22;
    // Comme pour INQUIRY : la branche n <= 4 écrit b[0..3] sans condition (fidèle
    // à HDC_Cmd_RequestSense) → garantir 4 octets de stockage minimum, dataLen_
    // restant la longueur demandée (n peut valoir 1..3 sur commande farfelue).
    uint8_t* b = prepRespBuf(std::max(n, 4));
    dataLen_ = n;
    memset(b, 0, std::max(n, 4));
    if (n <= 4) {
        b[0] = dev.lastError;
        if (dev.setLastBlockAddr) {
            b[0] |= 0x80;
            b[1] = dev.lastBlockAddr >> 16;
            b[2] = dev.lastBlockAddr >> 8;
            b[3] = dev.lastBlockAddr;
        }
    } else {
        b[0] = 0x70;
        if (dev.setLastBlockAddr) {
            b[0] |= 0x80;
            b[4] = dev.lastBlockAddr >> 16;
            b[5] = dev.lastBlockAddr >> 8;
            b[6] = dev.lastBlockAddr;
        }
        switch (dev.lastError) {
        case HD_REQSENS_OK:     b[2] = 0; break;
        case HD_REQSENS_OPCODE:
        case HD_REQSENS_INVADDR:
        case HD_REQSENS_INVARG:
        case HD_REQSENS_INVLUN: b[2] = 5; break;
        default:                b[2] = 4; break;
        }
        b[7] = 14;
        b[12] = dev.lastError;
        b[19] = dev.lastBlockAddr >> 16;
        b[20] = dev.lastBlockAddr >> 8;
        b[21] = dev.lastBlockAddr;
    }
    status_ = HD_STATUS_OK;
}

// MODE SENSE — page 0x00 (vendor) et 0x04 (géométrie), suffisant pour HDX/ASV.
void Acsi::cmdModeSense() {
    Dev& dev = devs_[target_];
    dev.setLastBlockAddr = false;
    if (command_[3]) { status_ = HD_STATUS_ERROR; dev.lastError = HD_REQSENS_INVARG; return; }

    auto page00 = [&](uint8_t* buf) {
        buf[0] = 0; buf[1] = 14; buf[2] = 0; buf[3] = 8; buf[4] = 0;
        buf[5] = dev.hdSize >> 16; buf[6] = dev.hdSize >> 8; buf[7] = dev.hdSize;
        buf[8] = 0; buf[9] = 0; buf[10] = 2; buf[11] = 0;
        buf[12] = buf[13] = buf[14] = buf[15] = 0;
    };
    auto page04 = [&](uint8_t* buf) {
        buf[0] = 4; buf[1] = 22;
        buf[2] = dev.hdSize >> 23; buf[3] = dev.hdSize >> 15; buf[4] = dev.hdSize >> 7;
        buf[5] = 128;
        for (int i = 6; i <= 19; i++) buf[i] = 0;
        buf[20] = 0x1c; buf[21] = 0x20;          // 7200 tr/min
        buf[22] = buf[23] = 0;
    };
    uint8_t* buf;
    switch (command_[2]) {
    case 0x00: buf = prepRespBuf(16); page00(buf); break;
    case 0x04: buf = prepRespBuf(28); page04(buf + 4); buf[0] = 24; buf[1] = buf[2] = buf[3] = 0; break;
    case 0x3f: buf = prepRespBuf(44); page04(buf + 4); page00(buf + 28);
               buf[0] = 43; buf[1] = buf[2] = buf[3] = 0; break;
    default:   status_ = HD_STATUS_ERROR; dev.lastError = HD_REQSENS_INVARG; return;
    }
    status_ = HD_STATUS_OK;
    dev.lastError = HD_REQSENS_OK;
}

void Acsi::cmdReadCapacity() {
    Dev& dev = devs_[target_];
    uint32_t nSectors = dev.hdSize - 1;
    uint8_t* b = prepRespBuf(8);
    b[0] = nSectors >> 24; b[1] = nSectors >> 16; b[2] = nSectors >> 8; b[3] = nSectors;
    b[4] = dev.blockSize >> 24; b[5] = dev.blockSize >> 16; b[6] = dev.blockSize >> 8; b[7] = dev.blockSize;
    status_ = HD_STATUS_OK;
    dev.lastError = HD_REQSENS_OK;
    dev.setLastBlockAddr = false;
}

void Acsi::cmdReadSector() {
    Dev& dev = devs_[target_];
    dev.lastBlockAddr = lba();
    if (dev.lastBlockAddr >= dev.hdSize ||
        fseeko(dev.fp, (off_t)dev.lastBlockAddr * dev.blockSize, SEEK_SET) != 0) {
        status_ = HD_STATUS_ERROR; dev.lastError = HD_REQSENS_INVADDR;
    } else {
        int c = count();
        uint8_t* buf = prepRespBuf(dev.blockSize * c);
        int n = (int)fread(buf, dev.blockSize, c, dev.fp);
        if (n == c) { status_ = HD_STATUS_OK; dev.lastError = HD_REQSENS_OK; }
        else { status_ = HD_STATUS_ERROR; dev.lastError = HD_REQSENS_NOSECTOR; }
    }
    dev.setLastBlockAddr = true;
}

void Acsi::cmdWriteSector() {
    Dev& dev = devs_[target_];
    dev.lastBlockAddr = lba();
    if (dev.lastBlockAddr >= dev.hdSize ||
        fseeko(dev.fp, (off_t)dev.lastBlockAddr * dev.blockSize, SEEK_SET) != 0) {
        status_ = HD_STATUS_ERROR; dev.lastError = HD_REQSENS_INVADDR;
    } else {
        dataLen_ = count() * dev.blockSize;
        if (dataLen_) {
            prepRespBuf(dataLen_);
            dmaWrite_ = true;                  // l'écriture se fait au transfert DMA
            status_ = HD_STATUS_OK; dev.lastError = HD_REQSENS_OK;
        } else {
            status_ = HD_STATUS_ERROR; dev.lastError = HD_REQSENS_WRITEERR;
        }
    }
    dev.setLastBlockAddr = true;
}

void Acsi::writeToDisk(const uint8_t* src, int len) {
    Dev& dev = devs_[target_];
    if (!dev.fp || dev.readOnly) { status_ = HD_STATUS_ERROR; dev.lastError = HD_REQSENS_WRITEERR; return; }
    fseeko(dev.fp, (off_t)dev.lastBlockAddr * dev.blockSize, SEEK_SET);
    int n = (int)fwrite(src, 1, len, dev.fp);
    fflush(dev.fp);
    if (n != len) { status_ = HD_STATUS_ERROR; dev.lastError = HD_REQSENS_WRITEERR; }
}

void Acsi::cmdSeek() {
    Dev& dev = devs_[target_];
    dev.lastBlockAddr = lba();
    if (dev.lastBlockAddr < dev.hdSize &&
        fseeko(dev.fp, (off_t)dev.lastBlockAddr * dev.blockSize, SEEK_SET) == 0) {
        status_ = HD_STATUS_OK; dev.lastError = HD_REQSENS_OK;
    } else {
        status_ = HD_STATUS_ERROR; dev.lastError = HD_REQSENS_INVADDR;
    }
    dev.setLastBlockAddr = true;
}

void Acsi::cmdFormatDrive() {
    Dev& dev = devs_[target_];
    status_ = HD_STATUS_OK; dev.lastError = HD_REQSENS_OK; dev.setLastBlockAddr = false;
}

void Acsi::cmdReportLuns() {
    Dev& dev = devs_[target_];
    uint8_t* b = prepRespBuf(16);
    b[0] = 0; b[1] = 0; b[2] = 0; b[3] = 8;
    memset(b + 4, 0, 12);
    status_ = HD_STATUS_OK; dev.lastError = HD_REQSENS_OK; dev.setLastBlockAddr = false;
}

void Acsi::emulateCommand() {
    Dev& dev = devs_[target_];
    dataLen_ = 0;
    dmaWrite_ = false;
    switch (opcode_) {
    case HD_TEST_UNIT_RDY: cmdTestUnitReady(); break;
    case HD_READ_CAPACITY1: cmdReadCapacity(); break;
    case HD_READ_SECTOR:
    case HD_READ_SECTOR1:  cmdReadSector(); break;
    case HD_WRITE_SECTOR:
    case HD_WRITE_SECTOR1: cmdWriteSector(); break;
    case HD_INQUIRY:       cmdInquiry(); break;
    case HD_SEEK:          cmdSeek(); break;
    case HD_SHIP:          status_ = 0xFF; break;
    case HD_REQ_SENSE:     cmdRequestSense(); break;
    case HD_MODESELECT:
        status_ = HD_STATUS_OK; dev.lastError = HD_REQSENS_OK; dev.setLastBlockAddr = false; break;
    case HD_MODESENSE:     cmdModeSense(); break;
    case HD_FORMAT_DRIVE:  cmdFormatDrive(); break;
    case HD_REPORT_LUNS:   cmdReportLuns(); break;
    default:
        status_ = HD_STATUS_ERROR; dev.lastError = HD_REQSENS_OPCODE; dev.setLastBlockAddr = false; break;
    }
    static const bool trace = getenv("NEOST_ACSI_TRACE") != nullptr;
    if (trace)
        std::fprintf(stderr, "[acsi] t=%d op=0x%02X lba=%u cnt=%d -> status=%d len=%d%s\n",
                     target_, opcode_, lba(), count(), status_, dataLen_, dmaWrite_ ? " (write)" : "");
}

// -----------------------------------------------------------------------------
//  Réception du paquet de commande (port de HDC_WriteCommandPacket)
// -----------------------------------------------------------------------------
void Acsi::selectTarget(uint8_t target) {
    target_ = target & 7;
    byteCount_ = 0;
}

bool Acsi::feedByte(uint8_t b) {
    Dev& dev = devs_[target_];
    if (!dev.enabled) { status_ = HD_STATUS_ERROR; return false; }

    if (byteCount_ == 0) { opcode_ = b; dmaError_ = false; }
    if (byteCount_ < (int)sizeof(command_)) command_[byteCount_] = b;
    ++byteCount_;

    bool didCmd = false;
    if ((opcode_ < 0x20 && byteCount_ == 6) ||
        (opcode_ >= 0x20 && opcode_ < 0x60 && byteCount_ == 10) ||
        (opcode_ == HD_REPORT_LUNS && byteCount_ == 12)) {
        if (lun() == 0 || opcode_ == HD_INQUIRY) {
            emulateCommand();
            didCmd = true;
        } else {
            dev.lastError = HD_REQSENS_INVLUN;
            if (opcode_ == HD_REQ_SENSE) { cmdRequestSense(); didCmd = true; }
            else status_ = HD_STATUS_ERROR;
        }
    } else if (opcode_ >= 0x60 && opcode_ != HD_REPORT_LUNS) {
        status_ = HD_STATUS_ERROR; dev.lastError = HD_REQSENS_OPCODE; dev.setLastBlockAddr = false;
    } else {
        status_ = HD_STATUS_OK;
    }
    return didCmd;
}

// -----------------------------------------------------------------------------
//  Comptage de partitions (port de HDC_PartitionCount, DOS + Atari)
// -----------------------------------------------------------------------------
static int partitionCountOne(FILE* fp) {
    if (!fp) return 0;
    off_t pos = ftello(fp);
    unsigned char bs[512];
    int parts = 0;
    if (fseeko(fp, 0, SEEK_SET) == 0 && fread(bs, sizeof(bs), 1, fp) == 1) {
        if (bs[0x1FE] == 0x55 && bs[0x1FF] == 0xAA) {           // DOS MBR
            const unsigned char* p = bs + 0x1BE;
            for (int i = 0; i < 4; i++, p += 16)
                if (p[4]) parts++;
        } else {                                                // Atari MBR
            const unsigned char* p = bs + 0x1C6;
            for (int i = 0; i < 4; i++, p += 12)
                if (p[0] & 0x1) parts++;
        }
    }
    fseeko(fp, pos, SEEK_SET);
    return parts;
}

int Acsi::partitionCount() const {
    int n = 0;
    for (const auto& d : devs_) if (d.enabled) n += partitionCountOne(d.fp);
    return n;
}
