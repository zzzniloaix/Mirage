#pragma once

#include <atomic>
#include <cstdarg>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

struct AVFormatContext;
struct AVIOContext;

// Who issued a request — the "Initiator" column of the network inspector.
// Inspector: requests the inspector itself makes (alignment probes).
enum class NetInitiator : std::uint8_t { Playback, Thumbnails, Scrub, Subtitles, Manifest, Inspector, Other };
const char* net_initiator_name(NetInitiator who);

// One HTTP request (or one local-file open) as shown by the network inspector.
// Times are µs on NetworkLogger::now_us()'s steady clock; -1 = not reached.
struct NetRequest {
    std::uint64_t id  = 0;           // monotonic, never reused
    std::uint64_t seq = 0;           // NetworkLogger change counter at the last update
    std::string   url;               // full URL as FFmpeg was asked to open it
    std::string   method   = "GET";
    std::string   protocol;          // "https", "http", "file", ...
    NetInitiator  initiator = NetInitiator::Other;

    std::int64_t  t_start      = 0;  // open() called — includes DNS/TCP/TLS on a new connection
    std::int64_t  t_sent       = -1; // request written to the socket
    std::int64_t  t_first_byte = -1; // response status line received (TTFB = t_first_byte - t_sent)
    std::int64_t  t_end        = -1; // body finished: all Content-Length bytes read, the handle
                                     // closed or reused for the next request, or (partial) last byte
    std::int64_t  t_last_byte  = -1; // last time the body byte count was seen growing

    std::int64_t  bytes          = -1;  // body bytes FFmpeg read (−1 unknown)
    std::int64_t  content_length = -1;  // Content-Length / file size
    std::int64_t  range_start    = -1;  // requested Range, inclusive; −1 = none
    std::int64_t  range_end      = -1;  //   −1 = open-ended

    int           status = 0;        // HTTP status code, 0 = none / not HTTP
    std::string   status_text;       // "OK", "Not Found", ...
    std::string   http_version;      // "1.1"
    std::string   mime;              // Content-Type
    std::vector<std::pair<std::string, std::string>> request_headers;
    std::vector<std::pair<std::string, std::string>> response_headers;

    std::string   error;             // failure / reconnect text
    int           reconnects = 0;    // "Will reconnect" events while reading
    std::uint32_t conn_id    = 0;    // FFmpeg I/O handle this request ran on (0 = unknown)
    bool          reused_connection = false;  // keep-alive request on an open handle
    bool          retry_of_failed   = false;  // same URL/range failed earlier
    bool          failed            = false;
    // The player stopped reading this body before its end and left the
    // connection open (variant no longer selected, or its buffer is full):
    // t_end is the last byte, bytes what arrived. Cleared if reading resumes.
    bool          partial           = false;

    // Response body kept for the inspector (NetworkLogger::copy_body).
    std::int64_t  body_bytes     = 0;      // bytes kept
    bool          body_truncated = false;  // more arrived than was kept (head only / size cap)
    bool          body_dropped   = false;  // was kept, then dropped for the memory budget

    bool done() const { return t_end >= 0; }
    std::int64_t effective_bytes() const { return bytes >= 0 ? bytes : (done() ? content_length : -1); }
};

// QA (--net-report): JSON summary of the requests in `reqs` that started in
// [t0_us, t1_us): totals per initiator (requests, failed, body bytes, and how
// many were playlists .m3u8 / .mpd vs media), then one entry per request with
// times in ms relative to t0_us. Bytes are what was read so far.
std::string net_report_json(const std::vector<NetRequest>& reqs, std::int64_t t0_us, std::int64_t t1_us);

// Records the network traffic FFmpeg performs, for the network inspector.
//
// Two sources feed it:
//  * io_open / io_close2 hooks on the AVFormatContexts we create
//    (hook_format_context) — open/close time, open errors, bytes read
//    (AVIOContext::bytes_read), initiator tagging.
//  * FFmpeg's own av_log output from http.c (captured via av_log_set_callback,
//    independent of the av_log level): "request: …" marks every HTTP request —
//    including keep-alive requests that HLS issues on an already-open
//    AVIOContext without another io_open — and "header='…'" lines give the
//    status line, response headers and the time of the first response byte.
// Requests issued by contexts we did not hook (e.g. VMAF's) still appear,
// from the log alone, with initiator Other.
//
// Response bodies: on the handles the hls demuxer opens (playlists,
// segments, init sections) and on manifest fetches, FFmpeg's reads are
// tapped as they pass through the AVIOContext buffer (its update_checksum
// hook, see tap_body) — the bytes the player actually got, after HTTP
// dechunking / decompression and AES-128 decryption, with no extra request
// and keep-alive untouched. Playlists and subtitle files are kept whole (up
// to kMaxTextBody), anything else its first kHeadBytes: enough to read a
// segment's start timestamps — kTsHeadBytes of MPEG-TS, whose first audio
// PES can follow a whole keyframe. Decryption keys are never kept.
//
// Thread-safe: FFmpeg threads (demux, thumbnails, scrub, …) write; the render
// thread reads via copy_changed_since(). Bounded: the newest kMaxRequests are kept.
class NetworkLogger {
public:
    NetworkLogger();
    ~NetworkLogger();
    NetworkLogger(const NetworkLogger&) = delete;
    NetworkLogger& operator=(const NetworkLogger&) = delete;

