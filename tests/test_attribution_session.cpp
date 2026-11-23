// The composite example (eval/phase3-survey/design.md §9.3 and §14, WP11b),
// test 3: the work order's example of an attribution, answered on a
// scripted session (DECISIONS.md D-068) over D-069's synthetic ad ladder
// (tests/SyntheticLadder.h), the ad from the main content's origin (D-080).
// The session's drop lists one event of each of three sources, each checked
// against the generator's layout, the ground truth:
//
// - scte35: the low rung's cue_out of the EXT-X-CUE-OUT line before the
//   ad's first segment, at offset 0 frames, and the reference's copy of it,
//   which a session's curve lists too (D-078, D-081 (f));
// - player: the session's variant_switch at the drop's first frame, from
//   the control rung's master entry to the low rung's, each with its URI and
//   both its BANDWIDTH ranks (D-075, D-099 (c)), its origin the plan;
// - network: the segment_request of the low rung's first ad segment, the
//   file the test's server answers late, by its URL on that server and its
//   media sequence number, over that segment's reference frames (D-105 (c)),
//   with status 200 and a duration of at least the delay (D-104 (a); the
//   design's P-S), longer than each other request of the session's fetch,
//   which the server answers at once.
//
// The plan is design §9.3's, written from the layout, a span per stretch of
// segments (D-105 (a)): the control rung, v270, over the main content before
// the ad; the low rung, v180, over the ad's two segments, where v180 scores
// low; and v270 again from the cue-in's segment to the end. The session's
// drop is then v180's drop over the ad clipped to the span that plays v180
// (D-077): the ad's reference frames.
//
// It runs the three programs as WP11a's test does (the design's P-T):
// mirage_vmaf --ladder in top-rung mode (D-002); mirage_vmaf timeline with
// the plan, the session's files fetched (D-104 (a)) at their paths from the
// master's directory (--fetch-base; D-108 (c)) from tests/HttpFileServer.h's
// server of the ladder's directory, whose delay rule holds the late
// segment's response head for kDelay, a delay of the test's own, under the
// analyzer's 5 s network limit (D-036; the design's P-O and P-S); and
// mirage_vmaf attribute --auto --format json on the session's curve.
// Detection's parameters are pinned on both command lines, T = 6, W = 50
// and N = 12, as in WP11a's test (D-077; the design's P-R), and --fetch on
// is given as well, so that no default is relied on. It reads the timeline
// document and the answer back with timeline::from_json() (src/TimelineDoc.h)
// and asserts the same of the session's drop in each: one, detected, over
// the ad's reference frames, listing the three events; and, by D-078's
// scope of a session's curve and its half-open overlap, nothing of the
// control rung, which the session doesn't play there, and none of the
// session's own events at the drop's end frame, where the plan switches
// back. Of the timeline document it also checks the session's spans, on the
// reference frames of the layout's segments (D-105 (a)), and its fetch: the
// plan's files, listed from the layout, each asked of the server once, in
// D-104 (a)'s order, and a network event for each, the late one the longest.
//
// What is asserted rests on decided rules (D-068, D-069, D-072 to D-078,
// D-081 (f), D-092 (d), D-099 (c), D-104, D-105, D-108): reference frame
// ranges, URIs, media sequence numbers, master entries and their ranks,
// event types, tag lines, a status, a measured duration against the delay,
// and offsets in frames; never seconds on the axis, scores, ids, a wall
// clock's value (only that the session has its anchor, D-108 (c)) or
// warnings (the design's P-R). Nor does it depend on WP09b's choices,
// which the owner kept as built (D-111): attribute's --curve session:<id>
// names the session's curve by the id the timeline document gives the
// session, whatever its form (D-111 (a)), and nothing here depends on
// which other curves detection runs on, in which order, or on
// detection.session_rules (D-111 (b), (f)).
//
// A test case of its own, so that ctest runs it in a process of its own,
// which makes its ladder once (the design's P-Q). With MIRAGE_KEEP_MEDIA=<dir>
// the ladder is kept under <dir>/<this test case's name>/ (the design's P-U;
// synthladder::media_dir()), which renaming the test case moves, beside the
// ladder report <name>.session.json, the plan <name>.session.plan.json, the
// timeline document <name>.session.timeline.json and the answer
// <name>.session.answer.json, <name> being the ladder's (synthladder::name()).
// The ladder carries the plain cue tags only, so no SCTE-35 message is
// decoded (design §5.5).

