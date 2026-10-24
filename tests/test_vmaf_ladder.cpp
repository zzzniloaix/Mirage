// Ladder scoring (src/VMAFLadder.h, DECISIONS.md D-029): a master playlist's
// entries, the pairs of each reference mode, and the scoring of a synthetic
// ladder written at run time, whose pairs must have the results of the same
// pairs scored one by one. No network: the ladders are local HLS playlists of
// MPEG-TS segments, coded by FFmpeg's own MPEG-2 encoder.

#include <catch2/catch_test_macros.hpp>

#include "TestTemp.h"
#include "VMAFAnalyzer.h"
#include "VMAFLadder.h"

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace fs = std::filesystem;
using vmafladder::Entry;
using vmafladder::Master;
using vmafladder::Plan;

namespace {

// bipbop 16x9's master as Apple serves it, in shape: five video entries, an
// audio-only one, renditions and I-frame playlists.
constexpr const char* kBipbop =
    "#EXTM3U\n"
    "\n"
    "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"bipbop_audio\",LANGUAGE=\"eng\",NAME=\"BipBop Audio 1\","
    "AUTOSELECT=YES,DEFAULT=YES\n"
    "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",NAME=\"English\",DEFAULT=YES,AUTOSELECT=YES,"
    "FORCED=NO,LANGUAGE=\"en\",URI=\"subtitles/eng/prog_index.m3u8\"\n"
    "\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=263851,CODECS=\"mp4a.40.2, avc1.4d400d\",RESOLUTION=416x234,"
    "AUDIO=\"bipbop_audio\",SUBTITLES=\"subs\"\n"
    "gear1/prog_index.m3u8\n"
    "#EXT-X-I-FRAME-STREAM-INF:BANDWIDTH=28451,CODECS=\"avc1.4d400d\",URI=\"gear1/iframe_index.m3u8\"\n"
    "\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=577610,CODECS=\"mp4a.40.2, avc1.4d401e\",RESOLUTION=640x360,"
    "AUDIO=\"bipbop_audio\",SUBTITLES=\"subs\"\n"
    "gear2/prog_index.m3u8\n"
    "\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=915905,CODECS=\"mp4a.40.2, avc1.4d401f\",RESOLUTION=960x540,"
    "AUDIO=\"bipbop_audio\",SUBTITLES=\"subs\"\n"
    "gear3/prog_index.m3u8\n"
    "\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=1030138,CODECS=\"mp4a.40.2, avc1.4d401f\",RESOLUTION=1280x720,"
    "AUDIO=\"bipbop_audio\",SUBTITLES=\"subs\"\n"
    "gear4/prog_index.m3u8\n"
    "\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=1924009,CODECS=\"mp4a.40.2, avc1.4d401f\",RESOLUTION=1920x1080,"
    "AUDIO=\"bipbop_audio\",SUBTITLES=\"subs\"\n"
    "gear5/prog_index.m3u8\n"
    "\n"
    "#EXT-X-STREAM-INF:BANDWIDTH=41457,CODECS=\"mp4a.40.2\",AUDIO=\"bipbop_audio\",SUBTITLES=\"subs\"\n"
    "gear0/prog_index.m3u8\n";

const std::string kBipbopUrl =
    "https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8";
const std::string kBipbopDir =
    "https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/";

std::string lookup(const std::vector<std::pair<std::string, std::string>>& attrs, const char* name)
{
    std::string out = "(absent)";
    for (const auto& [k, v] : attrs)
        if (k == name) out = v;
    return out;
}

void write_text(const fs::path& path, const std::string& text)
{
    fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
}

constexpr int kFps = 25;

// One rung of a synthetic ladder: <dir>/index.m3u8, an HLS media playlist of
// one MPEG-TS segment, <dir>/seg0.ts, of `frames` frames of w x h at 25 fps:
// a gray diagonal ramp that moves every frame, MPEG-2 coded, every frame a
// key frame at a fixed quantizer, BT.709 tags. Returns the playlist's path.
std::string make_rung(const fs::path& dir, int w, int h, int frames = 12)
{
    fs::create_directories(dir);
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
    REQUIRE(codec);
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    enc->width          = w;
    enc->height         = h;
    enc->pix_fmt        = AV_PIX_FMT_YUV420P;
    enc->time_base      = { 1, kFps };
    enc->framerate      = { kFps, 1 };
    enc->gop_size       = 1;
    enc->max_b_frames   = 0;
    enc->flags         |= AV_CODEC_FLAG_QSCALE;
    enc->global_quality = FF_QP2LAMBDA * 2;
    enc->colorspace     = AVCOL_SPC_BT709;
    enc->color_range    = AVCOL_RANGE_MPEG;
    REQUIRE(avcodec_open2(enc, codec, nullptr) >= 0);

    const fs::path seg = dir / "seg0.ts";
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "mpegts", seg.c_str()) >= 0);
    AVStream* st = avformat_new_stream(oc, nullptr);
    REQUIRE(avcodec_parameters_from_context(st->codecpar, enc) >= 0);
    st->time_base = { 1, 90000 };
    REQUIRE(avio_open(&oc->pb, seg.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);

    const auto drain = [&] {
        AVPacket* pkt = av_packet_alloc();
        while (avcodec_receive_packet(enc, pkt) == 0) {
            pkt->stream_index = 0;
            av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
            REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
        }
        av_packet_free(&pkt);
    };
    AVFrame* f = av_frame_alloc();
    f->format = AV_PIX_FMT_YUV420P;
    f->width  = w;
    f->height = h;
    REQUIRE(av_frame_get_buffer(f, 0) >= 0);
    for (int k = 0; k < frames; ++k) {
        REQUIRE(av_frame_make_writable(f) >= 0);
        for (int row = 0; row < h; ++row)
            for (int col = 0; col < w; ++col)
                f->data[0][row * f->linesize[0] + col] =
                    static_cast<uint8_t>((col * 256 / w + row * 128 / h + 3 * k) & 0xff);
        for (int p = 1; p < 3; ++p)
            for (int row = 0; row < (h + 1) / 2; ++row)
                std::fill_n(f->data[p] + row * f->linesize[p], (w + 1) / 2, static_cast<uint8_t>(128));
        f->pts = k;
        REQUIRE(avcodec_send_frame(enc, f) >= 0);
        drain();
    }
    REQUIRE(avcodec_send_frame(enc, nullptr) >= 0);
    drain();
    av_frame_free(&f);
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    avcodec_free_context(&enc);

    write_text(dir / "index.m3u8",
               std::format("#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:1\n"
                           "#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PLAYLIST-TYPE:VOD\n"
                           "#EXTINF:{:.6f},\nseg0.ts\n#EXT-X-ENDLIST\n",
                           static_cast<double>(frames) / kFps));
    return (dir / "index.m3u8").string();
}

