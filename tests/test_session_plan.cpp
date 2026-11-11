// Scripted sessions (src/SessionPlan.h): the plan's file and its errors
// (D-105 (a)); a session's spans on the reference's frames through the
// segments the build placed, the reference's unscored in top-rung mode
// (D-105 (b)); its variant_span and variant_switch events with both ranks
// (D-075); its curve (curve_points()) and the scope of its attributions
// (attribute(), D-078); the work order's example on a hand-built timeline;
// its drops under design §6.3's three rules (D-077); and its fetch's files
// in their order (D-104 (a)), on a fetch base at their paths from the
// master's directory (D-108 (c)), their dot segments resolved (pending the
// owner), and their events on the media axis (D-105 (c)), on rows the test
// builds by hand (the fetch itself is tests/test_session_fetch.cpp's).
// Mostly on a five-rung version of testgen's ad ladder
// (eval/phase3-survey/testgen.md §2.3), built by hand and merged by
// timeline::build() as tests/test_timeline_build.cpp merges its three-rung
// one: eight 25-frame segments at 25 fps, the reference v720 in top-rung
// mode and the pairs v540, v360, v270 and v180, the ad in segments 3 and 4
// restarting its timestamps at the main content's origin, v270's ad scored
// low.

#include <catch2/catch_test_macros.hpp>

#include "FileHash.h"
#include "LadderReportRead.h"
#include "PlaylistEvents.h"
#include "SessionPlan.h"
#include "TimelineBuild.h"
#include "TimelineDetect.h"
#include "TimelineModel.h"
#include "TimelineQuery.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

using timeline::Attribution;
using timeline::CurveRef;
using timeline::Drop;
using timeline::Event;
using timeline::FetchedFile;
using timeline::Found;
using timeline::Interval;
using timeline::PlanSpan;
using timeline::Request;
using timeline::RequestRow;
using timeline::Source;
using timeline::Value;

namespace {

bool contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

// ── A five-rung ad ladder ────────────────────────────────────────────────────

constexpr AVRational   kTs       = { 1, 90000 };   // MPEG-TS's time base
constexpr std::int64_t kOrigin   = 900000;         // 10 s: where testgen's main content starts
constexpr std::int64_t kFrame25  = 3600;           // a frame at 25 fps, in 1/90000
constexpr std::int64_t kFrames   = 200;            // eight segments of 25 frames
constexpr int          kEntries  = 5;
constexpr const char*  kRungs[]  = { "v720", "v540", "v360", "v270", "v180" };
constexpr std::int64_t kBandwidth[]  = { 3000000, 2000000, 1000000, 600000, 350000 };
constexpr const char*  kResolution[] = { "1280x720", "960x540", "640x360", "480x270", "320x180" };
constexpr int          kAdPair   = 2;              // pair 2, entry 3, variant 3 (v270): its ad scored low
const std::string      kMaster   = "http://test.invalid/ladder/master.m3u8";

// testgen's ad playlist, the same text in every rung: the ad in segments 3
// and 4, between two EXT-X-DISCONTINUITY tags (lines 13 and 20), with the
// plain cue tags (lines 14, 17 and 21).
const std::string kAdPlaylist =
    "#EXTM3U\n"
    "#EXT-X-VERSION:3\n"
    "#EXT-X-TARGETDURATION:1\n"
    "#EXT-X-MEDIA-SEQUENCE:0\n"
    "#EXT-X-DISCONTINUITY-SEQUENCE:0\n"
    "#EXT-X-PLAYLIST-TYPE:VOD\n"
    "#EXTINF:1.000000,\nmain_000.ts\n"
    "#EXTINF:1.000000,\nmain_001.ts\n"
    "#EXTINF:1.000000,\nmain_002.ts\n"
    "#EXT-X-DISCONTINUITY\n"
    "#EXT-X-CUE-OUT:2.000\n"
    "#EXTINF:1.000000,\nad_000.ts\n"
    "#EXT-X-CUE-OUT-CONT:1.000/2.000\n"
    "#EXTINF:1.000000,\nad_001.ts\n"
    "#EXT-X-DISCONTINUITY\n"
    "#EXT-X-CUE-IN\n"
    "#EXTINF:1.000000,\nmain_005.ts\n"
    "#EXTINF:1.000000,\nmain_006.ts\n"
    "#EXTINF:1.000000,\nmain_007.ts\n"
    "#EXT-X-ENDLIST\n";

std::string url_of(int entry)
{
    return std::format("http://test.invalid/ladder/{}/index.m3u8", kRungs[entry]);
}

// Segment k's URL in the playlist of entry `entry`.
std::string segment_url(int entry, std::int64_t k)
{
    return k == 3 || k == 4 ? std::format("http://test.invalid/ladder/{}/ad_00{}.ts", kRungs[entry], k - 3)
                            : std::format("http://test.invalid/ladder/{}/main_00{}.ts", kRungs[entry], k);
}

// The reference's PTS: the main content from 10 s, the ad's 50 frames from
// 10 s again, the main content back at 15 s.
std::int64_t ref_pts(std::int64_t i)
{
    return i < 75 || i >= 125 ? kOrigin + i * kFrame25 : kOrigin + (i - 75) * kFrame25;
}

// Pair k's score at frame i: near 97 - 2k, with the one-point dip of the
// first pair of the ad's libvmaf context (D-037); v270's ad near 25.
double score(int pair, std::int64_t i)
{
    const double wobble = 0.25 * static_cast<double>(i % 4);
    if (pair == kAdPair && i >= 75 && i < 125) return 25.0 + wobble;
    if (i == 75) return 80.0;
    return 97.0 - 2.0 * static_cast<double>(pair) + wobble;
}

// The ladder report as timeline::read_ladder_report() would give it: the
// master's five entries, the reference v720 and four complete pairs that
// pair every frame, the reference's sequences as the aligner finds them.
timeline::LadderScores ladder_report()
{
    timeline::LadderScores s;
    s.status                  = "complete";
    s.reference_mode          = "top-rung";
    s.model                   = "vmaf_v0.6.1";
    s.model_source            = "built-in";
    s.scaler                  = "bicubic";
    s.libvmaf_version         = "1.2.3";
    s.libvmaf_package_version = "1.2.4";
    s.master_url              = kMaster;
    s.master_location         = kMaster;
    s.master_sha256           = filehash::sha256_hex("the master's bytes");
    for (int k = 0; k < kEntries; ++k) {
        timeline::MasterEntry e;
        e.index      = static_cast<std::size_t>(k);
        e.line       = static_cast<std::size_t>(3 + 2 * k);
        e.uri        = std::format("{}/index.m3u8", kRungs[k]);
        e.url        = url_of(k);
        e.bandwidth  = kBandwidth[k];
        e.resolution = kResolution[k];
        e.codecs     = "avc1.64001e";
        e.video      = true;
        s.entries.push_back(e);
    }
    s.reference_url     = url_of(0);
    s.reference_entries = { 0 };
    for (int pair = 0; pair + 1 < kEntries; ++pair) {
        timeline::PairScores p;
        p.index         = static_cast<std::size_t>(pair);
        p.entries       = { static_cast<std::size_t>(pair + 1) };
        p.ref_url       = url_of(0);
        p.dis_url       = url_of(pair + 1);
        p.ref_time_base = kTs;
        p.dis_time_base = kTs;
        p.ref_rate      = { 25, 1 };
        p.dis_rate      = { 25, 1 };
        for (std::int64_t i = 0; i < kFrames; ++i) {
            p.ref_index.push_back(i);
            p.dis_index.push_back(i);
            p.ref_pts.push_back(ref_pts(i));
            p.dis_pts.push_back(ref_pts(i));
            p.vmaf.push_back(score(pair, i));
        }
        p.context_first = { 0, 75 };
        p.ref_sequences = { { .first_index = 0, .first_pts = kOrigin, .frames = 75 },
                            { .first_index = 75, .first_pts = kOrigin, .frames = 125 } };
        p.dis_sequences = p.ref_sequences;
        s.pairs.push_back(std::move(p));
    }
    s.n_threads   = 4;
    s.n_subsample = 1;
    return s;
}

// A demux pass of the ad ladder's shape: every frame in its 25-frame
// segment, a keyframe at each segment's first frame, the reference's PTS.
timeline::VariantMedia ad_media()
{
    timeline::VariantMedia m;
    for (std::int64_t k = 0; k < 8; ++k) {
        m.segments.push_back({ .seq = k, .first_index = 25 * k, .frames = 25, .bytes = 12000 + k });
        m.keyframes.push_back(25 * k);
    }
    for (std::int64_t i = 0; i < kFrames; ++i) m.pts.push_back(ref_pts(i));
    return m;
}

// The build's inputs, `refused` naming a variant whose demux pass refused
// its segment map (D-081 (c)), -1 none.
timeline::BuildInputs ladder_inputs(int refused = -1)
{
    timeline::BuildInputs in;
    in.scores = ladder_report();
    for (int v = 0; v < kEntries; ++v) {
        timeline::VariantRead r;
        r.playlist = timeline::parse_playlist_events(kAdPlaylist, url_of(v), v);
        REQUIRE(r.playlist->error.empty());
        r.playlist_location = url_of(v);
        r.playlist_sha256   = filehash::sha256_hex(kAdPlaylist);
        r.media             = ad_media();
        if (v == refused) r.media = timeline::VariantMedia{ .refused = "the test refuses it" };
        in.variants.push_back(std::move(r));
    }
    return in;
}

timeline::Timeline ladder(int refused = -1)
{
    std::string err;
    timeline::Timeline t = timeline::build(ladder_inputs(refused), err);
    INFO(err);
    REQUIRE(err.empty());
    return t;
}

timeline::SessionPlan plan_of(std::vector<PlanSpan> spans)
{
    return timeline::SessionPlan{ .name = "a test session", .spans = std::move(spans) };
}

// The example's plan, design §9.3's shape on five rungs: entry 1 (v540) for
// segments 0 to 2, entry 3 (v270) for 3 and 4, entry 1 for 5 to 7.
timeline::SessionPlan example_plan()
{
    return plan_of({ { .entry = 1, .first_seq = 0, .last_seq = 2 },
                     { .entry = 3, .first_seq = 3, .last_seq = 4 },
                     { .entry = 1, .first_seq = 5, .last_seq = 7 } });
}

// What make_session() refuses `plan` with on `t`: its error.
std::string refusal(const timeline::Timeline& t, const timeline::SessionPlan& plan,
                    const std::vector<timeline::MasterEntry>& entries = ladder_report().entries)
{
    std::string err;
    const timeline::Session s = timeline::make_session(t, entries, plan, err);
    CHECK(s.spans.empty());
    return err;
}

// The ladder's timeline with the session of `plan`, id "0", and its player
// events, the events' ids given and the index built again, as the build is
// to add them (src/SessionPlan.h).
timeline::Timeline with_session(const timeline::SessionPlan& plan, int refused = -1)
{
    timeline::Timeline t = ladder(refused);
    std::string err;
    timeline::Session s = timeline::make_session(t, ladder_report().entries, plan, err);
    INFO(err);
    REQUIRE(err.empty());
    s.id = "0";
    t.sessions.push_back(std::move(s));
    const std::vector<Event> player = timeline::session_events(t, ladder_report().entries, 0);
    t.events.insert(t.events.end(), player.begin(), player.end());
    timeline::assign_ids(t.events);
    t.build_index();
    return t;
}

// `t` with the network events of `files`, ids given again and the index built.
void add_requests(timeline::Timeline& t, const std::vector<Request>& files)
{
    const timeline::RequestEvents net = timeline::request_events(t, 0, files);
    CHECK(net.warnings.empty());
    t.events.insert(t.events.end(), net.events.begin(), net.events.end());
    timeline::assign_ids(t.events);
    t.build_index();
}

// One row of NetworkLogger's, as the fetch keeps one: an HTTP GET from
// `at` µs after the session's start, `length` µs long, its status line
// 500 µs before its end.
RequestRow row(const std::string& url, std::int64_t at, std::int64_t length, int status = 200,
               std::string text = "OK", std::int64_t bytes = 12032)
{
    return RequestRow{ .url = url, .t_start = at, .t_sent = at + 100, .t_first_byte = at + length - 500,
                       .t_end = at + length, .bytes = bytes, .status = status, .status_text = std::move(text),
                       .failed = status >= 400, .content_length = bytes, .initiator = "playback" };
}

// The example session's fetch, as fetch_files() lists it, each file given a
// row by hand, one after another from 1 ms: each 3 ms long, but for v270's
// segment 3, ad_000.ts, the work order's example request of 2.3 s.
std::vector<Request> example_fetch(const timeline::Timeline& t)
{
    std::string err;
    std::vector<Request> files = timeline::fetch_files(t, 0, kMaster, "", err);
    INFO(err);
    REQUIRE(err.empty());
    std::int64_t at = 1000;
    for (Request& r : files) {
        const bool slow = r.kind == FetchedFile::Segment && r.variant == 3 && r.segment == 3;
        const std::int64_t length = slow ? 2300000 : 3000;
        r.chain = { row(r.url, at, length) };
        at += length;
    }
    return files;
}

// ── Reading events ───────────────────────────────────────────────────────────

std::vector<const Event*> events_of(const timeline::Timeline& t, std::string_view type, int variant)
{
    std::vector<const Event*> out;
    for (const Event& e : t.events)
        if (e.type == type && e.variant == variant) out.push_back(&e);
    return out;
}

const Value* member(const timeline::Payload& p, std::string_view name)
{
    for (const auto& [n, v] : p)
        if (n == name) return &v;
    return nullptr;
}

std::optional<std::int64_t> int_member(const timeline::Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    if (!v || !std::holds_alternative<std::int64_t>(v->v)) return std::nullopt;
    return std::get<std::int64_t>(v->v);
}

std::optional<double> double_member(const timeline::Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    if (!v || !std::holds_alternative<double>(v->v)) return std::nullopt;
    return std::get<double>(v->v);
}

std::optional<std::string> text_member(const timeline::Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    if (!v || !std::holds_alternative<std::string>(v->v)) return std::nullopt;
    return std::get<std::string>(v->v);
}

std::optional<bool> bool_member(const timeline::Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    if (!v || !std::holds_alternative<bool>(v->v)) return std::nullopt;
    return std::get<bool>(v->v);
}

bool null_member(const timeline::Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    return v && std::holds_alternative<std::monostate>(v->v);
}

const Value::Object& object_member(const timeline::Payload& p, std::string_view name)
{
    static const Value::Object kNone;
    const Value* v = member(p, name);
    return v && std::holds_alternative<Value::Object>(v->v) ? std::get<Value::Object>(v->v) : kNone;
}

const Value::List& list_member(const timeline::Payload& p, std::string_view name)
{
    static const Value::List kNone;
    const Value* v = member(p, name);
    return v && std::holds_alternative<Value::List>(v->v) ? std::get<Value::List>(v->v) : kNone;
}

std::vector<std::string> names_of(const timeline::Payload& p)
{
    std::vector<std::string> out;
    for (const auto& [n, v] : p) out.push_back(n);
    return out;
}

// A rung (D-075), an object of its own or the first members of a
// variant_span's payload: entry, uri, bandwidth, resolution and both ranks
// of master entry `entry`.
void check_rung(const Value::Object& rung, int entry, int from_top, int from_bottom)
{
    INFO("entry " << entry);
    const std::vector<std::string> names = names_of(rung);
    CHECK(std::vector<std::string>(names.begin(), names.begin() + std::min<std::ptrdiff_t>(6, std::ssize(names))) ==
          std::vector<std::string>{ "entry", "uri", "bandwidth", "resolution", "rank_from_top", "rank_from_bottom" });
    CHECK(int_member(rung, "entry") == entry);
    CHECK(text_member(rung, "uri") == std::format("{}/index.m3u8", kRungs[entry]));
    CHECK(int_member(rung, "bandwidth") == kBandwidth[entry]);
    CHECK(text_member(rung, "resolution") == std::string(kResolution[entry]));
    CHECK(int_member(rung, "rank_from_top") == from_top);
    CHECK(int_member(rung, "rank_from_bottom") == from_bottom);
}

using Ids = std::vector<std::string>;

// The ids of the events of one source that an attribution lists, in order.
Ids listed(const timeline::Timeline& t, const Attribution& a, Source source)
{
    Ids out;
    for (const Found& f : a.by_source[static_cast<std::size_t>(source)]) out.push_back(t.events[f.event].id);
    return out;
}

const Found* found(const timeline::Timeline& t, const Attribution& a, std::string_view id)
{
    for (const auto& group : a.by_source)
        for (const Found& f : group)
            if (t.events[f.event].id == id) return &f;
    return nullptr;
}

constexpr CurveRef kSession{ .kind = CurveRef::Kind::Session, .index = 0 };

// ── Hand-built curves for the drops' rules ───────────────────────────────────

// A curve of `frames` points, one at every `step`th reference frame from 0,
// at `level`, or at `low` over the frames [low_from, low_to).
timeline::Curve flat_curve(int variant, std::int64_t frames, double level, double low, std::int64_t low_from,
                           std::int64_t low_to, std::int64_t step = 1)
{
    timeline::Curve c;
    c.variant = variant;
    for (std::int64_t f = 0; f < frames; f += step) {
        c.ref_index.push_back(f);
        c.vmaf.push_back(f >= low_from && f < low_to ? low : level);
    }
    c.context_first = { 0 };
    return c;
}

// A timeline of the reference (variant 0) and two pairs' variants 1 and 2,
// with `curves` and one session over `spans`, as {variant, from, to}.
struct SpanAt {
    int          variant;
    std::int64_t from, to;
};

timeline::Timeline hand_built(std::vector<timeline::Curve> curves, const std::vector<SpanAt>& spans)
{
    timeline::Timeline t;
    for (int v = 0; v < 3; ++v) {
        timeline::Variant var;
        var.index = v;
        var.pair  = v - 1;
        t.variants.push_back(var);
    }
    t.curves = std::move(curves);
    timeline::Session s;
    for (const SpanAt& sp : spans)
        s.spans.push_back({ .variant = sp.variant, .entry = static_cast<std::size_t>(sp.variant),
                            .ref_index = sp.from, .ref_end = sp.to, .scored = sp.variant != 0 });
    t.sessions.push_back(s);
    return t;
}

timeline::DetectOptions options(double t, std::int64_t w, std::int64_t n)
{
    return timeline::DetectOptions{ .threshold = t, .window = w, .min_points = n };
}

std::vector<Drop> drops_of(const timeline::Timeline& t, const timeline::DetectOptions& o)
{
    std::string err;
    std::vector<Drop> d = timeline::session_drops(t, 0, o, err);
    INFO(err);
    CHECK(err.empty());
    return d;
}

} // namespace

