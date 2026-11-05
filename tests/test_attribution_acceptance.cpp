// The acceptance tests of the work order's section 3.2 (DECISIONS.md D-069),
// as eval/phase3-survey/design.md §9.2 and §14 (WP11a) give them:
// attribution checked against the ground truth of the generator of D-069's
// synthetic ladders (tests/SyntheticLadder.h), each ladder's layout.
//
// 1. A segment encoded at a low quality: the degraded ladder, whose v270 has
//    segment 4 at the low quantizer. It passes when v270's curve has one
//    drop, over that segment's reference frames, whose playlist group holds
//    the segment by its media sequence number and its URI, the segment's span
//    holding the drop; and when v180, the control, has none.
// 2. A drop at an ad boundary: the ad ladder, whose v180 has its ad, the two
//    segments that EXT-X-DISCONTINUITY tags bound and EXT-X-CUE-OUT,
//    EXT-X-CUE-OUT-CONT and EXT-X-CUE-IN mark, at the low quantizer. It
//    passes when v180's curve has one drop, over the ad's reference frames,
//    whose scte35 group holds the cue_out of the EXT-X-CUE-OUT line at offset
//    0 frames and an ad_break that spans the drop, and whose playlist group
//    holds the discontinuity at offset 0; and when v270, the control, has
//    none. In each of D-080's three arrangements of the ad's timestamps
//    (Q34 (d)): restarting at the main content's origin, the acceptance test
//    proper; from 1 s; and continuing the main content's.
//
// Each case runs mirage_vmaf as its user does, as a program (the design's
// P-T): --ladder in top-rung mode (D-002); timeline with detection's
// parameters pinned at T = 6, W = 50 and N = 12, inside the region where the
// prototype found both drops exactly (D-077; the design's P-R); and
// attribute --auto --format json with the same parameters, once on the
// curve of each of the two lower rungs. It reads the timeline document and
// the two answers back with timeline::from_json() (src/TimelineDoc.h) and
// asserts the same of the document's attributions and of the answer on the
// low rung's curve, and that the answer on the control's curve has no drop.
// Each event it looks for, it looks for in the low rung's playlist and in
// the reference's: a pair's curve lists the events of both its inputs
// (D-078), and each rung keeps its copy of a tag (D-081 (f)). In each of the
// two documents with the drop it also checks what D-078 leaves out of the
// drop: the control rung's events, whose copies of the tags only its own
// curve lists, and every event that doesn't meet the drop's interval,
// half-open, among them those at its end frame: the segment after the drop,
// which starts there, and in the ad ladder the EXT-X-CUE-IN and the second
// EXT-X-DISCONTINUITY, which sit there (design §5.1). It checks as well that
// the ad_break ends at the cue-in (D-076), as its payload says, since the
// duration the cue-out declares ends on the same frame; and in every
// document that detection ran with the parameters given, in the timeline
// document on both lower rungs' curves and in each answer on the curve
// asked for alone, so that the control's lack of a drop is detection's
// answer on its curve.
//
// What is asserted rests on decided rules only (D-072, D-073, D-076 to
// D-078, D-081), whatever the owner answers to the choices still pending
// (the design's P-R): reference frame ranges, URIs, media sequence numbers,
// event types, playlist lines and offsets in frames; never seconds, scores,
// ids, ranks or warnings. So the test reaches what it checks through what the
// decisions settle:
// - a rung by its master entry and its URI (D-075), and the rung's curve
//   through its variant's pair, the document's own link from a curve to its
//   variant, whichever numbering of the pairs the owner keeps
//   (src/TimelineDoc.h), found again in each document, each answer
//   included, so that no document's numbering is used on another;
// - an event by its source's group, its type, its variant and the media
//   sequence number of its segment, and a tag's by its line, not by its id;
// - attribute given --report, --curve and all three parameters, so that it
//   relies on none of its defaults that are pending (src/TimelineCli.h).
// It takes two forms as built that are pending the owner, each read by one
// function only: the layout of attribute's JSON, a timeline document whose
// attributions are the answer (src/TimelineCli.h), by read_answer(); and
// the list of the curves detection ran on (detection.curves,
// docs/TIMELINE_FORMAT.md), by detected_pairs().
//
// One test case with a section per case, so that ctest runs the four cases
// in one process and each ladder is made once (the design's P-Q): four
// ladders per ctest run in this test case, the four design §9.5 counts for
// it. The encoder each ladder was made with (Q33 (c)) is in the test's
// output. With MIRAGE_KEEP_MEDIA=<dir> the ladders are kept under
// <dir>/<this test case's name>/ (the design's P-U; synthladder::media_dir()),
// which renaming the test case moves, each beside its ladder report
// <name>.json, its timeline document <name>.timeline.json and the answers
// <name>.<rung>.answer.json, <name> being the ladder's (synthladder::name())
// and <rung> each lower rung's. The tests need no session, no fetch and no
// SCTE-35 decoder: D-069's ladders carry the plain cue tags only (design
// §5.5).

