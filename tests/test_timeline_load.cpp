// Unit tests of src/TimelineLoad.h (eval/phase3-survey/design.md §9.1): written by WP12.
//
// The panel's loader on documents and reports the test writes: the ad ladder
// of tests/test_timeline_build.cpp (testgen's shape,
// eval/phase3-survey/testgen.md §2.3), its report written by
// vmafreport::ladder_to_json from results built here, as
// tests/test_ladder_report_read.cpp writes one, and its document by
// timeline::to_json from the timeline the build makes of that report read
// back, with detection's drops, as mirage_vmaf timeline would write them:
// - matching hashes accepted: the timeline with the report's curves, every
//   pair's and the session's (curve_points()), each with its pyramid, the
//   document's drops and the axis's scale;
// - a report redacted as the eval scripts redact one, accepted by its scores
//   digest and said to be a redacted copy (P-Z); a changed report refused,
//   both hashes named; an unknown schema_version refused (Q05 (a)); a
//   document or report that can't be read, isn't one, or doesn't fit;
// - a load stopped through its stop token; the snapshot handed over once; a
//   second load stopping the first, whose result is never handed over; a
//   cancel; a result not taken dropped by the next load.

#include <catch2/catch_test_macros.hpp>

#include "FileHash.h"
#include "LadderReportRead.h"
#include "PlaylistEvents.h"
#include "SessionPlan.h"
#include "TestTemp.h"
#include "TimelineBuild.h"
#include "TimelineDoc.h"
#include "TimelineLoad.h"
#include "TimelineView.h"
#include "VMAFReport.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <format>
#include <fstream>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

using timeline::LoadResult;

