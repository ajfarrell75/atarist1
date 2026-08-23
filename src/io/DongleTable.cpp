// DongleTable — table motif → clé. Cf. en-tête.
#include "io/DongleTable.hpp"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>

namespace neost {

namespace {
std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower((unsigned char)c)); return s; }
std::string trim(const std::string& s) {
    const auto a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
}
}

std::vector<DongleRule> parseDongleTable(const std::string& text, int* bad) {
    std::vector<DongleRule> out;
    if (bad) *bad = 0;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        line = trim(line);
        if (line.empty()) continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) { if (bad) ++*bad; continue; }
        DongleRule r;
        r.pattern = lower(trim(line.substr(0, eq)));
        const std::string target = lower(trim(line.substr(eq + 1)));
        const auto colon = target.find(':');
        if (r.pattern.empty() || colon == std::string::npos) { if (bad) ++*bad; continue; }
        const std::string where = target.substr(0, colon), what = target.substr(colon + 1);
        if (where == "cart") {
            r.cart = true;
            r.key = what == "cubase2" ? CartridgeKey::Model::Cubase2 : what == "cubase3" ? CartridgeKey::Model::Cubase3
                  : what == "auto"    ? CartridgeKey::Model::Auto    : what == "notator" ? CartridgeKey::Model::Notator
                  : CartridgeKey::Model::None;
            if (r.key == CartridgeKey::Model::None) { if (bad) ++*bad; continue; }
        } else {
            bool ok = true;
            r.port = PortDevices::portFromId(where.c_str(), &ok);
            r.dev  = PortDevices::fromId(what.c_str());
            if (!ok || r.dev == PortDevices::Device::None || !PortDevices::fits(r.port, r.dev)) { if (bad) ++*bad; continue; }
        }
        out.push_back(std::move(r));
    }
    return out;
}

std::vector<DongleRule> matchDongleRules(const std::vector<DongleRule>& rules, const std::string& imagePath) {
    std::vector<DongleRule> hits;
    const std::string name = lower(std::filesystem::path(imagePath).filename().string());
    for (const auto& r : rules)
        if (name.find(r.pattern) != std::string::npos) hits.push_back(r);
    return hits;
}

const char* defaultDongleTable() {
    return
        "# NeoST - which key to plug when a floppy image is mounted (disks/dongles.txt).\n"
        "# pattern (substring of the image name, case-insensitive) = port:device\n"
        "#   ports   : joy0 joy1 rs232 printer cartbutton   |   cart:model (cubase2 cubase3 auto notator)\n"
        "#   devices : leaderboard 10thframe cricket rugby soccer bat2 musicmaster jeannedarc prosound multiface urc\n"
        "# Only EMPTY slots are filled - an explicit setting on the Dongles page always wins.\n"
        "leader board    = joy1:leaderboard\n"
        "leaderboard     = joy1:leaderboard\n"
        "10th frame      = joy1:10thframe\n"
        "cricket captain = joy0:cricket\n"
        "rugby coach     = joy1:rugby\n"
        "soccer manager  = joy0:soccer\n"
        "b.a.t. ii       = rs232:bat2\n"
        "b.a.t. 2        = rs232:bat2\n"
        "bat ii          = rs232:bat2\n"
        "music master    = rs232:musicmaster\n"
        "jeanne d'arc    = rs232:jeannedarc\n"
        "notator         = cart:notator\n"
        "creator         = cart:notator\n"
        "cubase 2        = cart:cubase2\n"
        "cubase 3        = cart:cubase3\n"
        "cubase score    = cart:cubase3\n"
        "cubase audio    = cart:cubase3\n";
}

}  // namespace neost
