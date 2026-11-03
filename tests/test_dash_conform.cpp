// The DASH conformance check: the MPD read (inheritance, every addressing,
// templates, xs:duration, a Segment Index), the @bandwidth bucket model and
// the channel and SAP readings on numbers worked by hand, the verdict on
// hand-built measurements; then a package FFmpeg's dash muxer writes here
// (libx264 through libavformat, in the run's temporary directory), its MPD
// given wrong declarations, checked end to end by DashProbe.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "DashConform.h"
#include "DashConformProbe.h"
#include "TestEncode.h"
#include "TestTemp.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

using Catch::Approx;
using namespace dash;
namespace fs = std::filesystem;

namespace {

const Representation* rep_by_id(const Mpd& m, const std::string& id)
{
    for (const auto& r : m.reps)
        if (r.id == id) return &r;
    return nullptr;
}

const hls::Finding* find_finding(const Report& r, Severity sev, const std::string& title)
{
    for (const auto& f : r.findings)
        if (f.sev == sev && f.title == title) return &f;
    return nullptr;
}

std::string finding_list(const Report& r)
{
    std::string out;
    for (const auto& f : r.findings) out += std::format("{}: {}\n", hls::severity_name(f.sev), f.title);
    return out;
}

// A version 0 sidx box: (referenced_size, subsegment_duration, starts_with_SAP, SAP_type) per reference.
std::vector<std::uint8_t> sidx_box(std::uint32_t timescale, std::uint32_t ept, std::uint32_t first_offset,
                                   const std::vector<std::tuple<std::uint32_t, std::uint32_t, bool, int>>& refs)
{
    std::vector<std::uint8_t> b;
    auto u32 = [&](std::uint32_t v) {
        for (int s = 24; s >= 0; s -= 8) b.push_back(static_cast<std::uint8_t>(v >> s));
    };
    u32(0);
    b.insert(b.end(), { 's', 'i', 'd', 'x' });
    u32(0);   // version 0, flags
    u32(1);   // reference_ID
    u32(timescale);
    u32(ept);
    u32(first_offset);
    u32(static_cast<std::uint32_t>(refs.size()));   // reserved 0, reference_count
    for (const auto& [size, dur, sap, type] : refs) {
        u32(size & 0x7fffffff);
        u32(dur);
        u32((sap ? 0x80000000u : 0u) | (static_cast<std::uint32_t>(type) << 28));
    }
    const auto n = static_cast<std::uint32_t>(b.size());
    for (int i = 0; i < 4; ++i) b[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(n >> (24 - 8 * i));
    return b;
}

} // namespace

TEST_CASE("parse_duration reads xs:duration", "[dash_conform]")
{
    CHECK(parse_duration("PT634.566S") == Approx(634.566));
    CHECK(parse_duration("PT1H2M3.5S") == Approx(3723.5));
    CHECK(parse_duration("PT1M") == Approx(60.0));
    CHECK(parse_duration("P1DT2H") == Approx(93600.0));
    CHECK(parse_duration("P0Y0M0DT0H3M30.000S") == Approx(210.0));
    CHECK(parse_duration(" PT0S ") == 0.0);
    CHECK(std::isnan(parse_duration("P1Y")));       // no fixed length
    CHECK(std::isnan(parse_duration("P2M")));
    CHECK(std::isnan(parse_duration("1S")));
    CHECK(std::isnan(parse_duration("PT")));
    CHECK(std::isnan(parse_duration("PT1X")));
}

TEST_CASE("expand_template substitutes the identifiers", "[dash_conform]")
{
    CHECK(expand_template("$RepresentationID$/$Number%05d$.m4s", "v1", 7, 800000, 0) == "v1/00007.m4s");
    CHECK(expand_template("t-$Time$-b$Bandwidth$.m4s", "a", 0, 128000, 192512) == "t-192512-b128000.m4s");
    CHECK(expand_template("$$a$$$Number$", "x", 3, 0, 0) == "$a$3");
    CHECK(expand_template("$Unknown$-$Number", "x", 3, 0, 0) == "$Unknown$-$Number");
}

TEST_CASE("parse_mpd: SegmentTemplate@duration inherited from the AdaptationSet", "[dash_conform]")
{
    const Mpd m = parse_mpd(R"(<?xml version="1.0"?>
<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" mediaPresentationDuration="PT634.566S" minBufferTime="PT2.00S"
     profiles="urn:mpeg:dash:profile:isoff-live:2011" type="static">
 <BaseURL>./</BaseURL>
 <Period>
  <AdaptationSet mimeType="video/mp4" contentType="video" subsegmentAlignment="true" subsegmentStartsWithSAP="1" frameRate="30">
   <SegmentTemplate duration="120" timescale="30" media="$RepresentationID$/$RepresentationID$_$Number$.m4v"
                    startNumber="1" initialization="$RepresentationID$/$RepresentationID$_0.m4v"/>
   <Representation id="v1" codecs="avc1.64001f" bandwidth="3134488" width="1024" height="576" sar="1:1"/>
  </AdaptationSet>
  <AdaptationSet mimeType="audio/mp4" contentType="audio" lang="en">
   <SegmentTemplate duration="192512" timescale="48000" media="$RepresentationID$/$Number$.m4a" startNumber="1"
                    initialization="$RepresentationID$/init.m4a"/>
   <Representation id="a1" codecs="mp4a.40.5" bandwidth="67071" audioSamplingRate="48000">
    <AudioChannelConfiguration schemeIdUri="urn:mpeg:dash:23003:3:audio_channel_configuration:2011" value="2"/>
   </Representation>
  </AdaptationSet>
 </Period>
</MPD>)",
                            "https://example.com/bbb/bbb.mpd");
    REQUIRE(m.ok);
    CHECK(m.problems.empty());
    CHECK(m.min_buffer_time == Approx(2.0));
    REQUIRE(m.periods.size() == 1);
    CHECK(m.periods[0].duration == Approx(634.566));
    REQUIRE(m.reps.size() == 2);
    REQUIRE(m.sets.size() == 2);
    CHECK(m.sets[0].aligned());

