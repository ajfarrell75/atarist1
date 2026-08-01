// Symbols.cpp — cf. Symbols.hpp. Parseurs nm-style + DRI/GST (exécutable TOS).
#include "core/Symbols.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {
std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}
uint16_t be16(const uint8_t* p) { return (uint16_t(p[0]) << 8) | p[1]; }
uint32_t be32(const uint8_t* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}
} // namespace

void SymbolTable::clear() { syms_.clear(); byName_.clear(); }

void SymbolTable::finalize() {
    std::sort(syms_.begin(), syms_.end(),
              [](const Sym& a, const Sym& b) { return a.addr < b.addr; });
    byName_.clear();
    for (const auto& s : syms_) byName_.emplace(lower(s.name), s.addr);   // 1er gagne
}

bool SymbolTable::load(const std::string& path, uint32_t baseOffset) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    // ⚠ On ne vide PAS ici : un parsing qui échoue détruirait une table valide sans que
    // l'appelant (la GUI ignore le retour) puisse le dire. On charge dans une table
    // TEMPORAIRE et on n'échange qu'en cas de succès — cf. plus bas.

    uint8_t magic[2] = {0, 0};
    f.read(reinterpret_cast<char*>(magic), 2);
    f.close();
    // $601A = branche courte de tête d'un exécutable TOS → symboles DRI/GST embarqués.
    SymbolTable tmp;
    const bool ok = (magic[0] == 0x60 && magic[1] == 0x1A)
                  ? tmp.loadTosProgram(path, baseOffset)
                  : tmp.loadSymText(path);
    if (!ok) return false;              // table courante intacte
    *this = std::move(tmp);             // remplace (ne CUMULE pas : recharger doublait le compte)
    return true;
}

// nm-style : « ADDR [TYPE] NAME ». On lit le 1er token comme adresse hex (accepte le
// préfixe 0x) et le DERNIER token comme nom (le type au milieu, s'il existe, est ignoré).
// Lignes vides et commençant par '#'/';' ignorées.
bool SymbolTable::loadSymText(const std::string& path) {
    std::ifstream f(path);
    if (!f) return false;
    std::string line;
    size_t added = 0;
    while (std::getline(f, line)) {
        // Retire un éventuel '\r' final (fichiers CRLF).
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream ss(line);
        std::vector<std::string> tok;
        std::string t;
        while (ss >> t) tok.push_back(t);
        if (tok.empty()) continue;
        if (tok[0][0] == '#' || tok[0][0] == ';') continue;
        if (tok.size() < 2) continue;                 // besoin adresse + nom
        const char* as = tok[0].c_str();
        if (as[0] == '0' && (as[1] == 'x' || as[1] == 'X')) as += 2;
        char* end = nullptr;
        const unsigned long v = std::strtoul(as, &end, 16);
        if (end == as) continue;                      // 1er token pas hexadécimal → saute
        syms_.push_back({ tok.back(), uint32_t(v) & 0xFFFFFFu });
        ++added;
    }
    finalize();
    return added > 0;
}

// Exécutable TOS (GEMDOS) : en-tête 28 o (magic, tailles text/data/bss, taille symtab…),
// puis TEXT, DATA, table des SYMBOLES, table de relocation. Symbole DRI = 14 o :
// 8 o nom (complété d'espaces), 2 o type, 4 o valeur. Extension GST : quand
// (type & 0x0048) == 0x0048, l'entrée SUIVANTE (14 o) prolonge le nom.
bool SymbolTable::loadTosProgram(const std::string& path, uint32_t base) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const std::streamoff sz = f.tellg();
    if (sz < 28) return false;

    // On lit d'abord le SEUL en-tête, puis la seule table des symboles — jamais le fichier
    // entier. Auparavant un `vector<uint8_t> buf(sz)` avalait tout avant même de vérifier
    // le magic : un exécutable TOS de 3 Go (ou un fichier creux commençant par $601A)
    // faisait 3,1 Go de RSS et 9,5 s de gel du thread GUI, et sous quota mémoire le
    // std::bad_alloc n'était rattrapé par personne (SIGABRT).
    uint8_t hdr[28];
    f.seekg(0);
    f.read(reinterpret_cast<char*>(hdr), 28);
    if (f.gcount() != 28) return false;

    if (be16(&hdr[0]) != 0x601A) return false;
    const uint32_t textSize = be32(&hdr[2]);
    const uint32_t dataSize = be32(&hdr[6]);
    const uint32_t symSize  = be32(&hdr[14]);
    if (symSize == 0) return true;                    // pas de symboles : succès « à vide »

    const uint64_t symStart = uint64_t(28) + textSize + dataSize;
    if (symStart + symSize > uint64_t(sz)) return false;   // en-tête incohérent
    // Plafond de bon sens : 14 octets par symbole, donc 64 Mo ≈ 4,8 millions de symboles.
    // Au-delà, l'en-tête est fantaisiste — on refuse plutôt que d'allouer sur sa parole.
    static constexpr uint32_t kMaxSymTable = 64u * 1024u * 1024u;
    if (symSize > kMaxSymTable) return false;

    std::vector<uint8_t> sym_(static_cast<size_t>(symSize));
    f.seekg(static_cast<std::streamoff>(symStart));
    f.read(reinterpret_cast<char*>(sym_.data()), static_cast<std::streamsize>(symSize));
    if (static_cast<uint64_t>(f.gcount()) != symSize) return false;   // lecture courte
    const uint8_t* const symBase = sym_.data();

    size_t added = 0;
    for (uint32_t off = 0; off + 14 <= symSize; off += 14) {
        const uint8_t* e = symBase + off;
        const uint16_t type = be16(e + 8);
        const uint32_t val  = be32(e + 10);
        // Nom : 8 caractères, complétés d'espaces/zéros.
        char name[9] = {0};
        std::memcpy(name, e, 8);
        for (int i = 7; i >= 0 && (name[i] == ' ' || name[i] == '\0'); --i) name[i] = '\0';
        std::string sym(name);
        // Nom étendu GST : l'entrée suivante (14 o) donne 14 caractères de plus.
        if ((type & 0x0048) == 0x0048 && off + 28 <= symSize) {
            const uint8_t* n2 = symBase + off + 14;
            char ext[15] = {0};
            std::memcpy(ext, n2, 14);
            for (int i = 13; i >= 0 && (ext[i] == ' ' || ext[i] == '\0'); --i) ext[i] = '\0';
            sym += ext;
            off += 14;                                // consomme l'entrée de continuation
        }
        if (sym.empty()) continue;
        syms_.push_back({ sym, (val + base) & 0xFFFFFFu });
        ++added;
    }
    finalize();
    return added > 0;
}

bool SymbolTable::lookup(const std::string& name, uint32_t& outAddr) const {
    const auto it = byName_.find(lower(name));
    if (it == byName_.end()) return false;
    outAddr = it->second;
    return true;
}

std::string SymbolTable::nameFor(uint32_t addr, uint32_t* outOffset) const {
    if (syms_.empty()) return {};
    addr &= 0xFFFFFFu;
    // Dernier symbole d'adresse ≤ addr (syms_ trié par adresse).
    auto it = std::upper_bound(syms_.begin(), syms_.end(), addr,
                               [](uint32_t a, const Sym& s) { return a < s.addr; });
    if (it == syms_.begin()) return {};               // aucun symbole ≤ addr
    --it;
    if (outOffset) *outOffset = addr - it->addr;
    return it->name;
}
