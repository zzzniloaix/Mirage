#include <catch2/catch_test_macros.hpp>

#include "TestTemp.h"
#include "VMAFCli.h"

#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

// Parses `argv`; returns the error text ("" on success).
std::string run(std::vector<std::string_view> argv, vmafcli::Args& out)
{
    return vmafcli::parse(argv, out);
}

// Parses `argv` with config files read from `files` (path -> contents) rather
// than from disk; a path not in `files` can't be read. `loads` counts reads.
std::string run(std::vector<std::string_view> argv, vmafcli::Args& out,
                const std::map<std::string, std::string>& files, int* loads = nullptr)
{
    return vmafcli::parse(argv, out, [&](const std::string& path) {
        if (loads) ++*loads;
        const auto it = files.find(path);
        return it == files.end() ? vmafcli::ConfigFile{ {}, "No such file or directory" }
                                 : vmafcli::ConfigFile{ it->second, {} };
    });
}

bool contains(const std::string& s, std::string_view part)
{
    return s.find(part) != std::string::npos;
}

} // namespace

TEST_CASE("vmafcli::parse: required options, in any order", "[vmafcli]")
{
    vmafcli::Args a;
    REQUIRE(run({ "--out", "r.json", "--dis", "https://h/d.m3u8", "--ref", "ref.mp4" }, a).empty());
    CHECK(a.ref == "ref.mp4");
    CHECK(a.dis == "https://h/d.m3u8");
    CHECK(a.out == "r.json");
    CHECK(a.frames == 0);   // no limit
    CHECK_FALSE(a.help);
}

TEST_CASE("vmafcli::parse: --frames", "[vmafcli]")
{
    vmafcli::Args a;
    REQUIRE(run({ "--ref", "a", "--dis", "b", "--out", "c", "--frames", "30" }, a).empty());
    CHECK(a.frames == 30);
    REQUIRE(run({ "--ref", "a", "--dis", "b", "--out", "c", "--frames", "4294967295" }, a).empty());
    CHECK(a.frames == 4294967295u);

    for (std::string_view bad : { "0", "-1", "+5", "3x", " 7", "abc", "4294967296", "1e3" }) {
        INFO(bad);
        vmafcli::Args b;
        const std::string err = run({ "--ref", "a", "--dis", "b", "--out", "c", "--frames", bad }, b);
        CHECK(contains(err, "--frames"));
    }
}

TEST_CASE("vmafcli::parse: --scaler", "[vmafcli]")
{
    vmafcli::Args a;
    REQUIRE(run({ "--ref", "a", "--dis", "b", "--out", "c" }, a).empty());
    CHECK(a.scaler == VMAFScaler::Bicubic);    // the analyzer's scaler unless told otherwise (D-024)
    CHECK(a.scaler == VMAFOptions{}.scaler);
    for (const auto& [name, want] : { std::pair{ "bilinear", VMAFScaler::Bilinear },
                                      std::pair{ "bicubic", VMAFScaler::Bicubic },
                                      std::pair{ "lanczos", VMAFScaler::Lanczos } }) {
        INFO(name);
        vmafcli::Args b;
        REQUIRE(run({ "--ref", "a", "--dis", "b", "--out", "c", "--scaler", name }, b).empty());
        CHECK(b.scaler == want);
        CHECK(vmaf_scaler_name(want) == name);
    }
    for (std::string_view bad : { "Bicubic", "spline", "bicubic+accurate_rnd", "neighbor" }) {
        INFO(bad);
        vmafcli::Args b;
        CHECK(contains(run({ "--ref", "a", "--dis", "b", "--out", "c", "--scaler", bad }, b),
                       "--scaler wants bilinear, bicubic or lanczos"));
    }
    vmafcli::Args c;
    CHECK(contains(run({ "--ref", "a", "--dis", "b", "--out", "c", "--scaler", "" }, c),
                   "--scaler needs a non-empty value"));
}

