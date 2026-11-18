// testgen: a prototype of the synthetic content of the work order's §3.2
// acceptance tests (docs/DECISIONS.md D-069), made with FFmpeg's libraries
// only (libavformat, libavcodec, libavutil, libswscale), never the ffmpeg
// binary. It writes local HLS ladders of MPEG-TS segments:
//
//   degraded  one segment of one lower rung encoded at a much lower quality
//             than the rest of that rung;
//   ad        main content, then an "ad" of other content between
//             EXT-X-DISCONTINUITY tags, EXT-X-CUE-OUT before the ad's first
//             segment and EXT-X-CUE-IN after its last, one lower rung's ad at
//             a low quality;
//
// and, to check them: a lossless mezzanine of a ladder's source,
// the list of encoders the FFmpeg build has, and a probe that reads a playlist
// through FFmpeg's hls demuxer and prints each packet and decoded frame.
//
// Every rung is the same source, made at the top rung's size (640x360) and
// scaled down with libswscale, as a ladder's rungs are encoded from one
// mezzanine. The source is computed with integer arithmetic only, so that its
// pixels don't depend on a machine's floating point.
//
// usage:
//   testgen encoders
//   testgen degraded <dir> [--codec libx264|mpeg2video] [--source noise|ramp]
//                          [--k K] [--rung NAME] [--q-normal Q] [--q-low Q]
//                          [--nominal-bandwidth]
//   testgen ad <dir> [--codec libx264|mpeg2video] [--ad-origin SECONDS]
//                    [--no-ts-discontinuity] [--rung NAME] [--q-normal Q] [--q-low Q]
//                    [--ad-content calm|busy]
//   testgen mezzanine <file.mkv> [--kind degraded|ad] [--source noise|ramp]
//                     [--ad-content calm|busy]
//   testgen probe <url>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

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

namespace {

// ── Errors ───────────────────────────────────────────────────────────────────

std::string av_error(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof buf);
    return buf;
}

[[noreturn]] void fail(const std::string& what) { throw std::runtime_error(what); }

int check(int ret, const std::string& what)
{
    if (ret < 0) fail(what + ": " + av_error(ret));
    return ret;
}

void write_text(const fs::path& path, const std::string& text)
{
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
    if (!f) fail("cannot write " + path.string());
}

double seconds_since(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

// ── Constants of the ladders ─────────────────────────────────────────────────

constexpr int kFps          = 25;
constexpr int kSegFrames    = 25;       // 1 s segments
constexpr int kSegments     = 8;        // 8 s, 200 frames, in both ladders
constexpr int kSrcW         = 640;      // the source, at the top rung's size
constexpr int kSrcH         = 360;
constexpr int kMainOrigin   = 10 * kFps;   // the main content's first PTS: 10 s, in frames
constexpr int kTsPerFrame   = 90000 / kFps;

struct Rung {
    const char* name;
    int         w, h;
};
constexpr Rung kRungs[] = { { "v360", 640, 360 }, { "v270", 480, 270 }, { "v180", 320, 180 } };

// ── The source: integer value noise, moving shapes ───────────────────────────

// Chris Wellons' lowbias32 integer hash.
inline uint32_t mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

// A value in 0..255 at lattice point (cx, cy) of the lattice `seed`.
inline int lattice(int64_t cx, int64_t cy, uint32_t seed)
{
    const uint32_t h = mix32(static_cast<uint32_t>(cx) * 0x9E3779B1U ^
                             mix32(static_cast<uint32_t>(cy) + seed * 0x85EBCA77U));
    return static_cast<int>(h & 0xff);
}

// Value noise with cells of 2^log2_cell pixels at (x16, y16), in 1/16 pixel
// (both >= 0), bilinear between the lattice's values: 0..255.
inline int value_noise(int64_t x16, int64_t y16, int log2_cell, uint32_t seed)
{
    const int     shift = log2_cell + 4;
    const int64_t size  = int64_t{ 1 } << shift;
    const int64_t cx = x16 >> shift, cy = y16 >> shift;
    const int64_t fx = x16 & (size - 1), fy = y16 & (size - 1);
    const int64_t v00 = lattice(cx, cy, seed), v10 = lattice(cx + 1, cy, seed);
    const int64_t v01 = lattice(cx, cy + 1, seed), v11 = lattice(cx + 1, cy + 1, seed);
    const int64_t top = v00 * (size - fx) + v10 * fx;
    const int64_t bot = v01 * (size - fx) + v11 * fx;
    const int64_t v   = top * (size - fy) + bot * fy;
    return static_cast<int>((v + (size * size) / 2) >> (2 * shift));
}

// The weighted mean of octaves of value noise, each weighted by its cell size,
// so that the amplitude of each octave goes as 1/frequency: 0..255.
inline int fractal(int64_t x16, int64_t y16, int lo, int hi, uint32_t seed)
{
    int64_t sum = 0, weights = 0;
    for (int o = lo; o <= hi; ++o) {
        const int64_t w = int64_t{ 1 } << o;
        sum += w * value_noise(x16, y16, o, seed + static_cast<uint32_t>(o) * 101U);
        weights += w;
    }
    return static_cast<int>((sum + weights / 2) / weights);
}

inline uint8_t clip_luma(int v) { return static_cast<uint8_t>(std::clamp(v, 16, 235)); }
inline uint8_t clip_chroma(int v) { return static_cast<uint8_t>(std::clamp(v, 16, 240)); }

// A triangle wave of period 2p: 0 up to p and back.
inline int tri(int u, int p)
{
    const int m = u % (2 * p);
    return m <= p ? m : 2 * p - m;
}

constexpr int64_t kOff = 4096;   // keeps noise coordinates >= 0

// The main content's frame t: a fractal noise texture (cells of 2 to 64
// pixels) moving 1.5 px left and 0.5 px up a frame, a textured disc and a
// striped disc moving on their own paths across it, low-frequency color.
void main_frame(AVFrame* f, int t)
{
    const int64_t px = 24 * static_cast<int64_t>(t), py = 8 * static_cast<int64_t>(t);   // 1/16 px
    const int ax = 160 + tri(5 * t, 320), ay = 120 + tri(3 * t, 120), ar = 56;
    const int bx = 480 - tri(4 * t, 300), by = 240 - tri(2 * t, 100), br = 40;
    for (int y = 0; y < kSrcH; ++y) {
        uint8_t* row = f->data[0] + static_cast<std::ptrdiff_t>(y) * f->linesize[0];
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
        uint8_t* u = f->data[1] + static_cast<std::ptrdiff_t>(y) * f->linesize[1];
        uint8_t* w = f->data[2] + static_cast<std::ptrdiff_t>(y) * f->linesize[2];
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
                const int64_t X = (lx + kOff) * 16 + px, Y = (ly + kOff) * 16 + py;
                u[x] = clip_chroma(128 + (fractal(X, Y, 5, 6, 3) - 128) * 3 / 4);
                w[x] = clip_chroma(128 + (fractal(X, Y, 5, 6, 4) - 128) * 3 / 4);
            }
        }
    }
}

