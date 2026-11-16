// The timeline panel's matching, playhead and seek decision (eval/phase3-survey/design.md §10):
// written by WP12, the path among a playlist's equal bytes by WP12b (D-121 (d)).

#include "TimelineMatch.h"

#include "FileHash.h"
#include "PlaylistEvents.h"
#include "TimelineDoc.h"
#include "VMAFAlign.h"

extern "C" {
#include <libavutil/mathematics.h>
}

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <format>
#include <limits>

namespace timeline {

namespace {

// ── The match ────────────────────────────────────────────────────────────────

bool is_url(std::string_view s) { return s.find("://") != std::string_view::npos; }

// Two addresses of one file: the same text, or for two local paths the same
// path once lexically normalized ("a/./b", "a/c/../b"). Nothing is looked up
// on disk, so the match stays a pure function: a relative path and an
// absolute one of the same file don't match.
bool same_address(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty()) return false;
    if (a == b) return true;
    if (is_url(a) || is_url(b)) return false;
    return std::filesystem::path(a).lexically_normal() == std::filesystem::path(b).lexically_normal();
}

// The path components of an address, to compare where two addresses end
// (D-121 (d)): of a URL its path alone, after its scheme and authority and
// before its query or fragment (RFC 3986 §3), of a local path all of it;
// lexically normalized, as same_address() compares local paths, empty
// components left out.
std::vector<std::string> path_components(std::string_view address)
{
    std::string_view p = address;
    if (const std::size_t scheme = p.find("://"); scheme != std::string_view::npos) {
        const std::size_t start = p.find_first_of("/?#", scheme + 3);
        p = start == std::string_view::npos || p[start] != '/' ? std::string_view() : p.substr(start);
        p = p.substr(0, p.find_first_of("?#"));
    }
    const std::string normal = std::filesystem::path(p).lexically_normal().string();
    std::vector<std::string> out;
    for (std::size_t at = 0; at < normal.size();) {
        const std::size_t end = std::min(normal.find('/', at), normal.size());
        if (end > at) out.push_back(normal.substr(at, end - at));
        at = end + 1;
    }
    return out;
}

// How many trailing path components two addresses share: 1 where only their
// file names are the same; 0 for an empty address.
std::size_t shared_tail(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty()) return 0;
    const std::vector<std::string> x = path_components(a), y = path_components(b);
    std::size_t n = 0;
    while (n < x.size() && n < y.size() && x[x.size() - 1 - n] == y[y.size() - 1 - n]) ++n;
    return n;
}

// The last `n` path components of `address`, joined by '/'.
std::string tail_text(const std::string& address, std::size_t n)
{
    const std::vector<std::string> c = path_components(address);
    std::string out;
    for (std::size_t k = c.size() - std::min(n, c.size()); k < c.size(); ++k)
        out += (out.empty() ? "" : "/") + c[k];
    return out;
}

// What the player's own parser makes of the EXT-X-STREAM-INF URI `uri` in a
// master read from `base`: its resolution, from a master of that one entry,
// so that it is the player's whatever ManifestParser's rule becomes.
std::string player_url(const std::string& uri, const std::string& base)
{
    ManifestParser p;
    if (!p.parse_hls_text("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\n" + uri + "\n", base) ||
        p.variants().size() != 1)
        return {};
    return p.variants().front().url;
}

// Why the player's entries don't correspond to the report's in order, or
// empty: their numbers, then each entry's BANDWIDTH and URI, the player's
// URL being the report's URI as the player resolves it.
std::string order_refusal(const OpenStream& open, const LadderScores& scores)
{
    const std::size_t n = open.variants.size(), m = scores.entries.size();
    if (n != m)
        return std::format("the player lists {} entries and the report {}{}", n, m,
                           n < m ? ": the player's parser drops an entry whose URI isn't on the line "
                                   "after its EXT-X-STREAM-INF"
                                 : "");
    for (std::size_t i = 0; i < n; ++i) {
        const VariantStream& v = open.variants[i];
        const MasterEntry&   e = scores.entries[i];
        if (v.bandwidth != e.bandwidth)
            return std::format("entry {} has BANDWIDTH {} in the player and {} in the report", i,
                               v.bandwidth, e.bandwidth);
        const std::string url = player_url(e.uri, open.final_url);
        if (v.url != url)
            return std::format("the player's entry {} plays {}, where the report's URI {} resolves "
                               "to {}", i, v.url, e.uri, url);
    }
    return {};
}

