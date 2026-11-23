// timeline::read_ladder_report and timeline::scores_text
// (src/LadderReportRead.h): a report that vmafreport::ladder_to_json writes
// from results built here, read back field by field with its PTS in ticks;
// the refusals of DECISIONS.md D-081 (b), each with its reason; values the
// timeline can't use; and the scores digest (D-072), unchanged when a report
// is redacted as eval/ladder/pool_check.py and eval/consistency/ladder_check.py
// redact one, changed by anything else the timeline reads from it.

#include <catch2/catch_test_macros.hpp>

#include "FileHash.h"
#include "JsonRead.h"
#include "JsonText.h"
#include "LadderReportRead.h"
#include "TestTemp.h"
#include "VMAFReport.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <optional>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

constexpr AVRational kTs  = { 1, 90000 };   // MPEG-TS's time base
constexpr AVRational kMkv = { 1, 1000 };    // Matroska's

bool same(AVRational a, AVRational b) { return a.num == b.num && a.den == b.den; }

// The seconds the analyzer writes for a PTS of `ticks` in `tb`
// (src/VMAFAnalyzer.cpp, DecodeCtx::seconds).
double seconds(std::int64_t ticks, AVRational tb)
{
    return static_cast<double>(ticks) * tb.num / tb.den;
}

// ── Results as the analyzer and the ladder leave them ─────────────────────────

VMAFInputInfo input(AVRational tb)
{
    VMAFInputInfo in;
    in.width           = 1920;
    in.height          = 1080;
    in.pix_fmt         = "yuv420p";
    in.color_space     = "bt709";
    in.color_range     = "tv";
    in.color_primaries = "bt709";
    in.color_trc       = "bt709";
    in.time_base_num   = tb.num;
    in.time_base_den   = tb.den;
    in.ended           = true;
    return in;
}

// A scored pair: the positions of its two frames, their PTS in ticks, its score.
struct Scored {
    std::int64_t ref_index, dis_index, ref_ticks, dis_ticks;
    double       vmaf;
};

VMAFResult result(AVRational ref_tb, AVRational dis_tb, const std::vector<Scored>& frames)
{
    VMAFResult r;
    r.done     = true;
    r.settings = { .model = "vmaf_v0.6.1", .model_source = "built-in", .scaler = "bicubic",
                   .pix_fmt = "yuv420p", .bit_depth = 8, .n_threads = 4, .n_subsample = 1 };
    r.ref_info = input(ref_tb);
    r.dis_info = input(dis_tb);
    for (const Scored& f : frames) {
        r.ref_index.push_back(f.ref_index);
        r.dis_index.push_back(f.dis_index);
        r.ref_pts.push_back(seconds(f.ref_ticks, ref_tb));
        r.dis_pts.push_back(seconds(f.dis_ticks, dis_tb));
        r.per_frame.push_back(f.vmaf);
    }
    r.alignment.grid           = "reference";
    r.alignment.tolerance      = 0.02;
    r.alignment.ref_frame_rate = "25/1";
    r.alignment.dis_frame_rate = "25/1";
    r.alignment.paired         = static_cast<std::int64_t>(frames.size());
    r.alignment.contexts       = { { 0, static_cast<unsigned>(frames.size()) } };
    return r;
}

// The low rung against the top one at 25 fps, both in MPEG-TS ticks. The
// reference has 8 frames in two discontinuity sequences, the second
// restarting its PTS at the first one's origin, as D-069's ad does; the
// distorted input lacks the frame of reference frame 3, so that one is
// unmatched, and its last frame has no timestamp. A score of null (NaN)
// and one of 100, which the report writes as the integer literal 100.
VMAFResult low_pair()
{
    const std::int64_t o = 900000, d = 3600;   // the origin, a frame at 25 fps
    VMAFResult r = result(kTs, kTs, {
        { 0, 0, o, o, 0.1 + 0.2 }, { 1, 1, o + d, o + d, 100.0 },
        { 2, 2, o + 2 * d, o + 2 * d, 1.0 / 3.0 }, { 4, 3, o + 4 * d, o + 4 * d, 97.5 },
        { 5, 4, o, o, 41.25 }, { 6, 5, o + d, o + d, kNaN }, { 7, 6, o + 2 * d, o + 2 * d, 12.0 },
    });
    r.alignment.contexts      = { { 0, 3 }, { 3, 1 }, { 4, 3 } };
    r.alignment.ref_sequences = { { .first_index = 0, .first_pts = seconds(o, kTs), .frames = 5 },
                                  { .first_index = 5, .first_pts = seconds(o, kTs), .frames = 3 } };
    r.alignment.dis_sequences = { { .first_index = 0, .first_pts = seconds(o, kTs), .frames = 4 },
                                  { .first_index = 4, .first_pts = seconds(o, kTs), .frames = 4 } };
    r.alignment.failures = {
        { .input = "reference", .index = 3, .pts = seconds(o + 3 * d, kTs),
          .reason = "unmatched_reference" },
        { .input = "distorted", .index = 7, .pts = kNaN, .reason = "no_timestamp" },
    };
    r.alignment.failed = 2;
    return r;
}

// The middle rung against the top one: its frames in Matroska's
// milliseconds from 10 s, and its frame rate unknown.
VMAFResult mid_pair()
{
    VMAFResult r = result(kTs, kMkv, {
        { 0, 0, 900000, 10000, 70.25 }, { 1, 1, 903600, 10040, 71.5 },
        { 2, 2, 907200, 10080, 72.0 },  { 3, 3, 910800, 10120, 1e-7 },
    });
    r.alignment.dis_frame_rate = "0/1";
    r.alignment.ref_sequences  = { { .first_index = 0, .first_pts = 10.0, .frames = 4 } };
    r.alignment.dis_sequences  = { { .first_index = 0, .first_pts = 10.0, .frames = 4 } };
    return r;
}

