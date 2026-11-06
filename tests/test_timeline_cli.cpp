// mirage_vmaf's timeline and attribute subcommands (src/TimelineCli.h;
// DECISIONS.md D-079): their option tables and config files (the design's
// P-V), the seconds the options take exactly (P-A), the help, the records a
// document keeps of the options, the line timeline prints when it doesn't
// detect, the text of an answer (the design's P-AD) on a timeline the test
// builds by hand, mirage_vmaf's own help, and runs of each subcommand as
// mirage_vmaf runs them (the design's P-T), on a synthetic ad ladder
// (tests/SyntheticLadder.h) scored by mirage_vmaf --ladder: their outputs
// and exit statuses (P-W), intervals and frames past the axis, a lone
// --window, a report whose pairs' numbers skip a failed pair with --curve by
// those numbers (D-092 (b)), a redacted copy of the report and one that
// changed.

#include <catch2/catch_test_macros.hpp>

#include "FileHash.h"
#include "SyntheticLadder.h"
#include "TestTemp.h"
#include "TimelineAxis.h"
#include "TimelineCli.h"
#include "TimelineDoc.h"
#include "TimelineModel.h"
#include "TimelineQuery.h"
#include "VMAFCli.h"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

namespace fs = std::filesystem;
using timeline::Attribution;
using timeline::CurveRef;
using timeline::Event;
using timeline::Source;
using timeline::Value;
using timelinecli::AttributeArgs;
using timelinecli::Format;
using timelinecli::Query;
using timelinecli::TimelineArgs;

namespace {

bool contains(std::string_view s, std::string_view part) { return s.find(part) != std::string_view::npos; }

// A parse with config files from `files` (name -> text), the others missing.
struct Files {
    std::map<std::string, std::string> text;
    vmafcli::ConfigFile operator()(const std::string& path) const
    {
        const auto f = text.find(path);
        if (f == text.end()) return { {}, "No such file or directory" };
        return { f->second, {} };
    }
};

std::string parse_t(const std::vector<std::string_view>& argv, TimelineArgs& out, const Files& files = {})
{
    return timelinecli::parse_timeline(argv, out, files);
}

std::string parse_a(const std::vector<std::string_view>& argv, AttributeArgs& out, const Files& files = {})
{
    return timelinecli::parse_attribute(argv, out, files);
}

bool same(AVRational a, AVRational b) { return a.num == b.num && a.den == b.den; }

// Every line of `text` but the Usage line and the options' rows is at most
// 80 columns wide, as mirage_vmaf's own help (tests/test_vmaf_cli.cpp).
void check_width(const std::string& text)
{
    std::size_t from = 0;
    while (from < text.size()) {
        const std::size_t to = std::min(text.find('\n', from), text.size());
        const std::string_view line(text.data() + from, to - from);
        if (!line.starts_with("Usage:") && !line.starts_with("  -")) {
            INFO(line);
            CHECK(line.size() <= 80);
        }
        from = to + 1;
    }
}

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

// What a run of the built mirage_vmaf did.
struct ToolRun {
    int         status = -1;   // its exit status; -1: it didn't exit
    std::string out, err;      // its stdout and stderr
};

// Runs the built mirage_vmaf with `args`, its stdout and stderr into files
// of the test's temporary directory (tests/TestTemp.h).
ToolRun run_tool(std::vector<std::string> args)
{
    static int runs = 0;
    ++runs;
    const fs::path out_path = testtemp::path(std::format("timeline_cli_stdout_{}.txt", runs));
    const fs::path err_path = testtemp::path(std::format("timeline_cli_stderr_{}.txt", runs));
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
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {}
    ToolRun run;
    run.status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    run.out    = read_text(out_path);
    run.err    = read_text(err_path);
    return run;
}

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

std::size_t count_type(const timeline::Timeline& t, std::string_view type)
{
    return static_cast<std::size_t>(std::count_if(t.events.begin(), t.events.end(),
                                                  [type](const Event& e) { return e.type == type; }));
}

const Value* option(const timeline::Payload& p, std::string_view name)
{
    for (const auto& [n, v] : p)
        if (n == name) return &v;
    return nullptr;
}

} // namespace

// ── mirage_vmaf timeline's options ───────────────────────────────────────────

TEST_CASE("timelinecli::parse_timeline: the options, their defaults and their values",
          "[timeline][timeline_cli]")
{
    TimelineArgs a;
    REQUIRE(parse_t({ "--report", "r.json", "--out", "t.json" }, a).empty());
    CHECK(a.report == "r.json");
    CHECK(a.out == "t.json");
    // D-077's defaults: T = 6, W and N none; D-078's: no margins.
    CHECK(a.detect.threshold == 6.0);
    CHECK_FALSE(a.detect.window);
    CHECK_FALSE(a.detect.min_points);
    CHECK_FALSE(a.detect.given());
    CHECK(same(a.overlap.margin_before, { 0, 1 }));
    CHECK(same(a.overlap.margin_after, { 0, 1 }));
    CHECK(a.config.empty());
    CHECK_FALSE(a.help);

    TimelineArgs b;
    REQUIRE(parse_t({ "--threshold", "4.5", "--window", "50", "--min-points", "0", "--margin-before",
                      "0.04", "--margin-after", "2", "--out", "t.json", "--report", "r.json" }, b).empty());
    CHECK(b.detect.threshold == 4.5);
    CHECK(b.detect.window == 50);
    CHECK(b.detect.min_points == 0);
    CHECK(b.detect.given());
    CHECK(same(b.overlap.margin_before, { 1, 25 }));   // exact (P-A)
    CHECK(same(b.overlap.margin_after, { 2, 1 }));

    // -h / --help anywhere wins.
    TimelineArgs h;
    REQUIRE(parse_t({ "--bogus", "--help" }, h).empty());
    CHECK(h.help);
    REQUIRE(parse_t({ "-h" }, h).empty());
    CHECK(h.help);
}

TEST_CASE("timelinecli::parse_timeline: what it refuses", "[timeline][timeline_cli]")
{
    TimelineArgs a;
    a.report = "kept";
    const auto refused = [&a](const std::vector<std::string_view>& argv) { return parse_t(argv, a); };
    CHECK(refused({}) == "missing --report <report.json>");
    CHECK(refused({ "--report", "r" }) == "missing --out <timeline.json>");
    CHECK(refused({ "--report", "r", "--out", "o", "extra" }) == "unknown argument 'extra'");
    CHECK(refused({ "--report", "r", "--out", "o", "--interval", "1:2" }) == "unknown argument '--interval'");
    CHECK(refused({ "--report", "r", "--report", "s", "--out", "o" }) == "--report is given more than once");
    CHECK(refused({ "--report", "r", "--out" }) == "--out needs a value: --out <timeline.json>");
    CHECK(refused({ "--report", "--out", "o" }) == "--report needs a value: --report <report.json>");
    CHECK(refused({ "--report", "", "--out", "o" }) == "--report needs a non-empty value");
    for (std::string_view t : { "-1", "abc", "inf", "nan", "6x", "" }) {
        INFO(t);
        CHECK(contains(refused({ "--report", "r", "--out", "o", "--threshold", t }), "--threshold"));
    }
    CHECK(refused({ "--report", "r", "--out", "o", "--threshold", "-1" }) ==
          "--threshold wants a number of VMAF points from 0, such as 6, not '-1'");
    CHECK(refused({ "--report", "r", "--out", "o", "--window", "0" }) ==
          "--window wants a whole number of points from 1, not '0'");
    for (std::string_view w : { "1.5", "-3", "+3", "x" })
        CHECK(contains(refused({ "--report", "r", "--out", "o", "--window", w }), "--window wants"));
    CHECK(refused({ "--report", "r", "--out", "o", "--min-points", "-1" }) ==
          "--min-points wants a whole number of points from 0, not '-1'");
    for (std::string_view s : { "-0.5", "1/2", ".5", "5.", "1e3", " 1", "0.5s" }) {
        INFO(s);
        CHECK(refused({ "--report", "r", "--out", "o", "--margin-before", s }) ==
              std::format("--margin-before wants a number of seconds from 0, such as 0.5, not '{}'", s));
    }
    // A failed parse leaves the output alone.
    CHECK(a.report == "kept");
}

