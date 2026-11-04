// The HLS conformance check: the master's declarations, codec strings
// (parsed, and built from avcC / hvcC / av1C records and Annex B parameter
// sets), first pictures from NAL units, RFC 8216 peak / average segment bit
// rates, and the verdict on hand-built measurements with known numbers; then
// segments encoded here (libx264 / libx265 through libavformat, in the run's
// temporary directory) read from memory, and a whole local HLS package —
// two variant playlists cut by the hls muxer, a master written with wrong
// declarations — checked end to end by ConformProbe.

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "HlsConform.h"
#include "HlsConformProbe.h"
#include "TestEncode.h"
#include "TestTemp.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

using Catch::Approx;
using namespace hls;
namespace fs = std::filesystem;

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes cat(std::initializer_list<Bytes> parts)
{
    Bytes out;
    for (const auto& p : parts) out.insert(out.end(), p.begin(), p.end());
    return out;
}

// RBSP → NAL payload: an emulation prevention byte after two zeros before 0..3.
Bytes ep(const Bytes& rbsp)
{
    Bytes out;
    int zeros = 0;
    for (const auto b : rbsp) {
        if (zeros >= 2 && b <= 3) {
            out.push_back(3);
            zeros = 0;
        }
        out.push_back(b);
        zeros = b == 0 ? zeros + 1 : 0;
    }
    return out;
}

Bytes start_code(const Bytes& nal) { return cat({ { 0, 0, 0, 1 }, nal }); }

Bytes length_prefixed(const Bytes& nal)
{
    const auto n = static_cast<std::uint32_t>(nal.size());
    return cat({ { static_cast<std::uint8_t>(n >> 24), static_cast<std::uint8_t>(n >> 16),
                   static_cast<std::uint8_t>(n >> 8), static_cast<std::uint8_t>(n) }, nal });
}

// HEVC Main 10, Main tier, level 4.1 (123): general_profile_space 0, tier 0,
// profile_idc 2; compatibility flag 2 set; progressive, non-packed and
// frame-only constraint flags (0xB0).
const Bytes kPtlMain10 = { 0x02, 0x20, 0x00, 0x00, 0x00, 0xB0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7B };

const SegmentRead* find_read(const PlaylistRead& p, int index)
{
    for (const auto& r : p.reads) if (r.index == index) return &r;
    return nullptr;
}

const Finding* find_finding(const ConformReport& r, Severity sev, const std::string& part)
{
    for (const auto& f : r.findings)
        if (f.sev == sev && f.title.find(part) != std::string::npos) return &f;
    return nullptr;
}

std::string finding_list(const ConformReport& r)
{
    std::string s;
    for (const auto& f : r.findings) s += std::format("[{}] {}\n", severity_name(f.sev), f.title);
    return s;
}

} // namespace

// ── Master playlist ──────────────────────────────────────────────────────────

TEST_CASE("parse_attribute_list keeps commas inside quoted strings", "[hls_conform]")
{
    const auto a = parse_attribute_list(R"(BANDWIDTH=1280000, CODECS="avc1.4d401e,mp4a.40.2",RESOLUTION=640x360 ,X="")");
    REQUIRE(a.size() == 4);
    CHECK(a[0] == std::pair<std::string, std::string>{ "BANDWIDTH", "1280000" });
    CHECK(a[1] == std::pair<std::string, std::string>{ "CODECS", "avc1.4d401e,mp4a.40.2" });
    CHECK(a[2] == std::pair<std::string, std::string>{ "RESOLUTION", "640x360" });
    CHECK(a[3] == std::pair<std::string, std::string>{ "X", "" });
}

TEST_CASE("parse_master_decl reads every declaration and resolves URIs", "[hls_conform]")
{
    const std::string text =
        "#EXTM3U\n"
        "#EXT-X-VERSION:6\n"
        "#EXT-X-INDEPENDENT-SEGMENTS\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aud1\",NAME=\"English\",LANGUAGE=\"en\",CHANNELS=\"2\",URI=\"a1/prog_index.m3u8\"\n"
        "#EXT-X-MEDIA:TYPE=CLOSED-CAPTIONS,GROUP-ID=\"cc1\",NAME=\"English\",INSTREAM-ID=\"CC1\"\n"
        "#EXT-X-STREAM-INF:AVERAGE-BANDWIDTH=2168183,BANDWIDTH=2177116,CODECS=\"avc1.640020,mp4a.40.2\",RESOLUTION=960x540,"
        "FRAME-RATE=60.000,CLOSED-CAPTIONS=\"cc1\",AUDIO=\"aud1\"\n"
        "v5/prog_index.m3u8\n"
        "#EXT-X-I-FRAME-STREAM-INF:BANDWIDTH=186522,CODECS=\"avc1.64002a\",URI=\"v7/iframe_index.m3u8\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=8001098,CODECS=\"hvc1.2.4.L123.B0\",RESOLUTION=1920x1080,VIDEO-RANGE=PQ,AUDIO=\"aud1\"\n"
        "\n"
        "../hdr/v9.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=541052,AUDIO=\"aud1\"\n"
        "v5/prog_index.m3u8\n";
    const MasterDecl m = parse_master_decl(text, "https://example.com/streams/bipbop/master.m3u8");
    REQUIRE(m.ok);
    CHECK(m.problems.empty());
    CHECK(m.independent);
    REQUIRE(m.variants.size() == 3);
    const VariantDecl& v0 = m.variants[0];
    CHECK(v0.line == 6);
    CHECK(v0.uri == "https://example.com/streams/bipbop/v5/prog_index.m3u8");
    CHECK(v0.bandwidth == 2177116);
    CHECK(v0.avg_bandwidth == 2168183);
    CHECK(v0.has_codecs);
    CHECK(v0.codecs == std::vector<std::string>{ "avc1.640020", "mp4a.40.2" });
    CHECK(v0.width == 960);
    CHECK(v0.height == 540);
    CHECK(v0.frame_rate == Approx(60.0));
    CHECK(v0.video_range.empty());
    CHECK(v0.audio == "aud1");
    CHECK(m.variants[1].uri == "https://example.com/streams/hdr/v9.m3u8");
    CHECK(m.variants[1].video_range == "PQ");
    CHECK(m.variants[1].avg_bandwidth == -1);
    CHECK_FALSE(m.variants[2].has_codecs);
    REQUIRE(m.media.size() == 2);
    CHECK(m.media[0].uri == "https://example.com/streams/bipbop/a1/prog_index.m3u8");
    CHECK(m.media[0].channels == "2");
    CHECK(m.media[1].uri.empty());

    // Each playlist once: the variants', then the audio renditions'.
    CHECK(m.playlists() == std::vector<std::string>{ "https://example.com/streams/bipbop/v5/prog_index.m3u8",
                                                    "https://example.com/streams/hdr/v9.m3u8",
                                                    "https://example.com/streams/bipbop/a1/prog_index.m3u8" });
}