// ── A variant's clock ────────────────────────────────────────────────────────

const Value* member(const Payload& p, std::string_view name)
{
    for (const auto& [n, v] : p)
        if (n == name) return &v;
    return nullptr;
}

double number_member(const Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    const std::optional<double> d = v ? number_of(*v) : std::nullopt;
    return d ? *d : 0.0;
}

// The report sequence of input frame `index` (D-057): the last whose first
// frame is at or before it.
int sequence_of(const std::vector<Sequence>& sequences, std::int64_t index)
{
    int s = 0;
    for (std::size_t k = 0; k < sequences.size(); ++k)
        if (sequences[k].first_index <= index) s = static_cast<int>(k);
    return s;
}

// The frames of sequence `sequence`, [first, last) in the clock's frames,
// which come in their sequences' order.
std::pair<std::size_t, std::size_t> sequence_range(const VariantClock& c, int sequence)
{
    const auto lo = std::partition_point(c.frames.begin(), c.frames.end(),
                                         [&](const ClockFrame& f) { return f.sequence < sequence; });
    const auto hi = std::partition_point(lo, c.frames.end(),
                                         [&](const ClockFrame& f) { return f.sequence <= sequence; });
    return { static_cast<std::size_t>(lo - c.frames.begin()),
             static_cast<std::size_t>(hi - c.frames.begin()) };
}

// The frame of [first, last) whose PTS is nearest `t`, the earlier of two as
// near; the range is a sequence's, whose PTS rise.
std::size_t nearest_in(const VariantClock& c, std::size_t first, std::size_t last, std::int64_t t)
{
    const auto b = c.frames.begin() + static_cast<std::ptrdiff_t>(first);
    const auto e = c.frames.begin() + static_cast<std::ptrdiff_t>(last);
    const auto at = std::partition_point(b, e, [&](const ClockFrame& f) { return f.pts < t; });
    std::size_t i = static_cast<std::size_t>(at - c.frames.begin());
    if (i == last) return last - 1;
    if (i > first) {
        const auto before = static_cast<double>(t) - static_cast<double>(c.frames[i - 1].pts);
        const auto after  = static_cast<double>(c.frames[i].pts) - static_cast<double>(t);
        if (before <= after) return i - 1;
    }
    return i;
}

double distance_ticks(const ClockFrame& f, std::int64_t t)
{
    return std::fabs(static_cast<double>(f.pts) - static_cast<double>(t));
}

// ── The seek ─────────────────────────────────────────────────────────────────

constexpr std::int64_t kUs = 1000000;   // AV_TIME_BASE, the unit the hls demuxer seeks in

// Seconds in microseconds, truncated as the Demuxer passes a target to
// av_seek_frame() (src/Demuxer.cpp) and as hls.c stores an EXTINF.
std::int64_t us_truncated(double s) { return static_cast<std::int64_t>(s * static_cast<double>(kUs)); }
// Seconds in microseconds, to the nearest: a start time or a PTS.
std::int64_t us(double s) { return std::llround(s * static_cast<double>(kUs)); }

// Where the Demuxer seeks for `target` (src/Demuxer.cpp): into [start,
// start + duration - 0.5]; a target before the start goes to the start with
// no frame awaited, one past the end into the last segment, its frame still
// awaited.
struct Sought {
    double seconds = 0;
    bool   await = true;
    bool   clamped = false;
};
Sought sought_of(const SeekClock& seek, double target)
{
    if (target < seek.start) return { .seconds = seek.start, .await = false, .clamped = true };
    if (seek.duration > 0.0 && target > seek.start + seek.duration - 0.5)
        return { .seconds = std::max(seek.start, seek.start + seek.duration - 0.5), .clamped = true };
    return { .seconds = target };
}

// The target as hls_read_seek() takes it, in microseconds: the Demuxer's
// truncated microseconds, which av_seek_frame() puts in the default stream's
// time base to the nearest tick and the hls demuxer back in microseconds,
// rounded down, by the time base's denominator.
std::int64_t seek_us(double seconds, AVRational time_base)
{
    const std::int64_t t = us_truncated(seconds);
    if (time_base.num <= 0 || time_base.den <= 0) return t;
    const std::int64_t ticks = av_rescale(t, time_base.den, kUs * time_base.num);
    return av_rescale_rnd(ticks, kUs, time_base.den, AV_ROUND_DOWN);
}

