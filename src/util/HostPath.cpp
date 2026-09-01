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
