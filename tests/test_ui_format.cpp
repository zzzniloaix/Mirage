#include <catch2/catch_test_macros.hpp>

#include "UiFormat.h"
#include "SubtitleSelect.h"

#include <vector>

using namespace ui;

// ── format_clock / format_timecode ────────────────────────────────────────────

TEST_CASE("format_clock: minutes and hours forms", "[ui]")
{
    CHECK(format_clock(0.0, false)       == "00:00.000");
    CHECK(format_clock(12.345, false)    == "00:12.345");
    CHECK(format_clock(751.04, false)    == "12:31.040");
    CHECK(format_clock(3725.5, true)     == "1:02:05.500");
    CHECK(format_clock(-4.0, false)      == "00:00.000");   // negative clamped
}

TEST_CASE("format_timecode: mode 0 under and over one hour", "[ui]")
{
    CHECK(format_timecode(12.345, 2467.0, 0, 24.0) == "00:12.345 / 41:07.000");
    CHECK(format_timecode(12.345, 3600.0, 0, 24.0) == "0:00:12.345 / 1:00:00.000");
    CHECK(format_timecode(-1.0,   60.0,   0, 24.0) == "00:00.000 / 01:00.000");
}

TEST_CASE("format_timecode: mode 1 frames, falls back without fps", "[ui]")
{
    CHECK(format_timecode(41.408, 100.0, 1, 30.0) == "f 1242 / 3000");
    CHECK(format_timecode(1.0,    10.0,  1, 0.0)  == "00:01.000 / 00:10.000");
}

TEST_CASE("format_timecode: mode 2 remaining", "[ui]")
{
    CHECK(format_timecode(10.0, 70.0, 2, 30.0) == "-01:00.000 / 01:10.000");
    CHECK(format_timecode(90.0, 70.0, 2, 30.0) == "-00:00.000 / 01:10.000");
}

TEST_CASE("format_timecode: live or unknown duration", "[ui]")
{
    CHECK(format_timecode(12.345, -1.0, 0, 30.0) == "LIVE 00:12.345");
    CHECK(format_timecode(12.345,  0.0, 2, 30.0) == "LIVE 00:12.345");
}

// ── decimate_ticks ────────────────────────────────────────────────────────────

TEST_CASE("decimate_ticks: min spacing honoured, endpoints kept", "[ui]")
{
    std::vector<double> kf;
    for (int i = 0; i <= 600; ++i) kf.push_back(i);   // 1 s GOP, 10 min
    const auto ticks = decimate_ticks(kf, 600.0, 1000.0f, 3.0f);
    REQUIRE(ticks.size() >= 2);
    CHECK(ticks.front() == 0.0f);
    CHECK(ticks.back()  == 1000.0f);
    for (std::size_t i = 1; i < ticks.size(); ++i)
        CHECK(ticks[i] - ticks[i - 1] >= 3.0f);
    CHECK(ticks.size() <= 334);
}

TEST_CASE("decimate_ticks: sparse input untouched, empty input", "[ui]")
{
    CHECK(decimate_ticks({}, 10.0, 100.0f, 3.0f).empty());
    CHECK(decimate_ticks({ 1.0 }, 0.0, 100.0f, 3.0f).empty());
    const auto t = decimate_ticks({ 0.0, 5.0, 10.0 }, 10.0, 100.0f, 3.0f);
    REQUIRE(t.size() == 3);
    CHECK(t[1] == 50.0f);
}

// ── index_at_or_before ────────────────────────────────────────────────────────

TEST_CASE("index_at_or_before: before, exact, between, gap, empty", "[ui]")
{
    const std::vector<double> p = { 2.0, 4.0, 8.0 };
    CHECK(index_at_or_before(p, 1.0, 10.0)  == -1);   // before first
    CHECK(index_at_or_before(p, 4.0, 10.0)  ==  1);   // exact hit
    CHECK(index_at_or_before(p, 7.9, 10.0)  ==  1);   // between
    CHECK(index_at_or_before(p, 30.0, 10.0) == -1);   // past max_gap
    CHECK(index_at_or_before(p, 18.0, 10.0) ==  2);   // exactly max_gap
    CHECK(index_at_or_before({}, 1.0, 10.0) == -1);
}

// ── SubChoice::label ──────────────────────────────────────────────────────────

TEST_CASE("SubChoice::label formats embedded and sidecar choices", "[ui]")
{
    SubChoice e;
    e.stream_idx = 2; e.lang = "eng"; e.codec = "SRT";
    CHECK(e.label() == "2 · eng · SRT");
    e.is_forced = true; e.codec = "PGS"; e.lang = "jpn"; e.stream_idx = 3;
    CHECK(e.label() == "3 · jpn · PGS (forced)");

    SubChoice s;
    s.kind = SubChoice::Sidecar; s.path = "/tmp/x/movie.en.srt"; s.lang = "en"; s.codec = "SRT";
    CHECK(s.label() == "movie.en.srt · en · SRT");
    CHECK(sub_codec_display("subrip") == "SRT");
    CHECK(sub_codec_display("hdmv_pgs_subtitle") == "PGS");
}