    // Install as the global av_log callback (the default callback still prints).
    void install();
    void uninstall();
    static NetworkLogger* instance() { return instance_.load(std::memory_order_acquire); }

    // ── FFmpeg integration (no-ops while no logger is installed) ──────────────
    // Route ctx's io_open / io_close2 through the logger. Call right after
    // avformat_alloc_context(), before avformat_open_input(). Uses ctx->opaque.
    static void hook_format_context(AVFormatContext* ctx, NetInitiator who);
    // Manual tracking for plain avio_open() users (no AVFormatContext).
    // track_open_end also taps pb's body (read it with avio_read, no probing).
    // range_start / range_end (inclusive, -1 none): the byte range a
    // non-HTTP open reads (HTTP takes it from the Range header).
    static void track_open_begin(NetInitiator who, const char* url, std::int64_t range_start = -1,
                                 std::int64_t range_end = -1);
    static void track_open_end(int ret, AVIOContext* pb);
    static void track_close(AVIOContext* pb);

    // ── Event API (public so unit tests can drive it without FFmpeg I/O) ──────
    // An open of `url` begins on this thread.
    void ev_open_begin(NetInitiator who, const std::string& url, std::int64_t range_start = -1,
                       std::int64_t range_end = -1);
    // The open finished. pb: the I/O handle (nullptr on failure); log_ctx: the
    // URLContext http.c logs with; byte_counter: running body-byte count owned
    // by pb (read only on the opening thread / while pb is alive).
    void ev_open_end(int ret, const void* pb, const void* log_ctx,
                     const std::int64_t* byte_counter, std::int64_t size_hint,
                     const std::string& mime_hint, const std::string& err_text);
    // pb is about to be closed.
    void ev_close(const void* pb);
    // One formatted av_log line from object `log_ctx`.
    void ev_log(const void* log_ctx, const char* line);
    // `n` response bytes of the request now on handle `pb`.
    void ev_body(const void* pb, const std::uint8_t* data, std::size_t n);

    // Start keeping the response bodies read through `pb` (an open handle
    // ev_open_end registered), on the thread that reads it, before its first
    // read. Not for a handle FFmpeg may still probe (avformat_open_input's
    // own input): probing swaps its buffer. No-op if pb's checksum hook is
    // in use. Requests switch at each "request:" line; the tail still in the
    // buffer is credited at the switch and at close.
    void tap_body(AVIOContext* pb);

    // ── Consumer API (render thread) ──────────────────────────────────────────
    std::uint64_t change_seq() const { return seq_.load(std::memory_order_acquire); }
    std::uint64_t generation() const { return generation_.load(std::memory_order_acquire); }
    // Copy every request modified after `since_seq` (oldest first) and report
    // the oldest id still retained (older ids were evicted or cleared).
    void copy_changed_since(std::uint64_t since_seq, std::vector<NetRequest>& out,
                            std::uint64_t& oldest_id) const;
    std::size_t size() const;
    void clear();

    // Since the logger started, never decreasing (eviction and clear()
    // leave them): body bytes received over all requests, and requests whose
    // body arrived whole. An open waiting on a server that has stopped
    // sending sees neither move (poll() refreshes the bytes of requests in
    // flight).
    struct Progress { std::int64_t bytes = 0; std::uint64_t completed = 0; };
    Progress progress() const;