    const Representation& v = m.reps[0];
    CHECK(v.kind == Track::Video);
    CHECK(v.addressing == Addressing::Template);
    CHECK(v.mime_type == "video/mp4");
    CHECK(v.width == 1024);
    CHECK(v.frame_rate == Approx(30.0));
    CHECK(v.sar_num == 1);
    CHECK(v.subsegment_sap == 1);
    CHECK(v.start_with_sap == -1);
    CHECK(v.init_uri == "https://example.com/bbb/v1/v1_0.m4v");
    REQUIRE(v.segments.size() == 159);   // ⌈634.566 / 4⌉
    CHECK(v.segments[0].uri == "https://example.com/bbb/v1/v1_1.m4v");
    CHECK(v.segments[0].number == 1);
    CHECK(v.segments[158].uri == "https://example.com/bbb/v1/v1_159.m4v");
    CHECK(v.segments[158].start == Approx(632.0));
    CHECK(v.segments[158].seconds == Approx(77.0 / 30.0));   // to the Period's end, in whole ticks
    CHECK(v.total() == Approx(634.5667).margin(1e-3));

    const Representation& a = m.reps[1];
    CHECK(a.kind == Track::Audio);
    CHECK(a.lang == "en");
    CHECK(a.sampling_rate == 48000);
    REQUIRE(a.channels.size() == 1);
    CHECK(channel_count(a.channels[0]) == 2);
    CHECK(a.segments.size() == 159);   // ⌈634.566 / 4.0107⌉
    CHECK(a.segments[1].start == Approx(192512.0 / 48000.0));
}

