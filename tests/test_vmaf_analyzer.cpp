// VMAFAnalyzer on generated local clips: Matroska files with uncompressed
// yuv420p video (no encoder needed) whose color tags survive the container.
// Checks what the analyzer records about a run (frame limit, paired PTS,
// input parameters, how the inputs ended, the distorted frames' color
// conversion), and which tags change the scores, not the scores themselves.

#include <catch2/catch_test_macros.hpp>

#include "FileHash.h"
#include "ManifestParser.h"
#include "TestTemp.h"
#include "VMAFAnalyzer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/common.h>
#include <libavutil/imgutils.h>
#include <libavutil/mem.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

namespace fs = std::filesystem;

namespace {

constexpr int kFps = 25;   // Matroska stores milliseconds: frame k is at k * 40 ms

// What else a clip of make_clip() has besides its size, length and matrix.
struct ClipLook {
    bool          uneven = false;   // the ramp moves 4 and 1 steps in turn, not 3
    bool          colored = false;  // chroma gradients instead of gray; yuv420p only
    AVColorRange  range = AVCOL_RANGE_MPEG;
    AVFieldOrder  field_order = AV_FIELD_UNKNOWN;
    AVPixelFormat pix_fmt = AV_PIX_FMT_YUV420P;   // or another 8-bit planar YUV format, or NV12
};

// A clip of `frames` frames of w x h yuv420p (or look.pix_fmt): a diagonal
// ramp that moves every frame, so libvmaf's motion feature has something to
// measure, in gray unless `look.colored`; a matrix conversion leaves gray's
// luma alone. The luma plane comes first, every chroma byte is 128.
std::string make_clip(const char* name, int w, int h, int frames, AVColorSpace space,
                      const ClipLook& look = {})
{
    REQUIRE((!look.colored || look.pix_fmt == AV_PIX_FMT_YUV420P));
    const fs::path path = testtemp::path(name);
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "matroska", path.c_str()) >= 0);
    AVStream* st = avformat_new_stream(oc, nullptr);
    st->codecpar->codec_type  = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id    = AV_CODEC_ID_RAWVIDEO;
    st->codecpar->codec_tag   = avcodec_pix_fmt_to_codec_tag(look.pix_fmt);   // "I420" for yuv420p
    st->codecpar->format      = look.pix_fmt;
    st->codecpar->width       = w;
    st->codecpar->height      = h;
    st->codecpar->color_space = space;
    st->codecpar->color_range = look.range;
    st->codecpar->field_order = look.field_order;
    st->time_base             = { 1, kFps };
    st->avg_frame_rate        = { kFps, 1 };
    REQUIRE(avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);

    const int size = av_image_get_buffer_size(look.pix_fmt, w, h, 1);
    for (int k = 0; k < frames; ++k) {
        const int shift = look.uneven ? (k / 2) * 5 + (k % 2) * 4 : 3 * k;
        AVPacket* pkt = av_packet_alloc();
        REQUIRE(av_new_packet(pkt, size) >= 0);
        uint8_t* y = pkt->data;
        for (int row = 0; row < h; ++row)
            for (int col = 0; col < w; ++col)
                y[row * w + col] = static_cast<uint8_t>((col * 256 / w + row * 128 / h + shift) & 0xff);
        std::fill(pkt->data + w * h, pkt->data + size, static_cast<uint8_t>(128));
        if (look.colored) {
            const int cw = (w + 1) / 2, ch = (h + 1) / 2;
            uint8_t* u = pkt->data + w * h;
            uint8_t* v = u + cw * ch;
            for (int row = 0; row < ch; ++row)
                for (int col = 0; col < cw; ++col) {
                    u[row * cw + col] = static_cast<uint8_t>(40 + col * 176 / cw);
                    v[row * cw + col] = static_cast<uint8_t>(216 - row * 176 / ch);
                }
        }
        pkt->stream_index = 0;
        pkt->pts = pkt->dts = k;
        pkt->duration = 1;
        pkt->flags |= AV_PKT_FLAG_KEY;
        av_packet_rescale_ts(pkt, { 1, kFps }, st->time_base);
        REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
        av_packet_free(&pkt);
    }
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    return path.string();
}

const std::string& ref_clip()
{
    static const std::string p = make_clip("mirage_test_vmaf_ref.mkv", 192, 108, 12, AVCOL_SPC_BT709);
    return p;
}
const std::string& dis_clip()
{
    static const std::string p = make_clip("mirage_test_vmaf_dis.mkv", 96, 54, 12, AVCOL_SPC_SMPTE170M);
    return p;
}
const std::string& short_clip()
{
    static const std::string p = make_clip("mirage_test_vmaf_short.mkv", 96, 54, 8, AVCOL_SPC_SMPTE170M);
    return p;
}
const std::string& uneven_ref_clip()
{
    static const std::string p =
        make_clip("mirage_test_vmaf_uneven_ref.mkv", 192, 108, 8, AVCOL_SPC_BT709, { .uneven = true });
    return p;
}
const std::string& uneven_dis_clip()
{
    static const std::string p =
        make_clip("mirage_test_vmaf_uneven_dis.mkv", 96, 54, 8, AVCOL_SPC_BT709, { .uneven = true });
    return p;
}

// Colored clips: one reference, and distorted clips with the same pixels
// under other tags.
const std::string& colored_ref_clip()
{
    static const std::string p =
        make_clip("mirage_test_vmaf_colored_ref.mkv", 192, 108, 6, AVCOL_SPC_BT709, { .colored = true });
    return p;
}
std::string colored_dis_clip(const char* name, AVColorSpace space, ClipLook look = {})
{
    look.colored = true;
    return make_clip(name, 96, 54, 6, space, look);
}

// A Matroska file of 5 packets on one stream that FFmpeg opens but the
// analyzer can't score: `video` false, a mono PCM track, so the file has no
// video stream; true, an FFV1 stream whose extradata isn't an FFV1 header,
// so its decoder fails to open.
std::string make_unscorable(const char* name, bool video)
{
    const fs::path path = testtemp::path(name);
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "matroska", path.c_str()) >= 0);
    AVStream* st = avformat_new_stream(oc, nullptr);
    const AVRational tb = video ? AVRational{ 1, kFps } : AVRational{ 1, 8000 };
    if (video) {
        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id   = AV_CODEC_ID_FFV1;
        st->codecpar->format     = AV_PIX_FMT_YUV420P;
        st->codecpar->width      = 32;
        st->codecpar->height     = 16;
        st->codecpar->extradata  = static_cast<uint8_t*>(av_mallocz(16 + AV_INPUT_BUFFER_PADDING_SIZE));
        std::fill_n(st->codecpar->extradata, 16, static_cast<uint8_t>(1));
        st->codecpar->extradata_size = 16;
    } else {
        st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id    = AV_CODEC_ID_PCM_S16LE;
        st->codecpar->format      = AV_SAMPLE_FMT_S16;
        st->codecpar->sample_rate = 8000;
        av_channel_layout_default(&st->codecpar->ch_layout, 1);
    }
    st->time_base = tb;
    REQUIRE(avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);
    const int64_t step = video ? 1 : 400;
    for (int k = 0; k < 5; ++k) {
        AVPacket* pkt = av_packet_alloc();
        REQUIRE(av_new_packet(pkt, video ? 64 : 800) >= 0);
        std::fill_n(pkt->data, pkt->size, static_cast<uint8_t>(0x55));
        pkt->stream_index = 0;
        pkt->pts = pkt->dts = k * step;
        pkt->duration = step;
        pkt->flags |= AV_PKT_FLAG_KEY;
        av_packet_rescale_ts(pkt, tb, st->time_base);
        REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
        av_packet_free(&pkt);
    }
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    return path.string();
}

double ms(int k) { return static_cast<double>(k * 1000 / kFps) / 1000.0; }

// The luma of make_clip()'s ramp as it is at `ms` milliseconds: it moves 3
// steps every 40 ms, so a frame at 40 k ms shows make_clip()'s frame k.
void fill_ramp_at(uint8_t* y, int linesize, int w, int h, int ms)
{
    const int shift = 3 * ms / 40;
    for (int row = 0; row < h; ++row)
        for (int col = 0; col < w; ++col)
            y[row * linesize + col] = static_cast<uint8_t>((col * 256 / w + row * 128 / h + shift) & 0xff);
}