// A busier ad (--ad-content busy): other content, harder to encode than the
// main content: a noise texture of another lattice (cells of 2 to 32 pixels)
// moving 2.25 px right and 0.75 px down a frame, diagonal bands that move
// across it, a checkered box that moves left and right, and saturated color.
void ad_frame(AVFrame* f, int t)
{
    const int64_t px = -36 * static_cast<int64_t>(t), py = -12 * static_cast<int64_t>(t);
    const int bx = 40 + tri(7 * t, 400), by = 220, bw = 160, bh = 90;
    for (int y = 0; y < kSrcH; ++y) {
        uint8_t* row = f->data[0] + static_cast<std::ptrdiff_t>(y) * f->linesize[0];
        for (int x = 0; x < kSrcW; ++x) {
            int v;
            if (x >= bx && x < bx + bw && y >= by && y < by + bh) {
                v = (((x - bx) >> 3) + ((y - by) >> 3)) & 1 ? 40 : 210;
            } else {
                v = 120 + (fractal((x + kOff) * 16 + px + (int64_t{ 1 } << 20),
                                   (y + kOff) * 16 + py + (int64_t{ 1 } << 20), 1, 5, 11) - 128) * 3 / 2;
                if (((x + y + 3 * t) >> 4) & 1) v = v * 3 / 4 + 40;
            }
            row[x] = clip_luma(v);
        }
    }
    for (int y = 0; y < kSrcH / 2; ++y) {
        uint8_t* u = f->data[1] + static_cast<std::ptrdiff_t>(y) * f->linesize[1];
        uint8_t* w = f->data[2] + static_cast<std::ptrdiff_t>(y) * f->linesize[2];
        for (int x = 0; x < kSrcW / 2; ++x) {
            const int lx = 2 * x, ly = 2 * y;
            if (lx >= bx && lx < bx + bw && ly >= by && ly < by + bh) {
                u[x] = 128;
                w[x] = 128;
            } else {
                const int64_t X = (lx + kOff) * 16 + px + (int64_t{ 1 } << 20);
                const int64_t Y = (ly + kOff) * 16 + py + (int64_t{ 1 } << 20);
                u[x] = clip_chroma(60 + (fractal(X, Y, 4, 6, 13) - 128));
                w[x] = clip_chroma(190 + (fractal(X, Y, 4, 6, 14) - 128));
            }
        }
    }
}

// The ad's frame t (--ad-content calm, the default): other content of the
// main content's kind: a noise texture of another lattice (cells of 2 to 64
// pixels, as the main content's) moving 1 px left and 0.75 px down a frame, a
// textured box and a striped box moving on their own paths, other
// low-frequency color.
void ad_frame_calm(AVFrame* f, int t)
{
    const int64_t px = -16 * static_cast<int64_t>(t) + (int64_t{ 1 } << 20), py = 12 * static_cast<int64_t>(t);
    const int ax = 100 + tri(6 * t, 360), ay = 60 + tri(2 * t, 150), aw = 120, ah = 80;
    const int bx = 420 - tri(3 * t, 260), by = 200 - tri(4 * t, 120), bw = 90, bh = 90;
    for (int y = 0; y < kSrcH; ++y) {
        uint8_t* row = f->data[0] + static_cast<std::ptrdiff_t>(y) * f->linesize[0];
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
        uint8_t* u = f->data[1] + static_cast<std::ptrdiff_t>(y) * f->linesize[1];
        uint8_t* w = f->data[2] + static_cast<std::ptrdiff_t>(y) * f->linesize[2];
        for (int x = 0; x < kSrcW / 2; ++x) {
            const int lx = 2 * x, ly = 2 * y;
            if (lx >= ax && lx < ax + aw && ly >= ay && ly < ay + ah) {
                u[x] = 170;
                w[x] = 90;
            } else if (lx >= bx && lx < bx + bw && ly >= by && ly < by + bh) {
                u[x] = 100;
                w[x] = 180;
            } else {
                const int64_t X = (lx + kOff) * 16 + px, Y = (ly + kOff) * 16 + py;
                u[x] = clip_chroma(150 + (fractal(X, Y, 5, 6, 23) - 128) * 3 / 4);
                w[x] = clip_chroma(110 + (fractal(X, Y, 5, 6, 24) - 128) * 3 / 4);
            }
        }
    }
}

// The existing tests' source (tests/test_vmaf_ladder.cpp, make_rung()): a
// gray diagonal ramp that moves 3 steps a frame, for comparison.
void ramp_frame(AVFrame* f, int t)
{
    for (int y = 0; y < kSrcH; ++y)
        for (int x = 0; x < kSrcW; ++x)
            f->data[0][y * f->linesize[0] + x] =
                static_cast<uint8_t>((x * 256 / kSrcW + y * 128 / kSrcH + 3 * t) & 0xff);
    for (int p = 1; p < 3; ++p)
        for (int y = 0; y < kSrcH / 2; ++y)
            std::fill_n(f->data[p] + y * f->linesize[p], kSrcW / 2, static_cast<uint8_t>(128));
}

using FrameFn = std::function<void(AVFrame*, int)>;

AVFrame* new_frame(int w, int h)
{
    AVFrame* f = av_frame_alloc();
    if (!f) fail("av_frame_alloc");
    f->format          = AV_PIX_FMT_YUV420P;
    f->width           = w;
    f->height          = h;
    f->color_range     = AVCOL_RANGE_MPEG;
    f->colorspace      = AVCOL_SPC_BT709;
    f->color_primaries = AVCOL_PRI_BT709;
    f->color_trc       = AVCOL_TRC_BT709;
    check(av_frame_get_buffer(f, 0), "av_frame_get_buffer");
    return f;
}

// ITU-T P.910's spatial and temporal information of a source's luma: SI, the
// largest standard deviation over a frame of its Sobel gradient's magnitude
// (the frame's border left out), and TI, the largest standard deviation of
// the difference between a frame and the one before it.
struct SiTi {
    double si = 0.0, ti = 0.0;
    std::vector<uint8_t> prev;