TEST_CASE("parse_mpd: SegmentTimeline with @r, @r=-1, $Time$ and @presentationTimeOffset", "[dash_conform]")
{
    const Mpd m = parse_mpd(R"(<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="static" minBufferTime="PT4S"
     profiles="urn:mpeg:dash:profile:isoff-live:2011" mediaPresentationDuration="PT30S">
 <Period start="PT10S">
  <BaseURL>dash/</BaseURL>
  <AdaptationSet contentType="video" mimeType="video/mp4" segmentAlignment="true" startWithSAP="1">
   <SegmentTemplate timescale="600" presentationTimeOffset="600" media="x-$RepresentationID$-$Time$.m4s"
                    initialization="x-$RepresentationID$.init">
    <SegmentTimeline>
     <S t="600" d="2400" r="2"/>
     <S d="1200" r="-1"/>
    </SegmentTimeline>
   </SegmentTemplate>
   <Representation id="v" bandwidth="1000000" codecs="avc1.4d401f"/>
   <Representation id="w" bandwidth="2000000" codecs="avc1.4d401f">
    <SegmentTemplate media="w/$Number%03d$.m4s"/>
   </Representation>
  </AdaptationSet>
 </Period>
</MPD>)",
                            "https://h.example/a/manifest.mpd");
    REQUIRE(m.ok);
    REQUIRE(m.periods.size() == 1);
    CHECK(m.periods[0].start == Approx(10.0));
    CHECK(m.periods[0].duration == Approx(20.0));
    const Representation* v = rep_by_id(m, "v");
    REQUIRE(v);
    CHECK(v->addressing == Addressing::Timeline);
    CHECK(v->pto == 600);
    // 3 × 4 s, then 2 s segments to the Period's end: (20 − 12) / 2.
    REQUIRE(v->segments.size() == 7);
    CHECK(v->segments[0].uri == "https://h.example/a/dash/x-v-600.m4s");
    CHECK(v->segments[0].start == Approx(10.0));   // Period@start + (t − PTO) / timescale
    CHECK(v->segments[1].time == 3000);
    CHECK(v->segments[3].seconds == Approx(2.0));
    CHECK(v->segments[6].start == Approx(28.0));
    CHECK(v->init_uri == "https://h.example/a/dash/x-v.init");
    // The Representation's own SegmentTemplate gives @media; the rest is the AdaptationSet's.
    const Representation* w = rep_by_id(m, "w");
    REQUIRE(w);
    REQUIRE(w->segments.size() == 7);
    CHECK(w->segments[2].uri == "https://h.example/a/dash/w/003.m4s");
    CHECK(w->timescale == 600);
}

TEST_CASE("parse_mpd: SegmentList, SegmentBase, and what is missing", "[dash_conform]")
{
    const Mpd m = parse_mpd(R"(<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="static" mediaPresentationDuration="PT8S">
 <Period>
  <AdaptationSet contentType="video" mimeType="video/mp4">
   <Representation id="list" bandwidth="800000" codecs="avc1.64001e">
    <BaseURL>one.mp4</BaseURL>
    <SegmentList timescale="1000000" duration="2000000">
     <Initialization range="0-834"/>
     <SegmentURL mediaRange="835-187745"/>
     <SegmentURL mediaRange="187746-407176"/>
    </SegmentList>
   </Representation>
   <Representation id="base" bandwidth="400000" codecs="avc1.64001e">
    <BaseURL>od.mp4</BaseURL>
    <SegmentBase indexRange="860-1071" timescale="12800"><Initialization range="0-859"/></SegmentBase>
   </Representation>
   <Representation codecs="avc1.64001e"><BaseURL>x.mp4</BaseURL></Representation>
  </AdaptationSet>
 </Period>
</MPD>)",
                            "/media/pkg/manifest.mpd");
    REQUIRE(m.ok);
    // Neither @profiles nor @minBufferTime.
    CHECK(m.problems.size() == 2);
    const Representation* l = rep_by_id(m, "list");
    REQUIRE(l);
    CHECK(l->addressing == Addressing::List);
    CHECK(l->init_uri == "/media/pkg/one.mp4");
    CHECK(l->init_range.offset == 0);
    CHECK(l->init_range.length == 835);
    REQUIRE(l->segments.size() == 2);
    CHECK(l->segments[1].range.offset == 187746);
    CHECK(l->segments[1].range.length == 407176 - 187746 + 1);
    CHECK(l->segments[1].start == Approx(2.0));
    const Representation* b = rep_by_id(m, "base");
    REQUIRE(b);
    CHECK(b->addressing == Addressing::Base);
    CHECK(b->segments.empty());   // until its Segment Index is read
    CHECK(b->index_uri == "/media/pkg/od.mp4");
    CHECK(b->index_range.offset == 860);
    CHECK(b->index_range.length == 212);
    CHECK(b->init_range.length == 860);
    // No @id, no @bandwidth: both mandatory.
    REQUIRE(m.reps.size() == 3);
    CHECK(m.reps[2].problems.size() == 2);
}