TEST_CASE("vmafcli::parse: --model and --model-file", "[vmafcli]")
{
    vmafcli::Args a;
    REQUIRE(run({ "--ref", "a", "--dis", "b", "--out", "c" }, a).empty());
    CHECK(a.model.version == "vmaf_v0.6.1");   // the analyzer's model unless told otherwise
    CHECK(a.model.path.empty());

    vmafcli::Args b;
    REQUIRE(run({ "--ref", "a", "--dis", "b", "--out", "c", "--model", "vmaf_v0.6.1neg" }, b).empty());
    CHECK(b.model.version == "vmaf_v0.6.1neg");
    CHECK(b.model.path.empty());

    vmafcli::Args c;
    REQUIRE(run({ "--model-file", "models/custom v2.json", "--ref", "a", "--dis", "b", "--out", "c" },
                c).empty());
    CHECK(c.model.path == "models/custom v2.json");

    vmafcli::Args d;
    CHECK(contains(run({ "--ref", "a", "--dis", "b", "--out", "c", "--model", "vmaf_v0.6.1",
                         "--model-file", "m.json" }, d),
                   "--model and --model-file can't be given together"));
    CHECK(contains(run({ "--ref", "a", "--dis", "b", "--out", "c", "--model-file", "" }, d),
                   "--model-file needs a non-empty value"));
    CHECK(contains(run({ "--ref", "a", "--dis", "b", "--out", "c", "--model" }, d),
                   "--model needs a value"));
}

TEST_CASE("vmafcli::parse: -h / --help wins over everything else", "[vmafcli]")
{
    vmafcli::Args a;
    REQUIRE(run({ "--help" }, a).empty());
    CHECK(a.help);
    vmafcli::Args b;
    REQUIRE(run({ "--bogus", "--ref", "x", "-h" }, b).empty());
    CHECK(b.help);
    CHECK(b.ref.empty());
}

TEST_CASE("vmafcli::parse: errors name what is wrong", "[vmafcli]")
{
    vmafcli::Args a;
    CHECK(contains(run({}, a), "missing --ref"));
    CHECK(contains(run({ "--ref", "a", "--dis", "b" }, a), "missing --out"));
    CHECK(contains(run({ "--ref", "a", "--out", "c" }, a), "missing --dis"));
    CHECK(contains(run({ "--ref", "a", "--dis", "b", "--out", "c", "extra" }, a),
                   "unknown argument 'extra'"));
    CHECK(contains(run({ "--ref", "a", "--dis", "b", "--out", "c", "--bogus", "m" }, a),
                   "unknown argument '--bogus'"));
    CHECK(contains(run({ "--ref", "a", "--ref", "b", "--dis", "b", "--out", "c" }, a),
                   "--ref is given more than once"));
    CHECK(contains(run({ "--ref", "a", "--dis", "b", "--out" }, a), "--out needs a value"));
    CHECK(contains(run({ "--ref", "--dis", "b", "--out", "c" }, a), "--ref needs a value"));
    CHECK(contains(run({ "--ref", "", "--dis", "b", "--out", "c" }, a), "--ref needs a non-empty value"));
}

TEST_CASE("vmafcli::parse: a failed parse leaves the output alone", "[vmafcli]")
{
    vmafcli::Args a;
    a.ref = "kept";
    CHECK_FALSE(run({ "--ref", "new", "--dis", "b" }, a).empty());
    CHECK(a.ref == "kept");
}

TEST_CASE("vmafcli::usage: lists every option", "[vmafcli]")
{
    const std::string u = vmafcli::usage();
    CHECK(u.starts_with("Usage: mirage_vmaf --ref <url|path> --dis <url|path> --out <report.json> "
                        "[--frames N] [--scaler NAME] [--model NAME] [--model-file <model.json>] "
                        "[--config <file>]\n"));
    for (std::string_view opt : { "--ref", "--dis", "--out", "--frames", "--scaler", "--model",
                                  "--model-file", "--config", "-h, --help" }) {
        INFO(opt);
        CHECK(contains(u, opt));
    }
    CHECK(contains(u, "Exit status"));
    CHECK(contains(u, "Config file: --config FILE"));
}

// ── --config (D-030) ──────────────────────────────────────────────────────────

TEST_CASE("vmafcli::parse: --config sets what the command line doesn't", "[vmafcli]")
{
    const std::map<std::string, std::string> files = {
        { "run.cfg", "ref = r.mp4\ndis = d.mp4\nout = o.json\nframes = 10\nscaler = lanczos\n" },
    };
    vmafcli::Args a;
    REQUIRE(run({ "--config", "run.cfg" }, a, files).empty());
    CHECK(a.config == "run.cfg");
    CHECK(a.ref == "r.mp4");
    CHECK(a.dis == "d.mp4");
    CHECK(a.out == "o.json");
    CHECK(a.frames == 10);
    CHECK(a.scaler == VMAFScaler::Lanczos);
    CHECK(a.model.version == "vmaf_v0.6.1");   // not in the file: the default
    CHECK(a.model.path.empty());

    // The command line replaces the file's settings, wherever --config stands.
    vmafcli::Args b;
    REQUIRE(run({ "--scaler", "bilinear", "--config", "run.cfg", "--frames", "3", "--out", "x.json" },
                b, files).empty());
    CHECK(b.scaler == VMAFScaler::Bilinear);
    CHECK(b.frames == 3);
    CHECK(b.out == "x.json");
    CHECK(b.ref == "r.mp4");   // the file's, which the command line doesn't give

    // No config file: nothing is read.
    int loads = 0;
    vmafcli::Args c;
    REQUIRE(run({ "--ref", "a", "--dis", "b", "--out", "c" }, c, files, &loads).empty());
    CHECK(loads == 0);
    CHECK(c.config.empty());
}