    void add(const AVFrame* f)
    {
        const int w = f->width, h = f->height, ls = f->linesize[0];
        const uint8_t* p = f->data[0];
        double s = 0.0, ss = 0.0;
        long n = 0;
        for (int y = 1; y < h - 1; ++y)
            for (int x = 1; x < w - 1; ++x) {
                const auto at = [&](int dx, int dy) { return static_cast<int>(p[(y + dy) * ls + x + dx]); };
                const int gx = at(1, -1) + 2 * at(1, 0) + at(1, 1) - at(-1, -1) - 2 * at(-1, 0) - at(-1, 1);
                const int gy = at(-1, 1) + 2 * at(0, 1) + at(1, 1) - at(-1, -1) - 2 * at(0, -1) - at(1, -1);
                const double m = std::sqrt(static_cast<double>(gx * gx + gy * gy));
                s += m;
                ss += m * m;
                ++n;
            }
        si = std::max(si, std::sqrt(std::max(0.0, ss / n - (s / n) * (s / n))));
        std::vector<uint8_t> cur(static_cast<std::size_t>(w) * h);
        for (int y = 0; y < h; ++y) std::memcpy(&cur[static_cast<std::size_t>(y) * w], p + y * ls, w);
        if (!prev.empty()) {
            double d = 0.0, dd = 0.0;
            for (std::size_t i = 0; i < cur.size(); ++i) {
                const double v = static_cast<double>(cur[i]) - prev[i];
                d += v;
                dd += v * v;
            }
            const double m = static_cast<double>(cur.size());
            ti = std::max(ti, std::sqrt(std::max(0.0, dd / m - (d / m) * (d / m))));
        }
        prev = std::move(cur);
    }
};

// ── Encoding ─────────────────────────────────────────────────────────────────

struct Codec {
    std::string name;   // libx264 or mpeg2video
};

// An encoder for one segment of one rung: closed GOP, the segment's first
// frame a key frame, `q` the quantizer (x264's constant QP, MPEG-2's qscale).
AVCodecContext* open_encoder(const Codec& c, int w, int h, int q)
{
    const AVCodec* codec = avcodec_find_encoder_by_name(c.name.c_str());
    if (!codec) fail("this FFmpeg build has no encoder " + c.name);
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    if (!enc) fail("avcodec_alloc_context3");
    enc->width           = w;
    enc->height          = h;
    enc->pix_fmt         = AV_PIX_FMT_YUV420P;
    enc->time_base       = { 1, kFps };
    enc->framerate       = { kFps, 1 };
    enc->gop_size        = kSegFrames;
    enc->keyint_min      = kSegFrames;
    enc->thread_count    = 1;
    enc->colorspace      = AVCOL_SPC_BT709;
    enc->color_primaries = AVCOL_PRI_BT709;
    enc->color_trc       = AVCOL_TRC_BT709;
    enc->color_range     = AVCOL_RANGE_MPEG;
    enc->flags          |= AV_CODEC_FLAG_BITEXACT | AV_CODEC_FLAG_CLOSED_GOP;
    if (c.name == "libx264") {
        check(av_opt_set(enc->priv_data, "preset", "veryfast", 0), "x264 preset");
        check(av_opt_set_int(enc->priv_data, "qp", q, 0), "x264 qp");
        check(av_opt_set(enc->priv_data, "x264-params", "scenecut=0", 0), "x264-params");
    } else if (c.name == "mpeg2video") {
        enc->max_b_frames   = 2;
        enc->flags         |= AV_CODEC_FLAG_QSCALE;
        enc->global_quality = FF_QP2LAMBDA * q;
        // A closed GOP needs scene change detection off (mpegvideo_enc.c).
        check(av_opt_set_int(enc, "sc_threshold", 1000000000, AV_OPT_SEARCH_CHILDREN), "sc_threshold");
    } else {
        fail("unsupported codec " + c.name);
    }
    check(avcodec_open2(enc, codec, nullptr), "avcodec_open2 " + c.name);
    return enc;
}

// What a segment's encoder made: its packets, in the encoder's time base.
struct Encoded {
    std::vector<AVPacket*> packets;
    AVCodecParameters*     par = nullptr;
    std::string            x264_sei;   // libx264's settings, from its SEI

    Encoded() = default;
    Encoded(const Encoded&) = delete;
    Encoded& operator=(const Encoded&) = delete;
    Encoded(Encoded&& o) noexcept : packets(std::move(o.packets)), par(o.par), x264_sei(std::move(o.x264_sei)) { o.par = nullptr; }
    ~Encoded()
    {
        for (AVPacket*& p : packets) av_packet_free(&p);
        avcodec_parameters_free(&par);
    }
};

std::string find_x264_sei(const AVPacket* p)
{
    const std::string_view data(reinterpret_cast<const char*>(p->data), p->size);
    const std::size_t at = data.find("x264 - core");
    if (at == std::string_view::npos) return {};
    std::size_t end = at;
    while (end < data.size() && data[end] != '\0' && data[end] != '\x80') ++end;
    return std::string(data.substr(at, end - at));
}

// The CODECS name of an H.264 stream from its first SPS (RFC 6381: avc1.PPCCLL).
std::string avc1_name(const AVPacket* p)
{
    for (int i = 0; i + 6 < p->size; ++i)
        if (p->data[i] == 0 && p->data[i + 1] == 0 && p->data[i + 2] == 1 && (p->data[i + 3] & 0x1f) == 7)
            return std::format("avc1.{:02x}{:02x}{:02x}", p->data[i + 4], p->data[i + 5], p->data[i + 6]);
    return {};
}

void drain(AVCodecContext* enc, Encoded& out)
{
    for (;;) {
        AVPacket* pkt = av_packet_alloc();
        const int r = avcodec_receive_packet(enc, pkt);
        if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
            av_packet_free(&pkt);
            return;
        }
        check(r, "avcodec_receive_packet");
        if (out.x264_sei.empty()) out.x264_sei = find_x264_sei(pkt);
        out.packets.push_back(pkt);
    }
}

// ── MPEG-TS segments ─────────────────────────────────────────────────────────

// One muxer for the consecutive segments of a rung within one discontinuity
// sequence, as FFmpeg's hls muxer writes them: one MPEG-TS context whose
// output file changes at each segment, PAT and PMT sent again at the start of
// each, so that continuity counters and timestamps go on from one segment to
// the next (RFC 8216 §3). A new sequence gets a new muxer, whose counters
// start again, and, unless asked not to, the discontinuity indicator on its
// first packets (mpegts_flags initial_discontinuity).
class SequenceWriter {
public:
    SequenceWriter(const AVCodecParameters* par, bool mark_discontinuity)
    {
        check(avformat_alloc_output_context2(&oc_, nullptr, "mpegts", nullptr), "mpegts muxer");
        oc_->flags |= AVFMT_FLAG_BITEXACT;
        st_ = avformat_new_stream(oc_, nullptr);
        if (!st_) fail("avformat_new_stream");
        check(avcodec_parameters_copy(st_->codecpar, par), "avcodec_parameters_copy");
        st_->time_base = { 1, 90000 };
        discontinuity_ = mark_discontinuity;
    }
    ~SequenceWriter()
    {
        if (oc_ && oc_->pb) avio_closep(&oc_->pb);
        avformat_free_context(oc_);
    }
    SequenceWriter(const SequenceWriter&) = delete;
    SequenceWriter& operator=(const SequenceWriter&) = delete;