// A synthetic ladder under testtemp::dir()/<name>: rungs low (96x54), mid
// (128x72, listed twice, with two audio groups) and top (192x108), the top
// rung in the middle of the master, and an audio-only entry whose playlist
// doesn't exist, since it is never opened.
struct Ladder {
    fs::path    dir;
    std::string master, low, mid, top;
};

const Ladder& ladder()
{
    static const Ladder l = [] {
        Ladder x;
        x.dir    = testtemp::path("mirage_test_ladder");
        x.low    = make_rung(x.dir / "low", 96, 54);
        x.mid    = make_rung(x.dir / "mid", 128, 72);
        x.top    = make_rung(x.dir / "top", 192, 108);
        x.master = (x.dir / "master.m3u8").string();
        write_text(x.master,
                   "#EXTM3U\n#EXT-X-VERSION:3\n"
                   "#EXT-X-STREAM-INF:BANDWIDTH=300000,RESOLUTION=96x54,CODECS=\"mp2v,mp4a.40.2\"\n"
                   "low/index.m3u8\n"
                   "#EXT-X-STREAM-INF:BANDWIDTH=600000,RESOLUTION=128x72,AUDIO=\"a1\"\n"
                   "mid/index.m3u8\n"
                   "#EXT-X-STREAM-INF:BANDWIDTH=900000,RESOLUTION=192x108\n"
                   "top/index.m3u8\n"
                   "#EXT-X-STREAM-INF:BANDWIDTH=640000,RESOLUTION=128x72,AUDIO=\"a2\"\n"
                   "mid/index.m3u8\n"
                   "#EXT-X-STREAM-INF:BANDWIDTH=64000,CODECS=\"mp4a.40.2\"\n"
                   "audio/index.m3u8\n");
        return x;
    }();
    return l;
}