// Where each segment starts on the hls demuxer's clock, in microseconds from
// its first packet's DTS: the EXTINF before it added up as hls.c adds them,
// each truncated to a microsecond; from the playlist time where a segment
// the clock lacks breaks the run of media sequence numbers.
std::vector<std::int64_t> hls_starts(const VariantClock& c)
{
    std::vector<std::int64_t> out;
    for (std::size_t j = 0; j < c.segments.size(); ++j) {
        const ClockSegment& s = c.segments[j];
        if (j > 0 && s.media_sequence == c.segments[j - 1].media_sequence + 1)
            out.push_back(out.back() + us_truncated(c.segments[j - 1].extinf));
        else
            out.push_back(us(s.playlist_time));
    }
    return out;
}

// The segment whose frames hold frame `i`, or the last that starts before it.
std::optional<std::size_t> segment_of(const VariantClock& c, std::size_t i)
{
    std::optional<std::size_t> out;
    for (std::size_t j = 0; j < c.segments.size(); ++j)
        if (c.segments[j].first <= i && c.segments[j].first < c.segments[j].last) out = j;
    return out;
}

// simulate_seek() with the lead in microseconds.
SeekLanding simulate_us(const VariantClock& c, const SeekClock& seek, double target, std::int64_t lead_us)
{
    SeekLanding  out;
    const Sought sought = sought_of(seek, target);
    out.clamped = sought.clamped;
    if (c.segments.empty()) return out;
    const std::int64_t start_us = us(seek.start);
    // hls_read_seek(): the target from the first packet's DTS, `lead_us`
    // before the start, against the segments' EXTINF added up; the first
    // segment that ends after it.
    const std::int64_t first_dts = start_us - std::max<std::int64_t>(lead_us, 0);
    const std::int64_t pos       = seek_us(sought.seconds, c.time_base) - first_dts;
    if (pos < 0) {
        out.kind = SeekLanding::Kind::Failed;
        return out;
    }
    const std::vector<std::int64_t> starts = hls_starts(c);
    std::optional<std::size_t> k;
    for (std::size_t j = 0; j < c.segments.size() && !k; ++j)
        if (pos < starts[j] + us_truncated(c.segments[j].extinf)) k = j;
    if (!k) {
        out.kind = SeekLanding::Kind::Failed;
        return out;
    }
    out.segment = *k;
    // A backward video seek starts at the segment's start, and packets are
    // dropped until a keyframe whose DTS is at or after it: a segment's first
    // frame, its keyframe, passes when its PTS lies as far past the start as
    // the found segment does on the demuxer's clock, or farther, the two
    // packets' DTS leading their PTS alike, one encoder having made both.
    std::optional<std::size_t> from;
    for (std::size_t j = *k; j < c.segments.size() && !from; ++j) {
        const ClockSegment& s = c.segments[j];
        if (s.first >= s.last) {
            out.kind    = SeekLanding::Kind::Unknown;
            out.segment = j;
            return out;
        }
        if (us(c.seconds(c.frames[s.first])) - start_us >= starts[*k]) from = j;
    }
    if (!from) {
        out.kind = SeekLanding::Kind::Nothing;
        return out;
    }
    // The decode thread (src/MediaSession.cpp) drops each frame whose PTS
    // plus half a frame is before the target, show_from, and shows the
    // first it doesn't; where none is awaited, the first; and where every
    // frame to the end was before the target, the last.
    const double tol = c.frame_rate.num > 0 && c.frame_rate.den > 0 ? 0.5 / av_q2d(c.frame_rate) : 0.001;
    for (std::size_t i = c.segments[*from].first; i < c.frames.size(); ++i)
        if (!sought.await || c.seconds(c.frames[i]) + tol >= target) {
            out.kind  = SeekLanding::Kind::Shown;
            out.frame = i;
            return out;
        }
    out.kind  = SeekLanding::Kind::Shown;
    out.frame = c.frames.size() - 1;
    return out;
}

