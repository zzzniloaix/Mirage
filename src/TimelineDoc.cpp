#include "TimelineDoc.h"

#include "JsonRead.h"
#include "JsonText.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <numeric>
#include <system_error>
#include <type_traits>
#include <variant>

namespace timeline {

namespace {

using i128 = __int128;
using json::number;
using json::quote;

// ── Schema version 1 ─────────────────────────────────────────────────────────

// The event vocabulary of eval/phase3-survey/design.md §5.9, by source, in its
// order (Q04 (a)).
constexpr std::array<std::string_view, 2> kVmafTypes     = { "context_start", "unpaired_frame" };
constexpr std::array<std::string_view, 2> kNetworkTypes  = { "segment_request", "playlist_request" };
constexpr std::array<std::string_view, 6> kPlaylistTypes = {
    "segment", "discontinuity", "program_date_time", "daterange", "tag", "period",
};
constexpr std::array<std::string_view, 7> kScte35Types = {
    "cue_out", "cue_out_cont", "cue_in", "ad_break", "splice_point", "message", "cancel",
};
constexpr std::array<std::string_view, 2> kPlayerTypes  = { "variant_span", "variant_switch" };
constexpr std::array<std::string_view, 3> kDecoderTypes = { "keyframe", "corrupt_packet",
                                                            "decode_error" };

// The values of version 1 that its rules fix. Placement and comparison on
// the reference's frames (D-073), the axis's two clocks and the check's two
// states (src/TimelineAxis.h); the overlap of D-078, half-open on reference
// frames, over the events of a curve's own inputs (Q28 (a)); the curve kinds
// of CurveRef, a pair's and a session's (CurveRef::Kind::Session). Design §8
// names the clocks, the checks and the roles; kOverlapScope's name, a
// curve's kind "pair" in its {"kind", "index"} form and the run tests' names
// below are WP08's, which D-092 (d) keeps (src/TimelineDoc.h); a session's
// kind "session" in the same form is WP09b's, as design §7's --curve
// session:<id> has it.
constexpr std::string_view kAxisKind       = "reference-frames";
constexpr std::string_view kPlaylistTime   = "playlist-time";
constexpr std::string_view kFrameGrid      = "frame-grid";
constexpr std::string_view kSegmentMap     = "segment-map";
constexpr std::string_view kReportOnly     = "report-only";
constexpr std::string_view kOverlapRule    = "half-open";
constexpr std::string_view kOverlapScope   = "curve-inputs";
constexpr std::string_view kPairCurve      = "pair";
constexpr std::string_view kSessionCurve   = "session";
constexpr std::string_view kReference      = "reference";
constexpr std::string_view kDistorted      = "distorted";
constexpr std::string_view kMapOk          = "ok";
constexpr std::string_view kMapRefused     = "refused: ";   // + VariantMedia::refused (D-081 (c))

// The names of the check's tests in axis.runs (AxisRun::Test).
constexpr std::array<std::string_view, 3> kRunTests = { "clocks", "playlist-grid", "report-grid" };

// The reader expands the axis's stretches into a tick and a media key per
// reference frame, and refuses an axis of more frames than this. At the
// limit the expanded values take kMaxFrames x (sizeof(std::int64_t) +
// sizeof(MediaKey)) bytes, 2^24 x (8 + 24), 512 MiB, on the 64-bit builds,
// where a MediaKey's optional PTS pads it to 24 bytes (the sum is checked in
// tests/test_timeline_doc.cpp); a document of a few hundred bytes can claim
// that much with one stretch of each, so the reader first checks that the
// stretches cover the axis's frames, then reserves the values once and
// expands each stretch into them, with no temporary copy beside them. A
// stretch's scale (D-099 (a)) changes none of this: it scales values, not
// their number. The limit also keeps a stretch's k below 2^24, so that k
// times a 64-bit step stays within 2^87 in the reader's 128-bit arithmetic,
// well inside what stretch_fits() needs.
constexpr std::int64_t kMaxFrames = std::int64_t{ 1 } << 24;

std::string_view run_test_name(AxisRun::Test test)
{
    const auto i = static_cast<std::size_t>(test);
    return i < kRunTests.size() ? kRunTests[i] : std::string_view();
}

// The rules by which a session's drops come from its pairs' curves, which
// detection.session_rules records wherever detection ran on a session's
// curve (design §8): its pairs' drops clipped to the spans that played each
// pair's variant (D-077, Q22 (c)), under design §6.3's three rules, (i) a
// clipped piece a drop only if it keeps more than N points, (ii) the pieces
// of two pairs that meet at a switch two drops, each with its own pair's
// numbers, (iii) a piece's baseline its pair's at its first point, the
// drop's; and consecutive spans of one variant clipped as one stretch, as
// D-077's clipping to the spans that played each variant reads
// (session_drops(), src/SessionPlan.h). Each value names the rule taken
// where the design names another: every piece kept, a drop merged across
// the switch, spans clipped one by one. Design §8 names the key alone: this
// form, an object of five named rules, and its names are WP09b's, which
// the owner kept (D-111 (b)).
Payload session_rules_record()
{
    const auto text = [](std::string_view s) { return Value{ std::string(s) }; };
    return {
        { "drops", text("pair-drops-clipped-to-spans") },
        { "pieces", text("more-than-min-points") },
        { "at_switch", text("two-drops") },
        { "baseline", text("pair-drop") },
        { "spans_of_one_variant", text("one-stretch") },
    };
}

// Whether `text` is a UTC time as a session's anchor is written, RFC 3339
// to the microsecond with the zone Z, YYYY-MM-DDTHH:MM:SS.ffffffZ (D-108
// (c); src/SessionFetch.h): its digits and separators in their places, as
// the fetch writes it.
bool utc_anchor(std::string_view text)
{
    constexpr std::string_view kForm = "dddd-dd-ddTdd:dd:dd.ddddddZ";
    if (text.size() != kForm.size()) return false;
    for (std::size_t i = 0; i < kForm.size(); ++i)
        if (kForm[i] == 'd' ? text[i] < '0' || text[i] > '9' : text[i] != kForm[i]) return false;
    return true;
}

// ── Values of the reference's frames ─────────────────────────────────────────

// A PTS in seconds as the analyzer computes them (DecodeCtx::seconds in
// src/VMAFAnalyzer.cpp), so that a scored frame's are the report's own; none
// without a PTS or a positive time base.
std::optional<double> pts_seconds(std::optional<std::int64_t> ticks, AVRational tb)
{
    if (!ticks || tb.num <= 0 || tb.den <= 0) return std::nullopt;
    return static_cast<double>(*ticks) * tb.num / tb.den;
}

// The reference frames a pair's analysis read: its reference sequences'
// frames in all, held within INT64_MAX, as build_axis() counts them when it
// picks the pair whose reference it takes (src/TimelineAxis.cpp).
std::int64_t reference_frames_read(const PairScores& p)
{
    i128 n = 0;
    for (const Sequence& s : p.ref_sequences) n += s.frames;
    constexpr i128 kMax = std::numeric_limits<std::int64_t>::max();
    return n <= 0 ? 0 : static_cast<std::int64_t>(std::min(n, kMax));
}

bool fits_int64(i128 v)
{
    return v >= std::numeric_limits<std::int64_t>::min() &&
           v <= std::numeric_limits<std::int64_t>::max();
}

// `a` / `b` rounded toward minus infinity, for b > 0.
i128 floor_div(i128 a, i128 b)
{
    const i128 q = a / b;
    return a % b != 0 && a < 0 ? q - 1 : q;
}

// The greatest common divisor of |a| and |b|, in 128 bits, where the
// magnitude of INT64_MIN fits; gcd(0, b) is |b|.
i128 gcd_of(i128 a, i128 b)
{
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b != 0) {
        const i128 r = a % b;
        a = b;
        b = r;
    }
    return a;
}

// A stretch's step (D-092 (a)): a rational in lowest terms with a positive
// denominator, written "num/den" as the document's other rationals are, its
// terms in 64 bits, since a frame duration in ticks of 1/tick_den s can
// pass AVRational's 32.
struct Step {
    std::int64_t num = 0;
    std::int64_t den = 1;
    bool operator==(const Step&) const = default;
};

// num / den as a Step; none for a zero denominator, or terms beyond 64 bits
// once reduced.
std::optional<Step> step_of(i128 num, i128 den)
{
    if (den == 0) return std::nullopt;
    if (den < 0) num = -num, den = -den;
    const i128 g = gcd_of(num, den);
    num /= g;
    den /= g;
    if (!fits_int64(num) || !fits_int64(den)) return std::nullopt;
    return Step{ static_cast<std::int64_t>(num), static_cast<std::int64_t>(den) };
}

// The frame duration `fd` in ticks of which there are mul_num / mul_den to
// the second, the step a stretch tries first (D-092 (a)): tick_den / 1 for
// the axis's ticks, time_base.den / time_base.num for the PTS. None without
// a frame duration, which an axis without seconds has as 0/1, or a positive
// clock.
std::optional<Step> frame_step(AVRational fd, std::int64_t mul_num, std::int64_t mul_den)
{
    if (fd.num <= 0 || fd.den <= 0 || mul_num <= 0 || mul_den <= 0) return std::nullopt;
    return step_of(static_cast<i128>(fd.num) * mul_num, static_cast<i128>(fd.den) * mul_den);
}

// The axis's ticks to one tick of the reference's time base, tick_den x
// time_base, where that is a whole number above 1 within 64 bits: the scale
// the axis's ticks try with the frame duration in PTS ticks (D-099 (a)),
// since within a segment the playlist time follows the PTS at that many of
// the axis's ticks to a PTS tick (src/TimelineAxis.h). None otherwise, as
// where the axis's ticks are the time base's own.
std::optional<std::int64_t> ticks_per_pts_tick(std::int64_t tick_den, AVRational tb)
{
    if (tick_den <= 0 || tb.num <= 0 || tb.den <= 0) return std::nullopt;
    const i128 n = static_cast<i128>(tick_den) * tb.num;
    if (n % tb.den != 0 || n / tb.den <= 1 || !fits_int64(n / tb.den)) return std::nullopt;
    return static_cast<std::int64_t>(n / tb.den);
}

// A stretch of reference frames over which a per-frame value follows a
// rational step with a phase (D-092 (a)) and a scale (D-099 (a)): frames
// first to first + frames - 1, frame first + k at
// value + scale * floor((phase + k * step.num) / step.den), where
// 0 <= phase < step.den and scale >= 1, so that frame `first` is at `value`.
// The scale is 1 but where the values are kept in a unit `scale` times finer
// than the clock their frames follow, as the playlist time's ticks are
// (ticks_per_pts_tick()) and PTS rounded to a clock coarser than their time
// base (shared_scale(); D-100 (a)). The values run monotonically in k, the
// scale being positive, so they all fit in 64 bits when the first and the
// last do (stretch_fits()).
struct Stretch {
    std::int64_t first  = 0;
    std::int64_t frames = 0;
    std::int64_t value  = 0;
    Step         step;
    std::int64_t phase  = 0;
    std::int64_t scale  = 1;
};

// Whether every value of `s` fits in 64 bits: whether its last one does
// (Stretch). With 64-bit terms and k below 2^63, phase + k * step.num and
// its quotient stay within 2^127. A quotient of 2^64 or more either way
// puts the value past 64 bits whatever the scale, which is at least 1, the
// first value being within 2^63; below that the scale times the quotient
// stays within 2^127, so that a 128-bit intermediate computes every term
// exactly.
bool stretch_fits(const Stretch& s)
{
    if (s.frames < 1) return true;
    const i128 q = floor_div(s.phase + static_cast<i128>(s.frames - 1) * s.step.num, s.step.den);
    constexpr i128 kQuotientLimit = i128{ 1 } << 64;
    if (q >= kQuotientLimit || q <= -kQuotientLimit) return false;
    return fits_int64(s.value + s.scale * q);
}

// How many of `values`, from `from` on, follow `step` and `scale` from
// values[from], and the smallest phase that all of them take. The k-th value
// follows when its difference from values[from] is `scale` times a whole d,
// and a phase p gives d when d * den <= p + k * num < (d + 1) * den, an
// interval of phases: the values follow as long as their intervals and
// [0, den - 1] have a phase in common. floor((p + k * num) / den) doesn't
// decrease as p grows, and grows by at most 1 from one phase to the next, so
// d is reached within the phases left exactly when it lies between its
// values at their two ends; checking that before the products are taken
// keeps every one of them within 2^127. The scale divides a difference and
// multiplies nothing.
struct Follow {
    std::size_t  frames = 1;
    std::int64_t phase  = 0;
};

Follow follow(std::span<const std::int64_t> values, std::size_t from, Step step, std::int64_t scale)
{
    i128 lo = 0, hi = step.den - 1;
    std::size_t k = 1;
    for (; from + k < values.size(); ++k) {
        const i128 diff = static_cast<i128>(values[from + k]) - values[from];
        if (diff % scale != 0) break;
        const i128 d  = diff / scale;
        const i128 kn = static_cast<i128>(k) * step.num;
        if (d < floor_div(lo + kn, step.den) || d > floor_div(hi + kn, step.den)) break;
        lo = std::max(lo, d * step.den - kn);
        hi = std::min(hi, (d + 1) * step.den - kn - 1);
    }
    return { .frames = k, .phase = static_cast<std::int64_t>(lo) };
}

// A step a stretch tries, with its scale (D-099 (a)).
struct Try {
    Step         step;
    std::int64_t scale = 1;
    bool operator==(const Try&) const = default;
};

// What the stretches of one kind of value try, beside what their own values
// give (stretches()): the frame durations, each in its order with its
// scale (D-092 (a), D-099 (a)), and the frame duration in the values' own
// ticks, from which D-100 (a)'s step is made; none of either without a frame
// duration.
struct Tries {
    std::vector<Try>    frame;
    std::optional<Step> own;
};

// The values from which a stretch's own candidates are made (D-100): its
// first kOwnFrames differences from its first value, or as many as there
// are. (b)'s step (values[i + m] - values[i]) / m, in units of the factor
// they share (shared_scale()), is the frame duration of the stretch's frames
// exactly when m is a multiple of that step's denominator, so that with m
// from 2 to kOwnFrames a step of any denominator up to kOwnFrames is found:
// 15015/4 at 24000/1001 fps and 3003/2 at 60000/1001 fps on MPEG-TS's 90 kHz
// clock among them (tests/test_timeline_doc.cpp). A step of a larger
// denominator isn't found so, and (a)'s step is the axis's frame duration:
// a sequence at another frame rate than the axis's whose PTS were rounded to
// a clock coarser than their time base, as an ad at 24000/1001, 30000/1001
// or 60000/1001 fps on a millisecond clock, whose frame is 1001/24, 1001/30
// or 1001/60 ms, follows no step tried over more than part of its frames,
// so that its stretches grow with its frames. Neither part of D-100 names
// that case, which the owner kept as built (D-101 (a)).
constexpr std::size_t kOwnFrames = 8;

// The factor the differences of values[from + 1] to values[from +
// kOwnFrames] from values[from] share, their greatest common divisor, where
// it is above 1 and within 64 bits; 1 otherwise, as where the values follow
// their own ticks. Where PTS were rounded to a clock coarser than their time
// base it is that clock's tick in the values' ticks, 90 PTS ticks for a
// millisecond on the 90 kHz clock, or a multiple of it where a frame is so
// close to a whole number of the clock's ticks that the first differences
// are all multiples of more: on a clock of 1/600 s, 150 PTS ticks, a frame
// at 24000/1001, 30000/1001 and 60000/1001 fps is 1001/40, 1001/50 and
// 1001/100 of its ticks, and the first 8 differences are multiples of 25,
// 20 and 10 of them (tests/test_timeline_doc.cpp), a factor stretches()
// refines. For the playlist time's ticks, which follow the PTS within a
// segment, it is that times the axis's ticks to a PTS tick (D-100 (a)).
std::int64_t shared_scale(std::span<const std::int64_t> values, std::size_t from)
{
    i128 g = 0;
    for (std::size_t k = 1; k <= kOwnFrames && from + k < values.size(); ++k)
        g = gcd_of(g, static_cast<i128>(values[from + k]) - values[from]);
    return g > 1 && fits_int64(g) ? static_cast<std::int64_t>(g) : 1;
}

// The stretches of `values`, the first of them at frame `offset` (D-092
// (a), D-099, D-100). A stretch tries, in this order:
// - the steps of `tries.frame`, each a frame duration with its scale: the
//   frame duration in the values' own ticks with the scale 1, and for the
//   axis's ticks the frame duration in PTS ticks with the axis's ticks to a
//   PTS tick as the scale (D-099 (a));
// - the whole step, the difference between its first two values, with the
//   scale 1;
// - where its first values' differences share a factor G above 1
//   (shared_scale()), the frame duration in units of G of the values' ticks
//   with the scale G (D-100 (a)): for PTS rounded to a clock of G PTS ticks,
//   the frame duration in units of that clock, and for the playlist time's
//   ticks the same with G the clock's tick times the axis's ticks to a PTS
//   tick. Where it stops at a value whose difference from the first the
//   factor doesn't divide, it is tried again in units of the factor the two
//   share, while that is above 1, so that where G, from the first values
//   alone, is a multiple of the clock's tick, the tick itself is tried, a
//   scale that divides every difference of the stretch, as D-100 (a) has it;
// - the steps its own first values give, (values[i + m] - values[i]) / (m x
//   G) for m from 2 to kOwnFrames, with the scale G, or 1 where they share no
//   factor (D-100 (b)): for a sequence at another frame rate than the
//   axis's, its own frame duration.
// It takes the one that covers the most frames, the first tried on a tie,
// so that the axis's frame duration wins every tie, and D-100's steps win
// only by covering more frames than the frame durations and the whole step,
// WP08b's forms being kept otherwise (D-099 (b)); it ends before the first
// value that doesn't follow, with the smallest phase its values take, and a
// stretch of one value has the step 0/1, the phase 0 and the scale 1 (D-099
// (b)). A step of D-100's that is a whole number, in units of G, is no
// candidate: it covers no more frames than the whole step, nor, for (a)'s,
// than the frame duration in the values' own ticks, both tried before it.
// Without a frame duration the steps are whole (D-099 (b)). Of two frame
// durations that cover the same frames the first tried is taken, as D-100
// (c) keeps it. Whatever the values, each comes back exactly, and the same
// values give the same stretches. Each step tried is followed only as far as
// its values do, and a stretch tries at most kOwnFrames + 2 steps beside
// (a)'s, whose factor at least halves each time, a proper divisor of the one
// before it, so that they number fewer than 64: the writer stays linear in
// the frames.
std::vector<Stretch> stretches(std::span<const std::int64_t> values, const Tries& tries,
                               std::int64_t offset)
{
    std::vector<Stretch> out;
    // The steps the stretch at hand has tried, each with the frames it
    // follows, so that none is followed twice.
    std::vector<std::pair<Try, std::size_t>> tried;
    const std::size_t n = values.size();
    for (std::size_t i = 0; i < n;) {
        Stretch s{ .first = offset + static_cast<std::int64_t>(i), .frames = 1, .value = values[i] };
        tried.clear();
        // Follows `t` from values[i], keeps it where it covers more frames
        // than every step tried before it, and gives the frames it covers.
        const auto take = [&](const Try& t) -> std::size_t {
            for (const auto& [before, frames] : tried)
                if (before == t) return frames;
            const Follow f = follow(values, i, t.step, t.scale);
            tried.emplace_back(t, f.frames);
            if (static_cast<std::int64_t>(f.frames) > s.frames) {
                s.frames = static_cast<std::int64_t>(f.frames);
                s.step   = t.step;
                s.phase  = f.phase;
                s.scale  = t.scale;
            }
            return f.frames;
        };
        for (const Try& t : tries.frame) take(t);
        if (i + 1 < n && fits_int64(static_cast<i128>(values[i + 1]) - values[i]))
            take({ .step = { .num = values[i + 1] - values[i], .den = 1 } });
        if (tries.own && i + 1 < n) {
            // G divides each of the differences (b)'s steps are made of,
            // which shared_scale() takes them from.
            const std::int64_t g = shared_scale(values, i);
            // (a)'s, in units of G and then, wherever it stops at a value
            // whose difference from the first the factor doesn't divide, in
            // units of the factor the two share, while that is above 1: a
            // proper divisor, so that the factor at least halves each time.
            // It ends where the step stops at a value the factor divides,
            // which names no smaller one, and where the step is whole, as it
            // then is in units of every divisor of the factor, holding what
            // the frame duration in the values' own ticks holds.
            for (std::int64_t factor = g; factor > 1;) {
                const std::optional<Step> a =
                    step_of(tries.own->num, static_cast<i128>(tries.own->den) * factor);
                if (!a || a->den == 1) break;
                const std::size_t end = i + take({ .step = *a, .scale = factor });
                if (end >= n) break;
                const i128 diff = static_cast<i128>(values[end]) - values[i];
                if (diff % factor == 0) break;
                factor = static_cast<std::int64_t>(gcd_of(factor, diff));
            }
            for (std::size_t m = 2; m <= kOwnFrames && i + m < n; ++m)
                if (const std::optional<Step> b =
                        step_of((static_cast<i128>(values[i + m]) - values[i]) / g, static_cast<i128>(m));
                    b && b->den > 1)
                    take({ .step = *b, .scale = g });
        }
        out.push_back(s);
        i += static_cast<std::size_t>(s.frames);
    }
    return out;
}

// A stretch of media keys: frames of one sequence whose PTS follow a step as
// a Stretch's values do, or are all unknown, with the step 0/1, the phase 0
// and the scale 1 (D-092 (a), D-099 (a)).
struct KeyStretch {
    std::int64_t first  = 0;
    std::int64_t frames = 0;
    std::int32_t sequence = 0;
    std::optional<std::int64_t> pts;   // the first frame's; nullopt: every frame's unknown
    Step         step;
    std::int64_t phase  = 0;
    std::int64_t scale  = 1;
};

std::vector<KeyStretch> key_stretches(const std::vector<MediaKey>& keys, const Tries& tries)
{
    std::vector<KeyStretch> out;
    std::vector<std::int64_t> pts;
    const std::size_t n = keys.size();
    for (std::size_t i = 0; i < n;) {
        // The frames from i on of its sequence whose PTS are known, or
        // unknown, as its own is.
        std::size_t j = i + 1;
        while (j < n && keys[j].sequence == keys[i].sequence &&
               keys[j].pts.has_value() == keys[i].pts.has_value())
            ++j;
        if (!keys[i].pts) {
            out.push_back({ .first = static_cast<std::int64_t>(i),
                            .frames = static_cast<std::int64_t>(j - i), .sequence = keys[i].sequence });
        } else {
            pts.clear();
            for (std::size_t k = i; k < j; ++k) pts.push_back(*keys[k].pts);
            for (const Stretch& s : stretches(pts, tries, static_cast<std::int64_t>(i)))
                out.push_back({ .first = s.first, .frames = s.frames, .sequence = keys[i].sequence,
                                .pts = s.value, .step = s.step, .phase = s.phase, .scale = s.scale });
        }
        i = j;
    }
    return out;
}

// ── A pair's number ──────────────────────────────────────────────────────────

// The pairs' numbers of the variants of `t`, the report's pairs[].index of
// each (D-092 (b)), in ascending order: the order of the report's complete
// pairs, and so of its curves (src/LadderReportRead.h), by which a timeline
// without curves, one read back from a document, places a curve reference
// (pair_number(), curve_of_pair()).
std::vector<int> pair_numbers(const Timeline& t)
{
    std::vector<int> out;
    for (const Variant& v : t.variants)
        if (v.pair >= 0) out.push_back(v.pair);
    std::sort(out.begin(), out.end());
    return out;
}

// ── Writing ──────────────────────────────────────────────────────────────────

std::string opt_integer(const std::optional<std::int64_t>& v)
{
    return v ? std::format("{}", *v) : std::string("null");
}

std::string opt_number(const std::optional<double>& v) { return v ? number(*v) : "null"; }

std::string opt_string(const std::optional<std::string>& v) { return v ? quote(*v) : "null"; }

// An index into another array, -1 for none: null.
std::string index_json(int i) { return i < 0 ? std::string("null") : std::format("{}", i); }

// A rational exactly, as "num/den": time bases, frame rates, the frame
// duration and the margins, a margin of 1/3 s having no exact decimal. The
// form is WP08's, which D-092 (d) keeps, and the stretches' steps take it
// too (D-092 (a)).
std::string rational_json(AVRational q) { return quote(std::format("{}/{}", q.num, q.den)); }

std::string step_json(Step s) { return quote(std::format("{}/{}", s.num, s.den)); }

std::string value_json(const Value& v);

std::string payload_json(const Payload& p)
{
    std::string out = "{";
    for (std::size_t i = 0; i < p.size(); ++i)
        out += std::format("{}{}: {}", i == 0 ? "" : ", ", quote(p[i].first), value_json(p[i].second));
    return out + "}";
}

std::string value_json(const Value& v)
{
    if (const bool* b = std::get_if<bool>(&v.v)) return *b ? "true" : "false";
    if (const auto* i = std::get_if<std::int64_t>(&v.v)) return std::format("{}", *i);
    if (const auto* d = std::get_if<double>(&v.v)) return number(*d);
    if (const auto* s = std::get_if<std::string>(&v.v)) return quote(*s);
    if (const auto* l = std::get_if<Value::List>(&v.v)) {
        std::string out = "[";
        for (std::size_t i = 0; i < l->size(); ++i)
            out += (i == 0 ? "" : ", ") + value_json((*l)[i]);
        return out + "]";
    }
    if (const auto* o = std::get_if<Value::Object>(&v.v)) return payload_json(*o);
    return "null";
}

// `items` as a JSON array, one to a line at `indent`, its closing bracket
// two spaces less; "[]" when there are none.
std::string lines(const std::vector<std::string>& items, std::string_view indent)
{
    if (items.empty()) return "[]";
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        out += i == 0 ? "\n" : ",\n";
        out += indent;
        out += items[i];
    }
    out += '\n';
    out += indent.substr(0, indent.size() >= 2 ? indent.size() - 2 : 0);
    return out + "]";
}