// ── The plan's file ──────────────────────────────────────────────────────────

TEST_CASE("session plan: the plan's file, version 1, read strictly", "[timeline][session][session_plan]")
{
    std::string err;
    const timeline::SessionPlan p = timeline::parse_session_plan(
        R"({"schema": "mirage-session-plan", "schema_version": 1, "name": "down at the ad",
            "spans": [{"entry": 1, "first_seq": 0, "last_seq": 2},
                      {"entry": 2, "first_seq": 3, "last_seq": 4},
                      {"entry": 1, "first_seq": 5, "last_seq": 7}]})",
        err);
    CHECK(err.empty());
    CHECK(p.name == "down at the ad");
    REQUIRE(p.spans.size() == 3);
    const std::size_t   entry[] = { 1, 2, 1 };
    const std::int64_t  first[] = { 0, 3, 5 }, last[] = { 2, 4, 7 };
    for (std::size_t k = 0; k < 3; ++k) {
        CHECK(p.spans[k].entry == entry[k]);
        CHECK(p.spans[k].first_seq == first[k]);
        CHECK(p.spans[k].last_seq == last[k]);
    }

    // Members in any order; an empty name; a span of one segment.
    const timeline::SessionPlan q = timeline::parse_session_plan(
        R"({"spans": [{"last_seq": 9, "entry": 0, "first_seq": 9}], "name": "",
            "schema_version": 1, "schema": "mirage-session-plan"})",
        err);
    CHECK(err.empty());
    CHECK(q.name.empty());
    REQUIRE(q.spans.size() == 1);
    CHECK(q.spans[0].entry == 0);
    CHECK(q.spans[0].first_seq == 9);
    CHECK(q.spans[0].last_seq == 9);

    // Each refusal names what is wrong, and leaves the plan empty.
    const std::string head = R"("schema": "mirage-session-plan", "schema_version": 1, "name": "n")";
    const auto plan_text = [&head](std::string_view spans) { return std::format("{{{}, \"spans\": {}}}", head, spans); };
    const std::pair<std::string, std::string> refused[] = {
        { "", "the session plan isn't JSON: line 1, column 1" },
        { "[1]", "the session plan is an array, not an object" },
        { R"({"schema_version": 1, "name": "n", "spans": []})", "the session plan has no member \"schema\"" },
        { R"({"schema": 1})", "the session plan's schema is an integer, not a string" },
        { R"({"schema": "mirage-timeline", "schema_version": 1})",
          "the session plan's schema is \"mirage-timeline\", not \"mirage-session-plan\"" },
        { R"({"schema": "mirage-session-plan"})", "the session plan has no member \"schema_version\"" },
        { R"({"schema": "mirage-session-plan", "schema_version": 2})",
          "the session plan's schema_version is 2: this reader reads version 1" },
        { R"({"schema": "mirage-session-plan", "schema_version": 1.0})",
          "the session plan's schema_version is a number, not an integer" },
        { R"({"schema": "mirage-session-plan", "schema_version": 1, "name": "n"})",
          "the session plan has no member \"spans\"" },
        { std::format("{{{}, \"spans\": [], \"seed\": 3}}", head),
          "the session plan has a member \"seed\", which isn't one of version 1's: schema, schema_version, "
          "name and spans" },
        { R"({"schema": "mirage-session-plan", "schema_version": 1, "name": 7, "spans": []})",
          "the session plan's name is an integer, not a string" },
        { plan_text("{}"), "the session plan's spans is an object, not an array" },
        { plan_text("[]"), "the session plan's spans is empty: a session plays one span at least" },
        { plan_text("[3]"), "the session plan's spans[0] is an integer, not an object" },
        { plan_text(R"([{"entry": 1, "first_seq": 0}])"), "the session plan's spans[0] has no member \"last_seq\"" },
        { plan_text(R"([{"entry": 1, "first_seq": 0, "last_seq": 2},
                       {"entry": 1, "frist_seq": 3, "last_seq": 4}])"),
          "the session plan's spans[1] has a member \"frist_seq\", which isn't one of version 1's: entry, "
          "first_seq and last_seq" },
        { plan_text(R"([{"entry": -1, "first_seq": 0, "last_seq": 2}])"),
          "the session plan's spans[0]'s entry is -1: it is a whole number from 0" },
        { plan_text(R"([{"entry": "1", "first_seq": 0, "last_seq": 2}])"),
          "the session plan's spans[0]'s entry is a string, not an integer" },
        { plan_text(R"([{"entry": 1, "first_seq": 0.5, "last_seq": 2}])"),
          "the session plan's spans[0]'s first_seq is a number, not an integer" },
        { plan_text(R"([{"entry": 1, "first_seq": 0, "last_seq": 18446744073709551616}])"),
          "the session plan's spans[0]'s last_seq is a number, not an integer" },
        { plan_text(R"([{"entry": 1, "first_seq": 4, "last_seq": 3}])"),
          "the session plan's spans[0]'s last_seq is 3, before its first_seq 4" },
    };
    for (const auto& [text, why] : refused) {
        INFO(text);
        const timeline::SessionPlan r = timeline::parse_session_plan(text, err);
        CHECK(contains(err, why));
        CHECK(r.spans.empty());
        CHECK(r.name.empty());
    }
}

