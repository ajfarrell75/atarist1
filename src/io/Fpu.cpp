// =============================================================================
//  Fpu.cpp — MC68881 en mode périphérique (socket Mega STE, $FFFA40-$FFFA5F).
//
//  Protocole (MC68881 UM §7 + AN-947, vérifié contre la glue SFP004 de la
//  MiNTLib) : le CPU écrit le mot de commande F-line dans le Command CIR
//  ($0A), lit le Response CIR ($00) qui lui dicte la suite (primitive de
//  transfert d'opérande avec direction + longueur, ou null « processing
//  finished »), et transfère les octets par la fenêtre Operand CIR ($10-$13,
//  les transferts > 4 octets bouclent dessus, poids fort d'abord). NeoST
//  exécute instantanément : le Response CIR ne renvoie JAMAIS $8900 (« come
//  again » = occupé), la boucle de scrutation SFP004 `cmpiw #0x8900` sort
//  donc au premier tour.
//
//  Arithmétique : registres en étendu 80 bits (bit-exact tant qu'on ne
//  calcule pas), calculs en double hôte (53 bits de mantisse au lieu de 64 —
//  limitation documentée, cf. Fpu.hpp). Constantes ROM FMOVECR bit-exactes
//  (dumps silicium recoupés MAME/Previous/WinUAE).
// =============================================================================
#include "Fpu.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace {

// ---- Bits du FPSR (MC68881 UM §4) -------------------------------------------
constexpr uint32_t CC_N   = 1u << 27, CC_Z = 1u << 26, CC_I = 1u << 25,
                   CC_NAN = 1u << 24;
// Octet EXC (bits 15-8) : exceptions de l'instruction courante.
constexpr uint32_t EXC_BSUN = 1u << 15, EXC_SNAN = 1u << 14, EXC_OPERR = 1u << 13,
                   EXC_OVFL = 1u << 12, EXC_UNFL = 1u << 11, EXC_DZ = 1u << 10,
                   EXC_INEX2 = 1u << 9, EXC_INEX1 = 1u << 8;
// Octet AEXC (bits 7-3) : exceptions accumulées (collantes).
constexpr uint32_t AEXC_IOP = 1u << 7, AEXC_OVFL = 1u << 6, AEXC_UNFL = 1u << 5,
                   AEXC_DZ = 1u << 4, AEXC_INEX = 1u << 3;

// Octets big-endian ↔ entiers.
uint32_t get32(const uint8_t* b) {
    return uint32_t(b[0]) << 24 | uint32_t(b[1]) << 16 | uint32_t(b[2]) << 8 | b[3];
}
void put32(uint8_t* b, uint32_t v) {
    b[0] = uint8_t(v >> 24); b[1] = uint8_t(v >> 16);
    b[2] = uint8_t(v >> 8);  b[3] = uint8_t(v);
}
uint64_t get64(const uint8_t* b) {
    return uint64_t(get32(b)) << 32 | get32(b + 4);
}
void put64(uint8_t* b, uint64_t v) {
    put32(b, uint32_t(v >> 32)); put32(b + 4, uint32_t(v));
}

} // namespace

// =============================================================================
//  Conversions étendu 80 bits ↔ double hôte
// =============================================================================
double Fpu::extToD(const Ext& e) {
    const int  exp = e.se & 0x7FFF;
    const bool neg = (e.se & 0x8000) != 0;
    double v;
    if (exp == 0x7FFF)                       // infini (mantisse nulle) ou NaN
        v = (e.man & 0x7FFFFFFFFFFFFFFFull) ? std::nan("")
                                            : std::numeric_limits<double>::infinity();
    else if (e.man == 0)
        v = 0.0;
    else                                     // normal ou dénormal (exp 0 → -16382)
        v = std::ldexp(double(e.man), (exp ? exp : 1) - 16383 - 63);
    return neg ? -v : v;
}

Fpu::Ext Fpu::dToExt(double d) {
    Ext e;
    const uint16_t sign = std::signbit(d) ? 0x8000 : 0;
    if (std::isnan(d))      { e.se = sign | 0x7FFF; e.man = 0xFFFFFFFFFFFFFFFFull; }
    else if (std::isinf(d)) { e.se = sign | 0x7FFF; e.man = 0; }
    else if (d == 0.0)      { e.se = sign;          e.man = 0; }
    else {
        int ex; const double m = std::frexp(std::fabs(d), &ex);   // m ∈ [0.5,1)
        int bexp = ex - 1 + 16383;
        uint64_t man = uint64_t(std::ldexp(m, 64));               // ∈ [2^63,2^64)
        if (bexp >= 0x7FFF)    { e.se = sign | 0x7FFF; e.man = 0; return e; }
        if (bexp <= 0)         { man >>= (1 - bexp); bexp = 0; }  // dénormal étendu
        e.se = sign | uint16_t(bexp); e.man = man;
    }
    return e;
}

// FPSR : codes condition d'après le pattern étendu (N=signe, Z, I, NAN).
void Fpu::setCC(const Ext& v) {
    const int exp = v.se & 0x7FFF;
    uint32_t cc = (v.se & 0x8000) ? CC_N : 0;
    if (exp == 0x7FFF)
        cc |= (v.man & 0x7FFFFFFFFFFFFFFFull) ? CC_NAN : CC_I;
    else if (v.man == 0)
        cc |= CC_Z;
    fpsr_ = (fpsr_ & 0x00FFFFFF) | cc;
}