    void begin(const fs::path& file)
    {
        if (!opened_) {
            check(avio_open(&oc_->pb, file.c_str(), AVIO_FLAG_WRITE), "avio_open " + file.string());
            AVDictionary* opts = nullptr;
            if (discontinuity_) av_dict_set(&opts, "mpegts_flags", "initial_discontinuity", 0);
            const int r = avformat_write_header(oc_, &opts);
            av_dict_free(&opts);
            check(r, "avformat_write_header");
            opened_ = true;
            return;
        }
        check(av_write_frame(oc_, nullptr), "flush");
        avio_flush(oc_->pb);
        avio_closep(&oc_->pb);
        check(avio_open(&oc_->pb, file.c_str(), AVIO_FLAG_WRITE), "avio_open " + file.string());
        check(av_opt_set(oc_->priv_data, "mpegts_flags", "resend_headers", 0), "resend_headers");
    }

    void write(const AVPacket* in)
    {
        AVPacket* pkt = av_packet_clone(in);
        pkt->stream_index = 0;
        av_packet_rescale_ts(pkt, { 1, kFps }, st_->time_base);
        const int r = av_write_frame(oc_, pkt);
        av_packet_free(&pkt);
        check(r, "av_write_frame");
    }

    void end()
    {
        check(av_write_trailer(oc_), "av_write_trailer");
        avio_closep(&oc_->pb);
    }

private:
    AVFormatContext* oc_ = nullptr;
    AVStream*        st_ = nullptr;
    bool             opened_ = false;
    bool             discontinuity_ = false;
};

// ── The ladders ──────────────────────────────────────────────────────────────

// One segment of the plan, the same in every rung but for its quantizer.
struct Segment {
    std::string              file;            // in each rung's directory
    std::string              period;          // "main" or "ad"
    int                      source_first;    // first frame of its source
    int64_t                  pts_first;       // in frames (1/25 s)
    bool                     discontinuity;   // EXT-X-DISCONTINUITY before it
    std::vector<std::string> tags;            // other tags before it
    std::map<std::string, int> q;             // the quantizer of each rung
};

struct RungOut {
    std::string           name;
    int                   w = 0, h = 0;
    std::vector<uint64_t> bytes;   // of each segment
    std::string           codecs;
    int64_t               bandwidth = 0, average_bandwidth = 0;
};

std::string json_string(const std::string& s)
{
    std::string out = "\"";
    for (const char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        if (static_cast<unsigned char>(c) < 0x20) {
            out += std::format("\\u{:04x}", static_cast<int>(c));
            continue;
        }
        out += c;
    }
    return out + "\"";
}

struct Options {
    Codec       codec{ "libx264" };
    std::string source   = "noise";
    int         k        = 4;
    std::string rung;          // the rung made worse: degraded v270, ad v180 by default
    std::optional<int> q_normal, q_low;
    double      ad_origin_s = 10.0;
    bool        ts_discontinuity = true;
    bool        nominal_bandwidth = false;   // BANDWIDTH 900000, 600000, 300000 instead of the measured
    std::string mezzanine_kind = "degraded"; // ad: main frames 0-74, the ad's 0-49, main 125-199
    std::string ad_content = "calm";         // calm: ad_frame_calm(); busy: ad_frame()
};

int default_q_normal(const Codec& c) { return c.name == "libx264" ? 22 : 4; }
int default_q_low(const Codec& c) { return c.name == "libx264" ? 46 : 31; }

