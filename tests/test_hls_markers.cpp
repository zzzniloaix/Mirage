#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "HlsConformProbe.h"
#include "HlsMarkers.h"
#include "TestEncode.h"
#include "TestTemp.h"

#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using Catch::Approx;
using namespace hls;

// Every playlist here is made up: the durations, dates, IDs and the tag name
// (EXT-X-CUE-MARKER stands for any packager's own cue tag).

namespace {

// A VOD playlist dated from 2024-03-01T12:00:00Z by one PROGRAM-DATE-TIME,
// segments of the given durations; `tags[i]` goes before segment i
// (tags.size() == durations.size() + 1: the last after every segment).
std::string dated_vod(const std::vector<double>& durations, const std::vector<std::string>& tags, const char* name)
{
    std::string s = "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:6\n#EXT-X-PLAYLIST-TYPE:VOD\n"
                    "#EXT-X-PROGRAM-DATE-TIME:2024-03-01T12:00:00.000Z\n";
    for (std::size_t i = 0; i < durations.size(); ++i) {
        if (i < tags.size() && !tags[i].empty()) s += tags[i] + "\n";
        s += std::format("#EXTINF:{},\n{}{}.mp4\n", durations[i], name, i);
    }
    if (tags.size() > durations.size() && !tags.back().empty()) s += tags.back() + "\n";
    return s + "#EXT-X-ENDLIST\n";
}

// Video: 6 s segments; audio: the same timeline with an extra cut 0.36 s
// after 12 s, so the audio boundary nearest a break at 12.4 s is 12.36.
const std::vector<double> kVideo = { 6, 6, 6, 6 };
const std::vector<double> kAudio = { 6, 6, 0.36, 5.64, 6 };

std::string interstitial(const char* id, const char* date)
{
    return std::format("#EXT-X-DATERANGE:ID=\"{}\",CLASS=\"com.apple.hls.interstitial\",START-DATE=\"{}\","
                       "X-ASSET-URI=\"ad.m3u8\"", id, date);
}

std::vector<MarkerIn> renditions(const std::string& video, const std::string& audio)
{
    const std::vector<std::string> tags = { "EXT-X-CUE-MARKER" };
    return { { "v0 1080p", Track::Video, parse_media_timeline(video, "https://example.com/v.m3u8", tags) },
             { "audio aac/en", Track::Audio, parse_media_timeline(audio, "https://example.com/a.m3u8", tags) } };
}

} // namespace

TEST_CASE("parse_media_timeline — EXT-X-DATERANGE and tags asked for", "[hls_markers][playlist]")
{
    const std::string text = dated_vod({ 6, 6 }, { "", interstitial("break-1", "2024-03-01T12:00:06.000Z") + "\n"
                                                         "#EXT-X-CUE-MARKER:ID=\"break-1\"\n#EXT-X-OTHER:1", "" },
                                       "s");
    const MediaTimeline plain = parse_media_timeline(text, "https://example.com/p.m3u8");
    REQUIRE(plain.markers.size() == 1);   // the DATERANGE only

    const MediaTimeline tl = parse_media_timeline(text, "https://example.com/p.m3u8", { "EXT-X-CUE-MARKER" });
    REQUIRE(tl.ok);
    REQUIRE(tl.markers.size() == 2);
    const Marker& dr = tl.markers[0];
    CHECK(dr.tag == "EXT-X-DATERANGE");
    CHECK(dr.id == "break-1");
    CHECK(dr.cls == "com.apple.hls.interstitial");
    CHECK(dr.start_date == Approx(1709294406.0));
    CHECK(std::isnan(dr.duration));
    CHECK(dr.before == 1);
    const Marker& tag = tl.markers[1];
    CHECK(tag.tag == "EXT-X-CUE-MARKER");
    CHECK(tag.id == "break-1");
    CHECK(tag.before == 1);
    CHECK(tag.value == "ID=\"break-1\"");
    CHECK(playlist_time_of_date(tl, dr.start_date) == Approx(6.0));

    // A tag without an ID is known by its value; a DATERANGE without one is a problem.
    const MediaTimeline bare = parse_media_timeline(
        "#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXT-X-CUE-MARKER:42\n#EXT-X-DATERANGE:START-DATE=\"x\"\n"
        "#EXTINF:6,\na.ts\n#EXT-X-ENDLIST\n", "https://example.com/p.m3u8", { "EXT-X-CUE-MARKER" });
    REQUIRE(bare.markers.size() == 2);
    CHECK(bare.markers[0].id == "42");
    CHECK(bare.markers[0].before == 0);
    CHECK(bare.problems.size() == 2);   // no ID, no START-DATE that parses
}