namespace {

bool contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

// ── testgen's ad ladder, as tests/test_timeline_build.cpp builds it ──────────

constexpr std::int64_t kOrigin  = 900000;   // 10 s in MPEG-TS's 1/90000
constexpr std::int64_t kFrame25 = 3600;     // a frame at 25 fps
constexpr std::int64_t kFrames  = 200;      // eight segments of 25 frames
constexpr const char*  kRungs[] = { "v360", "v270", "v180" };

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

const std::string kLadderDir = "http://test.invalid/ladder";

std::string url_of(int variant) { return std::format("{}/{}/index.m3u8", kLadderDir, kRungs[variant]); }

std::int64_t ref_pts(std::int64_t i)
{
    return i < 75 || i >= 125 ? kOrigin + i * kFrame25 : kOrigin + (i - 75) * kFrame25;
}

double score(int pair, std::int64_t i)
{
    const double wobble = 0.25 * static_cast<double>(i % 4);
    if (pair == 0) return i == 75 ? 80.0 : 97.0 + wobble;
    return i >= 75 && i < 125 ? 25.0 + wobble : 91.0 + wobble;
}

// ── The report, as mirage_vmaf --ladder writes one ───────────────────────────

VMAFInputInfo input()
{
    VMAFInputInfo in;
    in.width           = 640;
    in.height          = 360;
    in.pix_fmt         = "yuv420p";
    in.color_space     = "bt709";
    in.color_range     = "tv";
    in.color_primaries = "bt709";
    in.color_trc       = "bt709";
    in.time_base_num   = 1;
    in.time_base_den   = 90000;
    in.ended           = true;
    return in;
}

// Pair `pair` of the ad ladder, every frame paired, its PTS in seconds as the
// analyzer writes them; `changed` moves one score.
VMAFResult pair_result(int pair, bool changed = false)
{
    VMAFResult r;
    r.done     = true;
    r.settings = { .model = "vmaf_v0.6.1", .model_source = "built-in", .scaler = "bicubic",
                   .pix_fmt = "yuv420p", .bit_depth = 8, .n_threads = 4, .n_subsample = 1 };
    r.ref_info = input();
    r.dis_info = input();
    r.ref_url  = url_of(0);
    r.dis_url  = url_of(pair + 1);
    for (std::int64_t i = 0; i < kFrames; ++i) {
        r.ref_index.push_back(i);
        r.dis_index.push_back(i);
        r.ref_pts.push_back(static_cast<double>(ref_pts(i)) / 90000.0);
        r.dis_pts.push_back(static_cast<double>(ref_pts(i)) / 90000.0);
        r.per_frame.push_back(changed && i == 40 ? 12.5 : score(pair, i));
    }
    r.alignment.grid           = "reference";
    r.alignment.tolerance      = 0.02;
    r.alignment.ref_frame_rate = "25/1";
    r.alignment.dis_frame_rate = "25/1";
    r.alignment.paired         = kFrames;
    r.alignment.contexts       = { { 0, 75 }, { 75, 125 } };
    r.alignment.ref_sequences  = { { .first_index = 0, .first_pts = 10.0, .frames = 75 },
                                   { .first_index = 75, .first_pts = 10.0, .frames = 125 } };
    r.alignment.dis_sequences  = r.alignment.ref_sequences;
    return r;
}

// The ladder run, as vmafladder::score() leaves it: top-rung mode, v360 the
// reference, v270 and v180 its pairs; `changed`: one of v180's scores moved;
// `failed`: v180's pair failed, so that the report has one complete pair.
vmafladder::Result ad_ladder(bool changed = false, bool failed = false)
{
    vmafladder::Result l;
    l.ladder           = kLadderDir + "/master.m3u8";
    l.fetched.location = l.ladder;
    l.fetched.sha256   = filehash::sha256_hex("the master's bytes");
    const std::int64_t bandwidth[]  = { 1000000, 600000, 350000 };
    const char* const  resolution[] = { "640x360", "480x270", "320x180" };
    for (int k = 0; k < 3; ++k)
        l.master.entries.push_back({ .index = static_cast<std::size_t>(k), .line = static_cast<std::size_t>(2 + 2 * k),
                                     .uri = std::format("{}/index.m3u8", kRungs[k]), .url = url_of(k),
                                     .bandwidth = bandwidth[k], .resolution = resolution[k],
                                     .codecs = "avc1.64001e", .video = true });
    l.plan.reference_mode    = "top-rung";
    l.plan.reference_url     = url_of(0);
    l.plan.reference_entries = { 0 };
    l.plan.pairs             = { { url_of(1), { 1 } }, { url_of(2), { 2 } } };
    l.pairs = { { .status = vmafladder::PairStatus::Complete, .result = pair_result(0), .started = 0.25,
                  .ended = 1.5 },
                { .status = vmafladder::PairStatus::Complete, .result = pair_result(1, changed),
                  .started = 0.5, .ended = 2.0 } };
    if (failed) {
        l.pairs[1].status = vmafladder::PairStatus::Failed;
        l.pairs[1].why    = "the test's";
    }
    l.status        = failed ? "failed" : "complete";
    l.jobs          = 2;
    l.max_running   = 2;
    l.peak_resident = 123456789;
    return l;
}

const vmafreport::RunInfo kRun{
    .tool = "mirage_vmaf", .reference_mode = "top-rung", .libvmaf_version = "3.0.0",
    .libvmaf_package_version = "3.1.0",
    .ffmpeg_libraries = { { "libavutil", "60.26.101" }, { "libavcodec", "62.28.101" },
                          { "libavformat", "62.12.101" } },
};

void write(const std::string& path, const std::string& text)
{
    std::ofstream f(path, std::ios::binary);
    f << text;
    REQUIRE(f);
}

// ── The document, as mirage_vmaf timeline writes one ─────────────────────────

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

const timeline::DetectOptions kDetect{ .threshold = 6, .window = 50, .min_points = 12 };

// A report and the document built from it, written under the test's directory.
struct Written {
    std::string report_path, report_text;
    std::string doc_path, doc_text;
    timeline::Timeline timeline;   // as the build made it, the session's included
    std::size_t drops = 0;
};

// The document of the report `report_text`, written at `name`.json and naming
// the report at `name`-report.json, by the hashes of `named_text` (the
// report's own by default); with a session (design §9.3's plan) when asked.
Written write_pair(const std::string& name, const std::string& report_text, bool session = false,
                   const std::string& named_text = {})
{
    Written w;
    w.report_text = report_text;
    w.report_path = testtemp::path(name + "-report.json").string();
    w.doc_path    = testtemp::path(name + ".json").string();
    write(w.report_path, report_text);

    std::string err;
    const std::string&           named  = named_text.empty() ? report_text : named_text;
    const timeline::LadderScores scores = timeline::read_ladder_report(report_text, err);
    INFO(err);
    REQUIRE(err.empty());
    timeline::BuildInputs in;
    in.scores = scores;
    for (int v = 0; v < 3; ++v) {
        timeline::VariantRead r;
        r.playlist = timeline::parse_playlist_events(kAdPlaylist, url_of(v), v);
        REQUIRE(r.playlist->error.empty());
        r.playlist_location = url_of(v);
        r.playlist_sha256   = filehash::sha256_hex(kAdPlaylist);
        r.media             = ad_media();
        in.variants.push_back(std::move(r));
    }
    w.timeline = timeline::build(in, err);
    REQUIRE(err.empty());
    timeline::DocInfo info;
    if (session) {
        const timeline::SessionPlan plan{ .name  = "down at the ad",
                                          .spans = { { .entry = 1, .first_seq = 0, .last_seq = 2 },
                                                     { .entry = 2, .first_seq = 3, .last_seq = 4 },
                                                     { .entry = 1, .first_seq = 5, .last_seq = 7 } } };
        REQUIRE(timeline::add_session(w.timeline, scores.entries, plan, err));
        info.session_plan = timeline::FileRef{ .path = "plan.json", .sha256 = filehash::sha256_hex("plan") };
    }
    const std::vector<timeline::CurveRef> curves = timeline::curve_refs(w.timeline);
    const std::vector<timeline::Attribution> drops =
        timeline::attribute_drops(w.timeline, curves, kDetect, {}, err);
    REQUIRE(err.empty());
    w.drops = drops.size();

    info.ffmpeg_libraries     = { { "libavutil", "60.26.101" } };
    info.report               = timeline::report_info(scores);
    info.report.path          = w.report_path;
    info.report.sha256        = filehash::sha256_hex(named);
    info.report.scores_sha256 = filehash::sha256_hex(timeline::scores_text(
        timeline::read_ladder_report(named, err)));
    REQUIRE(timeline::reference_info(scores, w.timeline.axis, info.reference, err));
    info.detection = timeline::Detection{ .options = kDetect, .curves = curves };
    w.doc_text     = timeline::to_json(w.timeline, drops, info);
    {
        timeline::Timeline                 back;
        std::vector<timeline::Attribution> back_drops;
        timeline::DocInfo                  back_info;
        INFO(err);
        REQUIRE(timeline::from_json(w.doc_text, back, back_drops, back_info, err));
    }
    write(w.doc_path, w.doc_text);
    return w;
}

// The eval scripts' redaction (eval/ladder/pool_check.py,
// eval/consistency/ladder_check.py): every occurrence of a path replaced by
// a placeholder, as tests/test_ladder_report_read.cpp's redact() does.
std::string redacted(std::string text, const std::string& path, const std::string& placeholder)
{
    for (std::size_t at = text.find(path); at != std::string::npos; at = text.find(path, at + placeholder.size()))
        text.replace(at, path.size(), placeholder);
    return text;
}

// The loader's result once it has one, waited for up to 30 s.
std::optional<LoadResult> wait_for(timeline::Loader& loader)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (std::chrono::steady_clock::now() < until) {
        if (std::optional<LoadResult> r = loader.take()) return r;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return std::nullopt;
}

// A read that waits until its load is stopped, then fails, recording that
// it was entered and stopped.
struct BlockingRead {
    std::mutex                  mutex;
    std::condition_variable_any cv;
    bool entered = false;
    bool stopped = false;

