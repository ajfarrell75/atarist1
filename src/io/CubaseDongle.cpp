// Clés Steinberg (port cartouche /ROM3). Cf. CubaseDongle.hpp pour le contexte.
#include "io/CubaseDongle.hpp"
#include "core/StateArchive.hpp"

void CubaseDongle::reset() {
    d_ = 0;
    r_ = 0;
    chosen_ = (model_ == Model::Auto) ? Model::None : model_;
    locked_ = (model_ != Model::Auto);
}

uint8_t CubaseDongle::outputByte() const {
    switch (chosen_) {
        case Model::Cubase2: return d_;
        // Clé rouge : seule D8 est pilotée (bit 0 de l'octet fort), D9-D15 flottent
        // à 1 comme le reste du port.
        case Model::Cubase3: return uint8_t(0xFE | ((r_ >> 15) & 1));
        default:             return 0xFF;
    }
}

uint8_t CubaseDongle::cartRead(uint32_t addr, bool first) {
    if (model_ == Model::None) return 0xFF;
    if (!locked_ && first) {
        // Heuristique MiSTery : Cubase 3 interroge toujours avec A7..A1 = 0.
        chosen_ = (addr & 0xFE) ? Model::Cubase2 : Model::Cubase3;
        locked_ = true;
    }
    // L'octet faible n'est relié à rien.
    const uint8_t v = (addr & 1) ? 0xFF : outputByte();
    // /ROM3 remonte à la fin de l'accès : la clé rouge avance APRÈS avoir été lue.
    if (first && chosen_ == Model::Cubase3) clock3((addr >> 8) & 1);
    return v;
}

void CubaseDongle::udsCycle(uint32_t addr) {
    if (chosen_ == Model::Cubase2) clock2(uint8_t((addr >> 1) & 0xFF));
}

// --- Clé noire : PAL16R8, sorties actives bas (D = NOT(somme de produits)) -------
void CubaseDongle::clock2(uint8_t a) {
    const bool a1 = a & 0x01, a2 = a & 0x02, a3 = a & 0x04, a4 = a & 0x08;
    const bool a5 = a & 0x10, a6 = a & 0x20, a7 = a & 0x40, a8 = a & 0x80;
    const bool d8  = d_ & 0x01, d9  = d_ & 0x02, d10 = d_ & 0x04, d11 = d_ & 0x08;
    const bool d12 = d_ & 0x10, d13 = d_ & 0x20, d14 = d_ & 0x40, d15 = d_ & 0x80;
    // Terme commun à toutes les sorties : le « reset logiciel » A8..A1 = %11011000.
    const bool R = a8 && a7 && !a6 && a5 && a4 && !a3 && !a2 && !a1;

    const bool n15 = !(R
        || (d14 && d12 && d10 && a1)
        || (d13 && !d10 && a4)
        || (!d15 && !d14 && !d13 && !d12 && !d11 && d10 && !d9 && a4)
        || (!d14 && !d10 && a1)
        || (d15 && !d10 && a4)
        || (!d12 && !d10 && a1)
        || (!d8 && a5));
    const bool n14 = !(R
        || (!d15 && !d14 && !d13 && !d12 && !d11 && !d10 && !d9 && d8 && a4)
        || (d14 && d12 && d10 && d8 && a1)
        || (!d10 && !d8 && a1)
        || (!d12 && !d8 && a1)
        || (d15 && !d8 && a4)
        || (!d14 && !d8 && a1)
        || (!d15 && a5));
    const bool n13 = !(R
        || (d15 && d14 && d13 && d12 && d11 && d10 && d8 && a1)
        || (!d15 && !d13 && d11 && a4)
        || (d13 && !d11 && a4)
        || (!d12 && !d11 && a1)
        || (d15 && !d11 && a4)
        || (!d14 && !d11 && a1)
        || (!d9 && a5));
    const bool n12 = !(R
        || (d15 && d14 && d13 && d12 && d10 && d8 && a1)
        || (!d13 && !d10 && a1)
        || (!d15 && d13 && a4)
        || (!d13 && !d12 && a1)
        || (d15 && !d13 && a4)
        || (!d14 && !d13 && a1)
        || (!d11 && a5));
    const bool n11 = !(R
        || (d15 && d14 && d12 && d10 && d8 && a1)
        || (!d15 && !d8 && a1)
        || (!d15 && !d10 && a1)
        || (!d15 && !d12 && a1)
        || (!d15 && !d14 && a1)
        || (d15 && a4)
        || (!d13 && a5));
    const bool n10 = !(R
        || (d15 && d14 && d13 && d12 && d11 && d10 && d9 && d8 && a1)
        || (!d15 && !d13 && !d11 && d9 && a4)
        || (d11 && !d9 && a4)
        || (d13 && !d9 && a4)
        || (d15 && !d9 && a4)
        || (!d14 && !d9 && a1)
        || (!d14 && a5));
    const bool n9 = !(R
        || (!d15 && d14 && !d13 && !d11 && !d9 && a4)
        || (!d14 && d9 && a4)
        || (!d14 && d11 && a4)
        || (!d14 && d13 && a4)
        || (d15 && !d14 && a4)
        || (d14 && a1)
        || (!d12 && a5));
    const bool n8 = !(R
        || (!d15 && !d14 && !d13 && d12 && !d11 && !d9 && a4)
        || (d14 && d12 && a1)
        || (!d12 && d11 && a4)
        || (d13 && !d12 && a4)
        || (d15 && !d12 && a4)
        || (!d14 && !d12 && a1)
        || (!d10 && a5));

    d_ = uint8_t((n15 << 7) | (n14 << 6) | (n13 << 5) | (n12 << 4)
               | (n11 << 3) | (n10 << 2) | (n9 << 1) | (n8 << 0));
}