std::string strings_json(const std::vector<std::string>& list, std::string_view indent)
{
    std::vector<std::string> items;
    items.reserve(list.size());
    for (const std::string& s : list) items.push_back(quote(s));
    return lines(items, indent);
}

// A curve as {"kind", "index"}, where design §7's --curve option writes
// "pair:<k>" or "session:<id>": WP08's form, which D-092 (d) keeps. A pair's
// index is the pair's number, the ladder report's own pairs[].index (D-092
// (b)), that of the curve's variant (pair_number()), where CurveRef::index
// is the curve's place in Timeline::curves; null for a curve that names no
// pair, which the schema and the reader refuse (src/TimelineDoc.h). A
// session's, {"kind": "session", "index": s}, is its place in the document's
// sessions, as CurveRef::index is in Timeline::sessions, and as a variant is
// named by its place in variants; null for a session the timeline doesn't
// have, which the reader refuses.
std::string curve_json(const Timeline& t, CurveRef curve)
{
    if (curve.kind == CurveRef::Kind::Session) {
        const bool there = curve.index >= 0 && static_cast<std::size_t>(curve.index) < t.sessions.size();
        return std::format("{{\"kind\": {}, \"index\": {}}}", quote(kSessionCurve),
                           there ? std::format("{}", curve.index) : std::string("null"));
    }
    const std::optional<int> pair = pair_number(t, curve);
    return std::format("{{\"kind\": {}, \"index\": {}}}", quote(kPairCurve),
                       pair ? std::format("{}", *pair) : std::string("null"));
}

