#include <catch2/catch_test_macros.hpp>

#include "UiFormat.h"
#include "SubtitleSelect.h"

#include <set>
#include <string>
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
    // Seconds since 1970 (live CMAF timestamps): the stream's UTC clock.
    CHECK(format_timecode(1792183923.84, -1.0, 0, 25.0) == "LIVE 20:52:03.840 UTC");
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

// ── ellipsize_middle ──────────────────────────────────────────────────────────

TEST_CASE("ellipsize_middle: head and tail kept, whole UTF-8 characters", "[ui]")
{
    // One unit a character, "…" included: a monospace font of width 1.
    const auto chars = [](const std::string& t) {
        float n = 0.0f;
        for (const char c : t) n += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1.0f : 0.0f;
        return n;
    };
    CHECK(ellipsize_middle("abc", 3.0f, chars) == "abc");                 // fits
    CHECK(ellipsize_middle("", 0.0f, chars).empty());
    CHECK(ellipsize_middle("abcdefghij", 5.0f, chars) == "ab…ij");
    CHECK(ellipsize_middle("abcdefghij", 4.0f, chars) == "a…ij");    // the tail gets the odd one
    CHECK(ellipsize_middle("abcdefghij", 9.0f, chars) == "abcd…ghij");
    CHECK(ellipsize_middle("abcdefghij", 0.5f, chars) == "…");       // nothing fits
    // Two-byte characters are kept or cut whole.
    CHECK(ellipsize_middle("αβγδεζηθ", 5.0f, chars)
          == "αβ…ηθ");
    // Rendition labels that share their head keep the part that tells them apart.
    CHECK(ellipsize_middle("audio main/commentary-stereo-en", 14.0f, chars) == "audio …ereo-en");
    CHECK(ellipsize_middle("audio main/commentary-stereo-fr", 14.0f, chars) == "audio …ereo-fr");
}

// ── distinct_labels ───────────────────────────────────────────────────────────

TEST_CASE("distinct_labels: a group's shared ends dropped, in whole tokens", "[ui]")
{
    using V = std::vector<std::string>;
    // NAMEs that differ only at the head, the tail shared: what tells them apart stays.
    CHECK(distinct_labels({ "audio aud/aa_main_x 256K", "audio aud/bb_main_x 256K" })
          == V{ "audio aud/aa…", "audio aud/bb…" });
    // Only in the middle: both ends go.
    CHECK(distinct_labels({ "audio aud/main_en_stereo", "audio aud/main_fr_stereo", "audio aud/main_de_stereo" })
          == V{ "audio aud/…en…", "audio aud/…fr…", "audio aud/…de…" });
    // Only at the tail: the head goes.
    CHECK(distinct_labels({ "audio aud/main-stereo-en", "audio aud/main-stereo-fr" })
          == V{ "audio aud/…en", "audio aud/…fr" });
    // Whole tokens only: "en" is no token shared with "en_sdh", and "256K" with "128K" shares no token.
    CHECK(distinct_labels({ "subtitles subs/en", "subtitles subs/en_sdh" })
          == V{ "subtitles subs/en", "subtitles subs/en_sdh" });
    CHECK(distinct_labels({ "audio aud/aa_main_x 256K", "audio aud/bb_main_x 256K", "audio aud/cc_main_y 128K" })
          == V{ "audio aud/aa_main_x 256K", "audio aud/bb_main_x 256K", "audio aud/cc_main_y 128K" });
    // Each group apart; a label alone in its group, and a variant's (no '/'), stay whole.
    CHECK(distinct_labels({ "v0 1080p", "audio a/x_1", "audio a/y_1", "audio b/z_2", "subtitles s/en_cc",
                            "subtitles s/fr_cc" })
          == V{ "v0 1080p", "audio a/x…", "audio a/y…", "audio b/z_2", "subtitles s/en…",
                "subtitles s/fr…" });
    // Two-byte characters are never cut: the cut falls at a separator.
    CHECK(distinct_labels({ "audio aud/éa_x", "audio aud/éb_x" })
          == V{ "audio aud/éa…", "audio aud/éb…" });
    CHECK(distinct_labels({}).empty());
}