    timeline::ReadFile reader()
    {
        return [this](const std::string&, std::stop_token stop, std::string&, std::string& err) {
            std::unique_lock lk(mutex);
            entered = true;
            cv.notify_all();
            cv.wait(lk, stop, [] { return false; });
            stopped = stop.stop_requested();
            cv.notify_all();
            err = "stopped";
            return false;
        };
    }
    void wait_entered()
    {
        std::unique_lock lk(mutex);
        cv.wait_for(lk, std::chrono::seconds(30), [this] { return entered; });
        REQUIRE(entered);
    }
    void wait_stopped()
    {
        std::unique_lock lk(mutex);
        cv.wait_for(lk, std::chrono::seconds(30), [this] { return stopped; });
        REQUIRE(stopped);
    }
};

} // namespace

// ── What a load gives ────────────────────────────────────────────────────────

TEST_CASE("timeline::load_snapshot: a document and the report it names, by the report's SHA-256: the "
          "timeline with the report's curves, their pyramids, the document's drops, the axis's scale",
          "[timeline][timeline_load]")
{
    const Written w = write_pair("load-plain", vmafreport::ladder_to_json(ad_ladder(), kRun));
    const LoadResult r = timeline::load_snapshot({ .document = w.doc_path });
    INFO(r.error);
    REQUIRE(r.snapshot);
    CHECK(r.error.empty());
    CHECK_FALSE(r.stopped);
    const timeline::Snapshot& s = *r.snapshot;
    CHECK(s.document_path == w.doc_path);
    CHECK(s.report_path == w.report_path);   // the one the document names
    CHECK_FALSE(s.report_redacted);
    CHECK(s.report_check == "its SHA-256 is the one the document names");
    CHECK(s.info.report.sha256 == filehash::sha256_hex(w.report_text));

    // The report's curves on the document's timeline, each pair's.
    REQUIRE(s.timeline.curves.size() == 2);
    REQUIRE(s.curves.size() == 2);
    for (std::size_t k = 0; k < 2; ++k) {
        INFO(k);
        CHECK(s.curves[k].ref.kind == timeline::CurveRef::Kind::Pair);
        CHECK(s.curves[k].ref.index == static_cast<int>(k));
        CHECK(s.curves[k].points.ref_index == w.timeline.curves[k].ref_index);
        CHECK(s.curves[k].points.vmaf == w.timeline.curves[k].vmaf);
        CHECK(s.curves[k].points.variant == w.timeline.curves[k].variant);
        CHECK(s.curves[k].envelope.size() == 200);
    }
    const timeline::MinMax all = timeline::range_min_max(s.curves[1].envelope, 0, 200);
    CHECK(all.min == 25.0);    // v180's ad
    CHECK(all.min_at == 76);   // the first at the lowest wobble: frames 76, 80, ... are 25.0
    CHECK(s.scores.pairs.size() == 2);
    CHECK(s.scores.entries.size() == 3);

    // The document's drops, and the events and axis as built.
    REQUIRE(w.drops == 1);
    REQUIRE(s.attributions.size() == 1);
    CHECK(s.attributions[0].detected);
    CHECK(s.attributions[0].interval.ref_index == 75);
    CHECK(s.attributions[0].interval.ref_end == 125);
    CHECK(s.timeline.events.size() == w.timeline.events.size());
    CHECK(s.scale.seconds);
    CHECK(s.scale.frames == 200);
    CHECK(s.scale.position(75) == 3.0);   // the ad's first frame, on the playlist time
    CHECK(s.scale.hi == 8.0);
    CHECK(s.info.detection);
}