#include <catch2/catch_test_macros.hpp>

#include "SyntheticLadder.h"
#include "TestTemp.h"
#include "TimelineDoc.h"
#include "TimelineModel.h"
#include "TimelineQuery.h"

#include <algorithm>
#include <cerrno>
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
using timeline::Event;
using timeline::Found;
using timeline::Source;
using timeline::Value;

namespace {

std::string read_text(const fs::path& path)
{
    std::ifstream f(path, std::ios::binary);
    return { std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>() };
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
    const fs::path out_path = testtemp::path(std::format("acceptance_stdout_{}.txt", runs));
    const fs::path err_path = testtemp::path(std::format("acceptance_stderr_{}.txt", runs));
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

// `args` with detection's parameters, pinned (D-077; the design's P-R): T =
// 6, the tool's default, given all the same so that no default is relied
// on; W = 50 and N = 12, the prototype's values, inside the region of
// eval/phase3-survey/design-evaluation/runs/05_detection_sweep.txt.
std::vector<std::string> with_detection(std::vector<std::string> args)
{
    for (const char* a : { "--threshold", "6", "--window", "50", "--min-points", "12" }) args.emplace_back(a);
    return args;
}

// ── Documents ────────────────────────────────────────────────────────────────

// A timeline document, read back (src/TimelineDoc.h): the events in
// canonical order, each Found naming its event by its index in them.
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

// attribute --format json's answer, read back. It is a timeline document
// whose attributions are the answer, as src/TimelineCli.h makes it: a layout
// the implementation chose and the owner hasn't settled, so that another
// layout changes this function only. Nothing else takes more of an answer
// than its Doc: its rungs and their curves are found in its own variants
// (rungs_in()).
Doc read_answer(const std::string& text) { return read_doc(text); }

// The pairs whose curves detection ran on, as `d`'s record of detection
// lists them, in its order; none where no detection ran. The list
// (detection.curves) is a form pending the owner (docs/TIMELINE_FORMAT.md),
// so that another form changes this function only.
std::vector<int> detected_pairs(const Doc& d)
{
    std::vector<int> out;
    if (!d.info.detection) return out;
    for (const timeline::CurveRef& c : d.info.detection->curves)
        if (c.kind == timeline::CurveRef::Kind::Pair) out.push_back(c.index);
    return out;
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

// A payload's member `name`; nullptr when it has none.
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

// The segment of media sequence number `index` in `layout`.
const SegmentLayout& segment(const Layout& layout, int index)
{
    const auto s = std::ranges::find(layout.segments, index, &SegmentLayout::index);
    REQUIRE(s != layout.segments.end());
    return *s;
}

// The variant of rung `rung` of `layout`, an index into the document's
// variants: the one variant that the master entry `rung` alone names (the
// master lists the rungs in synthladder::kRungs' order) and whose URL ends
// with the URI the master gives it, a rung's identity (D-075); -1 when the
// document has none or more than one.
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

// A case's rungs in one document: indices into its variants, and the two
// lower rungs' curves.
struct Rungs {
    int reference = -1, low = -1, control = -1;
    int low_pair = -1, control_pair = -1;
};

// The rungs of `layout` in `d`, each by its master entry and its URI
// (D-075): the reference, the top rung in top-rung mode (D-002), which is no
// pair's distorted input, and the two lower rungs, each with its curve
// through its variant's pair. Each document is read so, in its own
// numbering: the timeline document and each answer.
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
    r.low_pair     = t.variants[static_cast<std::size_t>(r.low)].pair;
    r.control_pair = t.variants[static_cast<std::size_t>(r.control)].pair;
    REQUIRE(r.low_pair >= 0);
    REQUIRE(r.control_pair >= 0);
    return r;
}

// The attributions of `d` on the curve of pair `pair`, in order.
std::vector<const Attribution*> on_curve(const Doc& d, int pair)
{
    std::vector<const Attribution*> out;
    for (const Attribution& a : d.attributions)
        if (a.curve.kind == timeline::CurveRef::Kind::Pair && a.curve.index == pair) out.push_back(&a);
    return out;
}

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

// ── One case ─────────────────────────────────────────────────────────────────

// A case as mirage_vmaf ran it: the ladder, the timeline document that
// mirage_vmaf timeline wrote, and attribute --auto's answers on the low
// rung's curve and on the control's.
struct Case {
    const synthladder::Ladder* ladder = nullptr;
    Doc built;
    Doc low, control;
};

// attribute --auto on the curve of rung `rung` of `layout`, the document
// `doc`'s pair `pair`, with detection's pinned parameters, the report and
// the curve given, written as JSON to `out` and read back, with the check
// that the answer says detection ran with those parameters on that rung's
// curve alone, found again through the answer's own variants.
Doc answer(const std::string& doc, const std::string& report, const Layout& layout, int rung, int pair,
           const std::string& out)
{
    const ToolRun r = run_tool(with_detection({ "attribute", "--timeline", doc, "--report", report, "--auto",
                                                "--curve", std::format("pair:{}", pair), "--format", "json",
                                                "--out", out }));
    INFO(r.err);
    REQUIRE(r.status == 0);
    Doc d = read_answer(read_text(out));
    INFO(d.err);
    REQUIRE(d.ok);
    check_parameters(d);
    const Rungs found = rungs_in(d, layout);
    const int   asked = rung == layout.low_rung ? found.low_pair : found.control_pair;
    CHECK(detected_pairs(d) == std::vector<int>{ asked });
    return d;
}

// The ladder of `spec`, made once in this process (the design's P-Q),
// scored, built and queried by mirage_vmaf as design §9.2 runs it, its files
// in synthladder::media_dir() (the design's P-U).
Case run_case(const synthladder::Spec& spec)
{
    Case c;
    const synthladder::Ladder& lad = synthladder::ladder(spec);
    {
        INFO(lad.error);
        REQUIRE(lad.error.empty());
    }
    c.ladder = &lad;
    const std::string name = synthladder::name(spec);
    WARN(std::format("{}: made with {}", name, synthladder::encoder_name(lad.layout.encoder)));   // Q33 (c)

    const fs::path    dir    = synthladder::media_dir();
    const std::string report = (dir / (name + ".json")).string();
    const std::string doc    = (dir / (name + ".timeline.json")).string();
    {
        const ToolRun scored = run_tool({ "--ladder", lad.master, "--out", report });   // top-rung mode (D-002)
        INFO(scored.err);
        REQUIRE(scored.status == 0);
    }
    {
        const ToolRun built = run_tool(with_detection({ "timeline", "--report", report, "--out", doc }));
        INFO(built.err);
        REQUIRE(built.status == 0);
    }
    c.built = read_doc(read_text(doc));
    {
        INFO(c.built.err);
        REQUIRE(c.built.ok);
    }

    const Layout& layout = lad.layout;
    const Rungs   r      = rungs_in(c.built, layout);
    // Every rung keeps its segment map (D-081 (c)): one refused would have
    // none of its segments, tags and cues, and would say why here.
    for (const timeline::Variant& v : c.built.timeline.variants) {
        INFO(v.url);
        CHECK(v.segment_map == "ok");
    }
    // Detection ran with the parameters given (D-077), and among the curves
    // it ran on are both lower rungs', so that the control's lack of a drop
    // is detection's answer on its curve (one_drop()).
    check_parameters(c.built);
    const std::vector<int> detected = detected_pairs(c.built);
    CHECK(std::ranges::count(detected, r.low_pair) == 1);
    CHECK(std::ranges::count(detected, r.control_pair) == 1);

    const auto answer_path = [&](int rung) {
        const char* rung_name = synthladder::kRungs[static_cast<std::size_t>(rung)].name;
        return (dir / std::format("{}.{}.answer.json", name, rung_name)).string();
    };
    c.low     = answer(doc, report, layout, layout.low_rung, r.low_pair, answer_path(layout.low_rung));
    c.control = answer(doc, report, layout, layout.control_rung, r.control_pair,
                       answer_path(layout.control_rung));
    return c;
}

// What both tests ask of `d` first (design §9.2): the low rung's curve has
// one drop, detected, over the reference frames `frames`, [first, end), and
// the control rung's curve has none, `r` being the rungs in `d`. Returns
// that drop.
const Attribution& one_drop(const Doc& d, const Rungs& r, std::pair<int, int> frames)
{
    CHECK(on_curve(d, r.control_pair).empty());
    const std::vector<const Attribution*> drops = on_curve(d, r.low_pair);
    REQUIRE(drops.size() == 1);
    const Attribution& a = *drops[0];
    CHECK(a.detected);
    CHECK(a.interval.ref_index == frames.first);
    CHECK(a.interval.ref_end == frames.second);
    return a;
}

// What D-078 leaves out of `a`, the drop on the low rung's curve of `d`, `r`
// being the rungs in `d`. Each event it lists is of one of the curve's
// inputs, the low rung's variant or the reference's, so that none is the
// control rung's copy of a segment or a tag (D-081 (f)); and meets the
// drop's interval, [first, end), by D-078's rule, written out here: a point
// at frame f when first <= f < end, a span [s, e) when s < end and e >
// first. So nothing that starts at frame end is listed: neither the segment
// after the drop nor, in the ad ladder, the EXT-X-CUE-IN and the second
// EXT-X-DISCONTINUITY, which sit where that segment starts (design §5.1).
// Each rule has something to leave out: the control rung has events over
// the drop's frames, and the low rung has events at frame end.
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
            CHECK((e.variant == r.low || e.variant == r.reference));
            CHECK(meets_drop(e));
        }
    const std::vector<Event>& events = d.timeline.events;
    CHECK(std::ranges::any_of(events, [&](const Event& e) { return e.variant == r.control && meets_drop(e); }));
    CHECK(std::ranges::any_of(events, [&](const Event& e) { return e.variant == r.low && e.ref_index == end; }));
}