// A session (design §8): its id, its plan's name, the UTC anchor of its wall
// clock, null when its files weren't fetched (D-108 (c)), and its spans in
// play order, each as Span holds it, in design §8's key order.
std::string session_json(const Session& s)
{
    std::vector<std::string> spans;
    spans.reserve(s.spans.size());
    for (const Span& p : s.spans)
        spans.push_back(std::format("{{\"variant\": {}, \"entry\": {}, \"first_seq\": {}, \"last_seq\": {}, "
                                    "\"ref_index\": {}, \"ref_end\": {}, \"scored\": {}}}",
                                    index_json(p.variant), p.entry, p.first_seq, p.last_seq, p.ref_index,
                                    p.ref_end, p.scored ? "true" : "false"));
    std::string out = "{\n";
    out += std::format("      \"id\": {},\n", quote(s.id));
    out += std::format("      \"name\": {},\n", quote(s.name));
    out += std::format("      \"wallclock_anchor_utc\": {},\n", opt_string(s.wallclock_anchor_utc));
    out += "      \"spans\": " + lines(spans, "        ") + "\n";
    return out + "    }";
}

std::string file_json(const FileRef& f)
{
    return std::format("{{\"path\": {}, \"sha256\": {}}}", quote(f.path), quote(f.sha256));
}

std::string report_json(const ReportInfo& r)
{
    return std::format(
        "{{\"path\": {}, \"sha256\": {}, \"scores_sha256\": {}, \"status\": {}, "
        "\"reference_mode\": {}, \"model\": {}, \"model_source\": {}, \"model_sha256\": {}, "
        "\"scaler\": {}, \"libvmaf_version\": {}, \"libvmaf_package_version\": {}, "
        "\"master\": {{\"url\": {}, \"location\": {}, \"sha256\": {}}}}}",
        quote(r.path), quote(r.sha256), quote(r.scores_sha256), quote(r.status),
        quote(r.reference_mode), quote(r.model), quote(r.model_source),
        r.model_sha256.empty() ? std::string("null") : quote(r.model_sha256), quote(r.scaler),
        quote(r.libvmaf_version), quote(r.libvmaf_package_version), quote(r.master_url),
        quote(r.master_location), quote(r.master_sha256));
}

std::string axis_json(const Axis& a, const ReferenceInfo& ref)
{
    std::string sequences = "[";
    for (std::size_t i = 0; i < ref.sequences.size(); ++i) {
        const Sequence& s = ref.sequences[i];
        sequences += std::format(
            "{}{{\"first_index\": {}, \"first_pts\": {}, \"first_pts_ticks\": {}, \"frames\": {}}}",
            i == 0 ? "" : ", ", s.first_index, opt_number(pts_seconds(s.first_pts, a.time_base)),
            opt_integer(s.first_pts), s.frames);
    }
    sequences += "]";

    // The per-frame values as stretches with a rational step, a phase and a
    // scale (D-092 (a), D-099 (a)), in WP08's members with those three
    // added. Each tries the frame duration in its values' own ticks first,
    // with the scale 1: ticks of 1/tick_den s for the seconds, of the
    // reference's time base for the PTS. The seconds then try the frame
    // duration in PTS ticks with the scale of the axis's ticks to a PTS tick,
    // where that is a whole number above 1: within a segment the playlist
    // time follows the PTS (src/TimelineAxis.h), at 8 or 4 of the axis's
    // ticks to a PTS tick at 24000/1001 and 60000/1001 fps on MPEG-TS's 90 kHz
    // clock with EXTINF in thousandths, and at a multiple of that where an
    // EXTINF needs finer ticks, the scale following it. Both kinds then try
    // a whole step, and last what their own values give (stretches()): the
    // frame duration on a clock coarser than their ticks, with its tick as
    // the scale, for PTS rounded to such a clock, as a millisecond one or
    // one of 1/600 s written on the 90 kHz clock (D-100 (a)), and the step
    // the stretch's own first values give, for a sequence at another frame
    // rate than the reference's, as an ad after its discontinuity can be
    // (design §4.2; D-100 (b)). Without a frame duration both take whole
    // steps (D-099 (b)).
    //
    // Where the two frame durations hold the same frames, as wherever a
    // frame is a whole number of PTS ticks, the first, tried first, is
    // taken: 6006/1 with the scale 1 at 30000/1001 fps on the 90 kHz clock,
    // not 3003/1 with the scale 2, as D-100 (c) keeps it.
    const std::optional<Step> frame_ticks = frame_step(a.frame_duration, a.tick_den, 1);
    const std::optional<Step> frame_pts =
        frame_step(a.frame_duration, a.time_base.den, a.time_base.num);
    const std::optional<std::int64_t> per_pts_tick = ticks_per_pts_tick(a.tick_den, a.time_base);
    Tries tick_tries, key_tries;
    tick_tries.own = frame_ticks;
    key_tries.own  = frame_pts;
    if (frame_ticks) tick_tries.frame.push_back({ .step = *frame_ticks });
    if (frame_pts && per_pts_tick)
        tick_tries.frame.push_back({ .step = *frame_pts, .scale = *per_pts_tick });
    if (frame_pts) key_tries.frame.push_back({ .step = *frame_pts });
    std::vector<std::string> ticks;
    for (const Stretch& s : stretches(a.ticks, tick_tries, 0))
        ticks.push_back(std::format("{{\"first\": {}, \"frames\": {}, \"first_tick\": {}, "
                                    "\"step\": {}, \"phase\": {}, \"scale\": {}}}",
                                    s.first, s.frames, s.value, step_json(s.step), s.phase, s.scale));
    std::vector<std::string> keys;
    for (const KeyStretch& s : key_stretches(a.keys, key_tries))
        keys.push_back(std::format("{{\"first\": {}, \"frames\": {}, \"sequence\": {}, "
                                   "\"first_pts_ticks\": {}, \"step\": {}, \"phase\": {}, "
                                   "\"scale\": {}}}",
                                   s.first, s.frames, s.sequence, opt_integer(s.pts),
                                   step_json(s.step), s.phase, s.scale));
    // Every run of the check, structured, with its warning flag: design §4.2
    // writes each run to axis.warnings, whose texts stay there, and lists the
    // runs of a playlist whose map was refused as facts without giving them
    // a place. axis.runs is WP08's addition to D-072's layout for both,
    // which D-092 (d) keeps (src/TimelineDoc.h).
    std::vector<std::string> runs;
    for (const AxisRun& r : a.runs)
        runs.push_back(std::format("{{\"test\": {}, \"sequence\": {}, \"first\": {}, \"last\": {}, "
                                   "\"frames\": {}, \"largest_at\": {}, \"largest\": {}, "
                                   "\"warning\": {}}}",
                                   quote(run_test_name(r.test)), r.sequence, r.first, r.last,
                                   r.frames, r.largest_at, number(r.largest),
                                   r.warning ? "true" : "false"));

    std::string out = "{\n";
    out += std::format("    \"kind\": {},\n", quote(a.kind));
    out += std::format("    \"seconds\": {},\n", quote(a.clock));
    out += std::format("    \"frame_duration\": {},\n", rational_json(a.frame_duration));
    out += std::format("    \"frames\": {},\n", a.frames);
    out += std::format("    \"reference\": {{\"url\": {}, \"time_base\": {}, \"frame_rate\": {}, "
                       "\"sequences\": {}}},\n",
                       quote(ref.url), rational_json(a.time_base), rational_json(ref.frame_rate),
                       sequences);
    out += std::format("    \"checked\": {},\n", quote(a.checked));
    out += std::format("    \"tick_den\": {},\n", a.tick_den);
    out += "    \"ticks\": " + lines(ticks, "      ") + ",\n";
    out += "    \"keys\": " + lines(keys, "      ") + ",\n";
    out += "    \"runs\": " + lines(runs, "      ") + ",\n";
    out += "    \"warnings\": " + strings_json(a.warnings, "      ") + "\n";
    return out + "  }";
}

std::string variant_json(const Variant& v)
{
    std::string entries = "[";
    for (std::size_t i = 0; i < v.entries.size(); ++i)
        entries += std::format("{}{}", i == 0 ? "" : ", ", v.entries[i]);
    entries += "]";
    // A value a variant doesn't have is null (D-092 (c)): the BANDWIDTH of
    // one without entries, a mezzanine; a rank of 0, none
    // (src/TimelineModel.h); the playlist of one not read from a playlist,
    // which has neither a location nor a SHA-256, as WP08 decided it, and
    // then its segment map, which it can't have either. The reader refuses
    // a playlist with neither, so that a document read back is written again
    // to the same bytes. Such a variant keeps Variant's default segment_map,
    // "ok"; any other value is written as it is given, for the reader to
    // refuse, as D-099 (b) keeps it (src/TimelineDoc.h).
    const auto rank = [](int r) { return r > 0 ? std::format("{}", r) : std::string("null"); };
    const bool playlist = !v.playlist_location.empty() || !v.playlist_sha256.empty();
    const std::string map = !playlist && v.segment_map == kMapOk ? std::string("null")
                                                                  : quote(v.segment_map);
    return std::format(
        "{{\"id\": {}, \"role\": {}, \"pair\": {}, \"entries\": {}, \"url\": {}, \"bandwidth\": {}, "
        "\"resolution\": {}, \"codecs\": {}, \"rank_from_top\": {}, \"rank_from_bottom\": {}, "
        "\"playlist\": {}, \"segment_map\": {}}}",
        v.index, quote(v.pair < 0 ? kReference : kDistorted), index_json(v.pair), entries,
        quote(v.url), v.entries.empty() ? std::string("null") : std::format("{}", v.bandwidth),
        opt_string(v.resolution), opt_string(v.codecs), rank(v.rank_from_top),
        rank(v.rank_from_bottom),
        playlist ? std::format("{{\"url\": {}, \"location\": {}, \"sha256\": {}}}", quote(v.url),
                               quote(v.playlist_location), quote(v.playlist_sha256))
                 : std::string("null"),
        map);
}

std::string event_json(const Event& e, const Axis& a)
{
    const std::optional<double> wallclock =
        e.wallclock_us ? std::optional<double>(static_cast<double>(*e.wallclock_us) / 1e6)
                       : std::nullopt;
    return std::format(
        "{{\"id\": {}, \"source\": {}, \"type\": {}, \"variant\": {}, \"session\": {}, "
        "\"segment\": {}, \"ref_index\": {}, \"ref_end\": {}, \"t\": {}, \"t_end\": {}, "
        "\"seq\": {}, \"pts\": {}, \"pts_ticks\": {}, \"wallclock\": {}, \"payload\": {}}}",
        quote(e.id), quote(source_name(e.source)), quote(e.type), index_json(e.variant),
        index_json(e.session), opt_integer(e.segment), e.ref_index, opt_integer(e.ref_end),
        number(a.seconds(e.ref_index)),
        e.ref_end ? number(a.seconds(*e.ref_end)) : std::string("null"), e.key.sequence,
        opt_number(pts_seconds(e.key.pts, a.time_base)), opt_integer(e.key.pts),
        opt_number(wallclock), payload_json(e.payload));
}

std::string detection_json(const std::optional<Detection>& d, const Timeline& t)
{
    if (!d || !d->options.given()) return "null";
    std::string out = "{";
    for (const auto& [name, value] : detection_record(d->options))
        out += std::format("{}: {}, ", quote(name), value_json(value));
    // The curves detection ran on, a pair's by its pair's number (D-092
    // (b)), a session's by its place among the sessions; and session_rules,
    // null where none of them is a session's, as WP08 wrote a ladder's and
    // D-092 (d) keeps it (src/TimelineDoc.h), and otherwise the rules that
    // make a session's drops of its pairs' (session_rules_record(), a form
    // D-111 (b) keeps).
    out += "\"curves\": [";
    bool session = false;
    for (std::size_t i = 0; i < d->curves.size(); ++i) {
        out += (i == 0 ? "" : ", ") + curve_json(t, d->curves[i]);
        session = session || d->curves[i].kind == CurveRef::Kind::Session;
    }
    return out + "], \"session_rules\": " + (session ? payload_json(session_rules_record()) : std::string("null")) +
           "}";
}

// The overlap record, its scope named "curve-inputs" by WP08, a name D-092
// (d) keeps (src/TimelineDoc.h).
std::string overlap_json(const QueryOptions& q)
{
    return std::format("{{\"rule\": {}, \"margin_before\": {}, \"margin_after\": {}, \"scope\": {}}}",
                       quote(kOverlapRule), rational_json(q.margin_before),
                       rational_json(q.margin_after), quote(kOverlapScope));
}

// An attribution, its overlapping_events one group per source in section
// 3.1's order (D-078), all six written, empty ones included, which design
// §8 leaves open: WP08's choice, which D-092 (d) keeps (src/TimelineDoc.h).
std::string attribution_json(const Attribution& a, const Timeline& t)
{
    const Axis& axis = t.axis;
    std::string groups;
    for (std::size_t s = 0; s < kSourceCount; ++s) {
        std::vector<std::string> found;
        for (const Found& f : a.by_source[s])
            found.push_back(std::format(
                "{{\"event\": {}, \"offset\": {}, \"offset_end\": {}, \"offset_frames\": {}, "
                "\"offset_end_frames\": {}}}",
                f.event < t.events.size() ? quote(t.events[f.event].id) : std::string("null"),
                number(f.offset_s), opt_number(f.offset_end_s), f.offset_frames,
                opt_integer(f.offset_end_frames)));
        groups += std::format("{}{{\"source\": {}, \"events\": {}}}",
                              s == 0 ? "\n        " : ",\n        ",
                              quote(source_name(static_cast<Source>(s))), lines(found, "          "));
    }
    std::string out = "{\n";
    out += "      \"curve\": " + curve_json(t, a.curve) + ",\n";
    out += std::format("      \"detected\": {},\n", a.detected ? "true" : "false");
    out += std::format("      \"interval\": {{\"ref_index\": {}, \"ref_end\": {}, \"t\": {}, "
                       "\"t_end\": {}, \"points\": {}}},\n",
                       a.interval.ref_index, a.interval.ref_end,
                       number(axis.seconds(a.interval.ref_index)),
                       number(axis.seconds(a.interval.ref_end)), a.points);
    out += std::format("      \"baseline\": {},\n", opt_number(a.baseline));
    out += std::format("      \"min\": {},\n", opt_number(a.min));
    out += std::format("      \"mean\": {},\n", opt_number(a.mean));
    out += std::format("      \"drop_magnitude\": {},\n", opt_number(a.drop_magnitude));
    out += std::format("      \"mean_drop\": {},\n", opt_number(a.mean_drop));
    out += "      \"overlapping_events\": [" + groups + "\n      ]\n";
    return out + "    }";
}