// A ladder run of two pairs in top-rung mode, as vmafladder::score() leaves
// it, under the paths `dir` (where the master is) and `uri_dir` (where the
// middle rung's URI points, absolute when not empty).
vmafladder::Result sample_ladder(const std::string& dir = "https://cdn.example.com/hls",
                                 const std::string& uri_dir = "")
{
    vmafladder::Result l;
    l.ladder           = dir + "/master.m3u8";
    l.fetched.location = dir + "/master.m3u8";
    l.fetched.sha256   = "2c26b46b68ffc68ff99b453c1d30413413422d706483bfa0f98a5e886266e7ae";
    const std::string mid_uri = uri_dir.empty() ? "mid/p.m3u8" : uri_dir + "/mid/p.m3u8";
    l.master.entries = {
        { .index = 0, .line = 3, .uri = "low/p.m3u8", .url = dir + "/low/p.m3u8",
          .bandwidth = 300000, .resolution = "640x360", .codecs = "avc1.4d401e,mp4a.40.2",
          .video = true },
        { .index = 1, .line = 5, .uri = "top/p.m3u8", .url = dir + "/top/p.m3u8",
          .bandwidth = 900000, .average_bandwidth = "850000", .resolution = "1920x1080",
          .frame_rate = "29.970", .video = true },
        { .index = 2, .line = 7, .uri = "a/p.m3u8", .url = dir + "/a/p.m3u8", .bandwidth = 64000,
          .codecs = "mp4a.40.2", .video = false },
        { .index = 3, .line = 9, .uri = mid_uri, .url = dir + "/mid/p.m3u8", .bandwidth = 600000,
          .resolution = "960x540", .video = true },
        { .index = 4, .line = 11, .uri = "low/p.m3u8", .url = dir + "/low/p.m3u8",
          .bandwidth = 310000, .resolution = "640x360", .video = true },
    };
    l.plan.reference_mode    = "top-rung";
    l.plan.reference_url     = dir + "/top/p.m3u8";
    l.plan.reference_entries = { 1 };
    l.plan.pairs   = { { dir + "/low/p.m3u8", { 0, 4 } }, { dir + "/mid/p.m3u8", { 3 } } };
    l.plan.skipped = { { 2, "no video" } };
    VMAFResult low = low_pair(), mid = mid_pair();
    low.ref_url = mid.ref_url = l.plan.reference_url;
    low.dis_url = l.plan.pairs[0].url;
    mid.dis_url = l.plan.pairs[1].url;
    l.pairs = { { .status = vmafladder::PairStatus::Complete, .result = low, .started = 0.25,
                  .ended = 1.5 },
                { .status = vmafladder::PairStatus::Complete, .result = mid, .started = 0.5,
                  .ended = 2.0 } };
    l.status        = "complete";
    l.jobs          = 2;
    l.max_running   = 2;
    l.peak_resident = 123456789;
    return l;
}

const vmafreport::RunInfo kRun{
    .tool = "mirage_vmaf", .reference_mode = "explicit-pair", .libvmaf_version = "3.0.0",
    .libvmaf_package_version = "3.1.0",
    .ffmpeg_libraries = { { "libavutil", "60.26.101" }, { "libavcodec", "62.28.101" },
                          { "libavformat", "62.12.101" }, { "libswscale", "9.5.101" },
                          { "libavfilter", "11.14.101" } },
    .frame_limit = 300,
};

std::string sample_report() { return vmafreport::ladder_to_json(sample_ladder(), kRun); }

timeline::LadderScores read_ok(const std::string& text)
{
    std::string err = "not cleared";
    timeline::LadderScores s = timeline::read_ladder_report(text, err);
    INFO(err);
    REQUIRE(err.empty());
    return s;
}

std::string refusal(const std::string& text)
{
    std::string err;
    const timeline::LadderScores s = timeline::read_ladder_report(text, err);
    REQUIRE_FALSE(err.empty());
    CHECK(s.pairs.empty());   // nothing is returned with a refusal
    CHECK(s.status.empty());
    return err;
}

// ── A report's tree, edited ──────────────────────────────────────────────────

json::Value tree(const std::string& text)
{
    json::Error err;
    std::optional<json::Value> v = json::parse(text, err);
    INFO(json::error_text(err));
    REQUIRE(v);
    return std::move(*v);
}

// `v` as JSON text, with json::quote and json::number.
std::string text_of(const json::Value& v)
{
    struct Writer {
        std::string operator()(std::monostate) const { return "null"; }
        std::string operator()(bool b) const { return b ? "true" : "false"; }
        std::string operator()(std::int64_t n) const { return std::to_string(n); }
        std::string operator()(double d) const { return json::number(d); }
        std::string operator()(const std::string& s) const { return json::quote(s); }
        std::string operator()(const json::Value::Array& a) const
        {
            std::string out = "[";
            for (std::size_t k = 0; k < a.size(); ++k) out += (k ? ", " : "") + text_of(a[k]);
            return out + "]";
        }
        std::string operator()(const json::Value::Object& o) const
        {
            std::string out = "{";
            for (std::size_t k = 0; k < o.size(); ++k)
                out += (k ? ", " : "") + json::quote(o[k].first) + ": " + text_of(o[k].second);
            return out + "}";
        }
    };
    return std::visit(Writer{}, v.v);
}

// The value at `path` ("pairs[0].frames[3].vmaf"; "" for `v` itself).
json::Value& walk(json::Value& v, std::string_view path)
{
    json::Value* cur = &v;
    std::size_t  i   = 0;
    while (i < path.size()) {
        if (path[i] == '.') ++i;
        if (path[i] == '[') {
            const std::size_t close = path.find(']', i);
            const std::size_t k     = std::stoul(std::string(path.substr(i + 1, close - i - 1)));
            auto& a = std::get<json::Value::Array>(cur->v);
            REQUIRE(k < a.size());
            cur = &a[k];
            i   = close + 1;
        } else {
            const std::size_t end = std::min(path.find_first_of(".[", i), path.size());
            const std::string_view key = path.substr(i, end - i);
            auto& o  = std::get<json::Value::Object>(cur->v);
            auto  it = std::find_if(o.begin(), o.end(), [&](const auto& m) { return m.first == key; });
            INFO(path);
            REQUIRE(it != o.end());
            cur = &it->second;
            i   = end;
        }
    }
    return *cur;
}

void erase(json::Value& v, std::string_view object, std::string_view key)
{
    auto& o = std::get<json::Value::Object>(walk(v, object).v);
    std::erase_if(o, [&](const auto& m) { return m.first == key; });
}

// The sample report with the value at `path` replaced by `value`, a JSON text.
std::string with(std::string_view path, const std::string& value)
{
    json::Value v = tree(sample_report());
    walk(v, path) = tree(value);
    return text_of(v);
}

// `text` with each of `subs` (a path, its placeholder) replaced wherever it
// occurs, the longest path first: how both eval scripts redact a report.
std::string redact(std::string text, std::vector<std::pair<std::string, std::string>> subs)
{
    std::sort(subs.begin(), subs.end(), [](const auto& a, const auto& b) {
        return a.first.size() != b.first.size() ? a.first.size() > b.first.size() : a.first < b.first;
    });
    for (const auto& [path, placeholder] : subs) {
        for (std::size_t at = text.find(path); at != std::string::npos;
             at = text.find(path, at + placeholder.size()))
            text.replace(at, path.size(), placeholder);
    }
    return text;
}

} // namespace

// ── Reading ───────────────────────────────────────────────────────────────────

