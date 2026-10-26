#include "VMAFLadder.h"
#include "FileHash.h"
#include "HlsMaster.h"
#include "VMAFNet.h"
#include "VMAFProcess.h"
#include "VMAFReport.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <format>
#include <limits>
#include <mutex>
#include <system_error>
#include <thread>
#include <utility>

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
}

namespace vmafladder {

namespace {

constexpr std::string_view kWhiteSpace = " \t\r\n\v\f";
constexpr std::string_view kHeader     = "#EXTM3U";
constexpr std::string_view kStreamInf  = "#EXT-X-STREAM-INF:";

std::string_view trimmed(std::string_view s)
{
    const auto first = s.find_first_not_of(kWhiteSpace);
    if (first == std::string_view::npos) return {};
    return s.substr(first, s.find_last_not_of(kWhiteSpace) - first + 1);
}

bool is_name_char(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
}

// The value of the last attribute `name` in `attrs`, if any.
std::optional<std::string> last(const std::vector<std::pair<std::string, std::string>>& attrs,
                                std::string_view name)
{
    std::optional<std::string> out;
    for (const auto& [k, v] : attrs)
        if (k == name) out = v;
    return out;
}

// Whether `uri` starts with a scheme, RFC 3986's ALPHA *( ALPHA / DIGIT /
// "+" / "-" / "." ) followed by ':'.
bool has_scheme(std::string_view uri)
{
    const auto alpha = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
    if (uri.empty() || !alpha(uri[0])) return false;
    for (std::size_t i = 1; i < uri.size(); ++i) {
        const char c = uri[i];
        if (c == ':') return true;
        if (!alpha(c) && !(c >= '0' && c <= '9') && c != '+' && c != '-' && c != '.') return false;
    }
    return false;
}

bool is_http(std::string_view s) { return s.starts_with("http://") || s.starts_with("https://"); }

std::string av_error_text(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof buf);
    return buf;
}

std::string entry_list(const std::vector<std::size_t>& entries)
{
    std::string out;
    for (std::size_t i = 0; i < entries.size(); ++i)
        out += std::format("{}{}", i == 0 ? "" : ", ", entries[i]);
    return out;
}

} // namespace

std::vector<std::pair<std::string, std::string>> attributes(std::string_view list)
{
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t pos = 0;
    while (pos < list.size()) {
        std::size_t name_end = pos;
        while (name_end < list.size() && is_name_char(list[name_end])) ++name_end;
        if (name_end == pos || name_end == list.size() || list[name_end] != '=') {
            ++pos;   // no match here: try from the next character, as a regex search does
            continue;
        }
        const std::size_t v = name_end + 1;
        std::size_t end = v;
        std::string value;
        if (v < list.size() && list[v] == '"' && list.find('"', v + 1) != std::string_view::npos) {
            end   = list.find('"', v + 1);
            value = std::string(list.substr(v + 1, end - v - 1));
            ++end;
        } else {
            // Unquoted, or a quote that doesn't close: up to the next '"' or ','.
            while (end < list.size() && list[end] != '"' && list[end] != ',') ++end;
            value = std::string(list.substr(v, end - v));
        }
        out.emplace_back(std::string(list.substr(pos, name_end - pos)), std::move(value));
        pos = end;
    }
    return out;
}

bool has_video(const std::optional<std::string>& resolution,
               const std::optional<std::string>& codecs)
{
    if (resolution) return true;
    if (!codecs) return false;
    std::string_view rest = *codecs;
    while (true) {
        const std::size_t comma = rest.find(',');
        const std::string_view codec = trimmed(rest.substr(0, comma));
        const std::string_view name  = codec.substr(0, codec.find('.'));
        if (std::ranges::find(kVideoCodecs, name) != kVideoCodecs.end()) return true;
        if (comma == std::string_view::npos) return false;
        rest.remove_prefix(comma + 1);
    }
}

std::string resolve(std::string_view location, std::string_view uri)
{
    if (is_http(location)) return hls::absolute_uri(std::string(location), std::string(uri));
    if (uri.empty()) return {};
    if (has_scheme(uri) || uri.starts_with('/')) return std::string(uri);
    const std::size_t slash = location.rfind('/');
    if (slash == std::string_view::npos) return std::string(uri);
    return std::format("{}{}", location.substr(0, slash + 1), uri);
}