TEST_CASE("timeline::load_snapshot: a session's curve, derived from its pairs' by curve_points()",
          "[timeline][timeline_load]")
{
    const Written w = write_pair("load-session", vmafreport::ladder_to_json(ad_ladder(), kRun), true);
    const LoadResult r = timeline::load_snapshot({ .document = w.doc_path });
    INFO(r.error);
    REQUIRE(r.snapshot);
    const timeline::Snapshot& s = *r.snapshot;
    REQUIRE(s.timeline.sessions.size() == 1);
    REQUIRE(s.curves.size() == 3);
    const timeline::PanelCurve& c = s.curves[2];
    CHECK(c.ref.kind == timeline::CurveRef::Kind::Session);
    CHECK(c.ref.index == 0);
    const timeline::Curve want = timeline::curve_points(w.timeline, c.ref);
    CHECK(c.points.ref_index == want.ref_index);
    CHECK(c.points.vmaf == want.vmaf);
    REQUIRE(c.points.ref_index.size() == 200);   // every span plays a pair's variant
    // v270 over segments 0 to 2 and 5 to 7, v180 over 3 and 4.
    CHECK(c.points.vmaf[30] == score(0, 30));
    CHECK(c.points.vmaf[80] == score(1, 80));
    CHECK(timeline::range_min_max(c.envelope, 0, 200).min == 25.0);
    // The drops: v180's, and the session's, its pair's clipped to its spans.
    CHECK(s.attributions.size() == 2);
}