TEST_CASE("parse_master_decl reports what the master gets wrong", "[hls_conform]")
{
    const MasterDecl m = parse_master_decl("#EXTM3U\n#EXT-X-STREAM-INF:CODECS=\"avc1.640020\",AUDIO=\"missing\"\nv.m3u8\n"
                                           "#EXT-X-STREAM-INF:BANDWIDTH=1\n",
                                           "https://example.com/master.m3u8");
    CHECK(m.ok);
    REQUIRE(m.problems.size() == 3);
    CHECK(m.problems[0].find("line 2: EXT-X-STREAM-INF without BANDWIDTH") == 0);
    CHECK(m.problems[1].find("line 4: EXT-X-STREAM-INF without a URI line") == 0);
    CHECK(m.problems[2].find("AUDIO=\"missing\" matches no EXT-X-MEDIA") != std::string::npos);

    const MasterDecl media = parse_master_decl("#EXTM3U\n#EXT-X-TARGETDURATION:6\n#EXTINF:6,\ns0.ts\n", "https://e.com/i.m3u8");
    CHECK_FALSE(media.ok);
    REQUIRE(media.problems.size() == 1);
    CHECK(media.problems[0].find("a media playlist") == 0);

    CHECK_FALSE(parse_master_decl("<html>", "https://e.com/x").ok);
}

// ── Codec strings ────────────────────────────────────────────────────────────

TEST_CASE("parse_codec reads RFC 6381 strings", "[hls_conform]")
{
    const CodecId avc = parse_codec("avc1.64002a");
    CHECK(avc.ok);
    CHECK(avc.kind == Track::Video);
    CHECK(avc.profile == 100);
    CHECK(avc.constraints == 0);
    CHECK(avc.level == 42);
    CHECK(describe_codec(avc) == "H.264 High, level 4.2");
    CHECK(describe_codec(parse_codec("avc1.42e01e")) == "H.264 Constrained Baseline, level 3.0");

    const CodecId legacy = parse_codec("avc1.66.30");
    CHECK(legacy.ok);
    CHECK(legacy.profile == 66);
    CHECK(legacy.level == 30);

    const CodecId hevc = parse_codec("hvc1.2.4.L123.B0");
    CHECK(hevc.ok);
    CHECK(hevc.profile == 2);
    CHECK(hevc.profile_space == 0);
    CHECK(hevc.compat == 4u);
    CHECK(hevc.tier == 0);
    CHECK(hevc.level == 123);
    CHECK(describe_codec(hevc) == "HEVC Main 10, Main tier, level 4.1");
    const CodecId high = parse_codec("hev1.A1.6.H150.90");
    CHECK(high.ok);
    CHECK(high.profile_space == 1);
    CHECK(high.profile == 1);
    CHECK(high.tier == 1);
    CHECK(high.level == 150);
    CHECK(describe_codec(high) == "HEVC Main, High tier, level 5");

    const CodecId av1 = parse_codec("av01.0.08M.10");
    CHECK(av1.ok);
    CHECK(av1.profile == 0);
    CHECK(av1.level == 8);
    CHECK(av1.tier == 0);
    CHECK(av1.bit_depth == 10);
    CHECK(describe_codec(av1) == "AV1 Main, level 4.0, 10-bit");

    CHECK(parse_codec("mp4a.40.2").object_type == 2);
    CHECK(describe_codec(parse_codec("mp4a.40.2")) == "AAC-LC");
    CHECK(describe_codec(parse_codec("mp4a.40.5")) == "HE-AAC");
    CHECK(describe_codec(parse_codec("mp4a.40.29")) == "HE-AAC v2");
    CHECK(describe_codec(parse_codec("mp4a.69")) == "MP3");
    CHECK(describe_codec(parse_codec("ec-3")) == "E-AC-3");
    CHECK(parse_codec("ac-3").kind == Track::Audio);
    const CodecId dv = parse_codec("dvh1.05.06");
    CHECK(dv.ok);
    CHECK(dv.dolby_vision);
    CHECK(dv.profile == 5);
    CHECK(dv.level == 6);
    CHECK(parse_codec("wvtt").kind == Track::Text);
    CHECK_FALSE(parse_codec("xyz1.2").ok);
    CHECK(parse_codec("xyz1.2").kind == Track::Unknown);
    CHECK_FALSE(parse_codec("avc1.6400").ok);
    CHECK_FALSE(parse_codec("hvc1.2.4").ok);
}