TEST_CASE("analyze_markers — a tag one segment early is an error", "[hls_markers]")
{
    // The break at 12.4 s (the video's DATERANGE). Audio boundaries: 12.0 and 12.36.
    const std::string video = dated_vod(kVideo, { "", "", interstitial("break-1", "2024-03-01T12:00:12.4Z"), "", "" }, "v");

    SECTION("before the segment nearest the break: aligned")
    {
        const std::string audio = dated_vod(kAudio, { "", "", "", "#EXT-X-CUE-MARKER:ID=\"break-1\"", "", "" }, "a");
        const MarkerReport r = analyze_markers(renditions(video, audio));
        REQUIRE(r.events.size() == 1);
        CHECK(r.reference == 0);
        CHECK(r.events[0].time == Approx(12.4));
        CHECK(r.errors == 0);
        REQUIRE(r.events[0].places.size() == 2);
        const MarkerPlace& a = r.events[0].places[1];
        CHECK(a.at == Approx(12.36));
        CHECK(a.nearest == Approx(12.36));
        CHECK(a.sev == Severity::Ok);
        // The video's START-DATE is 0.4 s into a 6 s segment: off a boundary.
        CHECK(r.warnings == 1);
        CHECK(r.events[0].places[0].sev == Severity::Warning);
    }
    SECTION("one segment before it: an error naming the segments")
    {
        const std::string audio = dated_vod(kAudio, { "", "", "#EXT-X-CUE-MARKER:ID=\"break-1\"", "", "", "" }, "a");
        const MarkerReport r = analyze_markers(renditions(video, audio));
        CHECK(r.errors == 1);
        REQUIRE(!r.findings.empty());
        CHECK(r.findings[0].sev == Severity::Error);
        CHECK(r.findings[0].title == "break-1: #EXT-X-CUE-MARKER in audio aac/en is one segment early");
        CHECK(r.findings[0].renditions == std::vector<int>{ 1 });
        const MarkerPlace& a = r.events[0].places[1];
        CHECK(a.at == Approx(12.0));
        CHECK(a.before == 2);
        CHECK(a.nearest_index == 3);
        CHECK(a.sev == Severity::Error);
        const std::string js = markers_json(renditions(video, audio), r);
        CHECK(js.find("\"before_sequence\": 2") != std::string::npos);
        CHECK(js.find("\"nearest_sequence\": 3") != std::string::npos);
    }
}

TEST_CASE("analyze_markers — on boundaries, missing markers, other dates", "[hls_markers]")
{
    // The break on a video boundary (12 s), and on the audio one at 12.0.
    const std::string video = dated_vod(
        kVideo, { "#EXT-X-DATERANGE:ID=\"chapter-1\",START-DATE=\"2024-03-01T12:00:00.3Z\",DURATION=24",
                  "", interstitial("break-1", "2024-03-01T12:00:12Z"), interstitial("break-2", "2024-03-01T12:00:18Z"),
                  "" }, "v");
    const std::string audio = dated_vod(kAudio, { "", "", "#EXT-X-CUE-MARKER:ID=\"break-1\"", "", "", "" }, "a");
    const MarkerReport r = analyze_markers(renditions(video, audio));
    REQUIRE(r.events.size() == 3);
    CHECK(r.errors == 0);
    // break-2 is not marked in audio, which marks break-1: a warning. The
    // chapter is not a splice point: not needed in audio, and off a boundary is fine.
    CHECK(r.warnings == 1);
    REQUIRE(!r.findings.empty());
    CHECK(r.findings[0].title == "break-2: no #EXT-X-CUE-MARKER in audio aac/en");
    for (const auto& ev : r.events)
        if (ev.id == "chapter-1") {
            CHECK(ev.missing.empty());
            CHECK_FALSE(ev.splice);
        }
}