// ── Reading ──────────────────────────────────────────────────────────────────

using Keys = std::span<const std::string_view>;

// The keys of each object of a version-1 document, in the order to_json()
// writes them: design §8's, session_plan and sessions among them, which a
// ladder's document holds as null and empty (src/TimelineDoc.h).
constexpr std::array<std::string_view, 17> kDocKeys = {
    "schema", "schema_version", "tool", "command", "ffmpeg_libraries", "options", "config_file",
    "ladder_report", "session_plan", "axis", "variants", "sessions", "timeline", "detection",
    "overlap", "attributions", "warnings",
};
constexpr std::array<std::string_view, 2>  kFileKeys   = { "path", "sha256" };
constexpr std::array<std::string_view, 12> kReportKeys = {
    "path", "sha256", "scores_sha256", "status", "reference_mode", "model", "model_source",
    "model_sha256", "scaler", "libvmaf_version", "libvmaf_package_version", "master",
};
constexpr std::array<std::string_view, 3>  kMasterKeys = { "url", "location", "sha256" };
constexpr std::array<std::string_view, 11> kAxisKeys   = {
    "kind", "seconds", "frame_duration", "frames", "reference", "checked", "tick_den", "ticks",
    "keys", "runs", "warnings",
};
constexpr std::array<std::string_view, 4> kReferenceKeys = { "url", "time_base", "frame_rate",
                                                             "sequences" };
constexpr std::array<std::string_view, 4> kSequenceKeys = { "first_index", "first_pts",
                                                            "first_pts_ticks", "frames" };
constexpr std::array<std::string_view, 6> kTickKeys = { "first", "frames", "first_tick", "step",
                                                        "phase", "scale" };
constexpr std::array<std::string_view, 7> kKeyKeys  = { "first", "frames", "sequence",
                                                        "first_pts_ticks", "step", "phase", "scale" };
constexpr std::array<std::string_view, 8> kRunKeys  = {
    "test", "sequence", "first", "last", "frames", "largest_at", "largest", "warning",
};
constexpr std::array<std::string_view, 12> kVariantKeys = {
    "id", "role", "pair", "entries", "url", "bandwidth", "resolution", "codecs", "rank_from_top",
    "rank_from_bottom", "playlist", "segment_map",
};
constexpr std::array<std::string_view, 3>  kPlaylistKeys = { "url", "location", "sha256" };
constexpr std::array<std::string_view, 4>  kSessionKeys  = { "id", "name", "wallclock_anchor_utc", "spans" };
constexpr std::array<std::string_view, 7>  kSpanKeys     = { "variant", "entry", "first_seq", "last_seq",
                                                            "ref_index", "ref_end", "scored" };
constexpr std::array<std::string_view, 15> kEventKeys    = {
    "id", "source", "type", "variant", "session", "segment", "ref_index", "ref_end", "t", "t_end",
    "seq", "pts", "pts_ticks", "wallclock", "payload",
};
constexpr std::array<std::string_view, 8> kDetectionKeys = {
    "threshold", "window", "min_points", "baseline", "warm_up", "context_first", "curves",
    "session_rules",
};
constexpr std::array<std::string_view, 2> kCurveKeys   = { "kind", "index" };
constexpr std::array<std::string_view, 4> kOverlapKeys = { "rule", "margin_before", "margin_after",
                                                           "scope" };
constexpr std::array<std::string_view, 9> kAttributionKeys = {
    "curve", "detected", "interval", "baseline", "min", "mean", "drop_magnitude", "mean_drop",
    "overlapping_events",
};
constexpr std::array<std::string_view, 5> kIntervalKeys = { "ref_index", "ref_end", "t", "t_end",
                                                            "points" };
constexpr std::array<std::string_view, 2> kGroupKeys = { "source", "events" };
constexpr std::array<std::string_view, 5> kFoundKeys = { "event", "offset", "offset_end",
                                                         "offset_frames", "offset_end_frames" };

// A value's place in the document, made on the stack as the reader goes down
// into it and turned into text only for a message: "timeline[3].payload".
struct Path {
    const Path*      parent  = nullptr;   // nullptr: the document itself
    std::string_view key;
    std::size_t      index   = 0;
    bool             element = false;

    Path member(std::string_view k) const { return { this, k, 0, false }; }
    Path item(std::size_t i) const { return { this, {}, i, true }; }
};

std::string text_of(const Path& p)
{
    std::vector<const Path*> chain;
    for (const Path* q = &p; q->parent; q = q->parent) chain.push_back(q);
    if (chain.empty()) return "the document";
    std::string out;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if ((*it)->element) {
            out += std::format("[{}]", (*it)->index);
        } else {
            if (!out.empty()) out += '.';
            out += (*it)->key;
        }
    }
    return out;
}

// "num/den" with both integers of type T; nullopt otherwise. std::from_chars
// also takes a term with a leading zero, and "-0", which rational_json() and
// step_json() never write: Reader::terms() refuses them.
template <typename T>
std::optional<std::pair<T, T>> terms_of(std::string_view s)
{
    const std::size_t slash = s.find('/');
    if (slash == std::string_view::npos) return std::nullopt;
    std::pair<T, T> r{ 0, 1 };
    const char* mid = s.data() + slash;
    const char* end = s.data() + s.size();
    const auto [p1, e1] = std::from_chars(s.data(), mid, r.first);
    const auto [p2, e2] = std::from_chars(mid + 1, end, r.second);
    if (e1 != std::errc() || p1 != mid || e2 != std::errc() || p2 != end) return std::nullopt;
    return r;
}

// A JSON value as a payload's value: the same alternatives (src/JsonRead.h).
Value value_of(const json::Value& v)
{
    Value out;
    std::visit(
        [&out](const auto& x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, json::Value::Array>) {
                Value::List list;
                list.reserve(x.size());
                for (const json::Value& e : x) list.push_back(value_of(e));
                out.v = std::move(list);
            } else if constexpr (std::is_same_v<T, json::Value::Object>) {
                Value::Object object;
                object.reserve(x.size());
                for (const auto& [k, e] : x) object.emplace_back(k, value_of(e));
                out.v = std::move(object);
            } else {
                out.v = x;
            }
        },
        v.v);
    return out;
}

// Reads a document's tree into a Timeline, its attributions and a DocInfo.
// Each function returns false with err saying why on the first thing wrong.
class Reader {
public:
    std::string err;

    bool document(const json::Value& doc, Timeline& t, std::vector<Attribution>& attributions,
                  DocInfo& info)
    {
        const Path root;
        if (!doc.object())
            return fail(std::format("not a timeline document: the text holds {}, not an object",
                                    json::kind_name(doc)));
        const json::Value* schema = doc.find("schema");
        if (!schema || !schema->string() || *schema->string() != kDocSchema)
            return fail(std::format("not a timeline document: its schema is {}, not {}",
                                    schema ? shown(*schema) : std::string("missing"),
                                    quote(kDocSchema)));
        const json::Value* version = doc.find("schema_version");
        if (!version) return fail("no schema_version: a timeline document has one (D-072)");
        if (!version->integer())
            return fail(std::format("schema_version is {}, not an integer (D-072)",
                                    json::kind_name(*version)));
        if (*version->integer() != kDocVersion)
            return fail(std::format("schema_version is {}, which this reader doesn't read: it reads "
                                    "schema version {} only (D-072)",
                                    *version->integer(), kDocVersion));
        if (!exact(doc, kDocKeys, root)) return false;

        if (!string(m(doc, "tool"), root.member("tool"), info.tool) ||
            !string(m(doc, "command"), root.member("command"), info.command) ||
            !libraries(m(doc, "ffmpeg_libraries"), root.member("ffmpeg_libraries"),
                       info.ffmpeg_libraries))
            return false;
        const Path options_at = root.member("options");
        const json::Value& options = m(doc, "options");
        if (!options.object()) return wrong(options, options_at, "an object");
        info.options = std::get<Value::Object>(value_of(options).v);

        const json::Value& config = m(doc, "config_file");
        if (!config.is_null()) {
            FileRef f;
            if (!file(config, root.member("config_file"), f)) return false;
            info.config_file = std::move(f);
        }
        if (!report(m(doc, "ladder_report"), root.member("ladder_report"), info.report) ||
            !session_plan(m(doc, "session_plan"), root.member("session_plan"), info.session_plan) ||
            !axis(m(doc, "axis"), root.member("axis"), t.axis, info.reference) ||
            !variants(m(doc, "variants"), root.member("variants"), t) ||
            !session_list(m(doc, "sessions"), root.member("sessions"), info.session_plan.has_value(), t) ||
            !events(m(doc, "timeline"), root.member("timeline"), t) ||
            !detection(m(doc, "detection"), root.member("detection"), t, info.detection) ||
            !overlap(m(doc, "overlap"), root.member("overlap"), info.overlap) ||
            !attribution_list(m(doc, "attributions"), root.member("attributions"), t, info.overlap,
                              info.detection.has_value(), attributions) ||
            !strings(m(doc, "warnings"), root.member("warnings"), t.warnings))
            return false;
        return true;
    }

private:
    // The events by id, once Timeline::build_index() has put them in order.
    std::map<std::string, std::size_t, std::less<>> ids_;

    bool fail(std::string what)
    {
        err = std::move(what);
        return false;
    }

    // A value as the document gives it, for a message.
    static std::string shown(const json::Value& v)
    {
        if (const std::string* s = v.string()) return quote(*s);
        return std::string(json::kind_name(v));
    }

    // `v` is an object whose keys are `list`'s, every one and no other. That
    // strictness within version 1, with every value in seconds recomputed,
    // is WP08's, which D-092 (d) keeps (src/TimelineDoc.h): any later key
    // takes another schema_version, where LadderReportRead passes over keys
    // it doesn't know in later report layouts.
    bool exact(const json::Value& v, Keys list, const Path& at)
    {
        const json::Value::Object* o = v.object();
        if (!o) return wrong(v, at, "an object");
        for (std::string_view k : list)
            if (!v.find(k))
                return fail(std::format("no {}: schema version {} has it", text_of(at.member(k)),
                                        kDocVersion));
        for (const json::Value::Member& member : *o)
            if (std::find(list.begin(), list.end(), member.first) == list.end())
                return fail(std::format("{} is no key of schema version {}",
                                        text_of(at.member(member.first)), kDocVersion));
        return true;
    }

    // A member exact() has checked is there.
    static const json::Value& m(const json::Value& obj, std::string_view key)
    {
        return *obj.find(key);
    }

    bool wrong(const json::Value& v, const Path& at, std::string_view expected)
    {
        return fail(std::format("{} is {}, not {}", text_of(at), json::kind_name(v), expected));
    }

    bool disagrees(const Path& at, std::string_view what)
    {
        return fail(std::format("{} disagrees with {}", text_of(at), what));
    }

    // ── Values of one kind ──

    bool string(const json::Value& v, const Path& at, std::string& out)
    {
        const std::string* s = v.string();
        if (!s) return wrong(v, at, "a string");
        out = *s;
        return true;
    }

    bool opt_string(const json::Value& v, const Path& at, std::optional<std::string>& out)
    {
        out.reset();
        if (v.is_null()) return true;
        if (!v.string()) return wrong(v, at, "a string or null");
        out = *v.string();
        return true;
    }

    // A string that must be one of `allowed`.
    bool one_of(const json::Value& v, const Path& at, std::span<const std::string_view> allowed,
                std::string& out)
    {
        if (!string(v, at, out)) return false;
        if (std::find(allowed.begin(), allowed.end(), out) != allowed.end()) return true;
        std::string list;
        for (std::size_t i = 0; i < allowed.size(); ++i)
            list += (i == 0 ? "" : i + 1 == allowed.size() ? " or " : ", ") + quote(allowed[i]);
        return fail(std::format("{} is {}, not {}: schema version {}'s", text_of(at), quote(out),
                                list, kDocVersion));
    }

    bool integer(const json::Value& v, const Path& at, std::int64_t& out)
    {
        const std::int64_t* n = v.integer();
        if (!n) return wrong(v, at, "an integer");
        out = *n;
        return true;
    }

    bool opt_integer(const json::Value& v, const Path& at, std::optional<std::int64_t>& out)
    {
        out.reset();
        if (v.is_null()) return true;
        if (!v.integer()) return wrong(v, at, "an integer or null");
        out = *v.integer();
        return true;
    }

    bool count(const json::Value& v, const Path& at, std::int64_t& out)
    {
        if (!integer(v, at, out)) return false;
        if (out < 0) return fail(std::format("{} is {}, not a count from 0", text_of(at), out));
        return true;
    }

    // An event's variant or session, or a span's variant: an index into the
    // `size` of them read (`what` names them), or -1 for null. A ladder's
    // document has no session, so there every event's session is null.
    bool index(const json::Value& v, const Path& at, std::size_t size, std::string_view what,
               int& out)
    {
        out = -1;
        if (v.is_null()) return true;
        if (!v.integer()) return wrong(v, at, "an integer or null");
        const std::int64_t n = *v.integer();
        if (n < 0 || static_cast<std::uint64_t>(n) >= size)
            return fail(std::format("{} is {}, which names none of the {} {}", text_of(at), n, size,
                                    what));
        out = static_cast<int>(n);
        return true;
    }

    bool number(const json::Value& v, const Path& at, double& out)
    {
        const std::optional<double> d = v.number();
        if (!d) return wrong(v, at, "a number");
        out = *d;
        return true;
    }

    bool opt_number(const json::Value& v, const Path& at, std::optional<double>& out)
    {
        out.reset();
        if (v.is_null()) return true;
        if (!v.number()) return wrong(v, at, "a number or null");
        out = v.number();
        return true;
    }

    bool boolean(const json::Value& v, const Path& at, bool& out)
    {
        const bool* b = v.boolean();
        if (!b) return wrong(v, at, "true or false");
        out = *b;
        return true;
    }