TEST_CASE("vmafcli::parse: --model and --model-file are one setting for --config", "[vmafcli]")
{
    const std::map<std::string, std::string> files = {
        { "file.cfg", "ref = r\ndis = d\nout = o\nmodel-file = models/m.json\n" },
        { "builtin.cfg", "ref = r\ndis = d\nout = o\nmodel = vmaf_v0.6.1neg\n" },
        { "both.cfg", "ref = r\nmodel = vmaf_v0.6.1neg\n\nmodel-file = m.json\n" },
    };
    vmafcli::Args a;
    REQUIRE(run({ "--config", "file.cfg", "--model", "vmaf_4k_v0.6.1" }, a, files).empty());
    CHECK(a.model.version == "vmaf_4k_v0.6.1");
    CHECK(a.model.path.empty());   // the file's model file is replaced

    vmafcli::Args b;
    REQUIRE(run({ "--config", "builtin.cfg", "--model-file", "x.json" }, b, files).empty());
    CHECK(b.model.path == "x.json");
    CHECK(b.model.version == "vmaf_v0.6.1");   // the file's built-in model is replaced

    vmafcli::Args c;
    REQUIRE(run({ "--config", "file.cfg" }, c, files).empty());
    CHECK(c.model.path == "models/m.json");

    vmafcli::Args d;
    CHECK(run({ "--config", "both.cfg", "--model", "x" }, d, files) ==
          "config file both.cfg, model (line 2) and model-file (line 4) can't both be set");
    CHECK(contains(run({ "--config", "file.cfg", "--model", "a", "--model-file", "b" }, d, files),
                   "--model and --model-file can't be given together"));
}

TEST_CASE("vmafcli::parse: comments, blank lines and white space in a config file", "[vmafcli]")
{
    const std::map<std::string, std::string> files = {
        { "a.cfg", "\xEF\xBB\xBF# settings of one run\n"
                   "\n"
                   "   # indented comment\n"
                   "\tframes\t=\t7 \r\n"
                   "ref=https://h/x.m3u8?a=1&b=2#t\n"
                   "dis =  dir with spaces/d.mp4  \n"
                   "out = report #1.json\n"
                   "model-file = C:\\models\\\"m\".json" },   // no newline at the end
        { "empty.cfg", "" },
        { "comments.cfg", "# only\n\n  \t\n" },
    };
    vmafcli::Args a;
    REQUIRE(run({ "--config", "a.cfg" }, a, files).empty());
    CHECK(a.frames == 7);
    CHECK(a.ref == "https://h/x.m3u8?a=1&b=2#t");   // the first '=' ends the name
    CHECK(a.dis == "dir with spaces/d.mp4");
    CHECK(a.out == "report #1.json");                 // no comment after a value
    CHECK(a.model.path == "C:\\models\\\"m\".json");     // taken as it is
    for (const char* f : { "empty.cfg", "comments.cfg" }) {
        INFO(f);
        vmafcli::Args b;
        REQUIRE(run({ "--config", f, "--ref", "r", "--dis", "d", "--out", "o" }, b, files).empty());
        CHECK(b.frames == 0);
        CHECK(b.ref == "r");
    }
}