// The frames of a timed clip, for pairing by timestamp (D-028).
struct Timing {
    std::vector<int> times_ms;                 // each frame's timestamp; it shows the ramp at that time
    AVRational       frame_rate = { kFps, 1 };   // the stream's average frame rate
    bool             audio = false;            // a mono PCM track as well, from 0 to the video's end
};

// 40 k for k = 0 .. n - 1: make_clip()'s timestamps.
std::vector<int> every_40ms(int n)
{
    std::vector<int> out;
    for (int k = 0; k < n; ++k) out.push_back(40 * k);
    return out;
}

// A Matroska clip of gray w x h yuv420p frames at the given timestamps, each
// showing the ramp at its own time, so that a frame dropped or repeated
// leaves the others' pictures and timestamps as they were.
std::string make_timed_clip(const char* name, int w, int h, AVColorSpace space, const Timing& t)
{
    const fs::path path = testtemp::path(name);
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "matroska", path.c_str()) >= 0);
    AVStream* st = avformat_new_stream(oc, nullptr);
    st->codecpar->codec_type  = AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id    = AV_CODEC_ID_RAWVIDEO;
    st->codecpar->codec_tag   = avcodec_pix_fmt_to_codec_tag(AV_PIX_FMT_YUV420P);
    st->codecpar->format      = AV_PIX_FMT_YUV420P;
    st->codecpar->width       = w;
    st->codecpar->height      = h;
    st->codecpar->color_space = space;
    st->codecpar->color_range = AVCOL_RANGE_MPEG;
    st->time_base             = { 1, 1000 };
    st->avg_frame_rate        = t.frame_rate;
    AVStream* au = nullptr;
    if (t.audio) {
        au = avformat_new_stream(oc, nullptr);
        au->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
        au->codecpar->codec_id    = AV_CODEC_ID_PCM_S16LE;
        au->codecpar->format      = AV_SAMPLE_FMT_S16;
        au->codecpar->sample_rate = 8000;
        av_channel_layout_default(&au->codecpar->ch_layout, 1);
        au->time_base = { 1, 1000 };
    }
    REQUIRE(avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);

    const auto write = [&](AVStream* s, int pts_ms, int duration_ms, int size,
                           const std::function<void(uint8_t*)>& fill) {
        AVPacket* pkt = av_packet_alloc();
        REQUIRE(av_new_packet(pkt, size) >= 0);
        fill(pkt->data);
        pkt->stream_index = s->index;
        pkt->pts = pkt->dts = pts_ms;
        pkt->duration = duration_ms;
        pkt->flags |= AV_PKT_FLAG_KEY;
        av_packet_rescale_ts(pkt, { 1, 1000 }, s->time_base);
        REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
        av_packet_free(&pkt);
    };
    const int size = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, w, h, 1);
    const int duration = static_cast<int>(av_rescale(1000, t.frame_rate.den, t.frame_rate.num));
    std::size_t next = 0;   // the next video frame
    const int end = t.times_ms.empty() ? 0 : t.times_ms.back() + 40;
    for (int slot = 0; slot < end || next < t.times_ms.size(); slot += 40) {
        if (au && slot < end)
            write(au, slot, 40, 640, [](uint8_t* d) { std::fill_n(d, 640, static_cast<uint8_t>(0)); });
        while (next < t.times_ms.size() && t.times_ms[next] < slot + 40) {
            const int ms = t.times_ms[next++];
            write(st, ms, duration, size, [&](uint8_t* d) {
                fill_ramp_at(d, w, w, h, ms);
                std::fill(d + w * h, d + size, static_cast<uint8_t>(128));
            });
        }
    }
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    return path.string();
}

// An MPEG-TS clip of `frames` frames of the ramp at 25 fps and w x h, MPEG-2
// coded with every frame a key frame and BT.601 tags, written as two TS
// files one after the other, as an HLS stream's segments are: the second
// starts at frame `restart`, its timestamps start again from 0, and it marks
// its first packets as discontinuous (mpegts_flags initial_discontinuity).
std::string make_ts_restart_clip(const char* name, int w, int h, int frames, int restart)
{
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
    enc->colorspace     = AVCOL_SPC_SMPTE170M;
    enc->color_range    = AVCOL_RANGE_MPEG;
    REQUIRE(avcodec_open2(enc, codec, nullptr) >= 0);

    std::vector<AVPacket*> packets;
    const auto drain = [&] {
        for (AVPacket* pkt = av_packet_alloc(); avcodec_receive_packet(enc, pkt) == 0;
             pkt = av_packet_alloc())
            packets.push_back(pkt);
    };
    AVFrame* f = av_frame_alloc();
    f->format = AV_PIX_FMT_YUV420P;
    f->width  = w;
    f->height = h;
    REQUIRE(av_frame_get_buffer(f, 0) >= 0);
    for (int k = 0; k < frames; ++k) {
        REQUIRE(av_frame_make_writable(f) >= 0);
        fill_ramp_at(f->data[0], f->linesize[0], w, h, 40 * k);
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
    REQUIRE(static_cast<int>(packets.size()) == frames);

    const fs::path path = testtemp::path(name);
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (int part = 0; part < 2; ++part) {
        const fs::path piece = testtemp::path(std::format("{}.part{}", name, part).c_str());
        AVFormatContext* oc = nullptr;
        REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "mpegts", piece.c_str()) >= 0);
        AVStream* st = avformat_new_stream(oc, nullptr);
        REQUIRE(avcodec_parameters_from_context(st->codecpar, enc) >= 0);
        st->time_base = { 1, 90000 };
        REQUIRE(avio_open(&oc->pb, piece.c_str(), AVIO_FLAG_WRITE) >= 0);
        AVDictionary* opts = nullptr;
        if (part == 1) av_dict_set(&opts, "mpegts_flags", "initial_discontinuity", 0);
        REQUIRE(avformat_write_header(oc, &opts) >= 0);
        av_dict_free(&opts);
        const int from = part == 0 ? 0 : restart, to = part == 0 ? restart : frames;
        for (int k = from; k < to; ++k) {
            AVPacket* pkt = av_packet_clone(packets[k]);
            pkt->stream_index = 0;
            pkt->pts -= from;   // the second file starts from 0 again
            pkt->dts -= from;
            av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
            REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
            av_packet_free(&pkt);
        }
        REQUIRE(av_write_trailer(oc) >= 0);
        avio_closep(&oc->pb);
        avformat_free_context(oc);
        std::ifstream in(piece, std::ios::binary);
        out << in.rdbuf();
        fs::remove(piece);
    }
    for (AVPacket*& pkt : packets) av_packet_free(&pkt);
    avcodec_free_context(&enc);
    return path.string();
}

