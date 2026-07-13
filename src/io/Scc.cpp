// =============================================================================
//  Scc.cpp — SCC Z85C30 du Mega STE (port de Hatari scc.c).
//
//  Port fonctionnel fidèle du cœur registre/IRQ de scc.c (cf. Scc.hpp pour le
//  périmètre). Les commentaires renvoient aux fonctions SCC_* d'Hatari.
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "io/Scc.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
constexpr int SCC_IRQ_ON = 0, SCC_IRQ_OFF = 1;

// Commandes WR0 (bits 3-5)
constexpr uint8_t WR0_CMD_RESET_EXT_STATUS = 0x02, WR0_CMD_RESET_TX_IP = 0x05,
    WR0_CMD_ERROR_RESET = 0x06, WR0_CMD_RESET_HIGHEST_IUS = 0x07;
// WR1
constexpr uint8_t WR1_EXT_INT_ENABLE = 0x01, WR1_TX_INT_ENABLE = 0x02, WR1_PARITY_SPECIAL = 0x04;
constexpr uint8_t WR1_RX_MODE_INT_OFF = 0x00, WR1_RX_MODE_FIRST_SPECIAL = 0x01, WR1_RX_MODE_ALL_SPECIAL = 0x02;
// WR3 / WR5
constexpr uint8_t WR3_RX_ENABLE = 0x01;
[[maybe_unused]] constexpr uint8_t WR5_RTS = 0x02, WR5_TX_ENABLE = 0x08, WR5_SEND_BREAK = 0x10, WR5_DTR = 0x80;
// WR4 stop bits
constexpr uint8_t WR4_PARITY_ENABLE = 0x01, WR4_STOP_SYNC = 0x00;
// WR9
constexpr uint8_t WR9_VIS = 0x01, WR9_NV = 0x02, WR9_MIE = 0x08, WR9_STATUS_HIGH_LOW = 0x10, WR9_SOFT_INTACK = 0x20;
constexpr uint8_t WR9_CMD_RESET_B = 0x01, WR9_CMD_RESET_A = 0x02, WR9_CMD_RESET_FORCE_HW = 0x03;
// WR15
[[maybe_unused]] constexpr uint8_t WR15_WR7_PRIME = 0x01, WR15_ZERO_COUNT_IE = 0x02, WR15_STATUS_FIFO = 0x04,
    WR15_DCD_IE = 0x08, WR15_SYNC_HUNT_IE = 0x10, WR15_CTS_IE = 0x20,
    WR15_TX_UNDERRUN_IE = 0x40, WR15_BREAK_ABORT_IE = 0x80;
// RR0
constexpr uint8_t RR0_RX_CHAR_AVAIL = 0x01, RR0_ZERO_COUNT = 0x02, RR0_TX_BUFFER_EMPTY = 0x04,
    RR0_DCD = 0x08, RR0_SYNC_HUNT = 0x10, RR0_CTS = 0x20, RR0_TX_UNDERRUN_EOM = 0x40, RR0_BREAK_ABORT = 0x80;
// RR1
constexpr uint8_t RR1_ALL_SENT = 0x01, RR1_PARITY_ERROR = 0x10, RR1_RX_OVERRUN = 0x20,
    RR1_CRC_FRAMING = 0x40, RR1_EOF_SDLC = 0x80;
// RR3 (interrupt pending, channel A holds all)
constexpr uint8_t RR3_EXT_IP_B = 0x01, RR3_TX_IP_B = 0x02, RR3_RX_IP_B = 0x04,
    RR3_EXT_IP_A = 0x08, RR3_TX_IP_A = 0x10, RR3_RX_IP_A = 0x20;
// Sources d'interruption (bitmask interne)
constexpr uint32_t INT_RX_CHAR = 1u << 0, INT_RX_OVERRUN = 1u << 1, INT_RX_FRAMING = 1u << 2,
    INT_RX_PARITY = 1u << 4, INT_TX_BUFFER_EMPTY = 1u << 5;
} // namespace