// Why a seek to `target` misses: what `l` shows instead.
std::string miss_text(const VariantClock& c, const SeekClock& seek, double target, const SeekLanding& l,
                      std::int64_t lead_us)
{
    const std::string lead =
        lead_us > 0 ? std::format(", with the first packet's DTS {:.3f} s before the start",
                                  static_cast<double>(lead_us) / static_cast<double>(kUs))
                    : std::string();
    if (target < seek.start)
        return std::format("its PTS, {:.3f} s, is before the stream's start, {:.3f} s: the player "
                           "would start the stream over", target, seek.start);
    switch (l.kind) {
    case SeekLanding::Kind::Shown: {
        const std::optional<std::size_t> s = segment_of(c, l.frame);
        return std::format("a seek to its PTS, {:.3f} s, would show the frame of PTS {:.3f} s{} "
                           "instead{}", target, c.seconds(c.frames[l.frame]),
                           s ? std::format(" in segment {}", c.segments[*s].media_sequence) : std::string(),
                           lead);
    }
    case SeekLanding::Kind::Nothing:
        return std::format("a seek to its PTS, {:.3f} s, would show nothing: the demuxer would drop "
                           "every packet to the stream's end{}", target, lead);
    case SeekLanding::Kind::Failed:
        return std::format("the demuxer would refuse a seek to its PTS, {:.3f} s{}", target, lead);
    case SeekLanding::Kind::Unknown:
        break;
    }
    return std::format("where a seek to its PTS, {:.3f} s, lands can't be told: segment {} has no "
                       "frame with a PTS in the report", target,
                       l.segment < c.segments.size() ? c.segments[l.segment].media_sequence : 0);
}

// Whether a seek to `target` shows frame `i` for every lead of the first
// packet's DTS from 0 to the largest of one frame, seek.video_delay frames
// and seek.dts_lead: the lead moves the position the demuxer looks the
// segment up by, so one seek is simulated with no lead and one at each
// segment end the lead reaches, from which on the next segment is found; the
// rest of the seek doesn't depend on the lead. `why` says how the first miss
// misses.
bool reaches(const VariantClock& c, const SeekClock& seek, double target, std::size_t i, std::string& why)
{
    // The decoder's reorder delay, and at least one frame (ui.md §2.4): the
    // Demuxer doesn't know the first packet's DTS, which precedes the first
    // PTS by that delay on tests/test_hls_seek_behaviour.cpp's segments.
    const double frame = c.frame_rate.num > 0 && c.frame_rate.den > 0 ? 1.0 / av_q2d(c.frame_rate) : 0.0;
    const double delay = static_cast<double>(std::max(seek.video_delay, 1)) * frame;
    const std::int64_t lead_us = us(std::max({ seek.dts_lead, delay, 0.0 }));
    const std::int64_t p0      = seek_us(sought_of(seek, target).seconds, c.time_base) - us(seek.start);
    const std::vector<std::int64_t> starts = hls_starts(c);
    std::vector<std::int64_t> leads{ 0 };
    for (std::size_t j = 0; j < c.segments.size(); ++j) {
        const std::int64_t end = starts[j] + us_truncated(c.segments[j].extinf);
        if (end > p0 && end - p0 <= lead_us) leads.push_back(end - p0);
    }
    for (const std::int64_t lead : leads) {
        const SeekLanding l = simulate_us(c, seek, target, lead);
        if (l.kind == SeekLanding::Kind::Shown && l.frame == i) continue;
        why = miss_text(c, seek, target, l, lead);
        return false;
    }
    return true;
}

} // namespace

// ── The match ────────────────────────────────────────────────────────────────