TEST_CASE("parse_sidx and apply_sidx list a Segment Index's subsegments", "[dash_conform]")
{
    const auto box = sidx_box(1000, 0, 0, { { 1000, 2000, true, 1 }, { 2000, 2000, true, 1 }, { 1500, 2000, false, 0 } });
    REQUIRE(box.size() == 68);
    const Sidx s = parse_sidx(box.data(), box.size(), 200);
    REQUIRE(s.ok);
    CHECK(s.timescale == 1000);
    REQUIRE(s.refs.size() == 3);
    CHECK(s.refs[0].offset == 268);   // the byte after the box
    CHECK(s.refs[1].offset == 1268);
    CHECK(s.refs[2].offset == 3268);
    CHECK(s.refs[2].time == 4000);
    CHECK_FALSE(s.refs[2].sap);

    Representation r;
    r.timescale = 1000;
    r.index_uri = "od.mp4";
    apply_sidx(r, s, 0.0);
    REQUIRE(r.segments.size() == 3);
    CHECK(r.segments[1].range.offset == 1268);
    CHECK(r.segments[1].range.length == 2000);
    CHECK(r.segments[1].start == Approx(2.0));
    CHECK(r.segments[2].seconds == Approx(2.0));
    CHECK(r.segments[0].sap_type == 1);
    CHECK(r.segments[2].sap_type == 0);
    CHECK(r.segments[0].uri == "od.mp4");

    CHECK_FALSE(parse_sidx(box.data(), 40, 0).ok);   // cut short
}

TEST_CASE("parse_mpd: a dynamic MPD addressed by @duration lists the segments available", "[dash_conform]")
{
    const char* text = R"(<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="dynamic" profiles="urn:mpeg:dash:profile:isoff-live:2011"
     availabilityStartTime="1970-01-01T00:00:00Z" minBufferTime="PT2S" timeShiftBufferDepth="PT10S">
 <Period id="P0" start="PT0S">
  <AdaptationSet contentType="video" mimeType="video/mp4">
   <SegmentTemplate media="$RepresentationID$/$Number$.m4s" initialization="$RepresentationID$/init.mp4" duration="2" startNumber="0"/>
   <Representation id="V" bandwidth="300000" codecs="avc1.64001e"/>
  </AdaptationSet>
 </Period>
</MPD>)";
    const Mpd m = parse_mpd(text, "https://live.example/m.mpd", 1000.5);
    REQUIRE(m.ok);
    CHECK(m.dynamic);
    REQUIRE(m.reps.size() == 1);
    // Ended by 1000.5 s, within the last 10 s: 990–1000 s.
    const auto& segs = m.reps[0].segments;
    REQUIRE(segs.size() == 5);
    CHECK(segs.front().number == 495);
    CHECK(segs.back().number == 499);
    CHECK(segs.back().uri == "https://live.example/V/499.m4s");
    CHECK_FALSE(parse_mpd(text, "https://live.example/m.mpd").reps[0].problems.empty());   // no clock
}

TEST_CASE("bucket_model: @bandwidth as ISO/IEC 23009-1 defines it", "[dash_conform]")
{
    // One 2 s segment of 8 Mb, @minBufferTime 2 s: in by its end at 8 Mb / 4 s,
    // by its start at 8 Mb / 2 s.
    Bucket b = bucket_model({ 0.0 }, { 2.0 }, { 1'000'000 }, 2.0, 8e6 / 3.0);
    CHECK(b.need == Approx(2e6));
    CHECK(b.safe == Approx(4e6));
    CHECK(b.buffer == Approx(1.0));   // 8 Mb at 2.67 Mb/s: 3 s, 1 s more than its 2 s of play
    CHECK(b.complete());

    // Two: the pair needs 16 Mb in 6 s.
    b = bucket_model({ 0.0, 2.0 }, { 2.0, 2.0 }, { 1'000'000, 1'000'000 }, 2.0, 0.0);
    CHECK(b.need == Approx(16e6 / 6.0));
    CHECK(b.safe == Approx(4e6));
    CHECK(b.from == 0);
    CHECK(b.to == 1);
    CHECK(std::isnan(b.buffer));

    // Front-loaded: the first segment alone decides.
    b = bucket_model({ 0.0, 2.0, 4.0 }, { 2.0, 2.0, 2.0 }, { 3'000'000, 500'000, 500'000 }, 2.0, 0.0);
    CHECK(b.need == Approx(6e6));
    CHECK(b.safe == Approx(12e6));
    CHECK(b.from == 0);
    CHECK(b.to == 0);

    // A size unknown counts as nothing: a lower bound.
    b = bucket_model({ 0.0, 2.0 }, { 2.0, 2.0 }, { 1'000'000, -1 }, 2.0, 0.0);
    CHECK(b.sized == 1);
    CHECK_FALSE(b.complete());
    CHECK(b.need == Approx(2e6));

    // No @minBufferTime: nothing to compute.
    b = bucket_model({ 0.0 }, { 2.0 }, { 1'000'000 }, 0.0, 1e6);
    CHECK(b.need == 0.0);
}

