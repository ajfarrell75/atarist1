// =============================================================================
//  FujiHostReplay.cpp — Backend FujiNet de rejeu (fixtures, déterministe).
//
//  (c) 2026 VERHILLE Arnaud — projet NeoST.
// =============================================================================
#include "net/FujiHostReplay.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>

#include "net/MiniJson.hpp"

namespace {
void rtrace(const char* fmt, const std::string& a, const std::string& b = {}) {
    static const bool on = std::getenv("NEOST_FUJI_TRACE") != nullptr;
    if (on) std::fprintf(stderr, "[fuji-replay] %s%s%s\n", fmt, a.c_str(), b.c_str());
}
} // namespace

std::string FujiHostReplay::sanitize(const std::string& spec) {
    std::string out;
    out.reserve(spec.size());
    for (char c : spec) {
        const bool okc = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                         (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        out += okc ? c : '_';
    }
    return out;
}

uint8_t FujiHostReplay::open(int chan, const std::string& spec, uint8_t, uint8_t) {
    Chan* c = at(chan);
    if (!c) return fn_err::BAD_CMD;
    const std::string path = dir_ + "/" + sanitize(spec);
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        rtrace("open MISS: ", spec, " (" + path + ")");
        c->open = false;
        c->lastError = fn_err::OFFLINE;
        return fn_err::OFFLINE;
    }
    c->buf.assign(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
    c->open = true;
    c->lastError = fn_err::OK;
    rtrace("open ", spec, " (" + std::to_string(c->buf.size()) + " bytes)");
    return fn_err::OK;
}

uint8_t FujiHostReplay::close(int chan) {
    if (Chan* c = at(chan)) { c->open = false; c->buf.clear(); c->jsonSrc.clear(); }
    return fn_err::OK;
}

int FujiHostReplay::read(int chan, uint8_t* dst, int len) {
    Chan* c = at(chan);
    if (!c || !c->open) return -1;
    const int n = std::min<int>(len, int(c->buf.size()));
    std::memcpy(dst, c->buf.data(), std::size_t(n));
    c->buf.erase(c->buf.begin(), c->buf.begin() + n);
    return n;
}

uint8_t FujiHostReplay::write(int chan, const uint8_t* src, int len) {
    Chan* c = at(chan);
    if (!c || !c->open) return fn_err::IO_ERROR;
    rtrace("write chan (discarded, replay): ",
           std::string(reinterpret_cast<const char*>(src), std::size_t(std::min(len, 64))));
    return fn_err::OK;
}

FujiChanStatus FujiHostReplay::status(int chan) {
    Chan* c = at(chan);
    FujiChanStatus st;
    if (!c || !c->open) return st;
    st.avail = uint16_t(std::min<std::size_t>(c->buf.size(), 65535));
    st.connected = c->buf.empty() ? 0 : 1;
    st.error = c->buf.empty() ? 136 /* EOF FujiNet */ : fn_err::OK;
    return st;
}

uint8_t FujiHostReplay::jsonParse(int chan) {
    Chan* c = at(chan);
    if (!c || !c->open) return fn_err::IO_ERROR;
    c->jsonSrc.assign(c->buf.begin(), c->buf.end());
    return minijson::looksLikeJson(c->jsonSrc) ? fn_err::OK : fn_err::BAD_CMD;
}

uint8_t FujiHostReplay::jsonQuery(int chan, const std::string& query) {
    Chan* c = at(chan);
    if (!c || !c->open || c->jsonSrc.empty()) return fn_err::IO_ERROR;
    std::string val;
    if (!minijson::query(c->jsonSrc, query, val)) return fn_err::BAD_CMD;
    c->buf.assign(val.begin(), val.end());
    return fn_err::OK;
}

std::string FujiHostReplay::fetchToFile(const std::string& url) {
    const std::string path = dir_ + "/" + sanitize(url);
    std::ifstream f(path, std::ios::binary);
    if (!f) { rtrace("fetch MISS: ", url); return {}; }
    rtrace("fetch ", url, " -> " + path);
    return path;
}

void FujiHostReplay::reset() {
    for (auto& c : chan_) { c.open = false; c.buf.clear(); c.jsonSrc.clear(); c.lastError = fn_err::OK; }
}