// État d'arrondi softfloat depuis le FPCR : mode (bits 5-4) + précision (bits 7-6).
// Le FPCR ordonne RN/RZ/RM/RP ; softfloat ordonne RN/RM/RP/RZ → remappage explicite.
sf::Status Fpu::sfStatus() const {
    static const int rm[4] = { sf::round_nearest_even, sf::round_to_zero,
                               sf::round_down, sf::round_up };
    sf::Status s;
    s.roundingMode = rm[(fpcr_ >> 4) & 3];
    switch ((fpcr_ >> 6) & 3) {
        case 1:  s.roundingPrecision = sf::prec_single; break;   // simple
        case 2:  s.roundingPrecision = sf::prec_double; break;   // double
        default: s.roundingPrecision = sf::prec_extended; break; // étendu (00) / réservé (11)
    }
    return s;
}

// Replie les drapeaux d'exception softfloat dans le FPSR (octet EXC courant + octet
// AEXC collant). invalid → OPERR (cause la plus courante), les autres 1:1.
void Fpu::sfFold(uint8_t f) {
    if (f & sf::flag_signaling) fpsr_ |= EXC_SNAN  | AEXC_IOP;   // entrée SNaN → SNAN (vecteur 54)
    if (f & sf::flag_invalid)   fpsr_ |= EXC_OPERR | AEXC_IOP;
    if (f & sf::flag_divzero)   fpsr_ |= EXC_DZ    | AEXC_DZ;
    if (f & sf::flag_overflow)  fpsr_ |= EXC_OVFL  | AEXC_OVFL;
    if (f & sf::flag_underflow) fpsr_ |= EXC_UNFL;
    if (f & sf::flag_inexact)   fpsr_ |= EXC_INEX2;
    // Octet AEXC accumulé (cf. Hatari updateaccrued / fpsr_make_status) : UNFL n'est
    // accumulé que si INEXACT l'est AUSSI ; INEX est accumulé sur INEX2 OU OVFL.
    if ((f & sf::flag_underflow) && (f & sf::flag_inexact)) fpsr_ |= AEXC_UNFL;
    if (f & (sf::flag_inexact | sf::flag_overflow))         fpsr_ |= AEXC_INEX;
}

// Livraison d'exception FP via le Response CIR (cf. Fpu.hpp). Octet enable du FPCR
// (bits 15-8) ET octet status du FPSR (bits 15-8) alignés → `fpcr_ & fpsr_ & 0xFF00`.
void Fpu::checkException() {
    const uint32_t pend = fpcr_ & fpsr_ & 0xFF00u;
    if (!pend) return;
    // Priorité décroissante 68881 et vecteurs FP ($30-$36).
    static const struct { uint32_t bit; uint8_t vec; } tab[] = {
        { EXC_BSUN, 48 }, { EXC_SNAN, 54 }, { EXC_OPERR, 52 }, { EXC_OVFL, 53 },
        { EXC_UNFL, 51 }, { EXC_DZ,   50 }, { EXC_INEX2, 49 }, { EXC_INEX1, 49 },
    };
    for (const auto& t : tab)
        if (pend & t.bit) {
            excVector_ = t.vec;
            // « Take Pre-Instruction Exception » : CA=0 (terminal → la scrutation SFP004
            // sort), vecteur en octet bas. Encodage pragmatique cohérent avec le schéma
            // CIR NeoST (cf. setIdle) ; aucun logiciel ST en mode périphérique ne le décode.
            response_ = uint16_t(0x7000 | t.vec);
            return;
        }
}

// Arrondi d'une valeur à l'entier selon le mode FPCR (bits 5-4).
double Fpu::roundMode(double v) const {
    switch ((fpcr_ >> 4) & 3) {
        case 1:  return std::trunc(v);      // RZ : vers zéro
        case 2:  return std::floor(v);      // RM : vers -inf
        case 3:  return std::ceil(v);       // RP : vers +inf
        default: return std::nearbyint(v);  // RN : au plus près, pair (mode hôte)
    }
}