TEST_CASE("analyze_markers — no date: the reference's tag sets the time", "[hls_markers]")
{
    const std::string video = dated_vod(kVideo, { "", "", "#EXT-X-CUE-MARKER:ID=\"b\"", "", "" }, "v");
    const std::string late = dated_vod(kAudio, { "", "", "", "", "#EXT-X-CUE-MARKER:ID=\"b\"", "" }, "a");
    const MarkerReport r = analyze_markers(renditions(video, late));
    CHECK(r.events.at(0).time == Approx(12.0));
    CHECK(r.events.at(0).time_from == "#EXT-X-CUE-MARKER in v0 1080p");
    CHECK(r.errors == 1);
    CHECK(r.findings.at(0).title == "b: #EXT-X-CUE-MARKER in audio aac/en is 2 segments late");
}

TEST_CASE("analyze_markers — a post-roll break before discontinuities", "[hls_markers]")
{
    // The break at 18 s, then end pieces after EXT-X-DISCONTINUITY, which
    // carry no date: the break is still placed by the segments before.
    const std::string post = interstitial("post", "2024-03-01T12:00:18Z") + "\n#EXT-X-DISCONTINUITY";
    const std::string video = dated_vod({ 6, 6, 6, 3, 3 }, { "", "", "", post, "#EXT-X-DISCONTINUITY", "" }, "v");
    // Audio boundaries at 12, 17.7 and 18.03: 18.03 is nearest 18.
    const std::vector<double> audio_d = { 6, 6, 5.7, 0.33, 3, 3 };
    SECTION("at the boundary nearest the break")
    {
        const std::string audio = dated_vod(
            audio_d, { "", "", "", "", "#EXT-X-CUE-MARKER:ID=\"post\"\n#EXT-X-DISCONTINUITY", "#EXT-X-DISCONTINUITY", "" },
            "a");
        const MarkerReport r = analyze_markers(renditions(video, audio));
        CHECK(r.events.at(0).time == Approx(18.0));
        CHECK(r.events.at(0).splice);
        CHECK(r.errors == 0);
        CHECK(r.warnings == 0);
    }
    SECTION("300 ms early")
    {
        const std::string audio = dated_vod(
            audio_d, { "", "", "", "#EXT-X-CUE-MARKER:ID=\"post\"", "#EXT-X-DISCONTINUITY", "#EXT-X-DISCONTINUITY", "" },
            "a");
        const MarkerReport r = analyze_markers(renditions(video, audio));
        CHECK(r.errors == 1);
        CHECK(r.findings.at(0).title == "post: #EXT-X-CUE-MARKER in audio aac/en is one segment early");
        CHECK(r.events.at(0).places.at(1).at == Approx(17.7));
    }
}

TEST_CASE("analyze_markers — the last main segment is short, then a new period", "[hls_markers]")
{
    // The break at 18.039 s. Audio: 6 + 6 + 5.43 = 17.43, then a short last
    // main segment (0.609 s, longer than half a second) ending at 18.039; a
    // discontinuity and a new init segment start the end pieces. The
    // boundary 609 ms before the break is not the nearest: the end of the
    // short segment, on it, is.
    const std::string post = interstitial("post", "2024-03-01T12:00:18.039Z");
    const std::string video = dated_vod({ 6, 6, 6.039, 3 }, { "", "", "", post + "\n#EXT-X-DISCONTINUITY", "" }, "v");
    const std::string next = "#EXT-X-DISCONTINUITY\n#EXT-X-MAP:URI=\"end_init.mp4\"";
    const std::vector<double> audio_d = { 6, 6, 5.43, 0.609, 3 };
    const auto audio = [&](int marker_before) {
        std::vector<std::string> tags(audio_d.size() + 1);
        tags[4] = next;
        const std::string mk = "#EXT-X-CUE-MARKER:ID=\"post\"";
        auto& t = tags[static_cast<std::size_t>(marker_before)];
        t = t.empty() ? mk : mk + "\n" + t;
        return dated_vod(audio_d, tags, "a");
    };
    SECTION("before the short segment: one segment early")
    {
        const MarkerReport r = analyze_markers(renditions(video, audio(3)));
        CHECK(r.errors == 1);
        const MarkerPlace& a = r.events.at(0).places.at(1);
        CHECK(a.at == Approx(17.43));
        CHECK(a.nearest == Approx(18.039));
        CHECK(a.nearest_index == 4);
        CHECK(r.findings.at(0).title == "post: #EXT-X-CUE-MARKER in audio aac/en is one segment early");
    }
    SECTION("before the new period: aligned")
    {
        const MarkerReport r = analyze_markers(renditions(video, audio(4)));
        CHECK(r.errors == 0);
        CHECK(r.warnings == 0);
    }
}