#include <catch2/catch_test_macros.hpp>

#include "HttpFileServer.h"
#include "SyntheticLadder.h"
#include "TestTemp.h"
#include "TimelineDoc.h"
#include "TimelineModel.h"
#include "TimelineQuery.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace fs = std::filesystem;
using synthladder::Layout;
using synthladder::SegmentLayout;
using timeline::Attribution;
using timeline::CurveRef;
using timeline::Event;
using timeline::Found;
using timeline::Source;
using timeline::Value;

namespace {

// How long the server holds the late segment's response head: the test's
// own delay, well under the analyzer's 5 s limit on a read (D-036;
// src/VMAFNet.h), which a longer one would fail, and far longer than the
// server takes to answer a file at once (the design's P-S).
constexpr std::chrono::milliseconds kDelay{ 1000 };

// The session's curve as --curve names it (design §7; D-079): this, then the
// session's id, the document's sessions[].id, which the test reads from the
// timeline document rather than writes, whatever its form, which the owner
// kept as built (D-111 (a); src/TimelineBuild.h).
constexpr std::string_view kSessionCurvePrefix = "session:";

// The session's place among the document's sessions, which an event's
// session and a curve reference count by: the only one, as a timeline holds
// one session at most (D-072).
constexpr int kSession = 0;

std::string read_text(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
}

void write_text(const fs::path& path, const std::string& text)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}

// ── mirage_vmaf, run as a program ────────────────────────────────────────────

// What a run of the built mirage_vmaf did.
struct ToolRun {
    int         status = -1;   // its exit status; -1: it didn't exit
    std::string out, err;      // its stdout and stderr
};