    // A rational's text into `text` and its terms of type T into `out`:
    // "num/den", each term in its shortest form, as std::format writes an
    // integer and so rational_json() and step_json() write them, without a
    // leading zero and without "-0", so that a rational has one text and a
    // document read back is written again to the same bytes.
    template <typename T>
    bool terms(const json::Value& v, const Path& at, std::string& text, std::pair<T, T>& out)
    {
        if (!string(v, at, text)) return false;
        const std::optional<std::pair<T, T>> t = terms_of<T>(text);
        if (!t)
            return fail(std::format("{} is {}, not a rational \"num/den\"", text_of(at), quote(text)));
        if (std::format("{}/{}", t->first, t->second) != text)
            return fail(std::format("{} is {}, whose terms aren't in their shortest form: no leading "
                                    "zero, and no sign on 0",
                                    text_of(at), quote(text)));
        out = *t;
        return true;
    }

    // Time bases, frame rates, the frame duration and the margins: WP08's
    // rationals, "num/den" strings, which D-092 (d) keeps.
    bool rational(const json::Value& v, const Path& at, AVRational& out)
    {
        std::string         text;
        std::pair<int, int> t;
        if (!terms(v, at, text, t)) return false;
        out = { t.first, t.second };
        return true;
    }

    bool array(const json::Value& v, const Path& at, const json::Value::Array*& out)
    {
        out = v.array();
        return out ? true : wrong(v, at, "an array");
    }

    bool strings(const json::Value& v, const Path& at, std::vector<std::string>& out)
    {
        const json::Value::Array* list = nullptr;
        if (!array(v, at, list)) return false;
        out.resize(list->size());
        for (std::size_t i = 0; i < list->size(); ++i)
            if (!string((*list)[i], at.item(i), out[i])) return false;
        return true;
    }

    // A number of seconds the document gives that a reader recomputes: `read`
    // must be `want` exactly, as json::number writes a double so that it reads
    // back as itself (src/JsonText.h).
    bool seconds(const json::Value& v, const Path& at, std::optional<double> want,
                 std::string_view what)
    {
        std::optional<double> read;
        if (!opt_number(v, at, read)) return false;
        if (read.has_value() != want.has_value() || (read && *read != *want))
            return disagrees(at, what);
        return true;
    }

    // ── The document's parts, in its order ──

    bool libraries(const json::Value& v, const Path& at,
                   std::vector<std::pair<std::string, std::string>>& out)
    {
        const json::Value::Object* o = v.object();
        if (!o) return wrong(v, at, "an object");
        for (const json::Value::Member& member : *o) {
            std::string version;
            if (!string(member.second, at.member(member.first), version)) return false;
            out.emplace_back(member.first, std::move(version));
        }
        return true;
    }

    bool file(const json::Value& v, const Path& at, FileRef& out)
    {
        return exact(v, kFileKeys, at) && string(m(v, "path"), at.member("path"), out.path) &&
               string(m(v, "sha256"), at.member("sha256"), out.sha256);
    }

    bool report(const json::Value& v, const Path& at, ReportInfo& r)
    {
        if (!exact(v, kReportKeys, at)) return false;
        std::optional<std::string> model_sha256;
        if (!string(m(v, "path"), at.member("path"), r.path) ||
            !string(m(v, "sha256"), at.member("sha256"), r.sha256) ||
            !string(m(v, "scores_sha256"), at.member("scores_sha256"), r.scores_sha256) ||
            !string(m(v, "status"), at.member("status"), r.status) ||
            !string(m(v, "reference_mode"), at.member("reference_mode"), r.reference_mode) ||
            !string(m(v, "model"), at.member("model"), r.model) ||
            !string(m(v, "model_source"), at.member("model_source"), r.model_source) ||
            !opt_string(m(v, "model_sha256"), at.member("model_sha256"), model_sha256) ||
            !string(m(v, "scaler"), at.member("scaler"), r.scaler) ||
            !string(m(v, "libvmaf_version"), at.member("libvmaf_version"), r.libvmaf_version) ||
            !string(m(v, "libvmaf_package_version"), at.member("libvmaf_package_version"),
                    r.libvmaf_package_version))
            return false;
        r.model_sha256 = model_sha256.value_or("");
        const json::Value& master = m(v, "master");
        const Path master_at = at.member("master");
        return exact(master, kMasterKeys, master_at) &&
               string(m(master, "url"), master_at.member("url"), r.master_url) &&
               string(m(master, "location"), master_at.member("location"), r.master_location) &&
               string(m(master, "sha256"), master_at.member("sha256"), r.master_sha256);
    }

    // The session part (design §8): the plan of a scripted session, by its
    // path and SHA-256 (D-105 (a)), or null for a ladder alone, whose
    // document has session_plan null and sessions empty, as it had before
    // sessions came.
    bool session_plan(const json::Value& v, const Path& at, std::optional<FileRef>& out)
    {
        out.reset();
        if (v.is_null()) return true;
        FileRef f;
        if (!file(v, at, f)) return false;
        out = std::move(f);
        return true;
    }

    // The sessions, the variants read: one at most, as a build has one
    // session at most (D-072), there exactly when session_plan names its plan,
    // since a scripted session comes from one (D-105 (a)). Each with a
    // non-empty id, its plan's name, its UTC anchor, null for a session whose
    // files weren't fetched or RFC 3339 UTC to the microsecond with Z (D-108
    // (c)), and its spans in play order, one at least, each starting where
    // the one before it ended, as make_session() places them (D-105 (a)).
    // Refusing anything else is WP09b's choice, which the owner kept (D-111
    // (i); src/TimelineDoc.h).
    bool session_list(const json::Value& v, const Path& at, bool has_plan, Timeline& t)
    {
        const json::Value::Array* list = nullptr;
        if (!array(v, at, list)) return false;
        if (list->size() > 1)
            return fail(std::format("{} has {} entries: a timeline document holds one session at most "
                                    "(D-072)",
                                    text_of(at), list->size()));
        if (has_plan != !list->empty())
            return fail(has_plan ? std::format("{} is empty, but session_plan names a plan: a plan makes a "
                                               "session (D-105 (a))",
                                               text_of(at))
                                 : std::format("{} has a session, but session_plan is null: a scripted "
                                               "session comes from a plan, recorded by its path and "
                                               "SHA-256 (D-105 (a))",
                                               text_of(at)));
        for (std::size_t i = 0; i < list->size(); ++i) {
            const json::Value&        x    = (*list)[i];
            const Path                x_at = at.item(i);
            Session                   s;
            const json::Value::Array* spans = nullptr;
            if (!exact(x, kSessionKeys, x_at) || !string(m(x, "id"), x_at.member("id"), s.id) ||
                !string(m(x, "name"), x_at.member("name"), s.name) ||
                !opt_string(m(x, "wallclock_anchor_utc"), x_at.member("wallclock_anchor_utc"),
                            s.wallclock_anchor_utc) ||
                !array(m(x, "spans"), x_at.member("spans"), spans))
                return false;
            if (s.id.empty()) return fail(std::format("{} is empty", text_of(x_at.member("id"))));
            if (s.wallclock_anchor_utc && !utc_anchor(*s.wallclock_anchor_utc))
                return fail(std::format("{} is {}, not RFC 3339 UTC to the microsecond with the zone Z, "
                                        "YYYY-MM-DDTHH:MM:SS.ffffffZ (D-108 (c))",
                                        text_of(x_at.member("wallclock_anchor_utc")),
                                        quote(*s.wallclock_anchor_utc)));
            const Path spans_at = x_at.member("spans");
            if (spans->empty())
                return fail(std::format("{} is empty: a session plays one span at least (D-105 (a))",
                                        text_of(spans_at)));
            for (std::size_t k = 0; k < spans->size(); ++k) {
                Span       p;
                const Path p_at = spans_at.item(k);
                if (!span((*spans)[k], p_at, t, p)) return false;
                if (k > 0 && p.ref_index != s.spans.back().ref_end)
                    return fail(std::format("{} is {}, not {}, where the span before it ends: each span "
                                            "starts where the one before it ended (D-105 (a))",
                                            text_of(p_at.member("ref_index")), p.ref_index,
                                            s.spans.back().ref_end));
                s.spans.push_back(p);
            }
            t.sessions.push_back(std::move(s));
        }
        return true;
    }

    // A session's span as make_session() places it (src/SessionPlan.h): the
    // variant it plays, one of the variants, and its plan's entry, one of
    // that variant's entries; the media sequence numbers of its first and
    // its last segment, the first no later than the last; the reference
    // frames it spans, half-open and on the axis; and scored exactly when it
    // plays a pair's distorted variant, the reference in top-rung mode having
    // no score (D-105 (b)). These checks, like those of session_list(), are
    // WP09b's, which the owner kept (D-111 (i); src/TimelineDoc.h).
    bool span(const json::Value& v, const Path& at, const Timeline& t, Span& p)
    {
        std::int64_t entry = 0;
        if (!exact(v, kSpanKeys, at) ||
            !index(m(v, "variant"), at.member("variant"), t.variants.size(), "variants", p.variant) ||
            !count(m(v, "entry"), at.member("entry"), entry) ||
            !count(m(v, "first_seq"), at.member("first_seq"), p.first_seq) ||
            !count(m(v, "last_seq"), at.member("last_seq"), p.last_seq) ||
            !integer(m(v, "ref_index"), at.member("ref_index"), p.ref_index) ||
            !integer(m(v, "ref_end"), at.member("ref_end"), p.ref_end) ||
            !boolean(m(v, "scored"), at.member("scored"), p.scored))
            return false;
        if (p.variant < 0)
            return wrong(m(v, "variant"), at.member("variant"), "an integer: a span plays a variant");
        const Variant& var = t.variants[static_cast<std::size_t>(p.variant)];
        p.entry = static_cast<std::size_t>(entry);
        if (std::find(var.entries.begin(), var.entries.end(), p.entry) == var.entries.end())
            return fail(std::format("{} is {}, none of the entries of variant {}, which the span plays",
                                    text_of(at.member("entry")), entry, p.variant));
        if (p.last_seq < p.first_seq)
            return fail(std::format("{} is {}, before its first_seq {}", text_of(at.member("last_seq")),
                                    p.last_seq, p.first_seq));
        if (p.ref_index < 0 || p.ref_end <= p.ref_index || p.ref_end > t.axis.frames)
            return fail(std::format("{} runs from reference frame {} to {}, which isn't a span of the axis's "
                                    "{} frames",
                                    text_of(at), p.ref_index, p.ref_end, t.axis.frames));
        if (p.scored != (var.pair >= 0))
            return fail(std::format("{} is {}, but the span plays {}: a span is scored exactly when it plays "
                                    "a pair's distorted variant, the reference in top-rung mode having no "
                                    "score (D-105 (b))",
                                    text_of(at.member("scored")), p.scored ? "true" : "false",
                                    var.pair >= 0 ? std::format("pair {}'s distorted variant", var.pair)
                                                  : std::string("the reference")));
        return true;
    }

    // The stretches of a per-frame value: each from the frame the one before
    // ended at, of a frame or more, together covering every one of the
    // axis's `frames` frames, since build_axis() gives every frame a tick
    // and a media key; an axis of no frames has no stretch. Once all of them
    // are known to cover the frames, `each` expands each one in turn.
    template <typename Each>
    bool stretches(const json::Value& v, const Path& at, Keys keys, std::int64_t frames, Each each)
    {
        const json::Value::Array* list = nullptr;
        if (!array(v, at, list)) return false;
        std::int64_t next = 0;
        for (std::size_t i = 0; i < list->size(); ++i) {
            const json::Value& s = (*list)[i];
            const Path         s_at = at.item(i);
            std::int64_t first = 0, n = 0;
            if (!exact(s, keys, s_at) || !count(m(s, "first"), s_at.member("first"), first) ||
                !count(m(s, "frames"), s_at.member("frames"), n))
                return false;
            if (first != next)
                return fail(std::format("{} is {}, not {}, the frame the stretch before ends at",
                                        text_of(s_at.member("first")), first, next));
            if (n < 1 || n > frames - next)
                return fail(std::format("{} is {}: the stretches cover the axis's {} frames, a "
                                        "frame or more each",
                                        text_of(s_at.member("frames")), n, frames));
            next += n;
        }
        if (next != frames)
            return fail(std::format("{} covers {} of the axis's {} frames", text_of(at), next, frames));
        for (std::size_t i = 0; i < list->size(); ++i) {
            const json::Value& s = (*list)[i];
            if (!each(s, at.item(i), *m(s, "frames").integer())) return false;
        }
        return true;
    }

    // A stretch's step, phase and scale (D-092 (a), D-099 (a)), into `out`:
    // the step "num/den" with 64-bit terms (terms()) in lowest terms and a
    // positive denominator, so that a step has one pair of terms; the phase
    // an integer from 0 to den - 1; the scale an integer from 1, on either
    // kind of stretch, the keys' too, and 1 alone for frames without a PTS
    // (axis()), as D-100 (c) keeps it: the steps and scales D-100 (a) and (b)
    // have the writer try are among those it took before them.
    bool rule(const json::Value& s, const Path& at, Stretch& out)
    {
        const Path  step_at = at.member("step"), phase_at = at.member("phase");
        const Path  scale_at = at.member("scale");
        std::string text;
        std::pair<std::int64_t, std::int64_t> q;
        if (!terms(m(s, "step"), step_at, text, q)) return false;
        if (q.second < 1)
            return fail(std::format("{} is {}, whose denominator isn't positive", text_of(step_at),
                                    quote(text)));
        if (gcd_of(q.first, q.second) != 1)
            return fail(std::format("{} is {}, not in lowest terms", text_of(step_at), quote(text)));
        out.step = { .num = q.first, .den = q.second };
        if (!integer(m(s, "phase"), phase_at, out.phase)) return false;
        if (out.phase < 0 || out.phase >= out.step.den)
            return fail(std::format("{} is {}, not a phase of the step {}, from 0 to {}",
                                    text_of(phase_at), out.phase, quote(text), out.step.den - 1));
        if (!integer(m(s, "scale"), scale_at, out.scale)) return false;
        if (out.scale < 1)
            return fail(std::format("{} is {}, not a scale from 1", text_of(scale_at), out.scale));
        return true;
    }