// Checks that `got`, a pair of a ladder run, has the result of `want`, the
// same two inputs scored on their own: every score, timestamp, position,
// pooled value and what the analysis recorded about the pair.
void check_same_pair(const VMAFResult& got, const VMAFResult& want)
{
    REQUIRE(want.error.empty());
    CHECK(got.ref_url == want.ref_url);
    CHECK(got.dis_url == want.dis_url);
    CHECK(got.per_frame == want.per_frame);   // ==: to the last bit
    CHECK(got.ref_pts == want.ref_pts);
    CHECK(got.dis_pts == want.dis_pts);
    CHECK(got.ref_index == want.ref_index);
    CHECK(got.dis_index == want.dis_index);
    CHECK(got.pooled.mean == want.pooled.mean);
    CHECK(got.pooled.harmonic_mean == want.pooled.harmonic_mean);
    CHECK(got.pooled.min == want.pooled.min);
    CHECK(got.pooled.p5 == want.pooled.p5);
    CHECK(got.pooled.p50 == want.pooled.p50);
    CHECK(got.settings == want.settings);
    CHECK(got.width == want.width);
    CHECK(got.height == want.height);
    CHECK(got.alignment.paired == want.alignment.paired);
    CHECK(got.alignment.failed == want.alignment.failed);
    CHECK(got.alignment.contexts == want.alignment.contexts);
    CHECK(got.warnings == want.warnings);
    CHECK(got.dis_conversions.size() == want.dis_conversions.size());
    CHECK(got.ref_info.width == want.ref_info.width);
    CHECK(got.dis_info.width == want.dis_info.width);
    CHECK(got.ref_info.ended == want.ref_info.ended);
    CHECK(got.dis_info.ended == want.dis_info.ended);
}

} // namespace

// ── The master playlist ──────────────────────────────────────────────────────

TEST_CASE("vmafladder::attributes: an attribute list read as fetch_streams.py reads it", "[ladder]")
{
    const auto a = vmafladder::attributes(
        "AVERAGE-BANDWIDTH=2168183,BANDWIDTH=2177116,CODECS=\"avc1.640020,mp4a.40.2\","
        "RESOLUTION=960x540,AUDIO=\"aud 1\"");
    REQUIRE(a.size() == 5);
    CHECK(lookup(a, "BANDWIDTH") == "2177116");   // not inside AVERAGE-BANDWIDTH
    CHECK(lookup(a, "AVERAGE-BANDWIDTH") == "2168183");
    CHECK(lookup(a, "CODECS") == "avc1.640020,mp4a.40.2");   // a quoted comma
    CHECK(lookup(a, "RESOLUTION") == "960x540");
    CHECK(lookup(a, "AUDIO") == "aud 1");

    // A name given twice: both listed, lookups take the last.
    CHECK(lookup(vmafladder::attributes("BANDWIDTH=1,BANDWIDTH=2"), "BANDWIDTH") == "2");
    // Lower-case names aren't names; an unquoted value ends at ',' or '"'.
    CHECK(vmafladder::attributes("bandwidth=5").empty());
    CHECK(lookup(vmafladder::attributes("X=a\"b\",Y=1"), "X") == "a");
    CHECK(lookup(vmafladder::attributes("X=a\"b\",Y=1"), "Y") == "1");
    // A quote that doesn't close gives an empty value, as the regular expression does.
    CHECK(lookup(vmafladder::attributes("CODECS=\"avc1,RESOLUTION=1x1"), "CODECS").empty());
    CHECK(lookup(vmafladder::attributes("CODECS=\"avc1,RESOLUTION=1x1"), "RESOLUTION") == "1x1");
    CHECK(lookup(vmafladder::attributes("A=,B=2"), "A").empty());
}

TEST_CASE("vmafladder::has_video: a RESOLUTION, or a video codec in CODECS", "[ladder]")
{
    using S = std::optional<std::string>;
    CHECK(vmafladder::has_video(S("416x234"), S("mp4a.40.2")));   // RESOLUTION decides
    CHECK(vmafladder::has_video(S(""), std::nullopt));            // given, even empty
    CHECK(vmafladder::has_video(std::nullopt, S("mp4a.40.2, avc1.4d400d")));
    CHECK(vmafladder::has_video(std::nullopt, S("hvc1.2.4.L123.B0")));
    CHECK(vmafladder::has_video(std::nullopt, S(" av01.0.08M.08 ")));
    CHECK(vmafladder::has_video(std::nullopt, S("vp09")));        // no '.'
    for (const char* name : { "avc1", "avc3", "hvc1", "hev1", "dvh1", "dvhe", "av01", "vp09",
                              "vp08", "mp4v" }) {
        INFO(name);
        CHECK(vmafladder::has_video(std::nullopt, S(std::format("mp4a.40.2,{}.1", name))));
    }
    CHECK_FALSE(vmafladder::has_video(std::nullopt, S("mp4a.40.2")));
    CHECK_FALSE(vmafladder::has_video(std::nullopt, S("ec-3,stpp.ttml.im1t")));
    CHECK_FALSE(vmafladder::has_video(std::nullopt, S("AVC1.4d401f")));   // case matters
    CHECK_FALSE(vmafladder::has_video(std::nullopt, S("")));
    CHECK_FALSE(vmafladder::has_video(std::nullopt, std::nullopt));
}

