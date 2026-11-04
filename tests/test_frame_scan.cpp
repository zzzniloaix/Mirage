// FrameScan on clips encoded here through libavcodec / libavformat (in a
// temporary directory removed afterwards): picture types, keyframes,
// timestamps and H.264 reference flags as the parsers report them per codec
// and container, a codec without a parser, failures, stop() and destruction
// mid-scan; and the pure helpers (display order, rolling / peak bitrate, GOP
// summary, H.264 nal_ref_idc) on hand-built records with known numbers.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "FrameScan.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <unistd.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
}

namespace fs = std::filesystem;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using framescan::Frame;
using Clock = std::chrono::steady_clock;

namespace {

// A directory of its own under the temp dir, removed with everything in it.
struct TempDir {
    fs::path path;
    TempDir()
    {
        static int n = 0;
        path = fs::temp_directory_path() / std::format("mirage_test_framescan_{}_{}", getpid(), ++n);
        fs::create_directories(path);
    }
    ~TempDir()
    {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
    TempDir(const TempDir&)            = delete;
    TempDir& operator=(const TempDir&) = delete;
};

// What to encode: `frames` frames at `fps` (pts 0, 1, 2 ... in 1/fps) of a
// pattern that moves every frame, a keyframe every `gop` frames (scene-cut
// detection is turned off through `options`).
struct Clip {
    const char* encoder       = "libx264";
    const char* format        = "matroska";   // muxer
    int         width         = 64;
    int         height        = 64;
    int         fps           = 24;
    int         frames        = 48;
    int         gop           = 12;
    int         b_frames      = 0;
    bool        global_header = true;         // parameter sets in the container (avcC / hvcC), not in-band
    std::vector<std::pair<const char*, const char*>> options;   // encoder private options
};

// Encodes `c` into `path`. False when this FFmpeg build has no such encoder.
bool encode(const fs::path& path, const Clip& c)
{
    const AVCodec* codec = avcodec_find_encoder_by_name(c.encoder);
    if (!codec) return false;
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    REQUIRE(enc);
    enc->width        = c.width;
    enc->height       = c.height;
    enc->pix_fmt      = AV_PIX_FMT_YUV420P;
    enc->time_base    = { 1, c.fps };
    enc->framerate    = { c.fps, 1 };
    enc->gop_size     = c.gop;
    enc->keyint_min   = c.gop;
    enc->max_b_frames = c.b_frames;
    if (c.global_header) enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* eo = nullptr;
    for (const auto& [k, v] : c.options) av_dict_set(&eo, k, v, 0);
    REQUIRE(avcodec_open2(enc, codec, &eo) >= 0);
    av_dict_free(&eo);

    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, c.format, path.c_str()) >= 0);
    AVStream* st = avformat_new_stream(oc, nullptr);
    REQUIRE(avcodec_parameters_from_context(st->codecpar, enc) >= 0);
    st->time_base = enc->time_base;
    REQUIRE(avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);

    AVPacket* pkt = av_packet_alloc();
    auto send = [&](const AVFrame* f) {
        REQUIRE(avcodec_send_frame(enc, f) >= 0);
        while (avcodec_receive_packet(enc, pkt) == 0) {
            // One frame. Some encoders leave it 0, and then an MP4's edit
            // list ends where the last frame starts: that frame is hidden.
            if (pkt->duration <= 0) pkt->duration = 1;
            av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
            pkt->stream_index = 0;
            REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
        }
    };
    AVFrame* f = av_frame_alloc();
    f->format = enc->pix_fmt;
    f->width  = enc->width;
    f->height = enc->height;
    REQUIRE(av_frame_get_buffer(f, 0) >= 0);
    for (int i = 0; i < c.frames; ++i) {
        REQUIRE(av_frame_make_writable(f) >= 0);
        for (int y = 0; y < f->height; ++y)
            for (int x = 0; x < f->width; ++x)
                f->data[0][y * f->linesize[0] + x] = static_cast<std::uint8_t>((x * 2 + y * 3 + i * 5) & 0xFF);
        for (int p = 1; p < 3; ++p)
            for (int y = 0; y < f->height / 2; ++y)
                std::memset(f->data[p] + y * f->linesize[p], 128 + (i & 15), static_cast<std::size_t>(f->width / 2));
        f->pts = i;
        send(f);
    }
    send(nullptr);
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    av_frame_free(&f);
    av_packet_free(&pkt);
    avcodec_free_context(&enc);
    return true;
}