// -----------------------------------------------------------------------------
//  Reset
// -----------------------------------------------------------------------------
void Scc::resetChannel(int c, bool hw) {
    Chn& ch = chn_[c];
    ch.WR[0] = 0x00;
    activeReg_ = 0;
    ch.WR[1] &= 0x24; ch.WR[3] &= 0xfe; ch.WR[4] |= 0x04; ch.WR[5] &= 0x61;
    ch.WR[15] = 0xf8; ch.WR7p = 0x20;
    if (hw) {
        chn_[0].WR[9] &= 0x03; chn_[0].WR[9] |= 0xC0; ius_ = 0;
        ch.WR[10] = 0x00; ch.WR[11] = 0x08; ch.WR[14] &= 0xC0; ch.WR[14] |= 0x30;
    } else {
        chn_[0].WR[9] &= 0xdf;
        ch.WR[10] &= 0x60; ch.WR[14] &= 0xC3; ch.WR[14] |= 0x20;
    }
    ch.RR[0] &= 0xb8; ch.RR[0] |= 0x44;
    ch.rr0NoLatch = ch.RR[0]; ch.rr0Latched = false;
    ch.RR[1] &= 0x01; ch.RR[1] |= 0x06;
    ch.RR[3] = 0x00; ch.RR[10] &= 0x40;
    ch.txWritten = false; ch.tsrFull = false;
}

void Scc::resetFull(bool hw) {
    uint8_t wr9old = chn_[0].WR[9];
    resetChannel(0, true);
    resetChannel(1, true);
    if (!hw) { chn_[0].WR[9] &= ~0x1c; chn_[0].WR[9] |= (wr9old & 0x1c); }
    chn_[0].intSources = 0; chn_[1].intSources = 0;
    setLineIRQ(SCC_IRQ_OFF);
}

void Scc::reset() {
    trace_ = getenv("NEOST_SCC_TRACE") != nullptr;
    for (auto& c : chn_) { std::memset(c.WR, 0, sizeof(c.WR)); std::memset(c.RR, 0, sizeof(c.RR)); }
    resetFull(true);
}

// -----------------------------------------------------------------------------
//  RR0 / RR2 / RR3
// -----------------------------------------------------------------------------
uint16_t Scc::getCTS(int) const { return 1; }   // ligne au repos = assertée (cf. Hatari sans TTY)
uint16_t Scc::getDCD(int) const { return 1; }