// =============================================================================
//  Constantes ROM FMOVECR (opclass 010, format 111) — patterns silicium exacts.
// =============================================================================
Fpu::Ext Fpu::romConstant(int off, int roundMode, bool& inexact) {
    struct Rom { uint8_t off; uint16_t se; uint64_t man; uint8_t inex; int8_t rnd[4]; };
    // inex (drapeau INEX2) + rnd[mode] (ajustement ±1 ulp du mot bas selon l'arrondi
    // {RN,RZ,RM,RP}) — port de fpp_cr[] (Hatari cpu/fpp.c) : sur le 68881, FMOVECR d'une
    // constante inexacte arme INEX2 et ajuste le dernier ulp selon le mode d'arrondi courant.
    static const Rom rom[] = {
        {0x00, 0x4000, 0xC90FDAA22168C235ull, 1, {0,-1,-1, 0}},   // pi
        {0x0B, 0x3FFD, 0x9A209A84FBCFF798ull, 1, {0, 0, 0, 1}},   // log10(2)
        {0x0C, 0x4000, 0xADF85458A2BB4A9Aull, 1, {0, 0, 0, 1}},   // e (1 ulp sous l'arrondi : silicium)
        {0x0D, 0x3FFF, 0xB8AA3B295C17F0BCull, 1, {0,-1,-1, 0}},   // log2(e)
        {0x0E, 0x3FFD, 0xDE5BD8A937287195ull, 0, {0, 0, 0, 0}},   // log10(e)
        {0x0F, 0x0000, 0x0000000000000000ull, 0, {0, 0, 0, 0}},   // 0.0
        {0x30, 0x3FFE, 0xB17217F7D1CF79ACull, 1, {0,-1,-1, 0}},   // ln(2)
        {0x31, 0x4000, 0x935D8DDDAAA8AC17ull, 1, {0,-1,-1, 0}},   // ln(10)
        {0x32, 0x3FFF, 0x8000000000000000ull, 0, {0, 0, 0, 0}},   // 1.0
        {0x33, 0x4002, 0xA000000000000000ull, 0, {0, 0, 0, 0}},   // 10^1
        {0x34, 0x4005, 0xC800000000000000ull, 0, {0, 0, 0, 0}},   // 10^2
        {0x35, 0x400C, 0x9C40000000000000ull, 0, {0, 0, 0, 0}},   // 10^4
        {0x36, 0x4019, 0xBEBC200000000000ull, 0, {0, 0, 0, 0}},   // 10^8
        {0x37, 0x4034, 0x8E1BC9BF04000000ull, 0, {0, 0, 0, 0}},   // 10^16
        {0x38, 0x4069, 0x9DC5ADA82B70B59Eull, 1, {0,-1,-1, 0}},   // 10^32
        {0x39, 0x40D3, 0xC2781F49FFCFA6D5ull, 1, {0, 0, 0, 1}},   // 10^64
        {0x3A, 0x41A8, 0x93BA47C980E98CE0ull, 1, {0,-1,-1, 0}},   // 10^128
        {0x3B, 0x4351, 0xAA7EEBFB9DF9DE8Eull, 1, {0,-1,-1, 0}},   // 10^256
        {0x3C, 0x46A3, 0xE319A0AEA60E91C7ull, 1, {0,-1,-1, 0}},   // 10^512
        {0x3D, 0x4D48, 0xC976758681750C17ull, 1, {0, 0, 0, 1}},   // 10^1024
        {0x3E, 0x5A92, 0x9E8B3B5DC53D5DE5ull, 1, {0,-1,-1, 0}},   // 10^2048
        {0x3F, 0x7525, 0xC46052028A20979Bull, 1, {0,-1,-1, 0}},   // 10^4096
    };
    for (const auto& r : rom)
        if (r.off == off) {
            inexact = r.inex != 0;
            uint64_t man = r.man;
            const int8_t d = r.rnd[roundMode & 3];
            if (d) { uint32_t lo = uint32_t(man) + uint32_t(int32_t(d)); man = (man & 0xFFFFFFFF00000000ull) | lo; }
            return Ext{r.se, man};
        }
    inexact = false;
    return Ext{0, 0};                            // offsets non documentés → 0.0
}

// =============================================================================
//  Formats d'opérande (champ source/destination, bits 12-10 du mot de commande)
// =============================================================================
int Fpu::fmtLen(int fmt) {
    static const int len[8] = {4, 4, 12, 12, 2, 8, 1, 0};   // L S X P W D B (CR)
    return len[fmt & 7];
}

Fpu::Ext Fpu::decodeFmt(int fmt, const uint8_t* b) {
    switch (fmt) {
        case 0:  return dToExt(double(int32_t(get32(b))));            // L
        case 1: {                                                     // S — élargissement EXACT
            // Port de float32_to_floatx80 (bit à bit) : l'ancien double(f) hôte
            // quiétait un SNaN (cvtss2sd) et dToExt écrasait le payload — un SNaN
            // simple ne levait plus jamais FPSR.SNAN en aval.
            const uint32_t u = get32(b);
            const int      sign = int(u >> 31);
            int32_t        e    = int32_t((u >> 23) & 0xFF);
            uint32_t       f    = u & 0x007FFFFFu;
            if (e == 0xFF) {
                if (f) return Ext{uint16_t(sign << 15 | 0x7FFF), uint64_t(f) << 40};  // NaN (bit signalant préservé)
                // ±inf : mantisse 0 (floatx80_default_infinity_low, forme canonique
                // 68881) — bit 63 posé donnerait une écriture FMOVE.X ≠ oracle.
                return Ext{uint16_t(sign << 15 | 0x7FFF), 0};
            }
            if (e == 0) {
                if (f == 0) return Ext{uint16_t(sign << 15), 0};
                const int sh = sf::clz64(f) - 32 - 8;                 // normalizeFloat32Subnormal
                f <<= sh; e = 1 - sh;
            }
            f |= 0x00800000u;
            return Ext{uint16_t(sign << 15 | (e + 0x3F80)), uint64_t(f) << 40};
        }
        case 2:  return Ext{uint16_t(b[0] << 8 | b[1]), get64(b + 4)}; // X (bit-exact)
        case 3: {                                                     // P (BCD)
            // SM(b95) SE(b94) YY | E2 E1 E0 | [E3] D16 | D15..D0 → via strtod.
            char s[32]; int p = 0;
            if (b[0] & 0x80) s[p++] = '-';
            s[p++] = char('0' + (b[3] & 0x0F)); s[p++] = '.';
            for (int i = 4; i < 12; i++) {
                s[p++] = char('0' + (b[i] >> 4)); s[p++] = char('0' + (b[i] & 0x0F));
            }
            s[p++] = 'e';
            if (b[0] & 0x40) s[p++] = '-';
            s[p++] = char('0' + (b[0] & 0x0F));
            s[p++] = char('0' + (b[1] >> 4)); s[p++] = char('0' + (b[1] & 0x0F));
            s[p] = 0;
            return dToExt(std::strtod(s, nullptr));
        }
        case 4:  return dToExt(double(int16_t(b[0] << 8 | b[1])));    // W
        case 5: {                                                     // D — élargissement EXACT
            // Port de float64_to_floatx80 (même raison que le format S).
            const uint64_t u = get64(b);
            const int      sign = int(u >> 63);
            int32_t        e    = int32_t((u >> 52) & 0x7FF);
            uint64_t       f    = u & 0x000FFFFFFFFFFFFFull;
            if (e == 0x7FF) {
                if (f) return Ext{uint16_t(sign << 15 | 0x7FFF), f << 11};            // NaN (signalant préservé)
                // ±inf : mantisse 0 (cf. format S ci-dessus).
                return Ext{uint16_t(sign << 15 | 0x7FFF), 0};
            }
            if (e == 0) {
                if (f == 0) return Ext{uint16_t(sign << 15), 0};
                const int sh = sf::clz64(f) - 11;                     // normalizeFloat64Subnormal
                f <<= sh; e = 1 - sh;
            }
            f |= 0x0010000000000000ull;
            return Ext{uint16_t(sign << 15 | (e + 0x3C00)), f << 11};
        }
        case 6:  return dToExt(double(int8_t(b[0])));                 // B
        default: return Ext{0, 0};
    }
}

