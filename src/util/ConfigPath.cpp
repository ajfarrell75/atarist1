// =============================================================================
//  ConfigPath.cpp — cf. ConfigPath.hpp pour la règle et sa raison d'être.
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "util/ConfigPath.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace neost::cfgpath {

namespace {
// Nom du dossier applicatif dans la config utilisateur. Minuscule : c'est la
// convention XDG, et sous Windows %APPDATA% ne distingue pas la casse.
constexpr const char* kAppDir  = "neost";
constexpr const char* kCfgName = "neost.cfg";
}  // namespace

Probe systemProbe() {
    Probe p;
    p.exists = [](const std::string& f) {
        std::error_code ec;
        return fs::exists(f, ec) && !fs::is_directory(f, ec);
    };
    // Inscriptible = on arrive VRAIMENT à y créer un fichier. Tester les bits de
    // permission mentirait : montage en lecture seule, ACL, conteneur, sandbox
    // macOS… Le seul test qui ne ment pas est l'essai, et il est bon marché
    // (une fois au démarrage).
    p.dirWritable = [](const std::string& d) {
        std::error_code ec;
        if (!fs::is_directory(d, ec)) return false;
        const std::string probe = d + "/.neost-write-test";
        { std::ofstream o(probe); if (!o) return false; }
        fs::remove(probe, ec);
        return true;
    };
    p.env = [](const char* name) {
        const char* v = std::getenv(name);
        return std::string(v ? v : "");
    };
    return p;
}

std::string userConfigDir(const Probe& p, hostpath::Style style) {
    if (style == hostpath::Style::Windows) {
        const std::string appdata = p.env("APPDATA");
        if (!appdata.empty()) return hostpath::join(appdata, kAppDir, style);
        return {};
    }
    // POSIX (Linux, macOS) : XDG_CONFIG_HOME s'il est posé ET absolu — la
    // spécification impose de l'ignorer s'il est relatif, et un chemin relatif
    // ferait écrire la config dans le répertoire courant du lancement.
    const std::string xdg = p.env("XDG_CONFIG_HOME");
    if (!xdg.empty() && hostpath::isAbsolute(xdg, style))
        return hostpath::join(xdg, kAppDir, style);
    const std::string home = p.env("HOME");
    if (!home.empty())
        return hostpath::join(hostpath::join(home, ".config", style), kAppDir, style);
    return {};
}

std::string resolve(const std::string& exeDir, const Probe& p, hostpath::Style style) {
    // 1. La config PORTABLE existante gagne toujours. C'est ce qui garantit qu'une
    //    mise à jour ne fait jamais « disparaître » les réglages d'un utilisateur
    //    d'AppImage, de zip Windows ou de borne.
    const std::string portableDir = hostpath::join(exeDir, "..", style);
    const std::string portable    = hostpath::join(portableDir, kCfgName, style);
    if (p.exists(portable)) return portable;

    // 2. Sinon la config utilisateur, si elle existe déjà.
    const std::string userDir = userConfigDir(p, style);
    const std::string user    = userDir.empty() ? std::string()
                                                : hostpath::join(userDir, kCfgName, style);
    if (!user.empty() && p.exists(user)) return user;

    // 3. Aucune des deux : on choisit où ÉCRIRE. À côté du binaire si c'est
    //    possible (installation portable neuve — le comportement historique),
    //    sinon dans la config utilisateur (installation système : /usr/bin).
    if (p.dirWritable(portableDir)) return portable;
    if (!user.empty()) return user;
    return portable;   // ni l'un ni l'autre : on rend le chemin historique, et
                       // l'écriture échouera EN LE DISANT plutôt qu'en silence
}

std::string profilesDirFor(const std::string& cfgFile, hostpath::Style style) {
    const std::string sep(1, style == hostpath::Style::Windows ? '\\' : hostpath::SEP);
    std::string dir = cfgFile;
    // Retire le nom de fichier (les deux séparateurs sont acceptés sous Windows).
    const std::size_t slash = dir.find_last_of("/\\");
    dir = (slash == std::string::npos) ? std::string(".") : dir.substr(0, slash);
    return hostpath::join(dir, "profiles", style);
}

}  // namespace neost::cfgpath