// Runs the built mirage_vmaf with `args` (the design's P-T), its stdout and
// stderr into files of the test's temporary directory (tests/TestTemp.h),
// and waits for it to end.
ToolRun run_tool(std::vector<std::string> args)
{
    static int runs = 0;
    ++runs;
    const fs::path out_path = testtemp::path(std::format("session_stdout_{}.txt", runs));
    const fs::path err_path = testtemp::path(std::format("session_stderr_{}.txt", runs));
    posix_spawn_file_actions_t files;
    posix_spawn_file_actions_init(&files);
    posix_spawn_file_actions_addopen(&files, STDOUT_FILENO, out_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_addopen(&files, STDERR_FILENO, err_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    args.insert(args.begin(), MIRAGE_VMAF_PATH);
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, argv[0], &files, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&files);
    REQUIRE(spawned == 0);
    int status = 0;
    pid_t waited = 0;
    while ((waited = ::waitpid(pid, &status, 0)) < 0 && errno == EINTR) {}
    REQUIRE(waited == pid);
    ToolRun run;
    run.status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    run.out    = read_text(out_path);
    run.err    = read_text(err_path);
    return run;
}

// `args` with detection's parameters, pinned (D-077; the design's P-R), as
// WP11a's test gives them: T = 6, the tool's default, given all the same so
// that no default is relied on; W = 50 and N = 12, the prototype's values,
// inside the region of
// eval/phase3-survey/design-evaluation/runs/05_detection_sweep.txt.
std::vector<std::string> with_detection(std::vector<std::string> args)
{
    for (const char* a : { "--threshold", "6", "--window", "50", "--min-points", "12" }) args.emplace_back(a);
    return args;
}

// ── Documents ────────────────────────────────────────────────────────────────

// A timeline document, read back (src/TimelineDoc.h): the events in
// canonical order, each Found naming its event by its index in them.
// attribute --format json's answer is one too, its attributions the answer
// (D-099 (c); src/TimelineCli.h).
struct Doc {
    bool                     ok = false;
    std::string              err;
    timeline::Timeline       timeline;
    std::vector<Attribution> attributions;
    timeline::DocInfo        info;
};

Doc read_doc(const std::string& text)
{
    Doc d;
    d.ok = timeline::from_json(text, d.timeline, d.attributions, d.info, d.err);
    return d;
}

// That `d` records detection as run with the parameters with_detection()
// gives (D-077): T 6, W 50 and N 12.
void check_parameters(const Doc& d)
{
    REQUIRE(d.info.detection);
    CHECK(d.info.detection->options.threshold == 6.0);
    CHECK(d.info.detection->options.window == 50);
    CHECK(d.info.detection->options.min_points == 12);
}

// Whether `c` is the session's curve.
bool session_curve(const CurveRef& c) { return c.kind == CurveRef::Kind::Session && c.index == kSession; }

// A payload's member `name`; nullptr when it has none. A payload object
// (Value::Object) is a Payload too.
const Value* member(const timeline::Payload& payload, std::string_view name)
{
    for (const auto& [n, v] : payload)
        if (n == name) return &v;
    return nullptr;
}

// A payload member's number, whichever of the two forms a document gives it
// back in (timeline::number_of()); nullopt when it isn't a number.
std::optional<double> number(const timeline::Payload& payload, std::string_view name)
{
    const Value* v = member(payload, name);
    return v ? timeline::number_of(*v) : std::nullopt;
}

// A payload member's text; nullopt when it isn't text.
std::optional<std::string> text(const timeline::Payload& payload, std::string_view name)
{
    const Value* v = member(payload, name);
    const std::string* s = v ? std::get_if<std::string>(&v->v) : nullptr;
    return s ? std::optional<std::string>(*s) : std::nullopt;
}

// A payload member's object; nullptr when it isn't one.
const Value::Object* object(const timeline::Payload& payload, std::string_view name)
{
    const Value* v = member(payload, name);
    return v ? std::get_if<Value::Object>(&v->v) : nullptr;
}

// ── The ladder's ground truth ────────────────────────────────────────────────

// The segment of media sequence number `index` in `layout`.
const SegmentLayout& segment(const Layout& layout, int index)
{
    const auto s = std::ranges::find(layout.segments, index, &SegmentLayout::index);
    REQUIRE(s != layout.segments.end());
    return *s;
}

// A span of the session's plan (D-105 (a)): a master entry, and the first
// and last media sequence numbers it plays.
struct PlanSpan {
    int entry = 0, first_seq = 0, last_seq = 0;
};

// The session's plan: design §9.3's, the control rung's entry for the
// segments before the cue-out's, the low rung's for the ad's, from the
// cue-out's segment to the one before the cue-in's, and the control rung's
// again from the cue-in's segment to the last. The master lists the rungs in
// synthladder::kRungs' order, so a rung's master entry is its index: entry 1
// (v270) for segments 0 to 2, entry 2 (v180) for 3 and 4, entry 1 for 5 to 7.
std::vector<PlanSpan> plan_spans(const Layout& layout)
{
    REQUIRE(!layout.low_segments.empty());
    REQUIRE(layout.low_segments.front() == layout.cue_out_segment);
    REQUIRE(layout.low_segments.back() + 1 == layout.cue_in_segment);
    return { { layout.control_rung, 0, layout.cue_out_segment - 1 },
             { layout.low_rung, layout.cue_out_segment, layout.cue_in_segment - 1 },
             { layout.control_rung, layout.cue_in_segment, synthladder::kSegments - 1 } };
}

// The plan's file, in the layout of src/SessionPlan.h, which the owner kept
// (D-108 (c)): its spans in play order (D-105 (a)), one to a line.
std::string plan_text(const Layout& layout)
{
    std::string spans;
    for (const PlanSpan& s : plan_spans(layout))
        spans += std::format("{}{{\"entry\": {}, \"first_seq\": {}, \"last_seq\": {}}}",
                             spans.empty() ? "" : ",\n           ", s.entry, s.first_seq, s.last_seq);
    return std::format("{{\"schema\": \"mirage-session-plan\", \"schema_version\": 1, \"name\": \"the composite "
                       "example\",\n"
                       " \"spans\": [{}]}}\n",
                       spans);
}

// The files the session's fetch asks for, by their paths from the master's
// directory, which a server of the ladder's directory answers (D-108 (c)), in
// D-104 (a)'s order: the master; then, span by span of the plan, the media
// playlist of the rung the span plays, the first time a span plays it, and
// the span's segments. For this plan: master.m3u8; v270/index.m3u8 and v270's
// main_000.ts to main_002.ts; v180/index.m3u8, ad_000.ts and ad_001.ts; and
// v270's main_005.ts to main_007.ts.
std::vector<std::string> fetch_order(const synthladder::Ladder& lad)
{
    const Layout&            layout = lad.layout;
    std::vector<std::string> out    = { fs::path(lad.master).filename().string() };
    std::vector<int>         played;   // the entries whose media playlist `out` has
    for (const PlanSpan& span : plan_spans(layout)) {
        const synthladder::RungLayout& rung = layout.rungs[static_cast<std::size_t>(span.entry)];
        if (std::ranges::find(played, span.entry) == played.end()) {
            played.push_back(span.entry);
            out.push_back(rung.uri);
        }
        for (int seq = span.first_seq; seq <= span.last_seq; ++seq)
            out.push_back(std::format("{}/{}", rung.name, segment(layout, seq).file));
    }
    return out;
}

// Rung `rung`'s two BANDWIDTH ranks among the ladder's rungs, from the top
// and from the bottom, 1 the highest and 1 the lowest, equal BANDWIDTHs
// sharing a rank (D-075, D-099 (c)), from the BANDWIDTHs the master gives.
std::pair<int, int> ranks(const Layout& layout, int rung)
{
    const std::int64_t b = layout.rungs[static_cast<std::size_t>(rung)].bandwidth;
    int above = 0, below = 0;
    for (const synthladder::RungLayout& r : layout.rungs) {
        above += r.bandwidth > b ? 1 : 0;
        below += r.bandwidth < b ? 1 : 0;
    }
    return { 1 + above, 1 + below };
}

// The variant of rung `rung` of `layout`, an index into the document's
// variants: the one variant that the master entry `rung` alone names and
// whose URL ends with the URI the master gives it, a rung's identity
// (D-075); -1 when the document has none or more than one.
int rung_variant(const timeline::Timeline& t, const Layout& layout, int rung)
{
    const std::vector<std::size_t> entries = { static_cast<std::size_t>(rung) };
    const std::string uri = "/" + layout.rungs[static_cast<std::size_t>(rung)].uri;
    int found = -1;
    for (std::size_t i = 0; i < t.variants.size(); ++i) {
        if (t.variants[i].entries != entries || !t.variants[i].url.ends_with(uri)) continue;
        if (found >= 0) return -1;
        found = static_cast<int>(i);
    }
    return found;
}

// The ladder's rungs in one document, indices into its variants: the
// reference, the top rung in top-rung mode (D-002), which is no pair's
// distorted input; the low rung, which the session plays over the ad; and
// the control rung, which it plays around it. Each document is read so,
// from its own variants.
struct Rungs {
    int reference = -1, low = -1, control = -1;
};

Rungs rungs_in(const Doc& d, const Layout& layout)
{
    const timeline::Timeline& t = d.timeline;
    Rungs r;
    r.reference = rung_variant(t, layout, 0);
    r.low       = rung_variant(t, layout, layout.low_rung);
    r.control   = rung_variant(t, layout, layout.control_rung);
    REQUIRE(r.reference >= 0);
    REQUIRE(r.low >= 0);
    REQUIRE(r.control >= 0);
    CHECK(t.variants[static_cast<std::size_t>(r.reference)].pair == -1);
    CHECK(t.variants[static_cast<std::size_t>(r.low)].pair >= 0);
    CHECK(t.variants[static_cast<std::size_t>(r.control)].pair >= 0);
    return r;
}

// ── The session's drop ───────────────────────────────────────────────────────

// What `a` lists in the group of `source` of the events of type `type` and
// of variant `variant`, and with `seq` of the segment of that media sequence
// number only, in the group's order.
std::vector<const Found*> listed(const Doc& d, const Attribution& a, Source source, std::string_view type,
                                 int variant, std::optional<std::int64_t> seq = std::nullopt)
{
    std::vector<const Found*> out;
    for (const Found& f : a.by_source[static_cast<std::size_t>(source)]) {
        REQUIRE(f.event < d.timeline.events.size());
        const Event& e = d.timeline.events[f.event];
        CHECK(e.source == source);
        if (e.type == type && e.variant == variant && (!seq || e.segment == *seq)) out.push_back(&f);
    }
    return out;
}

// The session's one drop in `d`, detected, over the reference frames
// `frames`, [first, end): its pairs' drops clipped to the spans that played
// them (D-077), v180's over the ad, whose span plays v180, and none of
// v270's, the control's, which has none.
const Attribution& session_drop(const Doc& d, std::pair<int, int> frames)
{
    REQUIRE(d.timeline.sessions.size() == 1);
    std::vector<const Attribution*> drops;
    for (const Attribution& a : d.attributions)
        if (session_curve(a.curve)) drops.push_back(&a);
    REQUIRE(drops.size() == 1);
    const Attribution& a = *drops[0];
    CHECK(a.detected);
    CHECK(a.interval.ref_index == frames.first);
    CHECK(a.interval.ref_end == frames.second);
    return a;
}

// The scte35 group of `a` holds variant `variant`'s cue_out of the
// EXT-X-CUE-OUT line, which sits where the segment whose URI follows it
// starts (design §5.1), the drop's first frame: offset 0 frames.
void check_cue_out(const Doc& d, const Attribution& a, int variant, const Layout& layout)
{
    const SegmentLayout& out_seg = segment(layout, layout.cue_out_segment);
    const auto tag = std::ranges::find_if(out_seg.tags, [](const synthladder::Tag& t) {
        return t.text.starts_with("#EXT-X-CUE-OUT:");
    });
    REQUIRE(tag != out_seg.tags.end());
    const std::vector<const Found*> cue = listed(d, a, Source::Scte35, "cue_out", variant, out_seg.index);
    REQUIRE(cue.size() == 1);
    const Event& e = d.timeline.events[cue[0]->event];
    CHECK(number(e.payload, "line") == tag->line);
    CHECK(text(e.payload, "raw") == tag->text);
    CHECK(e.ref_index == out_seg.first_frame);
    CHECK(cue[0]->offset_frames == 0);
}

// A rung of a variant_switch's payload (src/SessionPlan.h; D-108 (c)): rung
// `rung`'s master entry, its URI as the master gives it and its two ranks.
void check_rung(const Value::Object* o, const Layout& layout, int rung)
{
    REQUIRE(o);
    CHECK(number(*o, "entry") == rung);
    CHECK(text(*o, "uri") == layout.rungs[static_cast<std::size_t>(rung)].uri);
    const auto [from_top, from_bottom] = ranks(layout, rung);
    CHECK(number(*o, "rank_from_top") == from_top);
    CHECK(number(*o, "rank_from_bottom") == from_bottom);
}

// The player group of `a` holds the session's one variant_switch at the
// drop's first frame, offset 0 frames, a point: from the control rung, which
// the plan's first span plays, to the low rung, which its second plays
// (design §5.6; D-108 (b)), as the plan scripts it.
void check_switch(const Doc& d, const Attribution& a, const Layout& layout)
{
    std::vector<const Found*> found;
    for (const Found& f : a.by_source[static_cast<std::size_t>(Source::Player)]) {
        REQUIRE(f.event < d.timeline.events.size());
        if (d.timeline.events[f.event].type == "variant_switch") found.push_back(&f);
    }
    REQUIRE(found.size() == 1);
    const Event& e = d.timeline.events[found[0]->event];
    CHECK(e.session == kSession);
    CHECK_FALSE(e.ref_end);
    CHECK(e.ref_index == segment(layout, layout.cue_out_segment).first_frame);
    CHECK(found[0]->offset_frames == 0);
    {
        INFO("from");
        check_rung(object(e.payload, "from"), layout, layout.control_rung);
    }
    {
        INFO("to");
        check_rung(object(e.payload, "to"), layout, layout.low_rung);
    }
    CHECK(text(e.payload, "origin") == "plan");
}

// The network group of `a` holds the session's segment_request of variant
// `variant`'s segment `seg`, the late one, fetched from `url`: over the
// segment's reference frames, [its first frame, the next segment's) (D-105
// (c)), at the offsets in frames they give; status 200; and from its
// request to its last byte at least the time the server held its response
// head (the design's P-S).
void check_request(const Doc& d, const Attribution& a, int variant, const SegmentLayout& seg,
                   const std::string& url)
{
    const std::vector<const Found*> found = listed(d, a, Source::Network, "segment_request", variant, seg.index);
    REQUIRE(found.size() == 1);
    const Found& f = *found[0];
    const Event& e = d.timeline.events[f.event];
    CHECK(e.session == kSession);
    CHECK(text(e.payload, "url") == url);
    CHECK(number(e.payload, "status") == 200);
    const std::optional<double> duration = number(e.payload, "duration");
    REQUIRE(duration);
    CHECK(*duration >= std::chrono::duration<double>(kDelay).count());
    REQUIRE(e.ref_end);
    CHECK(e.ref_index == seg.first_frame);
    CHECK(*e.ref_end == seg.first_frame + seg.frames);
    CHECK(f.offset_frames == seg.first_frame - a.interval.ref_index);
    CHECK(f.offset_end_frames == seg.first_frame + seg.frames - a.interval.ref_index);
}

// What D-078 leaves out of `a`, the session's drop in `d`, `r` being the
// rungs in `d`. Each event it lists is the session's own, the reference's,
// or the low rung's, the variant the session plays over the whole drop, so
// that none is the control rung's copy of a segment or a tag (D-081 (f));
// and meets the drop's interval, [first, end), by D-078's rule, written out
// here: a point at frame f when first <= f < end, a span [s, e) when s < end
// and e > first. So none of the session's events that start at frame end,
// the switch back to the control rung, its span and its first segment's
// request, is listed. Each rule has something to leave out: the control
// rung has events over the drop's frames, and the session has events at
// frame end.
void check_left_out(const Doc& d, const Attribution& a, const Rungs& r)
{
    const std::int64_t first = a.interval.ref_index, end = a.interval.ref_end;
    const auto meets_drop = [&](const Event& e) {
        if (e.ref_end) return e.ref_index < end && *e.ref_end > first;
        return first <= e.ref_index && e.ref_index < end;
    };
    for (const std::vector<Found>& group : a.by_source)
        for (const Found& f : group) {
            REQUIRE(f.event < d.timeline.events.size());
            const Event& e = d.timeline.events[f.event];
            INFO(e.id);
            CHECK((e.session == kSession || e.variant == r.low || e.variant == r.reference));
            CHECK(e.variant != r.control);
            CHECK(meets_drop(e));
        }
    const std::vector<Event>& events = d.timeline.events;
    CHECK(std::ranges::any_of(events, [&](const Event& e) { return e.variant == r.control && meets_drop(e); }));
    CHECK(std::ranges::any_of(events, [&](const Event& e) {
        return e.session == kSession && e.type == "variant_switch" && e.ref_index == end;
    }));
}

} // namespace

