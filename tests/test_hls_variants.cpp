// HLS helpers opening one variant's media playlist instead of the master:
// the stream → variant mapping (hls::variant_of_stream, pure), and against a
// generated local HLS master shaped like Apple's bipbop (every video playlist
// listed once per audio group, audio as EXT-X-MEDIA renditions) the FFmpeg
// structure that mapping relies on, ffopen::open_variant's checks, and that
// the variant playlist yields the same timestamps as the master does.

#include <catch2/catch_test_macros.hpp>

#include "Demuxer.h"
#include "FfOpen.h"
#include "HlsMaster.h"
#include "HlsVariants.h"
#include "ManifestParser.h"
#include "NetworkLogger.h"
#include "TestTemp.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stop_token>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/frame.h>
}

namespace fs = std::filesystem;

namespace {

VariantStream variant(const char* url, std::int64_t bw)
{
    VariantStream v;
    v.url       = url;
    v.bandwidth = bw;
    return v;
}

// bipbop-like: 2 video playlists × 2 audio groups. Streams (FFmpeg order:
// renditions first as listed): 0 = a1, 1 = a2, 2..5 = the variants' own video.
struct Fixture {
    std::vector<VariantStream>     variants{ variant("v0.m3u8", 200), variant("v1.m3u8", 100),
                                             variant("v0.m3u8", 210), variant("v1.m3u8", 110) };
    std::vector<MediaRendition>    renditions;
    std::vector<hls::ProgramInfo>  programs{ { 200, { 2, 0 } }, { 100, { 3, 0 } },
                                             { 210, { 4, 1 } }, { 110, { 5, 1 } } };
};

} // namespace

TEST_CASE("variant_of_stream: a variant's own stream maps to its EXT-X-STREAM-INF", "[hls_variants]")
{
    Fixture f;
    CHECK(hls::variant_of_stream(f.programs, 2, 200, f.variants, f.renditions) == 0);
    CHECK(hls::variant_of_stream(f.programs, 3, 100, f.variants, f.renditions) == 1);
    // The same playlist listed again for the other audio group is its own entry.
    CHECK(hls::variant_of_stream(f.programs, 4, 210, f.variants, f.renditions) == 2);
    CHECK(hls::variant_of_stream(f.programs, 5, 110, f.variants, f.renditions) == 3);
}

TEST_CASE("variant_of_stream: renditions and inconsistent lists are not mapped", "[hls_variants]")
{
    Fixture f;
    SECTION("an audio rendition shared by a group (several programs)") {
        CHECK(hls::variant_of_stream(f.programs, 0, -1, f.variants, f.renditions) == -1);
        CHECK(hls::variant_of_stream(f.programs, 1, -1, f.variants, f.renditions) == -1);
    }
    SECTION("a stream in no program / negative index") {
        CHECK(hls::variant_of_stream(f.programs, 9, 200, f.variants, f.renditions) == -1);
        CHECK(hls::variant_of_stream(f.programs, -1, 200, f.variants, f.renditions) == -1);
    }
    SECTION("the stream's own variant_bitrate is not its program's") {
        CHECK(hls::variant_of_stream(f.programs, 2, -1, f.variants, f.renditions) == -1);
        CHECK(hls::variant_of_stream(f.programs, 2, 210, f.variants, f.renditions) == -1);
    }
    SECTION("program count differs from the parsed master") {
        f.programs.pop_back();
        CHECK(hls::variant_of_stream(f.programs, 2, 200, f.variants, f.renditions) == -1);
    }
    SECTION("a program's BANDWIDTH differs from the parsed master's") {
        f.programs[3].variant_bitrate = 111;
        CHECK(hls::variant_of_stream(f.programs, 2, 200, f.variants, f.renditions) == -1);
    }
    SECTION("single-variant master") {
        f.variants.resize(1);
        f.programs.resize(1);
        CHECK(hls::variant_of_stream(f.programs, 2, 200, f.variants, f.renditions) == -1);
    }
    SECTION("alternative video renditions could pass for a variant's own stream") {
        MediaRendition r;
        r.type = "VIDEO";
        r.url  = "angle2.m3u8";
        f.renditions.push_back(r);
        CHECK(hls::variant_of_stream(f.programs, 2, 200, f.variants, f.renditions) == -1);
    }
    SECTION("variant without a URL") {
        f.variants[0].url.clear();
        CHECK(hls::variant_of_stream(f.programs, 2, 200, f.variants, f.renditions) == -1);
    }
}

