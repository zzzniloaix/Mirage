#include "NetworkLogger.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string_view>

extern "C" {
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libavutil/log.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
}

std::atomic<NetworkLogger*> NetworkLogger::instance_{ nullptr };

const char* net_initiator_name(NetInitiator who)
{
    switch (who) {
        case NetInitiator::Playback:   return "playback";
        case NetInitiator::Thumbnails: return "thumbnails";
        case NetInitiator::Scrub:      return "scrub";
        case NetInitiator::Subtitles:  return "subtitles";
        case NetInitiator::Manifest:   return "manifest";
        case NetInitiator::Other:      break;
    }
    return "other";
}

// ── Small string helpers ─────────────────────────────────────────────────────

namespace {

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

bool iequals(std::string_view a, std::string_view b)
{
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

// "https://host/a/b?x" → "https"; "/local/file.mp4" → "file".
std::string url_scheme(const std::string& url)
{
    const auto p = url.find("://");
    if (p == std::string::npos || p == 0) return "file";
    for (std::size_t i = 0; i < p; ++i)
        if (!std::isalnum(static_cast<unsigned char>(url[i])) && url[i] != '+' && url[i] != '-' && url[i] != '.')
            return "file";
    std::string s = url.substr(0, p);
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    // "crypto+https" → "https" for display purposes
    if (const auto plus = s.rfind('+'); plus != std::string::npos) s = s.substr(plus + 1);
    return s;
}

// Path (+query) part of an absolute URL: "https://h/a/b?q" → "/a/b?q".
std::string_view url_path(std::string_view url)
{
    const auto p = url.find("://");
    if (p == std::string_view::npos) return url;
    const auto slash = url.find('/', p + 3);
    return slash == std::string_view::npos ? std::string_view("/") : url.substr(slash);
}

// "bytes=100-199" → {100, 199}; "bytes=100-" → {100, -1}.
void parse_range(std::string_view v, std::int64_t& a, std::int64_t& b)
{
    v = trim(v);
    if (v.starts_with("bytes=")) v.remove_prefix(6);
    const auto dash = v.find('-');
    if (dash == std::string_view::npos) return;
    const std::string s1(v.substr(0, dash)), s2(v.substr(dash + 1));
    if (!s1.empty()) a = std::strtoll(s1.c_str(), nullptr, 10);
    if (!s2.empty()) b = std::strtoll(s2.c_str(), nullptr, 10);
}

// Per-thread state that ties log lines to the io_open running on this thread.
struct TlsOpen {
    const NetworkLogger* owner   = nullptr;
    bool          active         = false;
    NetInitiator  who            = NetInitiator::Other;
    std::string   url;
    std::int64_t  t0             = 0;
    std::uint32_t conn_id        = 0;
    bool          first_used     = false;   // t0 already given to a request
    std::uint64_t last_req       = 0;       // newest request created during this open
    const void*   req_ctx        = nullptr; // URLContext that logged it
    std::string   last_opening;             // from the latest "Opening '…'" line
};
thread_local TlsOpen t_open;

} // namespace

// ── Lifetime / install ───────────────────────────────────────────────────────

NetworkLogger::NetworkLogger() = default;

NetworkLogger::~NetworkLogger()
{
    uninstall();
}

void NetworkLogger::install()
{
    instance_.store(this, std::memory_order_release);
    av_log_set_callback(av_log_cb);
    installed_ = true;
}

void NetworkLogger::uninstall()
{
    if (installed_.exchange(false)) {
        av_log_set_callback(av_log_default_callback);
        NetworkLogger* self = this;
        instance_.compare_exchange_strong(self, nullptr);
    }
}

std::int64_t NetworkLogger::now_us()
{
    static const auto t0 = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - t0).count();
}

std::int64_t NetworkLogger::wall_ms(std::int64_t t_us)
{
    // Offset between the steady clock and wall time, sampled once.
    static const std::int64_t offset_us = [] {
        const auto wall = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        return wall - NetworkLogger::now_us();
    }();
    return (t_us + offset_us) / 1000;
}

// ── av_log callback ──────────────────────────────────────────────────────────