// ── The session on the reference's frames ────────────────────────────────────

TEST_CASE("session plan: spans on the reference's frames through the segments the build placed, the "
          "reference's unscored in top-rung mode",
          "[timeline][session][session_plan]")
{
    const timeline::Timeline t = ladder();
    std::string err;
    const timeline::Session s = timeline::make_session(t, ladder_report().entries, example_plan(), err);
    CHECK(err.empty());
    CHECK(s.name == "a test session");
    CHECK(s.id.empty());                       // the caller's to give (design §7, §8)
    CHECK_FALSE(s.wallclock_anchor_utc);       // the fetch's (src/SessionFetch.h)
    REQUIRE(s.spans.size() == 3);
    // Entry 1 is variant 1, entry 3 variant 3: each pair's distorted
    // playlist, its segments paired frame for frame with the reference's.
    const int          variant[] = { 1, 3, 1 };
    const std::size_t  entry[]   = { 1, 3, 1 };
    const std::int64_t first[] = { 0, 3, 5 }, last[] = { 2, 4, 7 };
    const std::int64_t from[] = { 0, 75, 125 }, to[] = { 75, 125, 200 };
    for (std::size_t k = 0; k < 3; ++k) {
        INFO("span " << k);
        CHECK(s.spans[k].variant == variant[k]);
        CHECK(s.spans[k].entry == entry[k]);
        CHECK(s.spans[k].first_seq == first[k]);
        CHECK(s.spans[k].last_seq == last[k]);
        CHECK(s.spans[k].ref_index == from[k]);
        CHECK(s.spans[k].ref_end == to[k]);
        CHECK(s.spans[k].scored);
    }

    // The top rung, entry 0, is the reference in top-rung mode: it has no
    // score (D-105 (b)). A session may start and end inside the ladder.
    const timeline::Session top = timeline::make_session(
        t, ladder_report().entries,
        plan_of({ { .entry = 0, .first_seq = 1, .last_seq = 2 }, { .entry = 4, .first_seq = 3, .last_seq = 5 } }),
        err);
    CHECK(err.empty());
    REQUIRE(top.spans.size() == 2);
    CHECK(top.spans[0].variant == 0);
    CHECK(top.spans[0].ref_index == 25);
    CHECK(top.spans[0].ref_end == 75);
    CHECK_FALSE(top.spans[0].scored);
    CHECK(top.spans[1].variant == 4);
    CHECK(top.spans[1].ref_index == 75);
    CHECK(top.spans[1].ref_end == 150);
    CHECK(top.spans[1].scored);

    // Two spans of one entry in a row follow one another as any two do.
    const timeline::Session again = timeline::make_session(
        t, ladder_report().entries,
        plan_of({ { .entry = 2, .first_seq = 0, .last_seq = 3 }, { .entry = 2, .first_seq = 4, .last_seq = 7 } }),
        err);
    CHECK(err.empty());
    REQUIRE(again.spans.size() == 2);
    CHECK(again.spans[1].ref_index == 100);
    CHECK(again.spans[1].ref_end == 200);
}

TEST_CASE("session plan: a plan that names what the ladder doesn't have, or whose spans leave a gap or "
          "overlap, is refused, naming the frames",
          "[timeline][session][session_plan]")
{
    const timeline::Timeline t = ladder();
    const PlanSpan main{ .entry = 1, .first_seq = 0, .last_seq = 2 };

    CHECK(refusal(t, plan_of({})) == "the session plan has no span: a session plays one span at least");
    CHECK(refusal(t, plan_of({ main, { .entry = 9, .first_seq = 3, .last_seq = 4 } })) ==
          "the session plan's spans[1] (entry 9, segments 3 to 4) plays entry 9, which isn't one of the "
          "master's 5 entries");

    // A sixth entry of the master that no pair scored (as a pair that failed
    // would leave it): it has no variant on the timeline.
    std::vector<timeline::MasterEntry> six = ladder_report().entries;
    six.push_back({ .index = 5, .line = 13, .uri = "v144/index.m3u8", .url = "http://test.invalid/ladder/v144/index.m3u8",
                    .bandwidth = 200000, .video = true });
    CHECK(refusal(t, plan_of({ main, { .entry = 5, .first_seq = 3, .last_seq = 4 } }), six) ==
          "the session plan's spans[1] (entry 5, segments 3 to 4) plays entry 5 (v144/index.m3u8), whose "
          "playlist the timeline has no variant of: it is neither the reference nor a complete pair's "
          "distorted playlist, so it has no score and no segment on the reference's frames");

    CHECK(refusal(t, plan_of({ { .entry = 1, .first_seq = 6, .last_seq = 8 } })) ==
          "the session plan's spans[0] (entry 1, segments 6 to 8) plays segment 8 of variant 1 "
          "(http://test.invalid/ladder/v540/index.m3u8), which isn't on the timeline: its playlist lists no "
          "segment 8, or the build couldn't place it on the reference's frames, which one of the timeline's "
          "warnings then says (design §5.1)");
    CHECK(contains(refusal(t, plan_of({ { .entry = 1, .first_seq = 9, .last_seq = 9 } })),
                   "plays segment 9 of variant 1"));

    CHECK(refusal(t, plan_of({ main, { .entry = 3, .first_seq = 4, .last_seq = 4 } })) ==
          "the session plan's spans[0] (entry 1, segments 0 to 2) ends at reference frame 75 and its spans[1] "
          "(entry 3, segment 4) starts at frame 100: they leave reference frames 75 to 99 in no span, where "
          "each span starts where the one before it ended (D-105 (a))");
    CHECK(refusal(t, plan_of({ { .entry = 1, .first_seq = 0, .last_seq = 3 },
                               { .entry = 3, .first_seq = 3, .last_seq = 4 } })) ==
          "the session plan's spans[0] (entry 1, segments 0 to 3) ends at reference frame 100 and its spans[1] "
          "(entry 3, segments 3 to 4) starts at frame 75: they overlap on reference frames 75 to 99, where "
          "each span starts where the one before it ended (D-105 (a))");
    CHECK(refusal(t, plan_of({ { .entry = 1, .first_seq = 0, .last_seq = 2 },
                               { .entry = 3, .first_seq = 2, .last_seq = 2 } })) ==
          "the session plan's spans[0] (entry 1, segments 0 to 2) ends at reference frame 75 and its spans[1] "
          "(entry 3, segment 2) starts at frame 50: they overlap on reference frames 50 to 74, where each span "
          "starts where the one before it ended (D-105 (a))");
    CHECK(refusal(t, plan_of({ { .entry = 1, .first_seq = 5, .last_seq = 7 },
                               { .entry = 3, .first_seq = 0, .last_seq = 1 } })) ==
          "the session plan's spans[0] (entry 1, segments 5 to 7) ends at reference frame 200 and its spans[1] "
          "(entry 3, segments 0 to 1) starts at frame 0: it starts before the span before it does, where each "
          "span starts where the one before it ended (D-105 (a))");
    CHECK(refusal(t, plan_of({ { .entry = 1, .first_seq = 4, .last_seq = 3 } })) ==
          "the session plan's spans[0] (entry 1, segments 4 to 3) ends before it starts");

    // A variant whose segment map the demux pass refused has no segment on
    // the reference's frames (D-081 (c)).
    const timeline::Timeline r = ladder(3);
    CHECK(refusal(r, plan_of({ main, { .entry = 3, .first_seq = 3, .last_seq = 4 } })) ==
          "the session plan's spans[1] (entry 3, segments 3 to 4) plays variant 3 "
          "(http://test.invalid/ladder/v270/index.m3u8), whose segment map was refused: the test refuses it: "
          "its segments aren't on the reference's frames (D-081 (c))");
}