TEST_CASE("variant_of_stream: audio / subtitle renditions don't block the mapping", "[hls_variants]")
{
    Fixture f;
    MediaRendition a;
    a.type = "AUDIO";
    a.url  = "a1.m3u8";
    MediaRendition s;
    s.type = "SUBTITLES";
    s.url  = "s1.m3u8";
    MediaRendition cc;
    cc.type = "CLOSED-CAPTIONS";   // in-band, no URI
    MediaRendition v;
    v.type = "VIDEO";              // no URI: rendered from the variant itself
    f.renditions = { a, s, cc, v };
    CHECK(hls::variant_of_stream(f.programs, 3, 100, f.variants, f.renditions) == 1);
}

// ── Against FFmpeg's hls demuxer on a generated master ─────────────────────

namespace {

constexpr int kFps  = 25;
constexpr int kSecs = 4;

void write_frames(AVCodecContext* enc, AVFormatContext* oc, AVStream* st, AVFrame* f)
{
    AVPacket* pkt = av_packet_alloc();
    REQUIRE(avcodec_send_frame(enc, f) >= 0);
    while (avcodec_receive_packet(enc, pkt) == 0) {
        av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
        pkt->stream_index = 0;
        REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
    }
    av_packet_free(&pkt);
}

// dir/index.m3u8 with 1 s MPEG-TS segments of `enc`'s output; `fill`
// prepares frame i (or the encoder is flushed).
template <typename Fill>
void write_playlist(const fs::path& dir, AVCodecContext* enc, int n_frames, AVFrame* f, Fill fill)
{
    fs::create_directories(dir);
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "hls", (dir / "index.m3u8").c_str()) >= 0);
    AVStream* st = avformat_new_stream(oc, nullptr);
    REQUIRE(avcodec_parameters_from_context(st->codecpar, enc) >= 0);
    st->time_base = enc->time_base;
    AVDictionary* o = nullptr;
    av_dict_set(&o, "hls_time", "1", 0);
    av_dict_set(&o, "hls_playlist_type", "vod", 0);
    av_dict_set(&o, "hls_segment_filename", (dir / "seg%d.ts").c_str(), 0);
    REQUIRE(avformat_write_header(oc, &o) >= 0);
    av_dict_free(&o);
    for (int i = 0; i < n_frames; ++i) {
        REQUIRE(av_frame_make_writable(f) >= 0);
        fill(f, i);
        write_frames(enc, oc, st, f);
    }
    write_frames(enc, oc, st, nullptr);   // flush
    REQUIRE(av_write_trailer(oc) >= 0);
    avformat_free_context(oc);
}

void make_video_playlist(const fs::path& dir, int w, int h)
{
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
    REQUIRE(codec);
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    enc->width        = w;
    enc->height       = h;
    enc->pix_fmt      = AV_PIX_FMT_YUV420P;
    enc->time_base    = { 1, kFps };
    enc->framerate    = { kFps, 1 };
    enc->gop_size     = kFps;      // keyframe every second, segments start on one
    enc->max_b_frames = 2;         // pts != dts, as in real streams
    enc->bit_rate     = 200000;
    REQUIRE(avcodec_open2(enc, codec, nullptr) >= 0);
    AVFrame* f = av_frame_alloc();
    f->format = enc->pix_fmt;
    f->width  = w;
    f->height = h;
    REQUIRE(av_frame_get_buffer(f, 0) >= 0);
    write_playlist(dir, enc, kSecs * kFps, f, [&](AVFrame* fr, int i) {
        for (int y = 0; y < h; ++y)
            std::memset(fr->data[0] + y * fr->linesize[0], (i * 9 + y * 3) & 0xFF, static_cast<size_t>(w));
        for (int p = 1; p < 3; ++p)
            for (int y = 0; y < h / 2; ++y)
                std::memset(fr->data[p] + y * fr->linesize[p], 128, static_cast<size_t>(w / 2));
        fr->pts = i;
    });
    av_frame_free(&f);
    avcodec_free_context(&enc);
}