TEST_CASE("timeline::read_ladder_report: a report of ladder_to_json, read back field by field",
          "[timeline][ladder_report_read]")
{
    const vmafladder::Result l = sample_ladder();
    const timeline::LadderScores s = read_ok(vmafreport::ladder_to_json(l, kRun));

    CHECK(s.status == "complete");
    CHECK(s.reference_mode == "top-rung");
    CHECK(s.model == "vmaf_v0.6.1");
    CHECK(s.model_source == "built-in");
    CHECK(s.model_sha256.empty());   // null for a built-in model
    CHECK(s.scaler == "bicubic");
    CHECK(s.n_threads == 4);
    CHECK(s.n_subsample == 1);
    CHECK(s.frame_limit == 300);
    CHECK(s.libvmaf_version == "3.0.0");
    CHECK(s.libvmaf_package_version == "3.1.0");
    CHECK(s.master_url == l.ladder);
    CHECK(s.master_location == l.fetched.location);
    CHECK(s.master_sha256 == l.fetched.sha256);

    REQUIRE(s.entries.size() == 5);
    for (std::size_t k = 0; k < 5; ++k) {
        INFO(k);
        const vmafladder::Entry&     want = l.master.entries[k];
        const timeline::MasterEntry& e    = s.entries[k];
        CHECK(e.index == want.index);
        CHECK(e.line == want.line);
        CHECK(e.uri == want.uri);
        CHECK(e.url == want.url);
        CHECK(e.bandwidth == want.bandwidth);
        CHECK(e.average_bandwidth == want.average_bandwidth);
        CHECK(e.resolution == want.resolution);
        CHECK(e.codecs == want.codecs);
        CHECK(e.frame_rate == want.frame_rate);
        CHECK(e.video == want.video);
    }
    CHECK(s.entries[1].average_bandwidth == "850000");
    CHECK_FALSE(s.entries[0].frame_rate);   // absent: null
    CHECK(s.reference_url == l.plan.reference_url);
    CHECK(s.reference_entries == std::vector<std::size_t>{ 1 });

    REQUIRE(s.pairs.size() == 2);
    const timeline::PairScores& low = s.pairs[0];
    CHECK(low.index == 0);
    CHECK(low.entries == std::vector<std::size_t>{ 0, 4 });
    CHECK(low.ref_url == l.plan.reference_url);
    CHECK(low.dis_url == l.plan.pairs[0].url);
    CHECK(same(low.ref_time_base, kTs));
    CHECK(same(low.dis_time_base, kTs));
    CHECK(same(low.ref_rate, { 25, 1 }));
    CHECK(same(low.dis_rate, { 25, 1 }));
    CHECK(low.ref_index == std::vector<std::int64_t>{ 0, 1, 2, 4, 5, 6, 7 });
    CHECK(low.dis_index == std::vector<std::int64_t>{ 0, 1, 2, 3, 4, 5, 6 });
    // The PTS in ticks, those of the second sequence repeating the first's.
    CHECK(low.ref_pts ==
          std::vector<std::int64_t>{ 900000, 903600, 907200, 914400, 900000, 903600, 907200 });
    CHECK(low.dis_pts == low.ref_pts);
    REQUIRE(low.vmaf.size() == 7);
    CHECK(low.vmaf[0] == 0.1 + 0.2);   // full precision
    CHECK(low.vmaf[1] == 100.0);       // written as the integer literal 100
    CHECK(low.vmaf[2] == 1.0 / 3.0);
    CHECK(low.vmaf[4] == 41.25);
    CHECK(std::isnan(low.vmaf[5]));    // written as null
    CHECK(low.context_first == std::vector<std::int64_t>{ 0, 3, 4 });
    REQUIRE(low.failures.size() == 2);
    CHECK(low.failures[0].input == "reference");
    CHECK(low.failures[0].index == 3);
    CHECK(low.failures[0].pts == 910800);
    CHECK(low.failures[0].reason == "unmatched_reference");
    CHECK(low.failures[1].input == "distorted");
    CHECK(low.failures[1].index == 7);
    CHECK_FALSE(low.failures[1].pts);   // a frame without a timestamp
    CHECK(low.failures[1].reason == "no_timestamp");
    REQUIRE(low.ref_sequences.size() == 2);
    CHECK(low.ref_sequences[0].first_index == 0);
    CHECK(low.ref_sequences[0].first_pts == 900000);
    CHECK(low.ref_sequences[0].frames == 5);
    CHECK(low.ref_sequences[1].first_index == 5);
    CHECK(low.ref_sequences[1].first_pts == 900000);
    CHECK(low.ref_sequences[1].frames == 3);
    REQUIRE(low.dis_sequences.size() == 2);
    CHECK(low.dis_sequences[1].first_index == 4);
    CHECK(low.dis_sequences[1].frames == 4);

    const timeline::PairScores& mid = s.pairs[1];
    CHECK(mid.index == 1);
    CHECK(mid.entries == std::vector<std::size_t>{ 3 });
    CHECK(same(mid.dis_time_base, kMkv));
    CHECK(same(mid.dis_rate, { 0, 1 }));   // unknown
    CHECK(mid.ref_pts == std::vector<std::int64_t>{ 900000, 903600, 907200, 910800 });
    CHECK(mid.dis_pts == std::vector<std::int64_t>{ 10000, 10040, 10080, 10120 });
    CHECK(mid.vmaf == std::vector<double>{ 70.25, 71.5, 72.0, 1e-7 });
    CHECK(mid.context_first == std::vector<std::int64_t>{ 0 });
    CHECK(mid.failures.empty());
    REQUIRE(mid.dis_sequences.size() == 1);
    CHECK(mid.dis_sequences[0].first_pts == 10000);
}

