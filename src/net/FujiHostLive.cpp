// =============================================================================
//  FujiHostLive.cpp — Backend FujiNet réel (cf. FujiHostLive.hpp).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "net/FujiHostLive.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>

#include "net/FujiHostReplay.hpp"   // pour sanitize() (noms de cache)
#include "net/HttpClient.hpp"
#include "net/MiniJson.hpp"

namespace fs = std::filesystem;

namespace {
void ltrace(const char* what, const std::string& detail) {
    static const bool on = std::getenv("NEOST_FUJI_TRACE") != nullptr;
    if (on) std::fprintf(stderr, "[fuji-live] %s %s\n", what, detail.c_str());
}
} // namespace

FujiHostLive::FujiHostLive() {
    neonet::netInitOnce();
    std::error_code ec;
    fs::path dir = fs::temp_directory_path(ec) / "neost-fujinet";
    fs::create_directories(dir, ec);
    cacheDir_ = dir.string();
}

FujiHostLive::~FujiHostLive() { reset(); }

void FujiHostLive::closeLocked(Chan& c) {
    if (c.fd >= 0) { neonet::sockClose(c.fd); c.fd = -1; }
    c.open = false;
    c.kind = Kind::None;
    c.connected = false;
    c.buf.clear();
    c.jsonSrc.clear();
}

uint8_t FujiHostLive::open(int chanIdx, const std::string& spec, uint8_t mode, uint8_t trans) {
    (void)mode; (void)trans;
    Chan* c = at(chanIdx);
    if (!c) return fn_err::BAD_CMD;

    c->joinWorker();
    std::lock_guard<std::mutex> lk(c->mtx);
    closeLocked(*c);

    neonet::Url u;
    if (!neonet::parseUrl(spec, u)) { c->lastError = fn_err::BAD_CMD; return fn_err::BAD_CMD; }

    if (u.scheme == "http" || u.scheme == "https") {
        // Le corps arrive en arrière-plan ; le ST verra Status.avail grossir.
        c->kind = Kind::Http;
        c->open = true;
        c->connected = true;
        c->lastError = fn_err::OK;
        c->workerDone = false;
        c->worker = std::thread([c, spec]() {
            // &c->stop : close()/reset() (via joinWorker) interrompent le
            // téléchargement en ~1 s au lieu d'attendre sa fin complète.
            neonet::HttpResult r = neonet::httpFetch(spec, nullptr, nullptr, &c->stop);
            std::lock_guard<std::mutex> wl(c->mtx);
            if (r.status >= 200 && r.status < 300) {
                c->buf.insert(c->buf.end(), r.body.begin(), r.body.end());
                c->lastError = fn_err::OK;
            } else {
                ltrace("http error", spec + " -> " + (r.error.empty()
                        ? "HTTP " + std::to_string(r.status) : r.error));
                c->lastError = r.status ? fn_err::IO_ERROR : fn_err::OFFLINE;
            }
            c->connected = false;                 // corps complet (Connection: close)
            c->workerDone = true;
        });
        ltrace("open http", spec);
        return fn_err::OK;
    }

    if (u.scheme == "tcp" || u.scheme == "telnet") {
        std::string err;
        const int fd = neonet::tcpConnect(u.host, u.port ? u.port : 23, 5000, err);
        if (fd < 0) { ltrace("tcp connect failed", err); c->lastError = fn_err::OFFLINE; return fn_err::OFFLINE; }
        c->kind = Kind::Tcp;
        c->fd = fd;
        c->open = true;
        c->connected = true;
        c->lastError = fn_err::OK;
        c->workerDone = false;
        c->worker = std::thread([c]() {
            uint8_t tmp[4096];
            // Contre-pression : au-delà de ce seuil on cesse de lire (le TCP
            // freine l'émetteur) — sinon un pair rapide jamais lu par le ST
            // ferait grossir buf sans borne.
            constexpr std::size_t kBufCap = 1u << 20;
            while (!c->stop) {
                int fd;
                bool full;
                {
                    std::lock_guard<std::mutex> wl(c->mtx);
                    fd = c->fd;
                    full = c->buf.size() >= kBufCap;
                }
                if (fd < 0) break;
                if (full) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    continue;
                }
                const int rc = neonet::sockRecv(fd, tmp, int(sizeof tmp), 200);
                if (rc == -2) continue;                       // timeout de poll → re-teste stop
                std::lock_guard<std::mutex> wl(c->mtx);
                if (rc <= 0) { c->connected = false; break; } // fermé/erreur
                c->buf.insert(c->buf.end(), tmp, tmp + rc);
            }
            c->workerDone = true;
        });
        ltrace("open tcp", spec);
        return fn_err::OK;
    }

    ltrace("unsupported scheme", u.scheme);
    c->lastError = fn_err::BAD_CMD;
    return fn_err::BAD_CMD;
}

uint8_t FujiHostLive::close(int chanIdx) {
    Chan* c = at(chanIdx);
    if (!c) return fn_err::BAD_CMD;
    {
        // shutdown() AVANT le join débloque le recv du thread lecteur TCP sans
        // LIBÉRER le numéro de fd : close() ici ouvrait une fenêtre où l'OS
        // réattribuait le même numéro à une autre connexion (worker HTTP d'un
        // autre canal) et le lecteur périmé consommait SES octets. La fermeture
        // réelle attend le join (closeLocked ci-dessous).
        std::lock_guard<std::mutex> lk(c->mtx);
        if (c->fd >= 0) neonet::sockShutdown(c->fd);
    }
    c->joinWorker();
    std::lock_guard<std::mutex> lk(c->mtx);
    closeLocked(*c);
    return fn_err::OK;
}

