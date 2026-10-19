// HLS master deduplication (HlsMaster.h): which entries a copy keeps, the
// URI forms it resolves, and what makes it leave a master alone. Plus the
// playing-variant lookup through such a copy (HlsVariants.h) and the
// --net-report summary.

#include <catch2/catch_test_macros.hpp>

#include "HlsMaster.h"
#include "HlsVariants.h"
#include "ManifestParser.h"
#include "NetworkLogger.h"

#include <string>
#include <vector>

namespace {

// Apple bipbop's shape: 3 video playlists × 2 audio groups, listed group by
// group, renditions after the variants.
const char* kBipbopLike =
    "#EXTM3U\n"
    "#EXT-X-VERSION:6\n"
    "#EXT-X-INDEPENDENT-SEGMENTS\n"
    "\n"
    "#EXT-X-STREAM-INF:AVERAGE-BANDWIDTH=2168183,BANDWIDTH=2177116,CODECS=\"avc1.640020,mp4a.40.2\",RESOLUTION=960x540,AUDIO=\"aud1\",SUBTITLES=\"sub1\"\n"
    "v5/prog_index.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=8001098,RESOLUTION=1920x1080,AUDIO=\"aud1\",SUBTITLES=\"sub1\"\n"
    "v9/prog_index.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=541052,RESOLUTION=480x270,AUDIO=\"aud1\",SUBTITLES=\"sub1\"\n"
    "v2/prog_index.m3u8\n"
    "\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=2399619,RESOLUTION=960x540,AUDIO=\"aud2\",SUBTITLES=\"sub1\"\n"
    "v5/prog_index.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=8223601,RESOLUTION=1920x1080,AUDIO=\"aud2\",SUBTITLES=\"sub1\"\n"
    "v9/prog_index.m3u8\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=763555,RESOLUTION=480x270,AUDIO=\"aud2\",SUBTITLES=\"sub1\"\n"
    "v2/prog_index.m3u8\n"
    "\n"
    "#EXT-X-I-FRAME-STREAM-INF:BANDWIDTH=187492,URI=\"v7/iframe_index.m3u8\"\n"
    "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud1\",LANGUAGE=\"en\",NAME=\"English\",DEFAULT=YES,URI=\"a1/prog_index.m3u8\"\n"
    "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud2\",LANGUAGE=\"en\",NAME=\"English\",DEFAULT=YES,URI=\"a2/prog_index.m3u8\"\n"
    "#EXT-X-MEDIA:TYPE=CLOSED-CAPTIONS,GROUP-ID=\"cc1\",NAME=\"English\",INSTREAM-ID=\"CC1\"\n"
    "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"sub1\",NAME=\"English\",URI=\"s1/en/prog_index.m3u8\"\n";

const std::string kBase = "https://cdn.example.com/hls/bipbop/master.m3u8?token=abc";
const std::string kDir  = "https://cdn.example.com/hls/bipbop/";

std::vector<std::string> lines_of(const std::string& t)
{
    std::vector<std::string> out;
    std::size_t a = 0;
    while (a < t.size()) {
        const auto b = t.find('\n', a);
        out.push_back(t.substr(a, b - a));
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

int count(const std::string& t, const std::string& needle)
{
    int n = 0;
    for (std::size_t p = t.find(needle); p != std::string::npos; p = t.find(needle, p + 1)) ++n;
    return n;
}

} // namespace

TEST_CASE("dedupe_master: one entry per playlist, every audio group still referenced", "[hls_master]")
{
    const auto copy = hls::dedupe_master(kBipbopLike, kBase);
    REQUIRE(copy);
    // First entry of each URI (0, 1, 2 — all aud1), plus the cheapest aud2
    // entry so the aud2 rendition belongs to a variant: v2/aud2 (index 5).
    CHECK(copy->kept == std::vector<int>{ 0, 1, 2, 5 });
    CHECK(count(copy->text, "#EXT-X-STREAM-INF:") == 4);
    CHECK(count(copy->text, "AUDIO=\"aud2\"") == 1);
    CHECK(copy->text.find("BANDWIDTH=763555") != std::string::npos);
    // Every rendition kept, URIs absolute against the master's directory
    // (its query string is not inherited).
    CHECK(copy->text.find("URI=\"" + kDir + "a1/prog_index.m3u8\"") != std::string::npos);
    CHECK(copy->text.find("URI=\"" + kDir + "a2/prog_index.m3u8\"") != std::string::npos);
    CHECK(copy->text.find("URI=\"" + kDir + "s1/en/prog_index.m3u8\"") != std::string::npos);
    CHECK(copy->text.find("TYPE=CLOSED-CAPTIONS") != std::string::npos);
    // FFmpeg ignores I-frame playlists: dropped.
    CHECK(copy->text.find("I-FRAME") == std::string::npos);

    // Entries keep master order and each URI line follows its tag.
    const auto ls = lines_of(copy->text);
    REQUIRE_FALSE(ls.empty());
    CHECK(ls.front() == "#EXTM3U");
    std::vector<std::string> uris;
    for (std::size_t i = 0; i + 1 < ls.size(); ++i)
        if (ls[i].starts_with("#EXT-X-STREAM-INF:")) uris.push_back(ls[i + 1]);
    CHECK(uris == std::vector<std::string>{ kDir + "v5/prog_index.m3u8", kDir + "v9/prog_index.m3u8",
                                            kDir + "v2/prog_index.m3u8", kDir + "v2/prog_index.m3u8" });
}

TEST_CASE("dedupe_master: masters left alone", "[hls_master]")
{
    SECTION("no playlist listed twice") {
        const char* t = "#EXTM3U\n"
                        "#EXT-X-STREAM-INF:BANDWIDTH=1\nlow.m3u8\n"
                        "#EXT-X-STREAM-INF:BANDWIDTH=2\nhigh.m3u8\n";
        CHECK_FALSE(hls::dedupe_master(t, kBase));
    }
    SECTION("not served over http(s)") {
        CHECK_FALSE(hls::dedupe_master(kBipbopLike, "/Users/me/master.m3u8"));
        CHECK_FALSE(hls::dedupe_master(kBipbopLike, "file:///Users/me/master.m3u8"));
    }
    SECTION("a media playlist") {
        const char* t = "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXTINF:6,\nseg0.ts\n#EXT-X-ENDLIST\n";
        CHECK_FALSE(hls::dedupe_master(t, kBase));
    }
    SECTION("no #EXTM3U header") {
        CHECK_FALSE(hls::dedupe_master(std::string("\n") + (kBipbopLike + 8), kBase));
    }
    SECTION("tags it does not know") {
        for (const char* tag : { "#EXT-X-SESSION-KEY:METHOD=AES-128,URI=\"k.key\"",
                                 "#EXT-X-DEFINE:NAME=\"x\",VALUE=\"y\"",
                                 "#EXT-X-CONTENT-STEERING:SERVER-URI=\"s.json\"",
                                 "#EXT-X-START:TIME-OFFSET=10" }) {
            CAPTURE(tag);
            CHECK_FALSE(hls::dedupe_master(std::string(kBipbopLike) + tag + "\n", kBase));
        }
    }
    SECTION("a URI it would not resolve like FFmpeg") {
        for (const char* uri : { "../v5/prog_index.m3u8", "./v5/prog_index.m3u8", "a/../v5/p.m3u8",
                                 "//other.host/v5.m3u8", "?only=query", "skd://key" }) {
            CAPTURE(uri);
            std::string t = kBipbopLike;
            t.replace(t.find("v9/prog_index.m3u8"), 18, uri);
            CHECK_FALSE(hls::dedupe_master(t, kBase));
        }
    }
    SECTION("a rendition URI it would not resolve") {
        std::string t = kBipbopLike;
        t.replace(t.find("a2/prog_index.m3u8"), 18, "../a2/prog_index.m3u8");
        CHECK_FALSE(hls::dedupe_master(t, kBase));
    }
    SECTION("EXT-X-STREAM-INF without its URI line right after it") {
        std::string t = kBipbopLike;
        t.insert(t.find("v9/prog_index.m3u8"), "# comment\n");
        CHECK_FALSE(hls::dedupe_master(t, kBase));
        CHECK_FALSE(hls::dedupe_master(std::string(kBipbopLike) + "#EXT-X-STREAM-INF:BANDWIDTH=5\n", kBase));
    }
    SECTION("a URI line that belongs to no EXT-X-STREAM-INF") {
        CHECK_FALSE(hls::dedupe_master(std::string(kBipbopLike) + "stray.m3u8\n", kBase));
    }
}

TEST_CASE("dedupe_master: groups of every kind stay covered, by the cheapest entry", "[hls_master]")
{
    // The duplicate of `b` is the only entry of subtitle group s2 → kept.
    const char* t =
        "#EXTM3U\n"
        "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"s1\",NAME=\"en\",URI=\"en.m3u8\"\n"
        "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"s2\",NAME=\"fr\",URI=\"fr.m3u8\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=100,SUBTITLES=\"s1\"\na.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=200,SUBTITLES=\"s1\"\nb.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=300,SUBTITLES=\"s1\"\na.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=900,SUBTITLES=\"s2\"\nb.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=400,SUBTITLES=\"s2\"\na.m3u8\n";
    const auto copy = hls::dedupe_master(t, "http://h/x/master.m3u8");
    REQUIRE(copy);
    CHECK(copy->kept == std::vector<int>{ 0, 1, 4 });   // s2 via its cheapest entry (400)
    CHECK(copy->text.find("URI=\"http://h/x/fr.m3u8\"") != std::string::npos);
}

TEST_CASE("absolute_uri: the reference forms FFmpeg resolves the same way", "[hls_master]")
{
    CHECK(hls::absolute_uri(kBase, "v5/prog_index.m3u8") == kDir + "v5/prog_index.m3u8");
    CHECK(hls::absolute_uri(kBase, "v5/p.m3u8?x=1") == kDir + "v5/p.m3u8?x=1");
    CHECK(hls::absolute_uri(kBase, "/root/p.m3u8") == "https://cdn.example.com/root/p.m3u8");
    CHECK(hls::absolute_uri(kBase, "https://other/p.m3u8") == "https://other/p.m3u8");
    CHECK(hls::absolute_uri("http://host", "p.m3u8") == "http://host/p.m3u8");
    CHECK(hls::absolute_uri("http://host/", "p.m3u8") == "http://host/p.m3u8");
    CHECK(hls::absolute_uri("http://host:8080/a/b.m3u8#frag", "c.m3u8") == "http://host:8080/a/c.m3u8");
    // Not attempted.
    for (const char* uri : { "", "../p.m3u8", "./p.m3u8", "a/./p.m3u8", "a/..", "//h/p.m3u8",
                             "?q", "#f", "data:x", "p q.m3u8", "a\\b.m3u8" }) {
        CAPTURE(uri);
        CHECK(hls::absolute_uri(kBase, uri).empty());
    }
    CHECK(hls::absolute_uri("/local/master.m3u8", "p.m3u8").empty());
}

TEST_CASE("ManifestParser resolves root-relative URIs against the host", "[hls_master][manifest]")
{
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\n/abs/v.m3u8\n"
                              "#EXT-X-STREAM-INF:BANDWIDTH=2\nrel/v.m3u8\n",
                              "https://h.example/dir/master.m3u8"));
    REQUIRE(mp.variants().size() == 2);
    CHECK(mp.variants()[0].url == "https://h.example/abs/v.m3u8");
    CHECK(mp.variants()[1].url == "https://h.example/dir/rel/v.m3u8");
}