Master parse_master(std::string_view text, std::string_view location)
{
    Master m;
    const auto fail = [&](std::string why) {
        m.entries.clear();
        m.error = std::move(why);
        return m;
    };

    std::optional<Entry> pending;   // an EXT-X-STREAM-INF waiting for its URI line
    std::size_t line_no = 0;
    while (!text.empty()) {
        const std::size_t eol = text.find('\n');
        const std::string_view line = trimmed(text.substr(0, eol));
        text.remove_prefix(eol == std::string_view::npos ? text.size() : eol + 1);
        ++line_no;
        if (line_no == 1) {
            // FFmpeg's hls demuxer reads nothing else as a playlist.
            if (line != kHeader)
                return fail("it doesn't start with #EXTM3U, so it isn't an HLS playlist");
            continue;
        }
        if (line.empty()) continue;
        if (line.starts_with(kStreamInf)) {
            if (pending)
                return fail(std::format("line {}: an EXT-X-STREAM-INF before the URI line of the "
                                        "one on line {}", line_no, pending->line));
            const auto attrs = attributes(line.substr(kStreamInf.size()));
            Entry e;
            e.index = m.entries.size();
            e.line  = line_no;
            const std::optional<std::string> bw = last(attrs, "BANDWIDTH");
            if (!bw)
                return fail(std::format("line {}: the EXT-X-STREAM-INF has no BANDWIDTH", line_no));
            // RFC 8216's decimal-integer: digits only.
            const char* bw_end = bw->data() + bw->size();
            const auto [end, ec] = std::from_chars(bw->data(), bw_end, e.bandwidth);
            if (bw->empty() || bw->front() == '-' || ec != std::errc() || end != bw_end)
                return fail(std::format("line {}: BANDWIDTH '{}' is not a decimal integer up to {}",
                                        line_no, *bw, std::numeric_limits<int64_t>::max()));
            e.average_bandwidth = last(attrs, "AVERAGE-BANDWIDTH");
            e.resolution        = last(attrs, "RESOLUTION");
            e.codecs            = last(attrs, "CODECS");
            e.frame_rate        = last(attrs, "FRAME-RATE");
            e.video             = has_video(e.resolution, e.codecs);
            pending = std::move(e);
            continue;
        }
        if (line.starts_with('#')) continue;   // a comment or another tag
        if (!pending)
            return fail(std::format("line {}: a URI without an EXT-X-STREAM-INF before it: a "
                                    "media playlist's segment? --ladder takes a master playlist",
                                    line_no));
        pending->uri = std::string(line);
        pending->url = resolve(location, line);
        if (pending->url.empty())
            return fail(std::format("line {}: the URI '{}' is not one mirage_vmaf resolves "
                                    "against {}", line_no, line, location));
        m.entries.push_back(std::move(*pending));
        pending.reset();
    }
    if (line_no == 0) return fail("it is empty, so it isn't an HLS playlist");
    if (pending)
        return fail(std::format("the EXT-X-STREAM-INF on line {} has no URI line after it",
                                pending->line));
    if (m.entries.empty())
        return fail("it has no EXT-X-STREAM-INF entry, so it isn't a master playlist");
    return m;
}

Plan plan(const Master& master, const std::string& mezzanine)
{
    Plan p;
    p.reference_mode = std::string(mezzanine.empty() ? kTopRung : kMezzanine);
    if (!master.error.empty()) {
        p.error = master.error;
        return p;
    }

    const Entry* top = nullptr;   // the first video entry of the highest BANDWIDTH
    for (const Entry& e : master.entries) {
        if (!e.video) {
            p.skipped.push_back({ e.index, std::string(kNoVideo) });
            continue;
        }
        if (!top || e.bandwidth > top->bandwidth) top = &e;
    }
    if (!top) {
        p.error = "no entry of the master playlist has video";
        return p;
    }

    if (mezzanine.empty()) {
        p.reference_url = top->url;
        for (const Entry& e : master.entries) {
            if (!e.video || e.bandwidth != top->bandwidth || e.url == top->url) continue;
            p.warnings.push_back(std::format(
                "entries {} and {} both have the highest BANDWIDTH, {}, and name different "
                "playlists; the top rung is entry {}, the first of them in the master",
                top->index, e.index, top->bandwidth, top->index));
        }
    } else {
        p.reference_url = mezzanine;
    }

    for (const Entry& e : master.entries) {
        if (!e.video) continue;
        if (mezzanine.empty() && e.url == p.reference_url) {
            p.reference_entries.push_back(e.index);
            continue;
        }
        const auto it = std::ranges::find(p.pairs, e.url, &Pair::url);
        if (it != p.pairs.end()) it->entries.push_back(e.index);
        else p.pairs.push_back({ e.url, { e.index } });
    }
    if (p.pairs.empty())
        p.error = std::format("the only video playlist of the master is the top rung ({} {}): "
                              "there is nothing to score against it",
                              p.reference_entries.size() == 1 ? "entry" : "entries",
                              entry_list(p.reference_entries));
    return p;
}