TEST_CASE("timelinecli::parse_timeline: a config file, each option a setting, the command line "
          "replacing it (D-030)", "[timeline][timeline_cli]")
{
    const Files files{ { { "c.conf", "\xEF\xBB\xBF# the acceptance tests' parameters\r\n"
                                     "report = from-file.json\r\n"
                                     "\n"
                                     "  window = 50  \n"
                                     "min-points = 12\n"
                                     "threshold=6\n"
                                     "margin-after = 0.5\n" },
                         { "bad.conf", "out = a\nwindow = 0\n" },
                         { "twice.conf", "window = 1\nwindow = 2\n" },
                         { "unknown.conf", "windows = 1\n" },
                         { "dashes.conf", "--window = 1\n" },
                         { "config.conf", "config = other.conf\n" },
                         { "empty.conf", "window =\n" },
                         { "noeq.conf", "window 50\n" } } };
    TimelineArgs a;
    REQUIRE(parse_t({ "--config", "c.conf", "--out", "t.json", "--report", "line.json" }, a, files).empty());
    CHECK(a.report == "line.json");   // the command line replaces the file's setting
    CHECK(a.out == "t.json");
    CHECK(a.detect.window == 50);
    CHECK(a.detect.min_points == 12);
    CHECK(a.detect.threshold == 6.0);
    CHECK(same(a.overlap.margin_after, { 1, 2 }));
    CHECK(a.config == "c.conf");

    TimelineArgs b;
    REQUIRE(parse_t({ "--config", "c.conf", "--out", "t.json", "--window", "10" }, b, files).empty());
    CHECK(b.report == "from-file.json");   // a required option from the file
    CHECK(b.detect.window == 10);

    TimelineArgs c;
    const auto with = [&c, &files](std::string_view file) {
        return parse_t({ "--config", file, "--report", "r", "--out", "o" }, c, files);
    };
    CHECK(with("missing.conf") == "cannot read the config file missing.conf: No such file or directory");
    CHECK(with("bad.conf") == "config file bad.conf, line 2: window wants a whole number of points from "
                              "1, not '0'");
    CHECK(with("twice.conf") == "config file twice.conf, line 2: window is set more than once (first on "
                                "line 1)");
    CHECK(with("unknown.conf") == "config file unknown.conf, line 1: unknown setting 'windows'");
    CHECK(with("dashes.conf") == "config file dashes.conf, line 1: unknown setting '--window' (names "
                                 "are written without --)");
    CHECK(with("config.conf") == "config file config.conf, line 1: config can't be set in a config file");
    CHECK(with("empty.conf") == "config file empty.conf, line 1: window needs a non-empty value");
    CHECK(with("noeq.conf") == "config file noeq.conf, line 1: expected name = value, not 'window 50'");
}

// ── mirage_vmaf attribute's options ──────────────────────────────────────────

TEST_CASE("timelinecli::parse_attribute: each query, the curve, the format, the margins",
          "[timeline][timeline_cli]")
{
    AttributeArgs a;
    REQUIRE(parse_a({ "--timeline", "t.json", "--interval", "3:5.04" }, a).empty());
    CHECK(a.timeline == "t.json");
    CHECK(a.query == Query::Interval);
    CHECK(same(a.from, { 3, 1 }));
    CHECK(same(a.to, { 126, 25 }));
    CHECK(a.report.empty());   // the document's, by default
    CHECK_FALSE(a.curve);      // every pair's curve
    CHECK(a.format == Format::Text);
    CHECK(a.out.empty());

    AttributeArgs b;
    REQUIRE(parse_a({ "--frames", "75-124", "--timeline", "t.json", "--curve", "pair:1", "--format", "json",
                      "--out", "a.json", "--report", "r.json", "--margin-before", "0.5" }, b).empty());
    CHECK(b.query == Query::Frames);
    CHECK((b.first == 75 && b.last == 124));
    // --curve names a pair by its number, the report's pairs[].index (D-092
    // (b)), which the answer maps to its curve.
    REQUIRE(b.curve);
    CHECK((b.curve->kind == CurveRef::Kind::Pair && b.curve->number == 1));
    CHECK(b.format == Format::Json);
    CHECK(b.out == "a.json");
    CHECK(b.report == "r.json");
    CHECK(same(b.overlap.margin_before, { 1, 2 }));

    AttributeArgs c;
    REQUIRE(parse_a({ "--timeline", "t.json", "--drop", "2" }, c).empty());
    CHECK(c.query == Query::Drop);
    CHECK(c.drop == 2);

    AttributeArgs d;
    REQUIRE(parse_a({ "--timeline", "t.json", "--auto", "--window", "50", "--min-points", "12",
                      "--threshold", "3" }, d).empty());
    CHECK(d.query == Query::Auto);
    CHECK(d.detect.window == 50);
    CHECK(d.detect.min_points == 12);
    CHECK(d.detect.threshold == 3.0);

    // An empty interval and a frame range of one frame are queries too.
    AttributeArgs e;
    REQUIRE(parse_a({ "--timeline", "t", "--interval", "3:3" }, e).empty());
    REQUIRE(parse_a({ "--timeline", "t", "--frames", "7-7" }, e).empty());
    CHECK((e.first == 7 && e.last == 7));
}

TEST_CASE("timelinecli::parse_attribute: one query at a time, and detection's options with --auto only",
          "[timeline][timeline_cli]")
{
    AttributeArgs a;
    const auto refused = [&a](const std::vector<std::string_view>& argv) { return parse_a(argv, a); };
    CHECK(refused({ "--interval", "1:2" }) == "missing --timeline <timeline.json>");
    CHECK(refused({ "--timeline", "t" }) == "missing a query: --interval, --frames, --drop or --auto");
    CHECK(refused({ "--timeline", "t", "--interval", "1:2", "--drop", "1" }) ==
          "--interval and --drop can't be given together: one query at a time");
    CHECK(refused({ "--timeline", "t", "--auto", "--frames", "1-2" }) ==
          "--frames and --auto can't be given together: one query at a time");
    CHECK(refused({ "--timeline", "t", "--drop", "1", "--window", "50" }) ==
          "--window goes with --auto, not with --drop");
    CHECK(refused({ "--timeline", "t", "--threshold", "3", "--interval", "1:2" }) ==
          "--threshold goes with --auto, not with --interval");
    CHECK(refused({ "--timeline", "t", "--window", "50", "--min-points", "12" }) ==
          "--window goes with --auto, which isn't given");
    // D-077: W and N have no default.
    CHECK(refused({ "--timeline", "t", "--auto" }) ==
          "--auto needs --window and --min-points: W and N have no default (D-077)");
    CHECK(refused({ "--timeline", "t", "--auto", "--window", "50" }) ==
          "--auto needs --window and --min-points: W and N have no default (D-077)");
    CHECK(refused({ "--timeline", "t", "--auto", "on" }) == "unknown argument 'on'");   // a flag takes no value

    CHECK(refused({ "--timeline", "t", "--interval", "5:3" }) == "--interval wants t0 at or before t1, not '5:3'");
    for (std::string_view i : { "3", "3:", ":5", "a:b", "-1:2", "1:2:3" })
        CHECK(contains(refused({ "--timeline", "t", "--interval", i }), "--interval wants <t0>:<t1>"));
    CHECK(refused({ "--timeline", "t", "--frames", "9-3" }) == "--frames wants first at or before last, not '9-3'");
    for (std::string_view f : { "3", "-3", "3-", "a-b", "1.5-2" })
        CHECK(contains(refused({ "--timeline", "t", "--frames", f }), "--frames wants <first>-<last>"));
    CHECK(refused({ "--timeline", "t", "--drop", "0" }) ==
          "--drop wants a drop's number, a whole number from 1, not '0'");
    for (std::string_view c : { "1", "pair:", "pair:-1", "session:1", "pair:x", "pair:99999999999" })
        CHECK(contains(refused({ "--timeline", "t", "--drop", "1", "--curve", c }), "--curve wants pair:<k>"));
    CHECK(refused({ "--timeline", "t", "--drop", "1", "--format", "xml" }) ==
          "--format wants text or json, not 'xml'");
}