TEST_CASE("playing_variant: the entry with the video's playlist and the audio's group", "[hls_master]")
{
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text(kBipbopLike, kBase));
    const auto& vars = mp.variants();
    REQUIRE(vars.size() == 6);

    SECTION("master opened as served: programs are the 6 entries") {
        // Streams: 0..5 = the entries' own video, 6 = a1, 7 = a2.
        std::vector<hls::ProgramInfo> progs;
        for (int p = 0; p < 6; ++p)
            progs.push_back({ vars[static_cast<std::size_t>(p)].bandwidth, { p, p < 3 ? 6 : 7 } });
        // Video of entry 1 (v9/aud1) with its own group's audio.
        CHECK(hls::playing_variant(1, 6, progs, vars, {}) == 1);
        // ... after switching the audio to a2: the v9/aud2 entry.
        CHECK(hls::playing_variant(1, 7, progs, vars, {}) == 4);
        // No audio routed / unknown video.
        CHECK(hls::playing_variant(1, -1, progs, vars, {}) == 1);
        CHECK(hls::playing_variant(-1, 6, progs, vars, {}) == -1);
    }
    SECTION("deduplicated copy keeping entries 0, 1, 2, 5") {
        const std::vector<int> kept{ 0, 1, 2, 5 };
        // Streams: 0..3 = the kept entries' video, 4 = a1, 5 = a2.
        std::vector<hls::ProgramInfo> progs{
            { vars[0].bandwidth, { 0, 4 } }, { vars[1].bandwidth, { 1, 4 } },
            { vars[2].bandwidth, { 2, 4 } }, { vars[5].bandwidth, { 3, 5 } } };
        const auto opened = hls::opened_variants(vars, kept);
        REQUIRE(opened.size() == 4);
        CHECK(opened[3].bandwidth == vars[5].bandwidth);
        // Video stream 1 is entry 1's (v9/aud1).
        const int v9 = hls::variant_of_stream(progs, 1, vars[1].bandwidth, opened, mp.renditions());
        REQUIRE(v9 == 1);
        CHECK(kept[static_cast<std::size_t>(v9)] == 1);
        CHECK(hls::playing_variant(1, 4, progs, vars, kept) == 1);   // v9 + a1
        CHECK(hls::playing_variant(1, 5, progs, vars, kept) == 4);   // v9 + a2: v9/aud2, dropped from the copy
        CHECK(hls::playing_variant(0, 5, progs, vars, kept) == 3);   // v5 + a2
        CHECK(hls::opened_variants(vars, { 0, 9 }).empty());          // bad index
    }
}

