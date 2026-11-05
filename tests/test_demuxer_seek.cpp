// Where a backward seek lands (Demuxer::read_loop, "Where a backward seek
// lands"), on files encoded here. An MPEG-TS has no index: FFmpeg's mpegts
// demuxer lands on a packet at or before the target that need not be a
// keyframe, and decoding from there starts at the next keyframe — after the
// target, or none before the end (plan/engineer_tools_player_fixes.md,
// section 1). The video a seek queues is decoded here by a flushed decoder,
// as the video decode thread does after the seek's flush sentinel.
//  * HEVC, two encodes joined in one MPEG-TS as in the record's clip
//    (plan/engineer_tools_runs/seek-2026-11-03/): a seek after its last
//    keyframe but one gives the frame at the target.
//  * H.264 at a long GOP, with audio, in MPEG-TS: the frame shown is the
//    target's, not the next keyframe's, and the audio starts where the seek
//    asks, each packet once.
//  * The same H.264 in MP4 and Matroska: one read, from where FFmpeg's own
//    seek lands (the container index's keyframe), as before.
//  * A keyframe KeyframeIndex knows for certain is sought directly.
//  * H.264 in MPEG-TS whose keyframes after the first are not flagged: the
//    tries go back to the start, the frame shown is the target's, and from
//    16 s back the decoder makes none (D-093). A flagged GOP of 20 s: its
//    keyframe, from 32 s back.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Demuxer.h"
#include "KeyframeIndex.h"
#include "TestTemp.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <numbers>
#include <string>
#include <thread>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/frame.h>
}

using Catch::Matchers::WithinAbs;