TEST_CASE("Codec strings are built from decoder configuration records", "[hls_conform]")
{
    // avcC: version 1, High (0x64), no constraint flag, level 4.2; 4-byte lengths.
    const Bytes avcc = { 0x01, 0x64, 0x00, 0x2A, 0xFF, 0xE1, 0x00, 0x04, 0x67, 0x64, 0x00, 0x2A, 0x00 };
    CHECK(avc_codec_string(avcc.data(), avcc.size()) == "avc1.64002a");
    CHECK(avc_codec_string(avcc.data(), avcc.size(), "avc3") == "avc3.64002a");
    CHECK(nal_length_size(avcc.data(), avcc.size(), false) == 4);

    // Annex B parameter sets, as MPEG-TS extradata has them.
    const Bytes annexb = cat({ start_code({ 0x67, 0x4D, 0x40, 0x1F, 0xAC, 0xD9 }), start_code({ 0x68, 0xEE, 0x3C, 0x80 }) });
    CHECK(avc_codec_string(annexb.data(), annexb.size()) == "avc1.4d401f");
    CHECK(nal_length_size(annexb.data(), annexb.size(), false) == 0);

    // hvcC: version 1, the profile_tier_level bytes, … lengthSizeMinusOne 3.
    Bytes hvcc = cat({ { 0x01 }, kPtlMain10, { 0xF0, 0x00, 0xFC, 0xFD, 0xFA, 0xFA, 0x00, 0x00, 0x0F, 0x00 } });
    REQUIRE(hvcc.size() == 23);
    CHECK(hevc_codec_string(hvcc.data(), hvcc.size()) == "hvc1.2.4.L123.B0");
    CHECK(hevc_codec_string(hvcc.data(), hvcc.size(), "hev1") == "hev1.2.4.L123.B0");
    CHECK(nal_length_size(hvcc.data(), hvcc.size(), true) == 4);

    // The SPS (with emulation prevention: the zeros of the flags), Annex B.
    const Bytes sps_rbsp = cat({ { 0x01 }, kPtlMain10, { 0xA0, 0x03, 0xC0, 0x80 } });
    const Bytes sps = cat({ { 0x42, 0x01 }, ep(sps_rbsp) });
    REQUIRE(sps.size() > sps_rbsp.size() + 2);   // emulation prevention bytes were inserted
    const Bytes hevc_annexb = cat({ start_code({ 0x40, 0x01, 0x0C, 0x01 }), start_code(sps) });
    CHECK(hevc_codec_string(hevc_annexb.data(), hevc_annexb.size()) == "hvc1.2.4.L123.B0");

    // HEVC Main, compatibility flags 1 and 2, High tier level 5.1, constraint bytes 0x90 0x00 0x01.
    Bytes ptl = { 0x21, 0x60, 0x00, 0x00, 0x00, 0x90, 0x00, 0x01, 0x00, 0x00, 0x00, 0x99 };
    Bytes hvcc2 = cat({ { 0x01 }, ptl, { 0xF0, 0x00, 0xFC, 0xFD, 0xF8, 0xF8, 0x00, 0x00, 0x0F, 0x00 } });
    CHECK(hevc_codec_string(hvcc2.data(), hvcc2.size()) == "hvc1.1.6.H153.90.0.1");

    // av1C: marker + version, seq_profile 0 / seq_level_idx 8, high_bitdepth.
    const Bytes av1c = { 0x81, 0x08, 0x40, 0x00 };
    CHECK(av1_codec_string(av1c.data(), av1c.size()) == "av01.0.08M.10");
    const Bytes av1c_12 = { 0x81, 0x4D, 0xE0, 0x00 };   // profile 2, level 13, High tier, 12-bit
    CHECK(av1_codec_string(av1c_12.data(), av1c_12.size()) == "av01.2.13H.12");

    CHECK(avc_codec_string(nullptr, 0).empty());
    CHECK(hevc_codec_string(annexb.data(), annexb.size()).empty());   // no HEVC SPS in it
    CHECK(av1_codec_string(avcc.data(), avcc.size()).empty());
}

// ── First pictures ───────────────────────────────────────────────────────────

TEST_CASE("first_picture_h264 tells an IDR from other starts", "[hls_conform]")
{
    const Bytes aud = { 0x09, 0xF0 }, sps = { 0x67, 0x64, 0x00, 0x28, 0xAC }, pps = { 0x68, 0xEE, 0x3C, 0x80 };
    // Slice headers: first_mb_in_slice 0 ("1"), then slice_type — 7 (I) "0001000", 5 (P) "00110".
    const Bytes idr = { 0x65, 0x88, 0x84, 0x00 }, i_slice = { 0x41, 0x88, 0x84 }, p_slice = { 0x41, 0x9A, 0x02 };
    const Bytes sei_recovery = { 0x06, 0x06, 0x01, 0xC4, 0x80 };     // payload type 6, one byte
    const Bytes sei_other = { 0x06, 0x05, 0x02, 0xAA, 0xBB, 0x80 };  // user data unregistered

    auto annexb = [](std::initializer_list<Bytes> nals) {
        Bytes out;
        for (const auto& n : nals) out = cat({ out, start_code(n) });
        return out;
    };
    auto lp = [](std::initializer_list<Bytes> nals) {
        Bytes out;
        for (const auto& n : nals) out = cat({ out, length_prefixed(n) });
        return out;
    };
    const Bytes a = annexb({ aud, sps, pps, idr });
    CHECK(first_picture_h264(a.data(), a.size(), 0) == FirstPicture::Idr);
    const Bytes b = annexb({ aud, p_slice });
    CHECK(first_picture_h264(b.data(), b.size(), 0) == FirstPicture::Other);
    const Bytes c = annexb({ aud, sei_other, i_slice });
    CHECK(first_picture_h264(c.data(), c.size(), 0) == FirstPicture::Intra);
    const Bytes d = annexb({ sei_recovery, p_slice });
    CHECK(first_picture_h264(d.data(), d.size(), 0) == FirstPicture::Recovery);
    const Bytes e = lp({ sei_other, idr });
    CHECK(first_picture_h264(e.data(), e.size(), 4) == FirstPicture::Idr);
    const Bytes f = lp({ p_slice });
    CHECK(first_picture_h264(f.data(), f.size(), 4) == FirstPicture::Other);
    // Lengths that do not add up: read as Annex B after all.
    CHECK(first_picture_h264(a.data(), a.size(), 4) == FirstPicture::Idr);
    const Bytes g = annexb({ aud, sps, pps });
    CHECK(first_picture_h264(g.data(), g.size(), 0) == FirstPicture::Unknown);
    CHECK(first_picture_h264(nullptr, 0, 0) == FirstPicture::Unknown);
}