void Fpu::encodeFmt(int fmt, const Ext& v, uint8_t* b, int k) {
    const double d = extToD(v);
    // Entier : arrondi selon FPCR, saturation + OPERR en cas de débordement.
    auto toInt = [&](double lo, double hi) -> int64_t {
        if (std::isnan(d)) { fpsr_ |= EXC_OPERR; fpsr_ |= AEXC_IOP; return 0; }
        double r = roundMode(d);
        if (r < lo || r > hi) {
            fpsr_ |= EXC_OPERR; fpsr_ |= AEXC_IOP;
            r = r < lo ? lo : hi;
        }
        return int64_t(r);
    };
    switch (fmt) {
        case 0:  put32(b, uint32_t(toInt(-2147483648.0, 2147483647.0))); break; // L
        case 1: {                                                               // S
            const float f = float(d); uint32_t u; std::memcpy(&u, &f, 4);
            if (std::isinf(f) && !std::isinf(d)) { fpsr_ |= EXC_OVFL | AEXC_OVFL; }
            put32(b, u); break;
        }
        case 2:                                                                 // X
            b[0] = uint8_t(v.se >> 8); b[1] = uint8_t(v.se);
            b[2] = b[3] = 0; put64(b + 4, v.man); break;
        case 3: case 7: {                                                       // P
            // k-factor : nombre de digits significatifs (k ≤ 0 = style point
            // fixe, approché ici par 17 digits — limite documentée).
            int digits = (k & 0x40) ? 17 : k;                // k signé 7 bits
            if (digits < 1 || digits > 17) digits = 17;
            char s[40];
            std::snprintf(s, sizeof s, "%+.*e", digits - 1, d);
            std::memset(b, 0, 12);
            if (s[0] == '-') b[0] |= 0x80;
            b[3] = uint8_t(s[1] - '0');                      // D16 (partie entière)
            int bi = 4, hi = 1;                              // fraction → b[4..11]
            const char* c = s + 2;
            if (*c == '.') c++;                              // "%.0e" n'émet pas de point
            for (; *c && *c != 'e' && bi < 12; c++) {
                if (hi) b[bi] = uint8_t((*c - '0') << 4); else b[bi++] |= uint8_t(*c - '0');
                hi ^= 1;
            }
            const char* e = std::strchr(s, 'e');
            int ev = e ? std::atoi(e + 1) : 0;
            if (ev < 0) { b[0] |= 0x40; ev = -ev; }
            b[0] |= uint8_t((ev / 100) % 10);
            b[1] = uint8_t(((ev / 10) % 10) << 4 | (ev % 10));
            break;
        }
        case 4: {                                                               // W
            const int64_t i = toInt(-32768.0, 32767.0);
            b[0] = uint8_t(i >> 8); b[1] = uint8_t(i); break;
        }
        case 5: {                                                               // D
            uint64_t u; std::memcpy(&u, &d, 8); put64(b, u); break;
        }
        case 6:  b[0] = uint8_t(toInt(-128.0, 127.0)); break;                   // B
        default: std::memset(b, 0, 12); break;
    }
}

// =============================================================================
//  Interface CIR
// =============================================================================
void Fpu::reset() {
    for (auto& r : fp_) r = Ext{};               // NaN, comme le 68881 au reset
    fpcr_ = fpsr_ = fpiar_ = 0;
    for (auto& b : latch_) b = 0;
    excVector_ = 0;
    setIdle();
    traceCount_ = 0;
}

void Fpu::setIdle() {
    response_ = 0x0802;                          // null : PF=1, TF=0
    bufLen_ = bufPos_ = 0; bufIn_ = false; after_ = After::None;
}

// Primitive de transfert courante : longueur restante par tranches de 12 max
// (le vrai FPU ré-émet une primitive par tranche, CA=1 → le CPU relit).
void Fpu::armIn(int len, After after) {
    bufLen_ = len; bufPos_ = 0; bufIn_ = true; after_ = after;
    const int chunk = len > 12 ? 12 : len;
    response_ = uint16_t((chunk <= 4 ? 0x9500 : 0x9600) | chunk);
}
void Fpu::armOut(int len, After after) {
    bufLen_ = len; bufPos_ = 0; bufIn_ = false; after_ = after;
    const int chunk = len > 12 ? 12 : len;
    response_ = uint16_t((chunk <= 4 ? 0xB100 : 0xB200) | chunk);
}