TEST_CASE("attribution acceptance: test 3, the work order's example on a scripted session whose ad segment is "
          "answered late",
          "[timeline][attribution][acceptance]")
{
    // D-069's ad ladder, the ad from the main content's origin, made once in
    // this test case's process (the design's P-Q).
    const synthladder::Ladder& lad = synthladder::ladder({ .kind = synthladder::Kind::Ad });
    {
        INFO(lad.error);
        REQUIRE(lad.error.empty());
    }
    const Layout&     layout = lad.layout;
    const std::string name   = synthladder::name(lad.spec);
    WARN(std::format("{}: made with {}", name, synthladder::encoder_name(layout.encoder)));   // Q33 (c)

    const fs::path    dir    = synthladder::media_dir();
    const std::string stem   = name + ".session";
    const std::string report = (dir / (stem + ".json")).string();
    const std::string plan   = (dir / (stem + ".plan.json")).string();
    const std::string doc    = (dir / (stem + ".timeline.json")).string();
    const std::string answer = (dir / (stem + ".answer.json")).string();
    {
        const ToolRun scored = run_tool({ "--ladder", lad.master, "--out", report });   // top-rung mode (D-002)
        INFO(scored.err);
        REQUIRE(scored.status == 0);
    }
    write_text(plan, plan_text(layout));

    // The low rung's first ad segment, v180/ad_000.ts by its path from the
    // master's directory, answered late; every other file at once.
    const SegmentLayout& ad   = segment(layout, layout.cue_out_segment);
    const std::string    late = std::format("{}/{}", layout.rungs[static_cast<std::size_t>(layout.low_rung)].name,
                                            ad.file);
    std::string              base;     // the server's, which every URL fetched starts with
    std::vector<std::string> served;   // the paths asked of it, in order
    {
        HttpFileServer server(lad.dir, { .path = late, .delay = kDelay });
        REQUIRE(server.ok());
        base = server.base();
        const ToolRun built = run_tool(with_detection({ "timeline", "--report", report, "--out", doc, "--session",
                                                        plan, "--fetch", "on", "--fetch-base", base }));
        INFO(built.err);
        REQUIRE(built.status == 0);
        served = server.paths();
    }
    // Each file of the session's fetch asked for once, the late one too, in
    // D-104 (a)'s order (fetch_order()).
    const std::vector<std::string> files = fetch_order(lad);
    CHECK(served == files);
    const std::string late_url = std::format("{}/{}", base, late);

    const Doc built = read_doc(read_text(doc));
    {
        INFO(built.err);
        REQUIRE(built.ok);
    }
    REQUIRE(built.timeline.sessions.size() == 1);
    {
        // The session's curve by the id the timeline document gives the
        // session, whatever its form (D-111 (a)).
        const std::string curve = std::string(kSessionCurvePrefix) + built.timeline.sessions[kSession].id;
        const ToolRun r = run_tool(with_detection({ "attribute", "--timeline", doc, "--report", report, "--auto",
                                                    "--curve", curve, "--format", "json", "--out", answer }));
        INFO(r.err);
        REQUIRE(r.status == 0);
    }
    const Doc answered = read_doc(read_text(answer));
    {
        INFO(answered.err);
        REQUIRE(answered.ok);
    }

    // The timeline document: every rung with its segment map (D-081 (c));
    // the session of the plan, its files fetched, its spans on the
    // reference's frames from the layout's segments (D-105 (a)), each
    // scored, as none plays the reference (D-105 (b)); detection run with
    // the parameters given, on the session's curve among others.
    {
        const Rungs r = rungs_in(built, layout);
        for (const timeline::Variant& v : built.timeline.variants) {
            INFO(v.url);
            CHECK(v.segment_map == "ok");
        }
        const timeline::Session& s = built.timeline.sessions[kSession];
        CHECK(s.wallclock_anchor_utc);
        const std::int64_t out_frame = ad.first_frame;
        const std::int64_t in_frame  = segment(layout, layout.cue_in_segment).first_frame;
        REQUIRE(s.spans.size() == 3);
        CHECK((s.spans[0].variant == r.control && s.spans[0].ref_index == 0 && s.spans[0].ref_end == out_frame));
        CHECK((s.spans[1].variant == r.low && s.spans[1].ref_index == out_frame && s.spans[1].ref_end == in_frame));
        CHECK((s.spans[2].variant == r.control && s.spans[2].ref_index == in_frame &&
               s.spans[2].ref_end == synthladder::kFrames));
        for (const timeline::Span& span : s.spans) CHECK(span.scored);
        check_parameters(built);
        CHECK(std::ranges::any_of(built.info.detection->curves, session_curve));

        // The delay is what the late request's duration measures (the
        // design's P-S): a network event for each file asked of the server,
        // by its URL there (D-104 (a)), in the document's canonical order
        // (the design's P-I) rather than the fetch's, and each other request
        // took less time than the late one.
        std::optional<double>    late_duration;
        std::vector<double>      others;
        std::vector<std::string> urls;
        for (const Event& e : built.timeline.events) {
            if (e.source != Source::Network) continue;
            INFO(e.id);
            const std::optional<double>      duration = number(e.payload, "duration");
            const std::optional<std::string> url      = text(e.payload, "url");
            REQUIRE(duration);
            REQUIRE(url);
            urls.push_back(*url);
            if (*url == late_url) late_duration = duration;
            else others.push_back(*duration);
        }
        std::vector<std::string> fetched;
        for (const std::string& f : files) fetched.push_back(std::format("{}/{}", base, f));
        std::ranges::sort(urls);
        std::ranges::sort(fetched);
        CHECK(urls == fetched);
        REQUIRE(late_duration);
        for (const double other : others) CHECK(other < *late_duration);
    }

    // The answer: detection run with the parameters given, on the session's
    // curve alone, the one --curve names.
    check_parameters(answered);
    CHECK((answered.info.detection->curves.size() == 1 && session_curve(answered.info.detection->curves[0])));

    // The session's drop in both, over the ad's reference frames, and what
    // it lists (design §9.3).
    for (const auto& [what, d] : { std::pair{ "the timeline document", &built },
                                   std::pair{ "attribute's answer", &answered } }) {
        INFO(what);
        const Rungs        r = rungs_in(*d, layout);
        const Attribution& a = session_drop(*d, layout.low_frames());
        for (const int v : { r.low, r.reference }) {
            INFO(d->timeline.variants[static_cast<std::size_t>(v)].url);
            check_cue_out(*d, a, v, layout);
        }
        check_switch(*d, a, layout);
        check_request(*d, a, r.low, ad, late_url);
        check_left_out(*d, a, r);
    }
}
