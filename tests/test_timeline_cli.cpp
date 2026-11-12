// mirage_vmaf's timeline and attribute subcommands (src/TimelineCli.h;
// DECISIONS.md D-079): their option tables and config files (the design's
// P-V), a session's options and the decoded messages' --cue-types and
// --bad-crc (D-106 (b), (c)) among them, the seconds and the cue types the
// options take exactly (P-A), the help, the records a document keeps of the
// options, the line timeline prints when it doesn't detect, the refusal of
// a session fetch of local files (D-108 (a)), the text of an answer (the
// design's P-AD) on timelines the test builds by hand, a session's curve's
// and a decoded message's events among them, and on the decoded cues of a
// build, mirage_vmaf's own help, and
// runs of each subcommand as mirage_vmaf runs them (the design's P-T), on a
// synthetic ad ladder (tests/SyntheticLadder.h) scored by mirage_vmaf
// --ladder: their outputs and exit statuses (P-W), intervals and frames past
// the axis, a lone --window, a report whose pairs' numbers skip a failed
// pair with --curve by those numbers (D-092 (b)), a redacted copy of the
// report and one that changed; a scripted session over that ladder (D-068),
// its files fetched from tests/HttpFileServer.h's server of the ladder
// (D-104), or not fetched, the plans and fetches refused, a fetch of local
// files and one of files outside the master's directory among them, a fetch
// stopped by SIGINT or SIGTERM, and attribute on the session's curve; and a
// copy of the ladder whose media playlist carries SCTE-35 messages that
// tests/Scte35TestSections.h builds (D-107), decoded by the build.

#include <catch2/catch_test_macros.hpp>