TEST_CASE("current_variant: the program with both streams, else the playlist + group lookup", "[hls_master]")
{
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text(kBipbopLike, kBase));
    const auto& vars = mp.variants();

    SECTION("as served") {
        std::vector<hls::ProgramInfo> progs;
        for (int p = 0; p < 6; ++p)
            progs.push_back({ vars[static_cast<std::size_t>(p)].bandwidth, { p, p < 3 ? 6 : 7 } });
        const std::vector<int> video_variant{ 0, 1, 2, 3, 4, 5, -1, -1 };
        CHECK(hls::current_variant(progs, video_variant, 1, 6, vars, {}) == 1);
        CHECK(hls::current_variant(progs, video_variant, 4, 7, vars, {}) == 4);
        CHECK(hls::current_variant(progs, video_variant, 1, 7, vars, {}) == 4);   // v9/aud1's video + a2
        CHECK(hls::current_variant(progs, video_variant, 1, -1, vars, {}) == 1);
    }
    SECTION("single-variant master: the one program, though nothing maps") {
        ManifestParser one;
        REQUIRE(one.parse_hls_text("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=5,AUDIO=\"a\"\nv.m3u8\n"
                                   "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"x\",URI=\"a.m3u8\"\n", kBase));
        const std::vector<hls::ProgramInfo> progs{ { 5, { 1, 0 } } };
        CHECK(hls::current_variant(progs, { -1, -1 }, 1, 0, one.variants(), {}) == 0);
    }
    SECTION("deduplicated copy") {
        const std::vector<int> kept{ 0, 1, 2, 5 };
        const std::vector<hls::ProgramInfo> progs{
            { vars[0].bandwidth, { 0, 4 } }, { vars[1].bandwidth, { 1, 4 } },
            { vars[2].bandwidth, { 2, 4 } }, { vars[5].bandwidth, { 3, 5 } } };
        const std::vector<int> video_variant{ 0, 1, 2, 5, -1, -1 };
        CHECK(hls::current_variant(progs, video_variant, 1, 4, vars, kept) == 1);
        CHECK(hls::current_variant(progs, video_variant, 1, 5, vars, kept) == 4);
        CHECK(hls::current_variant(progs, video_variant, 3, 5, vars, kept) == 5);
        CHECK(hls::current_variant(progs, video_variant, 9, 5, vars, kept) == -1);
    }
    CHECK(hls::current_variant({}, {}, 0, 1, {}, {}) == -1);
}