Fetched read_master(const std::string& url)
{
    Fetched f;
    f.location = url;
    AVIOContext* ctx = nullptr;
    // A network read waits at most 5 s, as the analyzer's (D-036).
    AVDictionary* opts = nullptr;
    vmafnet::set_io_limits(&opts, url);
    const int r = avio_open2(&ctx, url.c_str(), AVIO_FLAG_READ, nullptr, &opts);
    av_dict_free(&opts);
    if (r < 0) {
        f.error = av_error_text(r);
        return f;
    }
    // HTTP: the URL the playlist came from after redirects, which FFmpeg's
    // hls demuxer resolves the playlist's URIs against.
    uint8_t* loc = nullptr;
    if (av_opt_get(ctx, "location", AV_OPT_SEARCH_CHILDREN, &loc) >= 0 && loc) {
        if (*loc) f.location = reinterpret_cast<const char*>(loc);
        av_free(loc);
    }
    uint8_t buf[4096];
    int n = 0;
    while ((n = avio_read(ctx, buf, sizeof buf)) > 0) {
        f.text.append(reinterpret_cast<const char*>(buf), static_cast<std::size_t>(n));
        const std::size_t head = std::min(f.text.size(), kHeader.size());
        if (std::string_view(f.text).substr(0, head) != kHeader.substr(0, head)) {
            f.error = "it doesn't start with #EXTM3U, so it isn't an HLS playlist";
            break;
        }
    }
    // A text cut short is a failed read, not a shorter playlist.
    const int err = ctx->error < 0 ? ctx->error : (n < 0 && n != AVERROR_EOF ? n : 0);
    if (f.error.empty() && err < 0) f.error = av_error_text(err);
    avio_closep(&ctx);
    if (!f.error.empty()) f.text.clear();
    else f.sha256 = filehash::sha256_hex(f.text);
    return f;
}

std::string_view status_name(PairStatus s)
{
    switch (s) {
    case PairStatus::NotStarted: return "not started";
    case PairStatus::Complete:   return "complete";
    case PairStatus::Failed:     return "failed";
    case PairStatus::Stopped:    return "stopped";
    }
    return "unknown";
}