TEST_CASE("timelinecli::parse_attribute: a config file, its query one setting, --auto as auto = on",
          "[timeline][timeline_cli]")
{
    const Files files{ { { "auto.conf", "timeline = t.json\nauto = on\nwindow = 50\nmin-points = 12\n"
                                        "format = json\n" },
                         { "off.conf", "auto = off\n" },
                         { "two.conf", "drop = 1\nwindow = 50\n" },
                         { "drop.conf", "drop = 2\ncurve = pair:1\n" } } };
    AttributeArgs a;
    REQUIRE(parse_a({ "--config", "auto.conf" }, a, files).empty());
    CHECK(a.query == Query::Auto);
    CHECK(a.timeline == "t.json");
    CHECK(a.detect.given());
    CHECK(a.format == Format::Json);

    // A query on the command line replaces the file's, and leaves out what
    // goes with the file's.
    AttributeArgs b;
    REQUIRE(parse_a({ "--config", "auto.conf", "--drop", "1" }, b, files).empty());
    CHECK(b.query == Query::Drop);
    CHECK_FALSE(b.detect.window);
    CHECK_FALSE(b.detect.min_points);
    CHECK(b.format == Format::Json);   // an option of every query stays

    AttributeArgs c;
    REQUIRE(parse_a({ "--config", "drop.conf", "--timeline", "t", "--interval", "1:2" }, c, files).empty());
    CHECK(c.query == Query::Interval);
    REQUIRE(c.curve);
    CHECK(c.curve->number == 1);

    AttributeArgs d;
    REQUIRE(parse_a({ "--config", "auto.conf", "--min-points", "3" }, d, files).empty());
    CHECK(d.query == Query::Auto);
    CHECK(d.detect.min_points == 3);
    CHECK(d.detect.window == 50);

    AttributeArgs e;
    CHECK(parse_a({ "--config", "off.conf", "--timeline", "t" }, e, files) ==
          "config file off.conf, line 1: auto takes no value but on, as auto = on, not 'off'");
    CHECK(parse_a({ "--config", "two.conf", "--timeline", "t" }, e, files) ==
          "config file two.conf, drop (line 1) and window (line 2) can't both be set: one query at a "
          "time, with what goes with it");
}

TEST_CASE("timelinecli::parse_seconds: decimal seconds as exact rationals (P-A)", "[timeline][timeline_cli]")
{
    const auto q = [](std::string_view s) { return timelinecli::parse_seconds(s); };
    REQUIRE(q("2"));
    CHECK(same(*q("2"), { 2, 1 }));
    CHECK(same(*q("0"), { 0, 1 }));
    CHECK(same(*q("0.5"), { 1, 2 }));
    CHECK(same(*q("3.040"), { 76, 25 }));
    CHECK(same(*q("0.04"), { 1, 25 }));
    CHECK(same(*q("29.988"), { 7497, 250 }));
    CHECK(same(*q("007.250"), { 29, 4 }));
    CHECK(same(*q("2147483647"), { 2147483647, 1 }));
    for (std::string_view bad : { "", ".", ".5", "5.", "-1", "+1", "1e3", "1/2", " 1", "1 ", "1.2.3", "0x10",
                                  "2147483648", "0.0000000001", "1234567890123456789" }) {
        INFO(bad);
        CHECK_FALSE(q(bad));
    }
}

TEST_CASE("timelinecli::check_interval: an interval holds no time past the axis, as --frames goes to "
          "its last frame only", "[timeline][timeline_cli]")
{
    // A frame grid of 50 frames at 25 fps: frame i at i/25 s, the last, 49,
    // at 1.96 s, the axis's end at 2 s.
    timeline::Axis a;
    a.clock          = "frame-grid";
    a.frame_duration = { 1, 25 };
    a.frames         = 50;
    a.tick_den       = 25;
    for (std::int64_t i = 0; i < 50; ++i) a.ticks.push_back(i);
    const auto q = [](int num, int den) { return AVRational{ num, den }; };
    CHECK(timelinecli::check_interval(a, q(0, 1), q(2, 1)).empty());       // the whole axis
    CHECK(timelinecli::check_interval(a, q(49, 25), q(2, 1)).empty());     // its last frame
    CHECK(timelinecli::check_interval(a, q(1, 1), q(1, 1)).empty());       // empty, at frame 25
    CHECK(timelinecli::check_interval(a, q(1, 1), q(201, 100)) ==
          "--interval 1:2.01: the document's axis ends at 2 s, where its last reference frame, 49, ends");
    CHECK(timelinecli::check_interval(a, q(197, 100), q(2, 1)) ==
          "--interval 1.97:2: no reference frame of the document's axis starts at or after 1.97 s; its "
          "last, 49, starts at 1.96 s");
    CHECK(timelinecli::check_interval(a, q(2, 1), q(2, 1)) ==
          "--interval 2:2: no reference frame of the document's axis starts at or after 2 s; its last, 49, "
          "starts at 1.96 s");

    // An axis without seconds has every frame at 0 s (src/TimelineAxis.h).
    timeline::Axis none = a;
    none.frame_duration = { 0, 1 };
    none.tick_den       = 1;
    none.ticks.assign(50, 0);
    CHECK(timelinecli::check_interval(none, q(0, 1), q(0, 1)).empty());
    CHECK(timelinecli::check_interval(none, q(0, 1), q(1, 2)) ==
          "--interval 0:0.5: the document's axis has no seconds, each of its 50 reference frames at 0 s "
          "(its warnings say why): give the frames with --frames");
    CHECK(timelinecli::check_interval(timeline::Axis{}, q(0, 1), q(0, 1)) ==
          "--interval 0:0: the document's axis has no reference frame");
}

// ── Help and records ─────────────────────────────────────────────────────────