// An MPEG-TS clip of `frames` frames of the ramp at 25 fps in `format`,
// MPEG-2 coded with every frame a key frame and BT.709 tags, in one stream
// whose timestamps go on: frames 0 to change - 1 are w x h, the others
// w2 x h2, as in a stream whose resolution changes midway. The low-delay
// flag keeps FFmpeg's decoder from dropping the frame before the change.
std::string make_ts_resize_clip(const char* name, AVPixelFormat format, int frames, int change,
                                int w, int h, int w2, int h2)
{
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
    REQUIRE(codec);
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(format);
    REQUIRE(desc);
    std::vector<AVPacket*> packets;
    AVCodecParameters* par = avcodec_parameters_alloc();
    for (const auto& [from, to, ew, eh] : { std::tuple{ 0, change, w, h },
                                            std::tuple{ change, frames, w2, h2 } }) {
        if (from == to) continue;
        AVCodecContext* enc = avcodec_alloc_context3(codec);
        enc->width          = ew;
        enc->height         = eh;
        enc->pix_fmt        = format;
        enc->time_base      = { 1, kFps };
        enc->framerate      = { kFps, 1 };
        enc->gop_size       = 1;
        enc->max_b_frames   = 0;
        enc->flags         |= AV_CODEC_FLAG_QSCALE | AV_CODEC_FLAG_LOW_DELAY;
        enc->global_quality = FF_QP2LAMBDA * 2;
        enc->colorspace     = AVCOL_SPC_BT709;
        enc->color_range    = AVCOL_RANGE_MPEG;
        REQUIRE(avcodec_open2(enc, codec, nullptr) >= 0);
        if (from == 0) REQUIRE(avcodec_parameters_from_context(par, enc) >= 0);
        const auto drain = [&] {
            for (AVPacket* pkt = av_packet_alloc(); avcodec_receive_packet(enc, pkt) == 0;
                 pkt = av_packet_alloc())
                packets.push_back(pkt);
        };
        AVFrame* f = av_frame_alloc();
        f->format = format;
        f->width  = ew;
        f->height = eh;
        REQUIRE(av_frame_get_buffer(f, 0) >= 0);
        const int cw = AV_CEIL_RSHIFT(ew, desc->log2_chroma_w);
        const int ch = AV_CEIL_RSHIFT(eh, desc->log2_chroma_h);
        for (int k = from; k < to; ++k) {
            REQUIRE(av_frame_make_writable(f) >= 0);
            fill_ramp_at(f->data[0], f->linesize[0], ew, eh, 40 * k);
            for (int p = 1; p < 3; ++p)
                for (int row = 0; row < ch; ++row)
                    std::fill_n(f->data[p] + row * f->linesize[p], cw, static_cast<uint8_t>(128));
            f->pts = k;
            REQUIRE(avcodec_send_frame(enc, f) >= 0);
            drain();
        }
        REQUIRE(avcodec_send_frame(enc, nullptr) >= 0);
        drain();
        av_frame_free(&f);
        avcodec_free_context(&enc);
    }
    REQUIRE(static_cast<int>(packets.size()) == frames);

    const fs::path path = testtemp::path(name);
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "mpegts", path.c_str()) >= 0);
    AVStream* st = avformat_new_stream(oc, nullptr);
    REQUIRE(avcodec_parameters_copy(st->codecpar, par) >= 0);
    st->time_base = { 1, 90000 };
    REQUIRE(avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);
    for (AVPacket*& pkt : packets) {
        pkt->stream_index = 0;
        av_packet_rescale_ts(pkt, { 1, kFps }, st->time_base);
        REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
        av_packet_free(&pkt);
    }
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    avcodec_parameters_free(&par);
    return path.string();
}

// Waits for a background analysis; false if it is still running after a minute.
bool wait_done(const VMAFAnalyzer& a)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    while (a.running() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    return !a.running();
}

} // namespace

TEST_CASE("vmaf_ffmpeg_libraries: the FFmpeg libraries' versions at run time (D-021)", "[vmaf]")
{
    const std::vector<VMAFLibraryVersion> libs = vmaf_ffmpeg_libraries();
    std::vector<std::string> names;
    for (const VMAFLibraryVersion& l : libs) names.push_back(l.name);
    CHECK(names == std::vector<std::string>{ "libavutil", "libavcodec", "libavformat", "libswscale",
                                             "libavfilter" });
    REQUIRE(libs.size() == 5);
    const auto text = [](unsigned v) {
        return std::format("{}.{}.{}", AV_VERSION_MAJOR(v), AV_VERSION_MINOR(v), AV_VERSION_MICRO(v));
    };
    CHECK(libs[0].version == text(avutil_version()));
    CHECK(libs[1].version == text(avcodec_version()));
    CHECK(libs[2].version == text(avformat_version()));
    CHECK(libs[3].version == text(swscale_version()));
    CHECK(libs[4].version == text(avfilter_version()));
    // The libraries loaded have the major versions the build compiled against.
    CHECK(libs[0].version.starts_with(std::format("{}.", LIBAVUTIL_VERSION_MAJOR)));
    CHECK(libs[3].version.starts_with(std::format("{}.", LIBSWSCALE_VERSION_MAJOR)));
    CHECK(libs[4].version.starts_with(std::format("{}.", LIBAVFILTER_VERSION_MAJOR)));
}

TEST_CASE("VMAFAnalyzer::analyze: frame limit, paired PTS, settings and inputs", "[vmaf]")
{
    const VMAFResult r = VMAFAnalyzer::analyze(ref_clip(), dis_clip(),
                                               { .max_frames = 5, .record_pts = true });
    REQUIRE(r.error.empty());
    CHECK(r.done);
    CHECK(r.score_error.empty());
    REQUIRE(r.per_frame.size() == 5);
    REQUIRE(r.ref_pts.size() == 5);
    REQUIRE(r.dis_pts.size() == 5);
    for (int k = 0; k < 5; ++k) {
        INFO(k);
        CHECK(r.ref_pts[k] == ms(k));
        CHECK(r.dis_pts[k] == ms(k));
        CHECK(std::isfinite(r.per_frame[k]));
    }

    CHECK(r.settings.model == "vmaf_v0.6.1");
    CHECK(r.settings.scaler == "bicubic");   // the default (D-024)
    CHECK(r.settings.pix_fmt == "yuv420p");
    CHECK(r.settings.bit_depth == 8);
    CHECK(r.settings.n_threads == 4);
    CHECK(r.settings.n_subsample == 1);
    CHECK(r.width == 192);
    CHECK(r.height == 108);

    CHECK(r.ref_info.width == 192);
    CHECK(r.ref_info.height == 108);
    CHECK(r.ref_info.pix_fmt == "yuv420p");
    CHECK(r.ref_info.color_space == "bt709");
    CHECK(r.ref_info.color_range == "tv");
    CHECK(r.ref_info.time_base_num == 1);
    CHECK(r.ref_info.time_base_den == 1000);
    CHECK(r.dis_info.width == 96);
    CHECK(r.dis_info.height == 54);
    CHECK(r.dis_info.color_space == "smpte170m");

    // The limit stopped the run: neither input was read to its end.
    CHECK_FALSE(r.ref_info.ended);
    CHECK_FALSE(r.dis_info.ended);
    CHECK(r.ref_info.end_error.empty());
    CHECK(r.dis_info.end_error.empty());
}

TEST_CASE("VMAFAnalyzer::analyze: without a limit both inputs are read to the end", "[vmaf]")
{
    const VMAFResult full = VMAFAnalyzer::analyze(ref_clip(), dis_clip(), { .record_pts = true });
    REQUIRE(full.error.empty());
    CHECK(full.score_error.empty());
    REQUIRE(full.per_frame.size() == 12);
    REQUIRE(full.ref_pts.size() == 12);
    CHECK(full.ref_pts.back() == ms(11));
    CHECK(full.ref_info.ended);
    CHECK(full.dis_info.ended);
    CHECK(full.ref_info.end_error.empty());

    const VMAFResult part = VMAFAnalyzer::analyze(ref_clip(), dis_clip(), { .max_frames = 5 });
    REQUIRE(part.per_frame.size() == 5);
    for (int k = 0; k < 4; ++k) {
        INFO(k);
        CHECK(part.per_frame[k] == full.per_frame[k]);
    }
    CHECK(part.ref_pts.empty());   // record_pts off
}

TEST_CASE("VMAFAnalyzer::analyze: the pooled values of D-027, from the per-frame scores", "[vmaf]")
{
    const std::string dis = colored_dis_clip("mirage_test_vmaf_colored_170m.mkv", AVCOL_SPC_SMPTE170M);
    const VMAFResult r = VMAFAnalyzer::analyze(colored_ref_clip(), dis);
    REQUIRE(r.error.empty());
    REQUIRE(r.per_frame.size() == 6);

    const VMAFPooled want = vmafpool::pool(r.per_frame);
    CHECK(r.pooled.mean == want.mean);
    CHECK(r.pooled.harmonic_mean == want.harmonic_mean);
    CHECK(r.pooled.min == want.min);
    CHECK(r.pooled.min_without_first_frame == want.min_without_first_frame);
    CHECK(r.pooled.p1 == want.p1);
    CHECK(r.pooled.p5 == want.p5);
    CHECK(r.pooled.p10 == want.p10);
    CHECK(r.pooled.p25 == want.p25);
    CHECK(r.pooled.p50 == want.p50);
    // The player's panel and write_json show three of them.
    CHECK(r.vmaf_mean == r.pooled.mean);
    CHECK(r.vmaf_min == r.pooled.min);
    CHECK(r.vmaf_p5 == r.pooled.p5);

    // p5 by linear interpolation: h = 5 * 5 / 100 = 0.25, a quarter of the way
    // from the lowest score to the next, where the analyzer took
    // sorted[6 * 5 / 100], the lowest, before D-027.
    std::vector<double> sorted = r.per_frame;
    std::sort(sorted.begin(), sorted.end());
    REQUIRE(sorted[1] > sorted[0]);
    CHECK(r.vmaf_p5 == sorted[0] + 0.25 * (sorted[1] - sorted[0]));
    CHECK(r.vmaf_p5 > sorted[0]);
}