void Scc::updateRR0(int c) {
    Chn& ch = chn_[c];
    uint8_t rr0New;
    bool updCTS = false, updDCD = false;
    if (!ch.rr0Latched) {
        rr0New = ch.rr0NoLatch;
        updCTS = updDCD = true;
    } else {
        rr0New = ch.rr0NoLatch & (RR0_RX_CHAR_AVAIL | RR0_TX_BUFFER_EMPTY);
        rr0New |= ch.rr0NoLatch & RR0_ZERO_COUNT;
        if (ch.WR[15] & WR15_DCD_IE) rr0New |= ch.RR[0] & RR0_DCD; else updDCD = true;
        if (ch.WR[15] & WR15_SYNC_HUNT_IE) rr0New |= ch.RR[0] & RR0_SYNC_HUNT;
        else rr0New |= ch.rr0NoLatch & RR0_SYNC_HUNT;
        if (ch.WR[15] & WR15_CTS_IE) rr0New |= ch.RR[0] & RR0_CTS; else updCTS = true;
        if (ch.WR[15] & WR15_TX_UNDERRUN_IE) rr0New |= ch.RR[0] & RR0_TX_UNDERRUN_EOM;
        else rr0New |= ch.rr0NoLatch & RR0_TX_UNDERRUN_EOM;
        if (ch.WR[15] & WR15_BREAK_ABORT_IE) rr0New |= ch.RR[0] & RR0_BREAK_ABORT;
        else rr0New |= ch.rr0NoLatch & RR0_BREAK_ABORT;
    }
    if (updCTS) { rr0New &= ~RR0_CTS; if (getCTS(c)) rr0New |= RR0_CTS; }
    if (updDCD) { rr0New &= ~RR0_DCD; if (getDCD(c)) rr0New |= RR0_DCD; }

    uint8_t rr0Old = ch.RR[0];
    ch.RR[0] = rr0New;

    if (ch.WR[1] & WR1_EXT_INT_ENABLE) {
        bool set = false;
        if (((rr0Old & RR0_ZERO_COUNT) == 0) && (rr0New & RR0_ZERO_COUNT) && (ch.WR[15] & WR15_ZERO_COUNT_IE)) set = true;
        else if (((rr0Old & RR0_DCD) != (rr0New & RR0_DCD)) && (ch.WR[15] & WR15_DCD_IE)) set = true;
        else if (((rr0Old & RR0_SYNC_HUNT) != (rr0New & RR0_SYNC_HUNT)) && (ch.WR[15] & WR15_SYNC_HUNT_IE)) set = true;
        else if (((rr0Old & RR0_CTS) != (rr0New & RR0_CTS)) && (ch.WR[15] & WR15_CTS_IE)) set = true;
        else if (((rr0Old & RR0_TX_UNDERRUN_EOM) == 0) && (rr0New & RR0_TX_UNDERRUN_EOM) && (ch.WR[15] & WR15_TX_UNDERRUN_IE)) set = true;
        else if (((rr0Old & RR0_BREAK_ABORT) != (rr0New & RR0_BREAK_ABORT)) && (ch.WR[15] & WR15_BREAK_ABORT_IE)) set = true;
        if (set) {
            ch.rr0Latched = true;
            updateRR3Bit(true, c ? RR3_EXT_IP_B : RR3_EXT_IP_A);
        }
    }
}

uint8_t Scc::getVectorStatus() {
    uint8_t status, mask;
    uint8_t rr3 = chn_[0].RR[3];
    if (rr3 & RR3_RX_IP_A) {
        mask = RR1_RX_OVERRUN | RR1_CRC_FRAMING | RR1_EOF_SDLC;
        if (chn_[0].WR[1] & WR1_PARITY_SPECIAL) mask |= RR1_PARITY_ERROR;
        status = (chn_[0].RR[0] & mask) ? 7 : 6;
    } else if (rr3 & RR3_TX_IP_A) status = 4;
    else if (rr3 & RR3_EXT_IP_A) status = 5;
    else if (rr3 & RR3_RX_IP_B) {
        mask = RR1_RX_OVERRUN | RR1_CRC_FRAMING | RR1_EOF_SDLC;
        if (chn_[1].WR[1] & WR1_PARITY_SPECIAL) mask |= RR1_PARITY_ERROR;
        status = (chn_[1].RR[0] & mask) ? 3 : 2;
    } else if (rr3 & RR3_TX_IP_B) status = 0;
    else if (rr3 & RR3_EXT_IP_B) status = 1;
    else status = 3;
    return status;
}

void Scc::updateRR2() {
    uint8_t vector = chn_[0].WR[2];
    chn_[0].RR[2] = vector;                       // RR2A = WR2
    uint8_t status = getVectorStatus();
    if (chn_[0].WR[9] & WR9_STATUS_HIGH_LOW) {
        status = ((status & 1) << 2) + (status & 2) + ((status & 4) >> 2);
        vector &= 0x8f; vector |= (status << 4);
    } else {
        vector &= 0xf1; vector |= (status << 1);
    }
    chn_[1].RR[2] = vector;                       // RR2B = WR2 + statut
}

void Scc::updateRR3Bit(bool set, uint8_t bit) {
    if (set) chn_[0].RR[3] |= bit; else chn_[0].RR[3] &= ~bit;
}

