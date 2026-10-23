#include <catch2/catch_test_macros.hpp>

#include "SubtitleSelect.h"
#include "Demuxer.h"   // SubtitleTrackInfo
#include "TestTemp.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// A fresh directory inside the run's own (TestTemp.h), removed on scope exit.
struct TempDir {
    fs::path dir;
    TempDir()
    {
        static int made = 0;
        dir = testtemp::path("mirage_subsel_" + std::to_string(made++));
        fs::create_directories(dir);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(dir, ec); }
    void touch(const std::string& name) const { std::ofstream(dir / name) << "x"; }
};

std::vector<std::string> names(const std::vector<std::string>& paths)
{
    std::vector<std::string> out;
    for (const auto& p : paths) out.push_back(fs::path(p).filename().string());
    return out;
}

SubChoice emb(int idx, std::string lang, bool text = true, bool def = false, bool forced = false)
{
    SubChoice c;
    c.kind = SubChoice::Embedded; c.stream_idx = idx; c.lang = std::move(lang);
    c.is_text = text; c.is_default = def; c.is_forced = forced;
    c.codec = text ? "SRT" : "PGS";
    return c;
}

SubChoice side(std::string path, std::string lang = {}, bool forced = false)
{
    SubChoice c = sidecar_choice(path);
    c.lang = std::move(lang); c.is_forced = forced;
    return c;
}

} // namespace

// ── find_sidecars ─────────────────────────────────────────────────────────────

TEST_CASE("find_sidecars — matching names and sort order", "[subtitle][select]")
{
    TempDir t;
    t.touch("movie.mp4");
    t.touch("movie.srt");          // exact stem
    t.touch("movie.ja.srt");
    t.touch("movie.en.srt");
    t.touch("movie.pt-BR.vtt");
    t.touch("movie.en.forced.srt");
    t.touch("movie.de.SRT");       // extension is case-insensitive
    t.touch("movie.fr.sdh.ass");
    // Rejected:
    t.touch("movie2.srt");         // different stem
    t.touch("movie.en.txt");       // not a subtitle extension
    t.touch("movie.english.srt");  // tag too long
    t.touch("movie.e.srt");        // tag too short
    t.touch("other.srt");

    const auto got = names(find_sidecars(t.dir / "movie.mp4"));
    const std::vector<std::string> want = {
        "movie.srt",
        "movie.en.forced.srt", "movie.en.srt",
        "movie.de.SRT", "movie.fr.sdh.ass", "movie.ja.srt", "movie.pt-BR.vtt",
    };
    CHECK(got == want);
}

TEST_CASE("find_sidecars — no matches, missing dir, cap of 16", "[subtitle][select]")
{
    TempDir t;
    t.touch("clip.mp4");
    t.touch("clip2.srt");
    CHECK(find_sidecars(t.dir / "clip.mp4").empty());
    CHECK(find_sidecars(t.dir / "nope" / "clip.mp4").empty());

    for (char a = 'a'; a <= 'u'; ++a) t.touch(std::string("clip.") + a + a + ".srt");
    CHECK(find_sidecars(t.dir / "clip.mp4").size() == 16);
}

TEST_CASE("sidecar_choice — language, forced, codec and label", "[subtitle][select]")
{
    auto c = sidecar_choice("/x/movie.en.forced.srt", "movie");
    CHECK(c.kind == SubChoice::Sidecar);
    CHECK(c.lang == "en");
    CHECK(c.is_forced);
    CHECK(c.codec == "SRT");
    CHECK(c.label() == "movie.en.forced.srt · en · SRT (forced)");

    c = sidecar_choice("/x/movie.srt", "movie");
    CHECK(c.lang.empty());
    CHECK(c.label() == "movie.srt · SRT");

    // Stem "the.end" must not read "end" as a language when the media stem is known.
    CHECK(sidecar_choice("/x/the.end.srt", "the.end").lang.empty());
    // Unknown media (--sub-file): trailing tag guessed.
    CHECK(sidecar_choice("/x/whatever.pt-BR.vtt").lang == "pt-BR");
    CHECK(sidecar_choice("/x/whatever.pt-BR.vtt").codec == "WebVTT");
    CHECK(sidecar_choice("/x/a.fr.sdh.ass").is_hi);
}

TEST_CASE("sub_lang_matches — ISO 639-1 vs 639-2", "[subtitle][select]")
{
    CHECK(sub_lang_matches("en", "eng"));
    CHECK(sub_lang_matches("ENG", "en-US"));
    CHECK(sub_lang_matches("fre", "fra"));
    CHECK(sub_lang_matches("xx", "xx"));
    CHECK_FALSE(sub_lang_matches("en", "jpn"));
    CHECK_FALSE(sub_lang_matches("", ""));
}