TEST_CASE("timeline::read_ladder_report: mezzanine mode, a cancelled run, a model file, no frame limit",
          "[timeline][ladder_report_read]")
{
    vmafladder::Result l = sample_ladder();
    l.plan.reference_mode    = "mezzanine";
    l.plan.reference_url     = "media/source \"cut\".mov";
    l.plan.reference_entries = {};
    l.plan.pairs.insert(l.plan.pairs.begin() + 1, { l.master.entries[1].url, { 1 } });
    l.pairs.insert(l.pairs.begin() + 1, { .status = vmafladder::PairStatus::Stopped });
    l.status = "cancelled";
    l.error  = "the run was cancelled: 2 of 3 pairs complete";
    for (vmafladder::PairRun& p : l.pairs) {
        p.result.ref_url  = l.plan.reference_url;
        p.result.settings = { .model = "models/vmaf_v0.6.1neg.json", .model_source = "file",
                              .model_sha256 = std::string(64, 'a'), .scaler = "lanczos",
                              .pix_fmt = "yuv420p", .bit_depth = 8, .n_threads = 2,
                              .n_subsample = 3 };
    }
    vmafreport::RunInfo run = kRun;
    run.frame_limit   = 0;
    run.config_path   = "ladder.cfg";
    run.config_sha256 = std::string(64, 'b');

    const timeline::LadderScores s = read_ok(vmafreport::ladder_to_json(l, run));
    CHECK(s.status == "cancelled");
    CHECK(s.reference_mode == "mezzanine");
    CHECK(s.reference_url == "media/source \"cut\".mov");   // as given, a relative path kept
    CHECK(s.reference_entries.empty());
    CHECK(s.model == "models/vmaf_v0.6.1neg.json");
    CHECK(s.model_source == "file");
    CHECK(s.model_sha256 == std::string(64, 'a'));
    CHECK(s.scaler == "lanczos");
    CHECK(s.n_threads == 2);
    CHECK(s.n_subsample == 3);
    CHECK_FALSE(s.frame_limit);   // null: no limit
    // The complete pairs only, with their indices in the plan.
    REQUIRE(s.pairs.size() == 2);
    CHECK(s.pairs[0].index == 0);
    CHECK(s.pairs[1].index == 2);
    CHECK(s.pairs[1].entries == std::vector<std::size_t>{ 3 });
    CHECK(s.pairs[0].ref_url == "media/source \"cut\".mov");

    // A run that scored no pair has none to read.
    for (vmafladder::PairRun& p : l.pairs) p.status = vmafladder::PairStatus::Failed;
    l.status = "failed";
    const timeline::LadderScores none = read_ok(vmafreport::ladder_to_json(l, run));
    CHECK(none.status == "failed");
    CHECK(none.pairs.empty());
    CHECK(none.entries.size() == 5);
}

TEST_CASE("timeline::read_ladder_report: PTS back in ticks, exactly, in the time bases inputs have",
          "[timeline][ladder_report_read]")
{
    // pts_ticks: the nearest tick, halves away from zero.
    CHECK(timeline::pts_ticks(10.0, kTs) == 900000);
    CHECK(timeline::pts_ticks(0.25, { 1, 2 }) == 1);    // 0.5 ticks
    CHECK(timeline::pts_ticks(-0.25, { 1, 2 }) == -1);
    CHECK(timeline::pts_ticks(0.75, { 1, 2 }) == 2);    // 1.5 ticks
    CHECK(timeline::pts_ticks(0.2, { 1, 2 }) == 0);
    CHECK(timeline::pts_ticks(0.12000000000000001, kTs) == 10800);
    CHECK(timeline::pts_ticks(1.0, { 1001, 30000 }) == 30);   // 29.97 ticks
    for (const double bad : { kNaN, std::numeric_limits<double>::infinity(), 1e300, -1e300 })
        CHECK_FALSE(timeline::pts_ticks(bad, kTs));
    for (const AVRational bad : { AVRational{ 0, 1 }, AVRational{ 1, 0 }, AVRational{ -1, 90000 },
                                  AVRational{ 1, -90000 } })
        CHECK_FALSE(timeline::pts_ticks(1.0, bad));

    // The analyzer's seconds come back as its ticks under 2^49 ticks, in
    // MPEG-TS's and Matroska's time bases, in 1/15360 and 1001/30000, and in
    // whole seconds: the PTS a 33-bit MPEG-TS clock wraps at, negative ones,
    // and random ones.
    std::mt19937_64 rng(7);
    std::uniform_int_distribution<std::int64_t> any(-(std::int64_t{ 1 } << 49) + 1,
                                                    (std::int64_t{ 1 } << 49) - 1);
    for (const AVRational tb : { kTs, kMkv, AVRational{ 1, 15360 }, AVRational{ 1001, 30000 },
                                 AVRational{ 1, 1 } }) {
        INFO(std::format("{}/{}", tb.num, tb.den));
        std::vector<std::int64_t> ticks = { 0, 1, -1, -1800, (std::int64_t{ 1 } << 33) - 1,
                                            std::int64_t{ 1 } << 33, (std::int64_t{ 1 } << 33) + 3600,
                                            (std::int64_t{ 1 } << 49) - 1,
                                            -(std::int64_t{ 1 } << 49) + 1 };
        for (int k = 0; k < 20000; ++k) ticks.push_back(any(rng));
        for (const std::int64_t t : ticks) REQUIRE(timeline::pts_ticks(seconds(t, tb), tb) == t);
    }

    // And through a report: a pair in 1001/30000 ticks from beyond the
    // 33-bit wrap, with a negative PTS before it.
    const AVRational ntsc = { 1001, 30000 };
    const std::int64_t wrap = std::int64_t{ 1 } << 33;
    vmafladder::Result l = sample_ladder();
    l.pairs[1].result = result(ntsc, ntsc, { { 0, 0, -2, -2, 50.0 }, { 1, 1, wrap - 1, wrap - 1, 51.0 },
                                             { 2, 2, wrap + 7, wrap + 9, 52.0 } });
    l.pairs[1].result.alignment.ref_sequences = { { .first_index = 0,
                                                    .first_pts = seconds(-2, ntsc), .frames = 3 } };
    l.pairs[1].result.alignment.dis_sequences = l.pairs[1].result.alignment.ref_sequences;
    const timeline::LadderScores s = read_ok(vmafreport::ladder_to_json(l, kRun));
    const timeline::PairScores& p = s.pairs[1];
    CHECK(same(p.ref_time_base, ntsc));
    CHECK(p.ref_pts == std::vector<std::int64_t>{ -2, wrap - 1, wrap + 7 });
    CHECK(p.dis_pts == std::vector<std::int64_t>{ -2, wrap - 1, wrap + 9 });
    CHECK(p.ref_sequences[0].first_pts == -2);
}

// ── Refusals (D-081 (b)) ──────────────────────────────────────────────────────