// ── Its events ───────────────────────────────────────────────────────────────

TEST_CASE("session plan: a variant_span over each span and a variant_switch at each boundary where the "
          "entry changes, a rung named by its entry and URI with both ranks",
          "[timeline][session][session_plan]")
{
    const timeline::Timeline t = with_session(example_plan());
    std::vector<const Event*> spans, switches;
    for (const Event& e : t.events) {
        if (e.type == "variant_span") spans.push_back(&e);
        if (e.type == "variant_switch") switches.push_back(&e);
    }
    REQUIRE(spans.size() == 3);
    REQUIRE(switches.size() == 2);

    // The ranks among the five rungs (D-075): entry 1 second from the top
    // and fourth from the bottom, entry 3 fourth from the top and second
    // from the bottom.
    const int          variant[] = { 1, 3, 1 };
    const int          top[]     = { 2, 4, 2 }, bottom[] = { 4, 2, 4 };
    const std::int64_t from[] = { 0, 75, 125 }, to[] = { 75, 125, 200 };
    const std::int64_t first[] = { 0, 3, 5 }, last[] = { 2, 4, 7 };
    const char* const  ids[] = { "player/variant_span/1/0", "player/variant_span/3/0", "player/variant_span/1/1" };
    for (std::size_t k = 0; k < 3; ++k) {
        INFO("span " << k);
        const Event& e = *spans[k];
        CHECK(e.id == ids[k]);
        CHECK(e.source == Source::Player);
        CHECK(e.variant == variant[k]);
        CHECK(e.session == 0);
        CHECK_FALSE(e.segment);
        CHECK(e.ref_index == from[k]);
        CHECK(e.ref_end == to[k]);
        CHECK(e.key.sequence == t.axis.keys[static_cast<std::size_t>(from[k])].sequence);
        CHECK(e.key.pts == t.axis.keys[static_cast<std::size_t>(from[k])].pts);
        CHECK_FALSE(e.wallclock_us);
        CHECK(names_of(e.payload) == std::vector<std::string>{ "entry", "uri", "bandwidth", "resolution",
                                                               "rank_from_top", "rank_from_bottom", "first_seq",
                                                               "last_seq", "scored" });
        check_rung(e.payload, variant[k], top[k], bottom[k]);
        CHECK(int_member(e.payload, "first_seq") == first[k]);
        CHECK(int_member(e.payload, "last_seq") == last[k]);
        CHECK(bool_member(e.payload, "scored") == true);
    }
    // Each switch at the first frame of the span it starts, from the rung
    // played before to the one played from there, on the latter's variant.
    const Event& down = *switches[0];
    CHECK(down.id == "player/variant_switch/3/0");
    CHECK(down.ref_index == 75);
    CHECK_FALSE(down.ref_end);
    CHECK(down.variant == 3);
    CHECK(down.session == 0);
    CHECK(down.key.sequence == 1);       // the ad's report sequence
    CHECK(down.key.pts == kOrigin);
    CHECK(names_of(down.payload) == std::vector<std::string>{ "from", "to", "origin" });
    check_rung(object_member(down.payload, "from"), 1, 2, 4);
    check_rung(object_member(down.payload, "to"), 3, 4, 2);
    CHECK(text_member(down.payload, "origin") == "plan");
    const Event& up = *switches[1];
    CHECK(up.id == "player/variant_switch/1/0");
    CHECK(up.ref_index == 125);
    check_rung(object_member(up.payload, "from"), 3, 4, 2);
    check_rung(object_member(up.payload, "to"), 1, 2, 4);

    // In top-rung mode a span of the reference is marked unscored; the
    // reference's rung is the top, first from the top and fifth from the
    // bottom.
    const timeline::Timeline r = with_session(
        plan_of({ { .entry = 0, .first_seq = 0, .last_seq = 2 }, { .entry = 3, .first_seq = 3, .last_seq = 7 } }));
    const auto ref_spans = events_of(r, "variant_span", 0);
    REQUIRE(ref_spans.size() == 1);
    CHECK(bool_member(ref_spans[0]->payload, "scored") == false);
    check_rung(ref_spans[0]->payload, 0, 1, 5);
    const auto to_270 = events_of(r, "variant_switch", 3);
    REQUIRE(to_270.size() == 1);
    check_rung(object_member(to_270[0]->payload, "from"), 0, 1, 5);

    // A boundary between two spans of one entry makes no switch: nothing
    // changes there (D-108 (b)). Each span keeps its variant_span.
    const timeline::Timeline same = with_session(
        plan_of({ { .entry = 2, .first_seq = 0, .last_seq = 3 }, { .entry = 2, .first_seq = 4, .last_seq = 7 } }));
    CHECK(events_of(same, "variant_switch", 2).empty());
    const auto halves = events_of(same, "variant_span", 2);
    REQUIRE(halves.size() == 2);
    CHECK((halves[0]->ref_index == 0 && halves[0]->ref_end == 100));
    CHECK((halves[1]->ref_index == 100 && halves[1]->ref_end == 200));

    // Two entries that name one playlist still switch, the master's entry
    // being what changes (D-108 (b)): entry 5, a second entry of v360's
    // playlist at another BANDWIDTH, names variant 2 as entry 2 does, and a
    // switch from one to the other is placed on that variant.
    timeline::BuildInputs twin_in = ladder_inputs();
    timeline::MasterEntry twin    = twin_in.scores.entries[2];
    twin.index     = 5;
    twin.line      = 13;
    twin.bandwidth = 1100000;
    twin_in.scores.entries.push_back(twin);
    twin_in.scores.pairs[1].entries = { 2, 5 };
    std::string twin_err;
    timeline::Timeline twins = timeline::build(twin_in, twin_err);
    INFO(twin_err);
    REQUIRE(twin_err.empty());
    REQUIRE(twins.variants[2].entries == std::vector<std::size_t>{ 2, 5 });
    timeline::Session two = timeline::make_session(
        twins, twin_in.scores.entries,
        plan_of({ { .entry = 2, .first_seq = 0, .last_seq = 3 }, { .entry = 5, .first_seq = 4, .last_seq = 7 } }),
        twin_err);
    REQUIRE(twin_err.empty());
    REQUIRE(two.spans.size() == 2);
    CHECK((two.spans[0].variant == 2 && two.spans[1].variant == 2));
    twins.sessions.push_back(std::move(two));
    const std::vector<Event> twin_events = timeline::session_events(twins, twin_in.scores.entries, 0);
    std::vector<const Event*> twin_switches;
    for (const Event& e : twin_events)
        if (e.type == "variant_switch") twin_switches.push_back(&e);
    REQUIRE(twin_switches.size() == 1);
    const Event& across = *twin_switches[0];
    CHECK(across.ref_index == 100);
    CHECK(across.variant == 2);
    const Value::Object& from_twin = object_member(across.payload, "from");
    const Value::Object& to_twin   = object_member(across.payload, "to");
    CHECK(int_member(from_twin, "entry") == 2);
    CHECK(int_member(to_twin, "entry") == 5);
    CHECK(text_member(from_twin, "uri") == text_member(to_twin, "uri"));
    CHECK(int_member(from_twin, "bandwidth") == kBandwidth[2]);
    CHECK(int_member(to_twin, "bandwidth") == 1100000);

    // The ladder's own events keep their ids: the session's are of sources
    // of their own (assign_ids() numbers each source, type and variant).
    const timeline::Timeline plain = ladder();
    for (const Event& e : plain.events) {
        const auto match = std::find_if(t.events.begin(), t.events.end(), [&e](const Event& x) { return x.id == e.id; });
        REQUIRE(match != t.events.end());
        CHECK(match->ref_index == e.ref_index);
        CHECK(match->type == e.type);
    }
    CHECK(timeline::session_events(t, ladder_report().entries, 1).empty());   // no such session
}

// ── Its curve ────────────────────────────────────────────────────────────────

TEST_CASE("session plan: the session's curve, the played pairs' points over its scored spans, a coarser "
          "pair's gaps kept",
          "[timeline][session][session_plan]")
{
    const timeline::Timeline t = with_session(example_plan());
    const timeline::Curve    c = timeline::curve_points(t, kSession);
    CHECK(c.variant == -1);
    REQUIRE(c.ref_index.size() == static_cast<std::size_t>(kFrames));
    REQUIRE(c.vmaf.size() == static_cast<std::size_t>(kFrames));
    for (std::int64_t i = 0; i < kFrames; ++i) {
        INFO("frame " << i);
        CHECK(c.ref_index[static_cast<std::size_t>(i)] == i);
        // v540 is pair 0, v270 pair 2 (D-068: the played variant's score).
        CHECK(c.vmaf[static_cast<std::size_t>(i)] == score(i >= 75 && i < 125 ? 2 : 0, i));
    }
    // Pair 0's contexts start at frames 0 and 75, pair 2's too: frame 0 is
    // in the span of pair 0, 75 in that of pair 2.
    CHECK(c.context_first == std::vector<std::int64_t>{ 0, 75 });

    // A pair's curve is as before; a session that isn't there has no point.
    CHECK(timeline::curve_points(t, CurveRef{ .index = 2 }).vmaf == t.curves[2].vmaf);
    CHECK(timeline::curve_points(t, CurveRef{ .kind = CurveRef::Kind::Session, .index = 1 }).ref_index.empty());
    CHECK(timeline::curve_points(t, CurveRef{ .kind = CurveRef::Kind::Session, .index = -1 }).ref_index.empty());

    // A span of the reference in top-rung mode has no point (D-105 (b)).
    const timeline::Timeline r = with_session(
        plan_of({ { .entry = 0, .first_seq = 0, .last_seq = 2 }, { .entry = 3, .first_seq = 3, .last_seq = 7 } }));
    const timeline::Curve unscored = timeline::curve_points(r, kSession);
    REQUIRE(unscored.ref_index.size() == 125);
    CHECK(unscored.ref_index.front() == 75);
    CHECK(unscored.ref_index.back() == 199);
    CHECK(unscored.vmaf.front() == score(2, 75));
    CHECK(unscored.context_first == std::vector<std::int64_t>{ 75 });

    // A pair on a coarser grid, at every other reference frame, keeps its
    // gaps: nothing is invented between its points (D-077).
    const timeline::Timeline g = hand_built({ flat_curve(1, 100, 90.0, 90.0, 0, 0),
                                              flat_curve(2, 100, 70.0, 70.0, 0, 0, 2) },
                                            { { 1, 0, 50 }, { 2, 50, 100 } });
    const timeline::Curve coarse = timeline::curve_points(g, kSession);
    REQUIRE(coarse.ref_index.size() == 75);
    for (std::size_t k = 0; k < 75; ++k) {
        INFO("point " << k);
        CHECK(coarse.ref_index[k] == (k < 50 ? static_cast<std::int64_t>(k) : 50 + 2 * static_cast<std::int64_t>(k - 50)));
        CHECK(coarse.vmaf[k] == (k < 50 ? 90.0 : 70.0));
    }
}