TEST_CASE("VMAFAnalyzer::analyze: the last pair of a limited run has no successor", "[vmaf]")
{
    // libvmaf's motion2 feature at frame i is min(motion i, motion i+1), so
    // the last scored frame can score differently from the same frame in a
    // longer run. The reference here moves 4, 1, 4, 1, ... steps: frame 3's
    // own motion is large and frame 4's is small.
    const VMAFResult full = VMAFAnalyzer::analyze(uneven_ref_clip(), uneven_dis_clip());
    const VMAFResult part = VMAFAnalyzer::analyze(uneven_ref_clip(), uneven_dis_clip(),
                                                  { .max_frames = 4 });
    REQUIRE(full.per_frame.size() == 8);
    REQUIRE(part.per_frame.size() == 4);
    for (int k = 0; k < 3; ++k) {
        INFO(k);
        CHECK(part.per_frame[k] == full.per_frame[k]);
    }
    CHECK(part.per_frame[3] != full.per_frame[3]);
}

TEST_CASE("VMAFAnalyzer::analyze: the frames of the longer input past the shorter's end fail to pair", "[vmaf]")
{
    // Since D-028 the longer input is read to its end, so that what is left
    // of it is counted: here the reference's last 4 frames, unmatched.
    const VMAFResult r = VMAFAnalyzer::analyze(ref_clip(), short_clip(), { .record_pts = true });
    REQUIRE(r.error.empty());
    CHECK(r.per_frame.size() == 8);
    CHECK(r.dis_pts.size() == 8);
    CHECK(r.dis_info.ended);
    CHECK(r.ref_info.ended);
    CHECK(r.alignment.paired == 8);
    CHECK(r.alignment.unmatched_reference == 4);
    CHECK(r.alignment.failed == 4);
    CHECK(r.alignment.failure_rate == 4.0 / 12.0);
    CHECK(r.alignment.exceeded);
    REQUIRE(r.alignment.failures.size() == 4);
    CHECK(r.alignment.failures[0].input == "reference");
    CHECK(r.alignment.failures[0].index == 8);
    CHECK(r.alignment.failures[0].pts == ms(8));
}

TEST_CASE("VMAFAnalyzer::analyze: an input that cannot be opened is an error", "[vmaf]")
{
    const std::string missing = testtemp::path("mirage_test_vmaf_missing.mkv").string();
    fs::remove(missing);
    const VMAFResult r = VMAFAnalyzer::analyze(missing, dis_clip());
    CHECK(r.done);
    CHECK(r.error == "cannot open reference: " + missing);
    CHECK(r.per_frame.empty());

    const VMAFResult d = VMAFAnalyzer::analyze(ref_clip(), missing);
    CHECK(d.error == "cannot open distorted: " + missing);
}

TEST_CASE("VMAFAnalyzer::analyze: an input without video, or whose decoder can't open, is an error", "[vmaf]")
{
    // Both open: the analyzer frees what it opened when it gives up (D-032).
    const std::string audio = make_unscorable("mirage_test_vmaf_audio_only.mkv", false);
    const std::string ffv1 = make_unscorable("mirage_test_vmaf_bad_ffv1.mkv", true);
    for (const std::string& bad : { audio, ffv1 }) {
        INFO(bad);
        const VMAFResult r = VMAFAnalyzer::analyze(bad, dis_clip());
        CHECK(r.done);
        CHECK(r.error == "cannot open reference: " + bad);
        CHECK(r.per_frame.empty());
        const VMAFResult d = VMAFAnalyzer::analyze(ref_clip(), bad);
        CHECK(d.done);
        CHECK(d.error == "cannot open distorted: " + bad);
        CHECK(d.per_frame.empty());
    }
}

TEST_CASE("VMAFAnalyzer::analyze: a reference whose frames change size is an error", "[vmaf]")
{
    // The pairs are scored at the reference's size, and FFmpeg's libvmaf
    // filter refuses inputs of different sizes (DECISIONS.md P-063): a
    // yuv420p reference, copied as it is, and one in another pixel format,
    // converted (D-022), alike.
    const std::string dis =
        make_ts_resize_clip("mirage_test_vmaf_resize_dis.ts", AV_PIX_FMT_YUV420P, 12, 12, 96, 54, 96, 54);
    for (const AVPixelFormat format : { AV_PIX_FMT_YUV420P, AV_PIX_FMT_YUV422P }) {
        const std::string name = std::format("mirage_test_vmaf_resize_ref_{}.ts", av_get_pix_fmt_name(format));
        INFO(name);
        const std::string ref = make_ts_resize_clip(name.c_str(), format, 12, 6, 192, 108, 128, 72);
        const VMAFResult r = VMAFAnalyzer::analyze(ref, dis);
        CHECK(r.done);
        CHECK(r.error == "the reference's frames change size, from 192x108 to 128x72 at its frame 6: "
                         "a reference is scored at one size, as FFmpeg's libvmaf filter takes inputs "
                         "of one size only");
        CHECK(r.per_frame.empty());

        // The frames before the change score as they did.
        const VMAFResult part = VMAFAnalyzer::analyze(ref, dis, { .max_frames = 6 });
        REQUIRE(part.error.empty());
        CHECK(part.per_frame.size() == 6);
        CHECK(part.width == 192);
        CHECK(part.height == 108);
    }

    // A distorted input whose frames change size is scaled frame by frame to
    // the reference's size, as before.
    const std::string ref =
        make_ts_resize_clip("mirage_test_vmaf_resize_ref.ts", AV_PIX_FMT_YUV420P, 12, 12, 192, 108, 192, 108);
    const std::string resized =
        make_ts_resize_clip("mirage_test_vmaf_resize_dis2.ts", AV_PIX_FMT_YUV420P, 12, 6, 96, 54, 128, 72);
    const VMAFResult d = VMAFAnalyzer::analyze(ref, resized);
    REQUIRE(d.error.empty());
    CHECK(d.per_frame.size() == 12);
    CHECK(d.alignment.failed == 0);
}

TEST_CASE("VMAFAnalyzer::start: scores like analyze() and records no PTS", "[vmaf]")
{
    VMAFAnalyzer a;
    a.start(ref_clip(), dis_clip());
    REQUIRE(wait_done(a));

    const auto results = a.results();
    REQUIRE(results.size() == 1);
    const VMAFResult& r = results[0];
    REQUIRE(r.error.empty());
    CHECK(r.label == "Analysis");
    CHECK(r.ref_pts.empty());
    CHECK(r.dis_pts.empty());
    CHECK(r.per_frame == VMAFAnalyzer::analyze(ref_clip(), dis_clip()).per_frame);
}

