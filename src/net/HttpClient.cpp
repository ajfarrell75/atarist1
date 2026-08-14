// =============================================================================
//  HttpClient.cpp — Client HTTP 1.1 minimal + aides sockets (cf. HttpClient.hpp).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "net/HttpClient.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace neonet {

void netInitOnce() {
#ifdef _WIN32
    static bool done = false;
    if (!done) { WSADATA wsa; WSAStartup(MAKEWORD(2, 2), &wsa); done = true; }
#endif
}

void sockClose(SocketHandle fd) {
    if (!socketValid(fd)) return;
#ifdef _WIN32
    closesocket(SOCKET(fd));
#else
    ::close(fd);
#endif
}

// Coupe les deux sens sans libérer le NUMÉRO de descripteur : le recv d'un
// thread lecteur retourne aussitôt, mais le fd ne peut pas être réattribué à
// une autre connexion tant que sockClose n'a pas été appelé — c'est la moitié
// « sûre » d'un arrêt de thread lecteur (shutdown → join → close).
void sockShutdown(SocketHandle fd) {
    if (!socketValid(fd)) return;
#ifdef _WIN32
    shutdown(SOCKET(fd), SD_BOTH);
#else
    ::shutdown(fd, SHUT_RDWR);
#endif
}

SocketHandle tcpConnect(const std::string& host, int port, int timeoutMs, std::string& err) {
    netInitOnce();
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    char portStr[16];
    std::snprintf(portStr, sizeof portStr, "%d", port);
    const int gai = getaddrinfo(host.c_str(), portStr, &hints, &res);
    if (gai != 0 || !res) { err = "cannot resolve " + host; return kInvalidSocket; }

    SocketHandle fd = kInvalidSocket;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = static_cast<SocketHandle>(socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (!socketValid(fd)) continue;
#ifndef _WIN32
        // Connexion avec timeout : non-bloquant + poll, puis retour en bloquant.
        const int fl = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, fl | O_NONBLOCK);
        int rc = ::connect(fd, ai->ai_addr, socklen_t(ai->ai_addrlen));
        if (rc != 0 && errno == EINPROGRESS) {
            pollfd pf{fd, POLLOUT, 0};
            rc = (poll(&pf, 1, timeoutMs) == 1 && (pf.revents & POLLOUT)) ? 0 : -1;
            if (rc == 0) {
                int soerr = 0;
                socklen_t sl = sizeof soerr;
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
                if (soerr != 0) rc = -1;
            }
        }
        fcntl(fd, F_SETFL, fl);
#else
        u_long nonBlocking = 1;
        ioctlsocket(SOCKET(fd), FIONBIO, &nonBlocking);
        int rc = ::connect(SOCKET(fd), ai->ai_addr, int(ai->ai_addrlen));
        if (rc == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(SOCKET(fd), &wfds);
            timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
            rc = select(0, nullptr, &wfds, nullptr, &tv) == 1 ? 0 : -1;
            if (rc == 0) {
                int soerr = 0;
                int sl = sizeof soerr;
                if (getsockopt(SOCKET(fd), SOL_SOCKET, SO_ERROR,
                               reinterpret_cast<char*>(&soerr), &sl) != 0 || soerr != 0)
                    rc = -1;
            }
        } else if (rc == SOCKET_ERROR) {
            rc = -1;
        }
        nonBlocking = 0;
        ioctlsocket(SOCKET(fd), FIONBIO, &nonBlocking);
#endif
        if (rc == 0) break;
        sockClose(fd);
        fd = kInvalidSocket;
    }
    freeaddrinfo(res);
    if (!socketValid(fd)) err = "cannot connect to " + host;
    return fd;
}

int sockSend(SocketHandle fd, const uint8_t* p, int n) {
    int sent = 0;
    while (sent < n) {
#ifdef _WIN32
        const int rc = ::send(SOCKET(fd), reinterpret_cast<const char*>(p) + sent, n - sent, 0);
#else
        const int rc = int(::send(fd, p + sent, std::size_t(n - sent), MSG_NOSIGNAL));
#endif
        if (rc <= 0) return -1;
        sent += rc;
    }
    return sent;
}