TEST_CASE("vmafcli::parse: what a config file can't have", "[vmafcli]")
{
    const std::map<std::string, std::string> files = {
        { "unknown.cfg", "ref = r\nscalr = bicubic\n" },
        { "dashes.cfg", "--scaler = bicubic\n" },
        { "config.cfg", "config = other.cfg\n" },
        { "help.cfg", "help = yes\n" },
        { "twice.cfg", "scaler = bicubic\n# again:\nscaler = lanczos\n" },
        { "no_equals.cfg", "\nframes 10\n" },
        { "no_name.cfg", " = 10\n" },
        { "empty_value.cfg", "frames =   \n" },
        { "bad_frames.cfg", "frames = 0\n" },
        { "bad_scaler.cfg", "scaler = Bicubic\n" },
    };
    const std::vector<std::pair<std::string, std::string>> cases = {
        { "unknown.cfg", "config file unknown.cfg, line 2: unknown setting 'scalr'" },
        { "dashes.cfg", "config file dashes.cfg, line 1: unknown setting '--scaler' "
                        "(names are written without --)" },
        { "config.cfg", "config file config.cfg, line 1: config can't be set in a config file" },
        { "help.cfg", "config file help.cfg, line 1: unknown setting 'help'" },
        { "twice.cfg", "config file twice.cfg, line 3: scaler is set more than once (first on "
                       "line 1)" },
        { "no_equals.cfg", "config file no_equals.cfg, line 2: expected name = value, not "
                           "'frames 10'" },
        { "no_name.cfg", "config file no_name.cfg, line 1: no name before '='" },
        { "empty_value.cfg", "config file empty_value.cfg, line 1: frames needs a non-empty value" },
        { "bad_frames.cfg", "config file bad_frames.cfg, line 1: frames wants a whole number from "
                            "1 to 4294967295, not '0'" },
        { "bad_scaler.cfg", "config file bad_scaler.cfg, line 1: scaler wants bilinear, bicubic or "
                            "lanczos, not 'Bicubic'" },
    };
    for (const auto& [file, want] : cases) {
        INFO(file);
        vmafcli::Args a;
        a.ref = "kept";
        CHECK(run({ "--config", file, "--ref", "r", "--dis", "d", "--out", "o" }, a, files) == want);
        CHECK(a.ref == "kept");   // a failed parse leaves the output alone
    }
    // A bad setting is an error even where the command line replaces it.
    vmafcli::Args b;
    CHECK(contains(run({ "--config", "bad_frames.cfg", "--frames", "5", "--ref", "r", "--dis", "d",
                         "--out", "o" }, b, files),
                   "line 1: frames wants a whole number"));
}

TEST_CASE("vmafcli::parse: --config itself", "[vmafcli]")
{
    const std::map<std::string, std::string> files = { { "a.cfg", "frames = 2\n" } };
    vmafcli::Args a;
    CHECK(run({ "--config", "missing.cfg", "--ref", "r", "--dis", "d", "--out", "o" }, a, files) ==
          "cannot read the config file missing.cfg: No such file or directory");
    CHECK(run({ "--config", "a.cfg", "--config", "a.cfg" }, a, files) ==
          "--config is given more than once");
    CHECK(contains(run({ "--ref", "r", "--config" }, a, files), "--config needs a value"));
    CHECK(contains(run({ "--config", "", "--ref", "r" }, a, files), "--config needs a non-empty value"));
    // Required options still have to come from somewhere.
    CHECK(run({ "--config", "a.cfg", "--ref", "r", "--dis", "d" }, a, files) ==
          "missing --out <report.json>");

    // -h wins, and the file isn't read.
    int loads = 0;
    vmafcli::Args b;
    REQUIRE(run({ "--config", "missing.cfg", "-h" }, b, files, &loads).empty());
    CHECK(b.help);
    CHECK(loads == 0);
}

TEST_CASE("vmafcli::read_config: a file's bytes as they are", "[vmafcli]")
{
    const std::filesystem::path path = testtemp::path("mirage_test_vmaf_cli.cfg");
    using namespace std::string_literals;
    const std::string text = "frames = 4\r\n# \xc3\xa9\n\0scaler = lanczos"s;   // a NUL too
    {
        std::ofstream f(path, std::ios::binary | std::ios::trunc);
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
    }
    const vmafcli::ConfigFile f = vmafcli::read_config(path.string());
    CHECK(f.error.empty());
    CHECK(f.text == text);

    std::filesystem::remove(path);
    const vmafcli::ConfigFile gone = vmafcli::read_config(path.string());
    CHECK(gone.text.empty());
    CHECK(gone.error == "No such file or directory");
    CHECK(vmafcli::read_config(testtemp::dir().string()).error == "it is a directory");

    // parse() reads the file when not told otherwise.
    {
        std::ofstream(path) << "ref = r\ndis = d\nout = o\nscaler = lanczos\n";
    }
    vmafcli::Args a;
    REQUIRE(run({ "--config", path.string() }, a).empty());
    CHECK(a.scaler == VMAFScaler::Lanczos);
    std::filesystem::remove(path);
}