TEST_CASE("vmafladder::resolve: URIs against an http(s) location and a local one", "[ladder]")
{
    CHECK(vmafladder::resolve(kBipbopUrl, "gear1/prog_index.m3u8") ==
          kBipbopDir + "gear1/prog_index.m3u8");
    CHECK(vmafladder::resolve("https://h/a/m.m3u8?t=1", "/b/p.m3u8") == "https://h/b/p.m3u8");
    CHECK(vmafladder::resolve("https://h/a/m.m3u8", "https://o/p.m3u8") == "https://o/p.m3u8");
    // Forms FFmpeg would resolve another way, or not at all: refused.
    for (const char* uri : { "../p.m3u8", "./p.m3u8", "//o/p.m3u8", "?x=1", "skd://k", "a b.m3u8" }) {
        INFO(uri);
        CHECK(vmafladder::resolve("https://h/a/m.m3u8", uri).empty());
    }

    CHECK(vmafladder::resolve("/data/ladder/master.m3u8", "low/index.m3u8") ==
          "/data/ladder/low/index.m3u8");
    CHECK(vmafladder::resolve("ladder/master.m3u8", "../x/i.m3u8") == "ladder/../x/i.m3u8");
    CHECK(vmafladder::resolve("master.m3u8", "low/index.m3u8") == "low/index.m3u8");
    CHECK(vmafladder::resolve("/data/master.m3u8", "/abs/i.m3u8") == "/abs/i.m3u8");
    CHECK(vmafladder::resolve("/data/master.m3u8", "https://h/i.m3u8") == "https://h/i.m3u8");
    CHECK(vmafladder::resolve("/data/master.m3u8", "file:/x/i.m3u8") == "file:/x/i.m3u8");
    CHECK(vmafladder::resolve("file:/data/master.m3u8", "i.m3u8") == "file:/data/i.m3u8");
    CHECK(vmafladder::resolve("/data/master.m3u8", "").empty());
}

TEST_CASE("vmafladder::parse_master: bipbop 16x9's entries", "[ladder]")
{
    const Master m = vmafladder::parse_master(kBipbop, kBipbopUrl);
    REQUIRE(m.error.empty());
    REQUIRE(m.entries.size() == 6);
    const std::vector<int64_t> bandwidths = { 263851, 577610, 915905, 1030138, 1924009, 41457 };
    for (std::size_t i = 0; i < 6; ++i) {
        INFO(i);
        const Entry& e = m.entries[i];
        CHECK(e.index == i);
        CHECK(e.bandwidth == bandwidths[i]);
        const std::string gear = std::format("gear{}/prog_index.m3u8", i < 5 ? i + 1 : 0);
        CHECK(e.uri == gear);
        CHECK(e.url == kBipbopDir + gear);
        CHECK(e.video == (i < 5));
        CHECK_FALSE(e.average_bandwidth);
        CHECK_FALSE(e.frame_rate);
    }
    CHECK(m.entries[0].line == 6);
    CHECK(m.entries[0].codecs == "mp4a.40.2, avc1.4d400d");
    CHECK(m.entries[0].resolution == "416x234");
    CHECK(m.entries[4].resolution == "1920x1080");
    CHECK_FALSE(m.entries[5].resolution);
    CHECK(m.entries[5].codecs == "mp4a.40.2");

    // CRLF line ends, white space around lines, comments and tags between an
    // EXT-X-STREAM-INF and its URI.
    const Master crlf = vmafladder::parse_master(
        "#EXTM3U\r\n  #EXT-X-STREAM-INF:BANDWIDTH=5,FRAME-RATE=29.970,AVERAGE-BANDWIDTH=4\r\n"
        "# a comment\r\n#EXT-X-SOMETHING\r\n\r\n  v/p.m3u8  \r\n",
        "https://h/m.m3u8");
    REQUIRE(crlf.error.empty());
    REQUIRE(crlf.entries.size() == 1);
    CHECK(crlf.entries[0].uri == "v/p.m3u8");
    CHECK(crlf.entries[0].url == "https://h/v/p.m3u8");
    CHECK(crlf.entries[0].frame_rate == "29.970");
    CHECK(crlf.entries[0].average_bandwidth == "4");
    CHECK_FALSE(crlf.entries[0].video);   // neither RESOLUTION nor CODECS
}