// Raw 8-bit grey video (a codec with no parser) in NUT, packet by packet:
// `frames` frames of w x h at `fps`, every one a keyframe. with_audio: a mono
// PCM track comes first (stream 0) and the video is stream 1.
void write_raw_nut(const fs::path& path, int frames, int w, int h, int fps, bool with_audio)
{
    constexpr int kRate = 8000, kAudioFrame = 400;   // 50 ms PCM packets
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "nut", path.c_str()) >= 0);
    AVStream* as = nullptr;
    if (with_audio) {
        as = avformat_new_stream(oc, nullptr);
        as->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
        as->codecpar->codec_id    = AV_CODEC_ID_PCM_S16LE;
        as->codecpar->sample_rate = kRate;
        as->codecpar->format      = AV_SAMPLE_FMT_S16;
        av_channel_layout_default(&as->codecpar->ch_layout, 1);
        as->time_base             = { 1, kRate };
    }
    AVStream* vs = avformat_new_stream(oc, nullptr);
    vs->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    vs->codecpar->codec_id   = AV_CODEC_ID_RAWVIDEO;
    vs->codecpar->format     = AV_PIX_FMT_GRAY8;
    vs->codecpar->width      = w;
    vs->codecpar->height     = h;
    vs->time_base            = { 1, fps };
    vs->avg_frame_rate       = { fps, 1 };
    REQUIRE(avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);

    // One check at the end: a long clip has hundreds of thousands of packets.
    bool ok = true;
    AVPacket* pkt = av_packet_alloc();
    int ai = 0;
    for (int vi = 0; vi < frames && ok; ++vi) {
        while (as && static_cast<std::int64_t>(ai) * kAudioFrame * fps <= static_cast<std::int64_t>(vi) * kRate) {
            ok = ok && av_new_packet(pkt, kAudioFrame * 2) >= 0;
            if (!ok) break;
            std::memset(pkt->data, 0, static_cast<std::size_t>(pkt->size));
            pkt->stream_index = as->index;
            pkt->pts = pkt->dts = static_cast<std::int64_t>(ai) * kAudioFrame;
            pkt->duration       = kAudioFrame;
            pkt->flags         |= AV_PKT_FLAG_KEY;
            av_packet_rescale_ts(pkt, { 1, kRate }, as->time_base);
            ok = ok && av_interleaved_write_frame(oc, pkt) >= 0;
            ++ai;
        }
        ok = ok && av_new_packet(pkt, w * h) >= 0;
        if (!ok) break;
        std::memset(pkt->data, vi & 0xFF, static_cast<std::size_t>(pkt->size));
        pkt->stream_index = vs->index;
        pkt->pts = pkt->dts = vi;
        pkt->duration       = 1;
        pkt->flags         |= AV_PKT_FLAG_KEY;
        av_packet_rescale_ts(pkt, { 1, fps }, vs->time_base);
        ok = ok && av_interleaved_write_frame(oc, pkt) >= 0;
    }
    REQUIRE(ok);
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    av_packet_free(&pkt);
}

// `src` (an MP4 of one video stream) cut at `cut` seconds into the MP4
// `dst`, as a stream copy does it: from the keyframe at or before the cut,
// timestamps shifted so that the cut is at 0. The frames before it keep
// negative times: the muxer writes an edit list starting at the cut.
void remux_cut(const fs::path& src, const fs::path& dst, double cut)
{
    AVFormatContext* ic = nullptr;
    REQUIRE(avformat_open_input(&ic, src.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(avformat_find_stream_info(ic, nullptr) >= 0);
    AVStream* is = ic->streams[0];
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "mp4", dst.c_str()) >= 0);
    AVStream* os = avformat_new_stream(oc, nullptr);
    REQUIRE(avcodec_parameters_copy(os->codecpar, is->codecpar) >= 0);
    os->codecpar->codec_tag = 0;
    os->time_base           = is->time_base;
    REQUIRE(avio_open(&oc->pb, dst.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);

    const std::int64_t cut_ts = std::llround(cut / av_q2d(is->time_base));
    REQUIRE(av_seek_frame(ic, 0, cut_ts, AVSEEK_FLAG_BACKWARD) >= 0);
    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(ic, pkt) >= 0) {
        pkt->pts -= cut_ts;
        pkt->dts -= cut_ts;
        av_packet_rescale_ts(pkt, is->time_base, os->time_base);
        pkt->stream_index = 0;
        pkt->pos          = -1;
        REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
    }
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    av_packet_free(&pkt);
    avformat_close_input(&ic);
}