StreamMatch match_stream(const Timeline& timeline, const LadderScores& scores, const OpenStream& open)
{
    StreamMatch m;
    m.variant_of.assign(open.variants.size(), -1);
    // The variant each of the report's entries names (Variant::entries).
    std::vector<int> of_entry(scores.entries.size(), -1);
    for (std::size_t v = 0; v < timeline.variants.size(); ++v)
        for (const std::size_t e : timeline.variants[v].entries)
            if (e < of_entry.size()) of_entry[e] = static_cast<int>(v);

    const std::string sha256 = open.text.empty() ? std::string() : filehash::sha256_hex(open.text);
    const bool bytes   = !sha256.empty() && sha256 == scores.master_sha256;
    const bool address = same_address(scores.master_location, open.final_url) ||
                         same_address(scores.master_url, open.path);
    if (bytes || address) {
        m.by = bytes ? StreamMatch::By::MasterBytes : StreamMatch::By::Address;
        std::string refusal;
        if (bytes) {
            refusal = order_refusal(open, scores);
            if (refusal.empty()) {
                m.entries = StreamMatch::Entries::Index;
                for (std::size_t i = 0; i < m.variant_of.size(); ++i) m.variant_of[i] = of_entry[i];
                return m;
            }
            m.warnings.push_back(std::format("the master's bytes are the ones scored, but its entries "
                                             "don't correspond in order ({}): they are matched by URL",
                                             refusal));
        } else {
            m.warnings.push_back(std::format(
                "the master at {} has changed since it was scored: its SHA-256 is {}, the report's {}; "
                "its entries are matched by URL", open.final_url.empty() ? open.path : open.final_url,
                sha256.empty() ? std::string("unknown") : sha256, scores.master_sha256));
        }
        // By URL. In the bytes scored, the report's URIs resolved by the
        // player's own parser against where the player read the master, as
        // order_refusal() resolves them, so that the same bytes served from
        // elsewhere still match; in a master changed since, the report's URLs
        // as VMAFLadder resolved them against where it read the master, each
        // side resolved by its own parser.
        m.entries = StreamMatch::Entries::Url;
        std::vector<std::string> report_url(scores.entries.size());
        for (std::size_t e = 0; e < scores.entries.size(); ++e)
            report_url[e] = bytes ? player_url(scores.entries[e].uri, open.final_url) : scores.entries[e].url;
        std::string unmatched;
        for (std::size_t i = 0; i < open.variants.size(); ++i) {
            bool listed = false;
            for (std::size_t e = 0; e < scores.entries.size(); ++e) {
                if (report_url[e] != open.variants[i].url) continue;
                listed = true;
                if (of_entry[e] >= 0) {
                    m.variant_of[i] = of_entry[e];
                    break;
                }
            }
            if (!listed)
                unmatched += std::format("{}entry {} ({})", unmatched.empty() ? "" : ", ", i,
                                         open.variants[i].url);
        }
        if (!unmatched.empty())
            m.warnings.push_back(
                bytes ? std::format("no entry of the report has the URL of the player's {}: the report's "
                                    "URIs and the player's resolved alike, by ManifestParser",
                                    unmatched)
                      : std::format("no entry of the report has the URL of the player's {}: VMAFLadder "
                                    "resolved the report's, ManifestParser the player's",
                                    unmatched));
        return m;
    }

    // One variant's own input opened alone (D-121 (d), Q89 (b)): the variant
    // by its address, the URL the report gives it or where the build read its
    // playlist; else its media playlist by its bytes.
    for (std::size_t v = 0; v < timeline.variants.size(); ++v) {
        const Variant& var = timeline.variants[v];
        if (!same_address(var.url, open.path) && !same_address(var.url, open.final_url) &&
            !same_address(var.playlist_location, open.path) &&
            !same_address(var.playlist_location, open.final_url))
            continue;
        m.by             = StreamMatch::By::Variant;
        m.opened_variant = static_cast<int>(v);
        if (!sha256.empty() && !var.playlist_sha256.empty() && sha256 != var.playlist_sha256)
            m.warnings.push_back(std::format("{} has changed since the document was built: its SHA-256 "
                                             "is {}, the document's {}", open.path, sha256,
                                             var.playlist_sha256));
        return m;
    }
    // By its bytes. The rungs of one ladder can share their playlist's text,
    // as D-069's do (eval/phase3-survey/testgen.md §2.3), so among the
    // variants whose playlists have those bytes the one is taken whose URL,
    // or playlist location, ends with the longest run of path components
    // that the open path or its final URL ends with, the first of the
    // longest (D-121 (d), Q89 (b)): a rung's v270/index.m3u8 opened from
    // another address matches the v270 rung. Where none shares more than its
    // file name, the first of them all is taken, as WP12 built it, on
    // D-069's ladders the reference. The open path is compared as the player
    // was given it, and a local file's final URL is the same text
    // (src/ManifestParser.cpp), so that a rung's playlist opened by a path
    // relative to its own directory, index.m3u8 or ./index.m3u8, shares only
    // its file name, and the first of them all is taken: a reading pending
    // the owner, whose alternative is the opened file's path from the
    // current directory (src/TimelineMatch.h). A warning says which was
    // taken, and the playhead and the seek take that variant's clock.
    std::vector<std::size_t> same_bytes;
    for (std::size_t v = 0; v < timeline.variants.size(); ++v)
        if (!sha256.empty() && sha256 == timeline.variants[v].playlist_sha256) same_bytes.push_back(v);
    if (!same_bytes.empty()) {
        m.by             = StreamMatch::By::Variant;
        m.opened_variant = static_cast<int>(same_bytes.front());
        if (same_bytes.size() == 1) return m;
        std::size_t best = same_bytes.front(), run = 0;
        bool        by_location = false;
        std::string tail;
        for (const std::size_t v : same_bytes) {
            const Variant& var = timeline.variants[v];
            for (const std::string* side : { &var.url, &var.playlist_location })
                for (const std::string* opened_at : { &open.path, &open.final_url }) {
                    const std::size_t n = shared_tail(*side, *opened_at);
                    if (n <= run) continue;
                    best        = v;
                    run         = n;
                    by_location = side == &var.playlist_location;
                    tail        = tail_text(*opened_at, n);
                }
        }
        // Where the path picks, a warning names the variant and the path, as
        // one is given wherever several variants have those bytes, so that
        // the match warns even where the path named the rung: a reading
        // pending the owner, whose alternative is no warning there
        // (src/TimelineMatch.h).
        if (run > 1) {
            m.opened_variant = static_cast<int>(best);
            m.warnings.push_back(std::format("the open playlist's bytes are those of {} of the document's "
                                             "variants: variant {}, whose {} ends with the open "
                                             "playlist's {}, is taken", same_bytes.size(), best,
                                             by_location ? "playlist location" : "URL", tail));
        } else {
            m.warnings.push_back(std::format("the open playlist's bytes are those of {} of the document's "
                                             "variants: the first, variant {}, is taken",
                                             same_bytes.size(), same_bytes.front()));
        }
        return m;
    }
    m.warnings.push_back(std::format("the document's master is {} (SHA-256 {}), which the open stream "
                                     "isn't", scores.master_url, scores.master_sha256));
    return m;
}

