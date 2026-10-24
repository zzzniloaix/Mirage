#include "VMAFLadder.h"
#include "FileHash.h"
#include "HlsMaster.h"
#include "VMAFReport.h"

#include <algorithm>
#include <charconv>
#include <format>
#include <limits>
#include <system_error>
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
        p.error = std::format("the only video playlist of the master is the top rung (entries {}): "
                              "there is nothing to score against it",
                              entry_list(p.reference_entries));
    return p;
}

Fetched read_master(const std::string& url)
{
    Fetched f;
    f.location = url;
    AVIOContext* ctx = nullptr;
    if (const int r = avio_open2(&ctx, url.c_str(), AVIO_FLAG_READ, nullptr, nullptr); r < 0) {
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

Result score(const std::string& ladder, const std::string& mezzanine, const VMAFOptions& opts,
             const OnPair& on_pair)
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
    for (std::size_t k = 0; k < n; ++k) {
        const Pair& pair = out.plan.pairs[k];
        out.results.push_back(VMAFAnalyzer::analyze(out.plan.reference_url, pair.url, opts));
        const VMAFResult& r = out.results.back();
        if (const std::string why = vmafreport::incomplete(r); !why.empty()) {
            out.error = std::format("pair {} of {}, {} against {}: {}", k + 1, n, pair.url,
                                    out.plan.reference_url, why);
            return out;
        }
        if (r.settings != out.results.front().settings) {
            out.error = std::format("pair {} of {} was scored with other settings than pair 1 "
                                    "(model {} {}, scaler {}): did the model file change?",
                                    k + 1, n, r.settings.model, r.settings.model_sha256,
                                    r.settings.scaler);
            return out;
        }
        if (on_pair) on_pair(k, n, r);
    }
    return out;
}

} // namespace vmafladder