// ── Its scope ────────────────────────────────────────────────────────────────

TEST_CASE("session plan: an attribution of the session's curve lists the played variant's events in each "
          "span, the reference's and the session's own, and no other rung's",
          "[timeline][session][session_plan]")
{
    timeline::Timeline t = with_session(example_plan());
    add_requests(t, example_fetch(t));

    // Frames 50 to 99 meet the first span (v540, to frame 75) and the second
    // (v270, from 75).
    const Attribution a = timeline::attribute(t, kSession, Interval{ .ref_index = 50, .ref_end = 100 }, {});
    CHECK(a.curve.kind == CurveRef::Kind::Session);
    CHECK(a.points == 50);
    CHECK(listed(t, a, Source::Vmaf) == Ids{ "vmaf/context_start/3/0" });
    CHECK(listed(t, a, Source::Network) ==
          Ids{ "network/segment_request/1/2", "network/playlist_request/3/0", "network/segment_request/3/0" });
    // v540's segment 2 and none of its later events; v270's segment 3 and
    // discontinuity and none of its earlier ones; the reference's throughout.
    CHECK(listed(t, a, Source::Playlist) ==
          Ids{ "playlist/segment/0/2", "playlist/segment/1/2", "playlist/discontinuity/0/0",
               "playlist/discontinuity/3/0", "playlist/segment/0/3", "playlist/segment/3/3" });
    CHECK(listed(t, a, Source::Scte35) ==
          Ids{ "scte35/cue_out/0/0", "scte35/cue_out/3/0", "scte35/ad_break/0/0", "scte35/ad_break/3/0" });
    CHECK(listed(t, a, Source::Player) ==
          Ids{ "player/variant_span/1/0", "player/variant_switch/3/0", "player/variant_span/3/0" });
    CHECK(listed(t, a, Source::Decoder) ==
          Ids{ "decoder/keyframe/0/2", "decoder/keyframe/1/2", "decoder/keyframe/0/3", "decoder/keyframe/3/3" });

    // A margin widens the interval and the parts of the spans with it: from
    // frame 25, a second before, v540's segment 1 too.
    const Attribution m = timeline::attribute(t, kSession, Interval{ .ref_index = 50, .ref_end = 100 },
                                              { .margin_before = { 1, 1 } });
    CHECK(m.searched.ref_index == 25);
    CHECK(m.points == 50);
    CHECK(listed(t, m, Source::Playlist)[0] == "playlist/segment/0/1");
    CHECK(found(t, m, "playlist/segment/1/1"));
    CHECK_FALSE(found(t, m, "playlist/segment/3/1"));

    // An empty interval at frame 80 lists the spans that straddle it, of the
    // variant whose span straddles it, and no point (D-078).
    const Attribution e = timeline::attribute(t, kSession, Interval{ .ref_index = 80, .ref_end = 80 }, {});
    CHECK(e.points == 0);
    CHECK(listed(t, e, Source::Playlist) == Ids{ "playlist/segment/0/3", "playlist/segment/3/3" });
    CHECK(listed(t, e, Source::Player) == Ids{ "player/variant_span/3/0" });
    CHECK(listed(t, e, Source::Network) == Ids{ "network/segment_request/3/0" });

    // A pair's curve lists none of the session's events, as before (D-078).
    const Attribution p = timeline::attribute(t, CurveRef{ .index = kAdPair }, Interval{ .ref_index = 50, .ref_end = 100 }, {});
    CHECK(listed(t, p, Source::Network).empty());
    CHECK(listed(t, p, Source::Player).empty());
    CHECK(found(t, p, "playlist/segment/3/2"));

    // A session that isn't there has nothing.
    const Attribution none = timeline::attribute(t, CurveRef{ .kind = CurveRef::Kind::Session, .index = 1 },
                                                 Interval{ .ref_index = 0, .ref_end = 200 }, {});
    for (const auto& group : none.by_source) CHECK(group.empty());
    CHECK(none.points == 0);
}

// ── The work order's example ─────────────────────────────────────────────────

TEST_CASE("session plan: the work order's example on a hand-built timeline, a drop over a cue-out, a switch "
          "between two numbered rungs and a request of 2.3 s",
          "[timeline][session][session_plan]")
{
    // Every input is the test's: the five-rung ladder, the plan, and the
    // fetch's rows, v270's ad_000.ts 2.3 s from its request to its last byte.
    timeline::Timeline t = with_session(example_plan());
    add_requests(t, example_fetch(t));

    // The session's drop, T = 6, W = 50 and N = 12 (the acceptance tests'
    // values, the design's P-R): v270's drop, over the frames the session
    // plays v270, with v270's baseline (D-077).
    const timeline::DetectOptions o = options(6.0, 50, 12);
    const std::vector<Drop> drops = drops_of(t, o);
    REQUIRE(drops.size() == 1);
    const Drop& d = drops[0];
    CHECK(d.interval.ref_index == 75);
    CHECK(d.interval.ref_end == 125);
    CHECK(d.points == 50);
    CHECK(d.first_point == 75);
    CHECK(d.last_point == 124);
    std::string err;
    const std::vector<Drop> pair = timeline::detect(t.curves[kAdPair], o, err);
    REQUIRE(pair.size() == 1);
    CHECK(d.baseline == pair[0].baseline);
    CHECK(d.min == 25.0);
    CHECK(d.drop_magnitude == d.baseline - 25.0);

    const Attribution a = timeline::attribute(t, kSession, d.interval, {});
    CHECK(a.points == 50);

    // scte35: the cue-out at the drop's first frame, v270's and the
    // reference's copy (D-078), with the ad breaks that span the drop.
    CHECK(listed(t, a, Source::Scte35) ==
          Ids{ "scte35/cue_out/0/0", "scte35/cue_out/3/0", "scte35/ad_break/0/0", "scte35/ad_break/3/0",
               "scte35/cue_out_cont/0/0", "scte35/cue_out_cont/3/0" });
    const Found* cue = found(t, a, "scte35/cue_out/3/0");
    REQUIRE(cue);
    CHECK(cue->offset_frames == 0);
    CHECK(cue->offset_s == 0.0);
    CHECK(int_member(t.events[cue->event].payload, "line") == 14);

    // player: the switch at +0 from entry 1 to entry 3, second and fourth
    // from the top, fourth and second from the bottom (D-075), and v270's span.
    CHECK(listed(t, a, Source::Player) == Ids{ "player/variant_switch/3/0", "player/variant_span/3/0" });
    const Found* sw = found(t, a, "player/variant_switch/3/0");
    REQUIRE(sw);
    CHECK(sw->offset_frames == 0);
    CHECK(sw->offset_s == 0.0);
    CHECK_FALSE(sw->offset_end_frames);
    const timeline::Payload& switched = t.events[sw->event].payload;
    check_rung(object_member(switched, "from"), 1, 2, 4);
    check_rung(object_member(switched, "to"), 3, 4, 2);
    const Found* span = found(t, a, "player/variant_span/3/0");
    REQUIRE(span);
    CHECK(span->offset_end_frames == 50);
    CHECK(span->offset_end_s == 2.0);

    // network: the request for ad_000.ts over its segment's frames, 2.3 s
    // from its request to its last byte, status 200 (D-104 (a)); v270's
    // playlist, fetched the first time a span plays it, at that span's
    // first frame (D-105 (c)); and ad_001.ts.
    CHECK(listed(t, a, Source::Network) ==
          Ids{ "network/playlist_request/3/0", "network/segment_request/3/0", "network/segment_request/3/1" });
    const Found* req = found(t, a, "network/segment_request/3/0");
    REQUIRE(req);
    CHECK(req->offset_frames == 0);
    CHECK(req->offset_end_frames == 25);
    CHECK(req->offset_s == 0.0);
    CHECK(req->offset_end_s == 1.0);
    const Event& ad = t.events[req->event];
    CHECK(ad.segment == 3);
    CHECK(text_member(ad.payload, "url") == segment_url(3, 3));
    CHECK(double_member(ad.payload, "duration") == 2.3);
    CHECK(int_member(ad.payload, "status") == 200);
    CHECK(text_member(ad.payload, "origin") == "fetch");

    // The groups in section 3.1's order (D-078): vmaf, network, playlist,
    // scte35, player, decoder.
    CHECK(listed(t, a, Source::Vmaf) == Ids{ "vmaf/context_start/3/0" });
    CHECK(listed(t, a, Source::Playlist) ==
          Ids{ "playlist/discontinuity/0/0", "playlist/discontinuity/3/0", "playlist/segment/0/3",
               "playlist/segment/3/3", "playlist/segment/0/4", "playlist/segment/3/4" });
    CHECK(listed(t, a, Source::Decoder) ==
          Ids{ "decoder/keyframe/0/3", "decoder/keyframe/3/3", "decoder/keyframe/0/4", "decoder/keyframe/3/4" });
}

// ── Its drops ────────────────────────────────────────────────────────────────