TEST_CASE("first_picture_hevc reads the first VCL NAL unit's type", "[hls_conform]")
{
    auto au = [](std::uint8_t type) {
        return cat({ start_code({ 0x46, 0x01, 0x50 }),          // AUD
                     start_code({ 0x40, 0x01, 0x0C }),          // VPS
                     start_code({ 0x42, 0x01, 0x01 }),          // SPS
                     start_code({ static_cast<std::uint8_t>(type << 1), 0x01, 0xAF, 0x00 }) });
    };
    const auto check = [&](std::uint8_t type, FirstPicture want) {
        const Bytes b = au(type);
        CHECK(first_picture_hevc(b.data(), b.size(), 0) == want);
    };
    check(19, FirstPicture::Idr);    // IDR_W_RADL
    check(20, FirstPicture::Idr);    // IDR_N_LP
    check(21, FirstPicture::Cra);
    check(16, FirstPicture::Bla);
    check(1, FirstPicture::Other);   // TRAIL_R
    check(9, FirstPicture::Other);   // RASL_R
    const Bytes lp = cat({ length_prefixed({ 0x4E, 0x01, 0x05, 0x80 }), length_prefixed({ 0x2A, 0x01, 0xAF }) });
    CHECK(first_picture_hevc(lp.data(), lp.size(), 4) == FirstPicture::Cra);
    CHECK(is_random_access(FirstPicture::Cra));
    CHECK_FALSE(is_random_access(FirstPicture::Recovery));
}

// ── Bit rates ────────────────────────────────────────────────────────────────

