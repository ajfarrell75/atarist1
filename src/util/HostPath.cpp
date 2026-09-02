// =============================================================================
//  HostPath.cpp — implémentation. Voir HostPath.hpp pour la raison d'être.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "util/HostPath.hpp"

#include <cctype>

#if defined(_WIN32)
#include <direct.h>              // _getcwd
#define neost_getcwd _getcwd
#else
#include <unistd.h>              // getcwd
#define neost_getcwd getcwd
#endif

namespace neost::hostpath {

bool hasDriveLetter(const std::string& p) {
    return p.size() >= 2 && std::isalpha((unsigned char)p[0]) && p[1] == ':';
}

bool isAbsolute(const std::string& p, Style style) {
    if (p.empty()) return false;
    if (style == Style::Windows) {
        if (hasDriveLetter(p)) return true;
        // Racine UNC : « \\serveur\partage » — testée sur les DEUX séparateurs,
        // la normalisation pouvant avoir déjà eu lieu.
        if (p.size() >= 2 && (p[0] == '\\' || p[0] == SEP)
                          && (p[1] == '\\' || p[1] == SEP)) return true;
    }
    return p[0] == SEP || (style == Style::Windows && p[0] == '\\');
}

std::string normalizeSeparators(std::string p, Style style) {
    if (style == Style::Windows)
        for (char& c : p) if (c == '\\') c = SEP;
    return p;
}

std::string stripTrailingSep(std::string p, Style style) {
    if (style == Style::Windows && p.size() == 3 && hasDriveLetter(p) && p[2] == SEP)
        return p;                                   // « C:/ » = racine du lecteur
    while (p.size() > 2 && p.back() == SEP) p.pop_back();
    return p;
}

std::string join(const std::string& base, const std::string& tail, Style style) {
    if (base.empty() || isAbsolute(tail, style)) return normalizeSeparators(tail, style);
    if (tail.empty()) return normalizeSeparators(base, style);
    std::string out = normalizeSeparators(base, style);
    if (out.back() != SEP) out.push_back(SEP);
    const std::string t = normalizeSeparators(tail, style);
    out += (t.front() == SEP) ? t.substr(1) : t;
    return out;
}

// Longueur de la RACINE d'un chemin DÉJÀ normalisé (séparateur « / »). C'est le
// plancher au-dessous duquel un « .. » ne doit jamais remonter.
//   Posix   : « / »                        → 1
//   Windows : « C:/ » → 3, « C: » → 2, et la racine UNC « //serveur/partage/ »
//             jusqu'au séparateur qui suit le partage.
// Hatari n'en a pas besoin (son analyseur est purement Unix, où la racine est
// toujours « / »), et c'est justement ce que le port avait gardé de trop : sous
// Windows « / » désigne la racine du lecteur COURANT du processus — un AUTRE
// lecteur en général —, donc y retomber ne clampe pas, ça déplace.
static std::size_t rootPrefixLen(const std::string& p, Style style) {
    if (style == Style::Windows) {
        if (p.size() >= 2 && p[0] == SEP && p[1] == SEP) {       // UNC //serveur/partage
            const std::size_t srv = p.find(SEP, 2);
            if (srv == std::string::npos) return p.size();
            const std::size_t shr = p.find(SEP, srv + 1);
            return (shr == std::string::npos) ? p.size() : shr + 1;
        }
        if (hasDriveLetter(p)) return (p.size() >= 3 && p[2] == SEP) ? 3 : 2;
    }
    return (!p.empty() && p[0] == SEP) ? 1 : 0;
}

// Port de File_MakeAbsoluteName (Hatari, src/file.c), avec deux corrections :
// la détection d'absolu passe par isAbsolute (Windows compris) et l'entrée est
// normalisée. Le corps de l'analyse — copie composant par composant, « ./ » sauté,
// « ../ » qui remonte d'un cran — reste celui d'Hatari.
std::string lexicalAbsolute(const std::string& p, const std::string& cwd, Style style) {
    const std::string src = normalizeSeparators(p, style);
    std::string out;
    if (!isAbsolute(src, style) && !cwd.empty()) {
        out = normalizeSeparators(cwd, style);
        if (out.back() != SEP) out.push_back(SEP);
    }
    const char* in = src.c_str();
    int i = 0;
    while (in[i]) {
        if (in[i] == '.' && in[i + 1] == SEP) {
            i += 2;
        } else if (in[i] == '.' && in[i + 1] == 0) {
            i += 1;
            // Le plancher est la racine, pas « 1 » : sur « C:/. » un pop_back nu
            // donnerait « C: », qui sous Windows désigne le répertoire COURANT du
            // lecteur C et non sa racine.
            { const std::size_t root = rootPrefixLen(out, style);
              if (out.size() > (root ? root : 1)) out.pop_back(); }   // retire le '/' final
        } else if (in[i] == '.' && in[i + 1] == '.'
                   && (in[i + 2] == SEP || in[i + 2] == 0)) {
            i += 2;
            // Remonter d'un cran, SANS jamais franchir la racine. Le corps d'Hatari
            // retombait sur « / » dès que rfind ne trouvait plus de séparateur : sur
            // « C:/../X » cela jetait la lettre de lecteur et rendait « /X », un chemin
            // relatif au lecteur courant. Même perte sur « //srv/share/../../X ».
            const std::size_t root = rootPrefixLen(out, style);
            if (out.size() > root) out.pop_back();       // retire le '/' final
            const std::size_t s = out.rfind(SEP);
            if (s != std::string::npos && s + 1 >= root) out.resize(s + 1);
            else if (root)                               out.resize(root);
            else                                         out = std::string(1, SEP);
            if (in[i] == SEP) i += 1;
            else if (out.size() > (root ? root : 1)) out.pop_back();
        } else {
            while (in[i]) { out.push_back(in[i++]); if (in[i - 1] == SEP) break; }
        }
    }
    return out;
}

std::string currentDir() {
    char buf[4096];
    if (!neost_getcwd(buf, sizeof(buf))) return {};
    // Sous Windows getcwd() rend des '\' : sans cette normalisation, tout chemin
    // relatif préfixé du cwd ressortait en séparateurs mélangés.
    return normalizeSeparators(std::string(buf), kNative);
}

std::string makeAbsolute(const std::string& p) {
    return lexicalAbsolute(p, currentDir(), kNative);
}

}  // namespace neost::hostpath