    // The kept response body of request `id` (see NetRequest::body_bytes);
    // false if none is kept.
    bool copy_body(std::uint64_t id, std::string& out) const;
    static constexpr std::size_t kHeadBytes   = 32 * 1024;           // non-text bodies: the first bytes
    static constexpr std::size_t kTsHeadBytes = 256 * 1024;          // ... of MPEG-TS
    // The first bytes kept of a media resource at `url` (kHeadBytes / kTsHeadBytes).
    static std::size_t head_bytes(const std::string& url, const std::string& mime = {});
    static constexpr std::size_t kMaxTextBody = 8 * 1024 * 1024;     // playlists, subtitles: whole up to this
    static constexpr std::size_t kBodyBudget  = 96 * 1024 * 1024;    // all bodies; the oldest are dropped

    // Render thread, once per frame: refresh the body byte count of requests
    // in flight, finish those whose Content-Length has arrived, and mark the
    // ones that stopped receiving for kIdleUs as partial.
    // `now` (µs, now_us() clock) is for tests; -1 = the current time.
    void poll(std::int64_t now = -1);
    static constexpr std::int64_t kIdleUs = 1'500'000;
    void set_recording(bool on) { recording_.store(on); }
    bool recording() const { return recording_.load(); }

    // Steady clock, µs since process start. wall_ms() maps such a time to
    // Unix epoch ms for display.
    static std::int64_t now_us();
    static std::int64_t wall_ms(std::int64_t t_us);

    static constexpr std::size_t kMaxRequests = 4000;

    // ── Legacy log-line API (kept for the classification unit tests) ──────────
    struct Entry {
        std::string time;   // "HH:MM:SS.mmm"
        std::string type;   // "manifest" | "variant" | "init" | "segment" | "other"
        std::string url;    // truncated to last 60 chars if longer
        float r, g, b;      // colour for display
    };
    std::vector<Entry> get_recent(int n) const;
    int total_count() const;
    // Classify an HTTP URL into a type string and assign a display colour.
    static std::string classify(const std::string& url, float& r, float& g, float& b);
    // Parse an "Opening '<url>' for reading" line into a legacy Entry.
    void ingest_line(const char* line);

private:
    static void av_log_cb(void* avcl, int level, const char* fmt, va_list vl);

    struct Conn {                      // an open I/O handle we saw io_open for
        const void*         pb       = nullptr;
        const void*         log_ctx  = nullptr;
        const std::int64_t* counter  = nullptr;
        std::int64_t        snap     = 0;     // counter value when cur_req started
        std::uint64_t       cur_req  = 0;
        std::uint32_t       conn_id  = 0;
        NetInitiator        who      = NetInitiator::Other;
        AVIOContext*        io       = nullptr;  // body tapped (tap_body); only touched on its thread
        std::int64_t        reported = 0;        // io->bytes_read already credited to a body
    };
    struct Body {
        std::string data;
    };

    // All private helpers expect mtx_ held.
    NetRequest*   find_locked(std::uint64_t id);
    // A tapped local-file request: [data, data + n) of c.io's buffer clipped
    // to its byte range — or, without one, where it starts becomes it.
    void          clip_file_body_locked(const Conn& c, const std::uint8_t*& data, std::size_t& n);
    NetRequest&   push_locked(NetRequest r);
    void          touch_locked(NetRequest& r);
    void          finish_locked(NetRequest& r, std::int64_t now, std::int64_t bytes);
    Conn*         conn_by_log_ctx_locked(const void* log_ctx);
    void          on_request_line_locked(const void* log_ctx, const std::string& text, std::int64_t now);
    void          on_header_line_locked(const void* log_ctx, const std::string& line, std::int64_t now);
    void          on_ctx_closed_locked(const void* log_ctx, std::int64_t now);
    void          append_body_locked(std::uint64_t id, const std::uint8_t* data, std::size_t n);
    void          flush_body_locked(Conn& c);
    void          drop_bodies_locked();

    std::atomic<bool> installed_{ false };
    std::atomic<bool> recording_{ true };

    mutable std::mutex       mtx_;
    std::deque<NetRequest>   reqs_;
    std::unordered_map<const void*, Conn>          conns_;     // by pb
    std::unordered_map<const void*, std::uint64_t> ctx_req_;   // log ctx → in-flight request
    std::unordered_map<std::uint64_t, Body>        bodies_;    // request id → kept body
    std::size_t              body_total_ = 0;
    Progress                 progress_;           // under mtx_ (progress())
    std::uint64_t            next_id_   = 1;
    std::uint32_t            next_conn_ = 1;
    std::atomic<std::uint64_t> seq_{ 0 };
    std::atomic<std::uint64_t> generation_{ 0 };

    std::deque<Entry>        entries_;   // legacy
    static constexpr int kMaxEntries = 200;

    static std::atomic<NetworkLogger*> instance_;
};