TEST_CASE("timelinecli: each subcommand's help lists its options; mirage_vmaf's names both",
          "[timeline][timeline_cli]")
{
    const std::string t = timelinecli::timeline_usage();
    CHECK(t.starts_with("Usage: mirage_vmaf timeline --report <report.json> --out <timeline.json> "
                        "[--threshold T] [--window W] [--min-points N] [--margin-before S] "
                        "[--margin-after S] [--config <file>]\n\n"));
    for (std::string_view opt : { "--report", "--out", "--threshold", "--window", "--min-points",
                                  "--margin-before", "--margin-after", "--config", "-h, --help" }) {
        INFO(opt);
        CHECK(contains(t, std::format("\n  {} ", opt)));
    }
    CHECK(contains(t, "Detection: "));
    CHECK(contains(t, "Config file: "));
    CHECK(contains(t, "Exit status: "));
    check_width(t);

    const std::string a = timelinecli::attribute_usage();
    CHECK(a.starts_with("Usage: mirage_vmaf attribute --timeline <timeline.json> (--interval <t0>:<t1> | "
                        "--frames <first>-<last> | --drop K | --auto) [--report <report.json>] "
                        "[--curve pair:<k>] [--threshold T] [--window W] [--min-points N] "
                        "[--margin-before S] [--margin-after S] [--format text|json] [--out <file>] "
                        "[--config <file>]\n\n"));
    for (std::string_view opt : { "--timeline", "--interval", "--frames", "--drop", "--auto", "--report",
                                  "--curve", "--threshold", "--window", "--min-points", "--margin-before",
                                  "--margin-after", "--format", "--out", "--config", "-h, --help" }) {
        INFO(opt);
        CHECK(contains(a, std::format("\n  {} ", opt)));
    }
    CHECK(contains(a, "Drops are numbered from 1"));
    CHECK(contains(a, "--auto is auto = on"));
    CHECK(contains(a, "\nAn --interval t0:t1 is the reference frames from the first whose seconds are\n"));
    CHECK(contains(a, "frames or an interval that the\ndocument doesn't have."));
    check_width(a);

    // mirage_vmaf --help names the subcommands after its synopsis lines,
    // which tests/test_vmaf_cli.cpp pins.
    const std::string m = vmafcli::usage();
    const std::size_t subcommands = m.find("\nSubcommands, each with its own --help: mirage_vmaf timeline");
    REQUIRE(subcommands != std::string::npos);
    CHECK(subcommands > m.find("\n\n"));
    CHECK(subcommands < m.find("\nOptions:\n"));
    CHECK(contains(m, "mirage_vmaf attribute --timeline\n<timeline.json>"));
}

TEST_CASE("timelinecli::timeline_options and attribute_options: every option with its value",
          "[timeline][timeline_cli]")
{
    TimelineArgs t;
    REQUIRE(timelinecli::parse_timeline({ "--report", "r.json", "--out", "o.json", "--window", "50",
                                          "--margin-before", "0.04" }, t).empty());
    const timeline::Payload p = timelinecli::timeline_options(t);
    std::vector<std::string> names;
    for (const auto& [n, v] : p) names.push_back(n);
    CHECK(names == std::vector<std::string>{ "report", "out", "threshold", "window", "min-points",
                                             "margin-before", "margin-after" });
    CHECK(std::get<std::string>(option(p, "report")->v) == "r.json");
    CHECK(std::get<double>(option(p, "threshold")->v) == 6.0);
    CHECK(std::get<std::int64_t>(option(p, "window")->v) == 50);
    CHECK(std::holds_alternative<std::monostate>(option(p, "min-points")->v));
    CHECK(std::get<std::string>(option(p, "margin-before")->v) == "1/25");
    CHECK(std::get<std::string>(option(p, "margin-after")->v) == "0/1");

    AttributeArgs a;
    REQUIRE(timelinecli::parse_attribute({ "--timeline", "t.json", "--interval", "2.5:3.04", "--curve",
                                           "pair:1" }, a).empty());
    const timeline::Payload q = timelinecli::attribute_options(a, "the report.json");
    names.clear();
    for (const auto& [n, v] : q) names.push_back(n);
    CHECK(names == std::vector<std::string>{ "timeline", "interval", "frames", "drop", "auto", "report",
                                             "curve", "threshold", "window", "min-points", "margin-before",
                                             "margin-after", "format", "out" });
    const auto& interval = std::get<Value::List>(option(q, "interval")->v);
    REQUIRE(interval.size() == 2);
    CHECK(std::get<std::string>(interval[0].v) == "5/2");
    CHECK(std::get<std::string>(interval[1].v) == "76/25");
    CHECK(std::holds_alternative<std::monostate>(option(q, "frames")->v));
    CHECK(std::get<bool>(option(q, "auto")->v) == false);
    CHECK(std::get<std::string>(option(q, "report")->v) == "the report.json");
    CHECK(std::get<std::string>(option(q, "curve")->v) == "pair:1");
    CHECK(std::get<std::string>(option(q, "format")->v) == "text");
    CHECK(std::holds_alternative<std::monostate>(option(q, "out")->v));
}

TEST_CASE("timelinecli::no_detection_note: which of W and N is missing (D-077)", "[timeline][timeline_cli]")
{
    // A lone W or N is D-077's "not both given", as the owner's D-099 (c)
    // keeps it: no detection, and the line names the option missing.
    timeline::DetectOptions d;
    CHECK(timelinecli::no_detection_note(d) ==
          "mirage_vmaf timeline: no detection: neither --window nor --min-points was given, and W and N "
          "have no default (D-077): the document has \"detection\": null and no attributions\n");
    d.window = 50;
    CHECK(timelinecli::no_detection_note(d) ==
          "mirage_vmaf timeline: no detection: --min-points wasn't given, and W and N have no default "
          "(D-077): the document has \"detection\": null and no attributions\n");
    d.window.reset();
    d.min_points = 12;
    CHECK(timelinecli::no_detection_note(d) ==
          "mirage_vmaf timeline: no detection: --window wasn't given, and W and N have no default "
          "(D-077): the document has \"detection\": null and no attributions\n");
    d.window = 50;
    CHECK(timelinecli::no_detection_note(d).empty());
}

// ── The text of an answer (the design's P-AD) ────────────────────────────────