// Writes the ladder of `plan` into `dir`: a directory per rung with its
// segments and media playlist, the master playlist, and layout.json, what
// each segment is. Returns nothing; prints what it made.
void write_ladder(const fs::path& dir, const std::string& kind, const Options& o,
                  const std::vector<Segment>& plan, const std::string& layout_extra)
{
    const auto t0 = std::chrono::steady_clock::now();
    if (fs::exists(dir) && !fs::is_empty(dir)) fail(dir.string() + " is not empty");
    fs::create_directories(dir);

    std::vector<RungOut> rungs;
    std::vector<SwsContext*> scalers;
    for (const Rung& r : kRungs) {
        fs::create_directories(dir / r.name);
        rungs.push_back({ r.name, r.w, r.h, {}, {}, 0, 0 });
        SwsContext* sws = nullptr;
        if (r.w != kSrcW || r.h != kSrcH) {
            sws = sws_getContext(kSrcW, kSrcH, AV_PIX_FMT_YUV420P, r.w, r.h, AV_PIX_FMT_YUV420P,
                                 SWS_LANCZOS | SWS_ACCURATE_RND | SWS_BITEXACT, nullptr, nullptr, nullptr);
            if (!sws) fail("sws_getContext");
        }
        scalers.push_back(sws);
    }

    SiTi main_siti, ad_siti;
    std::map<std::string, int> next_source;   // per period: the source frame that would follow
    std::string x264_sei;
    std::vector<std::unique_ptr<SequenceWriter>> writers(std::size(kRungs));
    std::string prev_period;
    double source_s = 0.0, encode_s = 0.0;
    for (const Segment& seg : plan) {
        const bool new_sequence = seg.period != prev_period;
        prev_period = seg.period;
        // The segment's source frames.
        const auto ts = std::chrono::steady_clock::now();
        std::vector<AVFrame*> src;
        SiTi& siti = seg.period == "ad" ? ad_siti : main_siti;
        if (next_source.contains(seg.period) && next_source[seg.period] != seg.source_first)
            siti.prev.clear();   // TI is measured between consecutive frames of a source only
        next_source[seg.period] = seg.source_first + kSegFrames;
        for (int i = 0; i < kSegFrames; ++i) {
            AVFrame* f = new_frame(kSrcW, kSrcH);
            const int t = seg.source_first + i;
            if (seg.period == "ad" && o.ad_content == "calm") ad_frame_calm(f, t);
            else if (seg.period == "ad") ad_frame(f, t);
            else if (o.source == "ramp") ramp_frame(f, t);
            else main_frame(f, t);
            siti.add(f);
            src.push_back(f);
        }
        source_s += seconds_since(ts);
        const auto te = std::chrono::steady_clock::now();
        for (std::size_t r = 0; r < std::size(kRungs); ++r) {
            const Rung& rung = kRungs[r];
            const int q = seg.q.at(rung.name);
            AVCodecContext* enc = open_encoder(o.codec, rung.w, rung.h, q);
            Encoded out;
            for (int i = 0; i < kSegFrames; ++i) {
                AVFrame* f = new_frame(rung.w, rung.h);
                if (scalers[r])
                    sws_scale(scalers[r], src[i]->data, src[i]->linesize, 0, kSrcH, f->data, f->linesize);
                else
                    check(av_frame_copy(f, src[i]), "av_frame_copy");
                f->pts = seg.pts_first + i;
                // FFmpeg's MPEG-2 encoder takes a fixed quantizer from each
                // frame's quality, not from global_quality (mpegvideo_enc.c,
                // estimate_qp()), as ffmpeg's -q:v sets it on every frame.
                if (o.codec.name == "mpeg2video") f->quality = FF_QP2LAMBDA * q;
                check(avcodec_send_frame(enc, f), "avcodec_send_frame");
                av_frame_free(&f);
                drain(enc, out);
            }
            check(avcodec_send_frame(enc, nullptr), "avcodec_send_frame (flush)");
            drain(enc, out);
            if (static_cast<int>(out.packets.size()) != kSegFrames)
                fail(std::format("{} {}: {} packets for {} frames", rung.name, seg.file,
                                 out.packets.size(), kSegFrames));
            if (!(out.packets.front()->flags & AV_PKT_FLAG_KEY))
                fail(std::format("{} {}: the first packet isn't a key frame", rung.name, seg.file));
            out.par = avcodec_parameters_alloc();
            check(avcodec_parameters_from_context(out.par, enc), "avcodec_parameters_from_context");
            if (x264_sei.empty()) x264_sei = out.x264_sei;
            if (rungs[r].codecs.empty() && o.codec.name == "libx264")
                rungs[r].codecs = avc1_name(out.packets.front());
            avcodec_free_context(&enc);

            if (new_sequence) {
                if (writers[r]) writers[r]->end();
                const bool mark = o.ts_discontinuity && &seg != &plan.front();
                writers[r] = std::make_unique<SequenceWriter>(out.par, mark);
            }
            const fs::path file = dir / rung.name / seg.file;
            writers[r]->begin(file);
            for (const AVPacket* p : out.packets) writers[r]->write(p);
        }
        encode_s += seconds_since(te);
        for (AVFrame*& f : src) av_frame_free(&f);
        // The sizes are known once the next segment begins, or at the end.
    }
    for (auto& w : writers)
        if (w) w->end();
    for (SwsContext* s : scalers) sws_freeContext(s);

    // Sizes, bandwidths (RFC 8216 §4.3.4.2: BANDWIDTH the peak segment bit
    // rate, AVERAGE-BANDWIDTH the average), media playlists.
    const double seg_s = static_cast<double>(kSegFrames) / kFps;
    for (RungOut& r : rungs) {
        uint64_t total = 0, peak = 0;
        for (const Segment& seg : plan) {
            const uint64_t b = fs::file_size(dir / r.name / seg.file);
            r.bytes.push_back(b);
            total += b;
            peak = std::max(peak, b);
        }
        r.bandwidth         = static_cast<int64_t>(std::ceil(static_cast<double>(peak) * 8.0 / seg_s));
        r.average_bandwidth = static_cast<int64_t>(std::ceil(static_cast<double>(total) * 8.0 /
                                                             (seg_s * static_cast<double>(plan.size()))));
        std::string m = std::format("#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:{}\n"
                                    "#EXT-X-MEDIA-SEQUENCE:0\n",
                                    static_cast<int>(std::ceil(seg_s)));
        if (kind == "ad") m += "#EXT-X-DISCONTINUITY-SEQUENCE:0\n";
        m += "#EXT-X-PLAYLIST-TYPE:VOD\n";
        for (const Segment& seg : plan) {
            if (seg.discontinuity) m += "#EXT-X-DISCONTINUITY\n";
            for (const std::string& t : seg.tags) m += t + "\n";
            m += std::format("#EXTINF:{:.6f},\n{}\n", seg_s, seg.file);
        }
        m += "#EXT-X-ENDLIST\n";
        write_text(dir / r.name / "index.m3u8", m);
    }
    if (o.nominal_bandwidth) {
        // The existing tests' values (tests/test_vmaf_ladder.cpp), highest first.
        const int64_t nominal[] = { 900000, 600000, 300000 };
        for (std::size_t i = 0; i < rungs.size(); ++i) {
            std::printf("rung %s: measured BANDWIDTH %lld, AVERAGE-BANDWIDTH %lld; written as %lld\n",
                        rungs[i].name.c_str(), static_cast<long long>(rungs[i].bandwidth),
                        static_cast<long long>(rungs[i].average_bandwidth),
                        static_cast<long long>(nominal[i]));
            rungs[i].bandwidth = rungs[i].average_bandwidth = nominal[i];
        }
    }
    for (std::size_t i = 1; i < rungs.size(); ++i)
        if (rungs[i].bandwidth >= rungs[i - 1].bandwidth)
            fail(std::format("BANDWIDTH of {} ({}) isn't below that of {} ({})", rungs[i].name,
                             rungs[i].bandwidth, rungs[i - 1].name, rungs[i - 1].bandwidth));
    std::string master = "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-INDEPENDENT-SEGMENTS\n";
    for (const RungOut& r : rungs) {
        master += std::format("#EXT-X-STREAM-INF:BANDWIDTH={},AVERAGE-BANDWIDTH={},RESOLUTION={}x{},"
                              "FRAME-RATE={:.3f}",
                              r.bandwidth, r.average_bandwidth, r.w, r.h, static_cast<double>(kFps));
        if (!r.codecs.empty()) master += std::format(",CODECS=\"{}\"", r.codecs);
        master += std::format("\n{}/index.m3u8\n", r.name);
    }
    write_text(dir / "master.m3u8", master);

    // layout.json: what each segment is, for the tables of the reports.
    std::string j = "{\n";
    j += std::format("  \"kind\": {},\n  \"codec\": {},\n  \"source\": {},\n", json_string(kind),
                     json_string(o.codec.name), json_string(o.source));
    j += std::format("  \"fps\": {},\n  \"segment_frames\": {},\n", kFps, kSegFrames);
    if (kind == "ad")
        j += std::format("  \"ts_discontinuity_indicator\": {},\n", o.ts_discontinuity ? "true" : "false");
    j += std::format("  \"x264_sei\": {},\n", json_string(x264_sei));
    j += std::format("  \"source_si_ti\": {{\"main\": {{\"si\": {:.3f}, \"ti\": {:.3f}}}", main_siti.si, main_siti.ti);
    if (kind == "ad") j += std::format(", \"ad\": {{\"si\": {:.3f}, \"ti\": {:.3f}}}", ad_siti.si, ad_siti.ti);
    j += "},\n" + layout_extra;
    j += "  \"rungs\": [\n";
    for (std::size_t i = 0; i < rungs.size(); ++i) {
        const RungOut& r = rungs[i];
        uint64_t total = 0;
        for (uint64_t b : r.bytes) total += b;
        j += std::format("    {{\"name\": {}, \"width\": {}, \"height\": {}, \"bandwidth\": {}, "
                         "\"average_bandwidth\": {}, \"codecs\": {}, \"bytes\": {}}}{}\n",
                         json_string(r.name), r.w, r.h, r.bandwidth, r.average_bandwidth,
                         json_string(r.codecs), total, i + 1 < rungs.size() ? "," : "");
    }
    j += "  ],\n  \"segments\": [\n";
    for (std::size_t s = 0; s < plan.size(); ++s) {
        const Segment& seg = plan[s];
        std::string q, bytes, tags;
        for (std::size_t r = 0; r < rungs.size(); ++r) {
            q += std::format("{}{}: {}", r ? ", " : "", json_string(rungs[r].name), seg.q.at(rungs[r].name));
            bytes += std::format("{}{}: {}", r ? ", " : "", json_string(rungs[r].name), rungs[r].bytes[s]);
        }
        for (std::size_t t = 0; t < seg.tags.size(); ++t) tags += (t ? ", " : "") + json_string(seg.tags[t]);
        j += std::format("    {{\"index\": {}, \"file\": {}, \"period\": {}, \"first_frame\": {}, "
                         "\"frames\": {}, \"position_s\": {:.3f}, \"pts_first\": {}, "
                         "\"pts_first_s\": {:.6f}, \"source_first\": {}, \"discontinuity\": {}, "
                         "\"tags\": [{}], \"q\": {{{}}}, \"bytes\": {{{}}}}}{}\n",
                         s, json_string(seg.file), json_string(seg.period), s * kSegFrames, kSegFrames,
                         static_cast<double>(s) * seg_s, seg.pts_first * kTsPerFrame,
                         static_cast<double>(seg.pts_first) / kFps, seg.source_first,
                         seg.discontinuity ? "true" : "false", tags, q, bytes,
                         s + 1 < plan.size() ? "," : "");
    }
    j += "  ]\n}\n";
    write_text(dir / "layout.json", j);

    const double total_s = seconds_since(t0);
    std::printf("%s ladder written: %zu segments of %d frames at %d fps, %zu rungs, codec %s, source %s\n",
                kind.c_str(), plan.size(), kSegFrames, kFps, rungs.size(), o.codec.name.c_str(),
                o.source.c_str());
    if (!x264_sei.empty()) std::printf("x264 SEI: %s\n", x264_sei.c_str());
    std::printf("source SI/TI (ITU-T P.910, luma at %dx%d): main SI %.3f TI %.3f", kSrcW, kSrcH,
                main_siti.si, main_siti.ti);
    if (kind == "ad") std::printf(", ad SI %.3f TI %.3f", ad_siti.si, ad_siti.ti);
    std::printf("\n");
    for (const RungOut& r : rungs) {
        uint64_t total = 0;
        for (uint64_t b : r.bytes) total += b;
        std::printf("rung %s %dx%d: %llu bytes, BANDWIDTH %lld, AVERAGE-BANDWIDTH %lld, CODECS %s\n",
                    r.name.c_str(), r.w, r.h, static_cast<unsigned long long>(total),
                    static_cast<long long>(r.bandwidth), static_cast<long long>(r.average_bandwidth),
                    r.codecs.empty() ? "(none)" : r.codecs.c_str());
        std::printf("  segment bytes:");
        for (uint64_t b : r.bytes) std::printf(" %llu", static_cast<unsigned long long>(b));
        std::printf("\n");
    }
    std::printf("wall time: %.3f s (source %.3f s, scaling, encoding and muxing %.3f s)\n", total_s,
                source_s, encode_s);
}