TEST_CASE("timeline::read_ladder_report: older layouts are refused, naming what they lack",
          "[timeline][ladder_report_read]")
{
    const std::string kLayouts =
        ": the timeline reads ladder reports in 75cf6e0's layout or a later one "
        "(DECISIONS.md D-081 (b))";
    const json::Value report = tree(sample_report());

    // Before D-057: no discontinuity sequences, which the media key needs.
    json::Value v = report;
    erase(v, "pairs[0].alignment.reference", "sequences");
    CHECK(refusal(text_of(v)) == "no pairs[0].alignment.reference.sequences" + kLayouts);

    // Between D-057 and D-058: pool.pair_memory a number, and no pool.baseline.
    v = report;
    erase(v, "pool", "baseline");
    walk(v, "pool.pair_memory") = tree("1048576");
    CHECK(refusal(text_of(v)) == "no pool.baseline" + kLayouts);

    // The committed records' older ladder layouts (eval/phase3-survey/schema.md
    // §1.8): with status, pool and not_scored but no sequences and no
    // pool.baseline; and without status, pool, not_scored or sequences.
    v = report;
    erase(v, "pool", "baseline");
    for (const char* in : { "reference", "distorted" })
        for (std::size_t k = 0; k < 2; ++k) erase(v, std::format("pairs[{}].alignment.{}", k, in), "sequences");
    CHECK(refusal(text_of(v)) == "no pool.baseline" + kLayouts);
    for (const char* key : { "status", "error", "pool", "not_scored" }) erase(v, "", key);
    CHECK(refusal(text_of(v)) == "no status" + kLayouts);

    // Every key of every object the reader opens, each taken out in turn.
    for (const char* object : { "", "pool", "ladder", "ladder.entries[3]", "reference", "pairs[1]",
                                "pairs[0].ref", "pairs[0].dis", "pairs[0].alignment",
                                "pairs[0].alignment.reference", "pairs[1].alignment.distorted",
                                "pairs[0].alignment.reference.sequences[1]",
                                "pairs[0].alignment.vmaf_contexts[2]",
                                "pairs[0].alignment.failures[1]", "pairs[1].frames[3]" }) {
        json::Value base = report;
        for (const auto& [key, value] : std::get<json::Value::Object>(walk(base, object).v)) {
            json::Value cut = report;
            erase(cut, object, key);
            const std::string path = *object ? std::format("{}.{}", object, key) : key;
            INFO(path);
            CHECK(refusal(text_of(cut)) == "no " + path + kLayouts);
        }
    }

    // A later layout's keys, at any level, are passed over.
    v = report;
    for (const char* object : { "", "pool", "ladder", "ladder.entries[0]", "pairs[0]",
                                "pairs[0].alignment", "pairs[0].alignment.reference",
                                "pairs[0].alignment.failures[0]", "pairs[1].frames[2]" })
        std::get<json::Value::Object>(walk(v, object).v).emplace_back("added_later", tree("[{}]"));
    CHECK(timeline::scores_text(read_ok(text_of(v))) == timeline::scores_text(read_ok(sample_report())));
}

TEST_CASE("timeline::read_ladder_report: a pair's report, the player's report and other text are refused",
          "[timeline][ladder_report_read]")
{
    CHECK(refusal(vmafreport::to_json(low_pair(), kRun)) ==
          "a pair's report (mirage_vmaf --ref/--dis), not a ladder report: only ladder reports "
          "are read (DECISIONS.md D-081 (b))");
    CHECK(refusal(R"({"results": [{"label": "720p", "per_frame": [1, 2]}]})") ==
          "the player's VMAF report, not a ladder report of mirage_vmaf");
    CHECK(refusal("[]") == "not a ladder report: the text holds an array, not an object");
    CHECK(refusal("\"report\"") == "not a ladder report: the text holds a string, not an object");
    CHECK(refusal("{}").starts_with("no status: "));
    CHECK(refusal("") == "not JSON: line 1, column 1: no JSON value: the text is empty or white space");
    CHECK(refusal("{\"status\": \"complete\",\n \"status\": 1}") ==
          "not JSON: line 2, column 2: the key \"status\" again in one object (first at line 1, "
          "column 2)");
    const std::string report = sample_report();
    CHECK(refusal(report.substr(0, report.size() / 2)).starts_with("not JSON: line "));
}