    // Whether a stretch's values fit in 64 bits (stretch_fits()). Its frames
    // are at most kMaxFrames, which the axis has checked.
    bool fits_stretch(const Stretch& s, const Path& at)
    {
        if (stretch_fits(s)) return true;
        return fail(std::format("{}'s values go past 64 bits", text_of(at)));
    }

    // A stretch's values, once fits_stretch() has passed, each given to `put`
    // in turn: phase + k * num is carried from frame to frame as q * den + r
    // with 0 <= r < den, exactly, which a division per frame would give as
    // well. Each quotient lies between 0 and the last one, which
    // stretch_fits() has bounded, so the scale times it is exact in 128 bits.
    template <typename Put>
    static void expand(const Stretch& s, Put put)
    {
        const i128 whole = floor_div(s.step.num, s.step.den);
        const i128 rest  = s.step.num - whole * s.step.den;   // 0 <= rest < den
        i128 q = 0, r = s.phase;
        for (std::int64_t k = 0; k < s.frames; ++k) {
            put(static_cast<std::int64_t>(s.value + s.scale * q));
            q += whole;
            r += rest;
            if (r >= s.step.den) {
                r -= s.step.den;
                ++q;
            }
        }
    }

    bool axis(const json::Value& v, const Path& at, Axis& a, ReferenceInfo& ref)
    {
        if (!exact(v, kAxisKeys, at)) return false;
        const std::array<std::string_view, 1> kinds  = { kAxisKind };
        const std::array<std::string_view, 2> clocks = { kPlaylistTime, kFrameGrid };
        const std::array<std::string_view, 2> checks = { kSegmentMap, kReportOnly };
        if (!one_of(m(v, "kind"), at.member("kind"), kinds, a.kind) ||
            !one_of(m(v, "seconds"), at.member("seconds"), clocks, a.clock) ||
            !rational(m(v, "frame_duration"), at.member("frame_duration"), a.frame_duration) ||
            !count(m(v, "frames"), at.member("frames"), a.frames) ||
            !one_of(m(v, "checked"), at.member("checked"), checks, a.checked) ||
            !integer(m(v, "tick_den"), at.member("tick_den"), a.tick_den) ||
            !strings(m(v, "warnings"), at.member("warnings"), a.warnings))
            return false;
        // build_axis() computes the playlist time only where the reference
        // has a segment map, which the check then compares both clocks on,
        // and otherwise falls back to the frame grid, checked on the report's
        // sequences only (src/TimelineAxis.h): the clock and the check come
        // paired.
        const std::string_view paired = a.clock == kPlaylistTime ? kSegmentMap : kReportOnly;
        if (a.checked != paired)
            return fail(std::format("{} is {}, not {}, the check that goes with {} {} (design §4.2)",
                                    text_of(at.member("checked")), quote(a.checked), quote(paired),
                                    text_of(at.member("seconds")), quote(a.clock)));
        if (a.frames > kMaxFrames)
            return fail(std::format("{} is {}, more than the {} frames this reader expands",
                                    text_of(at.member("frames")), a.frames, kMaxFrames));
        if (a.tick_den < 1)
            return fail(std::format("{} is {}, not a positive number of ticks per second",
                                    text_of(at.member("tick_den")), a.tick_den));

        const json::Value& r    = m(v, "reference");
        const Path         r_at = at.member("reference");
        const Path         sequences_at = r_at.member("sequences");
        const json::Value::Array* sequences = nullptr;
        if (!exact(r, kReferenceKeys, r_at) || !string(m(r, "url"), r_at.member("url"), ref.url) ||
            !rational(m(r, "time_base"), r_at.member("time_base"), a.time_base) ||
            !rational(m(r, "frame_rate"), r_at.member("frame_rate"), ref.frame_rate) ||
            !array(m(r, "sequences"), sequences_at, sequences))
            return false;
        for (std::size_t i = 0; i < sequences->size(); ++i) {
            const json::Value& s    = (*sequences)[i];
            const Path         s_at = sequences_at.item(i);
            Sequence           q;
            if (!exact(s, kSequenceKeys, s_at) ||
                !count(m(s, "first_index"), s_at.member("first_index"), q.first_index) ||
                !opt_integer(m(s, "first_pts_ticks"), s_at.member("first_pts_ticks"), q.first_pts) ||
                !seconds(m(s, "first_pts"), s_at.member("first_pts"),
                         pts_seconds(q.first_pts, a.time_base), "first_pts_ticks on the time base") ||
                !count(m(s, "frames"), s_at.member("frames"), q.frames))
                return false;
            ref.sequences.push_back(q);
        }

        // The ticks and the media keys, each reserved once its stretches are
        // known to cover the frames (kMaxFrames), then expanded in place by
        // the rule of D-092 (a) and D-099 (a).
        const auto covered = [](const json::Value&, const Path&, std::int64_t) { return true; };
        const auto frames  = static_cast<std::size_t>(a.frames);
        if (!stretches(m(v, "ticks"), at.member("ticks"), kTickKeys, a.frames, covered))
            return false;
        a.ticks.reserve(frames);
        if (!stretches(m(v, "ticks"), at.member("ticks"), kTickKeys, a.frames,
                       [&](const json::Value& s, const Path& s_at, std::int64_t n) {
                           Stretch st{ .frames = n };
                           if (!integer(m(s, "first_tick"), s_at.member("first_tick"), st.value) ||
                               !rule(s, s_at, st) || !fits_stretch(st, s_at))
                               return false;
                           expand(st, [&a](std::int64_t t) { a.ticks.push_back(t); });
                           return true;
                       }))
            return false;
        if (!stretches(m(v, "keys"), at.member("keys"), kKeyKeys, a.frames, covered)) return false;
        a.keys.reserve(frames);
        if (!stretches(m(v, "keys"), at.member("keys"), kKeyKeys, a.frames,
                       [&](const json::Value& s, const Path& s_at, std::int64_t n) {
                           std::int64_t sequence = 0;
                           Stretch      st{ .frames = n };
                           std::optional<std::int64_t> first;
                           if (!count(m(s, "sequence"), s_at.member("sequence"), sequence) ||
                               !opt_integer(m(s, "first_pts_ticks"), s_at.member("first_pts_ticks"),
                                            first) ||
                               !rule(s, s_at, st))
                               return false;
                           if (sequence > std::numeric_limits<std::int32_t>::max())
                               return fail(std::format("{} is {}, beyond a sequence's 32 bits",
                                                       text_of(s_at.member("sequence")), sequence));
                           const auto seq = static_cast<std::int32_t>(sequence);
                           // Frames without a PTS have no step: 0/1, whose only
                           // phase is 0, and the scale 1, as WP08 wrote the step
                           // 0 for them; frames with one, any scale rule()
                           // takes. Both are what the reader takes for the
                           // scale, which D-100 (c) keeps (src/TimelineDoc.h).
                           if (!first) {
                               if (st.step != Step{})
                                   return fail(std::format("{} is {}, not \"0/1\", for frames "
                                                           "without a PTS",
                                                           text_of(s_at.member("step")),
                                                           step_json(st.step)));
                               if (st.scale != 1)
                                   return fail(std::format("{} is {}, not 1, for frames without "
                                                           "a PTS",
                                                           text_of(s_at.member("scale")), st.scale));
                               for (std::int64_t k = 0; k < n; ++k)
                                   a.keys.push_back({ .sequence = seq, .pts = std::nullopt });
                               return true;
                           }
                           st.value = *first;
                           if (!fits_stretch(st, s_at)) return false;
                           expand(st, [&a, seq](std::int64_t p) {
                               a.keys.push_back({ .sequence = seq, .pts = p });
                           });
                           return true;
                       }))
            return false;

        const Path runs_at = at.member("runs");
        const json::Value::Array* runs = nullptr;
        if (!array(m(v, "runs"), runs_at, runs)) return false;
        for (std::size_t i = 0; i < runs->size(); ++i) {
            const json::Value& s    = (*runs)[i];
            const Path         s_at = runs_at.item(i);
            AxisRun            run;
            std::string        test;
            if (!exact(s, kRunKeys, s_at) ||
                !one_of(m(s, "test"), s_at.member("test"), kRunTests, test) ||
                !integer(m(s, "sequence"), s_at.member("sequence"), run.sequence) ||
                !integer(m(s, "first"), s_at.member("first"), run.first) ||
                !integer(m(s, "last"), s_at.member("last"), run.last) ||
                !count(m(s, "frames"), s_at.member("frames"), run.frames) ||
                !integer(m(s, "largest_at"), s_at.member("largest_at"), run.largest_at) ||
                !number(m(s, "largest"), s_at.member("largest"), run.largest) ||
                !boolean(m(s, "warning"), s_at.member("warning"), run.warning))
                return false;
            run.test = static_cast<AxisRun::Test>(
                std::find(kRunTests.begin(), kRunTests.end(), test) - kRunTests.begin());
            a.runs.push_back(run);
        }
        return true;
    }

    // Each variant once: one reference at most, and each pair's distorted
    // playlist alone in naming its pair, since a curve's events and the
    // curve itself are found by them (src/TimelineQuery.h).
    bool variants(const json::Value& v, const Path& at, Timeline& t)
    {
        const json::Value::Array* list = nullptr;
        if (!array(v, at, list)) return false;
        std::map<int, std::size_t> pairs;   // pair, or -1 for the reference: the variant naming it
        for (std::size_t i = 0; i < list->size(); ++i) {
            const json::Value& x    = (*list)[i];
            const Path         x_at = at.item(i);
            Variant            var;
            std::int64_t       id = 0;
            std::string        role;
            const std::array<std::string_view, 2> roles = { kReference, kDistorted };
            if (!exact(x, kVariantKeys, x_at) || !integer(m(x, "id"), x_at.member("id"), id) ||
                !one_of(m(x, "role"), x_at.member("role"), roles, role) ||
                !string(m(x, "url"), x_at.member("url"), var.url) ||
                !opt_string(m(x, "resolution"), x_at.member("resolution"), var.resolution) ||
                !opt_string(m(x, "codecs"), x_at.member("codecs"), var.codecs))
                return false;
            if (id != static_cast<std::int64_t>(i))
                return fail(std::format("{} is {}, not {}, its place among the variants",
                                        text_of(x_at.member("id")), id, i));
            var.index = static_cast<int>(i);

            const json::Value& pair = m(x, "pair");
            if (!pair.is_null()) {
                std::int64_t p = 0;
                if (!count(pair, x_at.member("pair"), p)) return false;
                if (p > std::numeric_limits<int>::max())
                    return fail(std::format("{} is {}, beyond any pair",
                                            text_of(x_at.member("pair")), p));
                var.pair = static_cast<int>(p);
            }
            if ((role == kReference) != (var.pair < 0))
                return fail(std::format("{} is {}, but its pair is {}", text_of(x_at.member("role")),
                                        quote(role),
                                        var.pair < 0 ? "null" : std::format("{}", var.pair)));
            if (const auto [it, added] = pairs.emplace(var.pair, i); !added)
                return fail(var.pair < 0
                                ? std::format("{} is the reference, and so is variants[{}]",
                                              text_of(x_at), it->second)
                                : std::format("{} is {}, the pair of variants[{}] too",
                                              text_of(x_at.member("pair")), var.pair, it->second));

            const json::Value::Array* entries = nullptr;
            if (!array(m(x, "entries"), x_at.member("entries"), entries)) return false;
            for (std::size_t k = 0; k < entries->size(); ++k) {
                std::int64_t e = 0;
                if (!count((*entries)[k], x_at.member("entries").item(k), e)) return false;
                var.entries.push_back(static_cast<std::size_t>(e));
            }
            // A BANDWIDTH exactly when there are entries; a rank from 1, or null.
            const json::Value& bw = m(x, "bandwidth");
            if (var.entries.empty() ? !bw.is_null() : !bw.integer())
                return wrong(bw, x_at.member("bandwidth"),
                             var.entries.empty() ? "null, for a variant without entries"
                                                 : "an integer, its entries' highest BANDWIDTH");
            var.bandwidth = bw.integer() ? *bw.integer() : 0;
            if (!rank(m(x, "rank_from_top"), x_at.member("rank_from_top"), var.rank_from_top) ||
                !rank(m(x, "rank_from_bottom"), x_at.member("rank_from_bottom"), var.rank_from_bottom))
                return false;

            const json::Value& pl = m(x, "playlist");
            if (!pl.is_null()) {
                const Path  pl_at = x_at.member("playlist");
                std::string url;
                if (!exact(pl, kPlaylistKeys, pl_at) ||
                    !string(m(pl, "url"), pl_at.member("url"), url) ||
                    !string(m(pl, "location"), pl_at.member("location"), var.playlist_location) ||
                    !string(m(pl, "sha256"), pl_at.member("sha256"), var.playlist_sha256))
                    return false;
                if (url != var.url) return disagrees(pl_at.member("url"), "the variant's url");
                // A variant was read from a playlist when it has a location
                // or a SHA-256, as variant_json() decides it, which writes
                // null for one with neither (D-092 (c)): such an object would
                // be written again as null, its segment map with it.
                if (var.playlist_location.empty() && var.playlist_sha256.empty())
                    return fail(std::format("{} has an empty location and an empty sha256: a "
                                            "variant read from no playlist has playlist null "
                                            "(D-092 (c))",
                                            text_of(pl_at)));
            }
            // The segment map: null exactly when the playlist is, since a
            // variant not read from a playlist, a mezzanine (design §5.2), has
            // no map (D-092 (c)), and it comes back as Variant's default, "ok";
            // otherwise "ok" or a refusal, nothing else (D-081 (c)).
            const json::Value& map    = m(x, "segment_map");
            const Path         map_at = x_at.member("segment_map");
            if (pl.is_null()) {
                if (!map.is_null())
                    return wrong(map, map_at, "null: a variant without a playlist has no segment map "
                                              "(D-092 (c))");
            } else {
                if (map.is_null())
                    return fail(std::format("{} is null, but the variant has a playlist, whose map "
                                            "is {} or a refusal that starts {} (D-092 (c))",
                                            text_of(map_at), quote(kMapOk), quote(kMapRefused)));
                if (!string(map, map_at, var.segment_map)) return false;
                if (var.segment_map != kMapOk && !var.segment_map.starts_with(kMapRefused))
                    return fail(std::format("{} is {}, neither {} nor a refusal that starts {} "
                                            "(D-081 (c))",
                                            text_of(map_at), quote(var.segment_map), quote(kMapOk),
                                            quote(kMapRefused)));
            }
            t.variants.push_back(std::move(var));
        }
        return true;
    }