void Scc::updateRR3(int c) {
    Chn& ch = chn_[c];
    updateRR0(c);
    uint8_t rxMode = (ch.WR[1] >> 3) & 3;
    bool intRx = (rxMode == WR1_RX_MODE_FIRST_SPECIAL || rxMode == WR1_RX_MODE_ALL_SPECIAL);
    bool intSpecial = (rxMode != WR1_RX_MODE_INT_OFF);
    bool set = (intRx && (ch.RR[0] & RR0_RX_CHAR_AVAIL))
        || (intSpecial && ((ch.RR[1] & RR1_RX_OVERRUN) || (ch.RR[1] & RR1_CRC_FRAMING)
            || (ch.RR[1] & RR1_EOF_SDLC) || ((ch.RR[1] & RR1_PARITY_ERROR) && (ch.WR[1] & WR1_PARITY_SPECIAL))));
    updateRR3Bit(set, c ? RR3_RX_IP_B : RR3_RX_IP_A);

    set = (ch.RR[0] & RR0_TX_BUFFER_EMPTY) && (ch.WR[1] & WR1_TX_INT_ENABLE) && ch.txWritten;
    updateRR3Bit(set, c ? RR3_TX_IP_B : RR3_TX_IP_A);
}

// -----------------------------------------------------------------------------
//  IRQ / IACK
// -----------------------------------------------------------------------------
void Scc::updateIRQ() {
    int irqNew = SCC_IRQ_OFF;
    if (chn_[0].WR[9] & WR9_MIE) {
        for (int i = 5; i >= 0; --i) {
            if (ius_ & (1 << i)) { irqNew = SCC_IRQ_OFF; break; }
            if (chn_[0].RR[3] & (1 << i)) { irqNew = SCC_IRQ_ON; break; }
        }
    }
    if (irqNew != irqLine_) setLineIRQ(irqNew);
}

void Scc::intSourcesChange(int c, uint32_t src, bool set) {
    if (set) { if ((chn_[c].intSources & src) == src) return; }
    else     { if ((chn_[c].intSources & src) == 0)   return; }
    updateRR3(c);
    if (set) chn_[c].intSources |= src; else chn_[c].intSources &= ~src;
    updateIRQ();
}

int Scc::doIACK(bool /*soft*/) {
    setLineIRQ(SCC_IRQ_OFF);
    for (int i = 5; i >= 0; --i)
        if (chn_[0].RR[3] & (1 << i)) { ius_ |= (1 << i); break; }
    updateRR2();
    return (chn_[0].WR[9] & WR9_VIS) ? chn_[1].RR[2] : chn_[0].RR[2];
}

int Scc::processIack() {
    int vector = doIACK(false);
    if (chn_[0].WR[9] & WR9_NV) return -1;   // No Vector
    return vector;
}

// -----------------------------------------------------------------------------
//  TX / RX
// -----------------------------------------------------------------------------
void Scc::copyTdrTsr(int c, uint8_t tdr) {
    chn_[c].tsr = tdr;
    chn_[c].tsrFull = true;
    chn_[c].RR[1] &= ~RR1_ALL_SENT;
    updateRR0Set(c, RR0_TX_BUFFER_EMPTY);
    intSourcesSet(c, INT_TX_BUFFER_EMPTY);
}