// The degraded ladder: 8 segments of main content; segment k of one rung at
// the low quantizer.
void make_degraded(const fs::path& dir, Options o)
{
    if (o.rung.empty()) o.rung = "v270";
    const int qn = o.q_normal.value_or(default_q_normal(o.codec));
    const int ql = o.q_low.value_or(default_q_low(o.codec));
    if (o.k < 0 || o.k >= kSegments) fail("k is out of range");
    std::vector<Segment> plan;
    for (int s = 0; s < kSegments; ++s) {
        Segment seg{ std::format("seg_{:03}.ts", s), "main", s * kSegFrames,
                     kMainOrigin + s * kSegFrames, false, {}, {} };
        for (const Rung& r : kRungs) seg.q[r.name] = (s == o.k && o.rung == r.name) ? ql : qn;
        plan.push_back(std::move(seg));
    }
    const std::string extra =
        std::format("  \"degraded\": {{\"rung\": {}, \"segment\": {}, \"q_normal\": {}, \"q_low\": {}}},\n",
                    json_string(o.rung), o.k, qn, ql);
    write_ladder(dir, "degraded", o, plan, extra);
}

// The ad ladder: main content in segments 0 to 2, the ad in 3 and 4 between
// EXT-X-DISCONTINUITY tags, with EXT-X-CUE-OUT before it, EXT-X-CUE-OUT-CONT
// before its second segment and EXT-X-CUE-IN after it, then main content in
// 5 to 7. The main content's timeline goes on through the break, as where an
// ad replaces the content of the same length: segment 5 has the content's
// frames from 5 s. The ad's timestamps start at --ad-origin seconds.
void make_ad(const fs::path& dir, Options o)
{
    if (o.rung.empty()) o.rung = "v180";
    const int qn = o.q_normal.value_or(default_q_normal(o.codec));
    const int ql = o.q_low.value_or(default_q_low(o.codec));
    const int ad_origin = static_cast<int>(std::lround(o.ad_origin_s * kFps));
    const double seg_s  = static_cast<double>(kSegFrames) / kFps;
    std::vector<Segment> plan;
    for (int s = 0; s < kSegments; ++s) {
        const bool ad = s == 3 || s == 4;
        Segment seg;
        seg.period        = ad ? "ad" : "main";
        seg.file          = ad ? std::format("ad_{:03}.ts", s - 3) : std::format("main_{:03}.ts", s);
        seg.source_first  = ad ? (s - 3) * kSegFrames : s * kSegFrames;
        seg.pts_first     = ad ? ad_origin + (s - 3) * kSegFrames : kMainOrigin + s * kSegFrames;
        seg.discontinuity = s == 3 || s == 5;
        if (s == 3) seg.tags.push_back(std::format("#EXT-X-CUE-OUT:{:.3f}", 2 * seg_s));
        if (s == 4) seg.tags.push_back(std::format("#EXT-X-CUE-OUT-CONT:{:.3f}/{:.3f}", seg_s, 2 * seg_s));
        if (s == 5) seg.tags.push_back("#EXT-X-CUE-IN");
        for (const Rung& r : kRungs) seg.q[r.name] = (ad && o.rung == r.name) ? ql : qn;
        plan.push_back(std::move(seg));
    }
    const std::string extra = std::format(
        "  \"ad\": {{\"rung\": {}, \"segments\": [3, 4], \"cue_out_segment\": 3, \"cue_in_segment\": 5, "
        "\"duration_s\": {:.3f}, \"ad_origin_pts\": {}, \"content\": {}, \"q_normal\": {}, \"q_low\": {}}},\n",
        json_string(o.rung), 2 * seg_s, ad_origin * kTsPerFrame, json_string(o.ad_content), qn, ql);
    write_ladder(dir, "ad", o, plan, extra);
}

