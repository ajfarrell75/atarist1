// =============================================================================
//  MiniJson.cpp — Extracteur JSON minimal (cf. MiniJson.hpp).
//
//  Parseur récursif descendant sans DOM : on SAUTE les valeurs qu'on ne cherche
//  pas et on ne matérialise que la valeur demandée. Suffisant pour les réponses
//  d'API que consomme un client FujiNet (météo, heure, flux JSON simples).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "net/MiniJson.hpp"

#include <cctype>
#include <cstdint>
#include <cstdlib>

namespace minijson {
namespace {

// Profondeur d'imbrication maximale : borne la récursion value→object/array
// sur un document hostile (des mégaoctets de « [ » feraient déborder la pile).
constexpr int kMaxDepth = 128;

struct P {
    const char* s;
    const char* end;
    bool ok = true;
    int depth = 0;

    bool eof() const { return s >= end; }
    char peek() const { return eof() ? '\0' : *s; }
    void ws() { while (!eof() && std::isspace(uint8_t(*s))) ++s; }
    bool lit(const char* t) {
        const char* p = s;
        while (*t) { if (p >= end || *p != *t) return false; ++p; ++t; }
        s = p;
        return true;
    }

    // Parse une chaîne JSON ; si `out` non nul, y dépose la valeur DÉCODÉE.
    bool str(std::string* out) {
        if (peek() != '"') return false;
        ++s;
        while (!eof() && *s != '"') {
            if (*s == '\\') {
                ++s;
                if (eof()) return false;
                if (out) {
                    switch (*s) {
                    case 'n': *out += '\n'; break;
                    case 't': *out += '\t'; break;
                    case 'r': *out += '\r'; break;
                    case 'b': *out += '\b'; break;
                    case 'f': *out += '\f'; break;
                    case 'u': {                       // \uXXXX → octet latin-1 approx.
                        if (end - s < 5) return false;
                        char h[5] = {s[1], s[2], s[3], s[4], 0};
                        *out += char(std::strtoul(h, nullptr, 16) & 0xFF);
                        s += 4;
                        break;
                    }
                    default: *out += *s; break;
                    }
                } else if (*s == 'u') {
                    if (end - s < 5) return false;
                    s += 4;
                }
                ++s;
            } else {
                if (out) *out += *s;
                ++s;
            }
        }
        if (eof()) return false;
        ++s;                                          // '"' fermant
        return true;
    }

    // Saute une valeur complète ; si `raw` non nul, y recopie le texte source.
    bool value(std::string* raw) {
        ws();
        const char* start = s;
        bool r = false;
        switch (peek()) {
        case '{': r = object(nullptr, nullptr); break;
        case '[': r = array(-1, nullptr); break;
        case '"': r = str(nullptr); break;
        default:
            if (lit("true") || lit("false") || lit("null")) { r = true; break; }
            {   // nombre
                const char* p = s;
                if (peek() == '-') ++s;
                bool digits = false;
                while (!eof() && (std::isdigit(uint8_t(*s)) || *s == '.' || *s == 'e' ||
                                  *s == 'E' || *s == '+' || *s == '-')) { ++s; digits = true; }
                r = digits && s > p;
            }
            break;
        }
        if (r && raw) raw->assign(start, std::size_t(s - start));
        return r;
    }

    // Objet : si `key` non nul, s'arrête sur cette clé et remplit `found` avec
    // la position de sa valeur (s pointe alors sur la valeur). Sinon, saute tout.
    bool object(const char* key, bool* found) {
        if (++depth > kMaxDepth) { --depth; return false; }
        const bool r = objectBody(key, found);
        --depth;
        return r;
    }

    bool objectBody(const char* key, bool* found) {
        if (peek() != '{') return false;
        ++s;
        ws();
        if (peek() == '}') { ++s; return true; }
        while (true) {
            ws();
            std::string k;
            if (!str(&k)) return false;
            ws();
            if (peek() != ':') return false;
            ++s;
            ws();
            if (key && k == key) { if (found) *found = true; return true; }
            if (!value(nullptr)) return false;
            ws();
            if (peek() == ',') { ++s; continue; }
            if (peek() == '}') { ++s; return true; }
            return false;
        }
    }

    // Tableau : si `idx` >= 0, s'arrête sur l'élément idx (s pointe dessus, via
    // le retour true + *foundAt true). Sinon, saute tout.
    bool array(int idx, bool* found) {
        if (++depth > kMaxDepth) { --depth; return false; }
        const bool r = arrayBody(idx, found);
        --depth;
        return r;
    }

    bool arrayBody(int idx, bool* found) {
        if (peek() != '[') return false;
        ++s;
        ws();
        if (peek() == ']') { ++s; return true; }
        int i = 0;
        while (true) {
            ws();
            if (idx >= 0 && i == idx) { if (found) *found = true; return true; }
            if (!value(nullptr)) return false;
            ws();
            if (peek() == ',') { ++s; ++i; continue; }
            if (peek() == ']') { ++s; return true; }
            return false;
        }
    }
};

} // namespace

bool query(const std::string& doc, const std::string& path, std::string& out) {
    P p{doc.c_str(), doc.c_str() + doc.size()};
    p.ws();

    // Découpe du chemin « /a/b/0 » (un préfixe « N: » éventuel est toléré).
    std::size_t pos = 0;
    if (path.size() >= 2 && (path[0] == 'N' || path[0] == 'n') && path[1] == ':') pos = 2;
    while (pos < path.size()) {
        while (pos < path.size() && path[pos] == '/') ++pos;
        if (pos >= path.size()) break;
        std::size_t e = path.find('/', pos);
        if (e == std::string::npos) e = path.size();
        const std::string comp = path.substr(pos, e - pos);
        pos = e;

        p.ws();
        if (p.peek() == '{') {
            bool found = false;
            if (!p.object(comp.c_str(), &found) || !found) return false;
        } else if (p.peek() == '[') {
            char* endp = nullptr;
            const long idx = std::strtol(comp.c_str(), &endp, 10);
            if (!endp || *endp != '\0') return false;
            bool found = false;
            if (!p.array(int(idx), &found) || !found) return false;
        } else {
            return false;                      // chemin trop profond
        }
    }

    // La valeur courante est la réponse : chaîne décodée, sinon texte source.
    p.ws();
    if (p.peek() == '"') { out.clear(); return p.str(&out); }
    return p.value(&out);
}

bool looksLikeJson(const std::string& doc) {
    P p{doc.c_str(), doc.c_str() + doc.size()};
    p.ws();
    return !p.eof() && p.value(nullptr);
}

} // namespace minijson
