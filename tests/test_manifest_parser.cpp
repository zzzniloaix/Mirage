#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "ManifestParser.h"

using namespace Catch::Matchers;

// ── is_manifest() ─────────────────────────────────────────────────────────────

TEST_CASE("ManifestParser::is_manifest — HLS URLs recognised", "[manifest][is_manifest]")
{
    CHECK(ManifestParser::is_manifest("https://cdn.example.com/playlist.m3u8"));
    CHECK(ManifestParser::is_manifest("/local/path/index.M3U8"));  // case-insensitive
    CHECK(ManifestParser::is_manifest("https://cdn.example.com/playlist.m3u8?v=2&t=abc"));
}

TEST_CASE("ManifestParser::is_manifest — DASH URLs recognised", "[manifest][is_manifest]")
{
    CHECK(ManifestParser::is_manifest("https://cdn.example.com/stream.mpd"));
    CHECK(ManifestParser::is_manifest("https://cdn.example.com/stream.MPD"));
    CHECK(ManifestParser::is_manifest("https://cdn.example.com/stream.mpd?start=0"));
}

TEST_CASE("ManifestParser::is_manifest — non-manifest URLs rejected", "[manifest][is_manifest]")
{
    CHECK_FALSE(ManifestParser::is_manifest("https://cdn.example.com/video.mp4"));
    CHECK_FALSE(ManifestParser::is_manifest("/local/file.mkv"));
    CHECK_FALSE(ManifestParser::is_manifest("rtmp://live.example.com/stream"));
}

// ── HLS parse_hls_text() ─────────────────────────────────────────────────────

TEST_CASE("HLS — DISCONTINUITY tag captured at correct PTS", "[manifest][hls]")
{
    const std::string hls = R"(#EXTM3U
#EXT-X-VERSION:3
#EXTINF:6.006,
seg001.ts
#EXTINF:6.006,
seg002.ts
#EXT-X-DISCONTINUITY
#EXTINF:4.004,
seg003.ts
)";
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text(hls));
    const auto& tags = mp.tags();
    REQUIRE(tags.size() == 1);
    CHECK(tags[0].kind == ManifestTagKind::Discontinuity);
    CHECK_THAT(tags[0].pts, WithinAbs(12.012, 0.001));
}

TEST_CASE("HLS — CUE-OUT and CUE-IN captured", "[manifest][hls]")
{
    const std::string hls = R"(#EXTM3U
#EXTINF:6.0,
seg001.ts
#EXT-X-CUE-OUT:DURATION=30
#EXTINF:6.0,
seg002.ts
#EXT-X-CUE-IN
#EXTINF:6.0,
seg003.ts
)";
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text(hls));
    const auto& tags = mp.tags();
    REQUIRE(tags.size() == 2);
    CHECK(tags[0].kind == ManifestTagKind::CueOut);
    CHECK_THAT(tags[0].pts, WithinAbs(6.0, 0.001));
    CHECK_THAT(tags[0].value, ContainsSubstring("DURATION=30"));
    CHECK(tags[1].kind == ManifestTagKind::CueIn);
    CHECK_THAT(tags[1].pts, WithinAbs(12.0, 0.001));
}

TEST_CASE("HLS — DISCONTINUITY-SEQUENCE updates disc_seq", "[manifest][hls]")
{
    const std::string hls = R"(#EXTM3U
#EXT-X-DISCONTINUITY-SEQUENCE:5
#EXTINF:6.0,
seg001.ts
#EXT-X-DISCONTINUITY
#EXTINF:6.0,
seg002.ts
)";
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text(hls));
    const auto& tags = mp.tags();
    // Should have DiscontinuitySequence + Discontinuity
    REQUIRE(tags.size() == 2);
    CHECK(tags[0].kind == ManifestTagKind::DiscontinuitySequence);
    CHECK(tags[0].disc_seq == 5);
    CHECK(tags[1].kind == ManifestTagKind::Discontinuity);
    CHECK(tags[1].disc_seq == 5);  // inherited from running disc_seq
}