TEST_CASE("channel_count reads the AudioChannelConfiguration schemes", "[dash_conform]")
{
    const std::string mpeg = "urn:mpeg:dash:23003:3:audio_channel_configuration:2011";
    const std::string cicp = "urn:mpeg:mpegB:cicp:ChannelConfiguration";
    const std::string dolby = "tag:dolby.com,2014:dash:audio_channel_configuration:2011";
    CHECK(channel_count({ mpeg, "2" }) == 2);
    CHECK(channel_count({ mpeg, "6" }) == 6);
    CHECK(channel_count({ cicp, "2" }) == 2);
    CHECK(channel_count({ cicp, "6" }) == 6);
    CHECK(channel_count({ cicp, "7" }) == 8);
    CHECK(channel_count({ cicp, "13" }) == 24);
    CHECK(channel_count({ cicp, "15" }) == -1);
    CHECK(channel_count({ dolby, "A000" }) == 2);   // L R
    CHECK(channel_count({ dolby, "F801" }) == 6);   // L C R Ls Rs LFE
    CHECK(channel_count({ dolby, "FA01" }) == 8);   // and the Lrs / Rrs pair
    CHECK(channel_count({ dolby, "zz" }) == -1);
    CHECK(channel_count({ "urn:other", "2" }) == -1);
}

TEST_CASE("sap_type from the first picture and the leading pictures", "[dash_conform]")
{
    using hls::FirstPicture;
    auto video = [](FirstPicture f, int leading, int rasl = 0) {
        hls::SegmentRead s;
        s.video = hls::VideoSeen{};
        s.first = f;
        s.leading = leading;
        s.rasl = rasl;
        s.frames = 10;
        return s;
    };
    CHECK(sap_type(video(FirstPicture::Idr, 0)) == 1);
    CHECK(sap_type(video(FirstPicture::Idr, 2)) == 2);
    CHECK(sap_type(video(FirstPicture::Cra, 0)) == 1);
    CHECK(sap_type(video(FirstPicture::Cra, 3, 3)) == 3);
    CHECK(sap_type(video(FirstPicture::Cra, 2, 0)) == 2);   // RADL only
    CHECK(sap_type(video(FirstPicture::Recovery, 1)) == 3);
    CHECK(sap_type(video(FirstPicture::Intra, 0)) == -1);
    CHECK(sap_type(video(FirstPicture::Other, 0)) == 0);
    hls::SegmentRead av1 = video(FirstPicture::Unknown, 0);
    av1.first_key = true;
    CHECK(sap_type(av1) == 1);
    av1.first_key = false;
    CHECK(sap_type(av1) == 0);
    hls::SegmentRead audio;
    audio.audio = hls::AudioSeen{};
    CHECK(sap_type(audio) == 1);
}