TEST_CASE("net_report_json: requests in the window, totals per initiator", "[network_logger][net_report]")
{
    auto req = [](NetInitiator who, std::int64_t t, std::int64_t bytes, const char* url) {
        NetRequest r;
        r.initiator = who;
        r.t_start   = t;
        r.t_end     = t + 1000;
        r.bytes     = bytes;
        r.status    = 200;
        r.url       = url;
        return r;
    };
    std::vector<NetRequest> reqs{
        req(NetInitiator::Playback,   500,     10, "https://h/before.m3u8"),        // before the window
        req(NetInitiator::Playback,   1000,    100, "https://h/master.m3u8"),
        req(NetInitiator::Playback,   2000,    5000, "https://h/v1/seg0.ts"),
        req(NetInitiator::Thumbnails, 3000,    700, "https://h/v2/prog_index.m3u8?t=\"q\""),
        req(NetInitiator::Thumbnails, 4000,    -1, "https://h/v2/seg9.ts"),          // unknown bytes → 0
        req(NetInitiator::Scrub,      21'000,  9, "https://h/after.ts"),             // after the window
    };
    reqs[2].range_start = 0;
    reqs[2].range_end   = 4999;
    reqs[4].failed      = true;
    const std::string j = net_report_json(reqs, 1000, 21'000);
    CHECK(j.find("\"window_s\": 0.020") != std::string::npos);
    CHECK(j.find("\"total\": {\"requests\":4,\"failed\":1,\"playlists\":2,\"media\":2,\"bytes\":5800}")
          != std::string::npos);
    CHECK(j.find("\"playback\": {\"requests\":2,\"failed\":0,\"playlists\":1,\"media\":1,\"bytes\":5100}")
          != std::string::npos);
    CHECK(j.find("\"thumbnails\": {\"requests\":2,\"failed\":1,\"playlists\":1,\"media\":1,\"bytes\":700}")
          != std::string::npos);
    CHECK(j.find("\"scrub\"") == std::string::npos);
    CHECK(j.find("before.m3u8") == std::string::npos);
    CHECK(j.find("\"range\":\"0-4999\"") != std::string::npos);
    CHECK(j.find("\\\"q\\\"") != std::string::npos);   // quotes in a URL escaped
    CHECK(j.find("\"t_ms\":1.0") != std::string::npos);   // ms since the window start
}