uint8_t Fpu::read8(uint32_t addr) {
    const uint32_t off = (addr - BASE) & 0x1F;
    switch (off & ~1u) {
        case 0x00:                               // Response CIR
            return uint8_t(off & 1 ? response_ : response_ >> 8);
        case 0x04:                               // Save CIR : trame IDLE 68881
            // Format $1F18 (version $1F, 24 octets de corps à lire ensuite par
            // l'Operand CIR — corps neutre, FRESTORE le ré-avale sans état).
            if (off & 1) {
                std::memset(buf_, 0, 24);
                armOut(24, After::MoveOutDone);
                return 0x18;
            }
            return 0x1F;
        case 0x10: case 0x12:                    // Operand CIR (fenêtre 4 octets)
            if (!bufIn_ && bufPos_ < bufLen_) {  // transfert FPU → CPU en cours
                const uint8_t v = buf_[bufPos_++];
                if (bufPos_ >= bufLen_) {
                    setIdle();
                    // Fin de drain d'un move-out : les exceptions levées par
                    // encodeFmt (OPERR saturation entière, OVFL simple) doivent
                    // pouvoir être livrées via le Response — comme genOp/FMOVECR.
                    checkException();
                } else {
                    const int rem = bufLen_ - bufPos_, chunk = rem > 12 ? 12 : rem;
                    response_ = uint16_t((chunk <= 4 ? 0xB100 : 0xB200) | chunk);
                }
                return v;
            }
            return latch_[off];
        default:
            return latch_[off];                  // Restore/RegSelect/latches divers
    }
}

void Fpu::write8(uint32_t addr, uint8_t v) {
    const uint32_t off = (addr - BASE) & 0x1F;
    latch_[off] = v;
    switch (off) {
        case 0x02: case 0x03:                    // Control CIR : abort/acquittement
            setIdle();
            break;
        case 0x07:                               // Restore CIR (mot complet écrit)
            restoreHeader(uint16_t(latch_[0x06] << 8 | v));
            break;
        case 0x0B:                               // Command CIR (mot complet écrit)
            command(uint16_t(latch_[0x0A] << 8 | v));
            break;
        case 0x0F:                               // Condition CIR : évaluer prédicat
            condition(uint16_t(latch_[0x0E] << 8 | v));
            break;
        case 0x10: case 0x11: case 0x12: case 0x13:   // Operand CIR
            if (bufIn_ && bufPos_ < bufLen_) {
                buf_[bufPos_++] = v;
                if (bufPos_ >= bufLen_) { bufIn_ = false; completeInput(); }
                else {
                    const int rem = bufLen_ - bufPos_, chunk = rem > 12 ? 12 : rem;
                    response_ = uint16_t((chunk <= 4 ? 0x9500 : 0x9600) | chunk);
                }
            }
            break;
        default:
            break;                               // Operation/InstrAddr/... : latch
    }
}

// =============================================================================
//  Décodage des commandes (mot F-line écrit dans le Command CIR)
// =============================================================================
void Fpu::command(uint16_t cmd) {
    trace("commande", cmd);
    cmd_ = cmd;
    setIdle();                                   // toute commande annule un transfert
    switch (cmd >> 13) {
        case 0:                                  // opclass 000 : FPm → FPn
            genOp(cmd, fp_[(cmd >> 10) & 7]);
            break;
        case 2: {                                // opclass 010 : <ea> → FPn / FMOVECR
            const int fmt = (cmd >> 10) & 7;
            if (fmt == 7) {                      // FMOVECR (offset ROM bits 6-0)
                bool inexact = false;
                const Ext c = romConstant(cmd & 0x7F, (fpcr_ >> 4) & 3, inexact);
                fp_[(cmd >> 7) & 7] = c;
                fpsr_ &= ~0x0000FF00u;            // EXC effacé (instruction courante)
                if (inexact) fpsr_ |= EXC_INEX2 | AEXC_INEX;   // constante inexacte → INEX2
                setCC(c);
                checkException();                // livre l'exception INEX si armée dans le FPCR
                break;                           // response déjà idle (PF=1)
            }
            armIn(fmtLen(fmt), After::GenOp);
            break;
        }
        case 3:                                  // opclass 011 : FMOVE FPn → <ea>
            startMoveOut(cmd);
            break;
        case 4: {                                // opclass 100 : <ea> → FPCR/FPSR/FPIAR
            // Masque VIDE = FPIAR (quirk 68881, cf. Hatari fpp.c:3317 « No control
            // register bits set: FPIAR ») — l'ancien no-op laissait l'instruction
            // sans transfert.
            if (!(cmd & 0x1C00)) cmd_ = cmd = uint16_t(cmd | 0x0400);
            const int n = !!(cmd & 0x1000) + !!(cmd & 0x0800) + !!(cmd & 0x0400);
            if (n) armIn(4 * n, After::CtrlIn);
            break;
        }
        case 5: {                                // opclass 101 : FPCR/FPSR/FPIAR → <ea>
            if (!(cmd & 0x1C00)) cmd = uint16_t(cmd | 0x0400);   // masque vide = FPIAR (fpp.c:3366)
            int p = 0;
            if (cmd & 0x1000) { put32(buf_ + p, fpcr_);  p += 4; }
            if (cmd & 0x0800) { put32(buf_ + p, fpsr_);  p += 4; }
            if (cmd & 0x0400) { put32(buf_ + p, fpiar_); p += 4; }
            if (p) armOut(p, After::MoveOutDone);
            break;
        }
        case 6: {                                // opclass 110 : FMOVEM <ea> → FPn
            int n = 0;
            for (int i = 0; i < 8; i++) if (cmd & (0x80 >> i)) n++;
            if (cmd & 0x0800) trace("FMOVEM dynamique (masque registre inconnu)", cmd);
            if (n) armIn(12 * n, After::MovemIn);
            break;
        }
        case 7: {                                // opclass 111 : FMOVEM FPn → <ea>
            // Masque MSB-first = premier registre transféré : FP0→FP7 en
            // post-incrément (bit12), FP7→FP0 en pré-décrément.
            int p = 0;
            const bool post = (cmd & 0x1000) != 0;
            if (cmd & 0x0800) trace("FMOVEM dynamique (masque registre inconnu)", cmd);
            for (int k = 0; k < 8; k++) {
                if (!(cmd & (0x80 >> k))) continue;
                const int reg = post ? k : 7 - k;
                encodeFmt(2, fp_[reg], buf_ + p, 0);   // toujours étendu 12 octets
                p += 12;
            }
            if (p) armOut(p, After::MoveOutDone);
            break;
        }
        default:                                 // opclass 001 : réservé
            trace("opclass réservé", cmd);
            break;
    }
}