    bool rank(const json::Value& v, const Path& at, int& out)
    {
        out = 0;
        if (v.is_null()) return true;
        std::int64_t r = 0;
        if (!integer(v, at, r)) return false;
        if (r < 1 || r > std::numeric_limits<int>::max())
            return fail(std::format("{} is {}, not a rank from 1", text_of(at), r));
        out = static_cast<int>(r);
        return true;
    }

    bool events(const json::Value& v, const Path& at, Timeline& t)
    {
        const std::size_t sessions = t.sessions.size();
        const json::Value::Array* list = nullptr;
        if (!array(v, at, list)) return false;
        std::map<std::string, std::size_t, std::less<>> seen;
        for (std::size_t i = 0; i < list->size(); ++i) {
            const json::Value& x    = (*list)[i];
            const Path         x_at = at.item(i);
            Event              e;
            std::string        source;
            std::int64_t       seq = 0;
            std::optional<std::int64_t> wallclock;
            if (!exact(x, kEventKeys, x_at) || !string(m(x, "id"), x_at.member("id"), e.id) ||
                !string(m(x, "source"), x_at.member("source"), source) ||
                !string(m(x, "type"), x_at.member("type"), e.type) ||
                !index(m(x, "variant"), x_at.member("variant"), t.variants.size(), "variants",
                       e.variant) ||
                !index(m(x, "session"), x_at.member("session"), sessions, "sessions", e.session) ||
                !opt_integer(m(x, "segment"), x_at.member("segment"), e.segment) ||
                !integer(m(x, "ref_index"), x_at.member("ref_index"), e.ref_index) ||
                !opt_integer(m(x, "ref_end"), x_at.member("ref_end"), e.ref_end) ||
                !seconds(m(x, "t"), x_at.member("t"), t.axis.seconds(e.ref_index),
                         "the axis's seconds of its ref_index") ||
                !seconds(m(x, "t_end"), x_at.member("t_end"),
                         e.ref_end ? std::optional<double>(t.axis.seconds(*e.ref_end)) : std::nullopt,
                         "the axis's seconds of its ref_end") ||
                !integer(m(x, "seq"), x_at.member("seq"), seq) ||
                !opt_integer(m(x, "pts_ticks"), x_at.member("pts_ticks"), e.key.pts) ||
                !seconds(m(x, "pts"), x_at.member("pts"), pts_seconds(e.key.pts, t.axis.time_base),
                         "pts_ticks on the axis's time base") ||
                !wallclock_us(m(x, "wallclock"), x_at.member("wallclock"), wallclock))
                return false;
            e.wallclock_us = wallclock;
            if (e.id.empty()) return fail(std::format("{} is empty", text_of(x_at.member("id"))));
            if (const auto [it, added] = seen.emplace(e.id, i); !added)
                return fail(std::format("{} is {}, the id of timeline[{}] too",
                                        text_of(x_at.member("id")), quote(e.id), it->second));
            const std::optional<Source> s = source_from_name(source);
            if (!s)
                return fail(std::format("{} is {}, none of section 3.1's sources",
                                        text_of(x_at.member("source")), quote(source)));
            e.source = *s;
            const auto types = event_types(e.source);
            if (std::find(types.begin(), types.end(), e.type) == types.end())
                return fail(std::format("{} is {}, no type of the {} source in schema version {}'s "
                                        "vocabulary (eval/phase3-survey/design.md §5.9)",
                                        text_of(x_at.member("type")), quote(e.type), source,
                                        kDocVersion));
            // A sequence of the report counts from 0 (MediaKey), and an event
            // at one of the axis's frames has that frame's media key
            // (Event::key, src/TimelineModel.h).
            if (seq < 0)
                return fail(std::format("{} is {}, not a sequence from 0",
                                        text_of(x_at.member("seq")), seq));
            if (seq > std::numeric_limits<std::int32_t>::max())
                return fail(std::format("{} is {}, beyond a sequence's 32 bits",
                                        text_of(x_at.member("seq")), seq));
            e.key.sequence = static_cast<std::int32_t>(seq);
            if (e.ref_index >= 0 && e.ref_index < t.axis.frames) {
                constexpr std::string_view kKey = "the axis's media key of its ref_index";
                const MediaKey& key = t.axis.keys[static_cast<std::size_t>(e.ref_index)];
                if (e.key.sequence != key.sequence) return disagrees(x_at.member("seq"), kKey);
                if (e.key.pts != key.pts) return disagrees(x_at.member("pts_ticks"), kKey);
            }
            // The network and player sources' events are a session's, its
            // fetch's requests and its plan's spans and switches (D-068;
            // design §5.6, §5.7), so an offline event, of another source, is
            // of no session; and only observed events, a fetch's and later the
            // player's log, have a wall clock (D-073): a ladder alone has
            // neither. A request a session's fetch made was observed, so it
            // has one; a wall clock counts from its session's UTC anchor,
            // which a session whose files weren't fetched hasn't got (D-108
            // (c)). The refusals of an offline event with a session, of a
            // request without a wall clock and of a wall clock without an
            // anchor are WP09b's, which the owner kept (D-111 (i);
            // src/TimelineDoc.h).
            const bool observed = e.source == Source::Network || e.source == Source::Player;
            if (observed && e.session < 0)
                return fail(std::format("{} is a {} event without a session: the {} source's events "
                                        "are a session's (D-068)",
                                        text_of(x_at), source, source));
            if (!observed && e.session >= 0)
                return fail(std::format("{} is {}, but a {} event is offline and of no session: a "
                                        "session's own events are of the network and player sources "
                                        "(D-068)",
                                        text_of(x_at.member("session")), e.session, source));
            if (!observed && e.wallclock_us)
                return fail(std::format("{} is {} s, but a {} event is offline and has no wall "
                                        "clock (D-073)",
                                        text_of(x_at.member("wallclock")),
                                        json::number(*m(x, "wallclock").number()), source));
            if (e.source == Source::Network && !e.wallclock_us)
                return fail(std::format("{} is null, but a network event is a request the session's fetch "
                                        "made, whose wall clock was observed (D-073)",
                                        text_of(x_at.member("wallclock"))));
            if (e.wallclock_us && e.session >= 0 &&
                !t.sessions[static_cast<std::size_t>(e.session)].wallclock_anchor_utc)
                return fail(std::format("{} is {} s, but session {} has no UTC anchor for it to count from: "
                                        "its files weren't fetched (D-073, D-108 (c))",
                                        text_of(x_at.member("wallclock")),
                                        json::number(*m(x, "wallclock").number()), e.session));
            const json::Value& payload = m(x, "payload");
            if (!payload.object()) return wrong(payload, x_at.member("payload"), "an object");
            e.payload = std::get<Value::Object>(value_of(payload).v);
            t.events.push_back(std::move(e));
        }
        t.build_index();
        for (std::size_t i = 0; i < t.events.size(); ++i) ids_.emplace(t.events[i].id, i);
        return true;
    }

    // A wall clock in seconds, as microseconds (D-073): the seconds a whole
    // number of microseconds writes, so that it is read back exactly.
    bool wallclock_us(const json::Value& v, const Path& at, std::optional<std::int64_t>& out)
    {
        std::optional<double> s;
        if (!opt_number(v, at, s)) return false;
        out.reset();
        if (!s) return true;
        const double us = *s * 1e6;
        if (!(std::fabs(us) < 9.0e18) || static_cast<double>(std::llround(us)) / 1e6 != *s)
            return fail(std::format("{} is {} s, not a whole number of microseconds",
                                    text_of(at), json::number(*s)));
        out = std::llround(us);
        return true;
    }

    // A curve, named by its pair's number, the ladder report's own
    // pairs[].index (D-092 (b)), which a variant's pair holds: given back as
    // the curve whose variant has it (curve_of_pair()). A timeline read back
    // has no curves yet, so that is its place among the variants' pairs in
    // ascending order of their numbers, the report's order of its complete
    // pairs, where attach_curves() puts the report's curves
    // (src/TimelineBuild.h). A number no variant has is refused, naming the
    // key; two variants with one number variants() has refused already. A
    // session's curve is named by its place among the sessions, given back
    // as CurveRef::Kind::Session with that place, one the document doesn't
    // have refused; any other kind is refused.
    bool curve(const json::Value& v, const Path& at, const Timeline& t, CurveRef& out)
    {
        std::string  kind;
        std::int64_t i = 0;
        const std::array<std::string_view, 2> kinds = { kPairCurve, kSessionCurve };
        if (!exact(v, kCurveKeys, at) || !one_of(m(v, "kind"), at.member("kind"), kinds, kind) ||
            !count(m(v, "index"), at.member("index"), i))
            return false;
        if (kind == kSessionCurve) {
            if (static_cast<std::uint64_t>(i) >= t.sessions.size())
                return fail(std::format("{} is {}, which names none of the {} sessions",
                                        text_of(at.member("index")), i, t.sessions.size()));
            out = CurveRef{ .kind = CurveRef::Kind::Session, .index = static_cast<int>(i) };
            return true;
        }
        const std::optional<CurveRef> c = i <= std::numeric_limits<int>::max()
                                              ? curve_of_pair(t, static_cast<int>(i))
                                              : std::nullopt;
        if (!c)
            return fail(std::format("{} is {}, the pair of none of the variants",
                                    text_of(at.member("index")), i));
        out = *c;
        return true;
    }

    bool detection(const json::Value& v, const Path& at, const Timeline& t,
                   std::optional<Detection>& out)
    {
        out.reset();
        if (v.is_null()) return true;
        Detection d;
        std::int64_t window = 0, min_points = 0, warm = 0;
        std::string  baseline, context_first;
        const json::Value::Array* curves = nullptr;
        if (!exact(v, kDetectionKeys, at) ||
            !number(m(v, "threshold"), at.member("threshold"), d.options.threshold) ||
            !integer(m(v, "window"), at.member("window"), window) ||
            !integer(m(v, "min_points"), at.member("min_points"), min_points) ||
            !string(m(v, "baseline"), at.member("baseline"), baseline) ||
            !integer(m(v, "warm_up"), at.member("warm_up"), warm) ||
            !string(m(v, "context_first"), at.member("context_first"), context_first) ||
            !array(m(v, "curves"), at.member("curves"), curves))
            return false;
        d.options.window     = window;
        d.options.min_points = min_points;
        if (const std::string why = check_options(d.options); !why.empty())
            return fail(std::format("{}: {}", text_of(at), why));
        // The rules detection decides are D-077's, which detection_record()
        // names: a document that names others wasn't made with them.
        const Payload record = detection_record(d.options);
        const auto rule = [&record](std::string_view name) {
            for (const auto& [n, value] : record)
                if (const auto* s = std::get_if<std::string>(&value.v); s && n == name) return *s;
            return std::string();
        };
        if (baseline != rule("baseline"))
            return fail(std::format("{} is {}, not {}, D-077's", text_of(at.member("baseline")),
                                    quote(baseline), quote(rule("baseline"))));
        if (context_first != rule("context_first"))
            return fail(std::format("{} is {}, not {}, D-077's", text_of(at.member("context_first")),
                                    quote(context_first), quote(rule("context_first"))));
        if (warm != warm_up(window))
            return fail(std::format("{} is {}, not {}, the warm-up of a window of {} (D-077)",
                                    text_of(at.member("warm_up")), warm, warm_up(window), window));
        bool session = false;
        for (std::size_t i = 0; i < curves->size(); ++i) {
            CurveRef c;
            if (!curve((*curves)[i], at.member("curves").item(i), t, c)) return false;
            d.curves.push_back(c);
            session = session || c.kind == CurveRef::Kind::Session;
        }
        // The rules that make a session's drops of its pairs', exactly where
        // detection ran on a session's curve (session_rules_record()): a
        // document that names others wasn't made with them. Their form and
        // this check are WP09b's, which the owner kept (D-111 (b), (i);
        // src/TimelineDoc.h).
        const json::Value& rules    = m(v, "session_rules");
        const Path         rules_at = at.member("session_rules");
        if (!session) {
            if (!rules.is_null())
                return wrong(rules, rules_at, "null: detection ran on no session's curve");
        } else {
            if (rules.is_null())
                return fail(std::format("{} is null, but detection ran on a session's curve, whose drops "
                                        "are its pairs' clipped to its spans by D-077's rules",
                                        text_of(rules_at)));
            const Payload want = session_rules_record();
            std::vector<std::string_view> keys;
            for (const auto& [name, value] : want) keys.push_back(name);
            if (!exact(rules, keys, rules_at)) return false;
            for (const auto& [name, value] : want) {
                std::string read;
                if (!string(m(rules, name), rules_at.member(name), read)) return false;
                const std::string& taken = std::get<std::string>(value.v);
                if (read != taken)
                    return fail(std::format("{} is {}, not {}, D-077's", text_of(rules_at.member(name)),
                                            quote(read), quote(taken)));
            }
        }
        out = std::move(d);
        return true;
    }

    bool overlap(const json::Value& v, const Path& at, QueryOptions& q)
    {
        std::string rule, scope;
        const std::array<std::string_view, 1> rules  = { kOverlapRule };
        const std::array<std::string_view, 1> scopes = { kOverlapScope };
        return exact(v, kOverlapKeys, at) && one_of(m(v, "rule"), at.member("rule"), rules, rule) &&
               rational(m(v, "margin_before"), at.member("margin_before"), q.margin_before) &&
               rational(m(v, "margin_after"), at.member("margin_after"), q.margin_after) &&
               one_of(m(v, "scope"), at.member("scope"), scopes, scope);
    }