namespace {

constexpr int    kFps   = 25;
constexpr double kFrame = 1.0 / kFps;
constexpr int    kRate  = 48000;   // the audio's (MP2)
constexpr int    kGop   = 5 * kFps;   // the long GOP's keyframe interval

// Video of `parts` encodes, `frames` each, one after another in one file of
// `format`: every encode starts with a keyframe and has one every `gop`
// frames (0: no other), with B-frames as `bframes` says (-1: the encoder's
// default). With `audio`, a mono MP2 track as long, interleaved by time.
struct Clip {
    const char* name    = "";
    const char* format  = "mpegts";
    const char* encoder = "libx264";
    const char* params  = "";   // the encoder's own options (x264-params / x265-params)
    int  parts   = 1;
    int  frames  = 0;
    int  gop     = 0;
    int  bframes = -1;
    bool audio   = false;
    const char* bsf = "";   // the video's bitstream filters (av_bsf_list_parse_str), if any
};

// Writes `c` under testtemp::dir(); empty if its video encoder is missing.
std::string write_clip(const Clip& c)
{
    const AVCodec* vc = avcodec_find_encoder_by_name(c.encoder);
    if (!vc) return {};
    const auto path = testtemp::path(c.name);
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, c.format, path.c_str()) >= 0);
    const bool global = (oc->oformat->flags & AVFMT_GLOBALHEADER) != 0;

    std::vector<AVCodecContext*> venc;   // one per part
    for (int i = 0; i < c.parts; ++i) {
        AVCodecContext* e = avcodec_alloc_context3(vc);
        REQUIRE(e);
        e->width     = 64;
        e->height    = 64;
        e->pix_fmt   = AV_PIX_FMT_YUV420P;
        e->time_base = { 1, kFps };
        e->framerate = { kFps, 1 };
        if (c.gop > 0) {
            e->gop_size   = c.gop;
            e->keyint_min = c.gop;
        }
        if (c.bframes >= 0) e->max_b_frames = c.bframes;
        if (global) e->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        AVDictionary* o = nullptr;
        av_dict_set(&o, std::strcmp(c.encoder, "libx265") == 0 ? "x265-params" : "x264-params", c.params, 0);
        REQUIRE(avcodec_open2(e, vc, &o) >= 0);
        av_dict_free(&o);
        venc.push_back(e);
    }
    AVCodecContext* aenc = nullptr;
    if (c.audio) {
        const AVCodec* ac = avcodec_find_encoder(AV_CODEC_ID_MP2);
        REQUIRE(ac);
        aenc = avcodec_alloc_context3(ac);
        REQUIRE(aenc);
        aenc->sample_fmt  = AV_SAMPLE_FMT_S16;
        aenc->sample_rate = kRate;
        av_channel_layout_default(&aenc->ch_layout, 1);
        aenc->bit_rate    = 64000;
        aenc->time_base   = { 1, kRate };
        if (global) aenc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        REQUIRE(avcodec_open2(aenc, ac, nullptr) >= 0);
    }
    AVStream* vs = avformat_new_stream(oc, nullptr);
    REQUIRE(avcodec_parameters_from_context(vs->codecpar, venc[0]) >= 0);
    vs->time_base = venc[0]->time_base;
    AVStream* as = nullptr;
    if (aenc) {
        as = avformat_new_stream(oc, nullptr);
        REQUIRE(avcodec_parameters_from_context(as->codecpar, aenc) >= 0);
        as->time_base = aenc->time_base;
    }
    REQUIRE(avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);

    AVPacket* pkt  = av_packet_alloc();
    AVPacket* apkt = av_packet_alloc();   // the audio's: written while a video packet is in hand
    AVFrame*  af   = nullptr;
    std::int64_t samples = 0;   // audio written so far
    const std::int64_t total = static_cast<std::int64_t>(c.parts) * c.frames * kRate / kFps;
    if (aenc) {
        af = av_frame_alloc();
        af->format      = aenc->sample_fmt;
        af->nb_samples  = aenc->frame_size;
        af->sample_rate = kRate;
        REQUIRE(av_channel_layout_copy(&af->ch_layout, &aenc->ch_layout) >= 0);
        REQUIRE(av_frame_get_buffer(af, 0) >= 0);
    }
    auto write_audio = [&](const AVFrame* f) {
        REQUIRE(avcodec_send_frame(aenc, f) >= 0);
        while (avcodec_receive_packet(aenc, apkt) == 0) {
            av_packet_rescale_ts(apkt, aenc->time_base, as->time_base);
            apkt->stream_index = as->index;
            REQUIRE(av_interleaved_write_frame(oc, apkt) >= 0);
        }
    };
    // The audio up to `t` seconds: a tone.
    auto audio_until = [&](double t) {
        while (aenc && samples < total && static_cast<double>(samples) / kRate < t) {
            REQUIRE(av_frame_make_writable(af) >= 0);
            auto* s = reinterpret_cast<std::int16_t*>(af->data[0]);
            for (int i = 0; i < af->nb_samples; ++i)
                s[i] = static_cast<std::int16_t>(
                    8000.0 * std::sin(2.0 * std::numbers::pi * 440.0 * static_cast<double>(samples + i) / kRate));
            af->pts  = samples;
            samples += af->nb_samples;
            write_audio(af);
        }
    };
    AVBSFContext* bsf = nullptr;
    if (*c.bsf) {
        REQUIRE(av_bsf_list_parse_str(c.bsf, &bsf) >= 0);
        REQUIRE(avcodec_parameters_from_context(bsf->par_in, venc[0]) >= 0);
        bsf->time_base_in = venc[0]->time_base;
        REQUIRE(av_bsf_init(bsf) >= 0);
    }
    auto put_video = [&](AVCodecContext* e, AVPacket* p) {   // p: in e's time base
        av_packet_rescale_ts(p, e->time_base, vs->time_base);
        p->stream_index = vs->index;
        REQUIRE(av_interleaved_write_frame(oc, p) >= 0);
    };
    auto write_video = [&](AVCodecContext* e, const AVFrame* f) {
        REQUIRE(avcodec_send_frame(e, f) >= 0);
        while (avcodec_receive_packet(e, pkt) == 0) {
            audio_until(static_cast<double>(pkt->dts) / kFps + 0.1);
            if (!bsf) {
                put_video(e, pkt);
                continue;
            }
            REQUIRE(av_bsf_send_packet(bsf, pkt) >= 0);
            while (av_bsf_receive_packet(bsf, pkt) == 0) put_video(e, pkt);
        }
    };
    AVFrame* f = av_frame_alloc();
    f->format = AV_PIX_FMT_YUV420P;
    f->width  = 64;
    f->height = 64;
    REQUIRE(av_frame_get_buffer(f, 0) >= 0);
    for (int part = 0; part < c.parts; ++part) {
        for (int i = 0; i < c.frames; ++i) {
            const int n = part * c.frames + i;
            REQUIRE(av_frame_make_writable(f) >= 0);
            for (int y = 0; y < f->height; ++y)
                for (int x = 0; x < f->width; ++x)
                    f->data[0][y * f->linesize[0] + x] = static_cast<std::uint8_t>((x * 2 + y * 3 + n * 5) & 0xFF);
            for (int p = 1; p < 3; ++p)
                for (int y = 0; y < f->height / 2; ++y)
                    std::memset(f->data[p] + y * f->linesize[p], 128 + (n & 15), static_cast<std::size_t>(f->width / 2));
            f->pts = n;
            write_video(venc[static_cast<std::size_t>(part)], f);
        }
        write_video(venc[static_cast<std::size_t>(part)], nullptr);   // this encode ends here
    }
    if (bsf) {
        REQUIRE(av_bsf_send_packet(bsf, nullptr) >= 0);
        while (av_bsf_receive_packet(bsf, pkt) == 0) put_video(venc.back(), pkt);
        av_bsf_free(&bsf);
    }
    if (aenc) {
        audio_until(1e9);
        write_audio(nullptr);
    }
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    av_frame_free(&f);
    av_frame_free(&af);
    av_packet_free(&pkt);
    av_packet_free(&apkt);
    for (AVCodecContext*& e : venc) avcodec_free_context(&e);
    avcodec_free_context(&aenc);
    return path.string();
}