TEST_CASE("analyze_conformance compares every declaration with the media", "[dash_conform]")
{
    const Mpd m = parse_mpd(R"(<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" profiles="urn:mpeg:dash:profile:isoff-live:2011"
     type="static" mediaPresentationDuration="PT8S" minBufferTime="PT2S" maxSegmentDuration="PT2S">
 <Period id="p0">
  <AdaptationSet id="v" contentType="video" mimeType="video/mp4" segmentAlignment="true" startWithSAP="1" frameRate="25">
   <SegmentTemplate timescale="1000" duration="2000" media="$RepresentationID$/$Number$.m4s"
                    initialization="$RepresentationID$/init.mp4"/>
   <Representation id="hi" codecs="avc1.4d401e" bandwidth="1000000" width="1280" height="720" sar="1:1"/>
   <Representation id="lo" codecs="avc1.64001e" bandwidth="400000" width="640" height="360" sar="1:1">
    <SupplementalProperty schemeIdUri="urn:mpeg:mpegB:cicp:TransferCharacteristics" value="1"/>
   </Representation>
  </AdaptationSet>
  <AdaptationSet id="a" contentType="audio" mimeType="audio/mp4" lang="en">
   <SegmentTemplate timescale="48000" duration="96000" media="a/$Number$.m4s" initialization="a/init.mp4"/>
   <Representation id="aac" codecs="mp4a.40.2" bandwidth="64000" audioSamplingRate="48000">
    <AudioChannelConfiguration schemeIdUri="urn:mpeg:dash:23003:3:audio_channel_configuration:2011" value="6"/>
   </Representation>
  </AdaptationSet>
 </Period>
</MPD>)",
                            "https://example.com/p/m.mpd");
    REQUIRE(m.ok);
    REQUIRE(m.reps.size() == 3);
    for (const auto& r : m.reps) REQUIRE(r.segments.size() == 4);

    auto video_read = [](int index, const char* codec, int w, int h, int transfer, hls::FirstPicture first, double ept) {
        SegmentSeen s;
        s.read.index = index;
        s.read.video = hls::VideoSeen{ .codec = "h264", .codec_string = codec, .sample_entry = "avc1", .width = w,
                                       .height = h, .sar_num = 1, .sar_den = 1, .transfer = transfer, .bit_depth = 8 };
        s.read.first = first;
        s.read.first_key = first == hls::FirstPicture::Idr;
        s.read.frames = 50;
        s.read.media_duration = 2.0;
        s.read.frame_rate = 25.0;
        s.start = ept;
        return s;
    };
    std::vector<RepRead> reads(3);
    // hi: 2 Mb/s of segments under a 1 Mb/s @bandwidth; @codecs Main for High.
    reads[0].sizes = { 500'000, 500'000, 500'000, 500'000 };
    reads[0].reads = { video_read(0, "avc1.64001e", 1280, 720, 1, hls::FirstPicture::Idr, 0.0),
                       video_read(3, "avc1.64001e", 1280, 720, 1, hls::FirstPicture::Idr, 6.0) };
    // lo: the right rate and codecs; 352 lines, PQ under TransferCharacteristics 1,
    // its last segment starting on a P picture 1.2 s late.
    reads[1].sizes = { 100'000, 100'000, 100'000, 100'000 };
    reads[1].reads = { video_read(0, "avc1.64001e", 640, 352, 16, hls::FirstPicture::Idr, 0.0),
                       video_read(3, "avc1.64001e", 640, 352, 16, hls::FirstPicture::Other, 7.2) };
    // aac: stereo under a 6-channel configuration.
    reads[2].sizes = { 16'000, 16'000, 16'000, 16'000 };
    SegmentSeen a;
    a.read.index = 0;
    a.read.audio = hls::AudioSeen{ .codec = "aac", .codec_string = "mp4a.40.2", .channels = 2, .sample_rate = 48000 };
    a.read.audio_duration = 2.0;
    a.start = 0.0;
    reads[2].reads = { a };

    const Report r = analyze_conformance(m, reads);
    INFO(finding_list(r));
    REQUIRE(r.rows.size() == 3);
    const RepRow& hi = r.rows[0];
    CHECK(hi.cell(Check::Bandwidth).sev == Severity::Error);
    CHECK(hi.bucket.need == Approx(1.6e6));   // 16 Mb by the end of 8 s of play + 2 s of buffer
    CHECK(hi.cell(Check::Codecs).sev == Severity::Error);
    CHECK(hi.cell(Check::Resolution).sev == Severity::Ok);
    CHECK(hi.cell(Check::FrameRate).sev == Severity::Ok);
    CHECK(hi.cell(Check::Sap).sev == Severity::Ok);
    CHECK(hi.cell(Check::Alignment).sev == Severity::Error);
    const RepRow& lo = r.rows[1];
    CHECK(lo.cell(Check::Bandwidth).sev == Severity::Ok);
    CHECK(lo.bucket.need == Approx(3.2e6 / 10.0));
    CHECK(lo.cell(Check::Codecs).sev == Severity::Ok);
    CHECK(lo.cell(Check::Resolution).sev == Severity::Warning);
    CHECK(lo.cell(Check::Colour).sev == Severity::Error);
    CHECK(lo.cell(Check::Sap).sev == Severity::Error);
    CHECK(lo.cell(Check::Timeline).sev == Severity::Warning);
    const RepRow& aac = r.rows[2];
    CHECK(aac.cell(Check::Audio).sev == Severity::Warning);
    CHECK(aac.cell(Check::Bandwidth).sev == Severity::Ok);
    CHECK_FALSE(aac.cell(Check::Alignment).checked);

    CHECK(find_finding(r, Severity::Error, "@bandwidth 1.00 Mb/s below the 1.60 Mb/s its segments need with @minBufferTime 2.000 s"));
    CHECK(find_finding(r, Severity::Error, "@codecs names another profile than the stream's"));
    CHECK(find_finding(r, Severity::Error, "@startWithSAP 1 but 1 of the segments read start with a P / B picture"));
    CHECK(find_finding(r, Severity::Error, "TransferCharacteristics 1 but the stream's is 16"));
    CHECK(find_finding(r, Severity::Warning, "segments start up to 1200 ms from their @duration slot, over half a segment"));
    CHECK(find_finding(r, Severity::Warning, "AudioChannelConfiguration says 6 channels, the audio decodes to 2"));
    const hls::Finding* f = find_finding(r, Severity::Error, "aligned segments start up to 1200 ms apart in the media");
    REQUIRE(f);
    CHECK(f->renditions == std::vector<int>{ 0, 1 });
    CHECK(r.errors == 5);
    CHECK(r.warnings == 3);
}