TEST_CASE("HLS — EXT-X-MAP tag captured", "[manifest][hls]")
{
    const std::string hls = R"(#EXTM3U
#EXT-X-MAP:URI="init-0.mp4"
#EXTINF:6.0,
seg001.m4s
)";
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text(hls));
    const auto& tags = mp.tags();
    REQUIRE(tags.size() == 1);
    CHECK(tags[0].kind == ManifestTagKind::Map);
    CHECK_THAT(tags[0].value, ContainsSubstring("init-0.mp4"));
}

TEST_CASE("HLS — PROGRAM-DATE-TIME tag captured", "[manifest][hls]")
{
    const std::string hls = R"(#EXTM3U
#EXT-X-PROGRAM-DATE-TIME:2024-03-15T12:00:00.000Z
#EXTINF:6.0,
seg001.ts
)";
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text(hls));
    const auto& tags = mp.tags();
    REQUIRE(tags.size() == 1);
    CHECK(tags[0].kind == ManifestTagKind::ProgramDateTime);
    CHECK_THAT(tags[0].value, ContainsSubstring("2024-03-15"));
}

TEST_CASE("HLS — unknown proprietary EXT-X tags captured", "[manifest][hls]")
{
    const std::string hls = R"(#EXTM3U
#EXT-X-CUSTOM-VENDOR:value=abc
#EXTINF:6.0,
seg001.ts
)";
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text(hls));
    const auto& tags = mp.tags();
    REQUIRE(tags.size() == 1);
    CHECK(tags[0].kind == ManifestTagKind::Unknown);
    CHECK_THAT(tags[0].raw, ContainsSubstring("EXT-X-CUSTOM-VENDOR"));
    CHECK_THAT(tags[0].value, ContainsSubstring("value=abc"));
}

TEST_CASE("HLS — standard housekeeping tags not captured as events", "[manifest][hls]")
{
    const std::string hls = R"(#EXTM3U
#EXT-X-VERSION:3
#EXT-X-TARGETDURATION:6
#EXT-X-MEDIA-SEQUENCE:0
#EXT-X-PLAYLIST-TYPE:VOD
#EXT-X-KEY:METHOD=AES-128,URI="key.bin"
#EXTINF:6.0,
seg001.ts
#EXT-X-ENDLIST
)";
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text(hls));
    CHECK(mp.tags().empty());
}

TEST_CASE("HLS — PTS accumulates correctly across multiple segments", "[manifest][hls]")
{
    const std::string hls = R"(#EXTM3U
#EXTINF:2.0,
a.ts
#EXTINF:3.5,
b.ts
#EXTINF:1.5,
c.ts
#EXT-X-DISCONTINUITY
#EXTINF:4.0,
d.ts
)";
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text(hls));
    const auto& tags = mp.tags();
    REQUIRE(tags.size() == 1);
    // DISCONTINUITY appears after 2.0+3.5+1.5 = 7.0 seconds
    CHECK_THAT(tags[0].pts, WithinAbs(7.0, 0.001));
}

TEST_CASE("HLS — empty playlist returns true with no tags", "[manifest][hls]")
{
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text("#EXTM3U\n"));
    CHECK(mp.tags().empty());
}

TEST_CASE("HLS — disc_seq_at() returns correct sequence number at given PTS", "[manifest][hls]")
{
    const std::string hls = R"(#EXTM3U
#EXT-X-DISCONTINUITY-SEQUENCE:2
#EXTINF:10.0,
seg001.ts
#EXT-X-DISCONTINUITY-SEQUENCE:3
#EXTINF:10.0,
seg002.ts
)";
    ManifestParser mp;
    REQUIRE(mp.parse_hls_text(hls));
    CHECK(mp.disc_seq_at(0.0)  == 2);
    CHECK(mp.disc_seq_at(5.0)  == 2);
    CHECK(mp.disc_seq_at(10.0) == 3);
    CHECK(mp.disc_seq_at(20.0) == 3);
}

// ── DASH parse_dash_text() ────────────────────────────────────────────────────

