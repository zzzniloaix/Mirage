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
// of CurveRef, a pair's alone until WP07 adds a session's. Design §8 names
// the clocks, the checks and the roles; kOverlapScope's name, a curve's kind
// "pair" in its {"kind", "index"} form and the run tests' names below are
// WP08's, pending the owner (src/TimelineDoc.h).
constexpr std::string_view kAxisKind       = "reference-frames";
constexpr std::string_view kPlaylistTime   = "playlist-time";
constexpr std::string_view kFrameGrid      = "frame-grid";
constexpr std::string_view kSegmentMap     = "segment-map";
constexpr std::string_view kReportOnly     = "report-only";
constexpr std::string_view kOverlapRule    = "half-open";
constexpr std::string_view kOverlapScope   = "curve-inputs";
constexpr std::string_view kPairCurve      = "pair";
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
// expands each stretch into them, with no temporary copy beside them.
constexpr std::int64_t kMaxFrames = std::int64_t{ 1 } << 24;

std::string_view run_test_name(AxisRun::Test test)
{
    const auto i = static_cast<std::size_t>(test);
    return i < kRunTests.size() ? kRunTests[i] : std::string_view();
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

// A stretch of reference frames over which a per-frame value steps evenly:
// frames first to first + frames - 1, the k-th at `value` + k * `step`.
//
// The stretches are WP08's encoding of the axis's per-frame values, pending
// the owner (src/TimelineDoc.h): evenly spaced frames take one, but frames
// that aren't a whole number of ticks long step unevenly once a muxer has
// rounded their PTS, and a new stretch starts at each change of step, every
// 4 frames at 24000/1001 fps and every 2 at 60000/1001 on a 90 kHz clock
// (tests/test_timeline_doc.cpp), so that the stretches grow with the frames
// there.
struct Stretch {
    std::int64_t first  = 0;
    std::int64_t frames = 0;
    std::int64_t value  = 0;
    std::int64_t step   = 0;
};

// The stretches of `values`, each as long as the step from its first value
// to the next holds, so that evenly spaced frames take one.
std::vector<Stretch> tick_stretches(const std::vector<std::int64_t>& values)
{
    std::vector<Stretch> out;
    const std::size_t n = values.size();
    for (std::size_t i = 0; i < n;) {
        std::size_t  j    = i + 1;
        std::int64_t step = 0;
        if (j < n && fits_int64(static_cast<i128>(values[j]) - values[i])) {
            step = values[j] - values[i];
            while (j < n && static_cast<i128>(values[j]) - values[j - 1] == step) ++j;
        }
        out.push_back({ .first = static_cast<std::int64_t>(i),
                        .frames = static_cast<std::int64_t>(j - i), .value = values[i],
                        .step = step });
        i = j;
    }
    return out;
}

// A stretch of media keys: one sequence, and PTS that step evenly or are all
// unknown; pending the owner as Stretch is.
struct KeyStretch {
    std::int64_t first  = 0;
    std::int64_t frames = 0;
    std::int32_t sequence = 0;
    std::optional<std::int64_t> pts;   // the first frame's; nullopt: every frame's unknown
    std::int64_t step   = 0;           // 0 when the PTS are unknown
};

std::vector<KeyStretch> key_stretches(const std::vector<MediaKey>& keys)
{
    std::vector<KeyStretch> out;
    const std::size_t n = keys.size();
    for (std::size_t i = 0; i < n;) {
        const MediaKey& k    = keys[i];
        std::size_t     j    = i + 1;
        std::int64_t    step = 0;
        const auto same_kind = [&](std::size_t x) {
            return keys[x].sequence == k.sequence && keys[x].pts.has_value() == k.pts.has_value();
        };
        if (j < n && same_kind(j)) {
            if (!k.pts) {
                while (j < n && same_kind(j)) ++j;
            } else if (fits_int64(static_cast<i128>(*keys[j].pts) - *k.pts)) {
                step = *keys[j].pts - *k.pts;
                while (j < n && same_kind(j) &&
                       static_cast<i128>(*keys[j].pts) - *keys[j - 1].pts == step)
                    ++j;
            }
        }
        out.push_back({ .first = static_cast<std::int64_t>(i),
                        .frames = static_cast<std::int64_t>(j - i), .sequence = k.sequence,
                        .pts = k.pts, .step = step });
        i = j;
    }
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
// form is WP08's, pending the owner (src/TimelineDoc.h).
std::string rational_json(AVRational q) { return quote(std::format("{}/{}", q.num, q.den)); }

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
// "pair:<k>"; the index is the pair's position among the report's complete
// pairs, as Timeline::curves holds them, not the report's pairs[].index.
// Both are WP08's, pending the owner (src/TimelineDoc.h).
std::string curve_json(CurveRef curve)
{
    return std::format("{{\"kind\": {}, \"index\": {}}}", quote(kPairCurve), curve.index);
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

    // The per-frame values as stretches, their members named as WP08 chose,
    // pending the owner (src/TimelineDoc.h).
    std::vector<std::string> ticks;
    for (const Stretch& s : tick_stretches(a.ticks))
        ticks.push_back(std::format(
            "{{\"first\": {}, \"frames\": {}, \"first_tick\": {}, \"step\": {}}}", s.first,
            s.frames, s.value, s.step));
    std::vector<std::string> keys;
    for (const KeyStretch& s : key_stretches(a.keys))
        keys.push_back(std::format("{{\"first\": {}, \"frames\": {}, \"sequence\": {}, "
                                   "\"first_pts_ticks\": {}, \"step\": {}}}",
                                   s.first, s.frames, s.sequence, opt_integer(s.pts), s.step));
    // Every run of the check, structured, with its warning flag: design §4.2
    // writes each run to axis.warnings, whose texts stay there, and lists the
    // runs of a playlist whose map was refused as facts without giving them
    // a place. axis.runs is WP08's addition to D-072's layout for both,
    // pending the owner (src/TimelineDoc.h).
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
    // A variant without entries, a mezzanine, has no BANDWIDTH; a rank of 0
    // is none (src/TimelineModel.h); one not read from a playlist, no
    // playlist. Each is written null, and segment_map as Variant holds it,
    // "ok" by default, a mezzanine's included, though it has no map: WP08's
    // sentinels, pending the owner (src/TimelineDoc.h).
    const auto rank = [](int r) { return r > 0 ? std::format("{}", r) : std::string("null"); };
    const bool playlist = !v.playlist_location.empty() || !v.playlist_sha256.empty();
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
        quote(v.segment_map));
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

std::string detection_json(const std::optional<Detection>& d)
{
    if (!d || !d->options.given()) return "null";
    std::string out = "{";
    for (const auto& [name, value] : detection_record(d->options))
        out += std::format("{}: {}, ", quote(name), value_json(value));
    // The curves detect() ran on, and session_rules null: WP08's reading of
    // design §8's "curves" and "session_rules", pending the owner
    // (src/TimelineDoc.h). The rules that clip a pair's drops to a session's
    // spans (design §6.3) are the session part's (WP09b): a ladder has no
    // session.
    out += "\"curves\": [";
    for (std::size_t i = 0; i < d->curves.size(); ++i)
        out += (i == 0 ? "" : ", ") + curve_json(d->curves[i]);
    return out + "], \"session_rules\": null}";
}

// The overlap record, its scope named "curve-inputs" by WP08, pending the
// owner (src/TimelineDoc.h).
std::string overlap_json(const QueryOptions& q)
{
    return std::format("{{\"rule\": {}, \"margin_before\": {}, \"margin_after\": {}, \"scope\": {}}}",
                       quote(kOverlapRule), rational_json(q.margin_before),
                       rational_json(q.margin_after), quote(kOverlapScope));
}

// An attribution, its overlapping_events one group per source in section
// 3.1's order (D-078), all six written, empty ones included, which design
// §8 leaves open: WP08's choice, pending the owner (src/TimelineDoc.h).
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
    out += "      \"curve\": " + curve_json(a.curve) + ",\n";
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
constexpr std::array<std::string_view, 4> kTickKeys = { "first", "frames", "first_tick", "step" };
constexpr std::array<std::string_view, 5> kKeyKeys  = { "first", "frames", "sequence",
                                                        "first_pts_ticks", "step" };
constexpr std::array<std::string_view, 8> kRunKeys  = {
    "test", "sequence", "first", "last", "frames", "largest_at", "largest", "warning",
};
constexpr std::array<std::string_view, 12> kVariantKeys = {
    "id", "role", "pair", "entries", "url", "bandwidth", "resolution", "codecs", "rank_from_top",
    "rank_from_bottom", "playlist", "segment_map",
};
constexpr std::array<std::string_view, 3>  kPlaylistKeys = { "url", "location", "sha256" };
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

// "num/den" with both integers, as rational_json() writes them; nullopt
// otherwise.
std::optional<AVRational> rational_of(std::string_view s)
{
    const std::size_t slash = s.find('/');
    if (slash == std::string_view::npos) return std::nullopt;
    AVRational r{ 0, 1 };
    const char* mid = s.data() + slash;
    const char* end = s.data() + s.size();
    const auto [p1, e1] = std::from_chars(s.data(), mid, r.num);
    const auto [p2, e2] = std::from_chars(mid + 1, end, r.den);
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
        std::size_t sessions = 0;
        if (!report(m(doc, "ladder_report"), root.member("ladder_report"), info.report) ||
            !session_plan(m(doc, "session_plan"), root.member("session_plan")) ||
            !axis(m(doc, "axis"), root.member("axis"), t.axis, info.reference) ||
            !variants(m(doc, "variants"), root.member("variants"), t) ||
            !session_list(m(doc, "sessions"), root.member("sessions"), sessions) ||
            !events(m(doc, "timeline"), root.member("timeline"), sessions, t) ||
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
    // is WP08's, pending the owner (src/TimelineDoc.h): any later key needs
    // another schema_version, where LadderReportRead passes over keys it
    // doesn't know in later report layouts.
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

    // An event's variant or session: an index into the `size` of them read
    // (`what` names them), or -1 for null. A ladder's document has no
    // session, so there every event's session is null; WP09b's sessions
    // give it something to name.
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

    bool rational(const json::Value& v, const Path& at, AVRational& out)
    {
        std::string s;
        if (!string(v, at, s)) return false;
        const std::optional<AVRational> q = rational_of(s);
        if (!q) return fail(std::format("{} is {}, not a rational \"num/den\"", text_of(at), quote(s)));
        out = *q;
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

    // The session part as a ladder alone has it (design §8): no plan and no
    // session, the only values version 1 takes until WP09b fills them, so
    // that the version has one layout before and after.
    bool session_plan(const json::Value& v, const Path& at)
    {
        return v.is_null() ? true : wrong(v, at, "null: a ladder's timeline has no session plan");
    }

    bool session_list(const json::Value& v, const Path& at, std::size_t& count)
    {
        const json::Value::Array* list = nullptr;
        if (!array(v, at, list)) return false;
        const std::size_t n = list->size();
        if (n != 0)
            return fail(std::format("{} has {} {}, not none: a ladder's timeline has no session",
                                    text_of(at), n, n == 1 ? "entry" : "entries"));
        count = n;
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

    // Whether the `n` values from `first` on, `step` apart, fit in 64 bits:
    // they run from `first` to the last one, so all fit when those two do.
    bool fits_stretch(std::int64_t first, std::int64_t step, std::int64_t n, const Path& at)
    {
        if (fits_int64(static_cast<i128>(first) + static_cast<i128>(step) * (n - 1))) return true;
        return fail(std::format("{}'s values go past 64 bits", text_of(at)));
    }

    // A stretch's k-th value, once fits_stretch() has passed.
    static std::int64_t stretch_value(std::int64_t first, std::int64_t step, std::int64_t k)
    {
        return static_cast<std::int64_t>(static_cast<i128>(first) + static_cast<i128>(step) * k);
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
        // known to cover the frames (kMaxFrames), then expanded in place.
        const auto covered = [](const json::Value&, const Path&, std::int64_t) { return true; };
        const auto frames  = static_cast<std::size_t>(a.frames);
        if (!stretches(m(v, "ticks"), at.member("ticks"), kTickKeys, a.frames, covered))
            return false;
        a.ticks.reserve(frames);
        if (!stretches(m(v, "ticks"), at.member("ticks"), kTickKeys, a.frames,
                       [&](const json::Value& s, const Path& s_at, std::int64_t n) {
                           std::int64_t first = 0, step = 0;
                           if (!integer(m(s, "first_tick"), s_at.member("first_tick"), first) ||
                               !integer(m(s, "step"), s_at.member("step"), step) ||
                               !fits_stretch(first, step, n, s_at))
                               return false;
                           for (std::int64_t k = 0; k < n; ++k)
                               a.ticks.push_back(stretch_value(first, step, k));
                           return true;
                       }))
            return false;
        if (!stretches(m(v, "keys"), at.member("keys"), kKeyKeys, a.frames, covered)) return false;
        a.keys.reserve(frames);
        if (!stretches(m(v, "keys"), at.member("keys"), kKeyKeys, a.frames,
                       [&](const json::Value& s, const Path& s_at, std::int64_t n) {
                           std::int64_t sequence = 0, step = 0;
                           std::optional<std::int64_t> first;
                           if (!count(m(s, "sequence"), s_at.member("sequence"), sequence) ||
                               !opt_integer(m(s, "first_pts_ticks"), s_at.member("first_pts_ticks"),
                                            first) ||
                               !integer(m(s, "step"), s_at.member("step"), step))
                               return false;
                           if (sequence > std::numeric_limits<std::int32_t>::max())
                               return fail(std::format("{} is {}, beyond a sequence's 32 bits",
                                                       text_of(s_at.member("sequence")), sequence));
                           if (!first && step != 0)
                               return fail(std::format("{} is {}, not 0, for frames without a PTS",
                                                       text_of(s_at.member("step")), step));
                           if (first && !fits_stretch(*first, step, n, s_at)) return false;
                           for (std::int64_t k = 0; k < n; ++k)
                               a.keys.push_back(
                                   { .sequence = static_cast<std::int32_t>(sequence),
                                     .pts = first ? std::optional<std::int64_t>(
                                                        stretch_value(*first, step, k))
                                                  : std::nullopt });
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
                !opt_string(m(x, "codecs"), x_at.member("codecs"), var.codecs) ||
                !string(m(x, "segment_map"), x_at.member("segment_map"), var.segment_map))
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
            }
            // "ok" or a refusal, nothing else, so that a reference that isn't
            // a playlist, a mezzanine, says "ok" for a map it doesn't have:
            // WP08's sentinel, pending the owner (src/TimelineDoc.h).
            if (var.segment_map != kMapOk && !var.segment_map.starts_with(kMapRefused))
                return fail(std::format("{} is {}, neither {} nor a refusal that starts {} (D-081 (c))",
                                        text_of(x_at.member("segment_map")), quote(var.segment_map),
                                        quote(kMapOk), quote(kMapRefused)));
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

    bool events(const json::Value& v, const Path& at, std::size_t sessions, Timeline& t)
    {
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
            // design §5.6, §5.7), and only observed events, a fetch's and later
            // the player's log, have a wall clock (D-073): a ladder alone has
            // neither.
            const bool observed = e.source == Source::Network || e.source == Source::Player;
            if (observed && e.session < 0)
                return fail(std::format("{} is a {} event without a session: the {} source's events "
                                        "are a session's (D-068)",
                                        text_of(x_at), source, source));
            if (!observed && e.wallclock_us)
                return fail(std::format("{} is {} s, but a {} event is offline and has no wall "
                                        "clock (D-073)",
                                        text_of(x_at.member("wallclock")),
                                        json::number(*m(x, "wallclock").number()), source));
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

    // A curve, named by a variant's pair: the pair's position among the
    // report's complete pairs, which Timeline::curves follows and the query's
    // scope reaches through Curve::variant, not the report's pairs[].index,
    // which skips the number of a pair that failed. That reading of the
    // index is WP08's, pending the owner (src/TimelineDoc.h).
    bool curve(const json::Value& v, const Path& at, const Timeline& t, CurveRef& out)
    {
        std::string  kind;
        std::int64_t i = 0;
        const std::array<std::string_view, 1> kinds = { kPairCurve };
        if (!exact(v, kCurveKeys, at) || !one_of(m(v, "kind"), at.member("kind"), kinds, kind) ||
            !count(m(v, "index"), at.member("index"), i))
            return false;
        const bool named = std::any_of(t.variants.begin(), t.variants.end(),
                                       [i](const Variant& var) { return var.pair == i; });
        if (!named)
            return fail(std::format("{} is {}, the pair of none of the variants",
                                    text_of(at.member("index")), i));
        out = { .kind = CurveRef::Kind::Pair, .index = static_cast<int>(i) };
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
        for (std::size_t i = 0; i < curves->size(); ++i) {
            CurveRef c;
            if (!curve((*curves)[i], at.member("curves").item(i), t, c)) return false;
            d.curves.push_back(c);
        }
        const json::Value& rules = m(v, "session_rules");
        if (!rules.is_null())
            return wrong(rules, at.member("session_rules"), "null: a ladder's timeline has no session");
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

    std::vector<std::string> variants, events, attribution_items;
    for (const Variant& v : timeline.variants) variants.push_back(variant_json(v));
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
    // The session part, in design §8's places, as a ladder alone has it
    // until WP09b fills it (src/TimelineDoc.h).
    out += "  \"session_plan\": null,\n";
    out += "  \"axis\": " + axis_json(timeline.axis, info.reference) + ",\n";
    out += "  \"variants\": " + lines(variants, "    ") + ",\n";
    out += "  \"sessions\": [],\n";
    out += "  \"timeline\": " + lines(events, "    ") + ",\n";
    out += "  \"detection\": " + detection_json(info.detection) + ",\n";
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