TEST_CASE("timeline::read_ladder_report: values the timeline can't use are refused, each named",
          "[timeline][ladder_report_read]")
{
    constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
    const std::vector<std::tuple<const char*, std::string, std::string>> cases = {
        { "pairs[0].frames[2].index", "5", "pairs[0].frames[2].index is 5, not its position in frames, 2" },
        { "pairs[0].frames[3].ref_index", "2",
          "pairs[0].frames[3].ref_index is 2, not after the frame before it (2), as the aligner "
          "pairs frames (src/VMAFAlign.h)" },
        { "pairs[0].frames[1].dis_index", "0",
          "pairs[0].frames[1].dis_index is 0, not after the frame before it (0), as the aligner "
          "pairs frames (src/VMAFAlign.h)" },
        { "pairs[0].frames[0].ref_index", "-1", "pairs[0].frames[0].ref_index is -1, not a count from 0" },
        // A position at or beyond its input's frames read, the frames of its
        // sequences in all: at their number, at INT64_MAX, the first of two
        // positions at or beyond a number made smaller, and with no sequence.
        { "pairs[0].frames[6].ref_index", "8",
          "pairs[0].frames[6].ref_index is 8, not a frame the analysis read of its input: "
          "pairs[0].alignment.reference.sequences have 8 frames in all" },
        { "pairs[0].frames[6].dis_index", std::to_string(kMax),
          std::format("pairs[0].frames[6].dis_index is {}, not a frame the analysis read of its "
                      "input: pairs[0].alignment.distorted.sequences have 8 frames in all",
                      kMax) },
        { "pairs[1].alignment.distorted.sequences[0].frames", "2",
          "pairs[1].frames[2].dis_index is 2, not a frame the analysis read of its input: "
          "pairs[1].alignment.distorted.sequences have 2 frames in all" },
        { "pairs[1].alignment.reference.sequences", "[]",
          "pairs[1].frames[0].ref_index is 0, not a frame the analysis read of its input: "
          "pairs[1].alignment.reference.sequences have 0 frames in all" },
        { "pairs[0].frames[4].dis_pts", "null",
          "pairs[0].frames[4].dis_pts is null, but a scored frame has a PTS, as only frames with a "
          "timestamp pair (src/VMAFAlign.h)" },
        { "pairs[0].frames[4].ref_pts", "1e300",
          "pairs[0].frames[4].ref_pts is 1e+300 s, which is no PTS in ticks of 1/90000" },
        { "pairs[0].frames[4].ref_pts", "\"10\"", "pairs[0].frames[4].ref_pts is a string, not a number" },
        { "pairs[0].frames[4].vmaf", "\"97\"", "pairs[0].frames[4].vmaf is a string, not a number or null" },
        { "pairs[0].frames[0]", "5", "pairs[0].frames[0] is an integer, not an object" },
        { "pairs[0].frames_scored", "6", "pairs[0].frames_scored is 6, but frames lists 7" },
        { "pairs[0].ref.time_base", "\"0/1\"",
          "pairs[0].ref.time_base is \"0/1\", not a time base, num/den with both above 0" },
        { "pairs[0].dis.time_base", "\"1/0\"",
          "pairs[0].dis.time_base is \"1/0\", not a time base, num/den with both above 0" },
        { "pairs[0].ref.time_base", "\"1/90000 \"",
          "pairs[0].ref.time_base is \"1/90000 \", not a time base, num/den with both above 0" },
        { "pairs[0].ref.time_base", "\"90000\"",
          "pairs[0].ref.time_base is \"90000\", not a time base, num/den with both above 0" },
        { "pairs[1].alignment.distorted.frame_rate", "\"fast\"",
          "pairs[1].alignment.distorted.frame_rate is \"fast\", not a frame rate, num/den" },
        { "pairs[1].alignment.reference.sequences[0].first_pts", "true",
          "pairs[1].alignment.reference.sequences[0].first_pts is a boolean, not a number or null" },
        { "pairs[1].entries", "[9]", "pairs[1].entries[0] is 9, not an entry of the master, which has 5" },
        { "reference.entries", "[1, 5]", "reference.entries[1] is 5, not an entry of the master, which has 5" },
        { "ladder.entries[3].index", "7",
          "ladder.entries[3].index is 7, not its position in the master's entries, 3" },
        { "ladder.entries[0].bandwidth", "1.5", "ladder.entries[0].bandwidth is a number, not an integer" },
        { "ladder.entries[0].video", "1", "ladder.entries[0].video is an integer, not true or false" },
        { "ladder.entries[0].codecs", "7", "ladder.entries[0].codecs is an integer, not a string or null" },
        { "pairs[0].alignment.vmaf_contexts[1].first_index", "7",
          "pairs[0].alignment.vmaf_contexts[1].first_index is 7, not a frame of the pair, which has 7" },
        { "pairs[0].alignment.vmaf_contexts[2].first_index", "3",
          "pairs[0].alignment.vmaf_contexts[2].first_index is 3, not after the context before it (3)" },
        { "pairs[1].index", "0",
          "pairs[1].index is 0, not after the pair before it (0): pairs are listed in the plan's order" },
        { "pairs[0].alignment.failures[0].input", "\"both\"",
          "pairs[0].alignment.failures[0].input is \"both\", not \"reference\" or \"distorted\"" },
        { "pairs[0].alignment.failures[1].pts", "\"none\"",
          "pairs[0].alignment.failures[1].pts is a string, not a number or null" },
        { "reference_mode", "\"explicit-pair\"",
          "reference_mode is \"explicit-pair\", not \"top-rung\" or \"mezzanine\", a ladder report's" },
        { "n_threads", "4294967296", "n_threads is 4294967296, beyond an int" },
        { "frame_limit", "\"300\"", "frame_limit is a string, not an integer" },
        { "model_sha256", "5", "model_sha256 is an integer, not a string or null" },
        { "status", "null", "status is null, not a string" },
        { "pairs", "{}", "pairs is an object, not an array" },
        { "ladder.entries", "null", "ladder.entries is null, not an array" },
        { "pool", "[]", "pool is an array, not an object" },
    };
    for (const auto& [path, value, why] : cases) {
        INFO(path << " = " << value);
        CHECK(refusal(with(path, value)) == why);
    }

    // The low pair's reference with 5 + `second` frames read and its last
    // position at `last`: with INT64_MAX frames read in all, a position of
    // INT64_MAX is refused and one below it is read; with more, a number
    // beyond 64 bits, a position of INT64_MAX is read.
    const auto near_max = [](std::int64_t second, std::int64_t last) {
        json::Value v = tree(sample_report());
        walk(v, "pairs[0].alignment.reference.sequences[1].frames") = tree(std::to_string(second));
        walk(v, "pairs[0].frames[6].ref_index") = tree(std::to_string(last));
        return text_of(v);
    };
    CHECK(refusal(near_max(kMax - 5, kMax)) ==
          std::format("pairs[0].frames[6].ref_index is {0}, not a frame the analysis read of its "
                      "input: pairs[0].alignment.reference.sequences have {0} frames in all",
                      kMax));
    CHECK(read_ok(near_max(kMax - 5, kMax - 1)).pairs[0].ref_index[6] == kMax - 1);
    CHECK(read_ok(near_max(kMax, kMax)).pairs[0].ref_index[6] == kMax);

    // An unknown frame rate is {0, 1}, however the report writes it.
    for (const char* unknown : { "\"0/0\"", "\"0/1\"", "\"-1/1\"" }) {
        const timeline::LadderScores s = read_ok(with("pairs[0].alignment.distorted.frame_rate", unknown));
        CHECK(same(s.pairs[0].dis_rate, { 0, 1 }));
    }
    // Nothing outside the layout's objects is read: a pair's other keys may hold anything.
    CHECK(read_ok(with("pairs[0].pooled", "null")).pairs.size() == 2);
    CHECK(read_ok(with("not_scored", "7")).pairs.size() == 2);
}

TEST_CASE("timeline::read_ladder_report: a ladder report nests 7 deep, within json::kMaxDepth",
          "[timeline][ladder_report_read]")
{
    const std::string report = sample_report();
    json::Error err;
    CHECK(json::parse(report, err, 7));
    CHECK_FALSE(json::parse(report, err, 6));
    CHECK(err.what == "arrays and objects nested more than 6 deep");
    CHECK(json::kMaxDepth >= 7u);
}

// ── The scores digest (D-072; design P-Z) ─────────────────────────────────────