std::string_view match_name(StreamMatch::By by)
{
    switch (by) {
    case StreamMatch::By::MasterBytes: return "master SHA-256";
    case StreamMatch::By::Address:     return "master address";
    case StreamMatch::By::Variant:     return "variant";
    case StreamMatch::By::None:        break;
    }
    return "none";
}

// ── A variant's clock ────────────────────────────────────────────────────────

double VariantClock::seconds(const ClockFrame& f) const
{
    return static_cast<double>(f.pts) * av_q2d(time_base);
}

VariantClock variant_clock(const Timeline& timeline, const LadderScores& scores, int variant)
{
    VariantClock c;
    if (variant < 0 || static_cast<std::size_t>(variant) >= timeline.variants.size()) return c;
    const Variant& v = timeline.variants[static_cast<std::size_t>(variant)];
    c.variant     = variant;
    c.pair        = v.pair;
    c.playlist    = !v.playlist_sha256.empty() || !v.playlist_location.empty();
    c.segment_map = v.segment_map;
    if (v.pair == -1) {
        // The reference: its own frames, by the axis's media keys (D-073).
        const Axis& axis = timeline.axis;
        c.time_base = axis.time_base;
        if (axis.frame_duration.num > 0 && axis.frame_duration.den > 0)
            c.frame_rate = av_inv_q(axis.frame_duration);
        for (std::size_t i = 0; i < axis.keys.size(); ++i)
            if (axis.keys[i].pts)
                c.frames.push_back({ .index     = static_cast<std::int64_t>(i),
                                     .ref_index = static_cast<std::int64_t>(i),
                                     .pts       = *axis.keys[i].pts,
                                     .sequence  = axis.keys[i].sequence });
    } else {
        // A pair's distorted input: its scored frames (D-092 (b): the pair by
        // its number, the report's pairs[].index).
        const PairScores* p = nullptr;
        for (const PairScores& s : scores.pairs)
            if (static_cast<int>(s.index) == v.pair) p = &s;
        if (!p) return c;
        c.time_base  = p->dis_time_base;
        c.frame_rate = p->dis_rate;
        const std::size_t n = std::min({ p->dis_index.size(), p->ref_index.size(), p->dis_pts.size() });
        for (std::size_t j = 0; j < n; ++j)
            c.frames.push_back({ .index     = p->dis_index[j],
                                 .ref_index = p->ref_index[j],
                                 .pts       = p->dis_pts[j],
                                 .sequence  = sequence_of(p->dis_sequences, p->dis_index[j]) });
    }

    // Its segments, from the document's segment events of the variant.
    for (const Event& e : timeline.events) {
        if (e.source != Source::Playlist || e.type != kTypeSegment || e.variant != variant ||
            e.session >= 0 || !e.segment || !e.ref_end)
            continue;
        ClockSegment s;
        s.media_sequence         = *e.segment;
        s.discontinuity_sequence =
            static_cast<std::int64_t>(number_member(e.payload, "discontinuity_sequence"));
        s.playlist_time          = number_member(e.payload, "playlist_time");
        s.extinf                 = number_member(e.payload, "extinf");
        s.ref_index              = e.ref_index;
        s.ref_end                = *e.ref_end;
        c.segments.push_back(s);
    }
    std::sort(c.segments.begin(), c.segments.end(), [](const ClockSegment& a, const ClockSegment& b) {
        return a.media_sequence < b.media_sequence;
    });
    for (ClockSegment& s : c.segments) {
        const auto first = std::partition_point(c.frames.begin(), c.frames.end(),
                                                [&](const ClockFrame& f) { return f.ref_index < s.ref_index; });
        const auto last = std::partition_point(first, c.frames.end(),
                                               [&](const ClockFrame& f) { return f.ref_index < s.ref_end; });
        s.first = static_cast<std::size_t>(first - c.frames.begin());
        s.last  = static_cast<std::size_t>(last - c.frames.begin());
    }
    return c;
}