// --- Recomposition Unicode NFD → NFC (cf. HostPath.hpp) -----------------------
// Table PORTÉE TELLE QUELLE de `mapDecomposedPrecomposed` (hatari/src/str.c:658-712) :
// 53 couples (lettre ASCII, marque combinante U+03xx) → point de code précomposé
// Latin-1. C'est volontairement le SEUL sous-ensemble couvert : ce sont les accents
// qui existent dans le jeu de caractères Atari, et recomposer au-delà produirait des
// noms que le TOS ne saurait de toute façon pas représenter.
namespace {
struct Precomp { char base; unsigned short comb; unsigned short pre; };
const Precomp kPrecomp[] = {
    { 'A', 0x0300, 0xC0 }, { 'A', 0x0301, 0xC1 }, { 'A', 0x0302, 0xC2 }, { 'A', 0x0303, 0xC3 }, { 'A', 0x0308, 0xC4 }, { 'A', 0x030A, 0xC5 },
    { 'C', 0x0327, 0xC7 }, { 'E', 0x0300, 0xC8 }, { 'E', 0x0301, 0xC9 }, { 'E', 0x0302, 0xCA }, { 'E', 0x0308, 0xCB }, { 'I', 0x0300, 0xCC },
    { 'I', 0x0301, 0xCD }, { 'I', 0x0302, 0xCE }, { 'I', 0x0308, 0xCF }, { 'N', 0x0303, 0xD1 }, { 'O', 0x0300, 0xD2 }, { 'O', 0x0301, 0xD3 },
    { 'O', 0x0302, 0xD4 }, { 'O', 0x0303, 0xD5 }, { 'O', 0x0308, 0xD6 }, { 'U', 0x0300, 0xD9 }, { 'U', 0x0301, 0xDA }, { 'U', 0x0302, 0xDB },
    { 'U', 0x0308, 0xDC }, { 'Y', 0x0301, 0xDD }, { 'a', 0x0300, 0xE0 }, { 'a', 0x0301, 0xE1 }, { 'a', 0x0302, 0xE2 }, { 'a', 0x0303, 0xE3 },
    { 'a', 0x0308, 0xE4 }, { 'a', 0x030A, 0xE5 }, { 'c', 0x0327, 0xE7 }, { 'e', 0x0300, 0xE8 }, { 'e', 0x0301, 0xE9 }, { 'e', 0x0302, 0xEA },
    { 'e', 0x0308, 0xEB }, { 'i', 0x0300, 0xEC }, { 'i', 0x0301, 0xED }, { 'i', 0x0302, 0xEE }, { 'i', 0x0308, 0xEF }, { 'n', 0x0303, 0xF1 },
    { 'o', 0x0300, 0xF2 }, { 'o', 0x0301, 0xF3 }, { 'o', 0x0302, 0xF4 }, { 'o', 0x0303, 0xF5 }, { 'o', 0x0308, 0xF6 }, { 'u', 0x0300, 0xF9 },
    { 'u', 0x0301, 0xFA }, { 'u', 0x0302, 0xFB }, { 'u', 0x0308, 0xFC }, { 'y', 0x0301, 0xFD }, { 'y', 0x0308, 0xFF },
};
} // namespace

std::string neost::hostpath::precomposeUtf8(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        // Une marque combinante U+0300-U+03FF s'encode « 110011xx 10xxxxxx » en UTF-8,
        // d'où le masque 0xFC == 0xCC sur le premier octet (≙ str.c:733).
        if (i + 2 < s.size() && (static_cast<unsigned char>(s[i + 1]) & 0xFC) == 0xCC) {
            const unsigned cp = ((static_cast<unsigned char>(s[i + 1]) & 31u) << 6)
                              |  (static_cast<unsigned char>(s[i + 2]) & 63u);
            bool done = false;
            for (const auto& e : kPrecomp) {
                if (static_cast<unsigned char>(e.base) == c && e.comb == cp) {
                    out.push_back(static_cast<char>(0xC0 | (e.pre >> 6)));   // UTF-8 1er octet
                    out.push_back(static_cast<char>(0x80 | (e.pre & 63)));   // UTF-8 2e octet
                    i += 3;                                                  // base + 2 octets
                    done = true;
                    break;
                }
            }
            if (done) continue;
        }
        out.push_back(s[i++]);
    }
    return out;
}