TEST_CASE("timeline::scores_text: a report redacted as the eval scripts redact one has the same digest",
          "[timeline][ladder_report_read]")
{
    // Local paths as an eval run has them, under the tests' temporary
    // directory: only names, nothing is created.
    const std::string tmp        = testtemp::dir().string();
    const std::string home       = tmp + "/home";
    const std::string repo       = home + "/mirage";
    const std::string out        = tmp + "/out";
    const std::string ladder_dir = repo + "/eval/media/ladder";
    const std::string program    = repo + "/build/mirage_vmaf";

    // A failed run whose every field that can name a path names one: the
    // master, its entries (a URI too), the reference, the pairs, a pair the
    // run couldn't score and the run's error, the plan's and a pair's
    // warnings, the config file and a model file.
    vmafladder::Result l = sample_ladder(ladder_dir, ladder_dir);
    l.plan.pairs.push_back({ ladder_dir + "/extra/p.m3u8", { 4 } });
    l.pairs.push_back({ .status = vmafladder::PairStatus::Failed,
                        .why    = "cannot open distorted: " + ladder_dir + "/extra/p.m3u8" });
    l.plan.warnings = { "entries 1 and 4 name " + ladder_dir + "/top/p.m3u8" };
    l.status = "failed";
    l.error  = "pair 3 of 3, " + ladder_dir + "/extra/p.m3u8: cannot open distorted";
    for (std::size_t k = 0; k < 2; ++k) {
        l.pairs[k].result.settings.model        = repo + "/models/vmaf_v0.6.1.json";
        l.pairs[k].result.settings.model_source = "file";
        l.pairs[k].result.settings.model_sha256 = std::string(64, 'c');
        l.pairs[k].result.warnings = { "the reference " + l.plan.reference_url + " is yuv422p" };
    }
    vmafreport::RunInfo run = kRun;
    run.config_path   = repo + "/eval/ladder.cfg";
    run.config_sha256 = std::string(64, 'd');
    const std::string report = vmafreport::ladder_to_json(l, run);

    // eval/ladder/pool_check.py: the program, the master and its directory,
    // and the output directory, as given and as absolute paths, which here
    // are the same; eval/consistency/ladder_check.py: the output directory,
    // the repository, the home and the temporary directory, each also as
    // its real path.
    const auto real = [](const std::string& p) { return fs::weakly_canonical(p).string(); };
    const std::vector<std::pair<std::string, std::string>> pool_check = {
        { program, "<mirage_vmaf>" }, { ladder_dir + "/master.m3u8", "<ladder>" },
        { out, "<out_dir>" }, { ladder_dir, "<ladder_dir>" }, { real(ladder_dir), "<ladder_dir>" },
    };
    std::vector<std::pair<std::string, std::string>> ladder_check;
    for (const auto& [path, name] : { std::pair{ out, "<out_dir>" }, std::pair{ repo, "<repo>" },
                                      std::pair{ home, "<home>" }, std::pair{ tmp, "<tmp>" } }) {
        ladder_check.emplace_back(path, name);
        ladder_check.emplace_back(real(path), name);
    }

    const timeline::LadderScores original = read_ok(report);
    const std::string digest = timeline::scores_text(original);
    for (const auto& [script, subs] : { std::pair{ "pool_check", pool_check },
                                        std::pair{ "ladder_check", ladder_check } }) {
        INFO(script);
        const std::string redacted = redact(report, subs);
        REQUIRE(redacted != report);
        CHECK(filehash::sha256_hex(redacted) != filehash::sha256_hex(report));   // the bytes' hash fails
        const timeline::LadderScores r = read_ok(redacted);
        CHECK(timeline::scores_text(r) == digest);   // the digest doesn't
        CHECK(filehash::sha256_hex(timeline::scores_text(r)) == filehash::sha256_hex(digest));
        // The paths the reader keeps were replaced.
        CHECK(r.master_url != original.master_url);
        CHECK(r.entries[3].uri != original.entries[3].uri);
        CHECK(r.pairs[0].dis_url != original.pairs[0].dis_url);
    }
    const timeline::LadderScores by_ladder_check = read_ok(redact(report, ladder_check));
    CHECK(by_ladder_check.master_url == "<repo>/eval/media/ladder/master.m3u8");
    CHECK(by_ladder_check.model == "<repo>/models/vmaf_v0.6.1.json");
    CHECK(read_ok(redact(report, pool_check)).master_url == "<ladder>");
}

TEST_CASE("timeline::scores_text: what the timeline reads is in the digest, the paths are not",
          "[timeline][ladder_report_read]")
{
    const timeline::LadderScores base = read_ok(sample_report());
    const std::string digest = timeline::scores_text(base);
    CHECK(timeline::scores_text(base) == digest);   // the same twice

    using Change = std::pair<const char*, std::function<void(timeline::LadderScores&)>>;
    const std::vector<Change> in = {
        { "status", [](auto& s) { s.status = "cancelled"; } },
        { "reference_mode", [](auto& s) { s.reference_mode = "mezzanine"; } },
        { "a built-in model", [](auto& s) { s.model = "vmaf_4k_v0.6.1"; } },
        { "model_source", [](auto& s) { s.model_source = "file"; } },
        { "model_sha256", [](auto& s) { s.model_sha256 = std::string(64, 'e'); } },
        { "scaler", [](auto& s) { s.scaler = "lanczos"; } },
        { "n_threads", [](auto& s) { s.n_threads = 8; } },
        { "n_subsample", [](auto& s) { s.n_subsample = 2; } },
        { "frame_limit", [](auto& s) { s.frame_limit = 301; } },
        { "no frame_limit", [](auto& s) { s.frame_limit.reset(); } },
        { "libvmaf_version", [](auto& s) { s.libvmaf_version = "3.0.1"; } },
        { "libvmaf_package_version", [](auto& s) { s.libvmaf_package_version = "unknown"; } },
        { "master_sha256", [](auto& s) { s.master_sha256[0] = '3'; } },
        { "an entry's index", [](auto& s) { s.entries[2].index = 7; } },
        { "an entry's line", [](auto& s) { s.entries[2].line = 8; } },
        { "an entry's bandwidth", [](auto& s) { s.entries[2].bandwidth = 64001; } },
        { "an entry's average_bandwidth", [](auto& s) { s.entries[1].average_bandwidth.reset(); } },
        { "an entry's resolution", [](auto& s) { s.entries[0].resolution = "640x361"; } },
        { "an entry's codecs", [](auto& s) { s.entries[2].codecs = "mp4a.40.5"; } },
        { "an entry's frame_rate", [](auto& s) { s.entries[0].frame_rate = "25.000"; } },
        { "an entry's video", [](auto& s) { s.entries[2].video = true; } },
        { "an entry fewer", [](auto& s) { s.entries.pop_back(); } },
        { "reference_entries", [](auto& s) { s.reference_entries = { 4 }; } },
        { "a pair's index", [](auto& s) { s.pairs[1].index = 2; } },
        { "a pair's entries", [](auto& s) { s.pairs[0].entries = { 0 }; } },
        { "ref_time_base", [](auto& s) { s.pairs[0].ref_time_base = kMkv; } },
        { "dis_time_base", [](auto& s) { s.pairs[1].dis_time_base = kTs; } },
        { "ref_rate", [](auto& s) { s.pairs[0].ref_rate = { 50, 2 }; } },
        { "dis_rate", [](auto& s) { s.pairs[1].dis_rate = { 25, 1 }; } },
        { "a sequence's first_index", [](auto& s) { s.pairs[0].ref_sequences[1].first_index = 4; } },
        { "a sequence's first_pts", [](auto& s) { s.pairs[0].dis_sequences[1].first_pts.reset(); } },
        { "a sequence's frames", [](auto& s) { s.pairs[1].ref_sequences[0].frames = 5; } },
        { "a sequence fewer", [](auto& s) { s.pairs[0].ref_sequences.pop_back(); } },
        { "context_first", [](auto& s) { s.pairs[0].context_first = { 0, 4 }; } },
        { "a failure's input", [](auto& s) { s.pairs[0].failures[0].input = "distorted"; } },
        { "a failure's index", [](auto& s) { s.pairs[0].failures[0].index = 2; } },
        { "a failure's pts", [](auto& s) { s.pairs[0].failures[1].pts = 0; } },
        { "a failure's reason", [](auto& s) { s.pairs[0].failures[1].reason = "extra_distorted"; } },
        { "a ref_index", [](auto& s) { s.pairs[0].ref_index[3] = 3; } },
        { "a dis_index", [](auto& s) { s.pairs[1].dis_index[3] = 4; } },
        { "a ref_pts", [](auto& s) { s.pairs[0].ref_pts[4] += 1; } },
        { "a dis_pts", [](auto& s) { s.pairs[1].dis_pts[0] -= 1; } },
        { "a score, by the least step", [](auto& s) {
              s.pairs[0].vmaf[2] = std::nextafter(s.pairs[0].vmaf[2], 0.0); } },
        { "a null score", [](auto& s) { s.pairs[0].vmaf[5] = 0; } },
        { "a frame fewer", [](auto& s) {
              for (auto* v : { &s.pairs[1].ref_index, &s.pairs[1].dis_index, &s.pairs[1].ref_pts,
                               &s.pairs[1].dis_pts })
                  v->pop_back();
              s.pairs[1].vmaf.pop_back(); } },
        { "a pair fewer", [](auto& s) { s.pairs.pop_back(); } },
    };
    for (const auto& [what, change] : in) {
        INFO(what);
        timeline::LadderScores s = base;
        change(s);
        CHECK(timeline::scores_text(s) != digest);
    }

    const std::vector<Change> out = {
        { "the master's URL", [](auto& s) { s.master_url = "<ladder>"; } },
        { "the master's location", [](auto& s) { s.master_location = "<ladder>"; } },
        { "an entry's URI", [](auto& s) { s.entries[3].uri = "<ladder_dir>/mid/p.m3u8"; } },
        { "an entry's URL", [](auto& s) { s.entries[0].url = "<ladder_dir>/low/p.m3u8"; } },
        { "the reference's URL", [](auto& s) { s.reference_url = "<ladder_dir>/top/p.m3u8"; } },
        { "a pair's reference URL", [](auto& s) { s.pairs[0].ref_url = "<tmp>/top.m3u8"; } },
        { "a pair's distorted URL", [](auto& s) { s.pairs[1].dis_url = "<tmp>/mid.m3u8"; } },
    };
    for (const auto& [what, change] : out) {
        INFO(what);
        timeline::LadderScores s = base;
        change(s);
        CHECK(timeline::scores_text(s) == digest);
    }
    // A model file is named by its path, which is out, and its SHA-256, which is in.
    timeline::LadderScores file = base;
    file.model_source = "file";
    file.model        = "<repo>/models/a.json";
    file.model_sha256 = std::string(64, 'f');
    const std::string file_digest = timeline::scores_text(file);
    file.model = "<home>/b.json";
    CHECK(timeline::scores_text(file) == file_digest);
    file.model_sha256[63] = '0';
    CHECK(timeline::scores_text(file) != file_digest);
}