TEST_CASE("VMAFAnalyzer::start_manifest: top-rung pairs and write_json's format", "[vmaf]")
{
    const std::vector<VariantStream> variants = {
        { .url = dis_clip(), .bandwidth = 500000, .width = 96, .height = 54 },
        { .url = ref_clip(), .bandwidth = 2000000, .width = 192, .height = 108 },
    };
    VMAFAnalyzer a;
    a.start_manifest(variants);
    REQUIRE(wait_done(a));

    const auto results = a.results();
    REQUIRE(results.size() == 1);
    const VMAFResult& r = results[0];
    REQUIRE(r.error.empty());
    CHECK(r.label == "96x54  0.5 Mbps");
    CHECK(r.ref_url == ref_clip());
    CHECK(r.dis_url == dis_clip());
    CHECK(r.ref_pts.empty());
    REQUIRE(r.per_frame == VMAFAnalyzer::analyze(ref_clip(), dis_clip()).per_frame);

    // The GUI's export keeps its layout: 4 decimals, 10 scores per line.
    std::string per_frame;
    for (std::size_t j = 0; j < r.per_frame.size(); ++j) {
        if (j % 10 == 0) per_frame += "\n        ";
        per_frame += std::format("{:.4f}", r.per_frame[j]);
        if (j + 1 < r.per_frame.size()) per_frame += ", ";
    }
    const std::string expected = std::format(
        "{{\n  \"results\": [\n    {{\n"
        "      \"label\": \"96x54  0.5 Mbps\",\n"
        "      \"ref\":   \"{}\",\n"
        "      \"dis\":   \"{}\",\n"
        "      \"bandwidth\": 500000,\n"
        "      \"resolution\": \"192x108\",\n"
        "      \"vmaf_mean\": {:.4f},\n"
        "      \"vmaf_min\":  {:.4f},\n"
        "      \"vmaf_p5\":   {:.4f},\n"
        "      \"per_frame\": [{}\n      ]\n"
        "    }}\n  ]\n}}\n",
        ref_clip(), dis_clip(), r.vmaf_mean, r.vmaf_min, r.vmaf_p5, per_frame);

    const fs::path out = testtemp::path("mirage_test_vmaf_gui.json");
    REQUIRE(a.write_json(out.string()));
    std::ifstream f(out);
    std::ostringstream text;
    text << f.rdbuf();
    CHECK(text.str() == expected);
    fs::remove(out);
}

TEST_CASE("VMAFAnalyzer::analyze: converts the distorted frames' matrix as FFmpeg does (D-016)", "[vmaf]")
{
    const std::string s170m = colored_dis_clip("mirage_test_vmaf_colored_170m.mkv", AVCOL_SPC_SMPTE170M);
    const std::string s470bg = colored_dis_clip("mirage_test_vmaf_colored_470bg.mkv", AVCOL_SPC_BT470BG);
    const std::string s709 = colored_dis_clip("mirage_test_vmaf_colored_709.mkv", AVCOL_SPC_BT709);
    const VMAFResult r170m = VMAFAnalyzer::analyze(colored_ref_clip(), s170m);
    const VMAFResult r470bg = VMAFAnalyzer::analyze(colored_ref_clip(), s470bg);
    const VMAFResult r709 = VMAFAnalyzer::analyze(colored_ref_clip(), s709);
    REQUIRE(r170m.error.empty());
    REQUIRE(r170m.per_frame.size() == 6);
    REQUIRE(r709.per_frame.size() == 6);

    // The same pixels: converted from BT.601 to the reference's BT.709, or not
    // converted. smpte170m and bt470bg are both BT.601.
    CHECK(r170m.per_frame != r709.per_frame);
    CHECK(r470bg.per_frame == r170m.per_frame);

    REQUIRE(r170m.dis_conversions.size() == 1);
    const VMAFColorConversion& c = r170m.dis_conversions[0];
    CHECK(c.from_space == "smpte170m");
    CHECK(c.from_range == "tv");
    CHECK(c.to_space == "bt709");
    CHECK(c.to_range == "tv");
    CHECK(c.matrix_changed);
    CHECK_FALSE(c.range_changed);
    CHECK(c.first_index == 0);
    CHECK(c.pairs == 6);

    REQUIRE(r709.dis_conversions.size() == 1);
    CHECK(r709.dis_conversions[0].from_space == "bt709");
    CHECK(r709.dis_conversions[0].to_space == "bt709");
    CHECK_FALSE(r709.dis_conversions[0].matrix_changed);
    CHECK_FALSE(r709.dis_conversions[0].range_changed);
}

TEST_CASE("VMAFAnalyzer::analyze: the scaler of the distorted frames, recorded in the settings", "[vmaf]")
{
    const std::string dis = colored_dis_clip("mirage_test_vmaf_colored_709.mkv", AVCOL_SPC_BT709);
    const VMAFResult def = VMAFAnalyzer::analyze(colored_ref_clip(), dis);
    const VMAFResult bilinear =
        VMAFAnalyzer::analyze(colored_ref_clip(), dis, { .scaler = VMAFScaler::Bilinear });
    const VMAFResult bicubic =
        VMAFAnalyzer::analyze(colored_ref_clip(), dis, { .scaler = VMAFScaler::Bicubic });
    const VMAFResult lanczos =
        VMAFAnalyzer::analyze(colored_ref_clip(), dis, { .scaler = VMAFScaler::Lanczos });
    for (const VMAFResult* r : { &def, &bilinear, &bicubic, &lanczos }) {
        REQUIRE(r->error.empty());
        REQUIRE(r->per_frame.size() == 6);
    }
    CHECK(def.settings.scaler == "bicubic");
    CHECK(bilinear.settings.scaler == "bilinear");
    CHECK(bicubic.settings.scaler == "bicubic");
    CHECK(lanczos.settings.scaler == "lanczos");
    CHECK(bicubic.per_frame == def.per_frame);   // the default is bicubic (D-024)
    CHECK(bicubic.per_frame != bilinear.per_frame);
    CHECK(lanczos.per_frame != bicubic.per_frame);

    // The player's analysis, start(), scores with the default too.
    VMAFAnalyzer a;
    a.start(colored_ref_clip(), dis);
    REQUIRE(wait_done(a));
    REQUIRE(a.results().size() == 1);
    CHECK(a.results()[0].settings.scaler == "bicubic");
    CHECK(a.results()[0].per_frame == bicubic.per_frame);
}

TEST_CASE("VMAFAnalyzer::analyze: built-in models, recorded in the settings", "[vmaf]")
{
    const std::string dis = colored_dis_clip("mirage_test_vmaf_colored_709.mkv", AVCOL_SPC_BT709);
    const VMAFResult def = VMAFAnalyzer::analyze(colored_ref_clip(), dis);
    const VMAFResult v061 =
        VMAFAnalyzer::analyze(colored_ref_clip(), dis, { .model = { .version = "vmaf_v0.6.1" } });
    const VMAFResult neg =
        VMAFAnalyzer::analyze(colored_ref_clip(), dis, { .model = { .version = "vmaf_v0.6.1neg" } });
    const VMAFResult k4 =
        VMAFAnalyzer::analyze(colored_ref_clip(), dis, { .model = { .version = "vmaf_4k_v0.6.1" } });
    for (const VMAFResult* r : { &def, &v061, &neg, &k4 }) {
        REQUIRE(r->error.empty());
        REQUIRE(r->per_frame.size() == 6);
        CHECK(r->settings.model_source == "built-in");
        CHECK(r->settings.model_sha256.empty());
    }
    CHECK(def.settings.model == "vmaf_v0.6.1");
    CHECK(v061.settings.model == "vmaf_v0.6.1");
    CHECK(neg.settings.model == "vmaf_v0.6.1neg");
    CHECK(k4.settings.model == "vmaf_4k_v0.6.1");
    CHECK(v061.per_frame == def.per_frame);   // the default is vmaf_v0.6.1
    CHECK(neg.per_frame != def.per_frame);
    CHECK(k4.per_frame != def.per_frame);
}