void NetworkLogger::av_log_cb(void* avcl, int level, const char* fmt, va_list vl)
{
    va_list vl_default;
    va_copy(vl_default, vl);
    av_log_default_callback(avcl, level, fmt, vl_default);
    va_end(vl_default);

    NetworkLogger* self = instance();
    if (!self || !fmt) return;

    // Match on the format string first so only the handful of lines we parse
    // are ever formatted (http.c traces every header line at AV_LOG_TRACE;
    // the callback sees them regardless of the av_log level).
    static constexpr const char* kPrefixes[] = {
        "Opening ", "request: ", "header='", "HTTP error ", "Will reconnect at ",
        "URL read error: ", "Error reading HTTP response: ", "Statistics: ",
    };
    bool interesting = false;
    for (const char* p : kPrefixes)
        if (std::strncmp(fmt, p, std::strlen(p)) == 0) { interesting = true; break; }
    if (!interesting) return;

    char buf[4096];
    std::vsnprintf(buf, sizeof(buf), fmt, vl);
    self->ev_log(avcl, buf);
    if (level <= AV_LOG_VERBOSE && std::strncmp(fmt, "Opening ", 8) == 0)
        self->ingest_line(buf);
}

// ── FFmpeg hooks ─────────────────────────────────────────────────────────────

