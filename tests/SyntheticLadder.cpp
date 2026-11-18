// The synthetic ladders of the attribution tests (DECISIONS.md D-069, D-080):
// testgen's prototype (eval/phase3-survey/testgen/src/testgen.cpp) ported.
// Its source pictures, encoder settings, muxing and playlists are the
// prototype's; what D-080 doesn't choose is left out: the ramp source, the
// busy ad, unmarked continuity counters, nominal BANDWIDTHs, the mezzanine,
// the list of encoders, the probe, and the source's SI and TI, which
// testgen.md §2.2 measured to choose the source. Errors are strings;
// FFmpeg's objects are owned by unique_ptrs, so that an error returns from
// anywhere.

#include "SyntheticLadder.h"

#include "JsonText.h"
#include "TestTemp.h"

#include <catch2/interfaces/catch_interfaces_capture.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <format>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/frame.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace fs = std::filesystem;

namespace synthladder {

namespace {

// ── The source: integer value noise, moving shapes ───────────────────────────
// testgen.cpp's, unchanged, so that the pictures are those testgen.md
// measured: integer arithmetic only.

constexpr int kSrcW = 640;   // the source, at the top rung's size
constexpr int kSrcH = 360;

// The lowbias32 integer hash.
inline std::uint32_t mix32(std::uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// A value in 0..255 at lattice point (cx, cy) of the lattice `seed`.
inline int lattice(std::int64_t cx, std::int64_t cy, std::uint32_t seed)
{
    const std::uint32_t h = mix32(static_cast<std::uint32_t>(cx) * 0x9E3779B1U ^
                                  mix32(static_cast<std::uint32_t>(cy) + seed * 0x85EBCA77U));
    return static_cast<int>(h & 0xff);
}

// Value noise with cells of 2^log2_cell pixels at (x16, y16), in 1/16 pixel
// (both >= 0), bilinear between the lattice's values: 0..255.
inline int value_noise(std::int64_t x16, std::int64_t y16, int log2_cell, std::uint32_t seed)
{
    const int          shift = log2_cell + 4;
    const std::int64_t size  = std::int64_t{ 1 } << shift;
    const std::int64_t cx = x16 >> shift, cy = y16 >> shift;
    const std::int64_t fx = x16 & (size - 1), fy = y16 & (size - 1);
    const std::int64_t v00 = lattice(cx, cy, seed), v10 = lattice(cx + 1, cy, seed);
    const std::int64_t v01 = lattice(cx, cy + 1, seed), v11 = lattice(cx + 1, cy + 1, seed);
    const std::int64_t top = v00 * (size - fx) + v10 * fx;
    const std::int64_t bot = v01 * (size - fx) + v11 * fx;
    const std::int64_t v   = top * (size - fy) + bot * fy;
    return static_cast<int>((v + (size * size) / 2) >> (2 * shift));
}

// The weighted mean of octaves of value noise, each weighted by its cell
// size, so that the amplitude of each octave goes as 1/frequency: 0..255.
inline int fractal(std::int64_t x16, std::int64_t y16, int lo, int hi, std::uint32_t seed)
{
    std::int64_t sum = 0, weights = 0;
    for (int o = lo; o <= hi; ++o) {
        const std::int64_t w = std::int64_t{ 1 } << o;
        sum += w * value_noise(x16, y16, o, seed + static_cast<std::uint32_t>(o) * 101U);
        weights += w;
    }
    return static_cast<int>((sum + weights / 2) / weights);
}

inline std::uint8_t clip_luma(int v) { return static_cast<std::uint8_t>(std::clamp(v, 16, 235)); }
inline std::uint8_t clip_chroma(int v) { return static_cast<std::uint8_t>(std::clamp(v, 16, 240)); }

// A triangle wave of period 2p: 0 up to p and back.
inline int tri(int u, int p)
{
    const int m = u % (2 * p);
    return m <= p ? m : 2 * p - m;
}

constexpr std::int64_t kOff = 4096;   // keeps noise coordinates >= 0

// The main content's frame t: a fractal noise texture (cells of 2 to 64
// pixels) moving 1.5 px left and 0.5 px up a frame, a textured disc and a
// striped disc moving on their own paths across it, low-frequency color.
void main_frame(AVFrame* f, int t)
{
    const std::int64_t px = 24 * static_cast<std::int64_t>(t), py = 8 * static_cast<std::int64_t>(t);
    const int ax = 160 + tri(5 * t, 320), ay = 120 + tri(3 * t, 120), ar = 56;
    const int bx = 480 - tri(4 * t, 300), by = 240 - tri(2 * t, 100), br = 40;
    for (int y = 0; y < kSrcH; ++y) {
        std::uint8_t* row = f->data[0] + static_cast<std::ptrdiff_t>(y) * f->linesize[0];
        for (int x = 0; x < kSrcW; ++x) {
            int v;
            const int dax = x - ax, day = y - ay, dbx = x - bx, dby = y - by;
            if (dax * dax + day * day < ar * ar) {
                v = 140 + (fractal((dax + kOff) * 16, (day + kOff) * 16, 0, 3, 2) - 128) * 2;
            } else if (dbx * dbx + dby * dby < br * br) {
                v = ((dbx + kOff) & 8) ? 60 : 200;
            } else {
                v = 126 + (fractal((x + kOff) * 16 + px, (y + kOff) * 16 + py, 1, 6, 1) - 128) * 3 / 2;
            }
            row[x] = clip_luma(v);
        }
    }
    for (int y = 0; y < kSrcH / 2; ++y) {
        std::uint8_t* u = f->data[1] + static_cast<std::ptrdiff_t>(y) * f->linesize[1];
        std::uint8_t* w = f->data[2] + static_cast<std::ptrdiff_t>(y) * f->linesize[2];
        for (int x = 0; x < kSrcW / 2; ++x) {
            const int lx = 2 * x, ly = 2 * y;
            const int dax = lx - ax, day = ly - ay, dbx = lx - bx, dby = ly - by;
            if (dax * dax + day * day < ar * ar) {
                u[x] = 90;
                w[x] = 170;
            } else if (dbx * dbx + dby * dby < br * br) {
                u[x] = 170;
                w[x] = 100;
            } else {
                const std::int64_t X = (lx + kOff) * 16 + px, Y = (ly + kOff) * 16 + py;
                u[x] = clip_chroma(128 + (fractal(X, Y, 5, 6, 3) - 128) * 3 / 4);
                w[x] = clip_chroma(128 + (fractal(X, Y, 5, 6, 4) - 128) * 3 / 4);
            }
        }
    }
}

// The ad's frame t, the calm ad (testgen.cpp's ad_frame_calm(); D-080):
// content of the main content's kind, a noise texture of another lattice
// (cells of 2 to 64 pixels) moving 1 px left and 0.75 px down a frame, a
// textured box and a striped box moving on their own paths, other
// low-frequency color.
void ad_frame(AVFrame* f, int t)
{
    const std::int64_t px = -16 * static_cast<std::int64_t>(t) + (std::int64_t{ 1 } << 20);
    const std::int64_t py = 12 * static_cast<std::int64_t>(t);
    const int ax = 100 + tri(6 * t, 360), ay = 60 + tri(2 * t, 150), aw = 120, ah = 80;
    const int bx = 420 - tri(3 * t, 260), by = 200 - tri(4 * t, 120), bw = 90, bh = 90;
    for (int y = 0; y < kSrcH; ++y) {
        std::uint8_t* row = f->data[0] + static_cast<std::ptrdiff_t>(y) * f->linesize[0];
        for (int x = 0; x < kSrcW; ++x) {
            int v;
            if (x >= ax && x < ax + aw && y >= ay && y < ay + ah) {
                v = 120 + (fractal((x - ax + kOff) * 16, (y - ay + kOff) * 16, 0, 3, 22) - 128) * 2;
            } else if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
                v = ((y - by + kOff) & 8) ? 70 : 190;
            } else {
                v = 126 + (fractal((x + kOff) * 16 + px, (y + kOff) * 16 + py, 1, 6, 21) - 128) * 3 / 2;
            }
            row[x] = clip_luma(v);
        }
    }
    for (int y = 0; y < kSrcH / 2; ++y) {
        std::uint8_t* u = f->data[1] + static_cast<std::ptrdiff_t>(y) * f->linesize[1];
        std::uint8_t* w = f->data[2] + static_cast<std::ptrdiff_t>(y) * f->linesize[2];
        for (int x = 0; x < kSrcW / 2; ++x) {
            const int lx = 2 * x, ly = 2 * y;
            if (lx >= ax && lx < ax + aw && ly >= ay && ly < ay + ah) {
                u[x] = 170;
                w[x] = 90;
            } else if (lx >= bx && lx < bx + bw && ly >= by && ly < by + bh) {
                u[x] = 100;
                w[x] = 180;
            } else {
                const std::int64_t X = (lx + kOff) * 16 + px, Y = (ly + kOff) * 16 + py;
                u[x] = clip_chroma(150 + (fractal(X, Y, 5, 6, 23) - 128) * 3 / 4);
                w[x] = clip_chroma(110 + (fractal(X, Y, 5, 6, 24) - 128) * 3 / 4);
            }
        }
    }
}

// ── FFmpeg's objects ─────────────────────────────────────────────────────────

std::string av_error(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof buf);
    return buf;
}

struct FrameFree {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct PacketFree {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct CodecFree {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct ParametersFree {
    void operator()(AVCodecParameters* p) const { avcodec_parameters_free(&p); }
};
struct ScalerFree {
    void operator()(SwsContext* s) const { sws_freeContext(s); }
};
using Frame      = std::unique_ptr<AVFrame, FrameFree>;
using Packet     = std::unique_ptr<AVPacket, PacketFree>;
using Codec      = std::unique_ptr<AVCodecContext, CodecFree>;
using Parameters = std::unique_ptr<AVCodecParameters, ParametersFree>;
using Scaler     = std::unique_ptr<SwsContext, ScalerFree>;

// Holds FFmpeg's log at AV_LOG_WARNING, or lower if it was, while it lives.
struct WarningLog {
    int level = av_log_get_level();
    WarningLog() { av_log_set_level(std::min(level, AV_LOG_WARNING)); }
    ~WarningLog() { av_log_set_level(level); }
    WarningLog(const WarningLog&)            = delete;
    WarningLog& operator=(const WarningLog&) = delete;
};

double seconds_since(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// A YUV 4:2:0 frame of w x h with BT.709 tags, its buffers allocated.
Frame new_frame(int w, int h, std::string& err)
{
    Frame f(av_frame_alloc());
    if (!f) {
        err = "av_frame_alloc failed";
        return {};
    }
    f->format          = AV_PIX_FMT_YUV420P;
    f->width           = w;
    f->height          = h;
    f->color_range     = AVCOL_RANGE_MPEG;
    f->colorspace      = AVCOL_SPC_BT709;
    f->color_primaries = AVCOL_PRI_BT709;
    f->color_trc       = AVCOL_TRC_BT709;
    if (const int r = av_frame_get_buffer(f.get(), 0); r < 0) {
        err = "av_frame_get_buffer: " + av_error(r);
        return {};
    }
    return f;
}

// An encoder for one segment of one rung: a closed GOP of the segment's
// frames, its first frame a key frame, `q` the quantizer, one thread and
// bit-exact output so that two runs write the same bytes (testgen.md §2.4,
// §3.3). libx264's wrapper otherwise picks its threads itself.
Codec open_encoder(Encoder e, int w, int h, int q, std::string& err)
{
    const AVCodec* codec = avcodec_find_encoder_by_name(encoder_name(e));
    if (!codec) {
        err = std::format("this FFmpeg build has no encoder {}", encoder_name(e));
        return {};
    }
    Codec enc(avcodec_alloc_context3(codec));
    if (!enc) {
        err = "avcodec_alloc_context3 failed";
        return {};
    }
    enc->width           = w;
    enc->height          = h;
    enc->pix_fmt         = AV_PIX_FMT_YUV420P;
    enc->time_base       = { 1, kFps };
    enc->framerate       = { kFps, 1 };
    enc->gop_size        = kSegmentFrames;
    enc->keyint_min      = kSegmentFrames;
    enc->thread_count    = 1;
    enc->colorspace      = AVCOL_SPC_BT709;
    enc->color_primaries = AVCOL_PRI_BT709;
    enc->color_trc       = AVCOL_TRC_BT709;
    enc->color_range     = AVCOL_RANGE_MPEG;
    enc->flags          |= AV_CODEC_FLAG_BITEXACT | AV_CODEC_FLAG_CLOSED_GOP;
    const auto set = [&](int r, std::string_view what) {
        if (r < 0 && err.empty()) err = std::format("{}: {}", what, av_error(r));
    };
    if (e == Encoder::X264) {
        set(av_opt_set(enc->priv_data, "preset", "veryfast", 0), "libx264's preset");
        set(av_opt_set_int(enc->priv_data, "qp", q, 0), "libx264's qp");
        set(av_opt_set(enc->priv_data, "x264-params", "scenecut=0", 0), "libx264's x264-params");
    } else {
        enc->max_b_frames   = 2;
        enc->flags         |= AV_CODEC_FLAG_QSCALE;
        enc->global_quality = FF_QP2LAMBDA * q;
        // A closed GOP needs scene change detection off (libavcodec/mpegvideo_enc.c,
        // testgen.md §2.4).
        set(av_opt_set_int(enc.get(), "sc_threshold", 1000000000, AV_OPT_SEARCH_CHILDREN),
            "mpeg2video's sc_threshold");
    }
    if (err.empty())
        set(avcodec_open2(enc.get(), codec, nullptr), std::format("avcodec_open2 {}", encoder_name(e)));
    if (!err.empty()) return {};
    return enc;
}

// What a segment's encoder made: its packets, in the encoder's time base.
struct Encoded {
    std::vector<Packet> packets;
    std::string         x264_sei;   // libx264's settings, from its SEI
};

// libx264's settings as its SEI in the first packet gives them, from
// "x264 - core" to the end of the string.
std::string find_x264_sei(const AVPacket* p)
{
    const std::string_view data(reinterpret_cast<const char*>(p->data), static_cast<std::size_t>(p->size));
    const std::size_t at = data.find("x264 - core");
    if (at == std::string_view::npos) return {};
    std::size_t end = at;
    while (end < data.size() && data[end] != '\0' && data[end] != '\x80') ++end;
    return std::string(data.substr(at, end - at));
}

// The CODECS name of an H.264 stream from its first SPS, RFC 6381's
// avc1.PPCCLL: profile_idc, the constraint flags and level_idc in hex.
std::string avc1_name(const AVPacket* p)
{
    for (int i = 0; i + 6 < p->size; ++i)
        if (p->data[i] == 0 && p->data[i + 1] == 0 && p->data[i + 2] == 1 && (p->data[i + 3] & 0x1f) == 7)
            return std::format("avc1.{:02x}{:02x}{:02x}", p->data[i + 4], p->data[i + 5], p->data[i + 6]);
    return {};
}

// Takes the packets `enc` has ready into `out`.
std::string drain(AVCodecContext* enc, Encoder e, Encoded& out)
{
    for (;;) {
        Packet pkt(av_packet_alloc());
        if (!pkt) return "av_packet_alloc failed";
        const int r = avcodec_receive_packet(enc, pkt.get());
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return {};
        if (r < 0) return "avcodec_receive_packet: " + av_error(r);
        if (e == Encoder::X264 && out.x264_sei.empty()) out.x264_sei = find_x264_sei(pkt.get());
        out.packets.push_back(std::move(pkt));
    }
}

// ── MPEG-TS segments ─────────────────────────────────────────────────────────

// One muxer for the consecutive segments of a rung within one playlist
// discontinuity sequence, as FFmpeg's hls muxer writes them: one MPEG-TS
// context whose output file changes at each segment, PAT and PMT sent again
// at the start of each (mpegts_flags resend_headers, libavformat/hlsenc.c),
// so that continuity counters and timestamps go on from one segment to the
// next (RFC 8216 §3). A new sequence gets a new muxer, whose counters start
// again, with the discontinuity indicator on its first packets
// (mpegts_flags initial_discontinuity): the counters marked at the splice
// (D-080). Without the indicator FFmpeg's MPEG-TS demuxer, which the hls
// demuxer runs across the splice, flags packets corrupt (testgen.md §3.6,
// item 2).
class SequenceWriter {
public:
    SequenceWriter() = default;
    ~SequenceWriter()
    {
        if (oc_ && oc_->pb) avio_closep(&oc_->pb);
        avformat_free_context(oc_);
    }
    SequenceWriter(const SequenceWriter&)            = delete;
    SequenceWriter& operator=(const SequenceWriter&) = delete;

    // A muxer for one stream of `par`; with `mark`, the discontinuity
    // indicator on its first packets.
    std::string open(const AVCodecParameters* par, bool mark)
    {
        if (const int r = avformat_alloc_output_context2(&oc_, nullptr, "mpegts", nullptr); r < 0)
            return "the mpegts muxer: " + av_error(r);
        oc_->flags |= AVFMT_FLAG_BITEXACT;
        st_ = avformat_new_stream(oc_, nullptr);
        if (!st_) return "avformat_new_stream failed";
        if (const int r = avcodec_parameters_copy(st_->codecpar, par); r < 0)
            return "avcodec_parameters_copy: " + av_error(r);
        st_->time_base = { 1, 90000 };
        mark_          = mark;
        return {};
    }

    // Starts the segment `file`: the first opens the muxer and writes its
    // header there; each later one flushes what the muxer holds into the file
    // before, then has PAT and PMT sent again in the new one.
    std::string begin(const fs::path& file)
    {
        if (!started_) {
            if (const int r = avio_open(&oc_->pb, file.c_str(), AVIO_FLAG_WRITE); r < 0)
                return std::format("avio_open {}: {}", file.string(), av_error(r));
            AVDictionary* opts = nullptr;
            if (mark_) av_dict_set(&opts, "mpegts_flags", "initial_discontinuity", 0);
            const int r = avformat_write_header(oc_, &opts);
            av_dict_free(&opts);
            if (r < 0) return "avformat_write_header: " + av_error(r);
            started_ = true;
            return {};
        }
        if (const int r = av_write_frame(oc_, nullptr); r < 0) return "the muxer's flush: " + av_error(r);
        avio_flush(oc_->pb);
        if (oc_->pb->error < 0) return "a write: " + av_error(oc_->pb->error);
        avio_closep(&oc_->pb);
        if (const int r = avio_open(&oc_->pb, file.c_str(), AVIO_FLAG_WRITE); r < 0)
            return std::format("avio_open {}: {}", file.string(), av_error(r));
        if (const int r = av_opt_set(oc_->priv_data, "mpegts_flags", "resend_headers", 0); r < 0)
            return "mpegts_flags resend_headers: " + av_error(r);
        return {};
    }

    // `in`, in the encoder's time base 1/kFps.
    std::string write(const AVPacket* in)
    {
        Packet pkt(av_packet_clone(in));
        if (!pkt) return "av_packet_clone failed";
        pkt->stream_index = 0;
        av_packet_rescale_ts(pkt.get(), { 1, kFps }, st_->time_base);
        if (const int r = av_write_frame(oc_, pkt.get()); r < 0) return "av_write_frame: " + av_error(r);
        return {};
    }

    std::string end()
    {
        if (const int r = av_write_trailer(oc_); r < 0) return "av_write_trailer: " + av_error(r);
        const int written = oc_->pb->error;
        avio_closep(&oc_->pb);
        if (written < 0) return "a write: " + av_error(written);
        return {};
    }

private:
    AVFormatContext* oc_      = nullptr;
    AVStream*        st_      = nullptr;
    bool             mark_    = false;
    bool             started_ = false;
};

// Encodes and muxes every segment of every rung of `l` into `dir`, whose rung
// directories exist, recording the x264 SEI and each rung's CODECS in `l`
// and adding the time the source's pictures took to `source_s`.
std::string encode(const fs::path& dir, Layout& l, double& source_s)
{
    std::array<Scaler, kRungs.size()> scalers;
    for (std::size_t r = 0; r < kRungs.size(); ++r) {
        if (kRungs[r].width == kSrcW && kRungs[r].height == kSrcH) continue;
        scalers[r].reset(sws_getContext(kSrcW, kSrcH, AV_PIX_FMT_YUV420P, kRungs[r].width,
                                        kRungs[r].height, AV_PIX_FMT_YUV420P,
                                        SWS_LANCZOS | SWS_ACCURATE_RND | SWS_BITEXACT, nullptr,
                                        nullptr, nullptr));
        if (!scalers[r]) return std::format("sws_getContext for {} failed", kRungs[r].name);
    }

    std::array<std::unique_ptr<SequenceWriter>, kRungs.size()> writers;
    std::string err;
    for (std::size_t s = 0; s < l.segments.size(); ++s) {
        const SegmentLayout& seg = l.segments[s];
        const bool new_sequence = s == 0 || seg.sequence != l.segments[s - 1].sequence;

        // The segment's source pictures, the same for every rung.
        const auto t0 = std::chrono::steady_clock::now();
        std::vector<Frame> src;
        for (int i = 0; i < seg.frames; ++i) {
            Frame f = new_frame(kSrcW, kSrcH, err);
            if (!f) return err;
            if (seg.period == "ad") ad_frame(f.get(), seg.source_first + i);
            else main_frame(f.get(), seg.source_first + i);
            src.push_back(std::move(f));
        }
        source_s += seconds_since(t0);

        for (std::size_t r = 0; r < kRungs.size(); ++r) {
            const Rung&       rung  = kRungs[r];
            const std::string where = std::format("{} {}: ", rung.name, seg.file);
            const int         q     = seg.q[r];
            Codec enc = open_encoder(l.encoder, rung.width, rung.height, q, err);
            if (!enc) return where + err;
            Encoded out;
            for (int i = 0; i < seg.frames; ++i) {
                const AVFrame* in = src[static_cast<std::size_t>(i)].get();
                Frame f = new_frame(rung.width, rung.height, err);
                if (!f) return where + err;
                if (scalers[r]) {
                    if (const int h = sws_scale(scalers[r].get(), in->data, in->linesize, 0, kSrcH,
                                                f->data, f->linesize);
                        h != rung.height)
                        return where + std::format("sws_scale gave {} lines of {}", h, rung.height);
                } else if (const int rc = av_frame_copy(f.get(), in); rc < 0) {
                    return where + "av_frame_copy: " + av_error(rc);
                }
                f->pts = seg.pts_first / kTicksPerFrame + i;   // in the encoder's 1/kFps
                // FFmpeg's MPEG-2 encoder takes a fixed quantizer from each
                // frame's quality, not from global_quality (estimate_qp() in
                // libavcodec/mpegvideo_enc.c), as ffmpeg's -q:v sets it on
                // every frame (testgen.md §1).
                if (l.encoder == Encoder::Mpeg2) f->quality = FF_QP2LAMBDA * q;
                if (const int rc = avcodec_send_frame(enc.get(), f.get()); rc < 0)
                    return where + "avcodec_send_frame: " + av_error(rc);
                if (err = drain(enc.get(), l.encoder, out); !err.empty()) return where + err;
            }
            if (const int rc = avcodec_send_frame(enc.get(), nullptr); rc < 0)
                return where + "the encoder's flush: " + av_error(rc);
            if (err = drain(enc.get(), l.encoder, out); !err.empty()) return where + err;
            if (out.packets.size() != static_cast<std::size_t>(seg.frames))
                return where + std::format("{} packets for {} frames", out.packets.size(), seg.frames);
            if (!(out.packets.front()->flags & AV_PKT_FLAG_KEY))
                return where + "the first packet isn't a key frame";
            Parameters par(avcodec_parameters_alloc());
            if (!par) return where + "avcodec_parameters_alloc failed";
            if (const int rc = avcodec_parameters_from_context(par.get(), enc.get()); rc < 0)
                return where + "avcodec_parameters_from_context: " + av_error(rc);
            if (l.x264_sei.empty()) l.x264_sei = out.x264_sei;
            if (l.rungs[r].codecs.empty() && l.encoder == Encoder::X264)
                l.rungs[r].codecs = avc1_name(out.packets.front().get());
            enc.reset();

            if (new_sequence) {
                if (writers[r] && !(err = writers[r]->end()).empty()) return where + err;
                writers[r] = std::make_unique<SequenceWriter>();
                if (err = writers[r]->open(par.get(), s > 0); !err.empty()) return where + err;
            }
            if (err = writers[r]->begin(dir / rung.name / seg.file); !err.empty()) return where + err;
            for (const Packet& p : out.packets)
                if (err = writers[r]->write(p.get()); !err.empty()) return where + err;
        }
    }
    for (std::size_t r = 0; r < kRungs.size(); ++r)
        if (writers[r] && !(err = writers[r]->end()).empty())
            return std::format("{}: {}", kRungs[r].name, err);
    return {};
}

// ── Playlists and the layout ─────────────────────────────────────────────────

// num / den, both > 0, with `places` decimals, rounded half up: the decimal
// numbers the playlists give (EXTINF, FRAME-RATE, the cue tags'), written
// without floating point.
std::string decimal(std::int64_t num, std::int64_t den, int places)
{
    std::int64_t scale = 1;
    for (int i = 0; i < places; ++i) scale *= 10;
    const std::int64_t v = (2 * num * scale + den) / (2 * den);
    if (places == 0) return std::format("{}", v);
    return std::format("{}.{:0{}}", v / scale, v % scale, places);
}

// The media playlist's lines, each segment's line numbers recorded in `l`
// on the way, so that the lines a layout gives are those of the text.
std::vector<std::string> playlist_lines(Layout& l)
{
    std::vector<std::string> out = {
        "#EXTM3U",
        "#EXT-X-VERSION:3",
        std::format("#EXT-X-TARGETDURATION:{}", (kSegmentFrames + kFps - 1) / kFps),
        "#EXT-X-MEDIA-SEQUENCE:0",
    };
    if (l.kind == Kind::Ad) out.push_back("#EXT-X-DISCONTINUITY-SEQUENCE:0");
    out.push_back("#EXT-X-PLAYLIST-TYPE:VOD");
    const auto line = [&] { return static_cast<int>(out.size()); };
    for (SegmentLayout& seg : l.segments) {
        if (seg.discontinuity) {
            out.push_back("#EXT-X-DISCONTINUITY");
            seg.discontinuity_line = line();
        }
        for (Tag& t : seg.tags) {
            out.push_back(t.text);
            t.line = line();
        }
        out.push_back(std::format("#EXTINF:{},", decimal(seg.frames, kFps, 6)));
        seg.extinf_line = line();
        out.push_back(seg.file);
        seg.uri_line = line();
    }
    out.push_back("#EXT-X-ENDLIST");
    return out;
}

// The ad's first PTS under each arrangement of Q34 (D-080), in 90 kHz ticks:
// the main content's origin, 1 s, or the PTS that the ad's first frame,
// `first_frame`, has on the main content's timeline.
std::int64_t ad_origin(AdTimestamps a, int first_frame)
{
    switch (a) {
    case AdTimestamps::MainOrigin:    return kMainOrigin;
    case AdTimestamps::FromOneSecond: return 90000;
    case AdTimestamps::Continuous:    return kMainOrigin + std::int64_t{ first_frame } * kTicksPerFrame;
    }
    return kMainOrigin;
}

std::string write_text(const fs::path& path, const std::string& text)
{
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
    f.close();
    if (!f) return "cannot write " + path.string();
    return {};
}

std::atomic<int> g_made{ 0 };

} // namespace

// ── Names ────────────────────────────────────────────────────────────────────

const char* kind_name(Kind k) { return k == Kind::Ad ? "ad" : "degraded"; }

const char* ad_timestamps_name(AdTimestamps a)
{
    switch (a) {
    case AdTimestamps::MainOrigin:    return "main-origin";
    case AdTimestamps::FromOneSecond: return "from-1s";
    case AdTimestamps::Continuous:    return "continuous";
    }
    return "main-origin";
}

const char* encoder_name(Encoder e) { return e == Encoder::X264 ? "libx264" : "mpeg2video"; }

Encoder default_encoder()
{
    static const Encoder e = avcodec_find_encoder_by_name("libx264") ? Encoder::X264 : Encoder::Mpeg2;
    return e;
}

int q_normal(Encoder e) { return e == Encoder::X264 ? 22 : 4; }
int q_low(Encoder e) { return e == Encoder::X264 ? 46 : 31; }

Encoder encoder_of(const Spec& spec) { return spec.encoder.value_or(default_encoder()); }

std::string name(const Spec& spec)
{
    std::string n = spec.kind == Kind::Ad ? std::format("ad-{}", ad_timestamps_name(spec.ad)) : "degraded";
    if (encoder_of(spec) != default_encoder()) n += std::format("-{}", encoder_name(encoder_of(spec)));
    return n;
}

// ── The layout ───────────────────────────────────────────────────────────────

std::pair<int, int> Layout::low_frames() const
{
    if (low_segments.empty()) return { 0, 0 };
    const SegmentLayout& last = segments[static_cast<std::size_t>(low_segments.back())];
    return { segments[static_cast<std::size_t>(low_segments.front())].first_frame,
             last.first_frame + last.frames };
}

Layout plan(const Spec& spec)
{
    Layout l;
    l.kind     = spec.kind;
    l.ad       = spec.ad;
    l.encoder  = encoder_of(spec);
    l.q_normal = q_normal(l.encoder);
    l.q_low    = q_low(l.encoder);
    for (const Rung& r : kRungs)
        l.rungs.push_back({ .name = r.name, .width = r.width, .height = r.height,
                            .uri = std::format("{}/index.m3u8", r.name) });

    // testgen.cpp's make_degraded() and make_ad(), with their defaults: the
    // degraded ladder's v270, segment 4; the ad's v180, segments 3 and 4,
    // the cue-out before segment 3 declaring the ad's 2 s, the cue-in before
    // segment 5.
    const bool ad_kind = spec.kind == Kind::Ad;
    l.low_rung     = ad_kind ? 2 : 1;
    l.control_rung = ad_kind ? 1 : 2;
    l.low_segments = ad_kind ? std::vector<int>{ 3, 4 } : std::vector<int>{ 4 };
    int ad_frames = 0;
    if (ad_kind) {
        l.cue_out_segment = 3;
        l.cue_in_segment  = 5;
        ad_frames         = (l.cue_in_segment - l.cue_out_segment) * kSegmentFrames;
        l.break_s         = ad_frames / kFps;
        l.ad_origin       = ad_origin(spec.ad, l.cue_out_segment * kSegmentFrames);
    }
    int sequence = 0;
    for (int s = 0; s < kSegments; ++s) {
        const bool in_ad = ad_kind && s >= l.cue_out_segment && s < l.cue_in_segment;
        SegmentLayout seg;
        seg.index        = s;
        seg.period       = in_ad ? "ad" : "main";
        seg.first_frame  = s * kSegmentFrames;
        // The ad's frames from its own start; the main content's go on
        // through the break.
        seg.source_first = in_ad ? (s - l.cue_out_segment) * kSegmentFrames : s * kSegmentFrames;
        seg.pts_first    = in_ad ? l.ad_origin + std::int64_t{ seg.source_first } * kTicksPerFrame
                                 : kMainOrigin + std::int64_t{ seg.first_frame } * kTicksPerFrame;
        if (!ad_kind) {
            seg.file = std::format("seg_{:03}.ts", s);
        } else {
            seg.file = in_ad ? std::format("ad_{:03}.ts", s - l.cue_out_segment)
                             : std::format("main_{:03}.ts", s);
            // AWS Elemental's plain forms, placed as MediaTailor's origin
            // playlist places them (testgen.md §2.3): the cue-out declaring
            // the break's duration, a cue-out-cont with the time elapsed in
            // it before each later segment of the break.
            seg.discontinuity = s == l.cue_out_segment || s == l.cue_in_segment;
            if (s == l.cue_out_segment)
                seg.tags.push_back({ std::format("#EXT-X-CUE-OUT:{}", decimal(ad_frames, kFps, 3)) });
            else if (in_ad)
                seg.tags.push_back({ std::format("#EXT-X-CUE-OUT-CONT:{}/{}",
                                                 decimal(seg.source_first, kFps, 3),
                                                 decimal(ad_frames, kFps, 3)) });
            else if (s == l.cue_in_segment)
                seg.tags.push_back({ "#EXT-X-CUE-IN" });
        }
        if (seg.discontinuity) ++sequence;
        seg.sequence = sequence;
        const bool low = std::find(l.low_segments.begin(), l.low_segments.end(), s) != l.low_segments.end();
        for (std::size_t r = 0; r < kRungs.size(); ++r)
            seg.q[r] = low && static_cast<int>(r) == l.low_rung ? l.q_low : l.q_normal;
        l.segments.push_back(std::move(seg));
    }
    playlist_lines(l);
    return l;
}

std::string media_playlist(const Layout& layout)
{
    Layout copy = layout;
    std::string text;
    for (const std::string& line : playlist_lines(copy)) text += line + '\n';
    return text;
}

std::string master_playlist(const Layout& layout)
{
    std::string m = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-INDEPENDENT-SEGMENTS\n";
    for (const RungLayout& r : layout.rungs) {
        m += std::format("#EXT-X-STREAM-INF:BANDWIDTH={},AVERAGE-BANDWIDTH={},RESOLUTION={}x{},"
                         "FRAME-RATE={}",
                         r.bandwidth, r.average_bandwidth, r.width, r.height, decimal(kFps, 1, 3));
        if (!r.codecs.empty()) m += std::format(",CODECS=\"{}\"", r.codecs);
        m += std::format("\n{}\n", r.uri);
    }
    return m;
}

std::string layout_json(const Layout& l)
{
    using json::quote;
    const auto per_rung = [](const auto& values) {
        std::string out;
        for (std::size_t r = 0; r < kRungs.size(); ++r)
            out += std::format("{}{}: {}", r ? ", " : "", quote(kRungs[r].name), values[r]);
        return out;
    };
    const auto list = [](const auto& items, const auto& item) {
        std::string out;
        for (std::size_t i = 0; i < items.size(); ++i) out += (i ? ", " : "") + item(items[i]);
        return "[" + out + "]";
    };
    const auto library = [](const char* lib, unsigned v) {
        return std::format("{}: \"{}.{}.{}\"", quote(lib), AV_VERSION_MAJOR(v), AV_VERSION_MINOR(v),
                           AV_VERSION_MICRO(v));
    };
    const bool ad_kind = l.kind == Kind::Ad;

    std::string j = "{\n";
    j += std::format("  \"kind\": {},\n  \"codec\": {},\n  \"source\": \"noise\",\n",
                     quote(kind_name(l.kind)), quote(encoder_name(l.encoder)));
    j += std::format("  \"fps\": {},\n  \"segment_frames\": {},\n", kFps, kSegmentFrames);
    if (ad_kind) j += "  \"ts_discontinuity_indicator\": true,\n";
    j += std::format("  \"x264_sei\": {},\n", quote(l.x264_sei));
    j += std::format("  \"ffmpeg_libraries\": {{{}, {}, {}, {}}},\n",
                     library("libavutil", avutil_version()), library("libavcodec", avcodec_version()),
                     library("libavformat", avformat_version()), library("libswscale", swscale_version()));
    const std::string low = quote(kRungs[static_cast<std::size_t>(l.low_rung)].name);
    const std::string control = quote(kRungs[static_cast<std::size_t>(l.control_rung)].name);
    if (ad_kind) {
        j += std::format("  \"ad\": {{\"rung\": {}, \"control\": {}, \"segments\": {}, "
                         "\"cue_out_segment\": {}, \"cue_in_segment\": {}, \"duration_s\": {}, "
                         "\"timestamps\": {}, \"ad_origin_pts\": {}, \"content\": \"calm\", "
                         "\"q_normal\": {}, \"q_low\": {}}},\n",
                         low, control, list(l.low_segments, [](int s) { return std::to_string(s); }),
                         l.cue_out_segment, l.cue_in_segment, l.break_s, quote(ad_timestamps_name(l.ad)),
                         l.ad_origin, l.q_normal, l.q_low);
    } else {
        j += std::format("  \"degraded\": {{\"rung\": {}, \"control\": {}, \"segment\": {}, "
                         "\"q_normal\": {}, \"q_low\": {}}},\n",
                         low, control, l.low_segments.front(), l.q_normal, l.q_low);
    }
    j += "  \"rungs\": [\n";
    for (std::size_t i = 0; i < l.rungs.size(); ++i) {
        const RungLayout& r = l.rungs[i];
        j += std::format("    {{\"name\": {}, \"width\": {}, \"height\": {}, \"uri\": {}, "
                         "\"bandwidth\": {}, \"average_bandwidth\": {}, \"codecs\": {}, \"bytes\": {}}}{}\n",
                         quote(r.name), r.width, r.height, quote(r.uri), r.bandwidth, r.average_bandwidth,
                         quote(r.codecs), r.bytes, i + 1 < l.rungs.size() ? "," : "");
    }
    j += "  ],\n  \"segments\": [\n";
    for (std::size_t i = 0; i < l.segments.size(); ++i) {
        const SegmentLayout& s = l.segments[i];
        j += std::format(
            "    {{\"index\": {}, \"file\": {}, \"period\": {}, \"first_frame\": {}, \"frames\": {}, "
            "\"position_s\": {}, \"pts_first\": {}, \"pts_first_s\": {}, \"source_first\": {}, "
            "\"discontinuity\": {}, \"discontinuity_sequence\": {}, \"discontinuity_line\": {}, "
            "\"tags\": {}, \"tag_lines\": {}, \"extinf_line\": {}, \"uri_line\": {}, \"q\": {{{}}}, "
            "\"bytes\": {{{}}}}}{}\n",
            s.index, quote(s.file), quote(s.period), s.first_frame, s.frames, s.position_s(), s.pts_first,
            json::number(static_cast<double>(s.pts_first) / 90000.0), s.source_first,
            s.discontinuity ? "true" : "false", s.sequence,
            s.discontinuity ? std::to_string(s.discontinuity_line) : "null",
            list(s.tags, [](const Tag& t) { return quote(t.text); }),
            list(s.tags, [](const Tag& t) { return std::to_string(t.line); }), s.extinf_line, s.uri_line,
            per_rung(s.q), per_rung(s.bytes), i + 1 < l.segments.size() ? "," : "");
    }
    j += "  ]\n}\n";
    return j;
}

std::pair<std::int64_t, std::int64_t> bandwidths(const std::vector<std::uint64_t>& segment_bytes)
{
    if (segment_bytes.empty()) return { 0, 0 };
    std::uint64_t peak = 0, total = 0;
    for (const std::uint64_t b : segment_bytes) {
        peak = std::max(peak, b);
        total += b;
    }
    // bytes x 8 bits over frames / kFps seconds, rounded up.
    const auto rate = [](std::uint64_t bytes, std::uint64_t frames) {
        return static_cast<std::int64_t>((bytes * 8 * kFps + frames - 1) / frames);
    };
    return { rate(peak, kSegmentFrames), rate(total, segment_bytes.size() * kSegmentFrames) };
}

std::string rung_order_error(const std::vector<RungLayout>& rungs)
{
    for (std::size_t i = 1; i < rungs.size(); ++i)
        if (rungs[i].bandwidth >= rungs[i - 1].bandwidth)
            return std::format("BANDWIDTH of {} ({}) isn't below that of {} ({})", rungs[i].name,
                               rungs[i].bandwidth, rungs[i - 1].name, rungs[i - 1].bandwidth);
    return {};
}

// ── Making a ladder ──────────────────────────────────────────────────────────

std::string Ladder::playlist(int rung) const
{
    return (dir / kRungs[static_cast<std::size_t>(rung)].name / "index.m3u8").string();
}

Ladder write_ladder(const fs::path& dir, const Spec& spec)
{
    const auto t0 = std::chrono::steady_clock::now();
    Ladder out;
    out.spec   = spec;
    out.dir    = dir;
    out.master = (dir / "master.m3u8").string();
    out.layout = plan(spec);
    Layout& l  = out.layout;

    std::error_code ec;
    if (fs::exists(dir, ec) && !fs::is_empty(dir, ec)) {
        out.error = dir.string() + " is not empty: a ladder is written into a new or empty directory";
        return out;
    }
    for (const Rung& r : kRungs) {
        fs::create_directories(dir / r.name, ec);
        if (ec) {
            out.error = std::format("cannot create {}: {}", (dir / r.name).string(), ec.message());
            return out;
        }
    }
    {
        const WarningLog quiet;
        out.error = encode(dir, l, out.source_seconds);
    }
    if (!out.error.empty()) return out;

    // Sizes and bandwidths (RFC 8216 §4.3.4.2), media playlists, the master
    // once the rungs' BANDWIDTHs are in top-rung order, the layout.
    for (std::size_t r = 0; r < kRungs.size(); ++r) {
        std::vector<std::uint64_t> sizes;
        for (SegmentLayout& seg : l.segments) {
            const fs::path file = dir / kRungs[r].name / seg.file;
            seg.bytes[r] = fs::file_size(file, ec);
            if (ec) {
                out.error = std::format("cannot read the size of {}: {}", file.string(), ec.message());
                return out;
            }
            sizes.push_back(seg.bytes[r]);
            l.rungs[r].bytes += seg.bytes[r];
        }
        std::tie(l.rungs[r].bandwidth, l.rungs[r].average_bandwidth) = bandwidths(sizes);
        out.error = write_text(dir / kRungs[r].name / "index.m3u8", media_playlist(l));
        if (!out.error.empty()) return out;
    }
    if (out.error = rung_order_error(l.rungs); !out.error.empty()) return out;
    if (out.error = write_text(out.master, master_playlist(l)); !out.error.empty()) return out;
    if (out.error = write_text(dir / "layout.json", layout_json(l)); !out.error.empty()) return out;
    out.seconds = seconds_since(t0);
    ++g_made;
    return out;
}

int made() { return g_made.load(); }

const Ladder& ladder(const Spec& spec)
{
    static std::mutex mtx;
    static std::map<std::string, std::unique_ptr<Ladder>> ladders;   // by directory
    const fs::path dir = media_dir() / name(spec);
    const std::lock_guard g(mtx);
    std::unique_ptr<Ladder>& slot = ladders[dir.string()];
    if (!slot) slot = std::make_unique<Ladder>(write_ladder(dir, spec));
    return *slot;
}

fs::path media_dir()
{
    const char* keep = std::getenv("MIRAGE_KEEP_MEDIA");
    if (!keep || !*keep) return testtemp::dir();
    std::error_code ec;
    fs::path dir = fs::absolute(fs::path(keep), ec);
    if (ec) dir = keep;
    dir /= dir_name(Catch::getResultCapture().getCurrentTestName());
    fs::create_directories(dir, ec);   // a failure shows where a file is written there
    return dir;
}

std::string dir_name(std::string_view test_name)
{
    std::string out;
    for (const char c : test_name) {
        const bool keep = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                          c == '-' || c == '_' || c == '.';
        out += keep ? c : '_';
    }
    if (out.find_first_not_of('.') == std::string::npos) out.insert(0, "_");
    return out;
}

} // namespace synthladder