// FMOVE FPn → mémoire (opclass 011) : encode le résultat, arme la sortie.
void Fpu::startMoveOut(uint16_t cmd) {
    const int fmt = (cmd >> 10) & 7;             // 011 = P k statique, 111 = P k Dn
    const Ext& src = fp_[(cmd >> 7) & 7];
    fpsr_ &= ~0x0000FF00u;                       // EXC effacé en début d'instruction
    // P à k-factor dynamique (fmt 111) : k vit dans un registre Dn du CPU,
    // inaccessible en mode périphérique → défaut 17 digits.
    encodeFmt(fmt, src, buf_, fmt == 7 ? 0 : cmd & 0x7F);
    setCC(src);
    armOut(fmt == 7 ? 12 : fmtLen(fmt), After::MoveOutDone);
}

// Tampon d'entrée complet → exécution selon le contexte.
void Fpu::completeInput() {
    switch (after_) {
        case After::GenOp:
            genOp(cmd_, decodeFmt((cmd_ >> 10) & 7, buf_));
            break;
        case After::CtrlIn: {
            int p = 0;
            // Masques 68881 (cf. Hatari get_features) : FPCR bits 3-0 et FPSR bits
            // 0-2 / 28-31 sont réservés → forcés à 0 (relus à 0).
            if (cmd_ & 0x1000) { fpcr_  = get32(buf_ + p) & 0xFFF0;     p += 4; }
            if (cmd_ & 0x0800) { fpsr_  = get32(buf_ + p) & 0x0FFFFFF8; p += 4; }
            if (cmd_ & 0x0400) { fpiar_ = get32(buf_ + p);          p += 4; }
            setIdle();
            break;
        }
        case After::MovemIn: {
            const bool post = (cmd_ & 0x1000) != 0;
            int p = 0;
            for (int k = 0; k < 8; k++) {
                if (!(cmd_ & (0x80 >> k))) continue;
                const int reg = post ? k : 7 - k;
                fp_[reg] = decodeFmt(2, buf_ + p);
                p += 12;
            }
            setIdle();
            break;
        }
        case After::RestoreIn:                   // corps de trame FRESTORE : avalé
        default:
            setIdle();
            break;
    }
}

// Restore CIR : trame nulle (version 0) = reset logiciel du FPU, sinon avaler
// le corps de la trame (NeoST ne garde pas d'état interne à restaurer).
void Fpu::restoreHeader(uint16_t fmt) {
    if ((fmt >> 8) == 0) {
        for (auto& r : fp_) r = Ext{};
        fpcr_ = fpsr_ = fpiar_ = 0;
        setIdle();
        return;
    }
    const int body = fmt & 0xFF;
    if (body > 0 && body <= int(sizeof buf_)) armIn(body, After::RestoreIn);
    else setIdle();
}

// Condition CIR : évalue un prédicat FBcc/FScc/FDBcc → null avec TF.
void Fpu::condition(uint16_t pred) {
    const bool n = fpsr_ & CC_N, z = fpsr_ & CC_Z, nan = fpsr_ & CC_NAN;
    bool t;
    switch (pred & 0x0F) {                       // table MC68881 UM §4.8
        case 0x0: t = false;                break;   // F   / SF
        case 0x1: t = z;                    break;   // EQ  / SEQ
        case 0x2: t = !(nan || z || n);     break;   // OGT / GT
        case 0x3: t = z || !(nan || n);     break;   // OGE / GE
        case 0x4: t = n && !(nan || z);     break;   // OLT / LT
        case 0x5: t = z || (n && !nan);     break;   // OLE / LE
        case 0x6: t = !(nan || z);          break;   // OGL / GL
        case 0x7: t = !nan;                 break;   // OR  / GLE
        case 0x8: t = nan;                  break;   // UN  / NGLE
        case 0x9: t = nan || z;             break;   // UEQ / NGL
        case 0xA: t = nan || !(n || z);     break;   // UGT / NLE
        case 0xB: t = nan || z || !n;       break;   // UGE / NLT
        case 0xC: t = nan || (n && !z);     break;   // ULT / NGE
        case 0xD: t = nan || z || n;        break;   // ULE / NGT
        case 0xE: t = !z;                   break;   // NE  / SNE
        default:  t = true;                 break;   // T   / ST
    }
    if ((pred & 0x10) && nan)                    // prédicats « signaling » sur NaN
        fpsr_ |= EXC_BSUN | AEXC_IOP;
    response_ = uint16_t(0x0802 | (t ? 1 : 0));  // null : PF=1, TF=prédicat
    bufLen_ = bufPos_ = 0; bufIn_ = false; after_ = After::None;
    checkException();                            // BSUN activé → livraison d'exception
}