TEST_CASE("analyze_conformance notes @bandwidths a hair under what the segments need once", "[dash_conform]")
{
    const Mpd m = parse_mpd(R"(<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" profiles="urn:mpeg:dash:profile:isoff-live:2011"
     type="static" mediaPresentationDuration="PT8S" minBufferTime="PT2S">
 <Period>
  <AdaptationSet contentType="video" mimeType="video/mp4" segmentAlignment="true">
   <SegmentTemplate timescale="1000" duration="2000" media="$RepresentationID$/$Number$.m4s"/>
   <Representation id="a" codecs="avc1.64001e" bandwidth="1596000"/>
   <Representation id="b" codecs="avc1.64001e" bandwidth="1592000"/>
   <Representation id="c" codecs="avc1.64001e" bandwidth="1500000"/>
  </AdaptationSet>
 </Period>
</MPD>)",
                            "https://example.com/m.mpd");
    REQUIRE(m.reps.size() == 3);
    // 4 Mb per 2 s segment: 16 Mb by the end of 8 s of play after 2 s of buffer, 1.6 Mb/s.
    std::vector<RepRead> reads(3);
    for (auto& r : reads) r.sizes = { 500'000, 500'000, 500'000, 500'000 };
    const Report r = analyze_conformance(m, reads);
    INFO(finding_list(r));
    CHECK(r.rows[0].cell(Check::Bandwidth).sev == Severity::Info);    // 0.25% short
    CHECK(r.rows[1].cell(Check::Bandwidth).sev == Severity::Info);    // 0.5%
    CHECK(r.rows[2].cell(Check::Bandwidth).sev == Severity::Error);   // 6.25%
    const hls::Finding* f = find_finding(r, Severity::Info, "@bandwidth of 2 Representations up to 0.5% below what the segments need");
    REQUIRE(f);
    CHECK(f->renditions == std::vector<int>{ 0, 1 });
    CHECK(f->detail.find("The most: b, 8000 b/s.") != std::string::npos);
    CHECK(find_finding(r, Severity::Error, "@bandwidth 1.50 Mb/s below the 1.60 Mb/s its segments need with @minBufferTime 2.000 s"));
    CHECK(r.errors == 1);
}

TEST_CASE("analyze_conformance names each Representation on the finding it belongs to", "[dash_conform]")
{
    // A and C: not declared aligned, the same note; B between them: declared
    // aligned over timelines that differ.
    const Mpd m = parse_mpd(R"(<MPD xmlns="urn:mpeg:dash:schema:mpd:2011" profiles="urn:mpeg:dash:profile:isoff-live:2011"
     type="static" mediaPresentationDuration="PT8S" minBufferTime="PT2S">
 <Period>
  <AdaptationSet contentType="video" mimeType="video/mp4">
   <SegmentTemplate timescale="1000" duration="2000" media="$RepresentationID$/$Number$.m4s"/>
   <Representation id="a1" codecs="avc1.64001e" bandwidth="1000000"/>
   <Representation id="a2" codecs="avc1.64001e" bandwidth="1000000"/>
  </AdaptationSet>
  <AdaptationSet contentType="video" mimeType="video/mp4" segmentAlignment="true">
   <Representation id="b1" codecs="avc1.64001e" bandwidth="1000000">
    <SegmentTemplate timescale="1000" duration="2000" media="b1/$Number$.m4s"/>
   </Representation>
   <Representation id="b2" codecs="avc1.64001e" bandwidth="1000000">
    <SegmentTemplate timescale="1000" duration="4000" media="b2/$Number$.m4s"/>
   </Representation>
  </AdaptationSet>
  <AdaptationSet contentType="video" mimeType="video/mp4">
   <SegmentTemplate timescale="1000" duration="2000" media="$RepresentationID$/$Number$.m4s"/>
   <Representation id="c1" codecs="avc1.64001e" bandwidth="1000000"/>
   <Representation id="c2" codecs="avc1.64001e" bandwidth="1000000"/>
  </AdaptationSet>
 </Period>
</MPD>)",
                            "https://example.com/m.mpd");
    REQUIRE(m.reps.size() == 6);
    const Report r = analyze_conformance(m, std::vector<RepRead>(6));
    INFO(finding_list(r));
    const hls::Finding* unset = find_finding(r, Severity::Info, "@segmentAlignment / @subsegmentAlignment not set");
    REQUIRE(unset);
    CHECK(unset->renditions == std::vector<int>{ 0, 1, 4, 5 });
    const hls::Finding* differ = find_finding(r, Severity::Error, "declared aligned, but the Representations' timelines differ");
    REQUIRE(differ);
    CHECK(differ->renditions == std::vector<int>{ 2, 3 });
}