TEST_CASE("readable_without_key — sample encryption only", "[hls_markers][encrypted]")
{
    CHECK(conformprobe::readable_without_key("SAMPLE-AES"));
    CHECK(conformprobe::readable_without_key("SAMPLE-AES-CTR"));
    CHECK(conformprobe::readable_without_key("ISO-23001-7"));
    CHECK_FALSE(conformprobe::readable_without_key("AES-128"));
    CHECK_FALSE(conformprobe::readable_without_key(""));
}

TEST_CASE("read_segment — CENC fMP4 read without its key", "[hls_markers][encrypted]")
{
    const auto path = testtemp::dir() / "cenc_frag.mp4";
    testencode::Encode e;
    e.format = "mp4";
    e.frames = 48;
    e.gop = 24;
    e.mux_opts = { { "movflags", "frag_keyframe+empty_moov+default_base_moof" },
                   { "encryption_scheme", "cenc-aes-ctr" },
                   { "encryption_key", "00112233445566778899aabbccddeeff" },
                   { "encryption_kid", "ffeeddccbbaa99887766554433221100" } };
    if (!testencode::encode(path, e)) SKIP("libx264 is not available");
    std::ifstream f(path, std::ios::binary);
    const std::string bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    REQUIRE(bytes.find("encv") != std::string::npos);   // an encrypted sample entry
    REQUIRE(bytes.find("senc") != std::string::npos);

    const SegmentRead r = conformprobe::read_segment(reinterpret_cast<const std::uint8_t*>(bytes.data()), bytes.size(),
                                                     0, static_cast<std::int64_t>(bytes.size()), true);
    CHECK(r.error.empty());
    CHECK(r.encrypted);
    REQUIRE(r.video);
    CHECK(r.video->codec == "h264");
    CHECK(r.video->codec_string.starts_with("avc1."));   // the decoder configuration is clear
    CHECK(r.video->width == 64);
    CHECK(r.frames == 48);
    CHECK(r.keyframes >= 2);   // x264 may add a scene cut
    CHECK(r.first_key);
    CHECK(r.first == FirstPicture::Idr);                  // CENC keeps the NAL headers clear
    CHECK(r.media_duration == Approx(2.0).margin(0.001));
}

TEST_CASE("marker_tag_candidates: a packager's own tags with an ID, not RFC 8216's", "[hls][markers]")
{
    const std::string text =
        "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:6\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"x\"\n"              // GROUP-ID is not an ID
        "#EXT-X-DATERANGE:ID=\"break-1\",START-DATE=\"2024-03-01T12:00:06Z\"\n"
        "#EXTINF:6.0,\ns0.ts\n"
        "#EXT-X-CUE-MARKER:ID=\"break-1\",KIND=\"start\"\r\n"
        "#EXT-X-OTHER-CUE:TYPE=\"x\",ID=\"post\"\n"
        "#EXT-X-CUE-OUT:30\n"                                            // no ID: not offered
        "#EXT-X-CUE-MARKER:ID=\"break-2\"\n"
        "#EXTINF:6.0,\ns1.ts\n#EXT-X-ENDLIST\n";
    CHECK(marker_tag_candidates(text) == std::vector<std::string>{ "EXT-X-CUE-MARKER", "EXT-X-OTHER-CUE" });
    CHECK(marker_tag_candidates("#EXTM3U\n#EXTINF:6.0,\ns0.ts\n").empty());
}