    bool attribution_list(const json::Value& v, const Path& at, const Timeline& t,
                          const QueryOptions& q, bool detection_ran, std::vector<Attribution>& out)
    {
        const json::Value::Array* list = nullptr;
        if (!array(v, at, list)) return false;
        for (std::size_t i = 0; i < list->size(); ++i) {
            Attribution a;
            if (!attribution((*list)[i], at.item(i), t, q, detection_ran, a)) return false;
            out.push_back(std::move(a));
        }
        return true;
    }

    // An attribution. A detected one is a drop detection found, so it needs
    // the detection record: without W and N a document has detection null
    // and no drop (D-077). One whose detected is false is an interval a user
    // gave (design §6.1), which mirage_vmaf timeline never writes, taken
    // whether detection ran or not (src/TimelineDoc.h).
    bool attribution(const json::Value& v, const Path& at, const Timeline& t, const QueryOptions& q,
                     bool detection_ran, Attribution& a)
    {
        if (!exact(v, kAttributionKeys, at)) return false;
        const json::Value& iv    = m(v, "interval");
        const Path         iv_at = at.member("interval");
        if (!curve(m(v, "curve"), at.member("curve"), t, a.curve) ||
            !boolean(m(v, "detected"), at.member("detected"), a.detected))
            return false;
        if (a.detected && !detection_ran)
            return fail(std::format("{} is true, but detection is null: a detected drop needs the "
                                    "detection that found it (D-077)",
                                    text_of(at.member("detected"))));
        if (!exact(iv, kIntervalKeys, iv_at) ||
            !integer(m(iv, "ref_index"), iv_at.member("ref_index"), a.interval.ref_index) ||
            !integer(m(iv, "ref_end"), iv_at.member("ref_end"), a.interval.ref_end) ||
            !seconds(m(iv, "t"), iv_at.member("t"), t.axis.seconds(a.interval.ref_index),
                     "the axis's seconds of its ref_index") ||
            !seconds(m(iv, "t_end"), iv_at.member("t_end"), t.axis.seconds(a.interval.ref_end),
                     "the axis's seconds of its ref_end") ||
            !count(m(iv, "points"), iv_at.member("points"), a.points) ||
            !opt_number(m(v, "baseline"), at.member("baseline"), a.baseline) ||
            !opt_number(m(v, "min"), at.member("min"), a.min) ||
            !opt_number(m(v, "mean"), at.member("mean"), a.mean) ||
            !opt_number(m(v, "drop_magnitude"), at.member("drop_magnitude"), a.drop_magnitude) ||
            !opt_number(m(v, "mean_drop"), at.member("mean_drop"), a.mean_drop))
            return false;
        a.searched = widen(t.axis, a.interval, q);

        // One group per source, in section 3.1's order (D-078).
        const Path groups_at = at.member("overlapping_events");
        const json::Value::Array* groups = nullptr;
        if (!array(m(v, "overlapping_events"), groups_at, groups)) return false;
        if (groups->size() != kSourceCount)
            return fail(std::format("{} has {} groups, not one per source, {}", text_of(groups_at),
                                    groups->size(), kSourceCount));
        for (std::size_t s = 0; s < kSourceCount; ++s) {
            const json::Value& g    = (*groups)[s];
            const Path         g_at = groups_at.item(s);
            const std::array<std::string_view, 1> names = { source_name(static_cast<Source>(s)) };
            std::string name;
            const json::Value::Array* found = nullptr;
            if (!exact(g, kGroupKeys, g_at) ||
                !one_of(m(g, "source"), g_at.member("source"), names, name) ||
                !array(m(g, "events"), g_at.member("events"), found))
                return false;
            for (std::size_t k = 0; k < found->size(); ++k) {
                Found f;
                if (!found_event((*found)[k], g_at.member("events").item(k), t, a.interval,
                                 static_cast<Source>(s), f))
                    return false;
                a.by_source[s].push_back(f);
            }
        }
        return true;
    }

    // One event an attribution lists, by its id, with its offsets from the
    // interval's start, which must be the event's own (design §6.1).
    bool found_event(const json::Value& v, const Path& at, const Timeline& t, Interval interval,
                     Source source, Found& f)
    {
        std::string id;
        std::optional<std::int64_t> end_frames;
        if (!exact(v, kFoundKeys, at) || !string(m(v, "event"), at.member("event"), id) ||
            !integer(m(v, "offset_frames"), at.member("offset_frames"), f.offset_frames) ||
            !opt_integer(m(v, "offset_end_frames"), at.member("offset_end_frames"), end_frames))
            return false;
        const auto it = ids_.find(id);
        if (it == ids_.end())
            return fail(std::format("{} is {}, the id of no event of the timeline",
                                    text_of(at.member("event")), quote(id)));
        f.event = it->second;
        const Event& e = t.events[f.event];
        if (e.source != source)
            return fail(std::format("{} is {}, an event of the {} source, not of {}",
                                    text_of(at.member("event")), quote(id), source_name(e.source),
                                    source_name(source)));
        const auto from_start = [&interval](std::int64_t frame) {
            return static_cast<i128>(frame) - interval.ref_index;
        };
        if (f.offset_frames != from_start(e.ref_index))
            return disagrees(at.member("offset_frames"), "its event's ref_index");
        if (end_frames.has_value() != e.ref_end.has_value() ||
            (end_frames && *end_frames != from_start(*e.ref_end)))
            return disagrees(at.member("offset_end_frames"), "its event's ref_end");
        f.offset_end_frames = end_frames;
        f.offset_s = t.axis.seconds_between(interval.ref_index, e.ref_index);
        if (e.ref_end) f.offset_end_s = t.axis.seconds_between(interval.ref_index, *e.ref_end);
        return seconds(m(v, "offset"), at.member("offset"), f.offset_s,
                       "the axis's seconds from the interval's start to its event") &&
               seconds(m(v, "offset_end"), at.member("offset_end"), f.offset_end_s,
                       "the axis's seconds from the interval's start to its event's end");
    }
};

} // namespace

std::span<const std::string_view> event_types(Source source)
{
    switch (source) {
    case Source::Vmaf:     return kVmafTypes;
    case Source::Network:  return kNetworkTypes;
    case Source::Playlist: return kPlaylistTypes;
    case Source::Scte35:   return kScte35Types;
    case Source::Player:   return kPlayerTypes;
    case Source::Decoder:  return kDecoderTypes;
    }
    return {};
}

ReportInfo report_info(const LadderScores& scores)
{
    ReportInfo r;
    r.status                  = scores.status;
    r.reference_mode          = scores.reference_mode;
    r.model                   = scores.model;
    r.model_source            = scores.model_source;
    r.model_sha256            = scores.model_sha256;
    r.scaler                  = scores.scaler;
    r.libvmaf_version         = scores.libvmaf_version;
    r.libvmaf_package_version = scores.libvmaf_package_version;
    r.master_url              = scores.master_url;
    r.master_location         = scores.master_location;
    r.master_sha256           = scores.master_sha256;
    return r;
}

std::optional<double> number_of(const Value& value)
{
    if (const auto* d = std::get_if<double>(&value.v)) return *d;
    if (const auto* i = std::get_if<std::int64_t>(&value.v)) return static_cast<double>(*i);
    return std::nullopt;
}

std::optional<int> pair_number(const Timeline& timeline, CurveRef curve)
{
    if (curve.kind != CurveRef::Kind::Pair || curve.index < 0) return std::nullopt;
    const auto k = static_cast<std::size_t>(curve.index);
    if (timeline.curves.empty()) {
        const std::vector<int> numbers = pair_numbers(timeline);
        return k < numbers.size() ? std::optional<int>(numbers[k]) : std::nullopt;
    }
    if (k >= timeline.curves.size()) return std::nullopt;
    const int v = timeline.curves[k].variant;
    if (v < 0 || static_cast<std::size_t>(v) >= timeline.variants.size()) return std::nullopt;
    const int pair = timeline.variants[static_cast<std::size_t>(v)].pair;
    return pair >= 0 ? std::optional<int>(pair) : std::nullopt;
}

std::optional<CurveRef> curve_of_pair(const Timeline& timeline, int pair)
{
    if (pair < 0) return std::nullopt;
    if (timeline.curves.empty()) {
        const std::vector<int> numbers = pair_numbers(timeline);
        const auto it = std::lower_bound(numbers.begin(), numbers.end(), pair);
        if (it == numbers.end() || *it != pair) return std::nullopt;
        return CurveRef{ .kind = CurveRef::Kind::Pair, .index = static_cast<int>(it - numbers.begin()) };
    }
    for (std::size_t k = 0; k < timeline.curves.size(); ++k) {
        const CurveRef c{ .kind = CurveRef::Kind::Pair, .index = static_cast<int>(k) };
        if (pair_number(timeline, c) == pair) return c;
    }
    return std::nullopt;
}

bool reference_info(const LadderScores& scores, const Axis& axis, ReferenceInfo& out,
                    std::string& err)
{
    out = {};
    err.clear();
    // build_axis()'s rule: the pair whose reference sequences hold the most
    // frames, the first of equal ones (src/TimelineAxis.cpp), whose time base
    // it copies into the axis as the report gives it.
    const PairScores* base = nullptr;
    for (const PairScores& p : scores.pairs)
        if (!base || reference_frames_read(p) > reference_frames_read(*base)) base = &p;
    ReferenceInfo ref;
    ref.url = scores.reference_url;
    if (base) {
        const AVRational tb = base->ref_time_base;
        const bool same = (tb.num == axis.time_base.num && tb.den == axis.time_base.den) ||
                          (tb.den > 0 && axis.time_base.den > 0 && av_cmp_q(tb, axis.time_base) == 0);
        if (!same) {
            err = std::format("pair {}, whose reference the axis takes by build_axis()'s rule, reads "
                              "it with time base {}/{}, but the axis has {}/{}: the axis wasn't "
                              "built from these scores, or its rule has changed",
                              base->index, tb.num, tb.den, axis.time_base.num, axis.time_base.den);
            return false;
        }
        ref.frame_rate = base->ref_rate;
        ref.sequences  = base->ref_sequences;
    }
    out = std::move(ref);
    return true;
}

std::string to_json(const Timeline& timeline, const std::vector<Attribution>& attributions,
                    const DocInfo& info)
{
    // Events in canonical order (the design's P-I), whatever order they come
    // in, so that the document doesn't depend on it.
    std::vector<std::size_t> order(timeline.events.size());
    std::iota(order.begin(), order.end(), std::size_t{ 0 });
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        return canonical_before(timeline.events[a], timeline.events[b]);
    });

    std::string libraries = "{";
    for (std::size_t i = 0; i < info.ffmpeg_libraries.size(); ++i)
        libraries += std::format("{}{}: {}", i == 0 ? "" : ", ", quote(info.ffmpeg_libraries[i].first),
                                 quote(info.ffmpeg_libraries[i].second));
    libraries += "}";

    std::vector<std::string> variants, sessions, events, attribution_items;
    for (const Variant& v : timeline.variants) variants.push_back(variant_json(v));
    for (const Session& s : timeline.sessions) sessions.push_back(session_json(s));
    for (std::size_t i : order) events.push_back(event_json(timeline.events[i], timeline.axis));
    for (const Attribution& a : attributions)
        attribution_items.push_back(attribution_json(a, timeline));

    std::string out = "{\n";
    out += std::format("  \"schema\": {},\n", quote(kDocSchema));
    out += std::format("  \"schema_version\": {},\n", kDocVersion);
    out += std::format("  \"tool\": {},\n", quote(info.tool));
    out += std::format("  \"command\": {},\n", quote(info.command));
    out += std::format("  \"ffmpeg_libraries\": {},\n", libraries);
    out += std::format("  \"options\": {},\n", payload_json(info.options));
    out += std::format("  \"config_file\": {},\n",
                       info.config_file ? file_json(*info.config_file) : std::string("null"));
    out += std::format("  \"ladder_report\": {},\n", report_json(info.report));
    // The session part, in design §8's places: the plan by its path and
    // SHA-256 and the session, or null and none for a ladder alone, whose
    // document is as it was before sessions came (src/TimelineDoc.h).
    out += std::format("  \"session_plan\": {},\n",
                       info.session_plan ? file_json(*info.session_plan) : std::string("null"));
    out += "  \"axis\": " + axis_json(timeline.axis, info.reference) + ",\n";
    out += "  \"variants\": " + lines(variants, "    ") + ",\n";
    out += "  \"sessions\": " + lines(sessions, "    ") + ",\n";
    out += "  \"timeline\": " + lines(events, "    ") + ",\n";
    out += "  \"detection\": " + detection_json(info.detection, timeline) + ",\n";
    out += "  \"overlap\": " + overlap_json(info.overlap) + ",\n";
    out += "  \"attributions\": " + lines(attribution_items, "    ") + ",\n";
    out += "  \"warnings\": " + strings_json(timeline.warnings, "    ") + "\n";
    return out + "}\n";
}

bool from_json(std::string_view text, Timeline& timeline, std::vector<Attribution>& attributions,
               DocInfo& info, std::string& err)
{
    timeline     = {};
    attributions = {};
    info         = {};
    err.clear();
    json::Error e;
    const std::optional<json::Value> doc = json::parse(text, e);
    if (!doc) {
        err = "not JSON: " + json::error_text(e);
        return false;
    }
    Timeline                 t;
    std::vector<Attribution> a;
    DocInfo                  d;
    Reader                   r;
    if (!r.document(*doc, t, a, d)) {
        err = std::move(r.err);
        return false;
    }
    timeline     = std::move(t);
    attributions = std::move(a);
    info         = std::move(d);
    return true;
}

bool write_document(const std::string& path, std::string_view text, std::string& err)
{
    namespace fs = std::filesystem;
    const fs::path target(path);
    fs::path tmp = target;
    tmp += ".partial";

    std::error_code ec;
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            err = std::format("cannot create {}: {}", tmp.string(), std::strerror(errno));
            return false;
        }
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
        f.close();
        if (f.fail()) {
            fs::remove(tmp, ec);
            err = std::format("cannot write {}", tmp.string());
            return false;
        }
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        err = std::format("cannot move {} to {}: {}", tmp.string(), path, ec.message());
        fs::remove(tmp, ec);
        return false;
    }
    return true;
}

} // namespace timeline