// The video keyframes of `path` (pts, seconds), reading it from the start.
std::vector<double> keyframes_of(const std::string& path)
{
    AVFormatContext* fc = nullptr;
    REQUIRE(avformat_open_input(&fc, path.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(avformat_find_stream_info(fc, nullptr) >= 0);
    const int v = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    REQUIRE(v >= 0);
    std::vector<double> out;
    AVPacket* p = av_packet_alloc();
    while (av_read_frame(fc, p) >= 0) {
        if (p->stream_index == v && (p->flags & AV_PKT_FLAG_KEY) && p->pts != AV_NOPTS_VALUE)
            out.push_back(p->pts * av_q2d(fc->streams[v]->time_base));
        av_packet_unref(p);
    }
    av_packet_free(&p);
    avformat_close_input(&fc);
    std::sort(out.begin(), out.end());
    return out;
}

// The first video packet after FFmpeg's own backward seek to t (seconds),
// asked for as read_loop asks: pts (seconds) and whether it is a keyframe.
struct Landed {
    double pts = -1.0;
    bool   key = false;
};
Landed ffmpeg_lands(const std::string& path, double t)
{
    AVFormatContext* fc = nullptr;
    REQUIRE(avformat_open_input(&fc, path.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(avformat_find_stream_info(fc, nullptr) >= 0);
    const int v = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    REQUIRE(v >= 0);
    REQUIRE(av_seek_frame(fc, -1, static_cast<std::int64_t>(t * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD) >= 0);
    Landed out;
    AVPacket* p = av_packet_alloc();
    while (av_read_frame(fc, p) >= 0) {
        const bool video = p->stream_index == v;
        if (video) {
            out.pts = (p->pts != AV_NOPTS_VALUE ? p->pts : p->dts) * av_q2d(fc->streams[v]->time_base);
            out.key = (p->flags & AV_PKT_FLAG_KEY) != 0;
        }
        av_packet_unref(p);
        if (video) break;
    }
    av_packet_free(&p);
    avformat_close_input(&fc);
    return out;
}

template <typename Pred>
bool wait_for(Pred pred, double secs = 10.0)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::duration<double>(secs);
    while (std::chrono::steady_clock::now() < until) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

void free_queued(Queue<AVPacket*>& q)
{
    AVPacket* p = nullptr;
    while (q.try_pop(p))
        if (p && p != flush_sentinel()) av_packet_free(&p);
}

// A Demuxer reading into queues the whole file fits in: read_loop never
// waits for room, reads to the end and idles there, and each seek's packets
// are all queued (no decoder runs).
struct Reader {
    Demuxer          d;
    Queue<AVPacket*> videoq{ 16384 }, audioq{ 16384 };
    std::jthread     th;

    // After open(): read the file once through, and drop what that queued.
    void start(KeyframeIndex* index = nullptr)
    {
        if (index) d.set_keyframe_index(index);
        th = std::jthread([this](std::stop_token st) { d.read_loop(st, videoq, audioq, nullptr); });
        REQUIRE(wait_for([&] { return d.exhausted(); }));
        free_queued(videoq);
        free_queued(audioq);
    }
    ~Reader()
    {
        if (th.joinable()) {
            th.request_stop();
            videoq.shutdown();
            audioq.shutdown();
            th.join();
        }
        free_queued(videoq);
        free_queued(audioq);
    }
};

// What one seek queues, requested as the player requests it (show_from: the
// target): the packets after its flush sentinels, up to the end markers.
struct Seeked {
    std::vector<AVPacket*> video, audio;
    Seeked() = default;
    Seeked(const Seeked&) = delete;
    Seeked& operator=(const Seeked&) = delete;
    ~Seeked()
    {
        for (std::vector<AVPacket*>* v : { &video, &audio })
            for (AVPacket*& p : *v) av_packet_free(&p);
    }
};
void seek(Reader& r, double t, Seeked& out)
{
    r.d.request_seek(t, false, t);
    const bool audio = r.d.audio_stream_index() >= 0;
    bool v_flushed = false, v_end = false, a_flushed = !audio, a_end = !audio;
    auto take = [](Queue<AVPacket*>& q, bool& flushed, bool& end, std::vector<AVPacket*>& to) {
        AVPacket* p = nullptr;
        while (!end && q.try_pop(p)) {
            if (p == flush_sentinel()) flushed = true;
            else if (p == nullptr)     end = flushed;   // before the sentinel: the last end's marker
            else if (flushed)          to.push_back(p);
            else                       av_packet_free(&p);
        }
    };
    REQUIRE(wait_for([&] {
        take(r.videoq, v_flushed, v_end, out.video);
        take(r.audioq, a_flushed, a_end, out.audio);
        return v_end && a_end;
    }, 30.0));
}

double pts_s(const Demuxer& d, const AVPacket* p)
{
    return p->pts * av_q2d(d.fmt_ctx()->streams[p->stream_index]->time_base);
}

// The frames a flushed decoder makes of `pkts`, as the video decode thread
// does after a seek's flush sentinel: their pts (seconds), in display
// order. A packet whose references are missing gives none.
std::vector<double> decode(const Demuxer& d, const std::vector<AVPacket*>& pkts)
{
    const AVCodecParameters* par = d.video_codecpar();
    const AVCodec* dec = avcodec_find_decoder(par->codec_id);
    REQUIRE(dec);
    AVCodecContext* c = avcodec_alloc_context3(dec);
    REQUIRE(c);
    REQUIRE(avcodec_parameters_to_context(c, par) >= 0);
    c->pkt_timebase = d.video_time_base();
    c->thread_count = 1;
    REQUIRE(avcodec_open2(c, dec, nullptr) >= 0);
    AVFrame* f = av_frame_alloc();
    std::vector<double> out;
    auto receive = [&] {
        while (avcodec_receive_frame(c, f) == 0) {
            if (f->best_effort_timestamp != AV_NOPTS_VALUE)
                out.push_back(f->best_effort_timestamp * av_q2d(c->pkt_timebase));
            av_frame_unref(f);
        }
    };
    for (const AVPacket* p : pkts) {
        (void)avcodec_send_packet(c, p);
        receive();
    }
    (void)avcodec_send_packet(c, nullptr);
    receive();
    av_frame_free(&f);
    avcodec_free_context(&c);
    return out;
}

// The frame the video decode thread shows after a seek with show_from t:
// the first one not before it (half a frame of tolerance); -1 if none.
double shown(const std::vector<double>& frames, double t)
{
    for (const double p : frames)
        if (p + 0.5 * kFrame >= t) return p;
    return -1.0;
}

// Reads of the file since run `before` was taken: the Demuxer starts a
// KeyframeIndex run (new_run) for each, every try of a seek's landing one.
std::uint32_t reads_since(KeyframeIndex& idx, std::uint32_t before)
{
    return idx.new_run() - before - 1;
}

} // namespace

TEST_CASE("Demuxer: a seek past the last keyframe but one of a joined HEVC MPEG-TS gives the frame at the target",
          "[demux][seek]")
{
    // As the record's clip: two encodes of 3 s, a keyframe at the start of each.
    const std::string path = write_clip({ .name = "mirage_test_seek_joined.ts", .format = "mpegts",
                                          .encoder = "libx265", .params = "log-level=error:scenecut=0",
                                          .parts = 2, .frames = 3 * kFps });
    if (path.empty()) SKIP("no HEVC encoder (libx265)");
    const std::vector<double> kfs = keyframes_of(path);
    REQUIRE(kfs.size() == 2);
    Reader r;
    REQUIRE(r.d.open(path));
    r.start();

    // Frames after the first keyframe, in its GOP and in the last one: the
    // second keyframe itself (a keyframe step's target) and the last frame.
    for (const int frame : { 25, 72, 75, 80, 130, 3 * kFps * 2 - 1 }) {
        const double t = kfs[0] + frame * kFrame;
        CAPTURE(frame, t);
        Seeked s;
        seek(r, t, s);
        // The first packet queued is the keyframe at or before the target.
        REQUIRE_FALSE(s.video.empty());
        CHECK((s.video.front()->flags & AV_PKT_FLAG_KEY) != 0);
        CHECK_THAT(pts_s(r.d, s.video.front()), WithinAbs(frame < 3 * kFps ? kfs[0] : kfs[1], 1e-6));
        // Decoded, a frame at or before the target comes out, and the one
        // shown is the target's.
        const std::vector<double> frames = decode(r.d, s.video);
        REQUIRE_FALSE(frames.empty());
        CHECK(frames.front() <= t + 1e-6);
        CHECK_THAT(shown(frames, t), WithinAbs(t, 1e-6));
    }
}

TEST_CASE("Demuxer: a seek into a long GOP of an H.264 MPEG-TS shows the target's frame, with its audio",
          "[demux][seek]")
{
    const std::string path = write_clip({ .name = "mirage_test_seek_longgop.ts", .format = "mpegts",
                                          .encoder = "libx264", .params = "scenecut=0:b-adapt=0",
                                          .frames = 3 * kGop, .gop = kGop, .bframes = 2, .audio = true });
    if (path.empty()) SKIP("no H.264 encoder (libx264)");
    const std::vector<double> kfs = keyframes_of(path);
    REQUIRE(kfs.size() == 3);
    Reader r;
    REQUIRE(r.d.open(path));
    REQUIRE(r.d.audio_stream_index() >= 0);
    r.start();
    const AVCodecParameters* apar = r.d.audio_codecpar();
    // The audio queued starts this long before show_from (Demuxer::audio_preroll_s).
    const double preroll = apar->frame_size > 0 && apar->sample_rate > 0
                         ? 3.0 * apar->frame_size / apar->sample_rate : 0.064;

    // Inside each GOP, two seconds or more after its keyframe.
    for (const int frame : { 2 * kFps, kGop + 2 * kFps, 2 * kGop + 90 }) {
        const double      t = kfs[0] + frame * kFrame;
        const std::size_t g = static_cast<std::size_t>(frame / kGop);   // the GOP holding the target
        CAPTURE(frame, t);
        Seeked s;
        seek(r, t, s);
        REQUIRE_FALSE(s.video.empty());
        CHECK((s.video.front()->flags & AV_PKT_FLAG_KEY) != 0);
        CHECK_THAT(pts_s(r.d, s.video.front()), WithinAbs(kfs[g], 1e-6));
        // The target's frame, not the next keyframe's.
        CHECK_THAT(shown(decode(r.d, s.video), t), WithinAbs(t, 1e-6));
        // The audio from the packet holding the warm-up's start on, one
        // after another to the end: none twice, none of the tries before.
        REQUIRE(s.audio.size() > 1);
        const double a0   = pts_s(r.d, s.audio.front());
        const double step = s.audio.front()->duration * av_q2d(r.d.audio_time_base());
        CHECK(a0 <= t - preroll + 1e-6);
        CHECK(a0 + step > t - preroll);
        std::size_t gaps = 0;
        for (std::size_t i = 1; i < s.audio.size(); ++i)
            gaps += std::abs(pts_s(r.d, s.audio[i]) - pts_s(r.d, s.audio[i - 1]) - step) > 1e-6;
        CHECK(gaps == 0);
    }
}

TEST_CASE("Demuxer: a seek into MP4 or Matroska reads once, from where FFmpeg's own seek lands", "[demux][seek]")
{
    for (const auto& [format, name] : { std::pair{ "mp4", "mirage_test_seek_longgop.mp4" },
                                        std::pair{ "matroska", "mirage_test_seek_longgop.mkv" } }) {
        CAPTURE(format);
        const std::string path = write_clip({ .name = name, .format = format, .encoder = "libx264",
                                              .params = "scenecut=0:b-adapt=0", .frames = 3 * kGop,
                                              .gop = kGop, .bframes = 2 });
        if (path.empty()) SKIP("no H.264 encoder (libx264)");
        const std::vector<double> kfs = keyframes_of(path);
        REQUIRE(kfs.size() == 3);
        const double t = kfs[0] + (kGop + 2 * kFps) * kFrame;
        // FFmpeg's own backward seek lands on the index's keyframe before it.
        const Landed own = ffmpeg_lands(path, t);
        REQUIRE(own.key);
        REQUIRE_THAT(own.pts, WithinAbs(kfs[1], 1e-6));

        Reader r;
        REQUIRE(r.d.open(path));
        // The stream played from the open: its keyframes are the container
        // index's (not tracked), as in playback. The index counts the reads.
        KeyframeIndex idx;
        idx.set_stream(r.d.video_stream_index(), kfs[0], kFrame, /*tracked*/ false);
        r.start(&idx);
        const std::uint32_t before = idx.new_run();
        Seeked s;
        seek(r, t, s);
        CHECK(reads_since(idx, before) == 1);   // no second try
        REQUIRE_FALSE(s.video.empty());
        CHECK_THAT(pts_s(r.d, s.video.front()), WithinAbs(own.pts, 1e-6));
        CHECK_THAT(shown(decode(r.d, s.video), t), WithinAbs(t, 1e-6));
    }
}

TEST_CASE("Demuxer: a keyframe KeyframeIndex knows for certain is sought directly", "[demux][seek][keyframes]")
{
    // No B-frames: a keyframe's dts is its pts, and a seek to it lands on it.
    const std::string path = write_clip({ .name = "mirage_test_seek_known.ts", .format = "mpegts",
                                          .encoder = "libx264", .params = "scenecut=0",
                                          .frames = 3 * kGop, .gop = kGop, .bframes = 0 });
    if (path.empty()) SKIP("no H.264 encoder (libx264)");
    const std::vector<double> kfs = keyframes_of(path);
    REQUIRE(kfs.size() == 3);
    const double t = kfs[0] + (kGop + 62) * kFrame;

    for (const bool tracked : { true, false }) {
        CAPTURE(tracked);
        Reader r;
        REQUIRE(r.d.open(path));
        KeyframeIndex idx;
        idx.set_stream(r.d.video_stream_index(), kfs[0], kFrame, tracked);
        r.start(&idx);   // read once through: tracked, every keyframe is known
        const std::uint32_t before = idx.new_run();
        Seeked s;
        seek(r, t, s);
        if (tracked) CHECK(reads_since(idx, before) == 1);   // straight to the keyframe
        else         CHECK(reads_since(idx, before) > 1);    // to the target, then from further back
        REQUIRE_FALSE(s.video.empty());
        CHECK_THAT(pts_s(r.d, s.video.front()), WithinAbs(kfs[1], 1e-6));
        CHECK_THAT(shown(decode(r.d, s.video), t), WithinAbs(t, 1e-6));
    }
}

TEST_CASE("Demuxer: a seek into an MPEG-TS whose keyframes after the first are not flagged reads from the start",
          "[demux][seek]")
{
    SECTION("open GOPs without SEI: the target's frame, decoded from the start") {
        // Every SEI taken out of open GOPs: their I-frames are neither IDR
        // nor marked by a recovery point, so FFmpeg's parser flags only the
        // first frame as a keyframe. The tries find none at or before the
        // target and go back to the media's start (D-093).
        const std::string path = write_clip({ .name = "mirage_test_seek_unflagged.ts", .format = "mpegts",
                                              .encoder = "libx264", .params = "scenecut=0:open-gop=1",
                                              .frames = 30 * kFps, .gop = 2 * kFps, .bframes = 2,
                                              .bsf = "filter_units=remove_types=6" });
        if (path.empty()) SKIP("no H.264 encoder (libx264)");
        const std::vector<double> kfs = keyframes_of(path);
        REQUIRE(kfs.size() == 1);
        const double t = kfs[0] + 25.0;
        Reader r;
        REQUIRE(r.d.open(path));
        KeyframeIndex idx;
        idx.set_stream(r.d.video_stream_index(), kfs[0], kFrame, /*tracked*/ false);
        r.start(&idx);
        const std::uint32_t before = idx.new_run();
        Seeked s;
        seek(r, t, s);
        // The first try, the ones 1, 2, 4, 8 and 16 s back, and the start.
        CHECK(reads_since(idx, before) == 7);
        REQUIRE_FALSE(s.video.empty());
        CHECK_THAT(pts_s(r.d, s.video.front()), WithinAbs(kfs[0], 1e-6));
        CHECK_THAT(shown(decode(r.d, s.video), t), WithinAbs(t, 1e-6));
        // Why from the start: from 16 s back, where its I-frames are, the
        // decoder (FFmpeg 8.1.1's H.264, with x264's default three
        // references) makes no frame at all. It starts at an IDR or a
        // recovery point, and this stream has one IDR, its first frame.
        const auto from16 = std::find_if(s.video.begin(), s.video.end(), [&](const AVPacket* p) {
            return p->dts != AV_NOPTS_VALUE && p->dts * av_q2d(r.d.video_time_base()) >= t - 16.0;
        });
        REQUIRE(from16 != s.video.end());
        CHECK(decode(r.d, std::vector<AVPacket*>(from16, s.video.end())).empty());
    }
    SECTION("a flagged GOP longer than 16 s: its keyframe, from 32 s back") {
        constexpr int kLong = 20 * kFps;
        const std::string path = write_clip({ .name = "mirage_test_seek_gop20.ts", .format = "mpegts",
                                              .encoder = "libx264", .params = "scenecut=0:b-adapt=0",
                                              .frames = 2 * kLong + 5 * kFps, .gop = kLong, .bframes = 2 });
        if (path.empty()) SKIP("no H.264 encoder (libx264)");
        const std::vector<double> kfs = keyframes_of(path);
        REQUIRE(kfs.size() == 3);
        const double t = kfs[1] + 18.0;   // 18 s after the keyframe before it
        Reader r;
        REQUIRE(r.d.open(path));
        r.start();
        Seeked s;
        seek(r, t, s);
        REQUIRE_FALSE(s.video.empty());
        CHECK_THAT(pts_s(r.d, s.video.front()), WithinAbs(kfs[1], 1e-6));
        CHECK_THAT(shown(decode(r.d, s.video), t), WithinAbs(t, 1e-6));
    }
}