namespace {

using IoOpenFn   = int (*)(AVFormatContext*, AVIOContext**, const char*, int, AVDictionary**);
using IoClose2Fn = int (*)(AVFormatContext*, AVIOContext*);

std::atomic<IoOpenFn>   g_orig_open{ nullptr };
std::atomic<IoClose2Fn> g_orig_close2{ nullptr };

constexpr NetInitiator kInitiators[] = {
    NetInitiator::Playback, NetInitiator::Thumbnails, NetInitiator::Scrub,
    NetInitiator::Subtitles, NetInitiator::Manifest, NetInitiator::Other,
};

std::string av_err_text(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

void finish_open(NetworkLogger* L, int ret, AVIOContext* pb)
{
    std::string err, mime;
    std::int64_t size = -1;
    const void* log_ctx = nullptr;
    const std::int64_t* counter = nullptr;
    if (ret < 0) {
        err = av_err_text(ret);
    } else if (pb) {
        log_ctx = pb->opaque;            // the URLContext http.c logs with
        counter = &pb->bytes_read;
        std::uint8_t* m = nullptr;
        if (av_opt_get(pb, "mime_type", AV_OPT_SEARCH_CHILDREN, &m) >= 0 && m) {
            mime = reinterpret_cast<const char*>(m);
            av_free(m);
        }
        // HTTP: the Content-Length FFmpeg already parsed (no I/O); file: fstat.
        size = avio_size(pb);
    }
    L->ev_open_end(ret, pb, log_ctx, counter, size, mime, err);
}

int hooked_io_open(AVFormatContext* s, AVIOContext** pb, const char* url, int flags, AVDictionary** opts)
{
    IoOpenFn orig = g_orig_open.load();
    NetworkLogger* L = NetworkLogger::instance();
    if (!L || (flags & AVIO_FLAG_WRITE) || !url)
        return orig(s, pb, url, flags, opts);

    const NetInitiator who = s->opaque ? *static_cast<const NetInitiator*>(s->opaque)
                                       : NetInitiator::Other;
    L->ev_open_begin(who, url);
    const int ret = orig(s, pb, url, flags, opts);
    finish_open(L, ret, ret >= 0 ? *pb : nullptr);
    return ret;
}

int hooked_io_close2(AVFormatContext* s, AVIOContext* pb)
{
    if (NetworkLogger* L = NetworkLogger::instance(); L && pb)
        L->ev_close(pb);
    return g_orig_close2.load()(s, pb);
}

} // namespace

void NetworkLogger::hook_format_context(AVFormatContext* ctx, NetInitiator who)
{
    if (!ctx || !ctx->io_open || !ctx->io_close2) return;
    if (ctx->io_open != hooked_io_open) {
        IoOpenFn expected = nullptr;
        g_orig_open.compare_exchange_strong(expected, ctx->io_open);
        IoClose2Fn expected_c = nullptr;
        g_orig_close2.compare_exchange_strong(expected_c, ctx->io_close2);
        // Only the stock defaults are wrapped; a context with custom callbacks
        // keeps them (its traffic is still seen through the av_log path).
        if (g_orig_open.load() != ctx->io_open || g_orig_close2.load() != ctx->io_close2) return;
        ctx->io_open   = hooked_io_open;
        ctx->io_close2 = hooked_io_close2;
    }
    ctx->opaque = const_cast<NetInitiator*>(&kInitiators[static_cast<int>(who)]);
}

void NetworkLogger::track_open_begin(NetInitiator who, const char* url)
{
    if (NetworkLogger* L = instance(); L && url) L->ev_open_begin(who, url);
}

void NetworkLogger::track_open_end(int ret, AVIOContext* pb)
{
    if (NetworkLogger* L = instance()) finish_open(L, ret, ret >= 0 ? pb : nullptr);
}

void NetworkLogger::track_close(AVIOContext* pb)
{
    if (NetworkLogger* L = instance(); L && pb) L->ev_close(pb);
}

// ── Request store helpers (mtx_ held) ────────────────────────────────────────

NetRequest* NetworkLogger::find_locked(std::uint64_t id)
{
    if (id == 0 || reqs_.empty() || id < reqs_.front().id || id > reqs_.back().id) return nullptr;
    auto it = std::lower_bound(reqs_.begin(), reqs_.end(), id,
                               [](const NetRequest& r, std::uint64_t v) { return r.id < v; });
    return (it != reqs_.end() && it->id == id) ? &*it : nullptr;
}

void NetworkLogger::touch_locked(NetRequest& r)
{
    r.seq = seq_.fetch_add(1, std::memory_order_acq_rel) + 1;
}

NetRequest& NetworkLogger::push_locked(NetRequest r)
{
    r.id = next_id_++;
    reqs_.push_back(std::move(r));
    while (reqs_.size() > kMaxRequests) reqs_.pop_front();
    touch_locked(reqs_.back());
    return reqs_.back();
}

void NetworkLogger::finish_locked(NetRequest& r, std::int64_t now, std::int64_t bytes)
{
    if (bytes >= 0) r.bytes = bytes;
    // Ended (next request on the handle, or closed) before its body was
    // read: e.g. a variant probed at startup, or a segment the player left.
    // Its time is the transfer that happened, not the wait for the next request.
    if (r.t_end < 0 && !r.failed && r.protocol != "file" && r.bytes >= 0
        && r.content_length > 0 && r.bytes < r.content_length) {
        r.partial = true;
        const std::int64_t last = std::max(r.t_last_byte, r.t_first_byte);
        r.t_end = last >= 0 ? last : now;
    }
    if (r.t_end < 0) r.t_end = now;
    touch_locked(r);
}

NetworkLogger::Conn* NetworkLogger::conn_by_log_ctx_locked(const void* log_ctx)
{
    if (!log_ctx) return nullptr;
    for (auto& [pb, c] : conns_)
        if (c.log_ctx == log_ctx) return &c;
    return nullptr;
}

// ── Events ───────────────────────────────────────────────────────────────────

void NetworkLogger::ev_open_begin(NetInitiator who, const std::string& url)
{
    std::uint32_t cid;
    {
        std::lock_guard lk(mtx_);
        cid = next_conn_++;
    }
    t_open = {};
    t_open.owner   = this;
    t_open.active  = true;
    t_open.who     = who;
    t_open.url     = url;
    t_open.t0      = now_us();
    t_open.conn_id = cid;
}

void NetworkLogger::ev_open_end(int ret, const void* pb, const void* log_ctx,
                                const std::int64_t* counter, std::int64_t size_hint,
                                const std::string& mime_hint, const std::string& err_text)
{
    if (t_open.owner != this || !t_open.active) return;
    const std::int64_t now = now_us();
    std::lock_guard lk(mtx_);
    NetRequest* r = find_locked(t_open.last_req);

    if (ret < 0 || !pb) {
        if (r) {
            r->failed = true;
            if (r->error.empty()) r->error = err_text;
            finish_locked(*r, now, -1);
        } else if (t_open.last_req == 0 && recording_.load()) {
            NetRequest n;
            n.url       = t_open.url;
            n.protocol  = url_scheme(t_open.url);
            n.initiator = t_open.who;
            n.t_start   = t_open.t0;
            n.t_end     = now;
            n.conn_id   = t_open.conn_id;
            n.failed    = true;
            n.error     = err_text.empty() ? "open failed" : err_text;
            push_locked(std::move(n));
        }
        if (t_open.req_ctx) ctx_req_.erase(t_open.req_ctx);
        t_open.active = false;
        return;
    }

    Conn c;
    c.pb      = pb;
    c.log_ctx = t_open.req_ctx ? t_open.req_ctx : log_ctx;
    c.counter = counter;
    c.snap    = counter ? *counter : 0;
    c.conn_id = t_open.conn_id;
    c.who     = t_open.who;

    if (r) {
        // HTTP: the request row(s) came from http.c's log during the open.
        if (!r->done()) c.cur_req = r->id;
        if (r->mime.empty() && !mime_hint.empty()) r->mime = mime_hint;
        if (r->content_length < 0 && size_hint >= 0) r->content_length = size_hint;
        touch_locked(*r);
    } else if (t_open.last_req == 0 && recording_.load()) {
        // Non-HTTP (local file, data:, …) — or no http.c log for this open.
        NetRequest n;
        n.url            = t_open.url;
        n.protocol       = url_scheme(t_open.url);
        n.initiator      = t_open.who;
        n.t_start        = t_open.t0;
        n.t_sent         = t_open.t0;
        n.t_first_byte   = now;
        // A local file has no request/response: the row is the open, done
        // once the handle is ready (the file is then read through playback).
        n.t_end          = now;
        n.content_length = size_hint;
        n.mime           = mime_hint;
        n.conn_id        = t_open.conn_id;
        c.cur_req = push_locked(std::move(n)).id;
        if (c.log_ctx) ctx_req_[c.log_ctx] = c.cur_req;
    }
    conns_[pb] = c;
    t_open.active = false;
}

void NetworkLogger::ev_close(const void* pb)
{
    const std::int64_t now = now_us();
    std::lock_guard lk(mtx_);
    auto it = conns_.find(pb);
    if (it == conns_.end()) return;
    const Conn& c = it->second;
    // A local-file row is done at open; its close still reports what was read.
    if (NetRequest* r = find_locked(c.cur_req); r && (!r->done() || r->protocol == "file"))
        finish_locked(*r, now, c.counter ? *c.counter - c.snap : -1);
    if (c.log_ctx) ctx_req_.erase(c.log_ctx);
    conns_.erase(it);
}

void NetworkLogger::ev_log(const void* log_ctx, const char* line)
{
    if (!line) return;
    std::string_view s(line);

    if (s.starts_with("Opening '")) {
        s.remove_prefix(9);
        const auto q = s.rfind("' for ");
        if (q == std::string_view::npos) return;
        if (t_open.owner != this) {      // first line from this thread for this logger
            t_open = {};
            t_open.owner = this;
        }
        t_open.last_opening.assign(s.substr(0, q));
        return;
    }

    const std::int64_t now = now_us();
    std::lock_guard lk(mtx_);

    if (s.starts_with("request: ")) {
        on_request_line_locked(log_ctx, std::string(s.substr(9)), now);
        return;
    }
    if (s.starts_with("header='")) {
        s.remove_prefix(8);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.remove_suffix(1);
        if (!s.empty() && s.back() == '\'') s.remove_suffix(1);
        on_header_line_locked(log_ctx, std::string(s), now);
        return;
    }
    if (s.starts_with("Statistics: ")) {
        on_ctx_closed_locked(log_ctx, now);
        return;
    }

    auto it = ctx_req_.find(log_ctx);
    if (it == ctx_req_.end()) return;
    NetRequest* r = find_locked(it->second);
    if (!r) return;
    const std::string text(trim(s));
    if (s.starts_with("Will reconnect at ")) {
        ++r->reconnects;
        r->error = text;
    } else {
        // HTTP error / URL read error / Error reading HTTP response
        r->failed = true;
        r->error  = text;
    }
    touch_locked(*r);
}

void NetworkLogger::on_request_line_locked(const void* log_ctx, const std::string& text, std::int64_t now)
{
    Conn* conn = conn_by_log_ctx_locked(log_ctx);

    // A new request on a handle ends the previous one (keep-alive, redirect, seek).
    std::string prev_scheme;
    if (auto it = ctx_req_.find(log_ctx); it != ctx_req_.end()) {
        if (NetRequest* prev = find_locked(it->second)) {
            prev_scheme = prev->protocol;
            if (!prev->done())
                finish_locked(*prev, now, (conn && conn->counter && conn->cur_req == prev->id)
                                              ? *conn->counter - conn->snap : -1);
        }
        ctx_req_.erase(it);
    }
    if (conn) conn->cur_req = 0;

    const bool in_open = t_open.owner == this && t_open.active;
    if (!recording_.load()) {
        if (in_open) t_open.first_used = true;
        return;
    }

    NetRequest n;
    // Request line + headers, CRLF separated.
    std::string_view rest(text);
    bool first = true;
    std::string path, host;
    while (!rest.empty()) {
        auto eol = rest.find('\n');
        std::string_view ln = rest.substr(0, eol);
        rest = (eol == std::string_view::npos) ? std::string_view() : rest.substr(eol + 1);
        ln = trim(ln);
        if (ln.empty()) { if (first) continue; else break; }
        if (first) {
            first = false;
            const auto sp1 = ln.find(' ');
            const auto sp2 = ln.rfind(' ');
            if (sp1 != std::string_view::npos) {
                n.method.assign(ln.substr(0, sp1));
                path.assign(sp2 > sp1 ? ln.substr(sp1 + 1, sp2 - sp1 - 1) : ln.substr(sp1 + 1));
            }
            continue;
        }
        const auto colon = ln.find(':');
        if (colon == std::string_view::npos) continue;
        std::string k(trim(ln.substr(0, colon))), v(trim(ln.substr(colon + 1)));
        if (iequals(k, "Host"))  host = v;
        if (iequals(k, "Range")) parse_range(v, n.range_start, n.range_end);
        n.request_headers.emplace_back(std::move(k), std::move(v));
    }

    // Full URL: prefer what FFmpeg was asked to open (exact, with scheme).
    auto path_matches = [&](const std::string& cand) {
        if (cand.empty() || path.empty()) return false;
        if (path.starts_with("http")) return cand == path;      // proxy form
        return url_path(cand) == path;
    };
    if (path.starts_with("http://") || path.starts_with("https://"))
        n.url = path;
    else if (t_open.owner == this && path_matches(t_open.last_opening))
        n.url = t_open.last_opening;
    else if (in_open && path_matches(t_open.url))
        n.url = t_open.url;
    else {
        // Redirect / unknown: rebuild from Host, keeping the handle's scheme.
        std::string scheme = !prev_scheme.empty() ? prev_scheme
                           : in_open ? url_scheme(t_open.url) : std::string("http");
        if (scheme == "file") scheme = "http";
        n.url = scheme + "://" + host + path;
    }
    n.protocol = url_scheme(n.url);
    n.initiator = in_open ? t_open.who : conn ? conn->who : NetInitiator::Other;
    n.t_sent  = now;
    n.t_start = now;
    if (in_open && !t_open.first_used) {
        n.t_start = t_open.t0;          // includes DNS / TCP / TLS setup
        t_open.first_used = true;
    }
    n.conn_id = in_open ? t_open.conn_id : conn ? conn->conn_id : 0;
    n.reused_connection = !in_open && conn != nullptr;

    // Retry of something that failed recently?
    int scanned = 0;
    for (auto it = reqs_.rbegin(); it != reqs_.rend() && scanned < 400; ++it, ++scanned)
        if (it->failed && it->url == n.url && it->range_start == n.range_start) {
            n.retry_of_failed = true;
            break;
        }

    NetRequest& r = push_locked(std::move(n));
    ctx_req_[log_ctx] = r.id;
    if (conn) {
        conn->cur_req = r.id;
        conn->snap    = conn->counter ? *conn->counter : 0;
    }
    if (in_open) {
        t_open.last_req = r.id;
        t_open.req_ctx  = log_ctx;
    }
}

void NetworkLogger::on_header_line_locked(const void* log_ctx, const std::string& line, std::int64_t now)
{
    auto it = ctx_req_.find(log_ctx);
    if (it == ctx_req_.end()) return;
    NetRequest* r = find_locked(it->second);
    if (!r) return;

    std::string_view ln = trim(line);
    if (ln.empty()) return;
    if ((r->status == 0 || r->status == 100) && ln.starts_with("HTTP/")) {
        // "HTTP/1.1 206 Partial Content"
        const auto sp = ln.find(' ');
        r->http_version.assign(ln.substr(5, sp == std::string_view::npos ? std::string_view::npos : sp - 5));
        if (sp != std::string_view::npos) {
            std::string_view rest = ln.substr(sp + 1);
            r->status = std::atoi(std::string(rest.substr(0, 3)).c_str());
            const auto sp2 = rest.find(' ');
            r->status_text = sp2 == std::string_view::npos ? std::string() : std::string(trim(rest.substr(sp2 + 1)));
        }
        if (r->t_first_byte < 0) r->t_first_byte = now;
        if (r->status >= 400) r->failed = true;
        touch_locked(*r);
        return;
    }
    const auto colon = ln.find(':');
    if (colon == std::string_view::npos) return;
    std::string k(trim(ln.substr(0, colon))), v(trim(ln.substr(colon + 1)));
    if (iequals(k, "Content-Length")) r->content_length = std::strtoll(v.c_str(), nullptr, 10);
    if (iequals(k, "Content-Type"))   r->mime = v;
    r->response_headers.emplace_back(std::move(k), std::move(v));
    touch_locked(*r);
}

void NetworkLogger::on_ctx_closed_locked(const void* log_ctx, std::int64_t now)
{
    // avio_close() logs "Statistics:" with the AVIOContext itself, while it
    // is still alive — on every close path, including those that bypass
    // io_close2 (avformat_open_input's failure path). Drop the handle here so
    // poll() never reads the byte counter of a freed context.
    if (auto it = conns_.find(log_ctx); it != conns_.end()) {
        const Conn& c = it->second;
        if (NetRequest* r = find_locked(c.cur_req); r && !r->done())
            finish_locked(*r, now, c.counter ? *c.counter - c.snap : -1);
        if (c.log_ctx) ctx_req_.erase(c.log_ctx);
        conns_.erase(it);
    }
    if (auto it = ctx_req_.find(log_ctx); it != ctx_req_.end()) {
        if (NetRequest* r = find_locked(it->second); r && !r->done())
            finish_locked(*r, now, -1);
        ctx_req_.erase(it);
    }
    // A handle closed without our io_close2 (e.g. avformat_open_input's own
    // failure path): forget it so a recycled pointer can't pick it up.
    for (auto it = conns_.begin(); it != conns_.end(); )
        it = (it->second.log_ctx == log_ctx) ? conns_.erase(it) : std::next(it);
}

// ── Consumer API ─────────────────────────────────────────────────────────────

void NetworkLogger::poll(std::int64_t now)
{
    if (now < 0) now = now_us();
    std::lock_guard lk(mtx_);
    // Every handle in conns_ is alive: it is removed under mtx_ before it is
    // freed (ev_close, or the "Statistics:" line avio_close logs). The count
    // is written by the reading thread; an aligned 64-bit relaxed load is
    // enough for a progress readout.
    for (auto& [pb, c] : conns_) {
        if (!c.counter) continue;
        NetRequest* r = find_locked(c.cur_req);
        if (!r || r->failed || (r->done() && !r->partial)) continue;
        const std::int64_t got = __atomic_load_n(c.counter, __ATOMIC_RELAXED) - c.snap;
        if (got < 0) continue;
        if (got != r->bytes) {
            if (got > 0 || r->bytes > 0) r->t_last_byte = now;
            r->bytes = got;
            if (r->partial) { r->partial = false; r->t_end = -1; }   // reading resumed
            touch_locked(*r);
        }
        if (r->done()) continue;
        if (r->content_length > 0 && got >= r->content_length) {
            finish_locked(*r, r->t_last_byte >= 0 ? r->t_last_byte : now, got);
            continue;
        }
        const std::int64_t last = std::max(r->t_last_byte, r->t_first_byte);
        if (r->t_first_byte >= 0 && now - last >= kIdleUs) {
            r->partial = true;
            r->t_end   = last;
            touch_locked(*r);
        }
    }
}

void NetworkLogger::copy_changed_since(std::uint64_t since_seq, std::vector<NetRequest>& out,
                                       std::uint64_t& oldest_id) const
{
    std::lock_guard lk(mtx_);
    oldest_id = reqs_.empty() ? next_id_ : reqs_.front().id;
    for (const auto& r : reqs_)
        if (r.seq > since_seq) out.push_back(r);
}

std::size_t NetworkLogger::size() const
{
    std::lock_guard lk(mtx_);
    return reqs_.size();
}

void NetworkLogger::clear()
{
    std::lock_guard lk(mtx_);
    reqs_.clear();
    entries_.clear();
    generation_.fetch_add(1, std::memory_order_acq_rel);
    seq_.fetch_add(1, std::memory_order_acq_rel);
}

// ── Legacy log-line API ──────────────────────────────────────────────────────

std::vector<NetworkLogger::Entry> NetworkLogger::get_recent(int n) const
{
    std::lock_guard<std::mutex> lock(mtx_);
    int start = std::max(0, (int)entries_.size() - n);
    return { entries_.begin() + start, entries_.end() };
}

int NetworkLogger::total_count() const
{
    std::lock_guard<std::mutex> lock(mtx_);
    return static_cast<int>(entries_.size());
}

void NetworkLogger::ingest_line(const char* line)
{
    // Match: Opening 'URL' for reading
    static constexpr const char* kPrefix = "Opening '";
    static constexpr const char* kSuffix = "' for reading";

    const char* p = strstr(line, kPrefix);
    if (!p) return;
    p += strlen(kPrefix);

    const char* q = strstr(p, kSuffix);
    if (!q) return;

    std::string url(p, q);
    if (url.find("http") != 0) return;  // only HTTP/HTTPS

    auto now    = std::chrono::system_clock::now();
    auto t      = std::chrono::system_clock::to_time_t(now);
    int  ms_val = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count() % 1000);
    struct tm tm_info{};
    localtime_r(&t, &tm_info);
    char tsbuf[20];
    snprintf(tsbuf, sizeof(tsbuf), "%02d:%02d:%02d.%03d",
             tm_info.tm_hour, tm_info.tm_min, tm_info.tm_sec, ms_val);