// ── The playhead ─────────────────────────────────────────────────────────────

std::optional<PlayheadFrame> track_playhead(const VariantClock& clock, PlayheadState& state, double pts)
{
    if (clock.frames.empty() || clock.time_base.num <= 0 || clock.time_base.den <= 0) return std::nullopt;
    const double tb = av_q2d(clock.time_base);
    const auto   t  = static_cast<std::int64_t>(std::llround(pts / tb));

    // A jump as the aligner defines a discontinuity (D-046): the next
    // sequence the clock has, if any.
    if (state.sequence >= 0 && state.last_pts) {
        const double jump = av_q2d(vmafalign::kForwardJump) / tb;
        if (t < *state.last_pts || static_cast<double>(t - *state.last_pts) > jump) {
            const auto next =
                std::partition_point(clock.frames.begin(), clock.frames.end(),
                                     [&](const ClockFrame& f) { return f.sequence <= state.sequence; });
            if (next != clock.frames.end()) state.sequence = next->sequence;
        }
    }
    bool known = state.sequence >= 0;
    if (known) {
        const auto [first, last] = sequence_range(clock, state.sequence);
        known = first < last;
    }
    if (!known) {
        // Not known, or a sequence the clock has no frame of: the first
        // sequence with a frame within half a frame of the PTS, else the
        // nearest frame's.
        const double half = clock.frame_rate.num > 0 && clock.frame_rate.den > 0
                          ? 0.5 / av_q2d(clock.frame_rate) / tb : 0.0;
        std::optional<int> within;
        std::size_t best = 0;
        double best_d = std::numeric_limits<double>::infinity();
        for (std::size_t a = 0; a < clock.frames.size() && !within;) {
            const int s = clock.frames[a].sequence;
            const auto [first, last] = sequence_range(clock, s);
            const std::size_t i = nearest_in(clock, first, last, t);
            const double d = distance_ticks(clock.frames[i], t);
            if (d <= half) within = s;
            if (d < best_d) {
                best_d = d;
                best   = i;
            }
            a = last;
        }
        state.sequence = within ? *within : clock.frames[best].sequence;
    }

    const auto [first, last] = sequence_range(clock, state.sequence);
    const std::size_t i = nearest_in(clock, first, last, t);
    state.last_pts = t;
    const ClockFrame& f = clock.frames[i];
    return PlayheadFrame{ .frame = i, .index = f.index, .ref_index = f.ref_index, .sequence = f.sequence,
                          .distance = clock.seconds(f) - pts };
}

void playhead_seeked(PlayheadState& state, int sequence)
{
    state.sequence = sequence;
    state.last_pts.reset();
}

// ── The seek decision ────────────────────────────────────────────────────────

SeekLanding simulate_seek(const VariantClock& clock, const SeekClock& seek, double target, double lead)
{
    return simulate_us(clock, seek, target, us(std::max(lead, 0.0)));
}