// ── row_labels ────────────────────────────────────────────────────────────────

TEST_CASE("row_labels: the tokens not all of a group's labels have, at a column's width", "[ui]")
{
    using V = std::vector<std::string>;
    // The shape that defeats both ends, made up: one group; per language two bit rates of one codec and one
    // of another, each codec with its own layout; a role token most labels have, another in two; tokens every
    // label has in between.
    const V langs = { "aa", "bb-CC", "dd", "ee-419", "ff", "gg-HH", "ii", "jj-KK", "ll", "mm" };
    V labels = { "v0 720p" };
    const auto name = [](const std::string& l, const char* role, const char* codec, const char* layout,
                         const char* rate) {
        return std::string("audio aud/") + l + "_" + role + "_" + codec + "_PKG_" + layout + "_ENC_SCHM " + codec
             + "_1_PKG_ENC_SCHM_" + rate;
    };
    for (const auto& l : langs) {
        labels.push_back(name(l, "MAIN", "CODA", "L2", "128K"));
        labels.push_back(name(l, "MAIN", "CODA", "L2", "64K"));
        labels.push_back(name(l, "MAIN", "CODB", "5.1", "256K"));
    }
    labels.push_back(name("aa", "DESC", "CODA", "L2", "128K"));
    labels.push_back(name("bb-CC", "DESC", "CODA", "L2", "128K"));
    labels.push_back("subtitles subs/aa_FULL_TXT");
    labels.push_back("subtitles subs/aa_SIGNS_TXT");
    labels.push_back("subtitles subs/bb-CC_FULL_TXT");
    REQUIRE(labels.size() == 36);
    const V rows = row_labels(labels);
    CHECK(rows[0] == "v0 720p");
    // PKG, ENC, SCHM and "1" every label has; MAIN 30 of 32; L2 comes with CODA, 5.1 and 256K with CODB.
    CHECK(rows[1] == "aa CODA 128K");
    CHECK(rows[2] == "aa CODA 64K");
    CHECK(rows[3] == "aa CODB");
    CHECK(rows[4] == "bb-CC CODA 128K");       // a language tag stays whole
    CHECK(rows[31] == "aa DESC CODA 128K");    // the role most labels lack stays
    CHECK(rows[33] == "aa FULL");
    CHECK(rows[34] == "aa SIGNS");
    CHECK(rows[35] == "bb-CC FULL");
    // Cut in the middle to 16 characters, about the column's default width: still one of a kind each.
    const auto chars = [](const std::string& t) {
        float n = 0.0f;
        for (const char c : t) n += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1.0f : 0.0f;
        return n;
    };
    std::set<std::string> shown;
    for (std::size_t i = 1; i <= 32; ++i) shown.insert(ellipsize_middle(rows[i], 16.0f, chars));
    CHECK(shown.size() == 32);

    // A layout stays whole: '.' does not split a token.
    CHECK(row_labels({ "audio a/x_5.1", "audio a/x_2.0", "audio a/y_5.1" }) == V{ "x 5.1", "x 2.0", "y 5.1" });
    // The record's packages: NAMEs joined by '-' that share their head, and NAMEs that share all but a token.
    CHECK(row_labels({ "audio aud/commentary-and-description-stereo-english",
                       "audio aud/commentary-and-description-stereo-french",
                       "audio aud/commentary-and-description-surround-english" })
          == V{ "…stereo-english", "…stereo-french", "…surround-english" });
    CHECK(row_labels({ "audio aud/aa_main_tone-x_2ch_enc 256K", "audio aud/bb_main_tone-x_2ch_enc 256K" })
          == V{ "aa", "bb" });
    // A row left empty: the group falls back to distinct_labels, without the head.
    CHECK(row_labels({ "subtitles subs/en_main", "subtitles subs/en_main_sdh" }) == V{ "…main", "…main_sdh" });
    // A label alone in its group loses only its head.
    CHECK(row_labels({ "audio aud/aa_main", "subtitles subs/en" }) == V{ "aa_main", "en" });
    CHECK(row_labels({}).empty());
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