void make_audio_playlist(const fs::path& dir)
{
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MP2);
    REQUIRE(codec);
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    enc->sample_rate = 48000;
    enc->sample_fmt  = AV_SAMPLE_FMT_S16;
    enc->bit_rate    = 64000;
    enc->time_base   = { 1, 48000 };
    av_channel_layout_default(&enc->ch_layout, 1);
    REQUIRE(avcodec_open2(enc, codec, nullptr) >= 0);
    AVFrame* f = av_frame_alloc();
    f->format     = enc->sample_fmt;
    f->nb_samples = enc->frame_size;
    REQUIRE(av_channel_layout_copy(&f->ch_layout, &enc->ch_layout) >= 0);
    REQUIRE(av_frame_get_buffer(f, 0) >= 0);
    const int n = kSecs * 48000 / enc->frame_size;
    write_playlist(dir, enc, n, f, [&](AVFrame* fr, int i) {
        std::memset(fr->data[0], 0, static_cast<size_t>(fr->nb_samples) * 2);
        fr->pts = static_cast<int64_t>(i) * fr->nb_samples;
    });
    av_frame_free(&f);
    avcodec_free_context(&enc);
}

// master.m3u8: a1/a2 audio groups; v0 (64x48) and v1 (32x32), each listed
// once per group — FFmpeg gives every entry its own playlist and streams.
fs::path make_master()
{
    const fs::path root = testtemp::path("mirage_test_hls_variants");
    static bool made = false;   // once per test run, never a stale fixture
    if (made) return root;
    made = true;
    fs::remove_all(root);
    make_video_playlist(root / "v0", 64, 48);
    make_video_playlist(root / "v1", 32, 32);
    make_audio_playlist(root / "a1");
    make_audio_playlist(root / "a2");
    std::ofstream m(root / "master.m3u8");
    m << "#EXTM3U\n#EXT-X-VERSION:3\n"
         "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"g1\",NAME=\"A1\",DEFAULT=YES,URI=\"a1/index.m3u8\"\n"
         "#EXT-X-MEDIA:TYPE=AUDIO,GROUP-ID=\"g2\",NAME=\"A2\",DEFAULT=YES,URI=\"a2/index.m3u8\"\n"
         "#EXT-X-STREAM-INF:BANDWIDTH=300000,RESOLUTION=64x48,AUDIO=\"g1\"\nv0/index.m3u8\n"
         "#EXT-X-STREAM-INF:BANDWIDTH=150000,RESOLUTION=32x32,AUDIO=\"g1\"\nv1/index.m3u8\n"
         "#EXT-X-STREAM-INF:BANDWIDTH=310000,RESOLUTION=64x48,AUDIO=\"g2\"\nv0/index.m3u8\n"
         "#EXT-X-STREAM-INF:BANDWIDTH=160000,RESOLUTION=32x32,AUDIO=\"g2\"\nv1/index.m3u8\n";
    return root;
}