TEST_CASE("VMAFAnalyzer::analyze: a model file, identified by its SHA-256", "[vmaf]")
{
    const fs::path file = fs::path(MIRAGE_LIBVMAF_MODEL_DIR) / "vmaf_v0.6.1neg.json";
    if (!fs::exists(file)) SKIP("no libvmaf model file at " << file.string());
    // A copy with one more newline at its end: other bytes, the same model.
    const fs::path copy = testtemp::path("mirage_test_vmaf_model_copy.json");
    {
        std::ifstream in(file, std::ios::binary);
        std::ofstream out(copy, std::ios::binary | std::ios::trunc);
        out << in.rdbuf() << '\n';
    }
    const std::string dis = colored_dis_clip("mirage_test_vmaf_colored_709.mkv", AVCOL_SPC_BT709);
    const VMAFResult a = VMAFAnalyzer::analyze(colored_ref_clip(), dis,
                                               { .model = { .path = file.string() } });
    const VMAFResult b = VMAFAnalyzer::analyze(colored_ref_clip(), dis,
                                               { .model = { .path = copy.string() } });
    REQUIRE(a.error.empty());
    REQUIRE(b.error.empty());
    REQUIRE(a.per_frame.size() == 6);
    CHECK(a.settings.model == file.string());
    CHECK(a.settings.model_source == "file");
    std::string err;
    CHECK(a.settings.model_sha256 == filehash::sha256_file(file.string(), err));
    CHECK(b.settings.model_sha256 == filehash::sha256_file(copy.string(), err));
    CHECK(a.settings.model_sha256 != b.settings.model_sha256);
    CHECK(a.per_frame == b.per_frame);
    CHECK(a.per_frame != VMAFAnalyzer::analyze(colored_ref_clip(), dis).per_frame);
    fs::remove(copy);
}

TEST_CASE("VMAFAnalyzer::analyze: a model libvmaf can't load is an error", "[vmaf]")
{
    const VMAFResult unknown = VMAFAnalyzer::analyze(ref_clip(), dis_clip(),
                                                     { .model = { .version = "vmaf_v0.0.0" } });
    CHECK(unknown.done);
    CHECK(unknown.error == "vmaf_model_load failed: this libvmaf has no built-in model vmaf_v0.0.0");
    CHECK(unknown.per_frame.empty());

    const fs::path missing = testtemp::path("mirage_test_vmaf_no_model.json");
    fs::remove(missing);
    const VMAFResult gone = VMAFAnalyzer::analyze(ref_clip(), dis_clip(),
                                                  { .model = { .path = missing.string() } });
    CHECK(gone.error.starts_with("cannot read the model file " + missing.string() + ": "));
    CHECK(gone.per_frame.empty());

    const fs::path junk = testtemp::path("mirage_test_vmaf_not_a_model.json");
    std::ofstream(junk) << "not a model\n";
    const VMAFResult bad = VMAFAnalyzer::analyze(ref_clip(), dis_clip(),
                                                 { .model = { .path = junk.string() } });
    CHECK(bad.error == "vmaf_model_load_from_path failed: libvmaf cannot load the model file " +
                       junk.string());
    CHECK(bad.settings.model_source == "file");
    CHECK(bad.settings.model_sha256 == filehash::sha256_hex("not a model\n"));   // hashed before loading
    fs::remove(junk);
}

TEST_CASE("VMAFAnalyzer::analyze: converts a full range distorted input to the reference's range", "[vmaf]")
{
    const std::string pc = colored_dis_clip("mirage_test_vmaf_colored_709pc.mkv", AVCOL_SPC_BT709,
                                            { .range = AVCOL_RANGE_JPEG });
    const std::string tv = colored_dis_clip("mirage_test_vmaf_colored_709.mkv", AVCOL_SPC_BT709);
    const VMAFResult rpc = VMAFAnalyzer::analyze(colored_ref_clip(), pc);
    const VMAFResult rtv = VMAFAnalyzer::analyze(colored_ref_clip(), tv);
    REQUIRE(rpc.error.empty());
    CHECK(rpc.per_frame != rtv.per_frame);
    REQUIRE(rpc.dis_conversions.size() == 1);
    CHECK(rpc.dis_conversions[0].from_range == "pc");
    CHECK(rpc.dis_conversions[0].to_range == "tv");
    CHECK(rpc.dis_conversions[0].range_changed);
    CHECK_FALSE(rpc.dis_conversions[0].matrix_changed);
}

TEST_CASE("VMAFAnalyzer::analyze: an untagged distorted input is BT.601 to libswscale", "[vmaf]")
{
    const std::string untagged =
        colored_dis_clip("mirage_test_vmaf_colored_untagged.mkv", AVCOL_SPC_UNSPECIFIED,
                         { .range = AVCOL_RANGE_UNSPECIFIED });
    const std::string s170m = colored_dis_clip("mirage_test_vmaf_colored_170m.mkv", AVCOL_SPC_SMPTE170M);
    const VMAFResult r = VMAFAnalyzer::analyze(colored_ref_clip(), untagged);
    REQUIRE(r.error.empty());
    CHECK(r.dis_info.color_space == "unknown");
    CHECK(r.per_frame == VMAFAnalyzer::analyze(colored_ref_clip(), s170m).per_frame);
    REQUIRE(r.dis_conversions.size() == 1);
    CHECK(r.dis_conversions[0].from_space == "unknown");
    CHECK(r.dis_conversions[0].from_range == "unknown");
    CHECK(r.dis_conversions[0].to_space == "bt709");
    CHECK(r.dis_conversions[0].to_range == "tv");
    CHECK(r.dis_conversions[0].matrix_changed);
}

TEST_CASE("VMAFAnalyzer::analyze: interlaced distorted frames are scaled as progressive", "[vmaf]")
{
    const std::string tt = colored_dis_clip("mirage_test_vmaf_colored_170m_tt.mkv", AVCOL_SPC_SMPTE170M,
                                            { .field_order = AV_FIELD_TT });
    const std::string s170m = colored_dis_clip("mirage_test_vmaf_colored_170m.mkv", AVCOL_SPC_SMPTE170M);
    const VMAFResult r = VMAFAnalyzer::analyze(colored_ref_clip(), tt);
    REQUIRE(r.error.empty());
    REQUIRE(r.per_frame.size() == 6);
    CHECK(r.per_frame == VMAFAnalyzer::analyze(colored_ref_clip(), s170m).per_frame);
}

TEST_CASE("VMAFAnalyzer::analyze: a matrix libswscale does not convert is an error", "[vmaf]")
{
    const std::string ycgco = colored_dis_clip("mirage_test_vmaf_colored_ycgco.mkv", AVCOL_SPC_YCGCO);
    const VMAFResult r = VMAFAnalyzer::analyze(colored_ref_clip(), ycgco);
    CHECK(r.done);
    CHECK(r.error.find("the distorted input has the color matrix ycgco") != std::string::npos);
    CHECK(r.per_frame.empty());

    const VMAFResult ref = VMAFAnalyzer::analyze(ycgco, colored_ref_clip());
    CHECK(ref.error.find("the reference has the color matrix ycgco") != std::string::npos);
}

TEST_CASE("VMAFAnalyzer::analyze: a reference that isn't yuv420p is converted to the distorted frames' tags (D-022)", "[vmaf]")
{
    // FFmpeg's section 5 graph converts such a reference itself, with a scale
    // filter it inserts, to the tags it negotiates for both inputs: here the
    // distorted frames' (vmafcolor::negotiated), which then keep theirs.
    static const std::string ref422 = make_clip("mirage_test_vmaf_ref422.mkv", 192, 108, 12,
                                                AVCOL_SPC_BT709, { .pix_fmt = AV_PIX_FMT_YUV422P });
    const VMAFResult r = VMAFAnalyzer::analyze(ref422, dis_clip());
    REQUIRE(r.error.empty());
    CHECK(r.ref_info.pix_fmt == "yuv422p");
    CHECK(r.per_frame.size() == 12);
    CHECK(r.warnings.empty());   // yuv422p is among the verified formats
    REQUIRE(r.ref_conversions.size() == 1);
    const VMAFColorConversion& c = r.ref_conversions[0];
    CHECK(c.from_space == "bt709");
    CHECK(c.from_range == "tv");
    CHECK(c.to_space == "smpte170m");
    CHECK(c.to_range == "tv");
    CHECK(c.matrix_changed);
    CHECK_FALSE(c.range_changed);
    CHECK(c.first_index == 0);
    CHECK(c.pairs == 12);
    REQUIRE(r.dis_conversions.size() == 1);
    const VMAFColorConversion& d = r.dis_conversions[0];
    CHECK(d.from_space == "smpte170m");
    CHECK(d.to_space == "smpte170m");
    CHECK(d.to_range == "tv");
    CHECK_FALSE(d.matrix_changed);
    CHECK_FALSE(d.range_changed);

    // A yuv420p reference is used as it is.
    const VMAFResult plain = VMAFAnalyzer::analyze(ref_clip(), dis_clip(), { .max_frames = 2 });
    CHECK(plain.ref_conversions.empty());
    CHECK(plain.warnings.empty());
}