TEST_CASE("timeline::load_snapshot: a redacted copy of the report accepted by its scores digest; a "
          "changed report refused, both hashes named",
          "[timeline][timeline_load]")
{
    const std::string report = vmafreport::ladder_to_json(ad_ladder(), kRun);
    const Written w = write_pair("load-redacted", report);

    // Redacted as the eval scripts redact a committed copy: its bytes differ,
    // its scores digest doesn't (P-Z).
    const std::string copy      = redacted(report, kLadderDir, "<ladder>");
    const std::string copy_path = testtemp::path("load-redacted-copy.json").string();
    REQUIRE(copy != report);
    write(copy_path, copy);
    LoadResult r = timeline::load_snapshot({ .document = w.doc_path, .report = copy_path });
    INFO(r.error);
    REQUIRE(r.snapshot);
    CHECK(r.snapshot->report_path == copy_path);
    CHECK(r.snapshot->report_redacted);
    CHECK(contains(r.snapshot->report_check, "a redacted copy (D-072)"));
    CHECK(r.snapshot->curves.size() == 2);

    // One score moved: neither hash is the document's.
    const std::string changed      = vmafreport::ladder_to_json(ad_ladder(true), kRun);
    const std::string changed_path = testtemp::path("load-changed.json").string();
    write(changed_path, changed);
    r = timeline::load_snapshot({ .document = w.doc_path, .report = changed_path });
    CHECK_FALSE(r.snapshot);
    CHECK_FALSE(r.stopped);
    CHECK(contains(r.error, "isn't the ladder report the document was built from"));
    CHECK(contains(r.error, filehash::sha256_hex(changed)));
    CHECK(contains(r.error, filehash::sha256_hex(report)));
}

TEST_CASE("timeline::load_snapshot: an unknown schema_version refused, and what can't be read, isn't a "
          "document or a report, or doesn't fit",
          "[timeline][timeline_load]")
{
    const std::string report = vmafreport::ladder_to_json(ad_ladder(), kRun);
    const Written w = write_pair("load-refusals", report);

    // schema_version 2, which this reader doesn't read (Q05 (a)).
    std::string v2 = w.doc_text;
    const std::size_t key = v2.find("\"schema_version\"");
    REQUIRE(key != std::string::npos);
    const std::size_t one = v2.find('1', key);
    REQUIRE(one != std::string::npos);
    v2[one] = '2';
    const std::string v2_path = testtemp::path("load-v2.json").string();
    write(v2_path, v2);
    LoadResult r = timeline::load_snapshot({ .document = v2_path });
    CHECK_FALSE(r.snapshot);
    CHECK(contains(r.error, "isn't a timeline document this reads"));
    CHECK(contains(r.error, "schema_version is 2"));

    r = timeline::load_snapshot({ .document = testtemp::path("nowhere.json").string() });
    CHECK(contains(r.error, "cannot read the timeline document"));
    r = timeline::load_snapshot({ .document = testtemp::dir().string() });
    CHECK(contains(r.error, "it is a directory"));
    r = timeline::load_snapshot({ .document = w.report_path });   // a report, not a document
    CHECK(contains(r.error, "isn't a timeline document this reads"));
    r = timeline::load_snapshot({ .document = w.doc_path, .report = testtemp::path("none.json").string() });
    CHECK(contains(r.error, "cannot read the ladder report"));
    r = timeline::load_snapshot({ .document = w.doc_path, .report = w.doc_path });   // a document, not a report
    CHECK(contains(r.error, "isn't a ladder report the timeline reads"));

    // A report whose hashes the document names but whose complete pairs
    // aren't the document's: v180's pair failed in it.
    const std::string one_pair = vmafreport::ladder_to_json(ad_ladder(false, true), kRun);
    const Written f = write_pair("load-unfit", report, false, one_pair);
    write(f.report_path, one_pair);
    r = timeline::load_snapshot({ .document = f.doc_path });
    CHECK_FALSE(r.snapshot);
    CHECK(contains(r.error, "don't fit"));
}