Result score(const std::string& ladder, const std::string& mezzanine, const VMAFOptions& opts,
             const Pool& pool, std::stop_token cancel, const OnPair& on_pair)
{
    Result out;
    out.ladder  = ladder;
    out.fetched = read_master(ladder);
    if (!out.fetched.error.empty()) {
        out.error = std::format("cannot read the master playlist {}: {}", ladder,
                                out.fetched.error);
        return out;
    }
    out.master = parse_master(out.fetched.text, out.fetched.location);
    out.plan   = plan(out.master, mezzanine);
    if (!out.plan.error.empty()) {
        out.error = std::format("the master playlist {}: {}", ladder, out.plan.error);
        return out;
    }

    const std::size_t n = out.plan.pairs.size();
    out.pairs.resize(n);
    out.jobs         = pool.jobs > 0 ? pool.jobs : vmafproc::default_jobs();
    out.memory_limit = pool.memory_limit;
    const std::function<std::uint64_t()> resident =
        pool.resident ? pool.resident : std::function<std::uint64_t()>(vmafproc::resident_bytes);

    // Stops the pairs still running, on a cancel or at the first pair that fails.
    std::stop_source stop;
    std::stop_callback on_cancel(cancel, [&] { stop.request_stop(); });

    std::mutex              mtx;           // guards everything below and out.pairs' states
    std::condition_variable cv;
    std::size_t             next    = 0;   // the next pair to start, in the plan's order
    unsigned                running = 0;
    std::vector<bool>       active(n, false);
    bool                    failed  = false;
    std::optional<std::size_t> first_complete;   // the first pair to complete, in time
    std::mutex              callback_mtx;  // one on_pair call at a time
    const auto t0    = std::chrono::steady_clock::now();
    const auto now_s = [&] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    };
    const std::uint64_t baseline = out.memory_limit > 0 ? resident() : 0;
    bool alone_warned = false;

    // With the lock held: a pair running alone over the memory limit gets a
    // warning, once (D-045).
    const auto check_alone = [&] {
        if (out.memory_limit == 0 || running != 1 || alone_warned) return;
        const std::uint64_t now = resident();
        if (now <= out.memory_limit) return;
        const auto it = std::find(active.begin(), active.end(), true);
        out.pool_warnings.push_back(std::format(
            "pair {} of {} took the process over the memory limit on its own: {} bytes of "
            "resident memory, the limit {}",
            static_cast<std::size_t>(it - active.begin()) + 1, n, now, out.memory_limit));
        alone_warned = true;
    };

    // With the lock held: whether one more pair may start (D-045). A running
    // pair is taken to use the memory gained since the run began, shared out
    // among the pairs running; with none running, a pair always starts.
    const auto admit = [&] {
        if (out.memory_limit == 0 || running == 0) return true;
        const std::uint64_t now = resident();
        if (now == 0) return true;   // unknown: the limit can't hold a pair back
        const std::uint64_t per_pair = (now > baseline ? now - baseline : 0) / running;
        return now + per_pair <= out.memory_limit;
    };

    // With the lock held: what becomes of pair k once its analysis returned.
    const auto settle = [&](std::size_t k) {
        PairRun& run = out.pairs[k];
        const Pair& pair = out.plan.pairs[k];
        if (const std::string why = vmafreport::incomplete(run.result); !why.empty()) {
            if (stop.stop_requested()) {
                run.status = PairStatus::Stopped;
                return;
            }
            run.status = PairStatus::Failed;
            run.why    = why;
            if (!failed)
                out.error = std::format("pair {} of {}, {} against {}: {}", k + 1, n, pair.url,
                                        out.plan.reference_url, why);
            failed = true;
            stop.request_stop();
            return;
        }
        if (first_complete && run.result.settings != out.pairs[*first_complete].result.settings) {
            const VMAFSettings& first = out.pairs[*first_complete].result.settings;
            run.status = PairStatus::Failed;
            run.why    = std::format("scored with other settings than pair {}", *first_complete + 1);
            if (!failed)
                out.error = std::format("pair {} of {} was scored with other settings than pair {}, "
                                        "model {} with the SHA-256 {} against {}: did the model "
                                        "file change during the run?",
                                        k + 1, n, *first_complete + 1, run.result.settings.model,
                                        run.result.settings.model_sha256, first.model_sha256);
            failed = true;
            stop.request_stop();
            return;
        }
        run.status = PairStatus::Complete;
        if (!first_complete) first_complete = k;
    };

    const auto worker = [&] {
        std::unique_lock lk(mtx);
        for (;;) {
            while (!(stop.stop_requested() || next >= n || admit()))
                cv.wait_for(lk, std::chrono::milliseconds(20));
            if (stop.stop_requested() || next >= n) return;
            const std::size_t k = next++;
            ++running;
            active[k] = true;
            out.max_running = std::max(out.max_running, running);
            out.pairs[k].started = now_s();
            lk.unlock();

            VMAFResult r = VMAFAnalyzer::analyze(out.plan.reference_url, out.plan.pairs[k].url,
                                                 opts, stop.get_token());

            lk.lock();
            check_alone();   // before the pair leaves: it may have run alone
            --running;
            active[k] = false;
            out.pairs[k].ended  = now_s();
            out.pairs[k].result = std::move(r);
            settle(k);
            cv.notify_all();
            if (out.pairs[k].status == PairStatus::Complete && on_pair) {
                lk.unlock();   // pair k's state no longer changes
                {
                    std::lock_guard g(callback_mtx);
                    on_pair(k, n, out.pairs[k].result);
                }
                lk.lock();
            }
        }
    };

    // With a memory limit, a pair running alone is also checked while it runs.
    std::jthread sampler;
    if (out.memory_limit > 0) {
        sampler = std::jthread([&](std::stop_token st) {
            while (!st.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                std::lock_guard g(mtx);
                check_alone();
                if (alone_warned) return;
            }
        });
    }

    {
        std::vector<std::jthread> workers;
        const std::size_t count = std::min<std::size_t>(out.jobs, n);
        for (std::size_t i = 0; i < count; ++i) workers.emplace_back(worker);
    }   // joined
    if (sampler.joinable()) {
        sampler.request_stop();
        sampler.join();
    }
    out.peak_resident = vmafproc::peak_resident_bytes();

    const std::size_t complete = static_cast<std::size_t>(std::count_if(
        out.pairs.begin(), out.pairs.end(),
        [](const PairRun& p) { return p.status == PairStatus::Complete; }));
    if (failed) {
        out.status = kFailed;
    } else if (complete < n) {
        out.status = kCancelled;
        out.error  = std::format("the run was cancelled: {} of {} pairs complete", complete, n);
    } else {
        out.status = kComplete;
    }
    return out;
}

} // namespace vmafladder