TEST_CASE("timelinecli::answer_text: a drop's block, then its events by source, with offsets in "
          "seconds and in frames", "[timeline][timeline_cli]")
{
    // Two variants on a frame grid of 50 frames at 25 fps: the reference,
    // entry 0, and the pair's distorted playlist, named by entries 1 and 3.
    timeline::Timeline t;
    t.axis.clock          = "frame-grid";
    t.axis.checked        = "report-only";
    t.axis.time_base      = { 1, 90000 };
    t.axis.frame_duration = { 1, 25 };
    t.axis.frames         = 50;
    t.axis.tick_den       = 25;
    for (std::int64_t i = 0; i < 50; ++i) {
        t.axis.keys.push_back({ .sequence = 0, .pts = i * 3600 });
        t.axis.ticks.push_back(i);
    }
    timeline::Variant ref, low;
    ref.index = 0;
    ref.entries = { 0 };
    ref.url = "http://h/top.m3u8";
    ref.bandwidth = 900000;
    ref.resolution = "640x360";
    ref.rank_from_top = 1;
    ref.rank_from_bottom = 2;
    low.index = 1;
    low.pair = 0;
    low.entries = { 1, 3 };
    low.url = "http://h/low.m3u8";
    low.bandwidth = 300000;
    low.resolution = "320x180";
    low.rank_from_top = 2;
    low.rank_from_bottom = 1;
    t.variants = { ref, low };
    timeline::Curve c;
    c.variant = 1;
    for (std::int64_t i = 0; i < 50; ++i) {
        c.ref_index.push_back(i);
        c.vmaf.push_back(i >= 20 && i < 30 ? 40.0 : 90.0);
    }
    t.curves = { c };

    const auto event = [&t](Source s, std::string type, int variant, std::optional<std::int64_t> segment,
                            std::int64_t at, std::optional<std::int64_t> end, timeline::Payload p) {
        Event e;
        e.source    = s;
        e.type      = std::move(type);
        e.variant   = variant;
        e.segment   = segment;
        e.ref_index = at;
        e.ref_end   = end;
        e.key       = t.axis.keys[static_cast<std::size_t>(at)];
        e.payload   = std::move(p);
        t.events.push_back(std::move(e));
    };
    const auto i64 = [](std::int64_t v) { return Value{ v }; };
    const auto str = [](const char* s) { return Value{ std::string(s) }; };
    event(Source::Playlist, "segment", 0, 0, 0, 25,
          { { "line", i64(5) }, { "uri", str("top0.ts") }, { "extinf", Value{ 1.0 } }, { "byte_range", Value{} } });
    event(Source::Playlist, "segment", 1, 1, 25, 50,
          { { "line", i64(7) }, { "uri", str("low1.ts") }, { "extinf", i64(1) },
            { "byte_range", Value{ Value::Object{ { "length", i64(100) }, { "offset", i64(200) } } } } });
    event(Source::Playlist, "tag", 0, 0, 0, std::nullopt,
          { { "line", i64(3) }, { "name", str("EXT-X-FOO") }, { "value", Value{} } });
    event(Source::Scte35, "cue_out", 1, 1, 25, std::nullopt,
          { { "line", i64(9) }, { "form", str("CUE-OUT") }, { "raw", str("#EXT-X-CUE-OUT:1.0") } });
    event(Source::Scte35, "ad_break", 1, 1, 25, 50,
          { { "cue_out_line", i64(9) }, { "cue_in_line", Value{} }, { "end", str("duration") },
            { "durations", Value{ Value::Object{ { "duration", i64(1) } } } } });
    event(Source::Decoder, "keyframe", 0, 1, 25, std::nullopt, { { "index", i64(25) } });
    event(Source::Decoder, "corrupt_packet", 1, 0, 24, std::nullopt,
          { { "index", i64(24) },
            { "next_boundary", Value{ Value::Object{ { "media_sequence", i64(1) }, { "index", i64(25) },
                                                     { "ref_index", i64(25) }, { "discontinuity", Value{ true } } } } } });
    event(Source::Vmaf, "context_start", 1, std::nullopt, 20, std::nullopt,
          { { "context", i64(1) }, { "first_index", i64(20) } });
    event(Source::Vmaf, "unpaired_frame", 1, std::nullopt, 22, std::nullopt,
          { { "input", str("distorted") }, { "index", i64(22) }, { "pts", Value{ 0.88 } },
            { "reason", str("extra_distorted") } });
    timeline::assign_ids(t.events);
    t.build_index();

    timeline::QueryOptions margins;
    margins.margin_before = { 4, 5 };   // 0.8 s: from frame 0
    Attribution a    = timeline::attribute(t, CurveRef{ .index = 0 }, { .ref_index = 20, .ref_end = 30 }, margins);
    a.detected       = true;
    a.baseline       = 90.0;
    a.drop_magnitude = 50.0;
    a.mean_drop      = 50.0;
    const std::string text = timelinecli::answer_text(t, { CurveRef{ .index = 0 } }, { a }, { 3 });
    CHECK(text ==
          "\n"
          "Curve pair:0: entries 1, 3 (http://h/low.m3u8, 320x180, BANDWIDTH 300000; rank 2 from the top, 1 "
          "from the bottom), scored against entry 0 (http://h/top.m3u8, 640x360, BANDWIDTH 900000; rank 1 "
          "from the top, 2 from the bottom)\n"
          "Drop 3: reference frames 20-29 (10 frames), 0.800-1.200 s; 10 points, baseline 90.000, min "
          "40.000, mean 40.000, drop_magnitude 50.000, mean_drop 50.000; events looked for in reference "
          "frames 0-29 (30 frames) with the margins\n"
          "  vmaf     context_start    entries 1, 3 at +0.000 s (+0 frames): libvmaf context 1, from frame "
          "pair 20\n"
          "  vmaf     unpaired_frame   entries 1, 3 at +0.080 s (+2 frames): distorted frame 22, "
          "extra_distorted\n"
          "  playlist tag              entry 0, segment 0 at -0.800 s (-20 frames): line 3, #EXT-X-FOO\n"
          "  playlist segment          entry 0, segment 0 at -0.800 s to +0.200 s (-20 to +5 frames): "
          "top0.ts, EXTINF 1 s\n"
          "  playlist segment          entries 1, 3, segment 1 at +0.200 s to +1.200 s (+5 to +30 frames): "
          "low1.ts, EXTINF 1 s, bytes 100 at 200\n"
          "  scte35   cue_out          entries 1, 3, segment 1 at +0.200 s (+5 frames): line 9, "
          "#EXT-X-CUE-OUT:1.0\n"
          "  scte35   ad_break         entries 1, 3, segment 1 at +0.200 s to +1.200 s (+5 to +30 frames): "
          "from the cue_out at line 9, ended by its duration of 1 s\n"
          "  decoder  corrupt_packet   entries 1, 3, segment 0 at +0.160 s (+4 frames): frame 24 of its "
          "input; the next segment, 1, starts at reference frame 25, after an EXT-X-DISCONTINUITY\n"
          "  decoder  keyframe         entry 0, segment 1 at +0.200 s (+5 frames): frame 25 of its input\n");

    // A user's interval: no drop's numbers; a curve without an answer.
    const Attribution b = timeline::attribute(t, CurveRef{ .index = 0 }, { .ref_index = 30, .ref_end = 30 }, {});
    const std::string empty = timelinecli::answer_text(t, { CurveRef{ .index = 0 } }, { b }, { 0 });
    CHECK(contains(empty, "Interval: no reference frame, an empty interval at 30, 1.200-1.200 s; 0 points\n"));
    CHECK(contains(empty, "  playlist segment          entries 1, 3, segment 1 at -0.200 s to +0.800 s "
                          "(-5 to +20 frames): low1.ts"));
    CHECK(timelinecli::answer_text(t, { CurveRef{ .index = 0 } }, {}, {}) ==
          "\nCurve pair:0: entries 1, 3 (http://h/low.m3u8, 320x180, BANDWIDTH 300000; rank 2 from the top, "
          "1 from the bottom), scored against entry 0 (http://h/top.m3u8, 640x360, BANDWIDTH 900000; rank 1 "
          "from the top, 2 from the bottom)\n  no drop\n");
    // The curve at place 0 is named by its pair's number, the report's
    // pairs[].index, here the plan's pair 3 (D-092 (b)).
    t.variants[1].pair = 3;
    CHECK(timelinecli::answer_text(t, { CurveRef{ .index = 0 } }, {}, {})
              .starts_with("\nCurve pair:3: entries 1, 3 ("));
}

// ── The subcommands, run as mirage_vmaf runs them ────────────────────────────