// =============================================================================
//  Opérations générales (opclass 000/010) : dst = op(dst, src)
// =============================================================================
void Fpu::genOp(uint16_t cmd, Ext src) {
    const int dn = (cmd >> 7) & 7;
    const int op = cmd & 0x7F;
    fpsr_ &= ~0x0000FF00u;                       // EXC effacé en début d'instruction

    // -------- Opérations BIT-EXACTES (pas de calcul) ------------------------
    // Une entrée SNaN est QUIÉTÉE + FPSR.SNAN levé, comme floatx80_move/abs/neg
    // (propagateFloatx80NaNOneArg) — l'ancien code stockait le SNaN brut sans drapeau.
    if (op == 0x00 || op == 0x18 || op == 0x1A) {
        Ext r = src;
        sf::f80 f = toF(r);
        if (sf::isNaN(f)) {
            // Tout NaN traverse propagateNaNOneArg (quiété, signe INCHANGÉ — abs/neg
            // ne touchent pas le signe d'un NaN chez floatx80_abs/neg).
            sf::Status q{};
            f = sf::propagateNaN1(q, f);
            sfFold(q.exceptionFlags);
            r = toE(f);
        }
        else if (op == 0x18) r.se &= 0x7FFF;                                  // FABS
        else if (op == 0x1A) r.se ^= 0x8000;                                  // FNEG
        fp_[dn] = r; setCC(r); setIdle(); checkException(); return;
    }

    // -------- Opérations ALGÉBRIQUES en softfloat 80 bits (mantisse 64 bits) -
    sf::Status st = sfStatus();
    const sf::f80 s = toF(src), d = toF(fp_[dn]);
    bool handled = true, store = true;
    sf::f80 rf{};
    switch (op) {
        case 0x04: case 0x05: rf = sf::sqrt_(s, st);    break;                 // FSQRT (0x05 alias)
        case 0x20: rf = sf::div(d, s, st);              break;                 // FDIV
        case 0x22: rf = sf::add(d, s, st);              break;                 // FADD
        case 0x23: rf = sf::mul(d, s, st);              break;                 // FMUL
        case 0x28: rf = sf::sub(d, s, st);              break;                 // FSUB
        case 0x01: rf = sf::roundToInt(s, st);          break;                 // FINT
        case 0x03: { sf::Status z = st; z.roundingMode = sf::round_to_zero;    // FINTRZ
                     rf = sf::roundToInt(s, z); }       break;
        case 0x1F: rf = sf::getMan(s, st);              break;                 // FGETMAN
        case 0x1E: {                                                           // FGETEXP
            // Ordre d'Hatari (softfloat.c:3201) : le NaN passe par propagateNaN1, qui
            // QUIÈTE un SNaN et lève flag_signaling → FPSR.SNAN + AEXC.IOP (vecteur 54
            // si armé). L'ancien code rendait l'opérande BRUT : le SNaN restait
            // signalant et aucun drapeau n'était posé — cf. le même correctif déjà
            // appliqué à FMOVE/FABS/FNEG, et FGETMAN qui délègue déjà à propagateNaN1.
            if (sf::expOf(s) == 0x7FFF) {
                if (sf::isNaN(s)) { rf = sf::propagateNaN1(st, s); break; }
                rf = sf::defaultNaN(); st.exceptionFlags |= sf::flag_invalid; break;   // ±∞ → OPERR
            }
            bool sp; int32_t e = sf::getExpUnbiased(s, sp);
            rf = sp ? s : toF(dToExt(double(e)));                               // sp ici ⇒ ±0 → ±0
            break;
        }
        case 0x26: {                                                          // FSCALE : d × 2^trunc(s)
            // NaN → propagation ; exposant ±∞ → OPERR + NaN par défaut ; d = ±∞/±0
            // inchangé (port floatx80_scale, softfloat.c:3258).
            if (sf::isNaN(s) || sf::isNaN(d)) { rf = sf::propagateNaN(st, d, s); break; }
            if (sf::expOf(s) == 0x7FFF) { rf = sf::defaultNaN(); st.exceptionFlags |= sf::flag_invalid; break; }
            if (sf::expOf(d) == 0x7FFF ||
                (sf::expOf(d) == 0 && sf::fracOf(d) == 0)) { rf = d; break; }
            // n = partie entière de s, EXACTE jusqu'à |n| ≤ 131071 (bExp ≤ $400F),
            // extraite de l'ÉTENDU directement (l'ancien trunc(double) clampé à
            // ±32768 rendait FSCALE(2^16383, −32769) = 2^−16385 au lieu de 2^−16386).
            // Au-delà, floatx80_scale SATURE l'exposant (−$6001 / $E000) → under/
            // overflow garanti par roundAndPack, avec les drapeaux corrects.
            int32_t aExp = sf::expOf(d); uint64_t aSig = sf::fracOf(d);
            if (aExp == 0) sf::normalizeSubnormal(aSig, aExp, aSig);
            const int32_t  bExp = sf::expOf(s);
            const uint64_t bSig = sf::fracOf(s);
            if (bExp > 0x400F) {
                aExp = sf::signOf(s) ? -0x6001 : 0xE000;
            } else if (bExp >= 0x3FFF) {
                const int32_t n = int32_t(bSig >> (0x403E - bExp));
                aExp = sf::signOf(s) ? aExp - n : aExp + n;
            }   // bExp < $3FFF → |s| < 1 → trunc = 0, exposant inchangé
            rf = sf::roundAndPack(st.roundingPrecision, sf::signOf(d), aExp, aSig, 0, st);
            break;
        }
        case 0x21: case 0x25: {                                               // FMOD / FREM
            uint64_t qq; int ss;
            rf = sf::rem(d, s, qq, ss, op == 0x21, st);
            uint32_t qb = uint32_t(qq & 0x7F); if (ss) qb |= 0x80;            // octet quotient FPSR
            fpsr_ = (fpsr_ & 0xFF00FFFFu) | (qb << 16);
            break;
        }
        case 0x24: { sf::Status z = st; z.roundingPrecision = sf::prec_single;   // FSGLDIV
                     rf = sf::div(d, s, z); } break;
        case 0x27: { sf::Status z = st; z.roundingPrecision = sf::prec_single;   // FSGLMUL
                     auto da = d, sa = s;                                        // entrées tronquées à
                     da.low &= 0xFFFFFF0000000000ull;                            // 24 bits AVANT le produit
                     sa.low &= 0xFFFFFF0000000000ull;                            // (cf. Hatari floatx80_sglmul)
                     rf = sf::mul(da, sa, z); } break;
        case 0x38: case 0x39: {                                               // FCMP (0x39 alias)
            // Port de floatx80_cmp (softfloat.c:3345) : les CC dérivent du RÉSULTAT
            // symbolique pack(sign,…) — en particulier Z **et** N pour −0 vs ±0 et
            // −∞ vs −∞ (l'ancien compare() ne posait que Z), et une entrée SNaN
            // lève le drapeau signalant (→ FPSR.SNAN).
            store = false;
            uint32_t cc = 0;
            if (sf::isNaN(d) || sf::isNaN(s)) {
                if (sf::isSNaN(d) || sf::isSNaN(s)) st.exceptionFlags |= sf::flag_signaling;
                cc = CC_NAN;
            } else {
                const int32_t  aExp  = sf::expOf(d),  bExp  = sf::expOf(s);
                const uint64_t aSig  = sf::fracOf(d), bSig  = sf::fracOf(s);
                const int      aSign = sf::signOf(d), bSign = sf::signOf(s);
                int z = 0, neg = 0;
                if      (bExp < aExp)        { neg = aSign; }
                else if (aExp < bExp)        { neg = bSign ^ 1; }
                else if (aExp == 0x7FFF)     { z = (aSign == bSign); neg = aSign; }
                else if (bSig < aSig)        { neg = aSign; }
                else if (aSig < bSig)        { neg = bSign ^ 1; }
                else if (aSig == 0)          { z = 1; neg = aSign; }          // ±0 vs ±0 : N = signe de FPn
                else if (aSign == bSign)     { z = 1; }
                else                         { neg = aSign; }
                cc = (z ? CC_Z : 0) | (neg ? CC_N : 0);
            }
            fpsr_ = (fpsr_ & 0x00FFFFFF) | cc;
            break;
        }
        case 0x3A: case 0x3B:                                                 // FTST (0x3B alias)
            store = false;
            if (sf::isSNaN(s)) st.exceptionFlags |= sf::flag_signaling;       // floatx80_tst
            setCC(src);
            break;
        default: handled = false; break;                                      // → chemin transcendantes
    }
    if (handled) {
        sfFold(st.exceptionFlags);
        if (store) { fp_[dn] = toE(rf); setCC(fp_[dn]); }
        setIdle(); checkException();
        return;
    }

    // -------- Transcendantes (et opmodes restants) : via le FPU hôte --------
    // Le 68881 les approxime lui-même (CORDIC/polynômes) ; la bit-exactitude est
    // hors de portée — on calcule en `double` hôte puis on convertit (cf. Fpu.hpp,
    // comme MAME/Previous). L'arithmétique algébrique, elle, est en 64 bits ci-dessus.
    const double sd = extToD(src);
    double r = 0.0;
    bool store2 = true;
    switch (op) {
        case 0x02: r = std::sinh(sd);     break;  // FSINH
        case 0x06: r = std::log1p(sd);    break;  // FLOGNP1
        case 0x08: r = std::expm1(sd);    break;  // FETOXM1
        case 0x09: r = std::tanh(sd);     break;  // FTANH
        case 0x0A: r = std::atan(sd);     break;  // FATAN
        case 0x0C: r = std::asin(sd);     break;  // FASIN
        case 0x0D: r = std::atanh(sd);    break;  // FATANH
        case 0x0E: r = std::sin(sd);      break;  // FSIN
        case 0x0F: r = std::tan(sd);      break;  // FTAN
        case 0x10: r = std::exp(sd);      break;  // FETOX
        case 0x11: r = std::exp2(sd);     break;  // FTWOTOX
        case 0x12: r = std::pow(10.0, sd); break; // FTENTOX
        case 0x14: r = std::log(sd);      break;  // FLOGN
        case 0x15: r = std::log10(sd);    break;  // FLOG10
        case 0x16: r = std::log2(sd);     break;  // FLOG2
        case 0x19: r = std::cosh(sd);     break;  // FCOSH
        case 0x1C: r = std::acos(sd);     break;  // FACOS
        case 0x1D: r = std::cos(sd);      break;  // FCOS
        case 0x30: case 0x31: case 0x32: case 0x33:   // FSINCOS : cos → FPc, sin → FPn
        case 0x34: case 0x35: case 0x36: case 0x37:
            fp_[op & 7] = dToExt(std::cos(sd)); r = std::sin(sd); break;
        default: trace("opmode non implémenté", cmd); store2 = false; break;
    }
    if (store2) {
        if (std::isnan(r) && !std::isnan(sd))                  fpsr_ |= EXC_OPERR | AEXC_IOP;
        if (std::isinf(r) && !std::isinf(sd))                  fpsr_ |= EXC_OVFL | AEXC_OVFL | EXC_INEX2 | AEXC_INEX;
        if (((fpcr_ >> 6) & 3) == 1) r = double(float(r));     // précision simple
        const Ext res = dToExt(r);
        fp_[dn] = res; setCC(res);
    }
    setIdle();
    checkException();
}

// =============================================================================
//  Trace de débogage (anti-spam : 32 premiers événements)
// =============================================================================
void Fpu::trace(const char* what, uint16_t v) {
    if (traceCount_ >= 32) return;
    std::fprintf(stderr, "[fpu] %s $%04X%s\n", what, v,
                 ++traceCount_ == 32 ? " (suite du dialogue non journalisée)" : "");
}