// Polls `pred` until it holds, for at most `limit`; its last value.
template <typename Pred>
bool wait_for(Pred pred, std::chrono::milliseconds limit = std::chrono::seconds(30))
{
    const auto end = Clock::now() + limit;
    while (!pred()) {
        if (Clock::now() > end) return pred();
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

double ms_since(Clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

struct Scanned {
    FrameScan::State                       state = FrameScan::State::Idle;
    std::string                            error;
    std::optional<FrameScan::StreamInfo>   info;
    double                                 progress = 0.0;
    std::vector<Frame>                     frames;
};

// Scans stream `stream` of `path` to the end (or failure).
Scanned scan(const fs::path& path, int stream = 0)
{
    FrameScan s;
    s.start(path.string(), stream);
    REQUIRE(wait_for([&] { return s.done(); }));
    Scanned out{ s.state(), s.error(), s.info(), s.progress(), {} };
    REQUIRE(s.copy(out.frames) == s.count());
    return out;
}

std::size_t n_of(const std::vector<Frame>& v, auto pred)
{
    return static_cast<std::size_t>(std::count_if(v.begin(), v.end(), pred));
}

// Sorted pts of `v`, each one present.
std::vector<double> sorted_pts(const std::vector<Frame>& v)
{
    std::vector<double> pts;
    for (const Frame& f : v) {
        REQUIRE(std::isfinite(f.pts));
        pts.push_back(f.pts);
    }
    std::sort(pts.begin(), pts.end());
    return pts;
}

// The pts are exactly those of frames 0, 1, 2 ... at `fps`, counted from `origin`.
void check_pts(const std::vector<Frame>& v, int fps, double origin = 0.0)
{
    const std::vector<double> pts = sorted_pts(v);
    for (std::size_t i = 0; i < pts.size(); ++i)
        CHECK_THAT(pts[i] - origin, WithinAbs(static_cast<double>(i) / fps, 1e-3));
}

// Checks every clip's scan must pass: all of it listed in read order, the
// first frame an intra keyframe, every keyframe intra, every frame shown,
// sizes and positions.
void check_common(const Scanned& s, std::size_t frames)
{
    REQUIRE(s.state == FrameScan::State::Done);
    CHECK(s.error.empty());
    CHECK(s.progress == 1.0);
    REQUIRE(s.info);
    REQUIRE(s.frames.size() == frames);
    CHECK(s.frames.front().type == 'I');
    CHECK(s.frames.front().key);
    for (const Frame& f : s.frames) {
        CHECK(f.size > 0);
        CHECK_FALSE(f.discard);
        if (f.key) CHECK(f.type == 'I');
    }
    for (std::size_t i = 1; i < s.frames.size(); ++i) CHECK(s.frames[i].pos > s.frames[i - 1].pos);
}

// The longest run of consecutive B-frames in `shown` (display order).
std::size_t longest_b_run(const std::vector<Frame>& shown)
{
    std::size_t run = 0, longest = 0;
    for (const Frame& f : shown) {
        run     = f.type == 'B' ? run + 1 : 0;
        longest = std::max(longest, run);
    }
    return longest;
}

// A hand-built record: pts = dts.
Frame frame_at(double pts, std::uint32_t size, char type = 'P', bool key = false, double duration = 0.0)
{
    Frame f;
    f.pts      = pts;
    f.dts      = pts;
    f.duration = duration;
    f.size     = size;
    f.type     = type;
    f.key      = key;
    return f;
}

} // namespace

// ── Codecs and containers ─────────────────────────────────────────────────────

TEST_CASE("FrameScan: H.264 in Matroska, MP4 and MPEG-TS — I/P/B, keyframes, timestamps, references",
          "[framescan]")
{
    if (!avcodec_find_encoder_by_name("libx264")) SKIP("no H.264 encoder (libx264)");
    const char* format = nullptr;
    const char* ext    = nullptr;
    SECTION("Matroska (avcC)")   { format = "matroska"; ext = "mkv"; }
    SECTION("MP4 (avcC)")        { format = "mp4";      ext = "mp4"; }
    SECTION("MPEG-TS (Annex B)") { format = "mpegts";   ext = "ts"; }
    REQUIRE(format);
    const bool ts = std::string(format) == "mpegts";

    TempDir dir;
    const fs::path path = dir.path / std::format("h264.{}", ext);
    const Clip c{ .encoder = "libx264", .format = format, .frames = 48, .gop = 12, .b_frames = 2,
                  .global_header = !ts,
                  .options = { { "preset", "veryfast" }, { "x264-params", "scenecut=0:bframes=2:b-adapt=0" } } };
    REQUIRE(encode(path, c));
    const Scanned s = scan(path);
    check_common(s, 48);
    const std::vector<Frame>& f = s.frames;

    CHECK(s.info->codec == "h264");
    CHECK(s.info->parser);
    CHECK_FALSE(s.info->intra_only);
    CHECK_THAT(s.info->frame_rate, WithinAbs(24.0, 0.1));
    CHECK(s.info->file_size == static_cast<std::int64_t>(fs::file_size(path)));

    CHECK(n_of(f, [](const Frame& x) { return x.key; }) == 4);
    CHECK(n_of(f, [](const Frame& x) { return x.type == 'I'; }) == 4);
    CHECK(n_of(f, [](const Frame& x) { return x.type == 'P'; }) > 0);
    CHECK(n_of(f, [](const Frame& x) { return x.type == 'B'; }) > 0);
    CHECK(n_of(f, [](const Frame& x) { return x.type == '?'; }) == 0);

    // Exactly the timestamps encoded (MPEG-TS: after the muxer's start offset).
    check_pts(f, 24, ts ? sorted_pts(f).front() : 0.0);

    // Display order: by pts, B-frames between their anchors.
    const std::vector<Frame> shown = framescan::display_order(f);
    REQUIRE(shown.size() == f.size());
    CHECK(std::is_sorted(shown.begin(), shown.end(), [](const Frame& a, const Frame& b) { return a.pts < b.pts; }));
    CHECK(shown.front().type == 'I');
    CHECK(longest_b_run(shown) <= 2);
    bool reordered = false;
    for (std::size_t i = 0; i < f.size(); ++i) reordered = reordered || f[i].pts != shown[i].pts;
    CHECK(reordered);   // read in decode order

    // A keyframe every 12 frames.
    const framescan::GopSummary g = framescan::summarize(f);
    CHECK(g.frames == 48);
    CHECK(g.i == 4);
    CHECK(g.i + g.p + g.b == 48);
    CHECK(g.keyframes == 4);
    CHECK(g.intervals == 3);
    CHECK(g.min_frames == 12);
    CHECK(g.max_frames == 12);
    CHECK_THAT(g.avg_frames, WithinAbs(12.0, 1e-9));
    CHECK_THAT(g.min_s, WithinAbs(0.5, 1e-3));
    CHECK_THAT(g.max_s, WithinAbs(0.5, 1e-3));
    CHECK_THAT(g.seconds, WithinAbs(2.0, 2e-3));   // Matroska: last pts and duration both in whole ms
    std::uint64_t bytes = 0;
    for (const Frame& x : f) bytes += x.size;
    CHECK(g.bytes == bytes);
    CHECK_THAT(g.mean_bps, WithinRel(static_cast<double>(bytes) * 8.0 / g.seconds, 1e-9));
    // Two disjoint one-second windows cover the clip: one holds half the bits.
    CHECK(g.peak_bps >= g.mean_bps - 1e-6);

    // nal_ref_idc: I and P are references, some B-frames are not (x264's
    // B-pyramid makes the others references).
    for (const Frame& x : f) {
        CHECK(x.ref != -1);
        if (x.type == 'I' || x.type == 'P') CHECK(x.ref == 1);
    }
    CHECK(n_of(f, [](const Frame& x) { return x.type == 'B' && x.ref == 0; }) > 0);
}

TEST_CASE("FrameScan: an MP4 cut inside a GOP lists the frames decoded before its start as discarded",
          "[framescan]")
{
    if (!avcodec_find_encoder_by_name("libx264")) SKIP("no H.264 encoder (libx264)");
    TempDir dir;
    const fs::path full = dir.path / "full.mp4", cut = dir.path / "cut.mp4";
    const Clip c{ .encoder = "libx264", .format = "mp4", .frames = 48, .gop = 12, .b_frames = 2,
                  .options = { { "preset", "veryfast" }, { "x264-params", "scenecut=0:bframes=2:b-adapt=0" } } };
    REQUIRE(encode(full, c));
    // At frame 18 (0.75 s): read from the keyframe at frame 12, whose six
    // frames up to the cut are decoded but not shown.
    remux_cut(full, cut, 0.75);

    const Scanned s = scan(cut);
    REQUIRE(s.state == FrameScan::State::Done);
    REQUIRE(s.info);
    CHECK_THAT(s.info->start, WithinAbs(0.0, 1e-6));
    REQUIRE(s.frames.size() == 36);
    CHECK(s.frames.front().key);
    CHECK(s.frames.front().discard);
    CHECK(n_of(s.frames, [](const Frame& x) { return x.discard; }) == 6);
    std::vector<Frame> shown;
    for (const Frame& f : s.frames) {
        if (f.discard)
            CHECK(f.pts < 0.0);
        else
            shown.push_back(f);
    }
    check_pts(shown, 24);   // 0 .. 29 / 24: what plays
}

TEST_CASE("FrameScan: MPEG-2 in MPEG-TS — I/P/B in a plausible pattern", "[framescan]")
{
    TempDir dir;
    const fs::path path = dir.path / "mpeg2.ts";
    const Clip c{ .encoder = "mpeg2video", .format = "mpegts", .frames = 48, .gop = 12, .b_frames = 2,
                  .global_header = false, .options = { { "sc_threshold", "1000000000" } } };
    REQUIRE(encode(path, c));
    const Scanned s = scan(path);
    check_common(s, 48);
    const std::vector<Frame>& f = s.frames;
    CHECK(s.info->codec == "mpeg2video");

    const std::size_t i = n_of(f, [](const Frame& x) { return x.type == 'I'; });
    CHECK(i >= 4);
    CHECK(i <= 5);
    CHECK(n_of(f, [](const Frame& x) { return x.key; }) == i);
    CHECK(n_of(f, [](const Frame& x) { return x.type == 'P'; }) > 0);
    CHECK(n_of(f, [](const Frame& x) { return x.type == 'B'; }) > 0);
    CHECK(n_of(f, [](const Frame& x) { return x.type == '?'; }) == 0);
    for (const Frame& x : f) CHECK(x.ref == -1);   // H.264 only

    const std::vector<Frame> shown = framescan::display_order(f);
    CHECK(shown.front().type == 'I');
    CHECK(longest_b_run(shown) <= 2);
    // B-frames come after the later of their anchors in decode order.
    CHECK(f[1].type != 'B');
    check_pts(f, 24, sorted_pts(f).front());
}

TEST_CASE("FrameScan: HEVC (libx265) in Matroska — I/P/B and keyframes", "[framescan]")
{
    if (!avcodec_find_encoder_by_name("libx265")) SKIP("no HEVC encoder (libx265)");
    TempDir dir;
    const fs::path path = dir.path / "hevc.mkv";
    const Clip c{ .encoder = "libx265", .format = "matroska", .frames = 48, .gop = 12, .b_frames = 2,
                  .options = { { "preset", "ultrafast" },
                               { "x265-params", "keyint=12:min-keyint=12:bframes=2:b-adapt=0:scenecut=0:log-level=error" } } };
    REQUIRE(encode(path, c));
    const Scanned s = scan(path);
    check_common(s, 48);
    const std::vector<Frame>& f = s.frames;
    CHECK(s.info->codec == "hevc");
    CHECK(n_of(f, [](const Frame& x) { return x.key; }) == 4);
    CHECK(n_of(f, [](const Frame& x) { return x.type == 'P'; }) > 0);
    CHECK(n_of(f, [](const Frame& x) { return x.type == 'B'; }) > 0);
    CHECK(n_of(f, [](const Frame& x) { return x.type == '?'; }) == 0);
    check_pts(f, 24);
    const framescan::GopSummary g = framescan::summarize(f);
    CHECK(g.min_frames == 12);
    CHECK(g.max_frames == 12);
}

TEST_CASE("FrameScan: VP9 (libvpx-vp9) in WebM — keyframes I, everything else P", "[framescan]")
{
    if (!avcodec_find_encoder_by_name("libvpx-vp9")) SKIP("no VP9 encoder (libvpx-vp9)");
    TempDir dir;
    const fs::path path = dir.path / "vp9.webm";
    // Alt-ref frames on: hidden frames packed into superframes.
    const Clip c{ .encoder = "libvpx-vp9", .format = "webm", .frames = 48, .gop = 12,
                  .global_header = false,
                  .options = { { "deadline", "good" }, { "cpu-used", "8" }, { "lag-in-frames", "16" },
                               { "auto-alt-ref", "1" }, { "b", "300k" } } };
    REQUIRE(encode(path, c));
    const Scanned s = scan(path);
    check_common(s, 48);   // one packet per frame shown
    const std::vector<Frame>& f = s.frames;
    CHECK(s.info->codec == "vp9");
    CHECK(n_of(f, [](const Frame& x) { return x.key; }) == 4);
    for (const Frame& x : f) CHECK(x.type == (x.key ? 'I' : 'P'));
    check_pts(f, 24);
}

TEST_CASE("FrameScan: AV1 in Matroska — one temporal unit per frame shown, I and P", "[framescan]")
{
    const char* encoder = nullptr;
    std::vector<std::pair<const char*, const char*>> options;
    if (avcodec_find_encoder_by_name("libaom-av1")) {
        encoder = "libaom-av1";
        options = { { "cpu-used", "8" }, { "lag-in-frames", "16" }, { "b", "200k" } };
    } else if (avcodec_find_encoder_by_name("libsvtav1")) {
        encoder = "libsvtav1";
        options = { { "preset", "12" } };
    } else if (avcodec_find_encoder_by_name("librav1e")) {
        encoder = "librav1e";
        options = { { "speed", "10" } };
    } else {
        SKIP("no AV1 encoder (libaom-av1, libsvtav1, librav1e)");
    }
    INFO("encoder " << encoder);
    TempDir dir;
    const fs::path path = dir.path / "av1.mkv";
    const Clip c{ .encoder = encoder, .format = "matroska", .frames = 48, .gop = 12, .options = options };
    REQUIRE(encode(path, c));
    const Scanned s = scan(path);
    // Hidden frames travel inside the temporal unit of a frame shown: one
    // record per frame shown, its time the encoded one.
    check_common(s, 48);
    const std::vector<Frame>& f = s.frames;
    CHECK(s.info->codec == "av1");
    for (const Frame& x : f) CHECK((x.type == 'I' || x.type == 'P'));
    check_pts(f, 24);
}

TEST_CASE("FrameScan: a codec without a parser lists every frame, type unknown", "[framescan]")
{
    TempDir dir;
    const fs::path path = dir.path / "raw.nut";
    write_raw_nut(path, 50, 16, 8, 25, /*with_audio=*/true);   // audio: stream 0, video: 1

    const Scanned s = scan(path, 1);
    REQUIRE(s.state == FrameScan::State::Done);
    REQUIRE(s.info);
    CHECK(s.info->stream == 1);
    CHECK(s.info->codec == "rawvideo");
    CHECK_FALSE(s.info->parser);
    CHECK(s.info->intra_only);
    REQUIRE(s.frames.size() == 50);   // the audio packets are not listed
    for (std::size_t i = 0; i < s.frames.size(); ++i) {
        const Frame& f = s.frames[i];
        CHECK(f.type == '?');
        CHECK(f.key);
        CHECK(f.ref == -1);
        CHECK(f.size == 16u * 8u);
        CHECK_THAT(f.pts, WithinAbs(static_cast<double>(i) / 25.0, 1e-6));
        CHECK_THAT(f.duration, WithinAbs(1.0 / 25.0, 1e-6));
    }
    const framescan::GopSummary g = framescan::summarize(s.frames);
    CHECK(g.unknown == 50);
    CHECK(g.keyframes == 50);
    CHECK(g.min_frames == 1);
    CHECK(g.max_frames == 1);
}

TEST_CASE("FrameScan: failures — not a video stream, no such stream, no such file", "[framescan]")
{
    TempDir dir;
    const fs::path path = dir.path / "raw.nut";
    write_raw_nut(path, 10, 16, 8, 25, /*with_audio=*/true);

    const Scanned audio = scan(path, 0);
    CHECK(audio.state == FrameScan::State::Failed);
    CHECK(audio.error.find("not a video stream") != std::string::npos);
    CHECK(audio.frames.empty());

    const Scanned none = scan(path, 7);
    CHECK(none.state == FrameScan::State::Failed);
    CHECK_FALSE(none.error.empty());

    const Scanned missing = scan(dir.path / "missing.mkv", 0);
    CHECK(missing.state == FrameScan::State::Failed);
    CHECK(missing.error.find("cannot open") != std::string::npos);
    CHECK_FALSE(missing.info);
}

TEST_CASE("FrameScan: incremental copies, versions, restarting", "[framescan]")
{
    TempDir dir;
    const fs::path a = dir.path / "a.nut", b = dir.path / "b.nut";
    write_raw_nut(a, 3000, 16, 8, 25, false);   // several batches
    write_raw_nut(b, 20, 16, 8, 25, false);

    FrameScan s;
    CHECK(s.state() == FrameScan::State::Idle);
    CHECK_FALSE(s.done());
    s.stop();   // idle: nothing to do
    CHECK(s.state() == FrameScan::State::Idle);

    // Copying what is new while the scan runs ends with every record once.
    s.start(a.string(), 0);
    std::vector<Frame> mine;
    std::uint64_t seen = 0;
    int polls = 0;
    REQUIRE(wait_for([&] {
        const bool fin = s.done();   // before the copy: nothing comes after
        if (const std::uint64_t v = s.version(); v != seen) {
            seen = v;
            s.copy(mine, mine.size());
            ++polls;
        }
        return fin && s.version() == seen;
    }));
    CHECK(s.state() == FrameScan::State::Done);
    CHECK(polls >= 1);
    REQUIRE(mine.size() == 3000);
    for (std::size_t i = 0; i < mine.size(); ++i)
        CHECK_THAT(mine[i].pts, WithinAbs(static_cast<double>(i) / 25.0, 1e-6));
    CHECK(s.copy(mine, mine.size()) == 3000);   // nothing new
    CHECK(mine.size() == 3000);
    std::vector<Frame> tail;
    CHECK(s.copy(tail, 2990) == 3000);
    CHECK(tail.size() == 10);
    CHECK(s.copy(tail, 5000) == 3000);
    CHECK(tail.size() == 10);

    // A new start() drops the old records.
    const std::uint64_t v0 = s.version();
    s.start(b.string(), 0);
    CHECK(s.version() > v0);
    REQUIRE(wait_for([&] { return s.done(); }));
    CHECK(s.state() == FrameScan::State::Done);
    std::vector<Frame> again;
    CHECK(s.copy(again) == 20);
    CHECK(again.size() == 20);
}

TEST_CASE("FrameScan: stop() and destruction mid-scan return promptly", "[framescan]")
{
    // Tiny frames, many of them: listing them all takes far longer than the
    // first batch.
    constexpr int kFrames = 200000;
    TempDir dir;
    const fs::path path = dir.path / "long.nut";
    write_raw_nut(path, kFrames, 16, 8, 25, false);

    SECTION("stop()") {
        FrameScan s;
        s.start(path.string(), 0);
        REQUIRE(wait_for([&] { return s.count() > 0 || s.done(); }));
        const auto t0 = Clock::now();
        s.stop();
        CHECK(ms_since(t0) < 1000.0);
        CHECK(s.done());
        CHECK(s.error().empty());
        if (s.state() == FrameScan::State::Done)
            WARN("the scan finished before stop() — nothing was interrupted");
        else
            CHECK(s.state() == FrameScan::State::Stopped);
        // Nothing comes after stop(), and what was read stays.
        const std::size_t   n = s.count();
        const std::uint64_t v = s.version();
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        CHECK(s.count() == n);
        CHECK(s.version() == v);
        std::vector<Frame> got;
        CHECK(s.copy(got) == n);
        std::size_t bad = got.size();   // the first record out of place
        for (std::size_t i = 0; i < got.size() && bad == got.size(); ++i)
            if (std::abs(got[i].pts - static_cast<double>(i) / 25.0) > 1e-6) bad = i;
        CHECK(bad == got.size());
    }
    SECTION("destroying a running scanner") {
        auto t0 = Clock::now();
        {
            FrameScan s;
            s.start(path.string(), 0);
            REQUIRE(wait_for([&] { return s.count() > 0 || s.done(); }));
            t0 = Clock::now();
        }
        CHECK(ms_since(t0) < 1000.0);
        t0 = Clock::now();
        {
            FrameScan s;
            s.start(path.string(), 0);   // destroyed at once: opening, or just reading
        }
        CHECK(ms_since(t0) < 1000.0);
    }
}

// ── Pure helpers ──────────────────────────────────────────────────────────────

TEST_CASE("FrameScan helpers: rolling and peak bitrate on known numbers", "[framescan]")
{
    // 10 fps for 3 s, 1000 bytes a frame but 6000 for the one at 1.0 s.
    std::vector<Frame> v;
    for (int i = 0; i < 30; ++i)
        v.push_back(frame_at(i * 0.1, i == 10 ? 6000 : 1000, i % 10 == 0 ? 'I' : 'P', i % 10 == 0, 0.1));

    // One second: frame k sums frames k-9 .. k.
    const std::vector<double> r = framescan::rolling_bitrate(v, 1.0);
    REQUIRE(r.size() == v.size());
    for (int k = 0; k < 30; ++k) {
        double bytes = 0.0;
        for (int j = std::max(0, k - 9); j <= k; ++j) bytes += v[static_cast<std::size_t>(j)].size;
        CHECK_THAT(r[static_cast<std::size_t>(k)], WithinAbs(bytes * 8.0, 1e-6));
    }
    CHECK_THAT(r[0], WithinAbs(8000.0, 1e-6));
    CHECK_THAT(r[9], WithinAbs(80000.0, 1e-6));
    CHECK_THAT(r[10], WithinAbs(120000.0, 1e-6));
    CHECK_THAT(r[19], WithinAbs(120000.0, 1e-6));
    CHECK_THAT(r[20], WithinAbs(80000.0, 1e-6));
    CHECK_THAT(framescan::peak_bitrate(v, 1.0), WithinAbs(120000.0, 1e-6));
    // Half a second: frames k-4 .. k, over 0.5 s.
    CHECK_THAT(framescan::rolling_bitrate(v, 0.5)[10], WithinAbs((4000.0 + 6000.0) * 8.0 / 0.5, 1e-6));
    CHECK_THAT(framescan::peak_bitrate(v, 0.5), WithinAbs(160000.0, 1e-6));

    // Decode order in, values out in that order, windows by display time:
    // I0 P3 B1 B2.
    const std::vector<Frame> d = { frame_at(0.0, 4000, 'I', true), frame_at(0.3, 2000, 'P'),
                                   frame_at(0.1, 500, 'B'), frame_at(0.2, 500, 'B') };
    const std::vector<double> rd = framescan::rolling_bitrate(d, 0.25);
    CHECK_THAT(rd[0], WithinAbs(4000.0 * 8 / 0.25, 1e-6));            // I0 alone
    CHECK_THAT(rd[1], WithinAbs((500 + 500 + 2000) * 8 / 0.25, 1e-6)); // B1 B2 P3
    CHECK_THAT(rd[2], WithinAbs((4000 + 500) * 8 / 0.25, 1e-6));       // I0 B1
    CHECK_THAT(rd[3], WithinAbs((4000 + 500 + 500) * 8 / 0.25, 1e-6)); // I0 B1 B2

    // A frame without any time has no rate and counts nowhere; dts stands in
    // for a missing pts.
    std::vector<Frame> n = { frame_at(0.0, 1000), frame_at(0.1, 1000), frame_at(0.2, 1000) };
    n[1].pts = n[1].dts = framescan::kNaN;
    n[2].pts = framescan::kNaN;
    const std::vector<double> rn = framescan::rolling_bitrate(n, 1.0);
    CHECK(std::isnan(rn[1]));
    CHECK_THAT(rn[2], WithinAbs(2000.0 * 8, 1e-6));

    CHECK(std::isnan(framescan::rolling_bitrate(v, 0.0)[0]));
    CHECK(framescan::peak_bitrate({}, 1.0) == 0.0);
}

TEST_CASE("FrameScan helpers: GOP summary on known numbers", "[framescan]")
{
    // 10 fps for 3 s, keyframes at 0, 1 and 2 s (frames 0, 10, 20).
    std::vector<Frame> v;
    for (int i = 0; i < 30; ++i)
        v.push_back(frame_at(i * 0.1, i == 10 ? 6000 : 1000, i % 10 == 0 ? 'I' : i % 2 ? 'B' : 'P', i % 10 == 0, 0.1));
    v[5].type = '?';
    v[7].type = 'S';
    const framescan::GopSummary g = framescan::summarize(v);
    CHECK(g.frames == 30);
    CHECK(g.i == 3);
    CHECK(g.unknown == 1);
    CHECK(g.s == 1);
    CHECK(g.i + g.p + g.b + g.s + g.unknown == 30);
    CHECK(g.keyframes == 3);
    CHECK(g.intervals == 2);
    CHECK(g.min_frames == 10);
    CHECK(g.max_frames == 10);
    CHECK_THAT(g.avg_frames, WithinAbs(10.0, 1e-9));
    CHECK_THAT(g.avg_s, WithinAbs(1.0, 1e-9));
    CHECK(g.bytes == 35000);
    CHECK_THAT(g.seconds, WithinAbs(3.0, 1e-9));   // 2.9 s + the last frame's 0.1
    CHECK_THAT(g.mean_bps, WithinAbs(35000.0 * 8 / 3.0, 1e-6));
    CHECK_THAT(g.peak_bps, WithinAbs(120000.0, 1e-6));

    // Unequal intervals (keyframes at frames 0, 4, 10), no durations: the
    // span adds the average frame interval.
    for (Frame& f : v) {
        f.key      = false;
        f.duration = 0.0;
    }
    v[0].key = v[4].key = v[10].key = true;
    const framescan::GopSummary u = framescan::summarize(v);
    CHECK(u.intervals == 2);
    CHECK(u.min_frames == 4);
    CHECK(u.max_frames == 6);
    CHECK_THAT(u.avg_frames, WithinAbs(5.0, 1e-9));
    CHECK_THAT(u.min_s, WithinAbs(0.4, 1e-9));
    CHECK_THAT(u.max_s, WithinAbs(0.6, 1e-9));
    CHECK_THAT(u.seconds, WithinAbs(3.0, 1e-9));

    // Keyframes without a pts (AVI with B-frames): positions as read, seconds
    // dts to dts; without any timestamp (raw H.264) no seconds at all.
    for (Frame& f : v) f.pts = framescan::kNaN;
    const framescan::GopSummary d = framescan::summarize(v);
    CHECK(d.intervals == 2);
    CHECK(d.min_frames == 4);
    CHECK(d.max_frames == 6);
    CHECK_THAT(d.min_s, WithinAbs(0.4, 1e-9));
    CHECK_THAT(d.max_s, WithinAbs(0.6, 1e-9));
    for (Frame& f : v) f.dts = framescan::kNaN;
    const framescan::GopSummary r = framescan::summarize(v);
    CHECK(r.intervals == 2);
    CHECK_THAT(r.avg_frames, WithinAbs(5.0, 1e-9));
    CHECK(r.avg_s == 0.0);
    CHECK(r.seconds == 0.0);
    CHECK(r.mean_bps == 0.0);
    CHECK(r.peak_bps == 0.0);

    const framescan::GopSummary e = framescan::summarize({});
    CHECK(e.frames == 0);
    CHECK(e.intervals == 0);
    CHECK(e.seconds == 0.0);
    CHECK(e.mean_bps == 0.0);
}

TEST_CASE("FrameScan helpers: display order", "[framescan]")
{
    // Decode order I0 P3 B1 B2 P6 B4 B5, then a frame without a pts read
    // after P6's B-frames, and two with the same pts (read order kept).
    std::vector<Frame> d = { frame_at(0.0, 1, 'I', true), frame_at(0.3, 2, 'P'), frame_at(0.1, 3, 'B'),
                             frame_at(0.2, 4, 'B'),       frame_at(0.6, 5, 'P'), frame_at(0.4, 6, 'B'),
                             frame_at(0.5, 7, 'B'),       frame_at(0.0, 8, 'P'), frame_at(0.7, 9, 'P'),
                             frame_at(0.7, 10, 'P') };
    d[7].pts = framescan::kNaN;   // stays behind B5 (0.5), the frame read before it
    const std::vector<Frame> shown = framescan::display_order(d);
    std::vector<std::uint32_t> sizes;
    for (const Frame& f : shown) sizes.push_back(f.size);
    CHECK(sizes == std::vector<std::uint32_t>{ 1, 3, 4, 2, 6, 7, 8, 5, 9, 10 });

    // No timestamps at all (raw H.264): read order.
    std::vector<Frame> raw = { frame_at(0, 1), frame_at(0, 2), frame_at(0, 3) };
    for (Frame& f : raw) f.pts = f.dts = framescan::kNaN;
    const std::vector<Frame> kept = framescan::display_order(raw);
    REQUIRE(kept.size() == 3);
    CHECK(kept[0].size == 1);
    CHECK(kept[1].size == 2);
    CHECK(kept[2].size == 3);
}

TEST_CASE("FrameScan helpers: H.264 nal_ref_idc of the first slice", "[framescan]")
{
    using Bytes = std::vector<std::uint8_t>;
    auto ref = [](const Bytes& b, int len) { return framescan::h264_ref_idc(b.data(), b.size(), len); };

    // avcC, 4-byte lengths: SEI (type 6), then a non-IDR slice, nal_ref_idc 2.
    const Bytes avcc = { 0, 0, 0, 2, 0x06, 0x05, 0, 0, 0, 3, 0x41, 0x9A, 0x00 };
    CHECK(ref(avcc, 4) == 2);
    // ... a non-reference slice (B), an IDR slice (3).
    CHECK(ref({ 0, 0, 0, 2, 0x01, 0x9E }, 4) == 0);
    CHECK(ref({ 0, 0, 0, 2, 0x65, 0x88 }, 4) == 3);
    // 2-byte lengths.
    CHECK(ref({ 0, 2, 0x09, 0x10, 0, 2, 0x21, 0x9A }, 2) == 1);
    // No slice: parameter sets only.
    CHECK(ref({ 0, 0, 0, 2, 0x67, 0x64, 0, 0, 0, 2, 0x68, 0xEB }, 4) == -1);

    // Annex B: AUD, then a non-reference slice; 4-byte start codes too.
    CHECK(ref({ 0, 0, 0, 1, 0x09, 0xF0, 0, 0, 1, 0x01, 0x9E, 0x00 }, 0) == 0);
    CHECK(ref({ 0, 0, 0, 1, 0x67, 0x64, 0, 0, 0, 1, 0x68, 0xEB, 0, 0, 1, 0x65, 0x88 }, 0) == 3);
    // Annex B data although avcC was announced: the lengths don't add up.
    CHECK(ref({ 0, 0, 0, 1, 0x09, 0x10, 0, 0, 0, 1, 0x65, 0x88, 0x80 }, 4) == 3);

    CHECK(ref({}, 4) == -1);
    CHECK(framescan::h264_ref_idc(nullptr, 0, 0) == -1);
}