TEST_CASE("timeline::load_snapshot: a load stopped through its stop token gives no snapshot",
          "[timeline][timeline_load]")
{
    const Written w = write_pair("load-stopped", vmafreport::ladder_to_json(ad_ladder(), kRun));
    std::stop_source stop;
    stop.request_stop();
    const LoadResult r = timeline::load_snapshot({ .document = w.doc_path }, stop.get_token());
    CHECK_FALSE(r.snapshot);
    CHECK(r.stopped);
    CHECK(r.error == "the load was stopped");
    std::string text, err;
    CHECK_FALSE(timeline::read_file(w.doc_path, stop.get_token(), text, err));
    CHECK(text.empty());
}

// ── The loader ───────────────────────────────────────────────────────────────

TEST_CASE("timeline::Loader: the snapshot handed over once, and a refusal too", "[timeline][timeline_load]")
{
    const Written w = write_pair("loader-once", vmafreport::ladder_to_json(ad_ladder(), kRun));
    timeline::Loader loader;
    CHECK_FALSE(loader.take());   // before any start()
    loader.start({ .document = w.doc_path });
    const std::optional<LoadResult> r = wait_for(loader);
    REQUIRE(r);
    INFO(r->error);
    REQUIRE(r->snapshot);
    CHECK(r->snapshot->document_path == w.doc_path);
    CHECK_FALSE(loader.take());   // once
    CHECK_FALSE(loader.loading());

    loader.start({ .document = testtemp::path("loader-none.json").string() });
    const std::optional<LoadResult> refused = wait_for(loader);
    REQUIRE(refused);
    CHECK_FALSE(refused->snapshot);
    CHECK(contains(refused->error, "cannot read the timeline document"));
    CHECK_FALSE(loader.take());
}

TEST_CASE("timeline::Loader: a second load stops the first, whose result is never handed over; a "
          "result not taken is dropped by the next load",
          "[timeline][timeline_load]")
{
    const Written w = write_pair("loader-second", vmafreport::ladder_to_json(ad_ladder(), kRun));
    BlockingRead first;   // before the loader, which joins its workers when it goes
    timeline::Loader loader;
    loader.start({ .document = testtemp::path("loader-first.json").string(), .read = first.reader() });
    first.wait_entered();
    CHECK(loader.loading());
    loader.start({ .document = w.doc_path });
    first.wait_stopped();   // its stop token, requested by the second start()
    const std::optional<LoadResult> r = wait_for(loader);
    REQUIRE(r);
    INFO(r->error);
    REQUIRE(r->snapshot);
    CHECK(r->snapshot->document_path == w.doc_path);
    CHECK_FALSE(loader.take());

    // A result left untaken: the next start() drops it.
    loader.start({ .document = w.doc_path });
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (loader.loading() && std::chrono::steady_clock::now() < until)
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    REQUIRE_FALSE(loader.loading());
    loader.start({ .document = testtemp::path("loader-none.json").string() });
    const std::optional<LoadResult> next = wait_for(loader);
    REQUIRE(next);
    CHECK_FALSE(next->snapshot);
    CHECK(contains(next->error, "cannot read the timeline document"));
}

TEST_CASE("timeline::Loader: cancel stops the load and hands nothing over", "[timeline][timeline_load]")
{
    BlockingRead read;   // before the loader, which joins its workers when it goes
    timeline::Loader loader;
    loader.start({ .document = testtemp::path("loader-cancel.json").string(), .read = read.reader() });
    read.wait_entered();
    CHECK(loader.loading());
    loader.cancel();
    CHECK_FALSE(loader.loading());
    read.wait_stopped();
    CHECK_FALSE(loader.take());
    // Its worker ends, and still hands nothing over.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    CHECK_FALSE(loader.take());
}