TEST_CASE("session plan: a session's drops are its pairs' drops clipped to the spans that played them, by "
          "design §6.3's three rules",
          "[timeline][session][session_plan]")
{
    // Two pairs at 90 and 80, the first low at 20 over frames 20 to 59, the
    // second at 30 over frames 50 to 84; T = 6, W = 10 (a warm-up of 5
    // points).
    const std::vector<timeline::Curve> curves = { flat_curve(1, 100, 90.0, 20.0, 20, 60),
                                                  flat_curve(2, 100, 80.0, 30.0, 50, 85) };
    std::string err;
    const std::vector<Drop> own1 = timeline::detect(curves[0], options(6.0, 10, 12), err);
    const std::vector<Drop> own2 = timeline::detect(curves[1], options(6.0, 10, 12), err);
    REQUIRE(own1.size() == 1);
    REQUIRE(own2.size() == 1);
    CHECK(own1[0].baseline == 90.0);
    CHECK(own2[0].baseline == 80.0);

    SECTION("(ii) the pieces of two pairs that meet at a switch stay two drops, each with its own pair's "
            "numbers") {
        const timeline::Timeline t = hand_built(curves, { { 1, 0, 50 }, { 2, 50, 100 } });
        const std::vector<Drop> d = drops_of(t, options(6.0, 10, 12));
        REQUIRE(d.size() == 2);
        CHECK(d[0].interval.ref_index == 20);
        CHECK(d[0].interval.ref_end == 50);
        CHECK(d[0].points == 30);
        CHECK(d[0].first_point == 20);
        CHECK(d[0].last_point == 49);
        CHECK(d[0].baseline == 90.0);
        CHECK(d[0].min == 20.0);
        CHECK(d[0].mean == 20.0);
        CHECK(d[0].drop_magnitude == 70.0);
        CHECK(d[0].mean_drop == 70.0);
        CHECK(d[1].interval.ref_index == 50);
        CHECK(d[1].interval.ref_end == 85);
        CHECK(d[1].points == 35);
        CHECK(d[1].first_point == 50);
        CHECK(d[1].last_point == 84);
        CHECK(d[1].baseline == 80.0);
        CHECK(d[1].min == 30.0);
        CHECK(d[1].drop_magnitude == 50.0);

        // Each piece's points are the session curve's in its interval.
        const Attribution a = timeline::attribute(t, kSession, d[1].interval, {});
        CHECK(a.points == d[1].points);
        CHECK(a.min == d[1].min);
    }

    SECTION("(i) a piece of N points or fewer is no drop, though its pair's drop is") {
        // The second pair's drop has 35 points; clipped to frames 55 on it
        // keeps 30, N = 30 or more.
        const timeline::Timeline t = hand_built(curves, { { 1, 0, 55 }, { 2, 55, 100 } });
        std::vector<Drop> d = drops_of(t, options(6.0, 10, 30));
        REQUIRE(d.size() == 1);
        CHECK(d[0].interval.ref_index == 20);
        CHECK(d[0].interval.ref_end == 55);
        CHECK(d[0].points == 35);
        d = drops_of(t, options(6.0, 10, 29));
        REQUIRE(d.size() == 2);
        CHECK(d[1].interval.ref_index == 55);
        CHECK(d[1].points == 30);
        CHECK(timeline::detect(curves[1], options(6.0, 10, 30), err).size() == 1);
    }

    SECTION("(iii) a piece reports its pair's baseline at its own first point, the drop's") {
        const timeline::Timeline t = hand_built(curves, { { 1, 0, 70 }, { 2, 70, 100 } });
        const std::vector<Drop> d = drops_of(t, options(6.0, 10, 12));
        REQUIRE(d.size() == 2);
        CHECK(d[1].interval.ref_index == 70);
        CHECK(d[1].interval.ref_end == 85);
        CHECK(d[1].baseline == own2[0].baseline);
        // The session's own curve has the first pair's low points before
        // frame 60 in its window: a baseline of its own would differ.
        CHECK(d[0].interval.ref_end == 60);
    }

    SECTION("consecutive spans of one variant are one stretch, spans not scored have none") {
        // A drop of 20 points across a boundary between two spans of the
        // first pair's variant is one piece; with N = 12 neither half of it
        // would be a drop.
        const std::vector<timeline::Curve> short_drop = { flat_curve(1, 100, 90.0, 20.0, 20, 40),
                                                          flat_curve(2, 100, 80.0, 80.0, 0, 0) };
        timeline::Timeline t = hand_built(short_drop, { { 1, 0, 30 }, { 1, 30, 100 } });
        std::vector<Drop> d = drops_of(t, options(6.0, 10, 12));
        REQUIRE(d.size() == 1);
        CHECK(d[0].interval.ref_index == 20);
        CHECK(d[0].interval.ref_end == 40);
        CHECK(d[0].points == 20);

        // The reference's span isn't scored (D-105 (b)): no drop there, and
        // the session curve's positions start after it.
        t = hand_built(curves, { { 0, 0, 40 }, { 1, 40, 100 } });
        d = drops_of(t, options(6.0, 10, 12));
        REQUIRE(d.size() == 1);
        CHECK(d[0].interval.ref_index == 40);
        CHECK(d[0].interval.ref_end == 60);
        CHECK(d[0].points == 20);
        CHECK(d[0].first_point == 0);
        CHECK(d[0].last_point == 19);
    }

    SECTION("points without a score are skipped, as detect() skips them") {
        std::vector<timeline::Curve> gaps = curves;
        for (std::size_t i = 40; i < 45; ++i) gaps[0].vmaf[i] = std::numeric_limits<double>::quiet_NaN();
        const timeline::Timeline t = hand_built(gaps, { { 1, 0, 50 }, { 2, 50, 100 } });
        const std::vector<Drop> d = drops_of(t, options(6.0, 10, 12));
        REQUIRE(d.size() == 2);
        CHECK(d[0].interval.ref_index == 20);
        CHECK(d[0].interval.ref_end == 50);
        CHECK(d[0].points == 25);
        CHECK(d[0].last_point == 49);
    }

    SECTION("no detection without W and N; refusals") {
        const timeline::Timeline t = hand_built(curves, { { 1, 0, 50 }, { 2, 50, 100 } });
        CHECK(drops_of(t, timeline::DetectOptions{}).empty());
        CHECK(drops_of(t, timeline::DetectOptions{ .window = 10 }).empty());
        // Without W and N nothing else is checked, as detect() checks
        // nothing then: a W of 0 alone, or a session that isn't there, is
        // no error.
        CHECK(drops_of(t, timeline::DetectOptions{ .window = 0 }).empty());
        CHECK(timeline::detect(curves[0], timeline::DetectOptions{ .window = 0 }, err).empty());
        CHECK(err.empty());
        CHECK(timeline::session_drops(t, 1, timeline::DetectOptions{ .min_points = 12 }, err).empty());
        CHECK(err.empty());
        // With both given, the session first, then the options.
        CHECK(timeline::session_drops(t, 1, options(6.0, 10, 12), err).empty());
        CHECK(err == "the timeline has no session 1: it has 1 session");
        CHECK(timeline::session_drops(t, 1, options(6.0, 0, 12), err).empty());
        CHECK(err == "the timeline has no session 1: it has 1 session");
        CHECK(timeline::session_drops(t, 0, options(6.0, 0, 12), err).empty());
        CHECK(err == "the window W wants a whole number of points from 1, not 0");
        // A scored span whose variant no pair's curve scores.
        const timeline::Timeline lone = hand_built({ curves[0] }, { { 1, 0, 50 }, { 2, 50, 100 } });
        CHECK(timeline::session_drops(lone, 0, options(6.0, 10, 12), err).empty());
        CHECK(err == "the session's span at reference frames 50 to 99 is scored, but no pair's curve is of its "
                     "variant 2");
        // A curve detect() refuses, named by its pair's number.
        std::vector<timeline::Curve> bad = curves;
        bad[1].vmaf.pop_back();
        const timeline::Timeline broken = hand_built(bad, { { 1, 0, 50 }, { 2, 50, 100 } });
        CHECK(timeline::session_drops(broken, 0, options(6.0, 10, 12), err).empty());
        CHECK(err == "pair 1's curve: the curve has 100 reference frames for 99 scores");
        CHECK(timeline::session_drops(broken, 0, timeline::DetectOptions{}, err).empty());
        CHECK(err.empty());
    }
}

// ── Its fetch's files ────────────────────────────────────────────────────────

TEST_CASE("session plan: the fetch's files in play order, each played variant's playlist once, at the "
          "report's URLs or rebased onto a fetch base",
          "[timeline][session][session_plan]")
{
    const timeline::Timeline t = with_session(example_plan());
    std::string err;
    const std::vector<Request> files = timeline::fetch_files(t, 0, kMaster, "", err);
    CHECK(err.empty());

    // The master; v540's playlist and segments 0 to 2; v270's playlist and
    // segments 3 and 4; v540's segments 5 to 7, its playlist not again
    // (D-104 (a)).
    struct Want {
        FetchedFile kind;
        int variant;
        std::optional<std::int64_t> segment;
        std::string url;
    };
    const std::vector<Want> want = {
        { FetchedFile::Master, -1, std::nullopt, kMaster },
        { FetchedFile::MediaPlaylist, 1, std::nullopt, url_of(1) },
        { FetchedFile::Segment, 1, 0, segment_url(1, 0) },
        { FetchedFile::Segment, 1, 1, segment_url(1, 1) },
        { FetchedFile::Segment, 1, 2, segment_url(1, 2) },
        { FetchedFile::MediaPlaylist, 3, std::nullopt, url_of(3) },
        { FetchedFile::Segment, 3, 3, segment_url(3, 3) },
        { FetchedFile::Segment, 3, 4, segment_url(3, 4) },
        { FetchedFile::Segment, 1, 5, segment_url(1, 5) },
        { FetchedFile::Segment, 1, 6, segment_url(1, 6) },
        { FetchedFile::Segment, 1, 7, segment_url(1, 7) },
    };
    REQUIRE(files.size() == want.size());
    for (std::size_t k = 0; k < want.size(); ++k) {
        INFO("file " << k);
        CHECK(files[k].kind == want[k].kind);
        CHECK(files[k].variant == want[k].variant);
        CHECK(files[k].segment == want[k].segment);
        CHECK(files[k].url == want[k].url);
        CHECK(files[k].range_offset == -1);
        CHECK(files[k].range_length == -1);
        CHECK(files[k].chain.empty());
        CHECK(files[k].error.empty());
    }

    // The same paths from the master's directory, on a fetch base (design
    // §5.7), with or without its '/'.
    const std::vector<Request> rebased = timeline::fetch_files(t, 0, kMaster, "http://127.0.0.1:9", err);
    CHECK(err.empty());
    REQUIRE(rebased.size() == want.size());
    CHECK(rebased[0].url == "http://127.0.0.1:9/master.m3u8");
    CHECK(rebased[1].url == "http://127.0.0.1:9/v540/index.m3u8");
    CHECK(rebased[6].url == "http://127.0.0.1:9/v270/ad_000.ts");
    CHECK(timeline::fetch_files(t, 0, kMaster, "http://127.0.0.1:9/base/", err)[6].url ==
          "http://127.0.0.1:9/base/v270/ad_000.ts");

    // The session of the top rung: the reference's playlist and segments.
    const timeline::Timeline r = with_session(plan_of({ { .entry = 0, .first_seq = 6, .last_seq = 7 } }));
    const std::vector<Request> top = timeline::fetch_files(r, 0, kMaster, "", err);
    REQUIRE(top.size() == 4);
    CHECK(top[1].kind == FetchedFile::MediaPlaylist);
    CHECK(top[1].variant == 0);
    CHECK(top[1].url == url_of(0));
    CHECK(top[3].url == segment_url(0, 7));

    // Refusals: no session; a URL that isn't under the master's directory.
    CHECK(timeline::fetch_files(t, 1, kMaster, "", err).empty());
    CHECK(err == "the timeline has no session 1: it has 1 session");
    CHECK(timeline::fetch_files(t, 0, "http://test.invalid/other/master.m3u8", "http://127.0.0.1:9", err).empty());
    CHECK(err == "http://test.invalid/ladder/v540/index.m3u8 isn't under the master's directory "
                 "http://test.invalid/other/, so it has no path to fetch from http://127.0.0.1:9 (design §5.7)");
}