TEST_CASE("vmafladder::parse_master: what isn't a master playlist it reads", "[ladder]")
{
    const std::vector<std::pair<std::string, std::string>> cases = {
        { "", "it is empty, so it isn't an HLS playlist" },
        { "\xEF\xBB\xBF#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\na.m3u8\n",
          "it doesn't start with #EXTM3U, so it isn't an HLS playlist" },
        { "#EXT-X-STREAM-INF:BANDWIDTH=1\na.m3u8\n",
          "it doesn't start with #EXTM3U, so it isn't an HLS playlist" },
        { "#EXTM3U\n#EXT-X-VERSION:3\n",
          "it has no EXT-X-STREAM-INF entry, so it isn't a master playlist" },
        { "#EXTM3U\n#EXT-X-TARGETDURATION:10\n#EXTINF:9.97,\nseg0.ts\n",
          "line 4: a URI without an EXT-X-STREAM-INF before it: a media playlist's segment? "
          "--ladder takes a master playlist" },
        { "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\n#EXT-X-STREAM-INF:BANDWIDTH=2\nb.m3u8\n",
          "line 3: an EXT-X-STREAM-INF before the URI line of the one on line 2" },
        { "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\na.m3u8\n#EXT-X-STREAM-INF:BANDWIDTH=2\n\n",
          "the EXT-X-STREAM-INF on line 4 has no URI line after it" },
        { "#EXTM3U\n#EXT-X-STREAM-INF:RESOLUTION=1x1\na.m3u8\n",
          "line 2: the EXT-X-STREAM-INF has no BANDWIDTH" },
        { "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=../a.m3u8\n",   // value up to the end
          "line 2: BANDWIDTH '../a.m3u8' is not a decimal integer up to 9223372036854775807" },
        { "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=\"../\"\n../a.m3u8\n",
          "line 2: BANDWIDTH '../' is not a decimal integer up to 9223372036854775807" },
        { "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=1\n../a.m3u8\n",
          "line 3: the URI '../a.m3u8' is not one mirage_vmaf resolves against "
          "https://h/x/m.m3u8" },
    };
    for (const auto& [text, want] : cases) {
        INFO(text);
        const Master m = vmafladder::parse_master(text, "https://h/x/m.m3u8");
        CHECK(m.error == want);
        CHECK(m.entries.empty());
    }
    for (const char* bad : { "-5", "+5", "1e6", "5 ", "", "9223372036854775808", "0x10" }) {
        INFO(bad);
        const Master m = vmafladder::parse_master(
            std::format("#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH={},RESOLUTION=1x1\na.m3u8\n", bad),
            "https://h/m.m3u8");
        CHECK(m.error.starts_with("line 2: BANDWIDTH '"));
    }
    const Master big = vmafladder::parse_master(
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=9223372036854775807\na.m3u8\n", "https://h/m.m3u8");
    REQUIRE(big.error.empty());
    CHECK(big.entries[0].bandwidth == 9223372036854775807);
}

// ── The plan of each reference mode ──────────────────────────────────────────

TEST_CASE("vmafladder::plan: top-rung mode on bipbop 16x9", "[ladder]")
{
    const Plan p = vmafladder::plan(vmafladder::parse_master(kBipbop, kBipbopUrl), "");
    REQUIRE(p.error.empty());
    CHECK(p.reference_mode == "top-rung");
    CHECK(p.reference_url == kBipbopDir + "gear5/prog_index.m3u8");
    CHECK(p.reference_entries == std::vector<std::size_t>{ 4 });
    REQUIRE(p.pairs.size() == 4);
    for (std::size_t k = 0; k < 4; ++k) {
        INFO(k);
        CHECK(p.pairs[k].url == kBipbopDir + std::format("gear{}/prog_index.m3u8", k + 1));
        CHECK(p.pairs[k].entries == std::vector<std::size_t>{ k });
    }
    REQUIRE(p.skipped.size() == 1);
    CHECK(p.skipped[0].entry == 5);   // gear0, audio only
    CHECK(p.skipped[0].reason == "no video");
    CHECK(p.warnings.empty());
}

TEST_CASE("vmafladder::plan: mezzanine mode scores the top rung too", "[ladder]")
{
    const Plan p = vmafladder::plan(vmafladder::parse_master(kBipbop, kBipbopUrl), "source.mov");
    REQUIRE(p.error.empty());
    CHECK(p.reference_mode == "mezzanine");
    CHECK(p.reference_url == "source.mov");
    CHECK(p.reference_entries.empty());
    REQUIRE(p.pairs.size() == 5);
    for (std::size_t k = 0; k < 5; ++k) {
        INFO(k);
        CHECK(p.pairs[k].entries == std::vector<std::size_t>{ k });
    }
    CHECK(p.pairs[4].url == kBipbopDir + "gear5/prog_index.m3u8");
    REQUIRE(p.skipped.size() == 1);
    CHECK(p.skipped[0].entry == 5);
}

TEST_CASE("vmafladder::plan: each video playlist once, the reference's other entries with it", "[ladder]")
{
    // Apple's advanced sample's shape: every video playlist once per audio
    // group, with BANDWIDTH counting the audio too.
    const Master m = vmafladder::parse_master(
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=2177116,RESOLUTION=960x540,AUDIO=\"a1\"\nv5/p.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=8001098,RESOLUTION=1920x1080,AUDIO=\"a1\"\nv9/p.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=541052,RESOLUTION=480x270,AUDIO=\"a1\"\nv2/p.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=2399619,RESOLUTION=960x540,AUDIO=\"a2\"\nv5/p.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=8223601,RESOLUTION=1920x1080,AUDIO=\"a2\"\nv9/p.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=763555,RESOLUTION=480x270,AUDIO=\"a2\"\nv2/p.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=50000,CODECS=\"mp4a.40.2\"\nv9/p.m3u8\n",
        "https://h/m.m3u8");
    REQUIRE(m.error.empty());
    const Plan p = vmafladder::plan(m, "");
    REQUIRE(p.error.empty());
    CHECK(p.reference_url == "https://h/v9/p.m3u8");
    CHECK(p.reference_entries == std::vector<std::size_t>{ 1, 4 });
    REQUIRE(p.pairs.size() == 2);
    CHECK(p.pairs[0].url == "https://h/v5/p.m3u8");
    CHECK(p.pairs[0].entries == std::vector<std::size_t>{ 0, 3 });
    CHECK(p.pairs[1].url == "https://h/v2/p.m3u8");
    CHECK(p.pairs[1].entries == std::vector<std::size_t>{ 2, 5 });
    // An entry without video is skipped, even where it names a video playlist.
    REQUIRE(p.skipped.size() == 1);
    CHECK(p.skipped[0].entry == 6);
    CHECK(p.warnings.empty());   // the second-highest BANDWIDTH names the same playlist

    const Plan mz = vmafladder::plan(m, "src.y4m");
    REQUIRE(mz.pairs.size() == 3);
    CHECK(mz.pairs[1].url == "https://h/v9/p.m3u8");
    CHECK(mz.pairs[1].entries == std::vector<std::size_t>{ 1, 4 });
}

TEST_CASE("vmafladder::plan: two playlists with the highest BANDWIDTH", "[ladder]")
{
    const Master m = vmafladder::parse_master(
        "#EXTM3U\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=100,RESOLUTION=1x1\nlow.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=900,CODECS=\"hvc1.1\"\nhevc.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=900,CODECS=\"avc1.64\"\navc.m3u8\n",
        "/d/m.m3u8");
    const Plan p = vmafladder::plan(m, "");
    REQUIRE(p.error.empty());
    CHECK(p.reference_url == "/d/hevc.m3u8");   // the first of them in the master
    CHECK(p.reference_entries == std::vector<std::size_t>{ 1 });
    REQUIRE(p.pairs.size() == 2);
    CHECK(p.pairs[1].url == "/d/avc.m3u8");
    REQUIRE(p.warnings.size() == 1);
    CHECK(p.warnings[0] == "entries 1 and 2 both have the highest BANDWIDTH, 900, and name "
                           "different playlists; the top rung is entry 1, the first of them in "
                           "the master");
    CHECK(vmafladder::plan(m, "src.mov").warnings.empty());   // no top rung in mezzanine mode
}

TEST_CASE("vmafladder::plan: nothing to score", "[ladder]")
{
    const Master audio = vmafladder::parse_master(
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=64000,CODECS=\"mp4a.40.2\"\na.m3u8\n", "/d/m.m3u8");
    CHECK(vmafladder::plan(audio, "").error == "no entry of the master playlist has video");
    CHECK(vmafladder::plan(audio, "src.mov").error == "no entry of the master playlist has video");

    const Master one = vmafladder::parse_master(
        "#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=900,RESOLUTION=2x2\nv.m3u8\n"
        "#EXT-X-STREAM-INF:BANDWIDTH=950,RESOLUTION=2x2,AUDIO=\"b\"\nv.m3u8\n",
        "/d/m.m3u8");
    const Plan top = vmafladder::plan(one, "");
    CHECK(top.error == "the only video playlist of the master is the top rung (entries 0, 1): "
                       "there is nothing to score against it");
    const Plan mz = vmafladder::plan(one, "src.mov");
    REQUIRE(mz.error.empty());
    REQUIRE(mz.pairs.size() == 1);
    CHECK(mz.pairs[0].entries == std::vector<std::size_t>{ 0, 1 });

    Master broken;
    broken.error = "it is empty, so it isn't an HLS playlist";
    CHECK(vmafladder::plan(broken, "").error == broken.error);
    CHECK(vmafladder::plan(broken, "").reference_mode == "top-rung");
}

// ── Reading the master playlist ──────────────────────────────────────────────

TEST_CASE("vmafladder::read_master: a local file, and what isn't one to read", "[ladder]")
{
    const fs::path dir = testtemp::path("mirage_test_ladder_read");
    fs::create_directories(dir);
    const fs::path master = dir / "master.m3u8";
    write_text(master, kBipbop);
    const vmafladder::Fetched f = vmafladder::read_master(master.string());
    REQUIRE(f.error.empty());
    CHECK(f.text == kBipbop);
    CHECK(f.location == master.string());   // no redirect for a file
    CHECK(f.sha256.size() == 64);

    const vmafladder::Fetched missing = vmafladder::read_master((dir / "none.m3u8").string());
    CHECK(missing.error == "No such file or directory");
    CHECK(missing.text.empty());

    // A video file: refused at its first bytes, not read to its end.
    const fs::path video = dir / "video.ts";
    write_text(video, std::string(1 << 20, '\x47'));
    const vmafladder::Fetched not_m3u = vmafladder::read_master(video.string());
    CHECK(not_m3u.error == "it doesn't start with #EXTM3U, so it isn't an HLS playlist");
    CHECK(not_m3u.text.empty());
    CHECK(not_m3u.sha256.empty());

    write_text(dir / "empty.m3u8", "");
    const vmafladder::Fetched empty = vmafladder::read_master((dir / "empty.m3u8").string());
    CHECK(empty.error.empty());   // parse_master() says what is wrong with it
    CHECK(empty.text.empty());
    fs::remove_all(dir);
}

// ── Scoring a ladder ─────────────────────────────────────────────────────────

TEST_CASE("vmafladder::score: top-rung mode, each pair as it scores on its own", "[ladder][vmaf]")
{
    const Ladder& l = ladder();
    const VMAFOptions opts{ .record_pts = true };
    std::vector<std::pair<std::size_t, std::size_t>> calls;
    const vmafladder::Result r = vmafladder::score(
        l.master, "", opts,
        [&](std::size_t k, std::size_t n, const VMAFResult&) { calls.emplace_back(k, n); });
    REQUIRE(r.error.empty());
    CHECK(r.ladder == l.master);
    CHECK(r.fetched.location == l.master);
    CHECK(r.master.entries.size() == 5);
    CHECK(r.plan.reference_mode == "top-rung");
    CHECK(r.plan.reference_url == l.top);
    CHECK(r.plan.reference_entries == std::vector<std::size_t>{ 2 });
    REQUIRE(r.plan.pairs.size() == 2);
    CHECK(r.plan.pairs[0].entries == std::vector<std::size_t>{ 0 });
    CHECK(r.plan.pairs[1].entries == std::vector<std::size_t>{ 1, 3 });
    REQUIRE(r.plan.skipped.size() == 1);
    CHECK(r.plan.skipped[0].entry == 4);   // never opened: its playlist doesn't exist
    CHECK(calls == std::vector<std::pair<std::size_t, std::size_t>>{ { 0, 2 }, { 1, 2 } });

    REQUIRE(r.results.size() == 2);
    CHECK(r.results[0].per_frame.size() == 12);
    check_same_pair(r.results[0], VMAFAnalyzer::analyze(l.top, l.low, opts));
    check_same_pair(r.results[1], VMAFAnalyzer::analyze(l.top, l.mid, opts));
    CHECK(r.results[0].per_frame != r.results[1].per_frame);   // two rungs, two series
}

TEST_CASE("vmafladder::score: mezzanine mode, the frame limit of each pair", "[ladder][vmaf]")
{
    const Ladder& l = ladder();
    const std::string mezzanine = make_rung(l.dir / "mezzanine", 192, 108);
    const VMAFOptions opts{ .max_frames = 5, .record_pts = true, .scaler = VMAFScaler::Lanczos };
    const vmafladder::Result r = vmafladder::score(l.master, mezzanine, opts);
    REQUIRE(r.error.empty());
    CHECK(r.plan.reference_mode == "mezzanine");
    CHECK(r.plan.reference_url == mezzanine);
    CHECK(r.plan.reference_entries.empty());
    REQUIRE(r.plan.pairs.size() == 3);   // low, mid, top
    CHECK(r.plan.pairs[2].url == l.top);
    REQUIRE(r.results.size() == 3);
    const std::vector<std::string> rungs = { l.low, l.mid, l.top };
    for (std::size_t k = 0; k < 3; ++k) {
        INFO(k);
        CHECK(r.results[k].per_frame.size() == 5);
        CHECK(r.results[k].settings.scaler == "lanczos");
        check_same_pair(r.results[k], VMAFAnalyzer::analyze(mezzanine, rungs[k], opts));
    }
}

TEST_CASE("vmafladder::score: a pair that can't be scored ends the run", "[ladder][vmaf]")
{
    const Ladder& l = ladder();
    const fs::path dir = testtemp::path("mirage_test_ladder_broken");
    fs::create_directories(dir);
    const std::string master = (dir / "master.m3u8").string();
    // The second pair's playlist doesn't exist; the third would be scored.
    write_text(master, std::format("#EXTM3U\n"
                                   "#EXT-X-STREAM-INF:BANDWIDTH=900000,RESOLUTION=192x108\n{}\n"
                                   "#EXT-X-STREAM-INF:BANDWIDTH=300000,RESOLUTION=96x54\n{}\n"
                                   "#EXT-X-STREAM-INF:BANDWIDTH=200000,RESOLUTION=96x54\n"
                                   "missing/index.m3u8\n"
                                   "#EXT-X-STREAM-INF:BANDWIDTH=600000,RESOLUTION=128x72\n{}\n",
                                   l.top, l.low, l.mid));
    int calls = 0;
    const vmafladder::Result r = vmafladder::score(
        master, "", {}, [&](std::size_t, std::size_t, const VMAFResult&) { ++calls; });
    const std::string missing = (dir / "missing/index.m3u8").string();
    CHECK(r.error == std::format("pair 2 of 3, {} against {}: cannot open distorted: {}", missing,
                                 l.top, missing));
    REQUIRE(r.results.size() == 2);   // the failed pair's result is the last
    CHECK(r.results[0].error.empty());
    CHECK_FALSE(r.results[1].error.empty());
    CHECK(calls == 1);

    const vmafladder::Result none = vmafladder::score((dir / "none.m3u8").string(), "", {});
    CHECK(none.error == std::format("cannot read the master playlist {}: No such file or directory",
                                    (dir / "none.m3u8").string()));
    CHECK(none.results.empty());

    write_text(dir / "media.m3u8", "#EXTM3U\n#EXTINF:1,\nseg.ts\n");
    const vmafladder::Result media = vmafladder::score((dir / "media.m3u8").string(), "", {});
    CHECK(media.error == std::format("the master playlist {}: line 3: a URI without an "
                                     "EXT-X-STREAM-INF before it: a media playlist's segment? "
                                     "--ladder takes a master playlist",
                                     (dir / "media.m3u8").string()));
    fs::remove_all(dir);
}

TEST_CASE("vmafladder::score: a model file that changes during the run ends it", "[ladder][vmaf]")
{
    const fs::path file = fs::path(MIRAGE_LIBVMAF_MODEL_DIR) / "vmaf_v0.6.1.json";
    if (!fs::exists(file)) SKIP("no libvmaf model file at " << file.string());
    const Ladder& l = ladder();
    const fs::path copy = testtemp::path("mirage_test_ladder_model.json");
    fs::copy_file(file, copy, fs::copy_options::overwrite_existing);
    // After the first pair, the file gets one more newline: other bytes, the
    // same model, so the second pair's settings name another SHA-256.
    const vmafladder::Result r = vmafladder::score(
        l.master, "", { .model = { .path = copy.string() } },
        [&](std::size_t k, std::size_t, const VMAFResult&) {
            if (k == 0) std::ofstream(copy, std::ios::binary | std::ios::app) << '\n';
        });
    REQUIRE(r.results.size() == 2);
    const std::string before = r.results[0].settings.model_sha256;
    const std::string after  = r.results[1].settings.model_sha256;
    CHECK(before != after);
    CHECK(r.error == std::format("pair 2 of 2 was scored with other settings than pair 1, model {} "
                                 "with the SHA-256 {} against {}: did the model file change "
                                 "during the run?",
                                 copy.string(), after, before));
    fs::remove(copy);
}