TEST_CASE("segment_bitrates follows RFC 8216's peak segment bit rate", "[hls_conform]")
{
    SECTION("segments as long as the target: one segment per window")
    {
        // 6 s segments, target 6: windows of 3 to 9 s hold one segment each.
        const Bitrates b = segment_bitrates({ { 6.0, 600'000 }, { 6.0, 750'000 }, { 6.0, 600'000 } }, 6.0);
        CHECK(b.peak == Approx(1'000'000.0));
        CHECK(b.peak_at == 1);
        CHECK(b.peak_n == 1);
        CHECK_FALSE(b.peak_single);
        CHECK(b.average == Approx(1'950'000.0 * 8.0 / 18.0));
        CHECK(b.complete());
    }
    SECTION("short segments: windows of several")
    {
        // 2 s segments, target 6: windows of 3 to 9 s are 2 to 4 segments.
        const Bitrates b = segment_bitrates({ { 2.0, 300 }, { 2.0, 600 }, { 2.0, 300 }, { 2.0, 300 } }, 6.0);
        CHECK(b.peak == Approx((300.0 + 600.0) * 8.0 / 4.0));   // the 2-segment run from 0 (tie with 1: first kept)
        CHECK(b.peak_at == 0);
        CHECK(b.peak_n == 2);
    }
    SECTION("windows include single segments of half the target or more")
    {
        const Bitrates b = segment_bitrates({ { 2.0, 100 }, { 2.0, 300 }, { 2.0, 100 }, { 2.0, 100 } }, 4.0);
        CHECK(b.peak == Approx(300.0 * 8.0 / 2.0));
        CHECK(b.peak_at == 1);
        CHECK(b.peak_n == 1);
    }
    SECTION("an unknown size breaks a run")
    {
        // 2 s each, target 6: a window needs two neighbours with sizes; none
        // has, so the peak is the highest single segment's.
        const Bitrates b = segment_bitrates({ { 2.0, 300 }, { 2.0, -1 }, { 2.0, 400 } }, 6.0);
        CHECK(b.peak_single);
        CHECK(b.peak == Approx(400.0 * 8.0 / 2.0));
        CHECK(b.peak_at == 2);
        CHECK(b.sized == 2);
        CHECK(b.total == 3);
        CHECK_FALSE(b.complete());
        CHECK(b.average == Approx(700.0 * 8.0 / 4.0));
    }
    SECTION("nothing sized")
    {
        const Bitrates b = segment_bitrates({ { 6.0, -1 } }, 6.0);
        CHECK(b.peak == 0.0);
        CHECK(b.peak_at == -1);
        CHECK(b.average == 0.0);
    }
    CHECK(format_bitrate(2'177'116.0) == "2.18 Mb/s");
    CHECK(format_list({ 0, 1, 2, 3, 5, 7, 8, 10 }) == "0–3, 5, 7, 8, 10");
    CHECK(format_list({}).empty());
    CHECK(format_bitrate(845'000.0) == "845 kb/s");
}

// ── The verdict ──────────────────────────────────────────────────────────────

namespace {

PlaylistRead playlist(const std::string& uri, int segments, double dur, std::vector<std::int64_t> sizes,
                      bool independent = false)
{
    std::string text = std::format("#EXTM3U\n#EXT-X-VERSION:6\n#EXT-X-TARGETDURATION:{}\n#EXT-X-PLAYLIST-TYPE:VOD\n{}",
                                   static_cast<int>(std::ceil(dur)), independent ? "#EXT-X-INDEPENDENT-SEGMENTS\n" : "");
    for (int i = 0; i < segments; ++i) text += std::format("#EXTINF:{:.3f},\ns{}.ts\n", dur, i);
    text += "#EXT-X-ENDLIST\n";
    PlaylistRead p;
    p.uri = uri;
    p.tl = parse_media_timeline(text, uri);
    p.sizes = std::move(sizes);
    return p;
}

SegmentRead video_read(int index, std::string codec, std::string codec_string, int w, int h, double fps, int transfer,
                       FirstPicture first, double duration)
{
    SegmentRead r;
    r.index = index;
    VideoSeen v;
    v.codec = std::move(codec);
    v.codec_string = std::move(codec_string);
    v.width = w;
    v.height = h;
    v.transfer = transfer;
    r.video = v;
    r.first = first;
    r.first_key = is_random_access(first);
    r.frames = static_cast<int>(std::lround(fps * duration));
    r.keyframes = 1;
    r.frame_rate = fps;
    r.media_duration = duration;
    return r;
}

SegmentRead audio_read(int index, std::string codec_string, int channels)
{
    SegmentRead r;
    r.index = index;
    AudioSeen a;
    a.codec = codec_string.starts_with("mp4a") ? "aac" : codec_string == "ac-3" ? "ac3" : "eac3";
    a.codec_string = std::move(codec_string);
    a.channels = channels;
    a.sample_rate = 48000;
    r.audio = a;
    return r;
}

} // namespace

TEST_CASE("analyze_conformance compares every declaration with the media", "[hls_conform]")
{
    const std::string base = "https://example.com/m/master.m3u8";
    const MasterDecl m = parse_master_decl(
        "#EXTM3U\n"
        "#EXT-X-INDEPENDENT-SEGMENTS\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"aac\",NAME=\"English\",CHANNELS=\"2\",URI=\"a/en.m3u8\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=900000,AVERAGE-BANDWIDTH=800000,CODECS=\"avc1.4d401f,mp4a.40.2\","
        "RESOLUTION=1280x720,FRAME-RATE=30.000,AUDIO=\"aac\"\n"
        "v1/index.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=3000000,CODECS=\"hvc1.2.4.L120.B0,mp4a.40.5\",RESOLUTION=1920x1080,"
        "FRAME-RATE=59.940,AUDIO=\"aac\"\n"
        "v2/index.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=3100000,CODECS=\"hvc1.2.4.L120.B0,ec-3\",RESOLUTION=1920x1080,"
        "FRAME-RATE=59.940,AUDIO=\"aac\"\n"
        "v2/index.m3u8\n",
        base);
    REQUIRE(m.ok);
    REQUIRE(m.variants.size() == 3);

    // v1: 6 s segments at 800 / 1000 / 800 kb/s; the audio at 128 kb/s.
    PlaylistRead v1 = playlist("https://example.com/m/v1/index.m3u8", 3, 6.0, { 600'000, 750'000, 600'000 });
    v1.reads.push_back(video_read(0, "h264", "avc1.640028", 1280, 720, 29.97, 1, FirstPicture::Idr, 6.006));
    // v2: 2 Mb/s, HEVC PQ at level 4.1; its last segment read starts with a P picture.
    PlaylistRead v2 = playlist("https://example.com/m/v2/index.m3u8", 3, 6.0, { 1'500'000, 1'500'000, 1'500'000 });
    v2.reads.push_back(video_read(0, "hevc", "hev1.2.4.L123.B0", 1920, 1080, 59.94006, 16, FirstPicture::Idr, 6.0));
    v2.reads.back().video->sample_entry = "hev1";
    v2.reads.push_back(video_read(2, "hevc", "hev1.2.4.L123.B0", 1920, 1080, 59.94006, 16, FirstPicture::Other, 6.0));
    v2.reads.back().video->sample_entry = "hev1";
    PlaylistRead en = playlist("https://example.com/m/a/en.m3u8", 3, 6.0, { 96'000, 96'000, 96'000 });
    en.reads.push_back(audio_read(0, "mp4a.40.5", 2));

    const ConformReport r = analyze_conformance(m, { v1, v2, en });
    INFO(finding_list(r));
    REQUIRE(r.rows.size() == 3);

    const VariantRow& a = r.rows[0];
    CHECK(a.peak == Approx(1'000'000.0 + 128'000.0));
    CHECK(a.rates_complete);
    CHECK(a.cell(Check::Bandwidth).sev == Severity::Error);
    CHECK(a.cell(Check::AverageBandwidth).sev == Severity::Warning);   // 800 k against 994.7 k
    CHECK(a.cell(Check::Codecs).sev == Severity::Error);               // Main declared, High read
    CHECK(a.cell(Check::Resolution).sev == Severity::Ok);
    CHECK(a.cell(Check::FrameRate).sev == Severity::Warning);          // 30.000 against 29.970
    CHECK(a.cell(Check::VideoRange).sev == Severity::Ok);
    CHECK(a.cell(Check::SegmentStart).sev == Severity::Ok);
    CHECK(a.cell(Check::Duration).sev == Severity::Ok);                // 6 ms over EXTINF
    CHECK(a.worst == Severity::Error);
    const Finding* bw = find_finding(r, Severity::Error,
                                     "v1/index.m3u8: BANDWIDTH 900 kb/s is 20.2% below the measured peak 1.13 Mb/s");
    REQUIRE(bw);
    CHECK(bw->renditions == std::vector<int>{ 0 });
    CHECK(bw->detail.find("segment #1, 6.000 s") != std::string::npos);
    CHECK(find_finding(r, Severity::Error, "v1/index.m3u8: CODECS names another profile"));
    CHECK(find_finding(r, Severity::Warning, "v1/index.m3u8: CODECS AAC-LC but the audio is HE-AAC"));

    const VariantRow& b = r.rows[1];
    CHECK(b.peak == Approx(2'128'000.0));
    CHECK(b.cell(Check::Bandwidth).sev == Severity::Ok);
    CHECK(b.cell(Check::AverageBandwidth).checked == false);
    CHECK(b.cell(Check::Codecs).sev == Severity::Error);       // level 4.0 declared, 4.1 read
    CHECK(b.cell(Check::FrameRate).sev == Severity::Ok);
    CHECK(b.cell(Check::VideoRange).sev == Severity::Error);   // no VIDEO-RANGE, PQ video
    CHECK(b.cell(Check::SegmentStart).sev == Severity::Error); // independent segments, a P start
    CHECK(b.cell(Check::SegmentStart).measured == "1/2 random access");

    // What is about v2's playlist is one finding for both variants using it.
    const Finding* range = find_finding(r, Severity::Error, "v2/index.m3u8: VIDEO-RANGE absent (SDR), the video is PQ");
    REQUIRE(range);
    CHECK(range->renditions == std::vector<int>{ 1, 2 });
    const Finding* start = find_finding(r, Severity::Error, "v2/index.m3u8: segment #2 starts with a P/B picture");
    REQUIRE(start);
    CHECK(start->renditions == std::vector<int>{ 1, 2 });
    const Finding* level = find_finding(r, Severity::Error, "v2/index.m3u8: CODECS level 4 below the stream's 4.1");
    REQUIRE(level);
    CHECK(level->renditions == std::vector<int>{ 1, 2 });
    CHECK(find_finding(r, Severity::Warning, "v2/index.m3u8: CODECS hvc1 but the init segment's sample entry is hev1"));

    // Variant 2 declares E-AC-3, its audio group is HE-AAC.
    const VariantRow& c = r.rows[2];
    CHECK(c.cell(Check::Codecs).sev == Severity::Error);
    const Finding* audio = find_finding(r, Severity::Error, "v2/index.m3u8: CODECS does not list the audio format");
    REQUIRE(audio);
    CHECK(audio->renditions == std::vector<int>{ 2 });
    CHECK(find_finding(r, Severity::Warning, "v2/index.m3u8: CODECS lists ec-3 but no segment read holds it"));

    CHECK(r.errors >= 6);
    CHECK(r.findings.front().sev == Severity::Error);
    CHECK(r.findings.back().sev != Severity::Error);

    const std::string js = conformance_json(m, { v1, v2, en }, r, base);
    CHECK(js.starts_with("{\n  \"source\": \"https://example.com/m/master.m3u8\""));
    CHECK(js.find("\"BANDWIDTH\": {\"checked\": true, \"severity\": \"error\"") != std::string::npos);
    CHECK(js.find("\"first_picture\": \"P/B\"") != std::string::npos);
    CHECK(std::count(js.begin(), js.end(), '{') == std::count(js.begin(), js.end(), '}'));
    CHECK(std::count(js.begin(), js.end(), '[') == std::count(js.begin(), js.end(), ']'));
}

TEST_CASE("analyze_conformance: target durations, CHANNELS, missing CODECS", "[hls_conform]")
{
    const MasterDecl m = parse_master_decl(
        "#EXTM3U\n"
        "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"a\",NAME=\"Surround\",CHANNELS=\"2\",URI=\"a6.m3u8\"\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=500000,AUDIO=\"a\"\n"
        "v.m3u8\n",
        "https://example.com/x/master.m3u8");
    PlaylistRead v = playlist("https://example.com/x/v.m3u8", 2, 6.0, { 300'000, 300'000 });
    v.reads.push_back(video_read(0, "h264", "avc1.64001f", 1280, 720, 25.0, 1, FirstPicture::Idr, 6.0));
    PlaylistRead a6 = playlist("https://example.com/x/a6.m3u8", 3, 4.0, { 64'000, 64'000, 64'000 });
    a6.reads.push_back(audio_read(0, "ac-3", 6));

    const ConformReport r = analyze_conformance(m, { v, a6 });
    INFO(finding_list(r));
    CHECK(find_finding(r, Severity::Error, "The playlists' EXT-X-TARGETDURATION differ"));
    CHECK(find_finding(r, Severity::Warning, "v.m3u8: no CODECS"));
    CHECK(find_finding(r, Severity::Warning, "audio \"Surround\" (x/a6.m3u8): CHANNELS \"2\", it decodes to 6 channels"));
    CHECK(find_finding(r, Severity::Info, "v.m3u8: no FRAME-RATE"));
    CHECK(find_finding(r, Severity::Info, "v.m3u8: no RESOLUTION"));
    // 400 kb/s video + 128 kb/s audio over 500 kb/s.
    CHECK(r.rows[0].peak == Approx(400'000.0 + 128'000.0));
    CHECK(r.rows[0].cell(Check::Bandwidth).sev == Severity::Error);
    CHECK(r.rows[0].cell(Check::Codecs).sev == Severity::Warning);

    // Unread and unreadable playlists are said so, nothing is guessed.
    PlaylistRead broken;
    broken.uri = "https://example.com/x/a6.m3u8";
    broken.error = "HTTP error 404 Not Found";
    const ConformReport r2 = analyze_conformance(m, { v, broken });
    INFO(finding_list(r2));
    CHECK(find_finding(r2, Severity::Warning, "x/a6.m3u8: could not be read"));
    CHECK_FALSE(r2.rows[0].rates_complete);
    CHECK(r2.rows[0].cell(Check::Bandwidth).note.find("an audio rendition unread") != std::string::npos);
    const ConformReport r3 = analyze_conformance(m, {});
    CHECK(find_finding(r3, Severity::Info, "v.m3u8: not read"));
    CHECK(r3.errors == 0);
}

TEST_CASE("analyze_conformance notes BANDWIDTHs a hair under the peak once", "[hls_conform]")
{
    const MasterDecl m = parse_master_decl("#EXTM3U\n"
                                           "#EXT-X-STREAM-INF:BANDWIDTH=995000,CODECS=\"avc1.64001f\"\nv.m3u8\n"
                                           "#EXT-X-STREAM-INF:BANDWIDTH=999900,CODECS=\"avc1.64001f\"\nv.m3u8\n"
                                           "#EXT-X-STREAM-INF:BANDWIDTH=900000,CODECS=\"avc1.64001f\"\nv.m3u8\n",
                                           "https://example.com/y/master.m3u8");
    // 750 000 bytes per 6 s segment: 1 Mb/s.
    const PlaylistRead v = playlist("https://example.com/y/v.m3u8", 3, 6.0, { 750'000, 750'000, 750'000 });
    const ConformReport r = analyze_conformance(m, { v });
    INFO(finding_list(r));
    CHECK(r.rows[0].cell(Check::Bandwidth).sev == Severity::Info);
    CHECK(r.rows[1].cell(Check::Bandwidth).sev == Severity::Info);
    CHECK(r.rows[2].cell(Check::Bandwidth).sev == Severity::Error);   // 10% short
    const Finding* f = find_finding(r, Severity::Info, "BANDWIDTH of 2 variants up to 0.5% below the measured peak");
    REQUIRE(f);
    CHECK(f->renditions == std::vector<int>{ 0, 1 });
    CHECK(f->detail.find("variant #0 (y/v.m3u8), 5000 b/s") != std::string::npos);
    CHECK(find_finding(r, Severity::Error, "y/v.m3u8: BANDWIDTH 900 kb/s is 10.0% below the measured peak 1.00 Mb/s"));
    CHECK(r.errors == 1);
}

// ── Segments encoded here ────────────────────────────────────────────────────

namespace {

using testencode::Encode;
using testencode::encode;
using testencode::slurp;

SegmentRead read_file(const fs::path& p, int index = 0)
{
    const std::string d = slurp(p);
    return conformprobe::read_segment(reinterpret_cast<const std::uint8_t*>(d.data()), d.size(), index,
                                      static_cast<std::int64_t>(d.size()));
}

const char* kX264 = "scenecut=0";

} // namespace

TEST_CASE("pick_segments: the first, the last, evenly between", "[hls_conform]")
{
    CHECK(conformprobe::pick_segments(10, 3) == std::vector<int>{ 0, 5, 9 });
    CHECK(conformprobe::pick_segments(100, 4) == std::vector<int>{ 0, 33, 66, 99 });
    CHECK(conformprobe::pick_segments(2, 3) == std::vector<int>{ 0, 1 });
    CHECK(conformprobe::pick_segments(5, 0) == std::vector<int>{ 0, 1, 2, 3, 4 });
    CHECK(conformprobe::pick_segments(7, 1) == std::vector<int>{ 0 });
    CHECK(conformprobe::pick_segments(0, 3).empty());
}

TEST_CASE("read_segment: H.264 in MPEG-TS and in fragmented MP4", "[hls_conform][probe]")
{
    if (!avcodec_find_encoder_by_name("libx264")) SKIP("no H.264 encoder (libx264)");
    const fs::path ts = testtemp::path("conform_h264.ts");
    REQUIRE(encode(ts, { .codec_opts = { { "x264-params", kX264 } } }));
    const SegmentRead r = read_file(ts);
    INFO(r.error);
    REQUIRE(r.error.empty());
    REQUIRE(r.video);
    CHECK(r.video->codec == "h264");
    CHECK(r.video->sample_entry.empty());
    const CodecId c = parse_codec(r.video->codec_string);
    CHECK(c.ok);
    CHECK(c.fourcc == "avc1");
    CHECK(c.profile == 100);   // libx264's default profile for 8-bit 4:2:0: High
    CHECK(r.video->width == 64);
    CHECK(r.video->height == 64);
    CHECK(r.video->bit_depth == 8);
    CHECK(r.first == FirstPicture::Idr);
    CHECK(r.first_key);
    CHECK(r.frames == 48);
    CHECK(r.keyframes == 4);
    CHECK(r.frame_rate == Approx(24.0).epsilon(1e-6));
    CHECK(r.media_duration == Approx(2.0).epsilon(1e-6));
    CHECK_FALSE(r.audio);

    const fs::path mp4 = testtemp::path("conform_h264_frag.mp4");
    REQUIRE(encode(mp4, { .format = "mp4", .codec_opts = { { "x264-params", kX264 } },
                          .mux_opts = { { "movflags", "frag_keyframe+empty_moov+default_base_moof" } } }));
    const SegmentRead m = read_file(mp4);
    INFO(m.error);
    REQUIRE(m.video);
    CHECK(m.video->sample_entry == "avc1");
    CHECK(m.video->codec_string == r.video->codec_string);   // the same SPS, from avcC this time
    CHECK(m.first == FirstPicture::Idr);                     // length-prefixed NAL units
    CHECK(m.frames == 48);

    const std::string junk(1000, 'x');
    const SegmentRead bad = conformprobe::read_segment(reinterpret_cast<const std::uint8_t*>(junk.data()), junk.size(), 3, 1000);
    CHECK_FALSE(bad.error.empty());
    CHECK(bad.index == 3);
    CHECK_FALSE(bad.video);
}

TEST_CASE("read_segment: HEVC in MPEG-TS", "[hls_conform][probe]")
{
    if (!avcodec_find_encoder_by_name("libx265")) SKIP("no HEVC encoder (libx265)");
    const fs::path ts = testtemp::path("conform_hevc.ts");
    REQUIRE(encode(ts, { .encoder = "libx265", .codec_opts = { { "x265-params", "scenecut=0:log-level=error" } } }));
    const SegmentRead r = read_file(ts);
    INFO(r.error);
    REQUIRE(r.video);
    CHECK(r.video->codec == "hevc");
    const CodecId c = parse_codec(r.video->codec_string);
    CHECK(c.ok);
    CHECK(c.fourcc == "hvc1");
    CHECK(c.profile == 1);     // Main: 8-bit 4:2:0
    CHECK(r.first == FirstPicture::Idr);
    CHECK(r.frames == 48);
}

TEST_CASE("ConformProbe checks a local HLS package end to end", "[hls_conform][probe]")
{
    if (!avcodec_find_encoder_by_name("libx264")) SKIP("no H.264 encoder (libx264)");
    const fs::path dir = testtemp::path("conform_package");
    fs::create_directories(dir / "a");
    fs::create_directories(dir / "b");
    // a: cut at keyframes, a GOP (0.5 s) per segment. b: cut every 0.3 s
    // whatever the picture (split_by_time), so most segments start inside a GOP.
    const auto hls_opts = [&](const char* sub, const char* time, bool by_time) {
        std::vector<std::pair<const char*, const char*>> o = { { "hls_time", time }, { "hls_list_size", "0" },
                                                               { "hls_playlist_type", "vod" } };
        static std::string names[2];
        names[sub[0] - 'a'] = (dir / sub / "s%d.ts").string();
        o.emplace_back("hls_segment_filename", names[sub[0] - 'a'].c_str());
        if (by_time) o.emplace_back("hls_flags", "split_by_time");
        return o;
    };
    REQUIRE(encode(dir / "a" / "index.m3u8", { .format = "hls", .codec_opts = { { "x264-params", kX264 } },
                                               .mux_opts = hls_opts("a", "0.5", false) }));
    REQUIRE(encode(dir / "b" / "index.m3u8", { .format = "hls", .codec_opts = { { "x264-params", kX264 } },
                                               .mux_opts = hls_opts("b", "0.3", true) }));
    const SegmentRead first = read_file(dir / "a" / "s0.ts");
    REQUIRE(first.video);
    const std::string cs = first.video->codec_string;
    REQUIRE_FALSE(cs.empty());

    // a: a BANDWIDTH far too low, a Constrained Baseline level 1 CODECS, a wrong
    // size and frame rate. b: right but for a BANDWIDTH far too high.
    {
        std::ofstream out(dir / "master.m3u8");
        out << "#EXTM3U\n#EXT-X-INDEPENDENT-SEGMENTS\n"
            << "#EXT-X-STREAM-INF:BANDWIDTH=1000,CODECS=\"avc1.42e00a\",RESOLUTION=128x128,FRAME-RATE=30.000\n"
            << "a/index.m3u8\n"
            << "#EXT-X-STREAM-INF:BANDWIDTH=100000000,CODECS=\"" << cs << "\",RESOLUTION=64x64,FRAME-RATE=24.000\n"
            << "b/index.m3u8\n";
    }
    ConformProbe probe;
    probe.start((dir / "master.m3u8").string(), { .segments = 0, .workers = 2 });
    const auto t0 = std::chrono::steady_clock::now();
    while (probe.running() && std::chrono::steady_clock::now() - t0 < std::chrono::seconds(60))
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    REQUIRE(probe.state() == ConformProbe::State::Done);
    const ConformProbe::Result res = probe.result();
    const ConformReport& r = res.report;
    INFO(finding_list(r));
    REQUIRE(res.playlists.size() == 2);
    const PlaylistRead& pa = res.playlists[0];
    const PlaylistRead& pb = res.playlists[1];
    CHECK(pa.error.empty());
    CHECK(pa.tl.segments.size() == 4);
    CHECK(pa.reads.size() == pa.tl.segments.size());   // segments = 0: every one
    CHECK(std::all_of(pa.sizes.begin(), pa.sizes.end(), [](std::int64_t b) { return b > 0; }));
    CHECK(pa.sizes[0] == static_cast<std::int64_t>(fs::file_size(dir / "a" / "s0.ts")));
    CHECK(pb.tl.segments.size() > pa.tl.segments.size());
    const auto progress = probe.progress();
    CHECK(progress.playlists == 2);
    CHECK(progress.reads == static_cast<int>(pa.tl.segments.size() + pb.tl.segments.size()));

    REQUIRE(r.rows.size() == 2);
    const VariantRow& a = r.rows[0];
    CHECK(a.rates_complete);
    CHECK(a.cell(Check::Bandwidth).sev == Severity::Error);
    CHECK(a.cell(Check::Codecs).sev == Severity::Error);
    CHECK(a.cell(Check::Resolution).sev == Severity::Warning);
    CHECK(a.cell(Check::FrameRate).sev == Severity::Warning);
    CHECK(a.cell(Check::SegmentStart).sev == Severity::Ok);
    CHECK(a.cell(Check::SegmentStart).measured == "4/4 random access");
    CHECK(a.cell(Check::Duration).sev == Severity::Ok);
    CHECK(a.cell(Check::VideoRange).sev == Severity::Ok);
    const VariantRow& b = r.rows[1];
    CHECK(b.cell(Check::Bandwidth).sev == Severity::Info);
    CHECK(b.cell(Check::Codecs).sev == Severity::Ok);
    CHECK(b.cell(Check::Resolution).sev == Severity::Ok);
    CHECK(b.cell(Check::FrameRate).sev == Severity::Ok);
    CHECK(b.cell(Check::SegmentStart).sev == Severity::Error);
    const Finding* starts = find_finding(r, Severity::Error, "segments read start with a P/B picture (#1");
    REQUIRE(starts);
    CHECK(starts->title.starts_with("b/index.m3u8: "));
    // The hls muxer writes EXT-X-TARGETDURATION:0 for these half-second segments.
    CHECK(find_finding(r, Severity::Warning, "a/index.m3u8: EXT-X-TARGETDURATION is 0"));
    CHECK_FALSE(find_finding(r, Severity::Error, "no EXT-X-TARGETDURATION"));
}