int FujiHostLive::read(int chanIdx, uint8_t* dst, int len) {
    Chan* c = at(chanIdx);
    if (!c) return -1;
    std::lock_guard<std::mutex> lk(c->mtx);
    if (!c->open) return -1;
    const int n = std::min<int>(len, int(c->buf.size()));
    std::memcpy(dst, c->buf.data(), std::size_t(n));
    c->buf.erase(c->buf.begin(), c->buf.begin() + n);
    return n;
}

uint8_t FujiHostLive::write(int chanIdx, const uint8_t* src, int len) {
    Chan* c = at(chanIdx);
    if (!c) return fn_err::BAD_CMD;
    std::lock_guard<std::mutex> lk(c->mtx);
    if (!c->open) return fn_err::IO_ERROR;
    if (c->kind == Kind::Tcp && c->fd >= 0)
        return neonet::sockSend(c->fd, src, len) < 0 ? fn_err::IO_ERROR : fn_err::OK;
    // HTTP v1 : l'écriture sur un canal GET n'a pas de destinataire.
    return fn_err::BAD_CMD;
}

FujiChanStatus FujiHostLive::status(int chanIdx) {
    Chan* c = at(chanIdx);
    FujiChanStatus st;
    if (!c) return st;
    std::lock_guard<std::mutex> lk(c->mtx);
    if (!c->open) return st;
    st.avail = uint16_t(std::min<std::size_t>(c->buf.size(), 65535));
    st.connected = (c->connected || !c->buf.empty()) ? 1 : 0;
    st.error = (!c->connected && c->buf.empty() && c->lastError == fn_err::OK)
                   ? 136 /* EOF FujiNet */ : c->lastError;
    return st;
}

uint8_t FujiHostLive::jsonParse(int chanIdx) {
    Chan* c = at(chanIdx);
    if (!c) return fn_err::BAD_CMD;
    // Attend la fin du téléchargement HTTP (borné) : parser un corps partiel
    // n'aurait aucun sens et le vrai FujiNet fait pareil (P bloque brièvement).
    // Canaux TCP exclus : leur lecteur tourne tant que la connexion vit,
    // attendre workerDone y gèlerait l'émulateur 30 s à chaque commande P.
    bool waitHttp;
    {
        std::lock_guard<std::mutex> lk(c->mtx);
        waitHttp = (c->kind == Kind::Http);
    }
    if (waitHttp)
        for (int i = 0; i < 300 && !c->workerDone; ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::lock_guard<std::mutex> lk(c->mtx);
    if (!c->open) return fn_err::IO_ERROR;
    c->jsonSrc.assign(c->buf.begin(), c->buf.end());
    return minijson::looksLikeJson(c->jsonSrc) ? fn_err::OK : fn_err::BAD_CMD;
}

uint8_t FujiHostLive::jsonQuery(int chanIdx, const std::string& query) {
    Chan* c = at(chanIdx);
    if (!c) return fn_err::BAD_CMD;
    std::lock_guard<std::mutex> lk(c->mtx);
    if (!c->open || c->jsonSrc.empty()) return fn_err::IO_ERROR;
    std::string val;
    if (!minijson::query(c->jsonSrc, query, val)) return fn_err::BAD_CMD;
    c->buf.assign(val.begin(), val.end());
    return fn_err::OK;
}

void FujiHostLive::getTime(uint8_t out[7]) {
    const std::time_t now = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &now);
#else
    localtime_r(&now, &tmv);
#endif
    const int year = tmv.tm_year + 1900;
    out[0] = uint8_t(year >> 8);
    out[1] = uint8_t(year & 0xFF);
    out[2] = uint8_t(tmv.tm_mon + 1);
    out[3] = uint8_t(tmv.tm_mday);
    out[4] = uint8_t(tmv.tm_hour);
    out[5] = uint8_t(tmv.tm_min);
    out[6] = uint8_t(tmv.tm_sec);
}

std::string FujiHostLive::fetchToFile(const std::string& url) {
    const std::string local = cacheDir_ + "/" + FujiHostReplay::sanitize(url);
    // Cache : une image déjà téléchargée dans la session est réutilisée.
    std::error_code ec;
    if (fs::exists(local, ec) && fs::file_size(local, ec) > 0) {
        ltrace("fetch (cached)", url);
        return local;
    }
    neonet::HttpResult r = neonet::httpFetch(url);
    if (r.status < 200 || r.status >= 300 || r.body.empty()) {
        ltrace("fetch failed", url + " -> " + (r.error.empty()
                ? "HTTP " + std::to_string(r.status) : r.error));
        return {};
    }
    std::ofstream f(local, std::ios::binary | std::ios::trunc);
    if (!f) return {};
    f.write(reinterpret_cast<const char*>(r.body.data()), std::streamsize(r.body.size()));
    f.close();
    ltrace("fetch", url + " -> " + local + " (" + std::to_string(r.body.size()) + " bytes)");
    return local;
}

void FujiHostLive::reset() {
    for (int i = 0; i < MAX_CHANNELS; ++i) close(i);
}