TEST_CASE("DashProbe checks a package FFmpeg's dash muxer wrote, end to end", "[dash_conform][probe]")
{
    if (!avcodec_find_encoder_by_name("libx264")) SKIP("no H.264 encoder (libx264)");
    const fs::path dir = testtemp::path("dash_package");
    fs::create_directories(dir);
    // 2 s at 24 fps, a GOP (0.5 s) per segment.
    REQUIRE(testencode::encode(dir / "manifest.mpd",
                               { .format = "dash",
                                 .codec_opts = { { "x264-params", "scenecut=0" }, { "b", "200k" } },
                                 .mux_opts = { { "seg_duration", "0.5" }, { "use_template", "1" }, { "use_timeline", "1" } } }));
    std::string mpd = testencode::slurp(dir / "manifest.mpd");
    // A @bandwidth far too low, a Constrained Baseline level 1 @codecs.
    auto replace_attr = [&](const std::string& name, const std::string& value) {
        const auto at = mpd.find(" " + name + "=\"", mpd.find("<Representation"));
        REQUIRE(at != std::string::npos);
        const auto v = at + name.size() + 3;
        mpd.replace(v, mpd.find('"', v) - v, value);
    };
    replace_attr("bandwidth", "1000");
    replace_attr("codecs", "avc1.42e00a");
    std::ofstream(dir / "manifest.mpd") << mpd;

    DashProbe probe;
    probe.start((dir / "manifest.mpd").string(), { .segments = 0, .workers = 2 });
    const auto t0 = std::chrono::steady_clock::now();
    while (probe.running() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60))
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(probe.state() == DashProbe::State::Done);
    const DashProbe::Result res = probe.result();
    const Report& r = res.report;
    INFO(finding_list(r));
    REQUIRE(res.mpd.reps.size() == 1);
    const Representation& rep = res.mpd.reps[0];
    CHECK(rep.addressing == Addressing::Timeline);
    REQUIRE(rep.segments.size() == 4);
    REQUIRE(res.reads.size() == 1);
    const RepRead& rr = res.reads[0];
    CHECK(rr.reads.size() == 4);   // segments = 0: every one
    REQUIRE(rr.sizes.size() == 4);
    CHECK(rr.sizes[0] == static_cast<std::int64_t>(fs::file_size(dir / "chunk-stream0-00001.m4s")));
    for (const auto& s : rr.reads) {
        CHECK(s.read.error.empty());
        CHECK(s.start == Approx(rep.segments[static_cast<std::size_t>(s.read.index)].start).margin(1e-6));
    }

    REQUIRE(r.rows.size() == 1);
    const RepRow& row = r.rows[0];
    CHECK(row.cell(Check::Bandwidth).sev == Severity::Error);
    CHECK(row.cell(Check::Codecs).sev == Severity::Error);
    CHECK(row.cell(Check::Resolution).sev == Severity::Ok);
    // The muxer writes no @frameRate for a stream without avg_frame_rate.
    CHECK_FALSE(row.cell(Check::FrameRate).checked);
    CHECK(row.cell(Check::FrameRate).note == "Not declared.");
    CHECK(row.cell(Check::FrameRate).measured == "24.000");
    CHECK(row.cell(Check::Sap).sev == Severity::Ok);
    CHECK(row.cell(Check::Sap).measured == "1");
    CHECK(row.cell(Check::Timeline).sev == Severity::Ok);
    CHECK(r.errors == 2);
}