TEST_CASE("build_choices — embedded then sidecars", "[subtitle][select]")
{
    SubtitleTrackInfo t;
    t.stream_idx = 2; t.codec_name = "subrip"; t.lang = "eng"; t.is_default = true;
    SubtitleTrackInfo b;
    b.stream_idx = 3; b.codec_name = "hdmv_pgs_subtitle"; b.is_text = false; b.is_forced = true;
    const auto ch = build_choices({ t, b }, { "/m/movie.ja.srt" }, "movie");
    REQUIRE(ch.size() == 3);
    CHECK(ch[0].label() == "2 · eng · SRT");
    CHECK(ch[0].is_default);
    CHECK(ch[1].label() == "3 · PGS (forced)");
    CHECK_FALSE(ch[1].is_text);
    CHECK(ch[2].kind == SubChoice::Sidecar);
    CHECK(ch[2].lang == "ja");
}

// ── choose_initial: one case per rule ─────────────────────────────────────────

TEST_CASE("choose_initial — policy rules 1-10", "[subtitle][select]")
{
    SubRule r{};
    const SubCli  none;
    const SubPref autop;

    // 1. --sub-file wins over everything, even pref Off.
    {
        std::vector<SubChoice> ch = { emb(2, "eng", true, true), side("/m/a.srt"), side("/m/b.srt") };
        SubCli cli; cli.file = "/m/b.srt";
        CHECK(choose_initial(ch, cli, { SubPref::Off, {} }, &r) == 2);
        CHECK(r == SubRule::CliFile);
    }
    // 2. --sub N, and --sub off.
    {
        std::vector<SubChoice> ch = { emb(2, "eng", true, true), emb(5, "jpn") };
        SubCli cli; cli.stream_idx = 5;
        CHECK(choose_initial(ch, cli, autop, &r) == 1);
        CHECK(r == SubRule::CliStream);
        SubCli off; off.off = true;
        CHECK(choose_initial(ch, off, autop, &r) == -1);
        CHECK(r == SubRule::CliOff);
        SubCli bad; bad.stream_idx = 9;   // invalid → falls through to default
        CHECK(choose_initial(ch, bad, autop, &r) == 0);
        CHECK(r == SubRule::Default);
    }
    // 3. pref Off beats a default track.
    {
        std::vector<SubChoice> ch = { emb(2, "eng", true, true) };
        CHECK(choose_initial(ch, none, { SubPref::Off, {} }, &r) == -1);
        CHECK(r == SubRule::PrefOff);
    }
    // 4. pref Lang: text over bitmap, embedded over sidecar; 639-1 matches 639-2.
    {
        std::vector<SubChoice> ch = { emb(2, "eng", true, true), emb(3, "jpn", false),
                                      side("/m/x.ja.srt", "ja"), emb(4, "jpn", true) };
        CHECK(choose_initial(ch, none, { SubPref::Lang, "ja" }, &r) == 3);
        CHECK(r == SubRule::PrefLang);
        ch.pop_back();
        CHECK(choose_initial(ch, none, { SubPref::Lang, "ja" }, &r) == 2);   // sidecar before bitmap
        ch.pop_back();
        CHECK(choose_initial(ch, none, { SubPref::Lang, "jpn" }, &r) == 1);  // bitmap as last resort
        // No match → continues down the list.
        CHECK(choose_initial(ch, none, { SubPref::Lang, "fre" }, &r) == 0);
        CHECK(r == SubRule::Default);
    }
    // 5. forced (embedded before sidecar), ahead of default.
    {
        std::vector<SubChoice> ch = { emb(2, "eng", true, true), side("/m/f.srt", "en", true),
                                      emb(4, "eng", true, false, true) };
        CHECK(choose_initial(ch, none, autop, &r) == 2);
        CHECK(r == SubRule::Forced);
        ch.pop_back();
        CHECK(choose_initial(ch, none, autop, &r) == 1);
        CHECK(r == SubRule::Forced);
    }
    // 6. embedded default, ahead of sidecar.
    {
        std::vector<SubChoice> ch = { emb(2, "eng"), side("/m/a.srt"), emb(4, "jpn", true, true) };
        CHECK(choose_initial(ch, none, autop, &r) == 2);
        CHECK(r == SubRule::Default);
    }
    // 7. first sidecar, ahead of non-default embedded text.
    {
        std::vector<SubChoice> ch = { emb(2, "eng"), side("/m/a.srt"), side("/m/b.srt") };
        CHECK(choose_initial(ch, none, autop, &r) == 1);
        CHECK(r == SubRule::Sidecar);
    }
    // 8. first embedded text, ahead of bitmap.
    {
        std::vector<SubChoice> ch = { emb(2, "eng", false), emb(3, "eng"), emb(4, "jpn") };
        CHECK(choose_initial(ch, none, autop, &r) == 1);
        CHECK(r == SubRule::AnyText);
    }
    // 9. bitmap only.
    {
        std::vector<SubChoice> ch = { emb(2, "eng", false) };
        CHECK(choose_initial(ch, none, autop, &r) == 0);
        CHECK(r == SubRule::AnyBitmap);
    }
    // 10. nothing.
    CHECK(choose_initial({}, none, autop, &r) == -1);
    CHECK(r == SubRule::Off);

    CHECK(sub_rule_is_auto(SubRule::Default));
    CHECK_FALSE(sub_rule_is_auto(SubRule::CliFile));
    CHECK_FALSE(sub_rule_is_auto(SubRule::PrefLang));
}