TEST_CASE("session plan: fetch_url, the same path from the master's directory on a fetch base",
          "[timeline][session][session_plan]")
{
    std::string err;
    const std::string base = "http://127.0.0.1:8080";
    CHECK(timeline::fetch_url("/data/ladder/v180/ad_000.ts", "/data/ladder/master.m3u8", base, err) ==
          "http://127.0.0.1:8080/v180/ad_000.ts");
    CHECK(err.empty());
    CHECK(timeline::fetch_url("/data/ladder/v180/ad_000.ts", "/data/ladder/master.m3u8", "", err) ==
          "/data/ladder/v180/ad_000.ts");
    // A master's query and fragment aren't part of its directory; a
    // segment's are kept.
    CHECK(timeline::fetch_url("https://cdn.test.invalid/a/v1/s.ts?t=1", "https://cdn.test.invalid/a/m.m3u8?t=1/x",
                              base + "/", err) == "http://127.0.0.1:8080/v1/s.ts?t=1");
    // A master in the current directory: only relative paths are under it.
    CHECK(timeline::fetch_url("v1/index.m3u8", "master.m3u8", base, err) == "http://127.0.0.1:8080/v1/index.m3u8");
    CHECK(timeline::fetch_url("/abs/v1/index.m3u8", "master.m3u8", base, err).empty());
    CHECK(err == "/abs/v1/index.m3u8 isn't under the master's directory (the current directory), so it has no "
                 "path to fetch from http://127.0.0.1:8080 (design §5.7)");
    CHECK(timeline::fetch_url("/data/other/x.ts", "/data/ladder/master.m3u8", base, err).empty());
    CHECK(contains(err, "isn't under the master's directory /data/ladder/"));
    CHECK(timeline::fetch_url("/data/ladder/", "/data/ladder/master.m3u8", base, err).empty());
}

TEST_CASE("session plan: fetch_url compares a URL with the master's directory once the dot segments of both "
          "are resolved, so that one that leaves it through ../ refuses the whole fetch (D-108 (c))",
          "[timeline][session][session_plan]")
{
    std::string err;
    const std::string base = "http://127.0.0.1:8080";
    // Where a URL holds dot segments, the checks below of its fetch at its
    // path from the master's directory with them resolved, and of a
    // refusal's message that gives it resolved, pin a choice pending the
    // owner (fetch_url(), src/SessionPlan.cpp); those of a refusal alone
    // rest on D-108 (c).
    // A media playlist that leaves the master's directory through "../": the
    // URL mirage_vmaf --ladder gives the entry ../other/v180/index.m3u8 of a
    // master given by an absolute path, joined to its directory as text
    // (vmafladder::resolve(), src/VMAFLadder.h), while its segments' URLs
    // come with their dot segments resolved, its URL being an absolute path
    // (hls::resolve_uri(), src/HlsAlign.cpp). Outside the directory,
    // whatever its text starts with.
    CHECK(timeline::fetch_url("/data/ladder/../other/v180/index.m3u8", "/data/ladder/master.m3u8", base, err)
              .empty());
    CHECK(err == "/data/ladder/../other/v180/index.m3u8 (/data/other/v180/index.m3u8 with its dot segments "
                 "resolved) isn't under the master's directory /data/ladder/, so it has no path to fetch from "
                 "http://127.0.0.1:8080 (design §5.7)");
    // One that leaves and comes back, or names a directory by ".": under it,
    // fetched at its path from it with its dot segments resolved, pending
    // the owner (fetch_url(), src/SessionPlan.cpp).
    CHECK(timeline::fetch_url("/data/ladder/v270/../v180/./index.m3u8", "/data/ladder/master.m3u8", base, err) ==
          "http://127.0.0.1:8080/v180/index.m3u8");
    CHECK(err.empty());
    CHECK(timeline::fetch_url("/data/ladder/../ladder/v180/ad_000.ts", "/data/ladder/master.m3u8", base, err) ==
          "http://127.0.0.1:8080/v180/ad_000.ts");
    // The master's own: its directory is the resolved one.
    CHECK(timeline::fetch_url("/data/ladder/v180/ad_000.ts", "/data/x/../ladder/./master.m3u8", base, err) ==
          "http://127.0.0.1:8080/v180/ad_000.ts");
    CHECK(timeline::fetch_url("/data/x/v180/ad_000.ts", "/data/x/../ladder/master.m3u8", base, err).empty());
    CHECK(err == "/data/x/v180/ad_000.ts isn't under the master's directory /data/ladder/, so it has no path to "
                 "fetch from http://127.0.0.1:8080 (design §5.7)");
    // An http URL's path; its host and query as they are, and a "../" at
    // the root removed, the path staying at the root (RFC 3986 §5.2.4).
    CHECK(timeline::fetch_url("https://cdn.test.invalid/a/../b/v1/s.ts", "https://cdn.test.invalid/a/m.m3u8", base,
                              err).empty());
    CHECK(timeline::fetch_url("https://cdn.test.invalid/a/./v1/../v2/s.ts?u=../x", "https://cdn.test.invalid/a/m.m3u8",
                              base, err) == "http://127.0.0.1:8080/v2/s.ts?u=../x");
    CHECK(timeline::fetch_url("http://cdn.test.invalid/../a/s.ts", "http://cdn.test.invalid/a/m.m3u8", base, err) ==
          "http://127.0.0.1:8080/s.ts");
    // Relative paths: a "../" that leaves the master's directory, the
    // current directory among them, and one that stays in a directory above
    // the current one.
    CHECK(timeline::fetch_url("../v1/index.m3u8", "master.m3u8", base, err).empty());
    CHECK(timeline::fetch_url("ladder/../../x.ts", "ladder/master.m3u8", base, err).empty());
    CHECK(timeline::fetch_url("../../x.ts", "../master.m3u8", base, err).empty());
    CHECK(timeline::fetch_url("v0/../v1/index.m3u8", "master.m3u8", base, err) ==
          "http://127.0.0.1:8080/v1/index.m3u8");
    CHECK(timeline::fetch_url("../ladder/v1/s.ts", "../ladder/master.m3u8", base, err) ==
          "http://127.0.0.1:8080/v1/s.ts");
    // A master given by a relative path whose entry leaves its directory:
    // the entry's media playlist and its segments, which keep their dot
    // segments, its URL being a relative path (hls::resolve_uri()), start
    // with the master's directory as text, and are outside it all the same.
    CHECK(timeline::fetch_url("beside/../ladder/v270/index.m3u8", "beside/master.m3u8", base, err).empty());
    CHECK(err == "beside/../ladder/v270/index.m3u8 (ladder/v270/index.m3u8 with its dot segments resolved) isn't "
                 "under the master's directory beside/, so it has no path to fetch from http://127.0.0.1:8080 "
                 "(design §5.7)");
    CHECK(timeline::fetch_url("beside/../ladder/v270/main_000.ts", "beside/master.m3u8", base, err).empty());
    // One that resolves to the current directory itself, which names no
    // file under the master's.
    CHECK(timeline::fetch_url("a/..", "master.m3u8", base, err).empty());
    CHECK(err == "a/.. (the current directory with its dot segments resolved) isn't under the master's directory "
                 "(the current directory), so it has no path to fetch from http://127.0.0.1:8080 (design §5.7)");
    // Without a fetch base, the URL as the report gives it.
    CHECK(timeline::fetch_url("/data/ladder/../other/x.ts", "/data/ladder/master.m3u8", "", err) ==
          "/data/ladder/../other/x.ts");
    CHECK(err.empty());

    // Through fetch_files(): a played variant's playlist outside the master's
    // directory refuses the whole fetch, though its segments are under it.
    timeline::Timeline t = with_session(example_plan());
    t.variants[1].url = "http://test.invalid/ladder/../outside/v540/index.m3u8";
    CHECK(timeline::fetch_files(t, 0, kMaster, "http://127.0.0.1:9", err).empty());
    CHECK(err == "http://test.invalid/ladder/../outside/v540/index.m3u8 (http://test.invalid/outside/v540/index.m3u8 "
                 "with its dot segments resolved) isn't under the master's directory http://test.invalid/ladder/, so "
                 "it has no path to fetch from http://127.0.0.1:9 (design §5.7)");
}

