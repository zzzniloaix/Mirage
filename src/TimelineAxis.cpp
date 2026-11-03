#include "TimelineAxis.h"

#include "HlsAlign.h"
#include "LadderReportRead.h"
#include "TimelineBuild.h"
#include "VMAFAlign.h"

extern "C" {
#include <libavutil/avutil.h>   // AV_NOPTS_VALUE
}

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstddef>
#include <format>
#include <limits>
#include <numeric>

namespace timeline {

namespace {

using i128 = __int128;

// A frame the demux pass read without a PTS: FFmpeg's AV_NOPTS_VALUE.
constexpr std::int64_t kNoPts = AV_NOPTS_VALUE;

// EXTINF are read to the nanosecond. hls::parse_media_timeline gives each as
// the double nearest its text; below 2^21 s that double is within 2^-32 s
// of the text's value (52 bits of fraction), so for a text of at most nine
// decimals the double times 10^9, rounded, is the value in nanoseconds
// exactly: the product is within 0.48 of it, its own rounding included.
constexpr std::int64_t kExtinfDen = 1000000000;
constexpr double       kExtinfMax = 2097152.0;   // 2^21 s

// Ticks beyond 2^62 either way are taken as a clock that can't be computed.
constexpr i128 kTickLimit = i128{ 1 } << 62;

bool fits(i128 v) { return v > -kTickLimit && v < kTickLimit; }

// `q` in lowest terms with a positive denominator; {0, 1} without one.
AVRational reduced(AVRational q)
{
    AVRational out{ 0, 1 };
    if (q.den == 0) return out;
    av_reduce(&out.num, &out.den, q.num, q.den, INT_MAX);
    return out;
}

// The least common multiple of two positive numbers; 0 when it is larger
// than the aligner's limit on ticks per second (src/VMAFAlign.h), or when
// either is 0.
std::int64_t lcm_within(std::int64_t a, std::int64_t b)
{
    if (a <= 0 || b <= 0) return 0;
    const i128 l = static_cast<i128>(a / std::gcd(a, b)) * b;
    return l > vmafalign::kMaxTicksPerSecond ? 0 : static_cast<std::int64_t>(l);
}

// `t` ticks of 1 / `den` s in seconds: exactly rounded while both fit a
// double's 53 bits, as they do below 2^53 ticks.
double to_seconds(i128 t, std::int64_t den)
{
    if (den <= 0) return 0.0;
    constexpr i128 kExact = i128{ 1 } << 53;
    if (t > -kExact && t < kExact && den < kExact)
        return static_cast<double>(static_cast<std::int64_t>(t)) / static_cast<double>(den);
    const i128 q = t / den, r = t % den;
    return static_cast<double>(q) + static_cast<double>(r) / static_cast<double>(den);
}

// `v` held within the ticks' limit.
std::int64_t saturate(i128 v)
{
    if (v >= kTickLimit) return static_cast<std::int64_t>(kTickLimit);
    if (v <= -kTickLimit) return -static_cast<std::int64_t>(kTickLimit);
    return static_cast<std::int64_t>(v);
}

std::string text(AVRational q) { return std::format("{}/{}", q.num, q.den); }

// A number of frames held within 0 and INT64_MAX, so that counts a
// hand-built input gives can't overflow.
std::int64_t frame_count(i128 n)
{
    constexpr i128 kMax = std::numeric_limits<std::int64_t>::max();
    return n <= 0 ? 0 : static_cast<std::int64_t>(std::min(n, kMax));
}

// The reference frames a pair's analysis read: its reference sequences'.
std::int64_t frames_read(const PairScores& p)
{
    i128 n = 0;
    for (const Sequence& s : p.ref_sequences) n += s.frames;
    return frame_count(n);
}

// What one test of the check found at one scored frame: the difference in
// ticks, and the sequence whose grid it was measured on.
struct Probe {
    std::int64_t frame = 0;
    i128         diff  = 0;
    std::int64_t sequence = 0;
    bool         off   = false;   // more than half a frame
};

i128 magnitude(i128 v) { return v < 0 ? -v : v; }

// The runs of consecutive probes that are off, a run of a grid test ending
// where its sequence does.
void add_runs(std::vector<AxisRun>& out, const std::vector<Probe>& probes, AxisRun::Test test,
              bool warning, std::int64_t den)
{
    bool open  = false;
    i128 worst = 0;
    for (const Probe& p : probes) {
        if (!p.off) {
            open = false;
            continue;
        }
        if (open && out.back().sequence == p.sequence) {
            AxisRun& r = out.back();
            r.last = p.frame;
            ++r.frames;
            if (magnitude(p.diff) > magnitude(worst)) {
                worst        = p.diff;
                r.largest_at = p.frame;
            }
        } else {
            out.push_back({ .test = test, .sequence = p.sequence, .first = p.frame, .last = p.frame,
                            .frames = 1, .largest_at = p.frame, .largest = 0, .warning = warning });
            worst = p.diff;
            open  = true;
        }
        out.back().largest = to_seconds(worst, den);
    }
}

// The segment map fitted to the playlist, as far as the axis needs it.
struct MapSegment {
    std::int64_t first    = 0;   // its first frame
    std::int64_t frames   = 0;
    std::int64_t disc_seq = 0;   // the playlist's discontinuity sequence number
    std::size_t  listed   = 0;   // its index among the playlist's segments
};

// Whether `media`'s segments fit `playlist` and cover `frames` reference
// frames: each a segment the playlist lists, each starting where the one
// before ends, the first at frame 0. Why not, or empty.
std::string fit_map(const VariantMedia& media, const hls::MediaTimeline& playlist,
                    std::int64_t frames, std::vector<MapSegment>& out)
{
    if (!playlist.ok) return "the reference's playlist has no media segments";
    std::int64_t next = 0;
    for (const SegmentFrames& s : media.segments) {
        const int listed = playlist.index_of(s.seq);
        if (listed < 0)
            return std::format("the segment map names media sequence {}, which the playlist "
                               "doesn't list", s.seq);
        if (s.first_index != next || s.frames < 0)
            return std::format("the segment map's media sequence {} starts at frame {}, "
                               "not at frame {}", s.seq, s.first_index, next);
        out.push_back({ .first = s.first_index, .frames = s.frames,
                        .disc_seq = playlist.segments[static_cast<std::size_t>(listed)].disc_seq,
                        .listed = static_cast<std::size_t>(listed) });
        next += s.frames;
    }
    if (next != frames)
        return std::format("the segment map covers {} of the reference's {} frames", next, frames);
    return {};
}

} // namespace

std::int64_t Axis::ticks_at(std::int64_t ref_index) const
{
    i128 step = 0;   // the frame duration in ticks
    if (frame_duration.num > 0 && frame_duration.den > 0)
        step = static_cast<i128>(frame_duration.num) * tick_den / frame_duration.den;
    if (ticks.empty()) return saturate(static_cast<i128>(ref_index) * step);
    const auto last = static_cast<std::int64_t>(ticks.size()) - 1;
    if (ref_index < 0) return saturate(ticks.front() + static_cast<i128>(ref_index) * step);
    if (ref_index > last) return saturate(ticks.back() + static_cast<i128>(ref_index - last) * step);
    return ticks[static_cast<std::size_t>(ref_index)];
}

double Axis::seconds(std::int64_t ref_index) const
{
    return to_seconds(ticks_at(ref_index), tick_den);
}

double Axis::seconds_between(std::int64_t from, std::int64_t to) const
{
    return to_seconds(static_cast<i128>(ticks_at(to)) - ticks_at(from), tick_den);
}

namespace {

// The first frame from `from` on whose ticks t meet t * den >= rhs, in
// index order, or the number of frames; 0 <= from, den > 0.
std::int64_t first_at_or_after(const std::vector<std::int64_t>& ticks, std::size_t from, i128 rhs,
                               std::int64_t den)
{
    for (std::size_t i = from; i < ticks.size(); ++i)
        if (static_cast<i128>(ticks[i]) * den >= rhs) return static_cast<std::int64_t>(i);
    return static_cast<std::int64_t>(ticks.size());
}

// Whether the axis has seconds: a frame duration, as ticks_at() takes one,
// which build_axis() leaves at 0/1 when it puts every frame at 0 s.
bool has_seconds(const Axis& axis)
{
    return axis.frame_duration.num > 0 && axis.frame_duration.den > 0;
}

} // namespace

std::int64_t Axis::frame_at_or_after(AVRational s) const
{
    if (s.den == 0) return s.num > 0 ? static_cast<std::int64_t>(ticks.size()) : 0;
    i128 num = s.num, den = s.den;
    if (den < 0) num = -num, den = -den;
    return first_at_or_after(ticks, 0, num * tick_den, static_cast<std::int64_t>(den));
}

std::int64_t Axis::frame_at_or_after(std::int64_t ref_index, AVRational offset) const
{
    if (offset.den == 0) return offset.num > 0 ? static_cast<std::int64_t>(ticks.size()) : 0;
    i128 num = offset.num, den = offset.den;
    if (den < 0) num = -num, den = -den;
    return first_at_or_after(ticks, 0, static_cast<i128>(ticks_at(ref_index)) * den + num * tick_den,
                             static_cast<std::int64_t>(den));
}

std::optional<std::int64_t> Axis::frame_after(std::int64_t ref_index, AVRational duration) const
{
    if (!has_seconds(*this) || ref_index < 0 || ref_index >= static_cast<std::int64_t>(ticks.size()))
        return std::nullopt;
    const auto from = static_cast<std::size_t>(ref_index);
    if (duration.den == 0) return duration.num > 0 ? static_cast<std::int64_t>(ticks.size()) : ref_index;
    i128 num = duration.num, den = duration.den;
    if (den < 0) num = -num, den = -den;
    return first_at_or_after(ticks, from, static_cast<i128>(ticks[from]) * den + num * tick_den,
                             static_cast<std::int64_t>(den));
}

std::string run_text(const AxisRun& run)
{
    const std::string frames =
        std::format("reference frames {} to {} ({} scored)", run.first, run.last, run.frames);
    switch (run.test) {
    case AxisRun::Test::Clocks:
        return std::format("{}: the frame grid's seconds differ from the playlist time by up to "
                           "{:+.3f} s, at frame {}", frames, run.largest, run.largest_at);
    case AxisRun::Test::PlaylistGrid:
        return std::format("{}: the PTS differ from the frame grid of the playlist's "
                           "discontinuity sequence {} by up to {:+.3f} s, at frame {}", frames,
                           run.sequence, run.largest, run.largest_at);
    case AxisRun::Test::ReportGrid:
        return std::format("{}: the PTS differ from the frame grid of the report's reference "
                           "sequence {} by up to {:+.3f} s, at frame {}", frames, run.sequence,
                           run.largest, run.largest_at);
    }
    return frames;
}

Axis build_axis(const LadderScores& scores, const VariantMedia* reference,
                const hls::MediaTimeline* reference_playlist)
{
    Axis axis;
    axis.clock   = "frame-grid";
    axis.checked = "report-only";

    // Every complete pair read the same reference; the one that read most of
    // it gives its time base, frame rate and sequences.
    const PairScores* base = nullptr;
    for (const PairScores& p : scores.pairs)
        if (!base || frames_read(p) > frames_read(*base)) base = &p;
    const AVRational tb   = base ? reduced(base->ref_time_base) : AVRational{ 0, 1 };
    const AVRational rate = base ? reduced(base->ref_rate) : AVRational{ 0, 1 };
    if (base) axis.time_base = base->ref_time_base;

    // The demux pass's frames count unless it was refused (D-081 (c)): its
    // frame positions are then not known to be the report's.
    const bool media = reference && reference->refused.empty();

    // The reference's frames: the most a pair read or the demux pass gives.
    // No scored frame's position counts: one outside the frames its pair
    // read isn't used (below), so that a position can't size the axis.
    std::int64_t frames = 0;
    for (const PairScores& p : scores.pairs) frames = std::max(frames, frames_read(p));
    if (media) {
        frames = std::max(frames, static_cast<std::int64_t>(reference->pts.size()));
        for (const SegmentFrames& s : reference->segments)
            frames = std::max(frames, frame_count(static_cast<i128>(s.first_index) + s.frames));
    }
    axis.frames = frames;
    const auto n = static_cast<std::size_t>(frames);

    // The PTS of every scored frame, from the report. Pairs that read the
    // reference with another time base or frame rate, or give a frame other
    // PTS than an earlier pair, are named; the earlier pair's PTS are kept.
    // So are scored frames at positions outside the frames their pair read,
    // which aren't used.
    std::vector<std::optional<std::int64_t>> pts(n);
    std::vector<bool> scored(n, false);
    for (const PairScores& p : scores.pairs) {
        if (av_cmp_q(reduced(p.ref_time_base), tb) != 0 || av_cmp_q(reduced(p.ref_rate), rate) != 0) {
            axis.warnings.push_back(std::format(
                "pair {} reads the reference with time base {} and frame rate {}, pair {} with {} "
                "and {}: its reference PTS aren't used", p.index, text(p.ref_time_base),
                text(p.ref_rate), base->index, text(base->ref_time_base), text(base->ref_rate)));
            continue;
        }
        const std::int64_t read = frames_read(p);   // at most `frames`
        std::int64_t outside = 0, first_outside = 0, differ = 0, first_differ = -1;
        const std::size_t k_end = std::min(p.ref_index.size(), p.ref_pts.size());
        for (std::size_t k = 0; k < k_end; ++k) {
            const std::int64_t i = p.ref_index[k];
            if (i < 0 || i >= read) {   // not one of the frames the pair read
                if (outside++ == 0) first_outside = i;
                continue;
            }
            auto& v = pts[static_cast<std::size_t>(i)];
            scored[static_cast<std::size_t>(i)] = true;
            if (!v) {
                v = p.ref_pts[k];
            } else if (*v != p.ref_pts[k]) {
                if (differ++ == 0) first_differ = i;
            }
        }
        if (outside > 0)
            axis.warnings.push_back(std::format(
                "pair {} has {} scored frame{} outside the {} reference frames it read, {} position "
                "{}: {} not used", p.index, outside, outside == 1 ? "" : "s", read,
                outside == 1 ? "at" : "the first at", first_outside,
                outside == 1 ? "it is" : "they are"));
        if (differ > 0)
            axis.warnings.push_back(std::format(
                "pair {} gives {} reference frame{} other PTS than an earlier pair, the first frame "
                "{}: the earlier pair's are kept", p.index, differ, differ == 1 ? "" : "s",
                first_differ));
    }
    // The demux pass's PTS for the frames no pair scored (design §5.2).
    if (media) {
        std::int64_t differ = 0, first_differ = -1;
        for (std::size_t i = 0; i < std::min(n, reference->pts.size()); ++i) {
            const std::int64_t v = reference->pts[i];
            if (v == kNoPts) continue;
            if (!pts[i]) {
                pts[i] = v;
            } else if (*pts[i] != v) {
                if (differ++ == 0) first_differ = static_cast<std::int64_t>(i);
            }
        }
        if (differ > 0)
            axis.warnings.push_back(std::format(
                "the demux pass gives {} scored reference frame{} other PTS than the report, the "
                "first frame {}: the report's are kept", differ, differ == 1 ? "" : "s",
                first_differ));
    }

    // The media keys: each frame's report sequence, by where the sequences
    // start, and its PTS.
    const std::vector<Sequence> no_sequences;
    const std::vector<Sequence>& sequences = base ? base->ref_sequences : no_sequences;
    axis.keys.resize(n);
    {
        std::size_t q = 0;
        for (std::size_t i = 0; i < n; ++i) {
            while (q + 1 < sequences.size() &&
                   sequences[q + 1].first_index <= static_cast<std::int64_t>(i))
                ++q;
            axis.keys[i] = { .sequence = static_cast<std::int32_t>(q), .pts = pts[i] };
        }
    }
    axis.ticks.assign(n, 0);

    // The common time base of the PTS, the frame duration and its half.
    if (tb.num <= 0 || rate.num <= 0) {
        axis.warnings.push_back(
            base ? std::format("the reference's time base {} or frame rate {} is unknown: every "
                               "frame is at 0 s", text(base->ref_time_base), text(base->ref_rate))
                 : std::string("the report has no complete pair, so the reference's time base and "
                               "frame rate are unknown: every frame is at 0 s"));
        return axis;
    }
    const AVRational fd = reduced(av_inv_q(rate));
    axis.frame_duration = fd;
    std::int64_t den = lcm_within(lcm_within(tb.den, fd.den), 2 * static_cast<std::int64_t>(fd.den));
    if (den == 0) {
        axis.frame_duration = { 0, 1 };
        axis.warnings.push_back(std::format(
            "the reference's time base {} and frame duration {} have no common time base of at "
            "most {} ticks per second: every frame is at 0 s", text(tb), text(fd),
            vmafalign::kMaxTicksPerSecond));
        return axis;
    }

    // The segment map, where the reference has one that fits its playlist,
    // with each listed segment's EXTINF in nanoseconds.
    std::vector<MapSegment> map;
    std::vector<std::int64_t> extinf_ns;
    bool have_map = false;
    if (media && reference_playlist && !reference->segments.empty()) {
        std::string why = fit_map(*reference, *reference_playlist, frames, map);
        if (why.empty()) {
            std::int64_t with_extinf = den;
            for (const hls::Segment& s : reference_playlist->segments) {
                if (!(s.duration >= 0.0 && s.duration < kExtinfMax)) {
                    why = std::format("media sequence {}'s EXTINF {} can't be read exactly",
                                      s.seq, s.duration);
                    break;
                }
                const auto ns = static_cast<std::int64_t>(std::llround(s.duration * kExtinfDen));
                extinf_ns.push_back(ns);
                with_extinf = lcm_within(with_extinf, kExtinfDen / std::gcd(ns, kExtinfDen));
                if (with_extinf == 0) {
                    why = std::format("the playlist's EXTINF and the reference's time base {} have "
                                      "no common time base of at most {} ticks per second",
                                      text(tb), vmafalign::kMaxTicksPerSecond);
                    break;
                }
            }
            if (why.empty()) den = with_extinf;
        }
        if (!why.empty())
            axis.warnings.push_back(why + ": the seconds are the frame grid's");
        have_map = why.empty();
    }

    const i128 tb_ticks = static_cast<i128>(tb.num) * (den / tb.den);   // per PTS tick
    const i128 fd_ticks = static_cast<i128>(fd.num) * (den / fd.den);   // per frame
    const i128 tol      = fd_ticks / 2;                                 // exact: 2 fd.den | den
    axis.tick_den = den;

    // The frame grid's seconds, (d): the frame index times the frame duration.
    std::vector<i128> grid(n);
    bool exact = true;
    for (std::size_t i = 0; i < n; ++i) {
        grid[i] = static_cast<i128>(i) * fd_ticks;
        exact   = exact && fits(grid[i]);
    }

    // The playlist time, (c): each segment at the EXTINF of the segments
    // listed before it, each frame at its segment's time plus its PTS's
    // distance from the PTS of the segment's first frame. Where frames have
    // no PTS: the segment's first frame with one sits at the segment's time
    // plus its distance in frames from the segment's first frame, as do the
    // frames before it, and a frame without a PTS after it sits a frame
    // duration after the frame before it.
    std::vector<i128> playlist(n);
    if (have_map) {
        std::vector<i128> listed_start(reference_playlist->segments.size());
        i128 t = 0;
        for (std::size_t j = 0; j < listed_start.size(); ++j) {
            listed_start[j] = t;
            const std::int64_t g = std::gcd(extinf_ns[j], kExtinfDen);
            t += static_cast<i128>(extinf_ns[j] / g) * (den / (kExtinfDen / g));
            exact = exact && fits(t);
        }
        for (const MapSegment& s : map) {
            const i128 start = listed_start[s.listed];
            std::optional<std::size_t> anchor;   // the segment's first frame with a PTS
            for (std::int64_t k = 0; k < s.frames; ++k) {
                const auto i = static_cast<std::size_t>(s.first + k);
                if (!anchor) {
                    playlist[i] = start + static_cast<i128>(k) * fd_ticks;
                    if (pts[i]) anchor = i;
                } else if (pts[i]) {
                    playlist[i] = playlist[*anchor] +
                                  (static_cast<i128>(*pts[i]) - *pts[*anchor]) * tb_ticks;
                } else {
                    playlist[i] = playlist[i - 1] + fd_ticks;
                }
                exact = exact && fits(playlist[i]);
            }
        }
    }
    if (!exact) {
        if (have_map)
            axis.warnings.push_back("the playlist time doesn't fit the axis's ticks: the seconds "
                                    "are the frame grid's");
        have_map = false;
        exact    = std::all_of(grid.begin(), grid.end(), fits);
        if (!exact) {
            axis.warnings.push_back("the frame grid doesn't fit the axis's ticks: every frame is "
                                    "at 0 s");
            axis.frame_duration = { 0, 1 };
            axis.tick_den       = 1;
            return axis;
        }
    }

    // The check, at every scored reference frame (design §4.2).
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < n; ++i)
        if (scored[i]) order.push_back(i);
    if (have_map) {
        axis.clock   = "playlist-time";
        axis.checked = "segment-map";
        for (std::size_t i = 0; i < n; ++i) axis.ticks[i] = static_cast<std::int64_t>(playlist[i]);

        std::vector<std::int64_t> disc_seq(n);
        for (const MapSegment& s : map)
            for (std::int64_t k = 0; k < s.frames; ++k)
                disc_seq[static_cast<std::size_t>(s.first + k)] = s.disc_seq;

        // (1) The two clocks.
        std::vector<Probe> clocks;
        for (std::size_t i : order) {
            const i128 d = grid[i] - playlist[i];
            clocks.push_back({ .frame = static_cast<std::int64_t>(i), .diff = d, .sequence = 0,
                               .off = magnitude(d) > tol });
        }
        add_runs(axis.runs, clocks, AxisRun::Test::Clocks, true, den);

        // (2) The PTS against the frame grid of each playlist discontinuity
        // sequence, from the sequence's first scored frame.
        std::vector<Probe> on_grid;
        std::optional<std::size_t> first;
        for (std::size_t i : order) {
            if (!first || disc_seq[*first] != disc_seq[i]) first = i;
            const i128 d = (static_cast<i128>(*pts[i]) - *pts[*first]) * tb_ticks -
                           static_cast<i128>(i - *first) * fd_ticks;
            on_grid.push_back({ .frame = static_cast<std::int64_t>(i), .diff = d,
                                .sequence = disc_seq[i], .off = magnitude(d) > tol });
        }
        add_runs(axis.runs, on_grid, AxisRun::Test::PlaylistGrid, true, den);
    } else {
        for (std::size_t i = 0; i < n; ++i) axis.ticks[i] = static_cast<std::int64_t>(grid[i]);

        // (2) on the report's sequences, each grid from its sequence's first
        // frame, or from its first scored frame where the report gives that
        // frame no PTS. For a reference that isn't a playlist, which signals
        // no discontinuity, a run is a warning; for a playlist without a map
        // it is a fact, since it may be a jump the playlist signals.
        std::vector<Probe> on_grid;
        std::optional<std::size_t> grid_frame;   // the frame the current grid starts at
        std::optional<std::int64_t> grid_pts;
        std::int64_t current = -1;
        for (std::size_t i : order) {
            const std::int64_t q = axis.keys[i].sequence;
            if (q != current) {
                current = q;
                const Sequence* s = static_cast<std::size_t>(q) < sequences.size()
                                        ? &sequences[static_cast<std::size_t>(q)] : nullptr;
                if (s && s->first_pts && s->first_index >= 0) {
                    grid_frame = static_cast<std::size_t>(s->first_index);
                    grid_pts   = *s->first_pts;
                } else {
                    grid_frame = i;
                    grid_pts   = *pts[i];
                }
            }
            const i128 d = (static_cast<i128>(*pts[i]) - *grid_pts) * tb_ticks -
                           (static_cast<i128>(i) - static_cast<i128>(*grid_frame)) * fd_ticks;
            on_grid.push_back({ .frame = static_cast<std::int64_t>(i), .diff = d, .sequence = q,
                                .off = magnitude(d) > tol });
        }
        add_runs(axis.runs, on_grid, AxisRun::Test::ReportGrid, reference_playlist == nullptr, den);
    }
    for (const AxisRun& r : axis.runs)
        if (r.warning) axis.warnings.push_back(run_text(r));
    return axis;
}

} // namespace timeline