void Scc::serialWriteByte(int c, uint8_t v) {
    if (trace_) std::fprintf(stderr, "[scc] TX %c: $%02X\n", 'A' + c, v);
    // Bouclage local interne (WR14 bit4 = Local Loopback, Zilog Z85C30 §WR14) : TxD est
    // relié à RxD à l'intérieur de la puce et la ligne externe reste au repos → on réinjecte
    // l'octet en RX et on NE l'émet PAS sur le puits. CHOIX DÉLIBÉRÉ « plus correct qu'Hatari » :
    // Hatari NE modélise PAS ce bit (SCC_Process_TX émet toujours), alors que le datasheet le
    // définit. ⚠ Le reset matériel met WR14=$30 (bit4=1) : un programme qui transmet SANS
    // reconfigurer WR14 boucle donc sur lui-même — inoffensif en pratique car tout pilote
    // série/LAN réécrit WR14 (réglage du BRG, bit4=0) avant d'émettre. NE PAS « corriger ».
    if (chn_[c].WR[14] & 0x10) { receiveByte(c, v); return; }
    if (sink_) sink_(c, v);
}

void Scc::processTX(int c) {
    Chn& ch = chn_[c];
    if ((ch.RR[0] & RR0_TX_BUFFER_EMPTY) && !ch.tsrFull) return;   // underrun
    if (ch.tsrFull && (ch.WR[5] & WR5_TX_ENABLE)) {
        serialWriteByte(c, ch.tsr);
        ch.tsrFull = false;
        ch.RR[1] |= RR1_ALL_SENT;
    }
    if ((ch.RR[0] & RR0_TX_BUFFER_EMPTY) == 0)
        copyTdrTsr(c, ch.WR[8]);
}

void Scc::receiveByte(int c, uint8_t b) {
    Chn& ch = chn_[c];
    if (!(ch.WR[3] & WR3_RX_ENABLE)) return;     // RX désactivé → octet perdu
    ch.RR[8] = b;
    if (ch.RR[0] & RR0_RX_CHAR_AVAIL) {
        ch.RR[1] |= RR1_RX_OVERRUN;
        intSourcesSet(c, INT_RX_OVERRUN);
    } else {
        updateRR0Set(c, RR0_RX_CHAR_AVAIL);
        intSourcesSet(c, INT_RX_CHAR);
    }
}

uint8_t Scc::readDataReg(int c) {
    updateRR0Clear(c, RR0_RX_CHAR_AVAIL);
    intSourcesClear(c, INT_RX_CHAR);
    return chn_[c].RR[8];
}

void Scc::writeDataReg(int c, uint8_t v) {
    chn_[c].WR[8] = v;
    chn_[c].txWritten = true;
    copyTdrTsr(c, v);            // pas de timer BRG : on transmet immédiatement
    processTX(c);
}

// -----------------------------------------------------------------------------
//  Lecture / écriture d'un registre de contrôle
// -----------------------------------------------------------------------------
uint8_t Scc::readControl(int c) {
    uint8_t value = 0;
    switch (activeReg_) {
    case 0: case 4: updateRR0(c); value = chn_[c].RR[0]; break;
    case 1: case 5: value = chn_[c].RR[1]; break;
    case 2:
        updateRR2();
        if (chn_[0].WR[9] & WR9_SOFT_INTACK) doIACK(true);
        value = chn_[c].RR[2];
        break;
    case 3: value = c ? 0 : chn_[0].RR[3]; break;
    case 6: case 7:
        if (chn_[0].WR[15] & WR15_STATUS_FIFO) value = chn_[c].RR[activeReg_];
        else value = chn_[0].RR[activeReg_ - 4];
        break;
    case 8: value = readDataReg(c); break;
    case 10: case 14: value = chn_[c].RR[10]; break;
    case 12: value = chn_[c].WR[12]; break;
    case 13: case 9: value = chn_[c].WR[13]; break;
    case 15: case 11: value = chn_[c].WR[15] &= 0xFA; break;
    default: value = 0; break;
    }
    return value;
}