TEST_CASE("DASH — Period boundary tag created for second period", "[manifest][dash]")
{
    const std::string mpd = R"(<?xml version="1.0"?>
<MPD>
  <Period id="p0" duration="PT30S">
  </Period>
  <Period id="content" start="PT30S" duration="PT60S">
  </Period>
</MPD>
)";
    ManifestParser mp;
    REQUIRE(mp.parse_dash_text(mpd));
    const auto& tags = mp.tags();
    REQUIRE(tags.size() == 1);
    CHECK(tags[0].kind == ManifestTagKind::Period);
    CHECK_THAT(tags[0].pts, WithinAbs(30.0, 0.001));
    CHECK_THAT(tags[0].raw, ContainsSubstring("content"));
}

TEST_CASE("DASH — multiple periods generate one tag per boundary", "[manifest][dash]")
{
    const std::string mpd = R"(<?xml version="1.0"?>
<MPD>
  <Period duration="PT10S"/>
  <Period duration="PT20S"/>
  <Period duration="PT15S"/>
</MPD>
)";
    ManifestParser mp;
    REQUIRE(mp.parse_dash_text(mpd));
    // Periods 1 and 2 generate boundary tags (Period 0 is the start, no tag)
    CHECK(mp.tags().size() == 2);
}

TEST_CASE("DASH — EventStream/Event captured with correct PTS", "[manifest][dash]")
{
    const std::string mpd = R"(<?xml version="1.0"?>
<MPD>
  <Period start="PT0S">
    <EventStream schemeIdUri="urn:scte:scte35:2014:xml+bin" timescale="90000">
      <Event presentationTime="900000" id="1"/>
      <Event presentationTime="1800000" id="2"/>
    </EventStream>
  </Period>
</MPD>
)";
    ManifestParser mp;
    REQUIRE(mp.parse_dash_text(mpd));
    const auto& tags = mp.tags();
    REQUIRE(tags.size() == 2);
    CHECK(tags[0].kind == ManifestTagKind::Event);
    CHECK_THAT(tags[0].pts, WithinAbs(10.0, 0.001));   // 900000/90000
    CHECK_THAT(tags[1].pts, WithinAbs(20.0, 0.001));   // 1800000/90000
    CHECK_THAT(tags[0].raw, ContainsSubstring("scte35"));
}

TEST_CASE("DASH — empty MPD returns true with no tags", "[manifest][dash]")
{
    ManifestParser mp;
    REQUIRE(mp.parse_dash_text("<MPD></MPD>"));
    CHECK(mp.tags().empty());
}

TEST_CASE("DASH — malformed XML returns false", "[manifest][dash]")
{
    ManifestParser mp;
    CHECK_FALSE(mp.parse_dash_text("this is not xml at all <<<"));
}

TEST_CASE("DASH — tags sorted by PTS when periods have explicit start times", "[manifest][dash]")
{
    const std::string mpd = R"(<?xml version="1.0"?>
<MPD>
  <Period id="p0" start="PT0S" duration="PT60S"/>
  <Period id="p1" start="PT60S" duration="PT30S"/>
  <Period id="p2" start="PT90S" duration="PT30S"/>
</MPD>
)";
    ManifestParser mp;
    REQUIRE(mp.parse_dash_text(mpd));
    const auto& tags = mp.tags();
    REQUIRE(tags.size() == 2);
    CHECK_THAT(tags[0].pts, WithinAbs(60.0, 0.001));
    CHECK_THAT(tags[1].pts, WithinAbs(90.0, 0.001));
}

// ── DASH variant extraction ───────────────────────────────────────────────────

TEST_CASE("DASH — Representations parsed with attributes", "[manifest][dash][variants]")
{
    const std::string mpd = R"(<?xml version="1.0"?>
<MPD>
  <Period>
    <AdaptationSet mimeType="video/mp4" codecs="avc1.640028">
      <Representation id="hi"  bandwidth="6000000" width="1920" height="1080">
        <BaseURL>video_1080p.mp4</BaseURL>
      </Representation>
      <Representation id="lo"  bandwidth="1500000" width="1280" height="720">
        <BaseURL>video_720p.mp4</BaseURL>
      </Representation>
    </AdaptationSet>
  </Period>
</MPD>
)";
    ManifestParser mp;
    REQUIRE(mp.parse_dash_text(mpd, "https://cdn.example.com/stream.mpd"));

    const auto& vs = mp.variants();
    REQUIRE(vs.size() == 2);

    CHECK(vs[0].bandwidth == 6'000'000);
    CHECK(vs[0].width  == 1920);
    CHECK(vs[0].height == 1080);
    CHECK_THAT(vs[0].codecs, ContainsSubstring("avc1"));
    CHECK_THAT(vs[0].url,    ContainsSubstring("video_1080p.mp4"));

    CHECK(vs[1].bandwidth == 1'500'000);
    CHECK(vs[1].width  == 1280);
    CHECK(vs[1].height == 720);
}