// attribute's answer on the control rung's curve, the one curve answer()
// found that detection ran on: no drop (design §9.2).
void check_no_drop(const Doc& d) { CHECK(d.attributions.empty()); }

// Test 1's (design §9.2): `a`'s playlist group holds variant `variant`'s
// segment `seg`, by its media sequence number and its URI as the playlist
// writes it, whose span, [its first frame, the next segment's) on the
// reference's frames (design §5.1), holds the drop, at the offsets in frames
// its frames give.
void check_segment(const Doc& d, const Attribution& a, int variant, const SegmentLayout& seg)
{
    const std::vector<const Found*> found = listed(d, a, Source::Playlist, "segment", variant, seg.index);
    REQUIRE(found.size() == 1);
    const Found& f = *found[0];
    const Event& e = d.timeline.events[f.event];
    CHECK(text(e.payload, "uri") == seg.file);
    REQUIRE(e.ref_end);
    CHECK(e.ref_index <= a.interval.ref_index);
    CHECK(*e.ref_end >= a.interval.ref_end);
    CHECK(f.offset_frames == seg.first_frame - a.interval.ref_index);
    CHECK(f.offset_end_frames == seg.first_frame + seg.frames - a.interval.ref_index);
}

// Test 2's (design §9.2): `a`'s scte35 group holds variant `variant`'s
// cue_out of the EXT-X-CUE-OUT line, at offset 0 frames, and its ad_break,
// which spans the drop, from the cue-out's segment to the cue-in's, which
// ends it (D-076): its payload says that the cue-in ended it and gives both
// tags' lines (src/PlaylistEvents.h), since the duration the cue-out
// declares ends on the same frame here. Its playlist group holds the
// variant's discontinuity before the ad, at offset 0. Each tag sits where
// the segment whose URI follows it starts (design §5.1).
void check_ad(const Doc& d, const Attribution& a, int variant, const Layout& layout)
{
    const SegmentLayout& out_seg = segment(layout, layout.cue_out_segment);
    const SegmentLayout& in_seg  = segment(layout, layout.cue_in_segment);
    const auto tag = std::ranges::find_if(out_seg.tags, [](const synthladder::Tag& t) {
        return t.text.starts_with("#EXT-X-CUE-OUT:");
    });
    REQUIRE(tag != out_seg.tags.end());
    const auto in_tag = std::ranges::find_if(in_seg.tags, [](const synthladder::Tag& t) {
        return t.text == "#EXT-X-CUE-IN";
    });
    REQUIRE(in_tag != in_seg.tags.end());

    const std::vector<const Found*> cue = listed(d, a, Source::Scte35, "cue_out", variant, out_seg.index);
    REQUIRE(cue.size() == 1);
    const Event& cue_out = d.timeline.events[cue[0]->event];
    CHECK(number(cue_out.payload, "line") == tag->line);
    CHECK(text(cue_out.payload, "raw") == tag->text);
    CHECK(cue[0]->offset_frames == 0);

    const std::vector<const Found*> brk = listed(d, a, Source::Scte35, "ad_break", variant);
    REQUIRE(brk.size() == 1);
    const Event& ad_break = d.timeline.events[brk[0]->event];
    REQUIRE(ad_break.ref_end);
    CHECK(ad_break.ref_index <= a.interval.ref_index);
    CHECK(*ad_break.ref_end >= a.interval.ref_end);
    CHECK(ad_break.ref_index == out_seg.first_frame);
    CHECK(*ad_break.ref_end == in_seg.first_frame);
    CHECK(text(ad_break.payload, "end") == "cue_in");
    CHECK(number(ad_break.payload, "cue_out_line") == tag->line);
    CHECK(number(ad_break.payload, "cue_in_line") == in_tag->line);
    CHECK(brk[0]->offset_frames == 0);
    CHECK(brk[0]->offset_end_frames == in_seg.first_frame - a.interval.ref_index);

    // The ad's first EXT-X-DISCONTINUITY comes before the cue-out's segment.
    REQUIRE(out_seg.discontinuity);
    const std::vector<const Found*> disc =
        listed(d, a, Source::Playlist, "discontinuity", variant, out_seg.index);
    REQUIRE(disc.size() == 1);
    CHECK(number(d.timeline.events[disc[0]->event].payload, "line") == out_seg.discontinuity_line);
    CHECK(disc[0]->offset_frames == 0);
}