// --- Clé rouge : EPLD 5C060, bascules T (p ^= somme de produits), entrée A8 -------
void CubaseDongle::clock3(bool a8) {
    const uint16_t r = r_;
    auto bit = [r](int i) { return bool((r >> i) & 1); };
    const bool p03 = bit(0),  p04 = bit(1),  p05 = bit(2),  p06 = bit(3);
    const bool p07 = bit(4),  p08 = bit(5),  p09 = bit(6),  p10 = bit(7);
    const bool p15 = bit(8),  p16 = bit(9),  p17 = bit(10), p18 = bit(11);
    const bool p19 = bit(12), p20 = bit(13), p21 = bit(14), d8  = bit(15);

    const bool t03 = (!p03 && !a8) || (p03 && a8);
    const bool t04 = (!p04 && a8) || (p03 && !p04 && !a8) || (p03 && p04 && a8);
    const bool t05 = (p03 && !p05 && a8) || (p04 && p05 && a8) || (p03 && p04 && !p05 && !a8);
    const bool t06 = (p03 && !p06 && !a8) || (p04 && !p05 && p06)
                  || (p03 && p04 && p05 && !p06 && !a8);
    const bool t07 = (!p03 && p05 && !p07) || (!p04 && !p06 && p07 && a8)
                  || (p03 && p04 && p05 && p06 && !p07 && !a8);
    const bool t08 = (!p03 && !p05 && p07 && !p08) || (!p04 && p06 && p08 && a8)
                  || (p03 && p04 && p05 && p06 && p07 && !p08 && !a8);
    const bool t09 = (!p07 && p08 && !p09) || (p04 && !p05 && !p06 && p09)
                  || (p03 && p04 && p05 && p06 && p07 && p08 && !p09 && !a8);
    const bool t10 = (!p04 && p07 && !p08 && !p10) || (p05 && p06 && !p09 && p10)
                  || (p03 && p04 && p05 && p06 && p07 && p08 && p09 && !p10 && !a8);
    const bool all10 = p03 && p04 && p05 && p06 && p07 && p08 && p09 && p10 && !a8;
    const bool t15 = (!p07 && p08 && !p15) || (!p06 && p09 && !p10 && p15)
                  || (all10 && !p15);
    const bool t16 = (!p09 && !p15 && p16) || (!p08 && p10 && !p16)
                  || (all10 && p15 && !p16);
    const bool t17 = (!p08 && p17) || (!p10 && !p16 && !p17)
                  || (all10 && p15 && p16 && !p17);
    const bool t18 = (!p15 && p16 && p18) || (p08 && !p10 && p17 && !p18)
                  || (all10 && p15 && p16 && p17 && !p18);
    const bool t19 = (p10 && !p15 && !p19) || (p16 && !p17 && p18 && p19)
                  || (all10 && p15 && p16 && p17 && p18 && !p19);
    const bool t20 = (!p16 && !p19 && !p20) || (p17 && !p18 && p20)
                  || (all10 && p15 && p16 && p17 && p18 && p19 && !p20);
    const bool t21 = (!p17 && p18 && !p21) || (!p16 && p19 && !p20 && p21)
                  || (all10 && p15 && p16 && p17 && p18 && p19 && p20 && !p21);
    const bool t22 = (!p04 && d8) || (p05 && a8 && !d8)
                  || (p09 && !a8 && !p16 && !p18 && d8)
                  || (!p06 && p09 && a8 && p17 && !p21 && d8)
                  || (all10 && p15 && p16 && p17 && p18 && p19 && p20 && p21 && !d8);

    const uint16_t t = uint16_t((t03 << 0) | (t04 << 1) | (t05 << 2) | (t06 << 3)
                              | (t07 << 4) | (t08 << 5) | (t09 << 6) | (t10 << 7)
                              | (t15 << 8) | (t16 << 9) | (t17 << 10) | (t18 << 11)
                              | (t19 << 12) | (t20 << 13) | (t21 << 14) | (t22 << 15));
    r_ = uint16_t(r ^ t);
}

void CubaseDongle::serialize(StateArchive& ar) {
    uint8_t m = uint8_t(model_), c = uint8_t(chosen_);
    ar(m); ar(c); ar(locked_); ar(d_); ar(r_);
    if (ar.loading()) { model_ = Model(m & 3); chosen_ = Model(c & 3); }
}