#include "FileHash.h"
#include "HttpFileServer.h"
#include "Scte35Events.h"
#include "Scte35TestSections.h"
#include "SyntheticLadder.h"
#include "TestTemp.h"
#include "TimelineAxis.h"
#include "TimelineBuild.h"
#include "TimelineCli.h"
#include "TimelineDoc.h"
#include "TimelineModel.h"
#include "TimelineQuery.h"
#include "VMAFCli.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <fcntl.h>
#include <signal.h>
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
// 80 columns wide, as mirage_vmaf's own help (tests/test_vmaf_cli.cpp); the
// help of an option too wide for the help's column, on the line after the
// option's, is its row's.
void check_width(const std::string& text)
{
    const std::string column(33, ' ');   // two spaces, then the option column's 31
    std::size_t from = 0;
    while (from < text.size()) {
        const std::size_t to = std::min(text.find('\n', from), text.size());
        const std::string_view line(text.data() + from, to - from);
        if (!line.starts_with("Usage:") && !line.starts_with("  -") && !line.starts_with(column)) {
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

// A run of the built mirage_vmaf that goes on while the test does something
// else: its process, and the files of the test's temporary directory
// (tests/TestTemp.h) its stdout and stderr go to.
struct Spawned {
    pid_t    pid = -1;
    fs::path out, err;
};

// Starts the built mirage_vmaf with `args`.
Spawned spawn_tool(std::vector<std::string> args)
{
    static int runs = 0;
    ++runs;
    Spawned s;
    s.out = testtemp::path(std::format("timeline_cli_stdout_{}.txt", runs));
    s.err = testtemp::path(std::format("timeline_cli_stderr_{}.txt", runs));
    posix_spawn_file_actions_t files;
    posix_spawn_file_actions_init(&files);
    posix_spawn_file_actions_addopen(&files, STDOUT_FILENO, s.out.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    posix_spawn_file_actions_addopen(&files, STDERR_FILENO, s.err.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    args.insert(args.begin(), MIRAGE_VMAF_PATH);
    std::vector<char*> argv;
    for (std::string& a : args) argv.push_back(a.data());
    argv.push_back(nullptr);
    const int spawned = posix_spawn(&s.pid, argv[0], &files, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&files);
    REQUIRE(spawned == 0);
    return s;
}

// Waits for `s` to exit: what it did.
ToolRun wait_tool(const Spawned& s)
{
    int status = 0;
    while (::waitpid(s.pid, &status, 0) < 0 && errno == EINTR) {}
    ToolRun run;
    run.status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    run.out    = read_text(s.out);
    run.err    = read_text(s.err);
    return run;
}

// Runs the built mirage_vmaf with `args` to its end.
ToolRun run_tool(std::vector<std::string> args) { return wait_tool(spawn_tool(std::move(args))); }

// A session plan of the ladder's (D-105 (a)), design §9.3's: entry 1 (v270)
// for segments 0 to 2, entry 2 (v180) for 3 and 4, the ad, and entry 1 for
// 5 to 7.
const std::string kPlan =
    "{\"schema\": \"mirage-session-plan\", \"schema_version\": 1, \"name\": \"down at the ad\",\n"
    " \"spans\": [{\"entry\": 1, \"first_seq\": 0, \"last_seq\": 2},\n"
    "           {\"entry\": 2, \"first_seq\": 3, \"last_seq\": 4},\n"
    "           {\"entry\": 1, \"first_seq\": 5, \"last_seq\": 7}]}\n";

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

std::size_t count_source(const timeline::Timeline& t, Source source)
{
    return static_cast<std::size_t>(std::count_if(t.events.begin(), t.events.end(),
                                                  [source](const Event& e) { return e.source == source; }));
}

// The ids of the events of one source that an attribution lists, in order.
std::vector<std::string> listed(const timeline::Timeline& t, const Attribution& a, Source source)
{
    std::vector<std::string> out;
    for (const timeline::Found& f : a.by_source[static_cast<std::size_t>(source)])
        out.push_back(t.events[f.event].id);
    return out;
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

TEST_CASE("timelinecli::parse_timeline: a session's options, their default, their config settings, and what "
          "goes with what", "[timeline][timeline_cli]")
{
    // --session alone: the fetch on, its default with a session (design §7),
    // from the report's URLs.
    TimelineArgs a;
    REQUIRE(parse_t({ "--report", "r.json", "--out", "t.json", "--session", "plan.json" }, a).empty());
    CHECK(a.session == "plan.json");
    CHECK_FALSE(a.fetch);
    CHECK(a.fetch_base.empty());
    TimelineArgs b;
    REQUIRE(parse_t({ "--report", "r", "--out", "o", "--session", "p", "--fetch", "on", "--fetch-base",
                      "http://127.0.0.1:8080" }, b).empty());
    CHECK(b.fetch == true);
    CHECK(b.fetch_base == "http://127.0.0.1:8080");
    TimelineArgs c;
    REQUIRE(parse_t({ "--report", "r", "--out", "o", "--session", "p", "--fetch", "off" }, c).empty());
    CHECK(c.fetch == false);
    REQUIRE(parse_t({ "--report", "r", "--out", "o", "--session", "p", "--fetch-base",
                      "https://cdn.test.invalid/ladder/" }, c).empty());
    CHECK(c.fetch_base == "https://cdn.test.invalid/ladder/");

    // Each a setting of the config file, the command line replacing it
    // (D-030); the plan itself a file of its own (D-105 (a)).
    const Files files{ { { "s.conf", "session = from-file.plan.json\nfetch = off\n" },
                         { "base.conf", "session = p\nfetch-base = http://127.0.0.1:9\n" },
                         { "lone.conf", "fetch = on\n" },
                         { "bad.conf", "fetch = maybe\n" },
                         { "local.conf", "session = p\nfetch-base = /srv/ladder\n" } } };
    TimelineArgs d;
    REQUIRE(parse_t({ "--config", "s.conf", "--report", "r", "--out", "o" }, d, files).empty());
    CHECK(d.session == "from-file.plan.json");
    CHECK(d.fetch == false);
    TimelineArgs e;
    REQUIRE(parse_t({ "--config", "s.conf", "--report", "r", "--out", "o", "--fetch", "on", "--session",
                      "line.plan.json" }, e, files).empty());
    CHECK(e.session == "line.plan.json");
    CHECK(e.fetch == true);
    TimelineArgs f;
    REQUIRE(parse_t({ "--config", "base.conf", "--report", "r", "--out", "o" }, f, files).empty());
    CHECK(f.fetch_base == "http://127.0.0.1:9");

    // The fetch's options go with a session, a rule pending the owner
    // (src/TimelineCli.h), and the fetch base with its fetch, whether the
    // command line or the file gives them; a fetch base is an http or https
    // URL, so that a session's network events come from network requests
    // alone (D-108 (a)'s reason): the fetch base's rules are D-111 (d)'s.
    TimelineArgs g;
    g.report = "kept";
    const auto refused = [&g, &files](const std::vector<std::string_view>& argv) { return parse_t(argv, g, files); };
    CHECK(refused({ "--report", "r", "--out", "o", "--fetch", "off" }) ==
          "--fetch goes with --session, which isn't given");
    CHECK(refused({ "--report", "r", "--out", "o", "--fetch-base", "http://h" }) ==
          "--fetch-base goes with --session, which isn't given");
    CHECK(refused({ "--config", "lone.conf", "--report", "r", "--out", "o" }) ==
          "--fetch goes with --session, which isn't given");
    CHECK(refused({ "--report", "r", "--out", "o", "--session", "p", "--fetch", "off", "--fetch-base",
                    "http://h" }) == "--fetch-base goes with --fetch on, not with --fetch off");
    CHECK(refused({ "--config", "base.conf", "--report", "r", "--out", "o", "--fetch", "off" }) ==
          "--fetch-base goes with --fetch on, not with --fetch off");
    CHECK(refused({ "--report", "r", "--out", "o", "--session", "p", "--fetch", "maybe" }) ==
          "--fetch wants on or off, not 'maybe'");
    CHECK(refused({ "--config", "bad.conf", "--report", "r", "--out", "o" }) ==
          "config file bad.conf, line 1: fetch wants on or off, not 'maybe'");
    for (std::string_view base :
         { "/srv/ladder", "file:///srv/ladder", "ftp://h/ladder", "http://", "127.0.0.1:8080" }) {
        INFO(base);
        CHECK(refused({ "--report", "r", "--out", "o", "--session", "p", "--fetch-base", base }) ==
              std::format("--fetch-base wants an http or https URL, a server of the ladder's files such as "
                          "http://127.0.0.1:8080, not '{}'", base));
    }
    CHECK(refused({ "--config", "local.conf", "--report", "r", "--out", "o" }) ==
          "config file local.conf, line 2: fetch-base wants an http or https URL, a server of the ladder's files "
          "such as http://127.0.0.1:8080, not '/srv/ladder'");
    CHECK(refused({ "--report", "r", "--out", "o", "--session" }) == "--session needs a value: --session <plan.json>");
    CHECK(g.report == "kept");   // a failed parse leaves the output alone
}

TEST_CASE("timelinecli::parse_timeline: --cue-types and --bad-crc, their defaults, their config settings, and "
          "what they refuse (D-106 (b), (c))", "[timeline][timeline_cli]")
{
    // Neither given: D-106's defaults for the build, the cue types of (b)
    // and a bad CRC kept (c).
    TimelineArgs a;
    REQUIRE(parse_t({ "--report", "r", "--out", "o" }, a).empty());
    CHECK_FALSE(a.cue_types);
    CHECK_FALSE(a.bad_crc);
    const timeline::MessageOptions d = timelinecli::message_options(a);
    CHECK(d.cue_types == std::vector<std::uint8_t>(std::begin(timeline::kDefaultCueTypes),
                                                   std::end(timeline::kDefaultCueTypes)));
    CHECK(d.bad_crc == timeline::BadCrc::Keep);

    // Given: the set ascending, each type once, whatever the order and the
    // case written; the policy.
    TimelineArgs b;
    REQUIRE(parse_t({ "--report", "r", "--out", "o", "--cue-types", "0x34-0x37,0x22,0X23,0x35", "--bad-crc",
                      "drop" }, b).empty());
    REQUIRE(b.cue_types);
    CHECK(*b.cue_types == std::vector<std::uint8_t>{ 0x22, 0x23, 0x34, 0x35, 0x36, 0x37 });
    CHECK(b.bad_crc == timeline::BadCrc::Drop);
    const timeline::MessageOptions e = timelinecli::message_options(b);
    CHECK(e.cue_types == *b.cue_types);
    CHECK(e.bad_crc == timeline::BadCrc::Drop);
    TimelineArgs c;
    REQUIRE(parse_t({ "--report", "r", "--out", "o", "--bad-crc", "keep" }, c).empty());
    CHECK(c.bad_crc == timeline::BadCrc::Keep);
    CHECK_FALSE(c.cue_types);

    // Each a setting of the config file, the command line replacing it
    // (D-030), a value checked on its line as on the command line.
    const Files files{ { { "m.conf", "cue-types = 0x30-0x31\nbad-crc = drop\n" },
                         { "types.conf", "# cue types\ncue-types = 0x22-0x28\n" },
                         { "syntax.conf", "cue-types = 0x22, 0x23\n" },
                         { "crc.conf", "bad-crc = maybe\n" } } };
    TimelineArgs f;
    REQUIRE(parse_t({ "--config", "m.conf", "--report", "r", "--out", "o" }, f, files).empty());
    CHECK(f.cue_types == std::vector<std::uint8_t>{ 0x30, 0x31 });
    CHECK(f.bad_crc == timeline::BadCrc::Drop);
    TimelineArgs g;
    REQUIRE(parse_t({ "--config", "m.conf", "--report", "r", "--out", "o", "--bad-crc", "keep", "--cue-types",
                      "0x22-0x23" }, g, files).empty());
    CHECK(g.cue_types == std::vector<std::uint8_t>{ 0x22, 0x23 });
    CHECK(g.bad_crc == timeline::BadCrc::Keep);

    // What they refuse, exit status 2 (src/TimelineCli.h): a value that
    // isn't hex ids and ranges, and a type that is neither a Start nor an
    // End type, which makes no cue (check_message_options()).
    TimelineArgs h;
    h.report = "kept";
    const auto refused = [&h, &files](const std::vector<std::string_view>& argv) { return parse_t(argv, h, files); };
    CHECK(refused({ "--report", "r", "--out", "o", "--cue-types", "22" }) ==
          "--cue-types wants segmentation_type_ids in hex, each alone or a range, comma-separated, such as "
          "0x22-0x23,0x30-0x37, not '22': its item 1, '22', is neither a type such as 0x30 nor a range such as "
          "0x30-0x37");
    CHECK(refused({ "--report", "r", "--out", "o", "--cue-types", "0x23-0x22" }) ==
          "--cue-types wants segmentation_type_ids in hex, each alone or a range, comma-separated, such as "
          "0x22-0x23,0x30-0x37, not '0x23-0x22': its item 1, '0x23-0x22', is a range whose first type comes after "
          "its last");
    timeline::MessageOptions reserved;
    reserved.cue_types = { 0x28 };
    CHECK(refused({ "--report", "r", "--out", "o", "--cue-types", "0x22,0x28" }) ==
          "--cue-types takes Start and End types only, not '0x22,0x28': " +
              timeline::check_message_options(reserved));
    CHECK(contains(timeline::check_message_options(reserved), "0x28"));
    CHECK(refused({ "--config", "types.conf", "--report", "r", "--out", "o" }) ==
          "config file types.conf, line 2: cue-types takes Start and End types only, not '0x22-0x28': " +
              timeline::check_message_options(reserved));
    CHECK(refused({ "--config", "syntax.conf", "--report", "r", "--out", "o" }).starts_with(
        "config file syntax.conf, line 1: cue-types wants segmentation_type_ids in hex, each alone or a range, "
        "comma-separated, such as 0x22-0x23,0x30-0x37, not '0x22, 0x23': its item 2, ' 0x23', is neither"));
    CHECK(refused({ "--report", "r", "--out", "o", "--bad-crc", "maybe" }) == "--bad-crc wants keep or drop, not 'maybe'");
    CHECK(refused({ "--config", "crc.conf", "--report", "r", "--out", "o" }) ==
          "config file crc.conf, line 1: bad-crc wants keep or drop, not 'maybe'");
    CHECK(refused({ "--report", "r", "--out", "o", "--cue-types" }) ==
          "--cue-types needs a value: --cue-types <segmentation_type_id ranges>");
    CHECK(h.report == "kept");   // a failed parse leaves the output alone
}

TEST_CASE("timelinecli::parse_cue_types and cue_types_text: segmentation_type_ids in hex, alone or as ranges "
          "(design §7)", "[timeline][timeline_cli]")
{
    std::string err;
    const auto types = [&err](std::string_view text) { return timelinecli::parse_cue_types(text, err); };
    // D-106 (b)'s set, as design §7 writes it, and back.
    const std::vector<std::uint8_t> defaults(std::begin(timeline::kDefaultCueTypes),
                                             std::end(timeline::kDefaultCueTypes));
    CHECK(types("0x22-0x23,0x30-0x37,0x44-0x47") == defaults);
    CHECK(err.empty());
    CHECK(timelinecli::cue_types_text(defaults) == "0x22-0x23,0x30-0x37,0x44-0x47");
    // One or two digits, either case; a range of one; overlaps and repeats
    // once; every id.
    CHECK(types("0x3") == std::vector<std::uint8_t>{ 0x03 });
    CHECK(types("0X30-0x30") == std::vector<std::uint8_t>{ 0x30 });
    CHECK(types("0xfF") == std::vector<std::uint8_t>{ 0xFF });
    CHECK(types("0x31,0x30-0x31,0x30") == std::vector<std::uint8_t>{ 0x30, 0x31 });
    const std::optional<std::vector<std::uint8_t>> all = types("0x00-0xFF");
    REQUIRE(all);
    CHECK(all->size() == 256);
    CHECK(timelinecli::cue_types_text(*all) == "0x00-0xFF");
    CHECK(timelinecli::cue_types_text({ 0x01, 0x02, 0x03, 0x05, 0x10 }) == "0x01-0x03,0x05,0x10");
    CHECK(timelinecli::cue_types_text({}).empty());

    // Anything else, each naming its item.
    for (std::string_view bad : { "", "0x", "0x123", "22", "x22", "0x22,", ",0x22", "0x22-", "-0x22", "0x22 ",
                                  " 0x22", "0x22, 0x23", "0x22-0x23-0x24", "0xGG", "0x-1", "+0x22", "0x22;0x23" }) {
        INFO(bad);
        CHECK_FALSE(types(bad));
        CHECK(err.starts_with("its item "));
    }
    CHECK_FALSE(types("0x30,0x22-0x20"));
    CHECK(err == "its item 2, '0x22-0x20', is a range whose first type comes after its last");
}

TEST_CASE("timelinecli::check_fetch: a session's fetch of files that aren't http or https URLs is refused "
          "without --fetch-base (D-108 (a))", "[timeline][timeline_cli]")
{
    using timeline::FetchedFile;
    std::vector<timeline::Request> files = {
        { .kind = FetchedFile::Master, .url = "http://h.test.invalid/ladder/master.m3u8" },
        { .kind = FetchedFile::MediaPlaylist, .variant = 1, .url = "https://h.test.invalid/ladder/v270/index.m3u8" },
        { .kind = FetchedFile::Segment, .variant = 1, .segment = 0, .url = "http://h.test.invalid/ladder/v270/s0.ts" },
    };
    CHECK(timelinecli::check_fetch(files, "").empty());
    CHECK(timelinecli::check_fetch({}, "").empty());
    files[2].url = "/srv/ladder/v270/s0.ts";
    CHECK(timelinecli::check_fetch(files, "") ==
          "the session's fetch would read segment 0 of variant 1 at /srv/ladder/v270/s0.ts, which isn't an http or "
          "https URL: NetworkLogger times a local file's open and not its read, so a fetch of local files is "
          "refused (D-108 (a)): give --fetch-base <url>, a server of the ladder's files, or --fetch off");
    files[0].url = "master.m3u8";
    CHECK(timelinecli::check_fetch(files, "").starts_with("the session's fetch would read the master at "
                                                          "master.m3u8, which isn't an http or https URL"));
    // With a fetch base the URLs are fetch_url()'s, on its server
    // (src/SessionPlan.h).
    CHECK(timelinecli::check_fetch(files, "http://127.0.0.1:8080").empty());
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

    // A session's curve by its id, the document's sessions[].id (design §7),
    // from the command line or a config file.
    AttributeArgs s;
    REQUIRE(parse_a({ "--timeline", "t", "--drop", "1", "--curve", "session:0" }, s).empty());
    REQUIRE(s.curve);
    CHECK(s.curve->kind == CurveRef::Kind::Session);
    CHECK(s.curve->session == "0");
    AttributeArgs u;
    REQUIRE(parse_a({ "--config", "c.conf", "--timeline", "t", "--auto", "--window", "50", "--min-points", "12" }, u,
                    Files{ { { "c.conf", "curve = session:a b\n" } } })
                .empty());
    REQUIRE(u.curve);
    CHECK(u.curve->session == "a b");
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
    for (std::string_view c : { "1", "pair:", "pair:-1", "session:", "sessions:0", "pair:x", "pair:99999999999" })
        CHECK(contains(refused({ "--timeline", "t", "--drop", "1", "--curve", c }), "--curve wants pair:<k>"));
    CHECK(refused({ "--timeline", "t", "--drop", "1", "--curve", "session:" }) ==
          "--curve wants pair:<k>, the curve of the ladder report's pair k, as its pairs[].index numbers it, or "
          "session:<id>, the curve of the document's session of that id, not 'session:'");
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
    // Design §7's synopsis, the session's options and the decoded messages'
    // in their places.
    const std::string t = timelinecli::timeline_usage();
    CHECK(t.starts_with("Usage: mirage_vmaf timeline --report <report.json> --out <timeline.json> "
                        "[--session <plan.json>] [--fetch on|off] [--fetch-base <url>] "
                        "[--threshold T] [--window W] [--min-points N] [--margin-before S] "
                        "[--margin-after S] [--cue-types <segmentation_type_id ranges>] [--bad-crc keep|drop] "
                        "[--config <file>]\n\n"));
    for (std::string_view opt : { "--report", "--out", "--session", "--fetch", "--fetch-base", "--threshold",
                                  "--window", "--min-points", "--margin-before", "--margin-after", "--cue-types",
                                  "--bad-crc", "--config", "-h, --help" }) {
        INFO(opt);
        CHECK(contains(t, std::format("\n  {} ", opt)));
    }
    // --cue-types with design §7's placeholder is wider than the option
    // column: its help is in the column on the next line; the rows around
    // it as before.
    CHECK(contains(t, "\n  --margin-after S               list the events up to S seconds after a drop's end too "
                      "(default 0)\n"
                      "  --cue-types <segmentation_type_id ranges>\n"
                      "                                 the segmentation types that make a time_signal a cue-out "
                      "or a cue-in (default 0x22-0x23,0x30-0x37,0x44-0x47; see SCTE-35 below)\n"
                      "  --bad-crc keep|drop            keep an SCTE-35 message whose CRC_32 fails, marked, or drop "
                      "it (default keep)\n"
                      "  --config <file>                read settings"));
    CHECK(contains(t, "Detection: "));
    CHECK(contains(t, "\nSession: --session FILE names a scripted session's plan"));
    CHECK(contains(t, "\nSCTE-35: the messages the playlists' tags carry (#EXT-OATCLS-SCTE35, the\n"));
    CHECK(contains(t, " by default 0x22-0x23,0x30-0x37,0x44-0x47\n"));
    CHECK(contains(t, timelinecli::cue_types_text({ std::begin(timeline::kDefaultCueTypes),
                                                    std::end(timeline::kDefaultCueTypes) })));
    CHECK(contains(t, "Config file: "));
    // What the document records of the options, a session's and the decoded
    // messages' where they are given (src/TimelineCli.h).
    CHECK(contains(t, "The document records\nthe file by its path and SHA-256 and every option with its value, but a\n"
                      "session's options only with --session, and --cue-types and --bad-crc only\n"
                      "where they are given."));
    CHECK(contains(t, "Exit status: "));
    // Both of a fetch's refusals with exit status 2: local files without
    // --fetch-base (D-108 (a)), and with it a file outside the master's
    // directory (D-108 (c)).
    CHECK(contains(t, "a fetch of files that aren't\nhttp or https URLs without --fetch-base, or with --fetch-base a "
                      "fetch of a\nfile outside the master's directory, which has no path on its server."));
    check_width(t);

    const std::string a = timelinecli::attribute_usage();
    CHECK(a.starts_with("Usage: mirage_vmaf attribute --timeline <timeline.json> (--interval <t0>:<t1> | "
                        "--frames <first>-<last> | --drop K | --auto) [--report <report.json>] "
                        "[--curve pair:<k>|session:<id>] [--threshold T] [--window W] [--min-points N] "
                        "[--margin-before S] [--margin-after S] [--format text|json] [--out <file>] "
                        "[--config <file>]\n\n"));
    // The option column fits the widest option with two spaces after it.
    CHECK(contains(a, "\n  --curve pair:<k>|session:<id>  the curve of the report's pair k"));
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

    // A session's options come with a session, in the table's order, so that
    // a ladder's record is as it was: the fetch "on" by default (design §7),
    // the fetch base null without one.
    TimelineArgs s;
    REQUIRE(timelinecli::parse_timeline({ "--report", "r.json", "--out", "o.json", "--session", "p.json" }, s)
                .empty());
    const timeline::Payload sp = timelinecli::timeline_options(s);
    names.clear();
    for (const auto& [n, v] : sp) names.push_back(n);
    CHECK(names == std::vector<std::string>{ "report", "out", "session", "fetch", "fetch-base", "threshold",
                                             "window", "min-points", "margin-before", "margin-after" });
    CHECK(std::get<std::string>(option(sp, "session")->v) == "p.json");
    CHECK(std::get<std::string>(option(sp, "fetch")->v) == "on");
    CHECK(std::holds_alternative<std::monostate>(option(sp, "fetch-base")->v));
    TimelineArgs off;
    REQUIRE(timelinecli::parse_timeline({ "--report", "r", "--out", "o", "--session", "p", "--fetch", "off" }, off)
                .empty());
    CHECK(std::get<std::string>(option(timelinecli::timeline_options(off), "fetch")->v) == "off");
    TimelineArgs based;
    REQUIRE(timelinecli::parse_timeline({ "--report", "r", "--out", "o", "--session", "p", "--fetch-base",
                                          "http://127.0.0.1:8080" }, based).empty());
    CHECK(std::get<std::string>(option(timelinecli::timeline_options(based), "fetch-base")->v) ==
          "http://127.0.0.1:8080");

    // A session's curve by its id.
    AttributeArgs b;
    REQUIRE(timelinecli::parse_attribute({ "--timeline", "t.json", "--drop", "1", "--curve", "session:0" }, b)
                .empty());
    CHECK(std::get<std::string>(option(timelinecli::attribute_options(b, "r.json"), "curve")->v) == "session:0");

    // --cue-types and --bad-crc each where it is given, after the margins, so
    // that a document built without them is as it was (a choice pending the
    // owner, Q75; src/TimelineCli.h): the set as ranges of consecutive types,
    // the policy as its word (pending the owner too, Q77). Their place alone:
    // which options come before them without a session is the session part's
    // choice, which the owner kept (D-111 (e)).
    TimelineArgs m;
    REQUIRE(timelinecli::parse_timeline({ "--report", "r", "--out", "o", "--cue-types", "0x31,0x30,0x44-0x47",
                                          "--bad-crc", "drop" }, m).empty());
    const timeline::Payload mp = timelinecli::timeline_options(m);
    names.clear();
    for (const auto& [n, v] : mp) names.push_back(n);
    REQUIRE(names.size() >= 3);
    CHECK(std::vector<std::string>(names.end() - 3, names.end()) ==
          std::vector<std::string>{ "margin-after", "cue-types", "bad-crc" });
    CHECK(std::get<std::string>(option(mp, "cue-types")->v) == "0x30-0x31,0x44-0x47");
    CHECK(std::get<std::string>(option(mp, "bad-crc")->v) == "drop");
    TimelineArgs k;
    REQUIRE(timelinecli::parse_timeline({ "--report", "r", "--out", "o", "--bad-crc", "keep" }, k).empty());
    const timeline::Payload kp = timelinecli::timeline_options(k);
    CHECK_FALSE(option(kp, "cue-types"));
    CHECK(std::get<std::string>(option(kp, "bad-crc")->v) == "keep");
    CHECK(kp.back().first == "bad-crc");
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

TEST_CASE("timelinecli::answer_text: a session's curve, its spans, and its player and network events",
          "[timeline][timeline_cli]")
{
    // A frame grid of 50 frames at 25 fps; the reference, entry 0, and two
    // pairs' playlists, entries 1 and 2; a session that plays entry 2 over
    // segment 0, frames 0 to 24, and entry 1 over segment 1, 25 to 49, where
    // entry 1's pair scores low.
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
    const auto variant = [](int index, int pair, std::size_t entry, const char* url, std::int64_t bandwidth,
                            const char* resolution, int top) {
        timeline::Variant v;
        v.index            = index;
        v.pair             = pair;
        v.entries          = { entry };
        v.url              = url;
        v.bandwidth        = bandwidth;
        v.resolution       = resolution;
        v.rank_from_top    = top;
        v.rank_from_bottom = 4 - top;
        return v;
    };
    t.variants = { variant(0, -1, 0, "http://h/top.m3u8", 900000, "640x360", 1),
                   variant(1, 0, 1, "http://h/low.m3u8", 300000, "320x180", 3),
                   variant(2, 1, 2, "http://h/mid.m3u8", 600000, "480x270", 2) };
    for (int v = 1; v <= 2; ++v) {
        timeline::Curve c;
        c.variant = v;
        for (std::int64_t i = 0; i < 50; ++i) {
            c.ref_index.push_back(i);
            c.vmaf.push_back(v == 1 && i >= 25 ? 40.0 : 90.0);
        }
        t.curves.push_back(std::move(c));
    }
    timeline::Session s;
    s.id                   = "0";
    s.name                 = "down and back";
    s.wallclock_anchor_utc = "2026-11-10T09:30:00.000250Z";
    s.spans = { { .variant = 2, .entry = 2, .first_seq = 0, .last_seq = 0, .ref_index = 0, .ref_end = 25 },
                { .variant = 1, .entry = 1, .first_seq = 1, .last_seq = 1, .ref_index = 25, .ref_end = 50 } };
    t.sessions = { s };

    const auto rung = [](std::int64_t entry, const char* uri, std::int64_t bandwidth, const char* resolution,
                         std::int64_t top) {
        return Value{ Value::Object{ { "entry", Value{ entry } }, { "uri", Value{ std::string(uri) } },
                                     { "bandwidth", Value{ bandwidth } },
                                     { "resolution", Value{ std::string(resolution) } },
                                     { "rank_from_top", Value{ top } }, { "rank_from_bottom", Value{ 4 - top } } } };
    };
    const auto event = [&t](Source source, std::string type, int variant, std::optional<std::int64_t> segment,
                            std::int64_t at, std::optional<std::int64_t> end, timeline::Payload p,
                            std::optional<std::int64_t> wallclock = std::nullopt) {
        Event e;
        e.source       = source;
        e.type         = std::move(type);
        e.variant      = variant;
        e.session      = 0;
        e.segment      = segment;
        e.ref_index    = at;
        e.ref_end      = end;
        e.key          = t.axis.keys[static_cast<std::size_t>(at)];
        e.wallclock_us = wallclock;
        e.payload      = std::move(p);
        t.events.push_back(std::move(e));
    };
    const Value low = rung(1, "low.m3u8", 300000, "320x180", 3), mid = rung(2, "mid.m3u8", 600000, "480x270", 2);
    timeline::Payload span = std::get<Value::Object>(low.v);
    span.emplace_back("first_seq", Value{ std::int64_t{ 1 } });
    span.emplace_back("last_seq", Value{ std::int64_t{ 1 } });
    span.emplace_back("scored", Value{ true });
    event(Source::Player, "variant_span", 1, std::nullopt, 25, 50, span);
    event(Source::Player, "variant_switch", 1, std::nullopt, 25, std::nullopt,
          { { "from", mid }, { "to", low }, { "origin", Value{ std::string("plan") } } });
    const auto request = [](const char* url, double start, double first_byte, double end, Value status,
                            Value text, Value bytes, Value error) {
        return timeline::Payload{ { "url", Value{ std::string(url) } }, { "range", Value{} }, { "status", status },
                                  { "status_text", text }, { "bytes", bytes }, { "start", Value{ start } },
                                  { "first_byte", Value{ first_byte } }, { "end", Value{ end } },
                                  { "duration", Value{ end - start } },
                                  { "failed", Value{ !std::holds_alternative<std::monostate>(error.v) } },
                                  { "error", error }, { "origin", Value{ std::string("fetch") } },
                                  { "chain", Value{ Value::List{} } } };
    };
    event(Source::Network, "segment_request", 1, 1, 25, 50,
          request("http://h/low1.ts", 0.01, 2.309, 2.31, Value{ std::int64_t{ 200 } }, Value{ std::string("OK") },
                  Value{ std::int64_t{ 12032 } }, Value{}),
          10000);
    event(Source::Network, "playlist_request", -1, std::nullopt, 0, std::nullopt,
          request("http://h/master.m3u8", 0.001, 0.002, 0.003, Value{}, Value{}, Value{},
                  Value{ std::string("Connection refused") }),
          1000);
    timeline::assign_ids(t.events);
    t.build_index();

    // A drop of the session's curve over entry 1's span: the session's
    // header with each span, then its events, a request with when it was
    // made, how long it took and to its response's head, its bytes and its
    // status (design §6.4), and a switch from rung to rung by entry, URI and
    // both ranks (D-075).
    const CurveRef session{ .kind = CurveRef::Kind::Session, .index = 0 };
    Attribution a    = timeline::attribute(t, session, { .ref_index = 25, .ref_end = 50 }, {});
    a.detected       = true;
    a.baseline       = 90.0;
    a.drop_magnitude = 50.0;
    a.mean_drop      = 50.0;
    CHECK(timelinecli::answer_text(t, { session }, { a }, { 2 }) ==
          "\n"
          "Curve session:0, the session \"down and back\": entry 2, segment 0, reference frames 0-24 (25 frames); "
          "entry 1, segment 1, reference frames 25-49 (25 frames); scored against entry 0 (http://h/top.m3u8, "
          "640x360, BANDWIDTH 900000; rank 1 from the top, 3 from the bottom)\n"
          "Drop 2: reference frames 25-49 (25 frames), 1.000-2.000 s; 25 points, baseline 90.000, min 40.000, "
          "mean 40.000, drop_magnitude 50.000, mean_drop 50.000\n"
          "  network  segment_request  entry 1, segment 1 at +0.000 s to +1.000 s (+0 to +25 frames): "
          "http://h/low1.ts, requested at 0.010 s: 2.300 s from request to last byte (2.299 s to the response "
          "head), 12032 bytes, 200 OK\n"
          "  player   variant_switch   entry 1 at +0.000 s (+0 frames): entry 2 (mid.m3u8, 480x270, BANDWIDTH "
          "600000; rank 2 from the top, 2 from the bottom) -> entry 1 (low.m3u8, 320x180, BANDWIDTH 300000; rank "
          "3 from the top, 1 from the bottom)\n"
          "  player   variant_span     entry 1 at +0.000 s to +1.000 s (+0 to +25 frames): entry 1 (low.m3u8, "
          "320x180, BANDWIDTH 300000; rank 3 from the top, 1 from the bottom), segment 1\n");

    // The master's request, of no variant, and a failed one; a span that
    // isn't scored, the reference's in top-rung mode (D-105 (b)).
    t.sessions[0].spans[0].scored = false;
    const Attribution b = timeline::attribute(t, session, { .ref_index = 0, .ref_end = 25 }, {});
    const std::string whole = timelinecli::answer_text(t, { session }, { b }, { 0 });
    CHECK(contains(whole, "entry 2, segment 0, reference frames 0-24 (25 frames), not scored; entry 1,"));
    CHECK(contains(whole, "  network  playlist_request the master at +0.000 s (+0 frames): http://h/master.m3u8, "
                          "requested at 0.001 s: 0.002 s from request to last byte (0.001 s to the response head), "
                          "failed: Connection refused\n"));
    // A curve the timeline doesn't have prints nothing.
    CHECK(timelinecli::answer_text(t, { CurveRef{ .kind = CurveRef::Kind::Session, .index = 1 } }, {}, {}).empty());
}

TEST_CASE("timelinecli::answer_text: a decoded SCTE-35 message's splice_point, message and cancel, each in a line "
          "of its own, and a mark where its CRC_32 fails (D-106)", "[timeline][timeline_cli]")
{
    // A frame grid of 50 frames at 25 fps: the reference, entry 0, and a
    // pair's playlist, entry 1, whose segment 1, frames 25 to 49, carries
    // the messages, their payloads in src/Scte35Events.h's layout.
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
    ref.index   = 0;
    ref.entries = { 0 };
    ref.url     = "http://h/top.m3u8";
    low.index   = 1;
    low.pair    = 0;
    low.entries = { 1 };
    low.url     = "http://h/low.m3u8";
    t.variants  = { ref, low };
    timeline::Curve c;
    c.variant = 1;
    for (std::int64_t i = 0; i < 50; ++i) {
        c.ref_index.push_back(i);
        c.vmaf.push_back(90.0);
    }
    t.curves = { c };
    const auto i64 = [](std::int64_t v) { return Value{ v }; };
    const auto str = [](const char* s) { return Value{ std::string(s) }; };
    const auto list = [](std::initializer_list<std::int64_t> l) {
        Value::List out;
        for (std::int64_t v : l) out.push_back(Value{ v });
        return Value{ std::move(out) };
    };
    const auto event = [&t](std::string type, std::int64_t at, timeline::Payload p) {
        Event e;
        e.source    = Source::Scte35;
        e.type      = std::move(type);
        e.variant   = 1;
        e.segment   = 1;
        e.ref_index = at;
        e.key       = t.axis.keys[static_cast<std::size_t>(at)];
        e.payload   = std::move(p);
        t.events.push_back(std::move(e));
    };
    // A message's anchors by its tag's position; a record that a cue holds.
    const auto position = [&](std::int64_t line, std::int64_t at) {
        return Value{ Value::List{ Value{ Value::Object{
            { "anchor", str("position") }, { "segment", i64(1) }, { "line", i64(line) }, { "ref_index", i64(at) } } } } };
    };
    const auto record = [&](std::int64_t line, const char* form, const char* raw,
                            std::initializer_list<const char*> classes, bool crc) {
        Value::List names;
        for (const char* c : classes) names.push_back(str(c));
        return Value{ Value::Object{ { "line", i64(line) }, { "form", str(form) }, { "raw", str(raw) },
                                     { "classes", Value{ std::move(names) } }, { "crc_valid", Value{ crc } } } };
    };
    // A cue_out of a message whose CRC_32 fails, kept (D-106 (c)).
    event("cue_out", 25, { { "line", i64(9) }, { "form", str("OATCLS") }, { "raw", str("#EXT-OATCLS-SCTE35:AAAA") },
                           { "classes", Value{ Value::List{ str("cue_out") } } }, { "crc_valid", Value{ false } },
                           { "anchors", position(9, 25) } });
    // A plain EXT-X-CUE-OUT that took the cue_out part of the OATCLS after
    // it, whose CRC_32 fails, and of one that holds (D-106 (d)), and that
    // OATCLS's cue_in, which joins it at its line (D-109 (e)), Wowza's order
    // (scte35.md §2.4); a signal's cue_out that took two messages whose
    // CRC_32 fails (D-109 (a)); and a cue_out of a section read from the
    // media, without a line.
    event("cue_out", 30, { { "line", i64(17) }, { "form", str("CUE-OUT") }, { "raw", str("#EXT-X-CUE-OUT:2.000") },
                           { "duration", Value{ 2.0 } },
                           { "messages", Value{ Value::List{
                               record(18, "OATCLS", "#EXT-OATCLS-SCTE35:AAAF", { "cue_in", "cue_out" }, false),
                               record(19, "OATCLS", "#EXT-OATCLS-SCTE35:AAAG", { "cue_out" }, true) } } } });
    event("cue_in", 30, { { "line", i64(17) }, { "form", str("OATCLS") }, { "raw", str("#EXT-OATCLS-SCTE35:AAAF") },
                          { "classes", Value{ Value::List{ str("cue_in"), str("cue_out") } } },
                          { "crc_valid", Value{ false } }, { "anchors", position(18, 30) } });
    event("cue_out", 35, { { "line", i64(20) }, { "form", str("DATERANGE SCTE35-OUT") },
                           { "raw", str("#EXT-X-DATERANGE:ID=\"b\",SCTE35-OUT=0xFC") },
                           { "classes", Value{ Value::List{ str("cue_out") } } }, { "crc_valid", Value{ true } },
                           { "anchors", position(20, 35) },
                           { "messages", Value{ Value::List{
                               record(21, "DATERANGE SCTE35-OUT", "#EXT-X-DATERANGE:ID=\"c\",SCTE35-OUT=0xFC",
                                      { "cue_out" }, false),
                               record(22, "DATERANGE SCTE35-OUT", "#EXT-X-DATERANGE:ID=\"d\",SCTE35-OUT=0xFC",
                                      { "cue_out" }, false) } } } });
    event("cue_out", 45, { { "line", Value{} }, { "form", str("TS section") }, { "raw", Value{} },
                           { "classes", Value{ Value::List{ str("cue_out") } } }, { "crc_valid", Value{ true } },
                           { "anchors", Value{ Value::List{ Value{ Value::Object{
                               { "anchor", str("media_pts") }, { "pts", i64(162000) }, { "time_base", str("1/90000") },
                               { "segment", i64(1) }, { "index", i64(45) }, { "ref_index", i64(45) } } } } } } });
    // Its splice time, and another's with a pts_adjustment, of a component,
    // outside its tag's segment, its CRC_32 failing; one of a section read
    // from the media, without a line.
    event("splice_point", 25,
          { { "line", i64(9) }, { "form", str("OATCLS") }, { "command", str("splice_insert") },
            { "pts_time", i64(90000) }, { "pts_adjustment", i64(0) }, { "pts_adjusted", i64(90000) },
            { "component_tag", Value{} }, { "index", i64(25) }, { "in_segment", Value{ true } },
            { "crc_valid", Value{ true } } });
    event("splice_point", 30,
          { { "line", i64(11) }, { "form", str("DATERANGE SCTE35-OUT") }, { "command", str("time_signal") },
            { "pts_time", i64(107000) }, { "pts_adjustment", i64(1000) }, { "pts_adjusted", i64(108000) },
            { "component_tag", i64(3) }, { "index", i64(30) }, { "in_segment", Value{ false } },
            { "crc_valid", Value{ false } } });
    event("splice_point", 35,
          { { "line", Value{} }, { "form", str("TS section") }, { "command", str("splice_insert") },
            { "pts_time", i64(126000) }, { "pts_adjustment", i64(0) }, { "pts_adjusted", i64(126000) },
            { "component_tag", Value{} }, { "index", i64(35) }, { "in_segment", Value{} },
            { "crc_valid", Value{ true } } });
    // A message of two segmentation types, and one of none; a cancel of a
    // splice_insert's event, and one of two segmentation descriptors'.
    event("message", 25, { { "line", i64(12) }, { "form", str("OATCLS") }, { "raw", str("#EXT-OATCLS-SCTE35:AAAB") },
                           { "command", str("time_signal") }, { "segmentation_types", list({ 0x10, 0x20 }) },
                           { "crc_valid", Value{ true } } });
    event("message", 45, { { "line", i64(16) }, { "form", str("OATCLS") }, { "raw", str("#EXT-OATCLS-SCTE35:AAAC") },
                           { "command", str("splice_null") }, { "segmentation_types", list({}) },
                           { "crc_valid", Value{ false } } });
    event("cancel", 40, { { "line", i64(14) }, { "form", str("OATCLS") }, { "raw", str("#EXT-OATCLS-SCTE35:AAAD") },
                          { "command", str("splice_insert") }, { "splice_event_id", i64(9) },
                          { "segmentation_event_ids", list({}) }, { "crc_valid", Value{ true } } });
    event("cancel", 40, { { "line", i64(15) }, { "form", str("OATCLS") }, { "raw", str("#EXT-OATCLS-SCTE35:AAAE") },
                          { "command", str("time_signal") }, { "splice_event_id", Value{} },
                          { "segmentation_event_ids", list({ 42, 43 }) }, { "crc_valid", Value{ true } } });
    timeline::assign_ids(t.events);
    t.build_index();

    const Attribution a = timeline::attribute(t, CurveRef{ .index = 0 }, { .ref_index = 20, .ref_end = 50 }, {});
    const std::string text = timelinecli::answer_text(t, { CurveRef{ .index = 0 } }, { a }, { 0 });
    INFO(text);
    for (std::string_view line :
         { "  scte35   cue_out          entry 1, segment 1 at +0.200 s (+5 frames): line 9, OATCLS, its CRC_32 fails: "
           "kept, crc_valid false (D-106 (c))\n",
           "  scte35   cue_out          entry 1, segment 1 at +0.400 s (+10 frames): line 17, #EXT-X-CUE-OUT:2.000, a "
           "message it holds fails its CRC_32: kept, crc_valid false (D-106 (c))\n",
           "  scte35   cue_in           entry 1, segment 1 at +0.400 s (+10 frames): line 18, OATCLS, joining the cue_out "
           "at line 17, its CRC_32 fails: kept, crc_valid false (D-106 (c))\n",
           "  scte35   cue_out          entry 1, segment 1 at +0.600 s (+15 frames): line 20, DATERANGE SCTE35-OUT, 2 "
           "messages it holds fail their CRC_32: kept, crc_valid false (D-106 (c))\n",
           "  scte35   cue_out          entry 1, segment 1 at +1.000 s (+25 frames): TS section\n",
           "  scte35   splice_point     entry 1, segment 1 at +0.200 s (+5 frames): line 9, OATCLS, splice_insert: "
           "splice time 90000 (90 kHz), frame 25 of its input\n",
           "  scte35   splice_point     entry 1, segment 1 at +0.400 s (+10 frames): line 11, DATERANGE SCTE35-OUT, "
           "time_signal: splice time 108000 (90 kHz), pts_time 107000 + pts_adjustment 1000, component 3, frame 30 "
           "of its input, outside its tag's segment (D-106 (a)), its CRC_32 fails: kept, crc_valid false (D-106 "
           "(c))\n",
           "  scte35   splice_point     entry 1, segment 1 at +0.600 s (+15 frames): TS section, splice_insert: "
           "splice time 126000 (90 kHz), frame 35 of its input\n",
           "  scte35   message          entry 1, segment 1 at +0.200 s (+5 frames): line 12, OATCLS, time_signal, "
           "segmentation types 0x10 (Program Start) and 0x20 (Chapter Start)\n",
           "  scte35   message          entry 1, segment 1 at +1.000 s (+25 frames): line 16, OATCLS, splice_null, "
           "its CRC_32 fails: kept, crc_valid false (D-106 (c))\n",
           "  scte35   cancel           entry 1, segment 1 at +0.800 s (+20 frames): line 14, OATCLS, splice_insert "
           "cancelling splice_event_id 9\n",
           "  scte35   cancel           entry 1, segment 1 at +0.800 s (+20 frames): line 15, OATCLS, time_signal "
           "cancelling segmentation_event_id 42 and segmentation_event_id 43\n" }) {
        INFO(line);
        CHECK(contains(text, line));
    }
    // None printed as a payload's JSON, as a type the text doesn't know is,
    // and none with the line of a carrier other than a tag, which has none.
    CHECK_FALSE(contains(text, "{\"line\""));
    CHECK_FALSE(contains(text, "line null"));

    // The same on the payloads a build makes (src/Scte35Events.h): the ad
    // ladder of tests/Scte35TestSections.h, v180 with an OATCLS after its
    // EXT-X-CUE-OUT, Wowza's order, of a time_signal of a Break End and a
    // Break Start whose CRC_32 fails. The plain cue_out takes its cue_out
    // part (D-106 (d)), and its cue_in joins that cue at its line (D-109
    // (e)): the cue_in by its own tag's line, the cue_out marked for the
    // message it holds.
    scte35test::Section joined = scte35test::time_signal(
        scte35test::ref_pts(75), { scte35test::Segmentation{ .event_id = 3, .type = 0x23 },
                                   scte35test::Segmentation{ .event_id = 4, .type = 0x22 } });
    joined.bad_crc = true;
    const std::string v180 = scte35test::ad_playlist({}, { { 3, scte35test::oatcls(joined) } });
    timeline::BuildInputs in;
    in.scores = scte35test::ad_report();
    for (int v = 0; v < 3; ++v) {
        const std::string playlist = v == 2 ? v180 : scte35test::ad_playlist({});
        timeline::VariantRead r;
        r.playlist = timeline::parse_playlist_events(playlist, scte35test::url_of(v), v);
        REQUIRE(r.playlist->error.empty());
        r.playlist_location = scte35test::url_of(v);
        r.playlist_sha256   = filehash::sha256_hex(playlist);
        r.media             = scte35test::ad_media();
        in.variants.push_back(std::move(r));
    }
    std::string err;
    const timeline::Timeline b = timeline::build(in, err);
    INFO(err);
    REQUIRE(err.empty());
    const Attribution ad = timeline::attribute(b, CurveRef{ .index = 1 }, { .ref_index = 75, .ref_end = 125 }, {});
    const std::string built = timelinecli::answer_text(b, { CurveRef{ .index = 1 } }, { ad }, { 0 });
    INFO(built);
    const std::int64_t cue_out_line = scte35test::line_with(v180, "#EXT-X-CUE-OUT:2.000");
    const std::int64_t oatcls_line  = scte35test::line_with(v180, "#EXT-OATCLS-SCTE35:");
    REQUIRE(cue_out_line < oatcls_line);
    CHECK(contains(built, std::format("  scte35   cue_in           entry 2, segment 3 at +0.000 s (+0 frames): line {}, "
                                      "OATCLS, joining the cue_out at line {}, its CRC_32 fails: kept, crc_valid false "
                                      "(D-106 (c))\n", oatcls_line, cue_out_line)));
    CHECK(contains(built, std::format("  scte35   cue_out          entry 2, segment 3 at +0.000 s (+0 frames): line {}, "
                                      "#EXT-X-CUE-OUT:2.000, a message it holds fails its CRC_32: kept, crc_valid false "
                                      "(D-106 (c))\n", cue_out_line)));
    CHECK_FALSE(contains(built, std::format("line {}, #EXT-OATCLS-SCTE35:", cue_out_line)));
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

    // The session of kPlan over the ladder, its files fetched from a server
    // of the ladder's directory by their paths from the master's (D-108 (c)),
    // W and N given; built once, for the sections that read its document.
    const std::string plan    = (dir / "ad.plan.json").string();
    const std::string session = (dir / "ad.session.timeline.json").string();
    static ToolRun                  session_run;
    static std::string              session_base;    // the server's base, which the URLs fetched start with
    static std::vector<std::string> session_paths;   // the paths it served, in order
    const auto build_session = [&] {
        static bool built = false;
        if (built) return;
        built = true;
        write_text(plan, kPlan);
        HttpFileServer server(lad.dir);
        REQUIRE(server.ok());
        session_base  = server.base();
        session_run   = run_tool({ "timeline", "--report", report, "--out", session, "--session", plan,
                                   "--fetch-base", session_base, "--window", "50", "--min-points", "12" });
        session_paths = server.paths();
    };
    // A rung's file by its path from the master's directory: "v180/ad_000.ts".
    const auto file_of = [&lad](int rung, int segment) {
        return std::format("{}/{}", lad.layout.rungs[static_cast<std::size_t>(rung)].name,
                           lad.layout.segments[static_cast<std::size_t>(segment)].file);
    };

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
        // The options record: every option with its value (P-V); a ladder's
        // has no session option, as before sessions came, nor a session part.
        CHECK(std::holds_alternative<std::monostate>(option(d.info.options, "window")->v));
        CHECK(std::get<std::string>(option(d.info.options, "out")->v) == events);
        CHECK_FALSE(option(d.info.options, "session"));
        CHECK_FALSE(option(d.info.options, "fetch"));
        CHECK_FALSE(d.info.session_plan);
        CHECK(d.timeline.sessions.empty());
        CHECK(contains(text, "  \"session_plan\": null,\n"));
        CHECK(contains(text, "  \"sessions\": [],\n"));
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

    SECTION("timeline --session, its files fetched from a server of the ladder: the session part, its requests "
            "and the session's drop (D-104, D-105)")
    {
        build_session();
        INFO(session_run.err);
        REQUIRE(session_run.status == 0);
        CHECK(contains(session_run.out, std::format("mirage_vmaf timeline: session 0 of {}: 3 spans over reference "
                                                    "frames 0-199\n", plan)));
        CHECK(contains(session_run.out, std::format("mirage_vmaf timeline: session 0's fetch: 11 files from {}, 0 "
                                                    "failed\n", session_base)));
        CHECK(contains(session_run.out, "; 1 drop on 2 curves and 1 on the session's; 0 warnings; document written "
                                        "to " + session + "\n"));

        // The fetch's files, each once, in play order (D-104 (a)): the master,
        // each played variant's media playlist the first time a span plays
        // it, each span's segments, by their paths from the master's
        // directory (D-108 (c)).
        std::vector<std::string> order = { "master.m3u8", lad.layout.rungs[1].uri };
        for (const int k : { 0, 1, 2 }) order.push_back(file_of(1, k));
        order.push_back(lad.layout.rungs[2].uri);
        for (const int k : { 3, 4 }) order.push_back(file_of(2, k));
        for (const int k : { 5, 6, 7 }) order.push_back(file_of(1, k));
        CHECK(session_paths == order);

        const std::string text = read_text(session);
        const Doc d = read_doc(text);
        INFO(d.err);
        REQUIRE(d.ok);
        // The plan by its path and SHA-256 (D-105 (a)); the session with its
        // UTC anchor, RFC 3339 to the microsecond with Z (D-108 (c)), and its
        // spans on the reference's frames; the options recorded.
        REQUIRE(d.info.session_plan);
        CHECK(d.info.session_plan->path == plan);
        CHECK(d.info.session_plan->sha256 == filehash::sha256_hex(kPlan));
        REQUIRE(d.timeline.sessions.size() == 1);
        const timeline::Session& s = d.timeline.sessions[0];
        CHECK(s.id == "0");
        CHECK(s.name == "down at the ad");
        REQUIRE(s.wallclock_anchor_utc);
        CHECK(s.wallclock_anchor_utc->size() == std::string_view("YYYY-MM-DDTHH:MM:SS.ffffffZ").size());
        CHECK(s.wallclock_anchor_utc->ends_with("Z"));
        REQUIRE(s.spans.size() == 3);
        CHECK((s.spans[0].variant == 1 && s.spans[0].ref_index == 0 && s.spans[0].ref_end == 75));
        CHECK((s.spans[1].variant == 2 && s.spans[1].ref_index == 75 && s.spans[1].ref_end == 125));
        CHECK((s.spans[2].variant == 1 && s.spans[2].ref_index == 125 && s.spans[2].ref_end == 200));
        CHECK(std::get<std::string>(option(d.info.options, "session")->v) == plan);
        CHECK(std::get<std::string>(option(d.info.options, "fetch")->v) == "on");
        CHECK(std::get<std::string>(option(d.info.options, "fetch-base")->v) == session_base);

        // Its events: a variant_span per span and a switch at each change of
        // entry (D-108 (b)); a request per file, each of status 200, with a
        // wall clock, in the order the server saw them (D-073).
        CHECK(count_type(d.timeline, "variant_span") == 3);
        CHECK(count_type(d.timeline, "variant_switch") == 2);
        std::vector<std::pair<std::int64_t, std::string>> requests;
        for (const Event& e : d.timeline.events) {
            if (e.source != Source::Network) continue;
            CHECK(e.session == 0);
            REQUIRE(e.wallclock_us);
            const Value* status = option(e.payload, "status");
            REQUIRE(status);
            CHECK(std::get<std::int64_t>(status->v) == 200);
            const std::string url = std::get<std::string>(option(e.payload, "url")->v);
            REQUIRE(url.starts_with(session_base + "/"));
            requests.emplace_back(*e.wallclock_us, url.substr(session_base.size() + 1));
        }
        std::sort(requests.begin(), requests.end());
        std::vector<std::string> by_wallclock;
        for (const auto& [at, path] : requests) by_wallclock.push_back(path);
        CHECK(by_wallclock == order);

        // Detection on every pair's curve and the session's (D-077): v180's
        // ad, its pair's drop, and the session's, which plays v180 there,
        // with its cue_out, its switch from entry 1 to entry 2 and the
        // request of the ad's first segment, all at its first frame.
        REQUIRE(d.info.detection);
        REQUIRE(d.info.detection->curves.size() == 3);
        CHECK(d.info.detection->curves[2].kind == CurveRef::Kind::Session);
        REQUIRE(d.attributions.size() == 2);
        const Attribution& a = d.attributions[1];
        CHECK(a.curve.kind == CurveRef::Kind::Session);
        CHECK((a.interval.ref_index == 75 && a.interval.ref_end == 125));
        CHECK(a.baseline == d.attributions[0].baseline);
        const auto at_start = [&](Source source) {
            std::vector<std::string> out;
            for (const timeline::Found& f : a.by_source[static_cast<std::size_t>(source)])
                if (f.offset_frames == 0) out.push_back(d.timeline.events[f.event].id);
            return out;
        };
        const std::vector<std::string> cues = at_start(Source::Scte35);
        CHECK(std::find(cues.begin(), cues.end(), "scte35/cue_out/2/0") != cues.end());
        CHECK(at_start(Source::Player) == std::vector<std::string>{ "player/variant_switch/2/0",
                                                                    "player/variant_span/2/0" });
        CHECK(at_start(Source::Network) == std::vector<std::string>{ "network/playlist_request/2/0",
                                                                     "network/segment_request/2/0" });
        CHECK(listed(d.timeline, d.attributions[0], Source::Network).empty());   // a pair's curve lists none
    }

    SECTION("timeline --session --fetch off: the session placed without its fetch, no request and no anchor "
            "(D-108 (c))")
    {
        write_text(plan, kPlan);
        const std::string off = (dir / "ad.session.off.timeline.json").string();
        const ToolRun r = run_tool({ "timeline", "--report", report, "--out", off, "--session", plan, "--fetch",
                                     "off", "--window", "50", "--min-points", "12" });
        INFO(r.err);
        REQUIRE(r.status == 0);
        CHECK(contains(r.out, std::format("mirage_vmaf timeline: session 0 of {}: 3 spans over reference frames "
                                          "0-199; its files aren't fetched (--fetch off)\n", plan)));
        CHECK_FALSE(contains(r.out, "'s fetch:"));
        const Doc d = read_doc(read_text(off));
        INFO(d.err);
        REQUIRE(d.ok);
        REQUIRE(d.timeline.sessions.size() == 1);
        CHECK_FALSE(d.timeline.sessions[0].wallclock_anchor_utc);
        CHECK(count_source(d.timeline, Source::Network) == 0);
        CHECK(count_type(d.timeline, "variant_span") == 3);
        CHECK(std::get<std::string>(option(d.info.options, "fetch")->v) == "off");
        CHECK(std::holds_alternative<std::monostate>(option(d.info.options, "fetch-base")->v));
        REQUIRE(d.attributions.size() == 2);   // the session's drop needs no fetch
        CHECK(d.attributions[1].curve.kind == CurveRef::Kind::Session);
    }

    SECTION("timeline --session refuses, with exit status 2 and no document, a fetch of local files without "
            "--fetch-base (D-108 (a)) and a plan it can't take")
    {
        write_text(plan, kPlan);
        const std::string never = (dir / "never.session.timeline.json").string();
        // Refused before the media is read: no line of progress.
        const ToolRun local = run_tool({ "timeline", "--report", report, "--out", never, "--session", plan });
        CHECK(local.status == 2);
        CHECK(local.err == std::format("mirage_vmaf timeline: the session's fetch would read the master at {}, "
                                       "which isn't an http or https URL: NetworkLogger times a local file's open "
                                       "and not its read, so a fetch of local files is refused (D-108 (a)): give "
                                       "--fetch-base <url>, a server of the ladder's files, or --fetch off\n",
                                       lad.master));
        CHECK(local.out.empty());
        CHECK_FALSE(fs::exists(never));

        // A plan that can't be read, isn't a plan this reads, or doesn't fit
        // the ladder: a bad argument, as a config file's errors are, a choice
        // the owner kept (D-111 (c)).
        const std::string missing = (dir / "no-such.plan.json").string();
        const ToolRun gone = run_tool({ "timeline", "--report", report, "--out", never, "--session", missing,
                                        "--fetch", "off" });
        CHECK(gone.status == 2);
        CHECK(contains(gone.err, "mirage_vmaf timeline: cannot read the session plan " + missing + ": "));
        const std::string later = (dir / "later.plan.json").string();
        write_text(later, R"({"schema": "mirage-session-plan", "schema_version": 2, "name": "", "spans": []})");
        const ToolRun wrong = run_tool({ "timeline", "--report", report, "--out", never, "--session", later,
                                         "--fetch", "off" });
        CHECK(wrong.status == 2);
        CHECK(wrong.err == "mirage_vmaf timeline: " + later + " isn't a session plan this reads: the session plan's "
                           "schema_version is 2: this reader reads version 1\n");
        const std::string far = (dir / "far.plan.json").string();
        write_text(far, R"({"schema": "mirage-session-plan", "schema_version": 1, "name": "",)"
                        R"( "spans": [{"entry": 1, "first_seq": 0, "last_seq": 9}]})");
        const ToolRun beyond = run_tool({ "timeline", "--report", report, "--out", never, "--session", far,
                                          "--fetch", "off" });
        CHECK(beyond.status == 2);
        CHECK(contains(beyond.err, "mirage_vmaf timeline: the session plan " + far + " doesn't fit the ladder report " +
                                       report + ": the session plan's spans[0] (entry 1, segments 0 to 9) plays "
                                       "segment 8 of variant 1 ("));
        CHECK_FALSE(fs::exists(never));

        // Bad arguments: the fetch's options without a session, a rule
        // pending the owner (src/TimelineCli.h), and a fetch base that isn't
        // a server's (D-111 (d)).
        const ToolRun lone_fetch = run_tool({ "timeline", "--report", report, "--out", never, "--fetch", "off" });
        CHECK(lone_fetch.status == 2);
        CHECK(lone_fetch.err.starts_with("mirage_vmaf timeline: --fetch goes with --session, which isn't given\n"));
        const ToolRun base = run_tool({ "timeline", "--report", report, "--out", never, "--session", plan,
                                        "--fetch-base", dir.string() });
        CHECK(base.status == 2);
        CHECK(base.err.starts_with("mirage_vmaf timeline: --fetch-base wants an http or https URL"));
        CHECK_FALSE(fs::exists(never));
    }

    SECTION("timeline --session --fetch-base refuses, with exit status 2 and no document, a fetch of a file outside "
            "the master's directory, which has no path on the server (D-108 (c))")
    {
        // A master in a directory beside the ladder's, naming its rungs by
        // ../ad-main-origin/<rung>/index.m3u8, scored as it is: the rungs'
        // media playlists are outside the master's directory once their dot
        // segments are resolved (fetch_url(), src/SessionPlan.h), so a server
        // of that directory has no path for them, and the whole fetch is
        // refused (D-108 (c)) at the first of them, not at their segments,
        // before anything is fetched, with exit status 2 (D-111 (c)). The
        // ladder's own directory, which other test cases of the process read
        // whole, is left as it is.
        write_text(plan, kPlan);
        const fs::path beside = dir / "ad-beside";
        fs::create_directories(beside);
        std::string master = read_text(lad.master);
        for (const synthladder::RungLayout& rung : lad.layout.rungs) {
            const std::size_t at = master.find("\n" + rung.uri + "\n");
            REQUIRE(at != std::string::npos);
            master.insert(at + 1, std::format("../{}/", lad.dir.filename().string()));
        }
        const std::string beside_master = (beside / "master.m3u8").string();
        write_text(beside_master, master);
        const std::string beside_report = (dir / "ad.beside.json").string();
        const ToolRun scored = run_tool({ "--ladder", beside_master, "--out", beside_report });
        INFO(scored.err);
        REQUIRE(scored.status == 0);

        HttpFileServer server(beside);
        REQUIRE(server.ok());
        const std::string never = (dir / "never.beside.timeline.json").string();
        const ToolRun r = run_tool({ "timeline", "--report", beside_report, "--out", never, "--session", plan,
                                     "--fetch-base", server.base() });
        INFO(r.err);
        CHECK(r.status == 2);
        CHECK(r.err.starts_with(std::format("mirage_vmaf timeline: --fetch-base {}: ", server.base())));
        CHECK(contains(r.err, std::format(" isn't under the master's directory {}/, so it has no path to fetch from "
                                          "{} (design §5.7)\n",
                                          beside.string(), server.base())));
        CHECK_FALSE(fs::exists(never));
        CHECK(server.paths().empty());   // refused before anything is fetched
    }

    SECTION("SIGINT or SIGTERM while the session's fetch waits for a file stops the build: exit status 1 and no "
            "document")
    {
        // v180's first ad segment, the fetch's seventh file, held without an
        // answer, so that the fetch waits there for the signal. Either signal
        // stops the build as it stops mirage_vmaf --ladder (D-047), with the
        // exit status --ladder gives a cancelled run, and no document, a
        // choice the owner kept (D-111 (h)).
        write_text(plan, kPlan);
        const std::string held = file_of(2, 3);
        for (const int sig : { SIGINT, SIGTERM }) {
            INFO((sig == SIGINT ? "SIGINT" : "SIGTERM"));
            HttpFileServer server(lad.dir, HttpFileRule{ .path = held, .hold_first = 1 });
            REQUIRE(server.ok());
            const std::string stopped = (dir / "ad.stopped.timeline.json").string();
            const Spawned p = spawn_tool({ "timeline", "--report", report, "--out", stopped, "--session", plan,
                                           "--fetch-base", server.base() });
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            bool       waits = false;
            while (!(waits = server.requests(held) > 0) && std::chrono::steady_clock::now() < until)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ::kill(p.pid, waits ? sig : SIGKILL);
            const ToolRun r = wait_tool(p);
            INFO(r.err);
            REQUIRE(waits);
            CHECK(r.status == 1);
            CHECK(contains(r.err, "mirage_vmaf timeline: the session's fetch didn't finish: a stop was requested: "
                                  "the fetch ended at its file 7 of 11\n"));
            CHECK_FALSE(fs::exists(stopped));
            CHECK(server.paths().size() == 7);   // nothing after the file it stopped in
        }
    }

    SECTION("attribute on a session's document: the session's curve by --curve session:0, its drop as text and "
            "as JSON, every curve without --curve, and a session the document hasn't got")
    {
        build_session();
        REQUIRE(session_run.status == 0);
        const ToolRun t = run_tool({ "attribute", "--timeline", session, "--auto", "--window", "50", "--min-points",
                                     "12", "--curve", "session:0" });
        INFO(t.err);
        REQUIRE(t.status == 0);
        CHECK(contains(t.out, "Query: the drops detection finds again, T 6, W 50, N 12 (--auto)\n"));
        CHECK(contains(t.out, "\nCurve session:0, the session \"down at the ad\": entry 1, segments 0-2, reference "
                              "frames 0-74 (75 frames); entry 2, segments 3-4, reference frames 75-124 (50 frames); "
                              "entry 1, segments 5-7, reference frames 125-199 (75 frames); scored against entry 0 ("));
        CHECK(contains(t.out, "\nDrop 1: reference frames 75-124 (50 frames), 3.000-5.000 s; 50 points, baseline "));
        CHECK(contains(t.out, "\n  player   variant_switch   entry 2 at +0.000 s (+0 frames): entry 1 "
                              "(v270/index.m3u8, 480x270, BANDWIDTH "));
        CHECK(contains(t.out, std::format("\n  network  segment_request  entry 2, segment 3 at +0.000 s to +1.000 s "
                                          "(+0 to +25 frames): {}/{}, requested at ", session_base, file_of(2, 3))));
        CHECK(contains(t.out, " 200 OK\n"));
        // A span's line gives its segments as the header does, first-last.
        const std::size_t span = t.out.find("\n  player   variant_span     entry 2 at +0.000 s to +2.000 s (+0 to +50 "
                                            "frames): entry 2 (v180/index.m3u8, ");
        REQUIRE(span != std::string::npos);
        CHECK(std::string_view(t.out).substr(span, t.out.find('\n', span + 1) - span).ends_with("), segments 3-4"));
        CHECK_FALSE(contains(t.out, "Curve pair:"));

        // The document's drops, the pairs' then the session's: its second.
        const ToolRun drop = run_tool({ "attribute", "--timeline", session, "--drop", "2" });
        INFO(drop.err);
        REQUIRE(drop.status == 0);
        CHECK(contains(drop.out, "Query: the document's drop 2 (--drop)\n"));
        CHECK(contains(drop.out, "\nCurve session:0, the session \"down at the ad\": "));
        CHECK(contains(drop.out, "\nDrop 2: reference frames 75-124 (50 frames)"));
        const ToolRun first = run_tool({ "attribute", "--timeline", session, "--drop", "1", "--curve", "session:0" });
        REQUIRE(first.status == 0);
        CHECK(contains(first.out, "Query: the document's drop 1 on curve session:0 (--drop)\n"));

        // As JSON: a document of the same schema, the session part the
        // queried document's, which reads back.
        const std::string answer = (dir / "ad.session.auto.json").string();
        const ToolRun json = run_tool({ "attribute", "--timeline", session, "--auto", "--window", "50", "--min-points",
                                        "12", "--curve", "session:0", "--format", "json", "--out", answer });
        INFO(json.err);
        REQUIRE(json.status == 0);
        const Doc a = read_doc(read_text(answer));
        INFO(a.err);
        REQUIRE(a.ok);
        CHECK(a.info.command == "attribute");
        REQUIRE(a.info.session_plan);
        CHECK(a.info.session_plan->path == plan);
        CHECK(a.timeline.sessions.size() == 1);
        REQUIRE(a.attributions.size() == 1);
        CHECK(a.attributions[0].curve.kind == CurveRef::Kind::Session);
        REQUIRE(a.info.detection);
        REQUIRE(a.info.detection->curves.size() == 1);
        CHECK(a.info.detection->curves[0].kind == CurveRef::Kind::Session);
        CHECK(std::get<std::string>(option(a.info.options, "curve")->v) == "session:0");

        // Without --curve every curve, each pair's, then the session's.
        const ToolRun every = run_tool({ "attribute", "--timeline", session, "--frames", "75-124" });
        INFO(every.err);
        REQUIRE(every.status == 0);
        const std::size_t p0 = every.out.find("\nCurve pair:0: "), p1 = every.out.find("\nCurve pair:1: ");
        const std::size_t s0 = every.out.find("\nCurve session:0, ");
        REQUIRE(s0 != std::string::npos);
        CHECK((p0 < p1 && p1 < s0));

        // A session the document hasn't got: an argument it doesn't fit.
        const ToolRun other = run_tool({ "attribute", "--timeline", session, "--drop", "1", "--curve", "session:1" });
        CHECK(other.status == 2);
        CHECK(other.err == "mirage_vmaf attribute: --curve session:1: the document has no session 1: its session is "
                           "session:0\n");
        const ToolRun none = run_tool({ "attribute", "--timeline", detected, "--drop", "1", "--curve", "session:0" });
        CHECK(none.status == 2);
        CHECK(none.err == "mirage_vmaf attribute: --curve session:0: the document has no session 0: it has none\n");
    }

    SECTION("timeline on a ladder whose media playlist carries SCTE-35 messages: one merged into its cue_out, "
            "their splice points, one whose CRC_32 fails kept or dropped, --cue-types recorded (D-106)")
    {
        // v180's playlist again, in a directory beside the ladder's, with
        // its segments copied beside it, and two OATCLS tags built from
        // field values (D-107): after its EXT-X-CUE-OUT, a splice_insert out
        // at the ad's first frame that declares the break's 2 s; before
        // segment 1, a time_signal of a Program Start, which no cue type is,
        // whose CRC_32 fails. The master there names the ladder's own v360
        // and v270 by ../<the ladder's directory>/, and the copy, and is
        // scored as it is. The ladder's own directory, which other test cases
        // of the process read whole, is left as it is.
        const fs::path beside = dir / "ad-message";
        const synthladder::RungLayout& v180 = lad.layout.rungs[2];
        fs::create_directories(beside / v180.name);
        for (const synthladder::SegmentLayout& s : lad.layout.segments)
            fs::copy_file(lad.dir / v180.name / s.file, beside / v180.name / s.file,
                          fs::copy_options::overwrite_existing);
        const std::int64_t ad_pts = lad.layout.segments[3].pts_first;
        const scte35test::Section out =
            scte35test::splice_insert({ .event_id = 1, .pts = ad_pts, .duration = std::pair{ true, std::int64_t{ 180000 } } });
        scte35test::Section bad = scte35test::time_signal(lad.layout.segments[1].pts_first,
                                                          { scte35test::Segmentation{ .event_id = 2, .type = 0x10 } });
        bad.bad_crc = true;
        std::string playlist = read_text(lad.playlist(2));
        const std::string cue_out = "#EXT-X-CUE-OUT:2.000\n", first = lad.layout.segments[0].file + "\n";
        REQUIRE(playlist.find(cue_out) != std::string::npos);
        playlist.insert(playlist.find(cue_out) + cue_out.size(), scte35test::oatcls(out));
        REQUIRE(playlist.find(first) != std::string::npos);
        playlist.insert(playlist.find(first) + first.size(), scte35test::oatcls(bad));
        write_text(beside / v180.uri, playlist);
        const std::int64_t out_line = scte35test::line_with(playlist, scte35test::base64(scte35test::section(out)));
        const std::int64_t bad_line = scte35test::line_with(playlist, scte35test::base64(scte35test::section(bad)));
        std::string master = read_text(lad.master);
        for (const int rung : { 0, 1 }) {
            const std::string& uri = lad.layout.rungs[static_cast<std::size_t>(rung)].uri;
            const std::size_t  at  = master.find("\n" + uri + "\n");
            REQUIRE(at != std::string::npos);
            master.insert(at + 1, std::format("../{}/", lad.dir.filename().string()));
        }
        const std::string beside_master = (beside / "master.m3u8").string();
        write_text(beside_master, master);
        const std::string beside_report = (dir / "ad.message.json").string();
        const ToolRun scored = run_tool({ "--ladder", beside_master, "--out", beside_report });
        INFO(scored.err);
        REQUIRE(scored.status == 0);

        // The build with neither option: the messages decoded and placed.
        const std::string doc = (dir / "ad.message.timeline.json").string();
        const ToolRun r = run_tool({ "timeline", "--report", beside_report, "--out", doc });
        INFO(r.err);
        REQUIRE(r.status == 0);
        const Doc d = read_doc(read_text(doc));
        INFO(d.err);
        REQUIRE(d.ok);
        REQUIRE(d.timeline.variants.size() == 3);
        CHECK(d.timeline.variants[2].url.ends_with("ad-message/" + v180.uri));
        // v180's one cue_out, the plain one, its message's record in its
        // payload (D-106 (d)); its splice points on its own frames, the ad's
        // first and segment 1's first (D-106 (a)); the kept message of the
        // bad CRC_32 (D-106 (c)), whose warning is the build's one.
        std::vector<const Event*> cues, points, messages;
        for (const Event& e : d.timeline.events) {
            if (e.variant != 2) continue;
            if (e.type == "cue_out") cues.push_back(&e);
            if (e.type == "splice_point") points.push_back(&e);
            if (e.type == "message") messages.push_back(&e);
        }
        REQUIRE(cues.size() == 1);
        CHECK(cues[0]->ref_index == 75);
        const Value* records = option(cues[0]->payload, "messages");
        REQUIRE(records);
        REQUIRE(std::get<Value::List>(records->v).size() == 1);
        const timeline::Payload& record = std::get<Value::Object>(std::get<Value::List>(records->v)[0].v);
        CHECK(std::get<std::int64_t>(option(record, "line")->v) == out_line);
        CHECK(std::get<std::string>(option(record, "form")->v) == "OATCLS");
        CHECK(std::get<bool>(option(record, "crc_valid")->v));
        CHECK(std::get<std::int64_t>(option(cues[0]->payload, "break_duration_ticks")->v) == 180000);
        REQUIRE(points.size() == 2);
        CHECK((points[0]->ref_index == 25 && points[1]->ref_index == 75));
        CHECK(std::get<std::int64_t>(option(points[1]->payload, "pts_adjusted")->v) == ad_pts);
        CHECK(std::get<std::int64_t>(option(points[1]->payload, "index")->v) == 75);
        CHECK_FALSE(std::get<bool>(option(points[0]->payload, "crc_valid")->v));
        REQUIRE(messages.size() == 1);
        CHECK(messages[0]->ref_index == 25);
        CHECK_FALSE(std::get<bool>(option(messages[0]->payload, "crc_valid")->v));
        REQUIRE(d.timeline.warnings.size() == 1);
        CHECK(contains(d.timeline.warnings[0], std::format("the OATCLS at line {}: its CRC_32 is 0x", bad_line)));
        CHECK(d.timeline.warnings[0].ends_with(": it is kept, marked crc_valid false (D-106 (c))"));
        CHECK(contains(r.err, "mirage_vmaf timeline: warning: " + d.timeline.warnings[0] + "\n"));
        // The ladder's other events as before: each rung's plain cues and
        // break, no message of the other rungs.
        CHECK(count_type(d.timeline, "cue_out") == 3);
        CHECK(count_type(d.timeline, "ad_break") == 3);
        CHECK(count_type(d.timeline, "splice_point") == 2);
        for (const Event& e : d.timeline.events)
            if (e.type == "ad_break") CHECK((e.ref_index == 75 && e.ref_end == 125));
        // Neither option is recorded, as they weren't given.
        CHECK_FALSE(option(d.info.options, "cue-types"));
        CHECK_FALSE(option(d.info.options, "bad-crc"));

        // attribute's line of the splice point, on v180's curve.
        const ToolRun answer = run_tool({ "attribute", "--timeline", doc, "--frames", "75-124", "--curve", "pair:1" });
        INFO(answer.err);
        REQUIRE(answer.status == 0);
        CHECK(contains(answer.out, std::format("\n  scte35   splice_point     entry 2, segment 3 at +0.000 s (+0 frames): "
                                               "line {}, OATCLS, splice_insert: splice time {} (90 kHz), frame 75 of "
                                               "its input\n", out_line, ad_pts)));

        // With --bad-crc drop and --cue-types: the message of the bad CRC_32
        // dropped, with its warning; the other as before; both recorded.
        const std::string dropped = (dir / "ad.message.drop.timeline.json").string();
        const ToolRun s = run_tool({ "timeline", "--report", beside_report, "--out", dropped, "--bad-crc", "drop",
                                     "--cue-types", "0x30-0x37" });
        INFO(s.err);
        REQUIRE(s.status == 0);
        const Doc e = read_doc(read_text(dropped));
        INFO(e.err);
        REQUIRE(e.ok);
        CHECK(count_type(e.timeline, "message") == 0);
        CHECK(count_type(e.timeline, "splice_point") == 1);
        REQUIRE(e.timeline.warnings.size() == 1);
        CHECK(e.timeline.warnings[0].ends_with(": it is dropped, as the bad-CRC policy drop says (D-106 (c))"));
        CHECK(std::get<std::string>(option(e.info.options, "cue-types")->v) == "0x30-0x37");
        CHECK(std::get<std::string>(option(e.info.options, "bad-crc")->v) == "drop");

        // A cue type that is neither a Start nor an End type: a bad
        // argument, exit status 2, no document.
        const std::string never = (dir / "never.message.timeline.json").string();
        const ToolRun refused = run_tool({ "timeline", "--report", beside_report, "--out", never, "--cue-types",
                                           "0x01" });
        CHECK(refused.status == 2);
        CHECK(refused.err.starts_with("mirage_vmaf timeline: --cue-types takes Start and End types only, not '0x01': "));
        CHECK_FALSE(fs::exists(never));
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