SeekDecision decide_seek(const VariantClock& clock, const SeekClock& seek, std::int64_t ref_index)
{
    SeekDecision d;
    if (clock.frames.empty() || clock.time_base.num <= 0 || clock.time_base.den <= 0) {
        d.why = "no frame of the variant on screen has a PTS in the report";
        return d;
    }
    // Its frame there: the last at or before the reference frame, or its first.
    const auto after = std::partition_point(clock.frames.begin(), clock.frames.end(),
                                            [&](const ClockFrame& f) { return f.ref_index <= ref_index; });
    const std::size_t i =
        after == clock.frames.begin() ? 0 : static_cast<std::size_t>(after - clock.frames.begin()) - 1;
    d.frame = i;
    const double target = clock.seconds(clock.frames[i]);
    if (!clock.playlist) {
        // A file opened alone: sought by its PTS, the Demuxer waiting for the
        // keyframe at or before it (src/Demuxer.cpp).
        d.kind   = SeekDecision::Kind::Seek;
        d.target = target;
        return d;
    }
    if (clock.segments.empty()) {
        d.why = clock.segment_map != "ok"
              ? std::format("its segment map was {}: where a seek lands can't be told", clock.segment_map)
              : std::string("the document has no segment of it: where a seek lands can't be told");
        return d;
    }
    if (reaches(clock, seek, target, i, d.why)) {
        d.kind   = SeekDecision::Kind::Seek;
        d.target = target;
        d.why.clear();
        return d;
    }
    // Not reachable by a seek (D-114 (b)): the latest segment start, at or
    // before the frame's segment, that a seek reaches. For an ad whose
    // timestamps restart, the segment before its discontinuity, as D-114 (b)
    // has it. For a frame that the first DTS's lead carries into the next
    // segment, a segment's last frame on MPEG-2 and its last two on libx264
    // (tests/test_hls_seek_behaviour.cpp), its own segment's start, since a
    // seek by its PTS shows the next segment's first frame: D-114 (b)
    // extended beyond ads as built, which the owner kept (D-121 (c), Q88 (a)).
    const std::optional<std::size_t> own = segment_of(clock, i);
    for (std::size_t j = own ? *own + 1 : 0; j-- > 0;) {
        const ClockSegment& s = clock.segments[j];
        if (s.first >= s.last || s.first == i) continue;
        std::string unused;
        if (!reaches(clock, seek, clock.seconds(clock.frames[s.first]), s.first, unused)) continue;
        d.kind    = SeekDecision::Kind::Offer;
        d.target  = clock.seconds(clock.frames[s.first]);
        d.segment = j;
        return d;
    }
    return d;
}

// ── A click ──────────────────────────────────────────────────────────────────

Interval click_interval(const Timeline& timeline, CurveRef curve, std::int64_t ref_index)
{
    const Interval frame{ .ref_index = ref_index, .ref_end = ref_index + 1 };
    int variant = -1;
    if (curve.kind == CurveRef::Kind::Pair) {
        if (curve.index >= 0 && static_cast<std::size_t>(curve.index) < timeline.curves.size())
            variant = timeline.curves[static_cast<std::size_t>(curve.index)].variant;
    } else if (curve.index >= 0 && static_cast<std::size_t>(curve.index) < timeline.sessions.size()) {
        for (const Span& s : timeline.sessions[static_cast<std::size_t>(curve.index)].spans)
            if (s.ref_index <= ref_index && ref_index < s.ref_end) variant = s.variant;
    }
    if (variant < 0) return frame;
    for (const std::size_t k : timeline.overlapping(ref_index, ref_index + 1)) {
        const Event& e = timeline.events[k];
        if (e.source == Source::Playlist && e.type == kTypeSegment && e.variant == variant && e.session < 0 &&
            e.ref_end && e.ref_index <= ref_index && ref_index < *e.ref_end)
            return { .ref_index = e.ref_index, .ref_end = *e.ref_end };
    }
    return frame;
}

ClickAnswer click_answer(const Snapshot& snapshot, CurveRef curve, std::int64_t ref_index)
{
    ClickAnswer out;
    for (std::size_t i = 0; i < snapshot.attributions.size(); ++i) {
        const Attribution& a = snapshot.attributions[i];
        if (!a.detected || a.curve.kind != curve.kind || a.curve.index != curve.index) continue;
        if (a.interval.ref_index <= ref_index && ref_index < a.interval.ref_end) {
            out.drop        = i;
            out.attribution = a;
            return out;
        }
    }
    // Elsewhere the query of the clicked frame's segment (D-114 (d)), with the
    // margins the document's attributions were made with, so that it agrees
    // with the document's drops, as built, which the owner kept (D-121 (b),
    // Q87 (a)); mirage_vmaf attribute --interval queries with none without
    // --margin-before or --margin-after (Q29's default).
    out.attribution = attribute(snapshot.timeline, curve, click_interval(snapshot.timeline, curve, ref_index),
                                snapshot.info.overlap);
    return out;
}

} // namespace timeline