// A lossless mezzanine of a ladder's source: FFV1 in Matroska, 200 frames from
// 10 s. --kind degraded: the main content's frames 0 to 199, at the degraded
// ladder's timestamps; --kind ad: the ad ladder's pictures in its playlist's
// order (main content's frames 0 to 74, the ad's 0 to 49, the main content's
// 125 to 199), but with timestamps that go on, as one file of the stitched
// programme would have them.
void make_mezzanine(const fs::path& file, const Options& o)
{
    const auto t0 = std::chrono::steady_clock::now();
    if (fs::exists(file)) fail(file.string() + " exists");
    if (file.has_parent_path()) fs::create_directories(file.parent_path());
    const AVCodec* codec = avcodec_find_encoder_by_name("ffv1");
    if (!codec) fail("no ffv1 encoder");
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    enc->width           = kSrcW;
    enc->height          = kSrcH;
    enc->pix_fmt         = AV_PIX_FMT_YUV420P;
    enc->time_base       = { 1, kFps };
    enc->framerate       = { kFps, 1 };
    enc->thread_count    = 1;
    enc->colorspace      = AVCOL_SPC_BT709;
    enc->color_primaries = AVCOL_PRI_BT709;
    enc->color_trc       = AVCOL_TRC_BT709;
    enc->color_range     = AVCOL_RANGE_MPEG;
    enc->flags          |= AV_CODEC_FLAG_BITEXACT;
    AVFormatContext* oc = nullptr;
    check(avformat_alloc_output_context2(&oc, nullptr, "matroska", file.c_str()), "matroska muxer");
    oc->flags |= AVFMT_FLAG_BITEXACT;
    if (oc->oformat->flags & AVFMT_GLOBALHEADER) enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    check(avcodec_open2(enc, codec, nullptr), "avcodec_open2 ffv1");
    AVStream* st = avformat_new_stream(oc, nullptr);
    check(avcodec_parameters_from_context(st->codecpar, enc), "avcodec_parameters_from_context");
    st->time_base = { 1, 1000 };
    check(avio_open(&oc->pb, file.c_str(), AVIO_FLAG_WRITE), "avio_open");
    check(avformat_write_header(oc, nullptr), "avformat_write_header");
    SiTi siti;
    const auto write_out = [&] {
        for (;;) {
            AVPacket* pkt = av_packet_alloc();
            const int r = avcodec_receive_packet(enc, pkt);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
                av_packet_free(&pkt);
                return;
            }
            check(r, "avcodec_receive_packet");
            pkt->stream_index = 0;
            av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
            check(av_interleaved_write_frame(oc, pkt), "av_interleaved_write_frame");
            av_packet_free(&pkt);
        }
    };
    for (int t = 0; t < kSegments * kSegFrames; ++t) {
        AVFrame* f = new_frame(kSrcW, kSrcH);
        if (o.mezzanine_kind == "ad" && t >= 3 * kSegFrames && t < 5 * kSegFrames) {
            if (o.ad_content == "calm") ad_frame_calm(f, t - 3 * kSegFrames);
            else ad_frame(f, t - 3 * kSegFrames);
            if (t == 3 * kSegFrames) siti.prev.clear();   // TI between consecutive frames only
        } else if (o.source == "ramp") {
            ramp_frame(f, t);
        } else {
            main_frame(f, t);
            if (o.mezzanine_kind == "ad" && t == 5 * kSegFrames) siti.prev.clear();
        }
        siti.add(f);
        f->pts = kMainOrigin + t;
        check(avcodec_send_frame(enc, f), "avcodec_send_frame");
        av_frame_free(&f);
        write_out();
    }
    check(avcodec_send_frame(enc, nullptr), "flush");
    write_out();
    check(av_write_trailer(oc), "av_write_trailer");
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    avcodec_free_context(&enc);
    std::printf("mezzanine written: FFV1 in Matroska, %dx%d, %d frames at %d fps, kind %s, source %s, "
                "%llu bytes, SI %.3f TI %.3f, wall time %.3f s\n",
                kSrcW, kSrcH, kSegments * kSegFrames, kFps, o.mezzanine_kind.c_str(), o.source.c_str(),
                static_cast<unsigned long long>(fs::file_size(file)), siti.si, siti.ti, seconds_since(t0));
}

// ── The encoders of this FFmpeg build ────────────────────────────────────────

void list_encoders()
{
    std::printf("FFmpeg %s; libavcodec %u.%u.%u, libavformat %u.%u.%u, libavutil %u.%u.%u, "
                "libswscale %u.%u.%u\n",
                av_version_info(), AV_VERSION_MAJOR(avcodec_version()), AV_VERSION_MINOR(avcodec_version()),
                AV_VERSION_MICRO(avcodec_version()), AV_VERSION_MAJOR(avformat_version()),
                AV_VERSION_MINOR(avformat_version()), AV_VERSION_MICRO(avformat_version()),
                AV_VERSION_MAJOR(avutil_version()), AV_VERSION_MINOR(avutil_version()),
                AV_VERSION_MICRO(avutil_version()), AV_VERSION_MAJOR(swscale_version()),
                AV_VERSION_MINOR(swscale_version()), AV_VERSION_MICRO(swscale_version()));
    std::printf("libavcodec's license: %s\n", avcodec_license());
    for (const char* name : { "libx264", "libx265", "libopenh264", "h264_videotoolbox", "hevc_videotoolbox",
                              "mpeg2video", "mpeg4", "mpeg1video", "libxvid", "ffv1", "rawvideo", "libvpx",
                              "libvpx-vp9", "libaom-av1", "libsvtav1", "librav1e" }) {
        const AVCodec* c = avcodec_find_encoder_by_name(name);
        if (!c) {
            std::printf("%-18s not found\n", name);
            continue;
        }
        std::printf("%-18s found: %s%s%s\n", name, c->long_name ? c->long_name : "",
                    (c->capabilities & AV_CODEC_CAP_HARDWARE) ? " [hardware]" : "",
                    (c->capabilities & AV_CODEC_CAP_EXPERIMENTAL) ? " [experimental]" : "");
    }
}

// ── A playlist read through FFmpeg's hls demuxer ────────────────────────────

int g_cc_failures = 0, g_corrupt_warnings = 0, g_warnings = 0;

void log_callback(void* avcl, int level, const char* fmt, va_list vl)
{
    if (std::strstr(fmt, "Continuity check failed")) ++g_cc_failures;
    if (std::strstr(fmt, "Packet corrupt")) ++g_corrupt_warnings;
    if (level > AV_LOG_WARNING) return;
    ++g_warnings;
    char line[1024];
    int  prefix = 1;
    av_log_format_line2(avcl, level, fmt, vl, line, sizeof line, &prefix);
    std::fprintf(stderr, "[av_log %d] %s", level, line);
}

