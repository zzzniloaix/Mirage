#include <catch2/catch_test_macros.hpp>

#include "VMAFCli.h"

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
    CHECK(a.scaler == VMAFScaler::Bilinear);   // the analyzer's scaler unless told otherwise
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
                        "[--frames N] [--scaler NAME]\n"));
    for (std::string_view opt : { "--ref", "--dis", "--out", "--frames", "--scaler", "-h, --help" }) {
        INFO(opt);
        CHECK(contains(u, opt));
    }
    CHECK(contains(u, "Exit status"));
}