// The two documents with a drop: the timeline document, and attribute's
// answer on the low rung's curve.
std::vector<std::pair<const char*, const Doc*>> with_drop(const Case& c)
{
    return { { "the timeline document", &c.built }, { "attribute's answer", &c.low } };
}

// Test 2 on the ad ladder whose ad's timestamps are `ad`.
void ad_case(synthladder::AdTimestamps ad)
{
    const Case    c      = run_case({ .kind = synthladder::Kind::Ad, .ad = ad });
    const Layout& layout = c.ladder->layout;
    for (const auto& [what, d] : with_drop(c)) {
        INFO(what);
        const Rungs        r = rungs_in(*d, layout);
        const Attribution& a = one_drop(*d, r, layout.low_frames());
        check_left_out(*d, a, r);
        for (const int v : { r.low, r.reference }) {
            INFO(d->timeline.variants[static_cast<std::size_t>(v)].url);
            check_ad(*d, a, v, layout);
        }
    }
    check_no_drop(c.control);
}

} // namespace

TEST_CASE("attribution acceptance: a degraded segment and an ad boundary", "[timeline][attribution][acceptance]")
{
    SECTION("test 1: v270's segment 4, encoded at a low quality, is v270's one drop, and v180 has none")
    {
        const Case    c      = run_case({ .kind = synthladder::Kind::Degraded });
        const Layout& layout = c.ladder->layout;
        REQUIRE(layout.low_segments.size() == 1);
        const SegmentLayout& seg = segment(layout, layout.low_segments[0]);
        for (const auto& [what, d] : with_drop(c)) {
            INFO(what);
            const Rungs        r = rungs_in(*d, layout);
            const Attribution& a = one_drop(*d, r, layout.low_frames());
            check_left_out(*d, a, r);
            for (const int v : { r.low, r.reference }) {
                INFO(d->timeline.variants[static_cast<std::size_t>(v)].url);
                check_segment(*d, a, v, seg);
            }
        }
        check_no_drop(c.control);
    }

    SECTION("test 2, the ad's timestamps from the main content's origin: v180's ad is v180's one drop, from "
            "its cue-out, and v270 has none")
    {
        ad_case(synthladder::AdTimestamps::MainOrigin);
    }

    SECTION("test 2, the ad's timestamps from 1 s") { ad_case(synthladder::AdTimestamps::FromOneSecond); }

    SECTION("test 2, the ad's timestamps continuing the main content's")
    {
        ad_case(synthladder::AdTimestamps::Continuous);
    }
}