    float r, g, b;
    std::string type = classify(url, r, g, b);

    std::string short_url = url;
    constexpr int kMaxUrlDisplay = 60;
    if ((int)short_url.size() > kMaxUrlDisplay)
        short_url = "..." + short_url.substr(short_url.size() - (kMaxUrlDisplay - 3));

    std::lock_guard<std::mutex> lock(mtx_);
    entries_.push_back({ tsbuf, type, short_url, r, g, b });
    if ((int)entries_.size() > kMaxEntries)
        entries_.pop_front();
}

// ── URL classification ───────────────────────────────────────────────────────

std::string NetworkLogger::classify(const std::string& url, float& r, float& g, float& b)
{
    // Extract just the path component (after host)
    std::string path;
    {
        auto slash = url.find('/', url.find("//") + 2);
        path = (slash != std::string::npos) ? url.substr(slash) : url;
    }

    // Get lower-case file extension
    std::string ext;
    {
        auto dot = path.rfind('.');
        if (dot != std::string::npos) {
            ext = path.substr(dot);
            for (auto& c : ext) c = static_cast<char>(std::tolower(c));
        }
    }

    auto path_has = [&](const char* needle) {
        return path.find(needle) != std::string::npos;
    };

    if (ext == ".mpd") {
        r = 0.35f; g = 0.90f; b = 0.35f;  // bright green — DASH manifest
        return "manifest";
    }
    if (ext == ".m3u8") {
        bool is_master = path_has("master") || url.find("master") != std::string::npos
                      || path_has("index") || path_has("playlist");
        if (is_master) {
            r = 0.35f; g = 0.90f; b = 0.35f;
            return "manifest";
        }
        r = 0.40f; g = 0.70f; b = 0.40f;  // dimmer green — variant playlist
        return "variant";
    }
    if (ext == ".mp4" && (path_has("init") || path_has("header") || path_has("Init"))) {
        r = 0.90f; g = 0.70f; b = 0.20f;  // orange — init segment
        return "init";
    }
    if (ext == ".m4s" || ext == ".ts" || ext == ".aac" || ext == ".m4a" ||
        (ext == ".mp4" && (path_has("seg") || path_has("frag") || path_has("chunk")))) {
        r = 0.50f; g = 0.55f; b = 0.90f;  // blue-purple — media segment
        return "segment";
    }
    if (ext == ".mp4" || ext == ".webm" || ext == ".mkv" || ext == ".mov") {
        r = 0.70f; g = 0.70f; b = 0.90f;  // light blue — plain media file
        return "media";
    }

    r = 0.60f; g = 0.60f; b = 0.60f;  // gray — other
    return "other";
}