TEST_CASE("VMAFAnalyzer::analyze: frames in a pixel format not checked against FFmpeg's are scored with a warning", "[vmaf]")
{
    // NV12 isn't among vmafcolor::verified_formats().
    static const std::string dis = make_clip("mirage_test_vmaf_nv12.mkv", 96, 54, 12,
                                             AVCOL_SPC_SMPTE170M, { .pix_fmt = AV_PIX_FMT_NV12 });
    static const std::string ref = make_clip("mirage_test_vmaf_nv12_ref.mkv", 192, 108, 12,
                                             AVCOL_SPC_BT709, { .pix_fmt = AV_PIX_FMT_NV12 });
    const VMAFResult a = VMAFAnalyzer::analyze(ref_clip(), dis);
    REQUIRE(a.error.empty());
    CHECK(a.dis_info.pix_fmt == "nv12");
    CHECK(a.per_frame.size() == 12);
    REQUIRE(a.warnings.size() == 1);   // once, not once per pair
    CHECK(a.warnings[0].starts_with("the distorted input's frames are nv12, a pixel format whose "
                                    "conversion isn't checked against FFmpeg's section 5 graph"));

    const VMAFResult b = VMAFAnalyzer::analyze(ref, dis_clip());
    REQUIRE(b.error.empty());
    CHECK(b.ref_info.pix_fmt == "nv12");
    CHECK(b.ref_conversions.size() == 1);
    REQUIRE(b.warnings.size() == 1);
    CHECK(b.warnings[0].starts_with("the reference's frames are nv12, a pixel format whose "
                                    "conversion isn't checked"));
}

// ── Pairing by timestamp (D-028) ─────────────────────────────────────────────

TEST_CASE("VMAFAnalyzer::analyze: frames that line up keep their pairs, and the alignment says so", "[vmaf]")
{
    const VMAFResult r = VMAFAnalyzer::analyze(ref_clip(), dis_clip(), { .record_pts = true });
    REQUIRE(r.error.empty());
    REQUIRE(r.per_frame.size() == 12);
    const VMAFAlignment& a = r.alignment;
    CHECK(a.rule == "reference-timeline");
    CHECK(a.timestamp_origin == "input-start");
    CHECK(a.grid == "reference");
    CHECK(a.tolerance == 0.02);   // half of 1/25 s
    CHECK(a.ref_frame_rate == "25/1");
    CHECK(a.dis_frame_rate == "25/1");
    CHECK(a.ref_start == 0.0);
    CHECK(a.ref_start_from == "container");
    CHECK(a.dis_start_from == "container");
    CHECK(a.paired == 12);
    CHECK(a.failed == 0);
    CHECK(a.failure_rate == 0.0);
    CHECK(a.threshold == 0.01);
    CHECK_FALSE(a.exceeded);
    CHECK(a.discontinuity_jump == 10.0);
    CHECK(a.failures.empty());
    CHECK(a.warning.empty());
    CHECK(r.warnings.empty());
    CHECK(a.contexts == std::vector<VMAFContextRun>{ { 0, 12 } });
    for (int k = 0; k < 12; ++k) {
        INFO(k);
        CHECK(r.ref_index[k] == k);
        CHECK(r.dis_index[k] == k);
        CHECK(r.ref_pts[k] == r.dis_pts[k]);
    }
}

TEST_CASE("VMAFAnalyzer::analyze: a stream with a frame dropped on purpose (section 1.4's acceptance)", "[vmaf]")
{
    // The distorted clip without its frame 5, the other frames' timestamps
    // and pictures as in dis_clip().
    std::vector<int> times = every_40ms(12);
    times.erase(times.begin() + 5);
    const std::string dropped = make_timed_clip("mirage_test_vmaf_dropped.mkv", 96, 54,
                                                AVCOL_SPC_SMPTE170M, { .times_ms = times });
    const VMAFResult full = VMAFAnalyzer::analyze(ref_clip(), dis_clip(), { .record_pts = true });
    const VMAFResult r = VMAFAnalyzer::analyze(ref_clip(), dropped, { .record_pts = true });
    REQUIRE(r.error.empty());
    REQUIRE(full.per_frame.size() == 12);

    // The failed frame is reported: the reference's frame 5, which no
    // distorted frame pairs with.
    const VMAFAlignment& a = r.alignment;
    CHECK(a.paired == 11);
    CHECK(a.unmatched_reference == 1);
    CHECK(a.failed == 1);
    CHECK(a.failure_rate == 1.0 / 12.0);
    CHECK(a.exceeded);
    REQUIRE(a.failures.size() == 1);
    CHECK(a.failures[0].input == "reference");
    CHECK(a.failures[0].index == 5);
    CHECK(a.failures[0].pts == ms(5));
    CHECK(a.failures[0].reason == "unmatched_reference");
    CHECK(a.warning.starts_with("8.33% of the frames failed to pair by timestamp (1 of 12: "
                                "1 unmatched reference), above the threshold of 1%"));
    CHECK(std::ranges::find(r.warnings, a.warning) != r.warnings.end());

    // No mismatched pair is scored: every pair's frames have the same
    // timestamp, the distorted frames after the gap with the next reference frame.
    REQUIRE(r.per_frame.size() == 11);
    for (std::size_t i = 0; i < 11; ++i) {
        INFO(i);
        CHECK(r.ref_pts[i] == r.dis_pts[i]);
        CHECK(r.ref_index[i] == static_cast<int64_t>(i < 5 ? i : i + 1));
        CHECK(r.dis_index[i] == static_cast<int64_t>(i));
    }
    // A new libvmaf context after the gap, and away from it the scores of the
    // full clip against itself: frames 0 to 3 before it, 7 to 11 after it.
    // Frame 6, the first of its context, has no motion to the frame before.
    CHECK(a.contexts == std::vector<VMAFContextRun>{ { 0, 5 }, { 5, 6 } });
    for (int k : { 0, 1, 2, 3, 7, 8, 9, 10, 11 }) {
        INFO(k);
        CHECK(r.per_frame[k < 5 ? k : k - 1] == full.per_frame[k]);
    }
    CHECK(r.per_frame[5] != full.per_frame[6]);
}

TEST_CASE("VMAFAnalyzer::analyze: a duplicated timestamp is a failure, and the pairs keep their scores", "[vmaf]")
{
    // Frame 5 twice, under the same timestamp.
    std::vector<int> times = every_40ms(12);
    times.insert(times.begin() + 6, 200);
    const std::string dup = make_timed_clip("mirage_test_vmaf_duplicate.mkv", 96, 54,
                                            AVCOL_SPC_SMPTE170M, { .times_ms = times });
    const VMAFResult r = VMAFAnalyzer::analyze(ref_clip(), dup, { .record_pts = true });
    REQUIRE(r.error.empty());
    CHECK(r.alignment.paired == 12);
    CHECK(r.alignment.duplicate_distorted == 1);
    CHECK(r.alignment.failed == 1);
    CHECK(r.alignment.exceeded);
    REQUIRE(r.alignment.failures.size() == 1);
    CHECK(r.alignment.failures[0].input == "distorted");
    CHECK(r.alignment.failures[0].index == 6);
    CHECK(r.alignment.failures[0].pts == ms(5));
    CHECK(r.alignment.failures[0].reason == "duplicate_distorted");
    CHECK(r.alignment.contexts == std::vector<VMAFContextRun>{ { 0, 12 } });
    // The reference's frames all pair with the frames of their time.
    CHECK(r.per_frame == VMAFAnalyzer::analyze(ref_clip(), dis_clip()).per_frame);
}