int sockRecv(SocketHandle fd, uint8_t* p, int n, int timeoutMs) {
#ifndef _WIN32
    pollfd pf{fd, POLLIN, 0};
    const int pr = poll(&pf, 1, timeoutMs);
    if (pr == 0) return -2;
    if (pr < 0) return -1;
    const int rc = int(::recv(fd, p, std::size_t(n), 0));
#else
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(SOCKET(fd), &fds);
    timeval tv{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    const int pr = select(0, &fds, nullptr, nullptr, &tv);
    if (pr == 0) return -2;
    if (pr < 0) return -1;
    const int rc = ::recv(SOCKET(fd), reinterpret_cast<char*>(p), n, 0);
#endif
    return rc < 0 ? -1 : rc;
}

bool sockHasData(SocketHandle fd) {
#ifndef _WIN32
    pollfd pf{fd, POLLIN, 0};
    return poll(&pf, 1, 0) == 1 && (pf.revents & POLLIN);
#else
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(SOCKET(fd), &fds);
    timeval tv{0, 0};
    return select(0, &fds, nullptr, nullptr, &tv) == 1;
#endif
}

// -----------------------------------------------------------------------------
//  URL
// -----------------------------------------------------------------------------
bool parseUrl(const std::string& url, Url& out) {
    const auto sep = url.find("://");
    if (sep == std::string::npos) return false;
    out.scheme = url.substr(0, sep);
    for (char& c : out.scheme) c = char(tolower(uint8_t(c)));
    std::string rest = url.substr(sep + 3);
    const auto delim = rest.find_first_of("/?#");
    std::string hostPort = (delim == std::string::npos) ? rest : rest.substr(0, delim);
    out.path = (delim == std::string::npos || rest[delim] == '#') ? "/"
             : (rest[delim] == '?' ? "/" + rest.substr(delim) : rest.substr(delim));
    if (const auto fragment = out.path.find('#'); fragment != std::string::npos)
        out.path.resize(fragment);

    std::string portText;
    if (!hostPort.empty() && hostPort.front() == '[') {
        const auto close = hostPort.find(']');
        if (close == std::string::npos) return false;
        out.host = hostPort.substr(1, close - 1);
        if (close + 1 < hostPort.size()) {
            if (hostPort[close + 1] != ':') return false;
            portText = hostPort.substr(close + 2);
        }
    } else {
        const auto colon = hostPort.rfind(':');
        if (colon != std::string::npos) {
            // Une adresse IPv6 littérale doit être entre crochets ; sinon le
            // dernier groupe serait pris à tort pour un port.
            if (hostPort.find(':') != colon) return false;
            out.host = hostPort.substr(0, colon);
            portText = hostPort.substr(colon + 1);
        } else {
            out.host = hostPort;
        }
    }
    out.port = (out.scheme == "http") ? 80 : (out.scheme == "https") ? 443 : 0;
    if (!portText.empty()) {
        if (!std::all_of(portText.begin(), portText.end(), [](unsigned char c) { return std::isdigit(c); }))
            return false;
        const unsigned long port = std::strtoul(portText.c_str(), nullptr, 10);
        if (port == 0 || port > 65535) return false;
        out.port = int(port);
    } else if (hostPort.find(':') != std::string::npos && hostPort.back() == ':') {
        return false;
    }
    return !out.host.empty();
}

// -----------------------------------------------------------------------------
//  HTTP
// -----------------------------------------------------------------------------
namespace {

// Lit la réponse complète (Connection: close) puis sépare en-têtes/corps.
// Budget MURAL de 30 s (pas seulement d'inactivité) : un serveur qui égrène un
// octet toutes les 4 s ne peut plus bloquer l'appelant indéfiniment.
bool readAll(SocketHandle fd, std::vector<uint8_t>& raw, std::string& err,
             const std::atomic<bool>* cancel) {
    uint8_t tmp[8192];
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (true) {
        if (cancel && cancel->load()) { err = "canceled"; return false; }
        const int rc = sockRecv(fd, tmp, int(sizeof tmp), 1000);
        if (rc == 0) return true;                 // fermeture propre
        if (rc == -1) { err = "recv error"; return false; }
        if (std::chrono::steady_clock::now() >= deadline) {
            err = "timeout";
            return false;
        }
        if (rc == -2) continue;
        raw.insert(raw.end(), tmp, tmp + rc);
        if (raw.size() > 128u * 1024u * 1024u) { err = "response too large (>128MB)"; return false; }
    }
}

bool dechunk(const std::vector<uint8_t>& in, std::vector<uint8_t>& out) {
    std::size_t p = 0;
    while (p < in.size()) {
        std::size_t eol = p;
        while (eol + 1 < in.size() && !(in[eol] == '\r' && in[eol + 1] == '\n')) ++eol;
        if (eol + 1 >= in.size()) return false;
        std::string lenStr(reinterpret_cast<const char*>(in.data()) + p, eol - p);
        if (const auto ext = lenStr.find(';'); ext != std::string::npos) lenStr.resize(ext);
        if (lenStr.empty() || !std::all_of(lenStr.begin(), lenStr.end(),
                                           [](unsigned char c) { return std::isxdigit(c); }))
            return false;
        const unsigned long long parsed = std::strtoull(lenStr.c_str(), nullptr, 16);
        if (parsed > std::numeric_limits<std::size_t>::max()) return false;
        const std::size_t n = std::size_t(parsed);
        p = eol + 2;
        if (n == 0) {
            // Un corps chunked se termine par une ligne vide après le chunk zéro
            // (les éventuels trailers sont donc eux aussi complètement reçus).
            if (p + 2 <= in.size() && in[p] == '\r' && in[p + 1] == '\n') return true;
            const char endTrailers[] = "\r\n\r\n";
            return std::search(in.begin() + long(p), in.end(), endTrailers, endTrailers + 4) != in.end();
        }
        // Comparaison par soustraction (p ≤ in.size() ici) : `p + n` pouvait
        // déborder avec une longueur hostile type ffffffffffffffff.
        if (n > in.size() - p) return false;
        out.insert(out.end(), in.begin() + long(p), in.begin() + long(p + n));
        p += n;
        if (p + 2 > in.size() || in[p] != '\r' || in[p + 1] != '\n') return false;
        p += 2;                                   // saute le CRLF du chunk
    }
    return true;
}

std::string headerValue(const std::string& headers, const std::string& key) {
    std::string lower = headers;
    for (char& c : lower) c = char(tolower(uint8_t(c)));
    std::string k = key + ":";
    for (char& c : k) c = char(tolower(uint8_t(c)));
    // Ancré en début de ligne : sinon « Content-Location » matcherait « Location »
    // (ou la clé pourrait être trouvée dans la VALEUR d'un autre en-tête).
    std::size_t pos = 0;
    while (true) {
        pos = lower.find(k, pos);
        if (pos == std::string::npos) return {};
        if (pos == 0 || (pos >= 2 && lower[pos - 2] == '\r' && lower[pos - 1] == '\n')) break;
        ++pos;
    }
    auto s = pos + k.size();
    while (s < headers.size() && (headers[s] == ' ' || headers[s] == '\t')) ++s;
    auto e = headers.find("\r\n", s);
    if (e == std::string::npos) e = headers.size();
    while (e > s && (headers[e - 1] == ' ' || headers[e - 1] == '\t')) --e;
    return headers.substr(s, e - s);
}

} // namespace

HttpResult httpFetch(const std::string& url, const std::string* postBody,
                     const std::vector<std::string>* headers,
                     const std::atomic<bool>* cancel) {
    HttpResult r;
    std::string cur = url;
    for (int redirect = 0; redirect < 5; ++redirect) {
        Url u;
        if (!parseUrl(cur, u)) { r.error = "bad URL: " + cur; return r; }
        if (u.scheme == "https") {
            r.error = "https:// is not supported yet (no TLS backend) — use http://";
            return r;
        }
        if (u.scheme != "http") { r.error = "unsupported scheme: " + u.scheme; return r; }

        const SocketHandle fd = tcpConnect(u.host, u.port, 5000, r.error);
        if (!socketValid(fd)) return r;

        const std::string hostHeader = (u.host.find(':') != std::string::npos ? "[" + u.host + "]" : u.host)
                                     + ((u.port == 80) ? "" : ":" + std::to_string(u.port));
        std::string req = std::string(postBody ? "POST " : "GET ") + u.path + " HTTP/1.1\r\n"
                        + "Host: " + hostHeader + "\r\n"
                        + "User-Agent: NeoST-FujiNet/1.0\r\n"
                        + "Accept: */*\r\nConnection: close\r\n";
        if (headers)
            for (const auto& h : *headers) req += h + "\r\n";
        if (postBody)
            req += "Content-Length: " + std::to_string(postBody->size()) + "\r\n";
        req += "\r\n";
        if (postBody) req += *postBody;

        if (sockSend(fd, reinterpret_cast<const uint8_t*>(req.data()), int(req.size())) < 0) {
            sockClose(fd);
            r.error = "send error";
            return r;
        }
        std::vector<uint8_t> raw;
        const bool okRead = readAll(fd, raw, r.error, cancel);
        sockClose(fd);
        if (!okRead) return r;

        // Sépare l'en-tête du corps.
        const char* sep = "\r\n\r\n";
        const auto it = std::search(raw.begin(), raw.end(), sep, sep + 4);
        if (it == raw.end()) { r.error = "malformed HTTP response"; return r; }
        const std::string head(raw.begin(), it);
        std::vector<uint8_t> body(it + 4, raw.end());

        // Ligne de statut sans espace (serveur cassé/hostile) : find() == npos
        // ferait déborder le pointeur passé à atoi.
        const auto sp = head.find(' ');
        if (sp == std::string::npos) { r.error = "malformed HTTP status line"; return r; }
        r.status = std::atoi(head.c_str() + sp + 1);
        if (r.status >= 301 && r.status <= 308 && r.status != 304) {
            const std::string loc = headerValue(head, "Location");
            if (!loc.empty()) {
                const std::string authority = "http://"
                    + (u.host.find(':') != std::string::npos ? "[" + u.host + "]" : u.host)
                    + ((u.port == 80) ? "" : ":" + std::to_string(u.port));
                if (loc.find("://") != std::string::npos) cur = loc;
                else if (loc.rfind("//", 0) == 0) cur = "http:" + loc;
                else if (!loc.empty() && loc.front() == '/') cur = authority + loc;
                else if (!loc.empty() && loc.front() == '?') {
                    const auto query = u.path.find('?');
                    cur = authority + u.path.substr(0, query) + loc;
                }
                else {
                    const auto slash = u.path.rfind('/');
                    cur = authority + u.path.substr(0, slash == std::string::npos ? 0 : slash + 1) + loc;
                }
                continue;
            }
        }
        std::string te = headerValue(head, "Transfer-Encoding");
        for (char& c : te) c = char(tolower(uint8_t(c)));
        if (te.find("chunked") != std::string::npos) {
            r.body.clear();
            if (!dechunk(body, r.body)) {
                r.status = 0;
                r.body.clear();
                r.error = "bad chunked body";
                return r;
            }
        } else {
            r.body = std::move(body);
            const std::string cl = headerValue(head, "Content-Length");
            if (!cl.empty()) {
                if (!std::all_of(cl.begin(), cl.end(), [](unsigned char c) { return std::isdigit(c); })) {
                    r.status = 0; r.body.clear(); r.error = "bad Content-Length"; return r;
                }
                const unsigned long long expected = std::strtoull(cl.c_str(), nullptr, 10);
                if (expected != r.body.size()) {
                    r.status = 0; r.body.clear(); r.error = "truncated HTTP body"; return r;
                }
            }
        }
        return r;
    }
    r.error = "too many redirects";
    return r;
}

} // namespace neonet