TEST_CASE("mirage_vmaf timeline and attribute: run as programs on a synthetic ad ladder",
          "[timeline][timeline_cli]")
{
    // D-069's ad ladder, from the main content's origin, made once in this
    // test case's process (P-Q) and scored once by mirage_vmaf --ladder, in
    // top-rung mode; its documents beside it in the media directory, which
    // MIRAGE_KEEP_MEDIA keeps (P-U).
    const synthladder::Ladder& lad = synthladder::ladder({ .kind = synthladder::Kind::Ad });
    REQUIRE(lad.error.empty());
    const fs::path    dir      = synthladder::media_dir();
    const std::string report   = (dir / "ad.json").string();
    const std::string events   = (dir / "ad.events.timeline.json").string();
    const std::string detected = (dir / "ad.timeline.json").string();
    const std::string config   = (dir / "ad.timeline.conf").string();
    const std::string lone     = (dir / "ad.window.timeline.json").string();
    static bool made = false;
    static ToolRun events_run, detected_run, lone_run;
    if (!made) {
        WARN("encoder: " << synthladder::encoder_name(lad.layout.encoder));   // Q33 (c)
        const ToolRun scored = run_tool({ "--ladder", lad.master, "--out", report });
        INFO(scored.err);
        REQUIRE(scored.status == 0);
        events_run = run_tool({ "timeline", "--report", report, "--out", events });
        // The window from a config file, the rest on the command line (D-030).
        write_text(config, "window = 50\nthreshold = 9\n");
        detected_run = run_tool({ "timeline", "--config", config, "--report", report, "--out", detected,
                                  "--min-points", "12", "--threshold", "6" });
        lone_run = run_tool({ "timeline", "--report", report, "--out", lone, "--window", "50" });
        made = true;
    }

    SECTION("timeline without W and N writes the events, detection null, and says so (D-077)")
    {
        INFO(events_run.err);
        REQUIRE(events_run.status == 0);
        CHECK(contains(events_run.err, "mirage_vmaf timeline: no detection: neither --window nor "
                                       "--min-points was given, and W and N have no default (D-077): "
                                       "the document has \"detection\": null and no attributions\n"));
        CHECK(contains(events_run.out, "no detection; 0 warnings; document written to " + events + "\n"));
        const std::string text = read_text(events);
        const Doc d = read_doc(text);
        INFO(d.err);
        REQUIRE(d.ok);
        // A progress line per variant, numbered as the document, its
        // warnings and its errors number them, then counted.
        REQUIRE(d.timeline.variants.size() == 3);
        for (std::size_t v = 0; v < 3; ++v)
            CHECK(contains(events_run.out,
                           std::format("mirage_vmaf timeline: variant {} ({} of 3), {}: 200 frames in 8 "
                                       "segments\n", v, v + 1, d.timeline.variants[v].url)));
        CHECK(d.info.command == "timeline");
        CHECK_FALSE(d.info.detection);
        CHECK(d.attributions.empty());
        CHECK_FALSE(d.info.config_file);
        CHECK(d.info.report.path == report);
        CHECK(d.info.report.sha256 == filehash::sha256_hex(read_text(report)));
        CHECK(d.info.report.status == "complete");
        CHECK(d.info.ffmpeg_libraries.size() == 5);
        CHECK(d.timeline.axis.clock == "playlist-time");
        CHECK(d.timeline.axis.frames == synthladder::kFrames);
        CHECK(d.timeline.warnings.empty());
        CHECK(d.timeline.axis.warnings.empty());
        for (const timeline::Variant& v : d.timeline.variants) CHECK(v.segment_map == "ok");
        // Each rung's copy of each tag (D-081 (f)), the keyframe each segment
        // starts on (tests/SyntheticLadder.h), and the context libvmaf starts
        // at the ad's first frame on each pair (D-037).
        CHECK(count_type(d.timeline, "segment") == 24);
        CHECK(count_type(d.timeline, "discontinuity") == 6);
        CHECK(count_type(d.timeline, "cue_out") == 3);
        CHECK(count_type(d.timeline, "cue_out_cont") == 3);
        CHECK(count_type(d.timeline, "cue_in") == 3);
        CHECK(count_type(d.timeline, "ad_break") == 3);
        CHECK(count_type(d.timeline, "keyframe") == 24);
        CHECK(count_type(d.timeline, "context_start") == 2);
        for (const Event& e : d.timeline.events)
            if (e.type == "ad_break") CHECK((e.ref_index == 75 && e.ref_end == 125));
        // The options record: every option with its value (P-V).
        CHECK(std::holds_alternative<std::monostate>(option(d.info.options, "window")->v));
        CHECK(std::get<std::string>(option(d.info.options, "out")->v) == events);
    }

    SECTION("timeline with --window alone doesn't detect, and names --min-points (D-077)")
    {
        // A lone W or N is D-077's "not both given", which the owner's
        // D-099 (c) keeps: exit status 0, detection null, and the W given
        // recorded with the options.
        INFO(lone_run.err);
        REQUIRE(lone_run.status == 0);
        CHECK(contains(lone_run.err, "mirage_vmaf timeline: no detection: --min-points wasn't given, and W "
                                     "and N have no default (D-077): the document has \"detection\": null "
                                     "and no attributions\n"));
        CHECK(contains(lone_run.out, "no detection; 0 warnings; document written to " + lone + "\n"));
        const Doc d = read_doc(read_text(lone));
        INFO(d.err);
        REQUIRE(d.ok);
        CHECK_FALSE(d.info.detection);
        CHECK(d.attributions.empty());
        CHECK(std::get<std::int64_t>(option(d.info.options, "window")->v) == 50);
        CHECK(std::holds_alternative<std::monostate>(option(d.info.options, "min-points")->v));
    }

    SECTION("timeline with W and N detects on every pair's curve, its config file recorded")
    {
        INFO(detected_run.err);
        REQUIRE(detected_run.status == 0);
        CHECK_FALSE(contains(detected_run.err, "no detection"));
        CHECK(contains(detected_run.out, "1 drop on 2 curves; 0 warnings; document written to " + detected));
        const Doc d = read_doc(read_text(detected));
        INFO(d.err);
        REQUIRE(d.ok);
        REQUIRE(d.info.detection);
        CHECK(d.info.detection->options.window == 50);
        CHECK(d.info.detection->options.min_points == 12);
        CHECK(d.info.detection->options.threshold == 6.0);   // the command line's
        CHECK(d.info.detection->curves.size() == 2);
        REQUIRE(d.info.config_file);
        CHECK(d.info.config_file->path == config);
        CHECK(d.info.config_file->sha256 == filehash::sha256_hex("window = 50\nthreshold = 9\n"));
        // v180's ad is pair 1's drop, and v270 has none (design §9.2).
        REQUIRE(d.attributions.size() == 1);
        const Attribution& a = d.attributions[0];
        CHECK(a.detected);
        CHECK(a.curve.index == 1);
        CHECK((a.interval.ref_index == 75 && a.interval.ref_end == 125));
        std::vector<std::string> cues;
        for (const timeline::Found& f : a.by_source[static_cast<std::size_t>(Source::Scte35)])
            if (f.offset_frames == 0) cues.push_back(d.timeline.events[f.event].id);
        CHECK(cues == std::vector<std::string>{ "scte35/cue_out/0/0", "scte35/cue_out/2/0",
                                                "scte35/ad_break/0/0", "scte35/ad_break/2/0" });
    }

    SECTION("attribute --auto prints each curve's drops and their events, as text by default")
    {
        const ToolRun r = run_tool({ "attribute", "--timeline", detected, "--auto", "--window", "50",
                                     "--min-points", "12" });
        INFO(r.err);
        REQUIRE(r.status == 0);
        CHECK(r.out.starts_with(std::format("Timeline {}: 68 events on 200 reference frames, offsets in "
                                            "seconds on the reference's playlist time\n"
                                            "Ladder report {}: its SHA-256 is the one the document names\n"
                                            "Query: the drops detection finds again, T 6, W 50, N 12 "
                                            "(--auto)\n", detected, report)));
        CHECK(contains(r.out, "\nCurve pair:0: entry 1 ("));
        CHECK(contains(r.out, ")\n  no drop\n\nCurve pair:1: entry 2 ("));
        CHECK(contains(r.out, "Drop 1: reference frames 75-124 (50 frames), 3.000-5.000 s; 50 points, "
                              "baseline "));
        CHECK(contains(r.out, "  scte35   cue_out          entry 2, segment 3 at +0.000 s (+0 frames): line "
                              "14, #EXT-X-CUE-OUT:2.000\n"));
        CHECK(contains(r.out, "  playlist discontinuity    entry 0, segment 3 at +0.000 s (+0 frames): line "
                              "13, discontinuity sequence 1\n"));
        CHECK(contains(r.out, "  scte35   ad_break         entry 2, segment 3 at +0.000 s to +2.000 s (+0 to "
                              "+50 frames): from the cue_out at line 14 to the cue_in at line 21\n"));
        CHECK(r.err.empty());
    }

    SECTION("attribute --format json writes a timeline document of the answer, which reads back")
    {
        const std::string out = (dir / "ad.auto.json").string();
        const ToolRun r = run_tool({ "attribute", "--timeline", detected, "--auto", "--window", "50",
                                     "--min-points", "12", "--format", "json", "--out", out });
        INFO(r.err);
        REQUIRE(r.status == 0);
        CHECK(r.out == std::format("mirage_vmaf attribute: 1 answer written to {}\n", out));
        const Doc d = read_doc(read_text(out));
        INFO(d.err);
        REQUIRE(d.ok);
        CHECK(d.info.command == "attribute");
        REQUIRE(d.info.detection);
        CHECK(d.info.detection->options.window == 50);
        REQUIRE(d.attributions.size() == 1);
        CHECK((d.attributions[0].curve.index == 1 && d.attributions[0].interval.ref_index == 75));
        CHECK(std::get<bool>(option(d.info.options, "auto")->v));

        // A drop of the document, to stdout, as it was detected.
        const ToolRun drop = run_tool({ "attribute", "--timeline", detected, "--drop", "1", "--format", "json" });
        REQUIRE(drop.status == 0);
        const Doc e = read_doc(drop.out);
        INFO(e.err);
        REQUIRE(e.ok);
        REQUIRE(e.attributions.size() == 1);
        CHECK(e.attributions[0].detected);
        CHECK(e.attributions[0].baseline == d.attributions[0].baseline);
        CHECK(e.attributions[0].drop_magnitude == d.attributions[0].drop_magnitude);

        // An interval on every pair's curve: two answers, none detected.
        const ToolRun interval = run_tool({ "attribute", "--timeline", detected, "--interval", "3:5",
                                            "--format", "json" });
        REQUIRE(interval.status == 0);
        const Doc f = read_doc(interval.out);
        REQUIRE(f.ok);
        CHECK_FALSE(f.info.detection);
        REQUIRE(f.attributions.size() == 2);
        for (const Attribution& a : f.attributions) {
            CHECK_FALSE(a.detected);
            CHECK((a.interval.ref_index == 75 && a.interval.ref_end == 125));
        }

        // That answer, queried for a drop, has none: its detection is null
        // because it answers an interval, not because W and N were missing.
        const std::string answer = (dir / "ad.interval.json").string();
        write_text(answer, interval.out);
        const ToolRun again = run_tool({ "attribute", "--timeline", answer, "--drop", "1" });
        CHECK(again.status == 2);
        CHECK(again.err == "mirage_vmaf attribute: --drop 1: the document has 0 detected drops: its "
                           "detection is null, the answer of mirage_vmaf attribute to an interval, which "
                           "detects nothing\n");
    }

    SECTION("attribute --frames and --drop as text")
    {
        const ToolRun frames = run_tool({ "attribute", "--timeline", detected, "--frames", "75-124", "--curve",
                                          "pair:1", "--margin-before", "0.04" });
        INFO(frames.err);
        REQUIRE(frames.status == 0);
        CHECK(contains(frames.out, "Query: reference frames 75-124 (50 frames) (--frames)\n"));
        CHECK(contains(frames.out, "Overlap: half-open on reference frames, margins 0.04 s before and 0 s after\n"));
        CHECK(contains(frames.out, "Interval: reference frames 75-124 (50 frames), 3.000-5.000 s; 50 points, "
                                   "min "));
        CHECK(contains(frames.out, "; events looked for in reference frames 74-124 (51 frames) with the margins\n"));
        CHECK_FALSE(contains(frames.out, "Curve pair:0"));

        const ToolRun drop = run_tool({ "attribute", "--timeline", detected, "--drop", "1", "--curve", "pair:1" });
        REQUIRE(drop.status == 0);
        CHECK(contains(drop.out, "Query: the document's drop 1 on curve pair:1 (--drop)\n"));
        CHECK(contains(drop.out, "Detection: T 6, W 50, N 12; the first 25 points of a curve can't be low\n"));
        CHECK(contains(drop.out, "\nDrop 1: reference frames 75-124 (50 frames)"));
    }

    SECTION("a report whose pairs' numbers skip a failed pair: the document, --curve and the answers take "
            "the report's numbers (D-092 (b))")
    {
        // The ladder's report as if the plan's pair 1 had failed and v180
        // were its pair 2: its complete pairs' pairs[].index 0 and 2, the
        // second pair's first key renumbered in the report's text.
        std::string text = read_text(report);
        const std::string second = ",\n    {\n      \"index\": 1,\n";
        const std::size_t at = text.find(second);
        REQUIRE(at != std::string::npos);
        text.replace(at, second.size(), ",\n    {\n      \"index\": 2,\n");
        const std::string skipped = (dir / "ad.skipped.json").string();
        const std::string doc     = (dir / "ad.skipped.timeline.json").string();
        write_text(skipped, text);
        const ToolRun built = run_tool({ "timeline", "--report", skipped, "--out", doc, "--window", "50",
                                         "--min-points", "12" });
        INFO(built.err);
        REQUIRE(built.status == 0);
        const std::string doc_text = read_text(doc);
        CHECK(contains(doc_text, R"("curves": [{"kind": "pair", "index": 0}, {"kind": "pair", "index": 2}])"));
        CHECK(contains(doc_text, R"("curve": {"kind": "pair", "index": 2})"));
        const Doc d = read_doc(doc_text);
        INFO(d.err);
        REQUIRE(d.ok);
        REQUIRE(d.timeline.variants.size() == 3);
        CHECK(d.timeline.variants[1].pair == 0);
        CHECK(d.timeline.variants[2].pair == 2);
        REQUIRE(d.attributions.size() == 1);
        CHECK(d.attributions[0].curve.index == 1);   // v180's curve, at its place among the pairs

        // --curve takes the report's number; the failed pair's is no curve of
        // the document's, an argument it doesn't fit (exit status 2).
        const ToolRun drop = run_tool({ "attribute", "--timeline", doc, "--drop", "1", "--curve", "pair:2" });
        INFO(drop.err);
        REQUIRE(drop.status == 0);
        CHECK(contains(drop.out, "Query: the document's drop 1 on curve pair:2 (--drop)\n"));
        CHECK(contains(drop.out, "\nCurve pair:2: entry 2 ("));
        CHECK(contains(drop.out, "\nDrop 1: reference frames 75-124 (50 frames)"));
        const ToolRun failed = run_tool({ "attribute", "--timeline", doc, "--frames", "1-2", "--curve", "pair:1" });
        CHECK(failed.status == 2);
        CHECK(failed.err == "mirage_vmaf attribute: --curve pair:1: the document has no curve of pair 1: its "
                            "curves are pair:0 and pair:2\n");

        // --auto on every curve, as text: each curve by its pair's number.
        const ToolRun again = run_tool({ "attribute", "--timeline", doc, "--auto", "--window", "50",
                                         "--min-points", "12" });
        INFO(again.err);
        REQUIRE(again.status == 0);
        CHECK(contains(again.out, "\nCurve pair:0: entry 1 ("));
        CHECK(contains(again.out, ")\n  no drop\n\nCurve pair:2: entry 2 ("));
        CHECK_FALSE(contains(again.out, "Curve pair:1"));

        // And as JSON: the answer names pair 2, its curve the second again.
        const std::string answer = (dir / "ad.skipped.auto.json").string();
        const ToolRun as_json = run_tool({ "attribute", "--timeline", doc, "--auto", "--window", "50",
                                           "--min-points", "12", "--curve", "pair:2", "--format", "json",
                                           "--out", answer });
        INFO(as_json.err);
        REQUIRE(as_json.status == 0);
        const std::string answered = read_text(answer);
        CHECK(contains(answered, R"("curves": [{"kind": "pair", "index": 2}])"));
        const Doc e = read_doc(answered);
        INFO(e.err);
        REQUIRE(e.ok);
        CHECK(e.info.command == "attribute");
        CHECK(std::get<std::string>(option(e.info.options, "curve")->v) == "pair:2");
        REQUIRE(e.attributions.size() == 1);
        CHECK(e.attributions[0].curve.index == 1);
        REQUIRE(e.info.detection);
        REQUIRE(e.info.detection->curves.size() == 1);
        CHECK(e.info.detection->curves[0].index == 1);
    }

    SECTION("a redacted copy of the report is taken by its scores digest; a changed one isn't (D-072)")
    {
        // The eval scripts replace local paths with placeholders before a
        // report is committed (design P-Z): every path of this report is
        // in the media directory.
        std::string text = read_text(report);
        const std::string from = dir.string();
        for (std::size_t at = text.find(from); at != std::string::npos; at = text.find(from, at))
            text.replace(at, from.size(), "<media>");
        REQUIRE(text != read_text(report));
        const std::string redacted = (dir / "ad.redacted.json").string();
        write_text(redacted, text);
        const ToolRun r = run_tool({ "attribute", "--timeline", detected, "--report", redacted, "--drop", "1" });
        INFO(r.err);
        REQUIRE(r.status == 0);
        CHECK(contains(r.err, "mirage_vmaf attribute: warning: " + redacted + ": its bytes aren't the ones "
                              "the document names, but its scores digest is: a redacted copy (D-072)\n"));
        CHECK(contains(r.out, "\nDrop 1: reference frames 75-124"));

        // One score changed: neither hash is the document's.
        std::string changed = read_text(report);
        const std::size_t score = changed.find("\"vmaf\": ");
        REQUIRE(score != std::string::npos);
        changed.insert(score + 8, "1");
        const std::string other = (dir / "ad.changed.json").string();
        write_text(other, changed);
        const ToolRun c = run_tool({ "attribute", "--timeline", detected, "--report", other, "--drop", "1" });
        CHECK(c.status == 1);
        CHECK(contains(c.err, other + " isn't the ladder report the document was built from: neither its "
                              "SHA-256, "));
        CHECK(c.out.empty());
    }

    SECTION("exit statuses: 2 for bad arguments and arguments the document doesn't fit, 1 for inputs "
            "that can't be read")
    {
        const ToolRun bad = run_tool({ "timeline", "--report", report });
        CHECK(bad.status == 2);
        CHECK(bad.err == "mirage_vmaf timeline: missing --out <timeline.json>\n" +
                             timelinecli::timeline_usage().substr(0, timelinecli::timeline_usage().find("\n\n") + 1) +
                             "Run mirage_vmaf timeline --help for details.\n");
        CHECK(run_tool({ "attribute", "--timeline", detected, "--drop", "1", "--window", "5" }).status == 2);
        const ToolRun drop = run_tool({ "attribute", "--timeline", detected, "--drop", "2" });
        CHECK(drop.status == 2);
        CHECK(drop.err == "mirage_vmaf attribute: --drop 2: the document has 1 detected drop\n");
        const ToolRun none = run_tool({ "attribute", "--timeline", events, "--drop", "1" });
        CHECK(none.status == 2);
        CHECK(contains(none.err, "the document has 0 detected drops: its detection is null, W and N weren't "
                                 "given when it was built (D-077)"));
        const ToolRun curve = run_tool({ "attribute", "--timeline", detected, "--frames", "1-2", "--curve", "pair:2" });
        CHECK(curve.status == 2);
        CHECK(curve.err == "mirage_vmaf attribute: --curve pair:2: the document has no curve of pair 2: its curves "
                           "are pair:0 and pair:1\n");
        const ToolRun frames = run_tool({ "attribute", "--timeline", detected, "--frames", "190-200" });
        CHECK(frames.status == 2);
        CHECK(frames.err == "mirage_vmaf attribute: --frames 190-200: the document's axis has 200 reference "
                            "frames, from 0\n");
        // An interval in seconds that holds time past the axis is refused as
        // --frames past its last frame is (D-099 (c) keeps both exit status
        // 2): the ladder's 200 frames at 25 fps end at 8 s on the playlist
        // time, the last starting at 7.96 s.
        const ToolRun late = run_tool({ "attribute", "--timeline", detected, "--interval", "7.6:9" });
        CHECK(late.status == 2);
        CHECK(late.err == "mirage_vmaf attribute: --interval 7.6:9: the document's axis ends at 8 s, where "
                          "its last reference frame, 199, ends\n");
        const ToolRun after = run_tool({ "attribute", "--timeline", detected, "--interval", "7.99:8" });
        CHECK(after.status == 2);
        CHECK(after.err == "mirage_vmaf attribute: --interval 7.99:8: no reference frame of the document's "
                           "axis starts at or after 7.99 s; its last, 199, starts at 7.96 s\n");
        const ToolRun beyond = run_tool({ "attribute", "--timeline", detected, "--interval", "100:200" });
        CHECK(beyond.status == 2);
        CHECK(contains(beyond.err, "--interval 100:200: no reference frame of the document's axis starts at "
                                   "or after 100 s"));
        // Its last frame, to the axis's end, is one the document has.
        const ToolRun last = run_tool({ "attribute", "--timeline", detected, "--interval", "7.96:8", "--curve",
                                        "pair:1" });
        INFO(last.err);
        CHECK(last.status == 0);
        CHECK(contains(last.out, "Query: 7.96 to 8 s on the axis, reference frames 199-199 (1 frame) "
                                 "(--interval)\n"));
        CHECK(contains(last.out, "Interval: reference frames 199-199 (1 frame), 7.960-8.000 s; 1 point, "));

        const std::string missing = (dir / "no-such-report.json").string();
        const std::string out     = (dir / "never.timeline.json").string();
        const ToolRun gone = run_tool({ "timeline", "--report", missing, "--out", out });
        CHECK(gone.status == 1);
        CHECK(contains(gone.err, "mirage_vmaf timeline: cannot read the ladder report " + missing + ": "));
        CHECK_FALSE(fs::exists(out));
        const ToolRun master = run_tool({ "timeline", "--report", lad.master, "--out", out });
        CHECK(master.status == 1);
        CHECK(contains(master.err, lad.master + " isn't a ladder report the timeline reads: not JSON: "));
        CHECK_FALSE(fs::exists(out));
        const ToolRun doc = run_tool({ "attribute", "--timeline", report, "--drop", "1" });
        CHECK(doc.status == 1);
        CHECK(contains(doc.err, report + " isn't a timeline document this reads: not a timeline document"));
    }

    SECTION("--help: each subcommand's, and mirage_vmaf's names both")
    {
        const ToolRun t = run_tool({ "timeline", "--help" });
        CHECK(t.status == 0);
        CHECK(t.out == timelinecli::timeline_usage());
        const ToolRun a = run_tool({ "attribute", "-h" });
        CHECK(a.status == 0);
        CHECK(a.out == timelinecli::attribute_usage());
        const ToolRun m = run_tool({ "--help" });
        CHECK(m.status == 0);
        CHECK(contains(m.out, "\nSubcommands, each with its own --help: mirage_vmaf timeline --report\n"));
        // A first word other than a subcommand's is the scorer's, as before.
        CHECK(run_tool({ "timelines" }).status == 2);
    }
}