void Scc::writeControl(int c, uint8_t value) {
    Chn& ch = chn_[c];
    if (activeReg_ == 0) {
        if (value <= 15) { activeReg_ = value & 0x0f; }
        else {
            uint8_t command = (value >> 3) & 7;
            if (command == WR0_CMD_RESET_EXT_STATUS) {
                updateRR3Bit(false, c ? RR3_EXT_IP_B : RR3_EXT_IP_A);
                updateRR0LatchOff(c);
                updateRR3(c);
                updateIRQ();
            } else if (command == WR0_CMD_RESET_TX_IP) {
                ch.txWritten = false;
                chn_[0].RR[3] &= ~(c ? RR3_TX_IP_B : RR3_TX_IP_A);
                updateIRQ();
            } else if (command == WR0_CMD_ERROR_RESET) {
                ch.RR[1] &= ~(RR1_PARITY_ERROR | RR1_RX_OVERRUN | RR1_CRC_FRAMING);
                intSourcesClear(c, INT_RX_PARITY | INT_RX_OVERRUN | INT_RX_FRAMING);
            } else if (command == WR0_CMD_RESET_HIGHEST_IUS) {
                for (int i = 5; i >= 0; --i) if (ius_ & (1 << i)) { ius_ &= ~(1 << i); break; }
                updateIRQ();
            }
        }
        return;
    }

    // WR7' : Active_Reg==7 et WR15 bit0=1
    if (activeReg_ == 7 && (ch.WR[15] & 1)) ch.WR7p = value;
    else ch.WR[activeReg_] = value;

    switch (activeReg_) {
    case 1: updateRR3(c); updateIRQ(); break;
    case 2: chn_[0].WR[2] = value; break;                 // WR2 commun
    case 4: {
        ch.parityBits = (value & WR4_PARITY_ENABLE) ? 1 : 0;
        if (((value >> 2) & 3) != WR4_STOP_SYNC) updateRR0Set(c, RR0_TX_UNDERRUN_EOM);
        break;
    }
    case 5: break;                                        // RTS/DTR/break : lignes non câblées
    case 8: writeDataReg(c, value); break;
    case 9: {
        chn_[0].WR[9] = value;
        if ((value & WR9_MIE) == 0) ius_ = 0;
        uint8_t command = (value >> 6) & 3;
        if (command == WR9_CMD_RESET_FORCE_HW) resetFull(false);
        else if (command == WR9_CMD_RESET_A) resetChannel(0, false);
        else if (command == WR9_CMD_RESET_B) resetChannel(1, false);
        updateIRQ();
        break;
    }
    case 15:
        if ((value & WR15_ZERO_COUNT_IE) == 0) updateRR0Clear(c, RR0_ZERO_COUNT);
        updateRR3(c); updateIRQ();
        break;
    default: break;
    }
    activeReg_ = 0;
}

// -----------------------------------------------------------------------------
//  Décodage MMIO $FF8C80-$FF8C87 (port de SCC_handleRead/Write + IoMem)
// -----------------------------------------------------------------------------
uint8_t Scc::handleRead(uint32_t addr) {
    int channel = (addr >> 2) & 1;               // bit2 : 0 = A, 1 = B
    int reg = activeReg_;
    uint8_t v = (addr & 2) ? readDataReg(channel) : readControl(channel);
    if (trace_) std::fprintf(stderr, "[scc] rd %c %s%d=$%02X\n", 'A' + channel,
                             (addr & 2) ? "RRdata" : "RR", reg, v);
    activeReg_ = 0;
    return v;
}
void Scc::handleWrite(uint32_t addr, uint8_t v) {
    int channel = (addr >> 2) & 1;
    if (trace_) std::fprintf(stderr, "[scc] wr %c %s%d=$%02X\n", 'A' + channel,
                             (addr & 2) ? "WRdata" : "WR", activeReg_, v);
    if (addr & 2) writeDataReg(channel, v);
    else          writeControl(channel, v);
}

uint8_t Scc::read8(uint32_t addr) {
    return (addr & 1) ? handleRead(addr) : 0xFF;   // octet pair = $FF
}
void Scc::write8(uint32_t addr, uint8_t v) {
    if (addr & 1) handleWrite(addr, v);            // octet pair ignoré
}