TEST_CASE("timeline::scores_text: one line of JSON, in the layout src/LadderReportRead.h gives",
          "[timeline][ladder_report_read]")
{
    const std::string text = timeline::scores_text(read_ok(sample_report()));
    REQUIRE(text.ends_with("\n"));
    CHECK(text.find('\n') == text.size() - 1);
    CHECK(text.find(' ') == std::string::npos);   // none of the sample's strings has one

    const json::Value v = tree(text);
    std::vector<std::string> keys;
    for (const auto& [key, value] : *v.object()) keys.push_back(key);
    CHECK(keys == std::vector<std::string>{ "digest", "version", "status", "reference_mode", "model",
                                            "model_source", "model_sha256", "scaler", "n_threads",
                                            "n_subsample", "frame_limit", "libvmaf_version",
                                            "libvmaf_package_version", "master_sha256", "entries",
                                            "reference_entries", "pairs" });
    CHECK(*v.find("digest")->string() == "mirage-ladder-scores");
    CHECK(*v.find("version")->integer() == timeline::kScoresTextVersion);
    CHECK(*v.find("model")->string() == "vmaf_v0.6.1");
    CHECK(v.find("model_sha256")->is_null());
    CHECK(*v.find("frame_limit")->integer() == 300);
    CHECK(text_of(v.find("entries")->array()->at(1)) ==
          R"({"index": 1, "line": 5, "bandwidth": 900000, "average_bandwidth": "850000", )"
          R"("resolution": "1920x1080", "codecs": null, "frame_rate": "29.970", "video": true})");
    CHECK(text_of(*v.find("reference_entries")) == "[1]");

    const json::Value& low = v.find("pairs")->array()->at(0);
    keys.clear();
    for (const auto& [key, value] : *low.object()) keys.push_back(key);
    CHECK(keys == std::vector<std::string>{ "index", "entries", "ref_time_base", "dis_time_base",
                                            "ref_rate", "dis_rate", "ref_sequences", "dis_sequences",
                                            "context_first", "failures", "frames" });
    CHECK(*low.find("ref_time_base")->string() == "1/90000");
    CHECK(*low.find("ref_rate")->string() == "25/1");
    CHECK(text_of(*low.find("ref_sequences")) == "[[0, 900000, 5], [5, 900000, 3]]");
    CHECK(text_of(*low.find("context_first")) == "[0, 3, 4]");
    CHECK(text_of(*low.find("failures")) ==
          R"([["reference", 3, 910800, "unmatched_reference"], ["distorted", 7, null, "no_timestamp"]])");
    const json::Value::Array& frames = *low.find("frames")->array();
    REQUIRE(frames.size() == 7);
    CHECK(text_of(frames[0]) == "[0, 0, 900000, 900000, 0.30000000000000004]");
    CHECK(text_of(frames[1]) == "[1, 1, 903600, 903600, 100]");
    CHECK(text_of(frames[5]) == "[6, 5, 903600, 903600, null]");
    const json::Value& mid = v.find("pairs")->array()->at(1);
    CHECK(*mid.find("dis_rate")->string() == "0/1");
    CHECK(text_of(mid.find("frames")->array()->at(3)) == "[3, 3, 910800, 10120, 1e-07]");

    // With nothing read, the same layout, empty.
    CHECK(timeline::scores_text({}) ==
          "{\"digest\":\"mirage-ladder-scores\",\"version\":1,\"status\":\"\",\"reference_mode\":\"\","
          "\"model\":null,\"model_source\":\"\",\"model_sha256\":null,\"scaler\":\"\",\"n_threads\":0,"
          "\"n_subsample\":0,\"frame_limit\":null,\"libvmaf_version\":\"\","
          "\"libvmaf_package_version\":\"\",\"master_sha256\":\"\",\"entries\":[],"
          "\"reference_entries\":[],\"pairs\":[]}\n");
}