TEST_CASE("DASH — non-video AdaptationSets ignored for variants", "[manifest][dash][variants]")
{
    const std::string mpd = R"(<?xml version="1.0"?>
<MPD>
  <Period>
    <AdaptationSet mimeType="audio/mp4" codecs="mp4a.40.2">
      <Representation id="audio" bandwidth="128000">
        <BaseURL>audio.mp4</BaseURL>
      </Representation>
    </AdaptationSet>
    <AdaptationSet mimeType="video/mp4">
      <Representation id="v" bandwidth="3000000" width="1280" height="720">
        <BaseURL>video.mp4</BaseURL>
      </Representation>
    </AdaptationSet>
  </Period>
</MPD>
)";
    ManifestParser mp;
    REQUIRE(mp.parse_dash_text(mpd));
    const auto& vs = mp.variants();
    REQUIRE(vs.size() == 1);
    CHECK(vs[0].bandwidth == 3'000'000);
}

TEST_CASE("DASH — BaseURL stacks hierarchically (MPD → Period → Representation)",
          "[manifest][dash][variants]")
{
    const std::string mpd = R"(<?xml version="1.0"?>
<MPD>
  <BaseURL>https://cdn.example.com/movie/</BaseURL>
  <Period>
    <BaseURL>p0/</BaseURL>
    <AdaptationSet mimeType="video/mp4">
      <Representation id="v" bandwidth="2000000" width="1280" height="720">
        <BaseURL>video.mp4</BaseURL>
      </Representation>
    </AdaptationSet>
  </Period>
</MPD>
)";
    ManifestParser mp;
    REQUIRE(mp.parse_dash_text(mpd));
    REQUIRE(mp.variants().size() == 1);
    CHECK(mp.variants()[0].url == "https://cdn.example.com/movie/p0/video.mp4");
}

TEST_CASE("DASH — Representations without BaseURL still captured (URL empty)",
          "[manifest][dash][variants]")
{
    const std::string mpd = R"(<?xml version="1.0"?>
<MPD>
  <Period>
    <AdaptationSet mimeType="video/mp4">
      <Representation id="v" bandwidth="3000000" width="1280" height="720">
        <SegmentTemplate media="$RepresentationID$/seg-$Number$.m4s" startNumber="1" timescale="90000" duration="540000"/>
      </Representation>
    </AdaptationSet>
  </Period>
</MPD>
)";
    ManifestParser mp;
    REQUIRE(mp.parse_dash_text(mpd));
    REQUIRE(mp.variants().size() == 1);
    CHECK(mp.variants()[0].bandwidth == 3'000'000);
    CHECK(mp.variants()[0].url.empty());
}

TEST_CASE("DASH — width/height/codecs inherited from AdaptationSet when omitted on Representation",
          "[manifest][dash][variants]")
{
    const std::string mpd = R"(<?xml version="1.0"?>
<MPD>
  <Period>
    <AdaptationSet mimeType="video/mp4" codecs="hev1.2.4.L150.B0" width="3840" height="2160">
      <Representation id="v" bandwidth="20000000">
        <BaseURL>4k.mp4</BaseURL>
      </Representation>
    </AdaptationSet>
  </Period>
</MPD>
)";
    ManifestParser mp;
    REQUIRE(mp.parse_dash_text(mpd));
    REQUIRE(mp.variants().size() == 1);
    const auto& v = mp.variants()[0];
    CHECK(v.width  == 3840);
    CHECK(v.height == 2160);
    CHECK_THAT(v.codecs, ContainsSubstring("hev1"));
}