TEST_CASE("session plan: a segment's byte range is what the fetch reads of its file",
          "[timeline][session][session_plan]")
{
    // A playlist of EXT-X-BYTERANGE segments of one file, on the reference
    // alone in a ladder of one pair.
    const std::string ranged = "#EXTM3U\n#EXT-X-VERSION:4\n#EXT-X-TARGETDURATION:1\n#EXT-X-PLAYLIST-TYPE:VOD\n"
                               "#EXTINF:1.000000,\n#EXT-X-BYTERANGE:1000@0\nall.ts\n"
                               "#EXTINF:1.000000,\n#EXT-X-BYTERANGE:1200\nall.ts\n"
                               "#EXT-X-ENDLIST\n";
    timeline::BuildInputs in = ladder_inputs();
    in.scores.pairs.resize(1);
    in.variants.resize(2);
    for (auto& p : in.scores.pairs)
        for (auto* v : { &p.ref_index, &p.dis_index, &p.ref_pts, &p.dis_pts }) v->resize(50);
    in.scores.pairs[0].vmaf.resize(50);
    in.scores.pairs[0].context_first = { 0 };
    in.scores.pairs[0].ref_sequences = { { .first_index = 0, .first_pts = kOrigin, .frames = 50 } };
    in.scores.pairs[0].dis_sequences = in.scores.pairs[0].ref_sequences;
    for (int v = 0; v < 2; ++v) {
        in.variants[static_cast<std::size_t>(v)].playlist = timeline::parse_playlist_events(ranged, url_of(v), v);
        timeline::VariantMedia& m = in.variants[static_cast<std::size_t>(v)].media;
        m.segments = { { .seq = 0, .first_index = 0, .frames = 25, .bytes = 1000 },
                       { .seq = 1, .first_index = 25, .frames = 25, .bytes = 1200 } };
        m.keyframes = { 0, 25 };
        m.pts.resize(50);
        for (std::int64_t i = 0; i < 50; ++i) m.pts[static_cast<std::size_t>(i)] = ref_pts(i);
    }
    std::string err;
    timeline::Timeline t = timeline::build(in, err);
    REQUIRE(err.empty());
    timeline::Session s = timeline::make_session(t, in.scores.entries,
                                                 plan_of({ { .entry = 1, .first_seq = 0, .last_seq = 1 } }), err);
    REQUIRE(err.empty());
    t.sessions.push_back(s);
    const std::vector<Request> files = timeline::fetch_files(t, 0, kMaster, "", err);
    REQUIRE(err.empty());
    REQUIRE(files.size() == 4);
    CHECK(files[2].url == "http://test.invalid/ladder/v540/all.ts");
    CHECK(files[2].range_offset == 0);
    CHECK(files[2].range_length == 1000);
    CHECK(files[3].url == "http://test.invalid/ladder/v540/all.ts");
    CHECK(files[3].range_offset == 1000);
    CHECK(files[3].range_length == 1200);
    CHECK(files[1].range_offset == -1);
}

// ── Its requests ─────────────────────────────────────────────────────────────

TEST_CASE("session plan: one event per fetched file on the media axis, its wall clock its first row's start, "
          "a redirect's chain in one event, failures marked",
          "[timeline][session][session_plan]")
{
    timeline::Timeline t = with_session(example_plan());
    std::vector<Request> files = example_fetch(t);
    REQUIRE(files.size() == 11);
    // The master redirected once: a row of its 302 and one of the 200 from
    // the place it was sent to, the second starting where the first ended.
    files[0].chain = { row(kMaster, 1000, 1500, 302, "Found", -1),
                       row("http://test.invalid/elsewhere/master.m3u8", 2500, 1500) };
    // v540's segment 1 not found; its segment 2 cut short after 512 bytes,
    // a read error the row doesn't mark.
    files[3].chain = { row(files[3].url, 10000, 1000, 404, "Not Found", 0) };
    files[3].chain[0].error = "HTTP error 404 Not Found";
    files[3].error = "Server returned 404 Not Found";
    files[4].chain = { row(files[4].url, 11000, 2000, 200, "OK", 512) };
    files[4].chain[0].content_length = 12032;
    files[4].error = "Input/output error after 512 bytes";

    const timeline::RequestEvents net = timeline::request_events(t, 0, files);
    CHECK(net.warnings.empty());
    REQUIRE(net.events.size() == 11);

    // Placed on the media axis (D-105 (c)): the master at the session's
    // first frame; a playlist at the first frame of the first span that
    // plays its variant; a segment over its own frames.
    const std::int64_t at[]  = { 0, 0, 0, 25, 50, 75, 75, 100, 125, 150, 175 };
    const std::int64_t end[] = { -1, -1, 25, 50, 75, -1, 100, 125, 150, 175, 200 };
    for (std::size_t k = 0; k < 11; ++k) {
        INFO("file " << k);
        const Event& e = net.events[k];
        CHECK(e.source == Source::Network);
        CHECK(e.type == (files[k].kind == FetchedFile::Segment ? "segment_request" : "playlist_request"));
        CHECK(e.variant == files[k].variant);
        CHECK(e.session == 0);
        CHECK(e.segment == files[k].segment);
        CHECK(e.ref_index == at[k]);
        CHECK(e.ref_end == (end[k] < 0 ? std::nullopt : std::optional<std::int64_t>(end[k])));
        CHECK(e.key.sequence == t.axis.keys[static_cast<std::size_t>(at[k])].sequence);
        CHECK(e.key.pts == t.axis.keys[static_cast<std::size_t>(at[k])].pts);
        CHECK(e.wallclock_us == files[k].chain.front().t_start);
        CHECK(names_of(e.payload) == std::vector<std::string>{ "url", "range", "status", "status_text", "bytes",
                                                               "start", "first_byte", "end", "duration", "failed",
                                                               "error", "origin", "chain" });
        CHECK(text_member(e.payload, "url") == files[k].url);
        CHECK(null_member(e.payload, "range"));
        CHECK(text_member(e.payload, "origin") == "fetch");
        CHECK(list_member(e.payload, "chain").size() == files[k].chain.size());
    }

    // The redirected master is one event: from its first row's start to its
    // last row's end, its status the last row's, the chain in its payload
    // (D-104 (a)).
    const timeline::Payload& master = net.events[0].payload;
    CHECK(int_member(master, "status") == 200);
    CHECK(text_member(master, "status_text") == "OK");
    CHECK(int_member(master, "bytes") == 12032);
    CHECK(double_member(master, "start") == 0.001);
    CHECK(double_member(master, "first_byte") == 0.0035);
    CHECK(double_member(master, "end") == 0.004);
    CHECK(double_member(master, "duration") == 0.003);
    CHECK(bool_member(master, "failed") == false);
    CHECK(null_member(master, "error"));
    const Value::List& chain = list_member(master, "chain");
    REQUIRE(chain.size() == 2);
    const auto& hop = std::get<Value::Object>(chain[0].v);
    CHECK(names_of(hop) == std::vector<std::string>{ "url", "start", "sent", "first_byte", "end", "status",
                                                     "status_text", "bytes", "content_length", "failed", "error" });
    CHECK(text_member(hop, "url") == kMaster);
    CHECK(int_member(hop, "status") == 302);
    CHECK(text_member(hop, "status_text") == "Found");
    CHECK(null_member(hop, "bytes"));
    CHECK(double_member(hop, "start") == 0.001);
    CHECK(double_member(hop, "sent") == 0.0011);
    CHECK(double_member(hop, "end") == 0.0025);
    const auto& landed = std::get<Value::Object>(chain[1].v);
    CHECK(text_member(landed, "url") == "http://test.invalid/elsewhere/master.m3u8");
    CHECK(int_member(landed, "status") == 200);
    CHECK(int_member(landed, "content_length") == 12032);
    CHECK(bool_member(landed, "failed") == false);
    CHECK(null_member(landed, "error"));

    // A 404: failed, by its row and by the fetch (D-104 (a)).
    const timeline::Payload& missing = net.events[3].payload;
    CHECK(int_member(missing, "status") == 404);
    CHECK(bool_member(missing, "failed") == true);
    CHECK(text_member(missing, "error") == "Server returned 404 Not Found");
    CHECK(text_member(std::get<Value::Object>(list_member(missing, "chain")[0].v), "error") ==
          "HTTP error 404 Not Found");
    // A body cut short: status 200 and fewer bytes than its Content-Length,
    // failed by the fetch's own error.
    const timeline::Payload& cut = net.events[4].payload;
    CHECK(int_member(cut, "status") == 200);
    CHECK(int_member(cut, "bytes") == 512);
    CHECK(bool_member(cut, "failed") == true);
    CHECK(text_member(cut, "error") == "Input/output error after 512 bytes");
    // The 2.3 s request.
    CHECK(double_member(net.events[6].payload, "duration") == 2.3);

    // A local file's row has no HTTP status, nor a time it didn't reach; a
    // byte range is in the payload.
    std::vector<Request> local = { files[2] };
    local[0].chain = { RequestRow{ .url = "/data/v540/main_000.ts", .t_start = 7, .t_sent = 7, .t_first_byte = 9,
                                   .t_end = -1, .bytes = 1000, .content_length = 30000, .initiator = "playback" } };
    local[0].range_offset = 2000;
    local[0].range_length = 1000;
    const timeline::RequestEvents l = timeline::request_events(t, 0, local);
    REQUIRE(l.events.size() == 1);
    const timeline::Payload& lp = l.events[0].payload;
    CHECK(null_member(lp, "status"));
    CHECK(null_member(lp, "status_text"));
    CHECK(null_member(lp, "end"));
    CHECK(null_member(lp, "duration"));
    CHECK(int_member(object_member(lp, "range"), "offset") == 2000);
    CHECK(int_member(object_member(lp, "range"), "length") == 1000);
    CHECK(double_member(lp, "start") == 7e-6);

    // What can't be placed makes no event and a warning (design §5.1).
    std::vector<Request> odd = { files[1], files[2], files[5], files[6] };
    odd[0].chain.clear();                    // no row
    odd[1].segment = 8;                      // a segment the timeline doesn't have
    odd[2].variant = 2;                      // a playlist of a variant no span plays
    const timeline::RequestEvents w = timeline::request_events(t, 0, odd);
    REQUIRE(w.events.size() == 1);
    CHECK(w.events[0].segment == 3);
    CHECK(w.warnings == std::vector<std::string>{
        "the session fetch's request for variant 1's media playlist (" + url_of(1) +
            ") has no row of NetworkLogger's: it makes no event (design §5.1)",
        "the session fetch's request for segment 8 of variant 1 (" + segment_url(1, 0) +
            ") is of a segment that isn't on the timeline: it makes no event (design §5.1)",
        "the session fetch's request for variant 2's media playlist (" + url_of(3) +
            ") is of variant 2, which no span of the session plays: it makes no event (design §5.1)" });
    const timeline::RequestEvents none = timeline::request_events(t, 1, files);
    CHECK(none.events.empty());
    CHECK(none.warnings == std::vector<std::string>{
        "the timeline has no session 1: it has 1 session: none of the session fetch's 11 files makes an event" });
}
