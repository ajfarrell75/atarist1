// PortDongle — adaptateurs spéciaux (joystick / série / parallèle / boutons de
// cartouche). Voir l'en-tête pour l'inventaire et les sources (Steem SSE, WinUAE).
#include "io/PortDongle.hpp"
#include "io/Mfp.hpp"
#include <cstring>

namespace {
struct Entry { const char* id; const char* label; };
// Indexé par PortDongle::Type.
constexpr Entry kTable[] = {
    { "none",        "None" },
    { "bat2",        "B.A.T. II (serial port)" },
    { "musicmaster", "Music Master (serial port)" },
    { "jeannedarc",  "Jeanne d'Arc (serial port)" },
    { "leaderboard", "Leader Board (joystick 1)" },
    { "10thframe",   "10th Frame (joystick 1)" },
    { "cricket",     "Cricket Captain (joystick 0)" },
    { "rugby",       "Rugby Coach (joystick 1)" },
    { "soccer",      "Multi Player Soccer Manager (joystick 0)" },
    { "prosound",    "Pro Sound Designer DAC (printer port)" },
    { "multiface",   "Multiface ST freeze button (cartridge)" },
    { "urc",         "Ultimate Ripper button (cartridge)" },
};
static_assert(sizeof(kTable) / sizeof(kTable[0]) == size_t(PortDongle::Type::Count),
              "kTable must follow PortDongle::Type");
}

const char* PortDongle::id(Type t)    { return kTable[size_t(t) < size_t(Type::Count) ? size_t(t) : 0].id; }
const char* PortDongle::label(Type t) { return kTable[size_t(t) < size_t(Type::Count) ? size_t(t) : 0].label; }

PortDongle::Type PortDongle::fromId(const char* s) {
    if (!s) return Type::None;
    for (size_t i = 0; i < size_t(Type::Count); ++i)
        if (!std::strcmp(kTable[i].id, s)) return Type(i);
    return Type::None;
}

// --- Port série ---------------------------------------------------------------
void PortDongle::onPortA(uint8_t a, int64_t now, Mfp& mfp) {
    switch (type_) {
    case Type::MusicMaster:
        // Steem stports.cpp TSTPort::SetDTR : « old - new » + date de l'écriture.
        value_  = uint16_t(((value_ << 1) | ((a & 0x10) ? 1u : 0u)) & 3u);
        timing_ = now;
        break;
    case Type::JeanneDArc: {
        // Steem iow.cpp : DCD (GPIP1) = !(New && New < Old) sur les bits RTS|DTR.
        const uint8_t oldv = uint8_t(value_ & 0xFF);
        const uint8_t newv = uint8_t(a & (0x10 | 0x08));
        value_ = newv;
        // mfp_gpip_set_bit(1, level) : level 0 ↔ ligne DCD assertée (actif bas).
        mfp.setRs232Dcd(newv && newv < oldv);
        break;
    }
    default: break;
    }
}

void PortDongle::gpipRead(uint8_t& v, int64_t now) const {
    switch (type_) {
    case Type::Bat2:
        v &= uint8_t(~0x04);                         // CTS (GPIP2) à 0 en permanence
        break;
    case Type::MusicMaster: {
        // DTR recopié sur DCD avec ~200 cycles de retard (Steem ior.cpp, « inspired by
        // WinUAE ») : pendant ce délai on lit encore la valeur PRÉCÉDENTE de DTR.
        const bool bit = (now - timing_ > 200) ? (value_ & 1) != 0 : (value_ & 2) != 0;
        if (bit) v |= 0x02; else v &= uint8_t(~0x02);
        break;
    }
    default: break;
    }
}

// --- Port joystick ------------------------------------------------------------
void PortDongle::onJoystick(uint8_t& joy0, uint8_t& joy1) {
    switch (type_) {
    case Type::LeaderBoard:
    case Type::TenthFrame:
        joy1 |= 0x03;                                // haut ET bas : impossible au joystick
        break;
    case Type::Cricket:
    case Type::Soccer:
    case Type::Rugby:
        // Oscillateur : %1100 ↔ %1101 à chaque sonde (Steem ikbd.cpp).
        value_ = (value_ == 0xC) ? 0xD : 0xC;
        (type_ == Type::Rugby ? joy1 : joy0) |= uint8_t(value_);
        break;
    default: break;
    }
}

// --- Boutons de cartouche -----------------------------------------------------
void PortDongle::pressButton(Mfp& mfp) {
    if (!hasButton()) return;
    pressed_ = true;
    if (type_ == Type::Multiface) mfp.setMonitorButton(true);   // GPIP7 → 0
    else                          mfp.setRs232Ri(true);         // GPIP6 : RI assertée
}

void PortDongle::onVbl(Mfp& mfp) {
    if (!pressed_) return;
    pressed_ = false;
    if (type_ == Type::Multiface) mfp.setMonitorButton(false);
    else if (type_ == Type::Urc)  mfp.setRs232Ri(false);
}