AVFormatContext* open_probed(const std::string& url)
{
    AVFormatContext* fc = nullptr;
    REQUIRE(avformat_open_input(&fc, url.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(avformat_find_stream_info(fc, nullptr) >= 0);
    return fc;
}

// Seconds of the first `n` packets of `stream` (everything else discarded),
// optionally after a seek to `seek_s`.
std::vector<double> packet_times(AVFormatContext* fc, int stream, int n, double seek_s = -1.0)
{
    for (unsigned i = 0; i < fc->nb_streams; ++i)
        fc->streams[i]->discard = static_cast<int>(i) == stream ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    const AVRational tb = fc->streams[stream]->time_base;
    if (seek_s >= 0.0) {
        const int64_t ts = static_cast<int64_t>(seek_s / av_q2d(tb));
        REQUIRE(avformat_seek_file(fc, stream, INT64_MIN, ts, ts, 0) >= 0);
    }
    std::vector<double> out;
    AVPacket* pkt = av_packet_alloc();
    while (static_cast<int>(out.size()) < n && av_read_frame(fc, pkt) >= 0) {
        if (pkt->stream_index == stream && pkt->pts != AV_NOPTS_VALUE)
            out.push_back(pkt->pts * av_q2d(tb));
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    return out;
}

} // namespace

TEST_CASE("HLS master → variant playlist: FFmpeg structure, open_variant checks, same timeline",
          "[hls_variants][ffmpeg]")
{
    const fs::path root = make_master();
    const std::string master = (root / "master.m3u8").string();

    ManifestParser mp;
    REQUIRE(mp.parse(master));
    REQUIRE(mp.variants().size() == 4);

    AVFormatContext* fc = open_probed(master);
    REQUIRE(std::string(fc->iformat->name) == "hls");
    REQUIRE(fc->nb_programs == 4);

    // Every video stream maps to the variant whose playlist it reads; the
    // audio renditions (shared by a group's two variants) map to none.
    int mapped = 0;
    std::vector<int> seen_variants;
    for (unsigned i = 0; i < fc->nb_streams; ++i) {
        const AVCodecParameters* par = fc->streams[i]->codecpar;
        const int v = hls::variant_index_for(fc, static_cast<int>(i), mp);
        if (par->codec_type != AVMEDIA_TYPE_VIDEO) {
            CHECK(v == -1);
            continue;
        }
        REQUIRE(v >= 0);
        ++mapped;
        seen_variants.push_back(v);
        const VariantStream& vs = mp.variants()[static_cast<std::size_t>(v)];
        CHECK(vs.width == par->width);
        CHECK(vs.height == par->height);
    }
    CHECK(mapped == 4);
    std::sort(seen_variants.begin(), seen_variants.end());
    CHECK(seen_variants == std::vector<int>{ 0, 1, 2, 3 });

    // The 64x48 stream of the first entry, opened through its own playlist.
    int vstream = -1;
    for (unsigned i = 0; i < fc->nb_streams && vstream < 0; ++i)
        if (hls::variant_index_for(fc, static_cast<int>(i), mp) == 0) vstream = static_cast<int>(i);
    REQUIRE(vstream >= 0);
    const AVCodecParameters* par = fc->streams[vstream]->codecpar;
    const ffopen::VariantInput in{ mp.variants()[0].url, static_cast<int>(par->codec_id), par->width, par->height };

    std::stop_source ss;
    AVFormatContext* vc = nullptr;
    int vidx = -1;
    REQUIRE(ffopen::open_variant(&vc, &vidx, in, ss.get_token(), NetInitiator::Other) == 0);
    REQUIRE(vc);
    REQUIRE(vidx >= 0);
    CHECK(vc->streams[vidx]->codecpar->width == 64);
    CHECK(av_cmp_q(vc->streams[vidx]->time_base, fc->streams[vstream]->time_base) == 0);

    SECTION("same packet timestamps from the start") {
        const auto a = packet_times(fc, vstream, 60);
        const auto b = packet_times(vc, vidx, 60);
        REQUIRE(a.size() == 60);
        CHECK(a == b);
    }
    SECTION("same keyframe after a seek") {
        const auto a = packet_times(fc, vstream, 5, 2.5);
        const auto b = packet_times(vc, vidx, 5, 2.5);
        REQUIRE_FALSE(a.empty());
        CHECK(a == b);
    }
    avformat_close_input(&vc);
    avformat_close_input(&fc);
}

TEST_CASE("open_variant rejects what doesn't match the playback stream", "[hls_variants][ffmpeg]")
{
    const fs::path root = make_master();
    std::stop_source ss;
    AVFormatContext* vc = nullptr;
    int vidx = -1;
    const std::string v0 = (root / "v0" / "index.m3u8").string();

    SECTION("different frame size") {
        const ffopen::VariantInput in{ v0, AV_CODEC_ID_MPEG2VIDEO, 32, 32 };
        CHECK(ffopen::open_variant(&vc, &vidx, in, ss.get_token(), NetInitiator::Other) < 0);
    }
    SECTION("different codec") {
        const ffopen::VariantInput in{ v0, AV_CODEC_ID_H264, 0, 0 };
        CHECK(ffopen::open_variant(&vc, &vidx, in, ss.get_token(), NetInitiator::Other) < 0);
    }
    SECTION("an audio-only playlist") {
        const ffopen::VariantInput in{ (root / "a1" / "index.m3u8").string(), AV_CODEC_ID_MPEG2VIDEO, 0, 0 };
        CHECK(ffopen::open_variant(&vc, &vidx, in, ss.get_token(), NetInitiator::Other) < 0);
    }
    SECTION("not an HLS playlist (a segment)") {
        const ffopen::VariantInput in{ (root / "v0" / "seg0.ts").string(), AV_CODEC_ID_MPEG2VIDEO, 0, 0 };
        CHECK(ffopen::open_variant(&vc, &vidx, in, ss.get_token(), NetInitiator::Other) < 0);
    }
    SECTION("no URL") {
        CHECK(ffopen::open_variant(&vc, &vidx, {}, ss.get_token(), NetInitiator::Other) < 0);
    }
    CHECK(vc == nullptr);
    CHECK(vidx == -1);
}

TEST_CASE("Demuxer on a deduplicated master copy: fewer playlists, every rendition still read",
          "[hls_variants][ffmpeg]")
{
    const fs::path root = make_master();
    const std::string master = (root / "master.m3u8").string();
    std::ifstream in(master);
    std::stringstream buf;
    buf << in.rdbuf();

    ManifestParser mp;
    REQUIRE(mp.parse(master));

    // The rewrite resolves against an http(s) master; point its URIs back
    // at the fixture files. They need a scheme, as the http(s) ones have:
    // FFmpeg resolves a bare "/path" against the data: URL it opened.
    const std::string stand_in = "http://fixture.invalid/";
    auto copy = hls::dedupe_master(buf.str(), stand_in + "master.m3u8");
    REQUIRE(copy);
    // v0/g1 and v1/g1 (first of each playlist), plus g2's cheapest entry v1/g2.
    REQUIRE(copy->kept == std::vector<int>{ 0, 1, 3 });
    std::string text = copy->text;
    for (std::size_t p = text.find(stand_in); p != std::string::npos; p = text.find(stand_in, p))
        text.replace(p, stand_in.size(), "file:" + root.string() + "/");

    Demuxer as_is;
    REQUIRE(as_is.open(master));
    CHECK_FALSE(as_is.opened_master_copy());
    CHECK(as_is.fmt_ctx()->nb_programs == 4);

    Demuxer d;
    REQUIRE(d.open(master, text));
    REQUIRE(d.opened_master_copy());
    AVFormatContext* fc = d.fmt_ctx();
    CHECK(fc->nb_programs == 3);
    CHECK(fc->nb_streams == 5);   // a1, a2 + three video playlists instead of four

    // Each video stream maps, through `kept`, to the entry it was copied from.
    for (unsigned i = 0; i < fc->nb_streams; ++i) {
        const AVCodecParameters* par = fc->streams[i]->codecpar;
        const int v = hls::variant_index_for(fc, static_cast<int>(i), mp, copy->kept);
        if (par->codec_type != AVMEDIA_TYPE_VIDEO) { CHECK(v == -1); continue; }
        REQUIRE(v >= 0);
        CHECK(std::find(copy->kept.begin(), copy->kept.end(), v) != copy->kept.end());
        CHECK(mp.variants()[static_cast<std::size_t>(v)].width == par->width);
    }

    // Playback starts on the same rendition pair as with the master as served.
    auto group_of_audio = [&](const AVFormatContext* c, int a, const std::vector<int>& kept) {
        const auto opened = hls::opened_variants(mp.variants(), kept);
        const auto progs  = hls::programs_of(c);
        for (std::size_t p = 0; p < progs.size(); ++p)
            if (std::find(progs[p].streams.begin(), progs[p].streams.end(), a) != progs[p].streams.end())
                return opened[p].audio_group;
        return std::string();
    };
    REQUIRE(d.video_stream_index() >= 0);
    REQUIRE(d.audio_stream_index() >= 0);
    CHECK(fc->streams[d.video_stream_index()]->codecpar->width
          == as_is.fmt_ctx()->streams[as_is.video_stream_index()]->codecpar->width);
    CHECK(group_of_audio(fc, d.audio_stream_index(), copy->kept)
          == group_of_audio(as_is.fmt_ctx(), as_is.audio_stream_index(), {}));
    CHECK(hls::playing_variant(hls::variant_index_for(fc, d.video_stream_index(), mp, copy->kept),
                               d.audio_stream_index(), hls::programs_of(fc), mp.variants(), copy->kept)
          == hls::playing_variant(hls::variant_index_for(as_is.fmt_ctx(), as_is.video_stream_index(), mp),
                                  as_is.audio_stream_index(), hls::programs_of(as_is.fmt_ctx()),
                                  mp.variants(), {}));

    // The g2 rendition only reaches FFmpeg through the cheap v1/g2 entry:
    // with the 64x48 video it is still read.
    const auto progs = hls::programs_of(fc);
    int a2 = -1, v0 = -1;
    for (const int s : progs[2].streams)
        if (fc->streams[s]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) a2 = s;
    for (unsigned i = 0; i < fc->nb_streams; ++i)
        if (fc->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && fc->streams[i]->codecpar->width == 64)
            v0 = static_cast<int>(i);
    REQUIRE(a2 >= 0);
    REQUIRE(v0 >= 0);
    for (unsigned i = 0; i < fc->nb_streams; ++i)
        fc->streams[i]->discard = (static_cast<int>(i) == a2 || static_cast<int>(i) == v0) ? AVDISCARD_DEFAULT
                                                                                           : AVDISCARD_ALL;
    int got_a = 0, got_v = 0;
    AVPacket* pkt = av_packet_alloc();
    for (int n = 0; n < 400 && (got_a < 5 || got_v < 5) && av_read_frame(fc, pkt) >= 0; ++n) {
        got_a += pkt->stream_index == a2;
        got_v += pkt->stream_index == v0;
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    CHECK(got_a >= 5);
    CHECK(got_v >= 5);
}

TEST_CASE("NetworkLogger: a data: URL (master copy) is not a request row, a file open is",
          "[hls_variants][network_logger]")
{
    const fs::path root = make_master();
    NetworkLogger log;
    log.install();
    auto open_with = [](const std::string& url, const char* fmt) {
        AVFormatContext* fc = avformat_alloc_context();
        NetworkLogger::hook_format_context(fc, NetInitiator::Playback);
        const int r = avformat_open_input(&fc, url.c_str(), fmt ? av_find_input_format(fmt) : nullptr, nullptr);
        if (r >= 0) avformat_close_input(&fc);
        return r;
    };
    const std::string data = ffopen::hls_data_url(std::string(64, '\0'));
    REQUIRE(data.starts_with("data:application/vnd.apple.mpegurl;base64,"));
    CHECK(open_with(data, "s16le") >= 0);
    CHECK(log.size() == 0);
    CHECK(open_with((root / "v0" / "seg0.ts").string(), "mpegts") >= 0);
    CHECK(log.size() == 1);
    log.uninstall();
}