void probe(const std::string& url)
{
    av_log_set_callback(log_callback);
    AVFormatContext* ic = nullptr;
    check(avformat_open_input(&ic, url.c_str(), nullptr, nullptr), "avformat_open_input " + url);
    check(avformat_find_stream_info(ic, nullptr), "avformat_find_stream_info");
    const AVCodec* dec_codec = nullptr;
    const int vs = check(av_find_best_stream(ic, AVMEDIA_TYPE_VIDEO, -1, -1, &dec_codec, 0), "no video");
    AVStream* st = ic->streams[vs];
    for (unsigned i = 0; i < ic->nb_streams; ++i)
        ic->streams[i]->discard = static_cast<int>(i) == vs ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    AVCodecContext* dec = avcodec_alloc_context3(dec_codec);
    check(avcodec_parameters_to_context(dec, st->codecpar), "avcodec_parameters_to_context");
    dec->thread_count = 1;
    check(avcodec_open2(dec, dec_codec, nullptr), "avcodec_open2 decoder");
    std::printf("format %s, stream %d: %s %dx%d, time base %d/%d, start_time %lld, avg_frame_rate %d/%d, "
                "r_frame_rate %d/%d\n",
                ic->iformat->name, vs, dec_codec->name, st->codecpar->width, st->codecpar->height,
                st->time_base.num, st->time_base.den, static_cast<long long>(st->start_time),
                st->avg_frame_rate.num, st->avg_frame_rate.den, st->r_frame_rate.num, st->r_frame_rate.den);
    const double tb = av_q2d(st->time_base);
    int packets = 0, frames = 0, corrupt_packets = 0, corrupt_frames = 0, decode_errors = 0;
    int64_t last_pts = AV_NOPTS_VALUE;
    std::vector<std::string> jumps;
    AVPacket* pkt = av_packet_alloc();
    AVFrame*  fr  = av_frame_alloc();
    const auto take_frames = [&] {
        for (;;) {
            const int r = avcodec_receive_frame(dec, fr);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) return;
            if (r < 0) {
                ++decode_errors;
                return;
            }
            const int64_t pts = fr->best_effort_timestamp;
            const bool corrupt = fr->flags & AV_FRAME_FLAG_CORRUPT;
            corrupt_frames += corrupt;
            std::printf("frame %3d pts %lld (%.6f s) type %c%s%s\n", frames, static_cast<long long>(pts),
                        pts * tb, av_get_picture_type_char(fr->pict_type),
                        (fr->flags & AV_FRAME_FLAG_KEY) ? " key" : "", corrupt ? " CORRUPT" : "");
            if (last_pts != AV_NOPTS_VALUE && pts != AV_NOPTS_VALUE) {
                const double step = (pts - last_pts) * tb;
                if (step < 0 || step > 1.5 / kFps)
                    jumps.push_back(std::format("before frame {}: {:+.6f} s ({:.6f} -> {:.6f})", frames, step,
                                                last_pts * tb, pts * tb));
            }
            last_pts = pts;
            ++frames;
            av_frame_unref(fr);
        }
    };
    while (av_read_frame(ic, pkt) >= 0) {
        if (pkt->stream_index == vs) {
            const bool corrupt = pkt->flags & AV_PKT_FLAG_CORRUPT;
            corrupt_packets += corrupt;
            std::printf("packet %3d pts %lld dts %lld (%.6f / %.6f s) size %d%s%s\n", packets,
                        static_cast<long long>(pkt->pts), static_cast<long long>(pkt->dts), pkt->pts * tb,
                        pkt->dts * tb, pkt->size, (pkt->flags & AV_PKT_FLAG_KEY) ? " key" : "",
                        corrupt ? " CORRUPT" : "");
            ++packets;
            if (avcodec_send_packet(dec, pkt) < 0) ++decode_errors;
            take_frames();
        }
        av_packet_unref(pkt);
    }
    avcodec_send_packet(dec, nullptr);
    take_frames();
    std::printf("summary: %d packets, %d frames, %d packets flagged corrupt, %d frames flagged corrupt, "
                "%d decode errors, %d continuity check failures logged, %d 'Packet corrupt' warnings, "
                "%d messages at warning level or above\n",
                packets, frames, corrupt_packets, corrupt_frames, decode_errors, g_cc_failures,
                g_corrupt_warnings, g_warnings);
    std::printf("timestamp jumps between consecutive frames (back, or forward by more than 1.5 frames): %zu\n",
                jumps.size());
    for (const std::string& j : jumps) std::printf("  %s\n", j.c_str());
    av_packet_free(&pkt);
    av_frame_free(&fr);
    avcodec_free_context(&dec);
    avformat_close_input(&ic);
}

// ── Arguments ────────────────────────────────────────────────────────────────

Options parse_options(int argc, char** argv, int first)
{
    Options o;
    for (int i = first; i < argc; ++i) {
        const std::string a = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= argc) fail(a + " needs a value");
            return argv[++i];
        };
        if (a == "--codec") o.codec.name = value();
        else if (a == "--source") o.source = value();
        else if (a == "--k") o.k = std::stoi(value());
        else if (a == "--rung") o.rung = value();
        else if (a == "--q-normal") o.q_normal = std::stoi(value());
        else if (a == "--q-low") o.q_low = std::stoi(value());
        else if (a == "--ad-origin") o.ad_origin_s = std::stod(value());
        else if (a == "--no-ts-discontinuity") o.ts_discontinuity = false;
        else if (a == "--nominal-bandwidth") o.nominal_bandwidth = true;
        else if (a == "--kind") o.mezzanine_kind = value();
        else if (a == "--ad-content") o.ad_content = value();
        else fail("unknown option " + a);
    }
    if (o.source != "noise" && o.source != "ramp") fail("--source is noise or ramp");
    if (o.ad_content != "busy" && o.ad_content != "calm") fail("--ad-content is calm or busy");
    if (!o.rung.empty() && o.rung != "v270" && o.rung != "v180") fail("--rung is v270 or v180");
    return o;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        const std::string cmd = argc > 1 ? argv[1] : "";
        if (cmd == "encoders") {
            list_encoders();
        } else if (cmd == "degraded" && argc > 2) {
            make_degraded(argv[2], parse_options(argc, argv, 3));
        } else if (cmd == "ad" && argc > 2) {
            make_ad(argv[2], parse_options(argc, argv, 3));
        } else if (cmd == "mezzanine" && argc > 2) {
            make_mezzanine(argv[2], parse_options(argc, argv, 3));
        } else if (cmd == "probe" && argc > 2) {
            probe(argv[2]);
        } else {
            std::fprintf(stderr, "usage: testgen encoders | degraded <dir> [options] | ad <dir> [options] | "
                                 "mezzanine <file.mkv> [options] | probe <url>\n");
            return 2;
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "testgen: %s\n", e.what());
        return 1;
    }
    return 0;
}