TEST_CASE("VMAFAnalyzer::analyze: half and double the reference's frame rate are scored on the coarser grid", "[vmaf]")
{
    // Half: every other picture of dis_clip(), 80 ms apart.
    std::vector<int> half_times;
    for (int k = 0; k < 6; ++k) half_times.push_back(80 * k);
    const std::string half = make_timed_clip("mirage_test_vmaf_half_rate.mkv", 96, 54,
                                             AVCOL_SPC_SMPTE170M,
                                             { .times_ms = half_times, .frame_rate = { kFps, 2 } });
    const VMAFResult h = VMAFAnalyzer::analyze(ref_clip(), half, { .record_pts = true });
    REQUIRE(h.error.empty());
    CHECK(h.alignment.grid == "distorted");
    CHECK(h.alignment.dis_frame_rate == "25/2");
    CHECK(h.alignment.tolerance == 0.02);   // still half the reference's frame duration
    CHECK(h.alignment.paired == 6);
    CHECK(h.alignment.off_grid_reference == 6);
    CHECK(h.alignment.failed == 0);
    CHECK_FALSE(h.alignment.exceeded);
    CHECK(h.warnings.empty());
    REQUIRE(h.per_frame.size() == 6);
    for (int k = 0; k < 6; ++k) {
        INFO(k);
        CHECK(h.ref_index[k] == 2 * k);
        CHECK(h.dis_index[k] == k);
        CHECK(h.ref_pts[k] == h.dis_pts[k]);
    }

    // Double: a frame every 20 ms, every other one a picture of dis_clip().
    std::vector<int> double_times;
    for (int j = 0; j < 24; ++j) double_times.push_back(20 * j);
    const std::string dbl = make_timed_clip("mirage_test_vmaf_double_rate.mkv", 96, 54,
                                            AVCOL_SPC_SMPTE170M,
                                            { .times_ms = double_times, .frame_rate = { 2 * kFps, 1 } });
    const VMAFResult d = VMAFAnalyzer::analyze(ref_clip(), dbl, { .record_pts = true });
    REQUIRE(d.error.empty());
    CHECK(d.alignment.grid == "reference");
    CHECK(d.alignment.paired == 12);
    CHECK(d.alignment.off_grid_distorted == 12);
    CHECK(d.alignment.failed == 0);
    CHECK(d.warnings.empty());
    for (int k = 0; k < 12; ++k) {
        INFO(k);
        CHECK(d.dis_index[k] == 2 * k);
        CHECK(d.ref_pts[k] == d.dis_pts[k]);
    }
    // The pairs are dis_clip()'s, and so are the scores.
    CHECK(d.per_frame == VMAFAnalyzer::analyze(ref_clip(), dis_clip()).per_frame);
}

TEST_CASE("VMAFAnalyzer::analyze: a later start, measured from each input's start", "[vmaf]")
{
    const VMAFResult full = VMAFAnalyzer::analyze(ref_clip(), dis_clip());
    std::vector<int> times = every_40ms(12);
    times.erase(times.begin(), times.begin() + 3);   // the first three frames cut, timestamps kept

    // Video only: the container starts at the first frame left, 120 ms, and
    // times count from there, as `ffmpeg` counts them without -copyts. So the
    // cut clip pairs from its start, each frame with a reference frame three
    // frames earlier in the picture, and the reference's last three frames
    // are left over.
    const std::string cut = make_timed_clip("mirage_test_vmaf_cut.mkv", 96, 54,
                                            AVCOL_SPC_SMPTE170M, { .times_ms = times });
    const VMAFResult c = VMAFAnalyzer::analyze(ref_clip(), cut, { .record_pts = true });
    REQUIRE(c.error.empty());
    CHECK(c.alignment.dis_start == 0.12);
    CHECK(c.alignment.dis_start_from == "container");
    CHECK(c.alignment.paired == 9);
    CHECK(c.alignment.unmatched_reference == 3);
    for (int k = 0; k < 9; ++k) {
        INFO(k);
        CHECK(c.ref_index[k] == k);
        CHECK(c.ref_pts[k] == ms(k));
        CHECK(c.dis_pts[k] == ms(k + 3));
    }
    REQUIRE(c.alignment.failures.size() == 3);
    CHECK(c.alignment.failures[0].index == 9);

    // With an audio track from 0 the container starts at 0: the video starts
    // 120 ms in, and the reference's first three frames are unmatched.
    const std::string late = make_timed_clip("mirage_test_vmaf_late.mkv", 96, 54,
                                             AVCOL_SPC_SMPTE170M, { .times_ms = times, .audio = true });
    const VMAFResult l = VMAFAnalyzer::analyze(ref_clip(), late, { .record_pts = true });
    REQUIRE(l.error.empty());
    CHECK(l.alignment.dis_start == 0.0);
    CHECK(l.alignment.paired == 9);
    CHECK(l.alignment.unmatched_reference == 3);
    REQUIRE(l.alignment.failures.size() == 3);
    for (int k = 0; k < 3; ++k) {
        INFO(k);
        CHECK(l.alignment.failures[k].input == "reference");
        CHECK(l.alignment.failures[k].index == k);
    }
    REQUIRE(l.per_frame.size() == 9);
    CHECK(l.alignment.contexts == std::vector<VMAFContextRun>{ { 0, 9 } });
    for (int k = 0; k < 9; ++k) {
        INFO(k);
        CHECK(l.ref_index[k] == k + 3);
        CHECK(l.ref_pts[k] == l.dis_pts[k]);
        // The first pair has no frame before it; the others score as in the full run.
        if (k > 0) CHECK(l.per_frame[k] == full.per_frame[k + 3]);
    }
}

TEST_CASE("VMAFAnalyzer::analyze: MPEG-TS timestamps that restart midway stop the pairing", "[vmaf]")
{
    const std::string ts = make_ts_restart_clip("mirage_test_vmaf_restart.ts", 96, 54, 12, 6);
    const VMAFResult r = VMAFAnalyzer::analyze(ref_clip(), ts, { .record_pts = true });
    REQUIRE(r.error.empty());
    CHECK(r.dis_info.time_base_den == 90000);
    CHECK(r.alignment.dis_frame_rate == "25/1");
    // The six frames before the restart pair with the reference's first six.
    REQUIRE(r.per_frame.size() == 6);
    for (int k = 0; k < 6; ++k) {
        INFO(k);
        CHECK(r.ref_index[k] == k);
        CHECK(r.dis_index[k] == k);
        CHECK(std::abs(r.dis_pts[k] - r.alignment.dis_start - ms(k)) < 1e-9);
    }
    // Then pairing stops, and the rest of both inputs counts as failed.
    CHECK(r.alignment.discontinuities == 1);
    CHECK(r.alignment.after_discontinuity == 12);
    CHECK(r.alignment.failed == 12);
    CHECK(r.alignment.failure_rate == 12.0 / 18.0);
    CHECK(r.alignment.exceeded);
    CHECK(r.ref_info.ended);
    CHECK(r.dis_info.ended);
    REQUIRE(r.alignment.failures.size() == 12);
    for (const VMAFAlignmentFailure& f : r.alignment.failures) CHECK(f.reason == "after_discontinuity");
    CHECK(r.alignment.warning.find("12 after a discontinuity") != std::string::npos);
}

TEST_CASE("VMAFAnalyzer::start: the player's analysis records the alignment and its warning", "[vmaf]")
{
    std::vector<int> times = every_40ms(12);
    times.erase(times.begin() + 5);
    const std::string dropped = make_timed_clip("mirage_test_vmaf_dropped_gui.mkv", 96, 54,
                                                AVCOL_SPC_SMPTE170M, { .times_ms = times });
    VMAFAnalyzer a;
    a.start(ref_clip(), dropped);
    REQUIRE(wait_done(a));
    const auto results = a.results();
    REQUIRE(results.size() == 1);
    const VMAFResult& r = results[0];
    REQUIRE(r.error.empty());
    CHECK(r.per_frame.size() == 11);
    CHECK(r.alignment.unmatched_reference == 1);
    CHECK(r.alignment.exceeded);
    CHECK_FALSE(r.alignment.warning.empty());   // what the VMAF panel shows next to the label
    CHECK(r.ref_index.empty());                 // no PTS or indices recorded
}
