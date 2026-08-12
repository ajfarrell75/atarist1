// =============================================================================
//  HttpClient.cpp — Client HTTP 1.1 minimal + aides sockets (cf. HttpClient.hpp).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "net/HttpClient.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
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

void sockClose(int fd) {
    if (fd < 0) return;
#ifdef _WIN32
    closesocket(SOCKET(fd));
#else
    ::close(fd);
#endif
}

int tcpConnect(const std::string& host, int port, int timeoutMs, std::string& err) {
    netInitOnce();
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    char portStr[16];
    std::snprintf(portStr, sizeof portStr, "%d", port);
    const int gai = getaddrinfo(host.c_str(), portStr, &hints, &res);
    if (gai != 0 || !res) { err = "cannot resolve " + host; return -1; }

    int fd = -1;
    for (addrinfo* ai = res; ai; ai = ai->ai_next) {
        fd = int(socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (fd < 0) continue;
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
        (void)timeoutMs;
        int rc = ::connect(SOCKET(fd), ai->ai_addr, int(ai->ai_addrlen));
#endif
        if (rc == 0) break;
        sockClose(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) err = "cannot connect to " + host;
    return fd;
}

int sockSend(int fd, const uint8_t* p, int n) {
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

int sockRecv(int fd, uint8_t* p, int n, int timeoutMs) {
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

bool sockHasData(int fd) {
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
    const auto slash = rest.find('/');
    std::string hostPort = (slash == std::string::npos) ? rest : rest.substr(0, slash);
    out.path = (slash == std::string::npos) ? "/" : rest.substr(slash);
    const auto colon = hostPort.rfind(':');
    if (colon != std::string::npos) {
        out.host = hostPort.substr(0, colon);
        out.port = std::atoi(hostPort.c_str() + colon + 1);
    } else {
        out.host = hostPort;
        out.port = (out.scheme == "http") ? 80 : (out.scheme == "https") ? 443 : 0;
    }
    return !out.host.empty();
}

// -----------------------------------------------------------------------------
//  HTTP
// -----------------------------------------------------------------------------
namespace {

// Lit la réponse complète (Connection: close) puis sépare en-têtes/corps.
bool readAll(int fd, std::vector<uint8_t>& raw, std::string& err) {
    uint8_t tmp[8192];
    int idleBudget = 30000;                       // budget total ~30 s
    while (true) {
        const int rc = sockRecv(fd, tmp, int(sizeof tmp), 5000);
        if (rc == 0) return true;                 // fermeture propre
        if (rc == -1) { err = "recv error"; return !raw.empty(); }
        if (rc == -2) {
            idleBudget -= 5000;
            if (idleBudget <= 0) { err = "timeout"; return !raw.empty(); }
            continue;
        }
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
        const std::string lenStr(reinterpret_cast<const char*>(in.data()) + p, eol - p);
        const unsigned long n = std::strtoul(lenStr.c_str(), nullptr, 16);
        p = eol + 2;
        if (n == 0) return true;
        if (p + n > in.size()) return false;
        out.insert(out.end(), in.begin() + long(p), in.begin() + long(p + n));
        p += n + 2;                               // saute le CRLF du chunk
    }
    return true;
}

std::string headerValue(const std::string& headers, const std::string& key) {
    std::string lower = headers;
    for (char& c : lower) c = char(tolower(uint8_t(c)));
    std::string k = key + ":";
    for (char& c : k) c = char(tolower(uint8_t(c)));
    const auto pos = lower.find(k);
    if (pos == std::string::npos) return {};
    auto s = pos + k.size();
    while (s < headers.size() && headers[s] == ' ') ++s;
    auto e = headers.find("\r\n", s);
    return headers.substr(s, e == std::string::npos ? std::string::npos : e - s);
}

} // namespace

HttpResult httpFetch(const std::string& url, const std::string* postBody,
                     const std::vector<std::string>* headers) {
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

        const int fd = tcpConnect(u.host, u.port, 5000, r.error);
        if (fd < 0) return r;

        std::string req = std::string(postBody ? "POST " : "GET ") + u.path + " HTTP/1.1\r\n"
                        + "Host: " + u.host + "\r\n"
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
        const bool okRead = readAll(fd, raw, r.error);
        sockClose(fd);
        if (!okRead && raw.empty()) return r;

        // Sépare l'en-tête du corps.
        const char* sep = "\r\n\r\n";
        const auto it = std::search(raw.begin(), raw.end(), sep, sep + 4);
        if (it == raw.end()) { r.error = "malformed HTTP response"; return r; }
        const std::string head(raw.begin(), it);
        std::vector<uint8_t> body(it + 4, raw.end());

        r.status = std::atoi(head.c_str() + head.find(' '));
        if (r.status >= 301 && r.status <= 308 && r.status != 304) {
            const std::string loc = headerValue(head, "Location");
            if (!loc.empty()) {
                cur = (loc.find("://") != std::string::npos)
                          ? loc
                          : "http://" + u.host + ":" + std::to_string(u.port) + loc;
                continue;
            }
        }
        std::string te = headerValue(head, "Transfer-Encoding");
        for (char& c : te) c = char(tolower(uint8_t(c)));
        if (te.find("chunked") != std::string::npos) {
            r.body.clear();
            if (!dechunk(body, r.body)) { r.error = "bad chunked body"; return r; }
        } else {
            r.body = std::move(body);
        }
        return r;
    }
    r.error = "too many redirects";
    return r;
}

} // namespace neonet
