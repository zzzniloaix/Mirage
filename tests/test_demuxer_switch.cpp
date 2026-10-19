// Track switching in Demuxer::read_loop on a generated local file with two
// video and two audio streams (rawvideo + PCM: every packet is a keyframe,
// no encoder needed). Checks the seamless take-over contract the decode
// threads rely on: what stays queued from the old track, where the new one
// starts, and that tracks which keep playing see no duplicated packets after
// the container is repositioned.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Demuxer.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/mem.h>
}

namespace {

constexpr int    kFps        = 25;
constexpr double kSecs       = 40.0;   // long enough that read_loop never hits EOF mid-test
constexpr int    kRate       = 8000;
constexpr int    kAudioFrame = 400;   // 50 ms PCM packets

// v0: 32x18, v1: 16x8 (stream 0/1), a0/a1: mono PCM (stream 2/3).
std::string make_multitrack_file()
{
    const auto path = std::filesystem::temp_directory_path() / "mirage_test_multitrack.nut";
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "nut", path.c_str()) >= 0);
    const int vw[2] = { 32, 16 }, vh[2] = { 18, 8 };
    for (int i = 0; i < 2; ++i) {
        AVStream* st = avformat_new_stream(oc, nullptr);
        st->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
        st->codecpar->codec_id   = AV_CODEC_ID_RAWVIDEO;
        st->codecpar->format     = AV_PIX_FMT_GRAY8;
        st->codecpar->width      = vw[i];
        st->codecpar->height     = vh[i];
        st->time_base            = { 1, kFps };
        st->avg_frame_rate       = { kFps, 1 };
    }
    for (int i = 0; i < 2; ++i) {
        AVStream* st = avformat_new_stream(oc, nullptr);
        st->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
        st->codecpar->codec_id    = AV_CODEC_ID_PCM_S16LE;
        st->codecpar->sample_rate = kRate;
        st->codecpar->format      = AV_SAMPLE_FMT_S16;
        av_channel_layout_default(&st->codecpar->ch_layout, 1);
        st->time_base             = { 1, kRate };
    }
    REQUIRE(avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);

    const int vframes = static_cast<int>(kSecs * kFps);
    const int aframes = static_cast<int>(kSecs * kRate / kAudioFrame);
    int vi = 0, ai = 0;
    while (vi < vframes || ai < aframes) {
        // Interleave by time.
        const double tv = static_cast<double>(vi) / kFps;
        const double ta = static_cast<double>(ai) * kAudioFrame / kRate;
        AVPacket* pkt = av_packet_alloc();
        if (vi < vframes && (ai >= aframes || tv <= ta)) {
            for (int s = 0; s < 2; ++s) {
                REQUIRE(av_new_packet(pkt, vw[s] * vh[s]) >= 0);
                std::fill(pkt->data, pkt->data + pkt->size, static_cast<uint8_t>(vi));
                pkt->stream_index = s;
                pkt->pts = pkt->dts = vi;
                pkt->duration = 1;
                pkt->flags |= AV_PKT_FLAG_KEY;
                av_packet_rescale_ts(pkt, { 1, kFps }, oc->streams[s]->time_base);
                REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
            }
            ++vi;
        } else {
            for (int s = 2; s < 4; ++s) {
                REQUIRE(av_new_packet(pkt, kAudioFrame * 2) >= 0);
                std::fill(pkt->data, pkt->data + pkt->size, static_cast<uint8_t>(s));
                pkt->stream_index = s;
                pkt->pts = pkt->dts = static_cast<int64_t>(ai) * kAudioFrame;
                pkt->duration = kAudioFrame;
                pkt->flags |= AV_PKT_FLAG_KEY;
                av_packet_rescale_ts(pkt, { 1, kRate }, oc->streams[s]->time_base);
                REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
            }
            ++ai;
        }
        av_packet_free(&pkt);
    }
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    return path.string();
}

// v: 16x8 rawvideo (stream 0), a0/a1: mono PCM (stream 1/2), s: a text
// cue "cue N" from N to N+0.5 s every second (stream 3).
std::string make_subtitle_file()
{
    const auto path = std::filesystem::temp_directory_path() / "mirage_test_subtitles.nut";
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "nut", path.c_str()) >= 0);
    AVStream* vs = avformat_new_stream(oc, nullptr);
    vs->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    vs->codecpar->codec_id   = AV_CODEC_ID_RAWVIDEO;
    vs->codecpar->format     = AV_PIX_FMT_GRAY8;
    vs->codecpar->width      = 16;
    vs->codecpar->height     = 8;
    vs->time_base            = { 1, kFps };
    vs->avg_frame_rate       = { kFps, 1 };
    AVStream* as[2] = {};
    for (AVStream*& a : as) {
        a = avformat_new_stream(oc, nullptr);
        a->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
        a->codecpar->codec_id    = AV_CODEC_ID_PCM_S16LE;
        a->codecpar->sample_rate = kRate;
        a->codecpar->format      = AV_SAMPLE_FMT_S16;
        av_channel_layout_default(&a->codecpar->ch_layout, 1);
        a->time_base             = { 1, kRate };
    }
    AVStream* ss = avformat_new_stream(oc, nullptr);
    ss->codecpar->codec_type = AVMEDIA_TYPE_SUBTITLE;
    ss->codecpar->codec_id   = AV_CODEC_ID_TEXT;
    ss->time_base            = { 1, 1000 };
    REQUIRE(avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);

    const int vframes = static_cast<int>(kSecs * kFps);
    const int aframes = static_cast<int>(kSecs * kRate / kAudioFrame);
    const int cues    = static_cast<int>(kSecs);
    int vi = 0, ai = 0, ci = 0;
    while (vi < vframes || ai < aframes || ci < cues) {
        const double tv = vi < vframes ? static_cast<double>(vi) / kFps : 1e9;
        const double ta = ai < aframes ? static_cast<double>(ai) * kAudioFrame / kRate : 1e9;
        const double tc = ci < cues ? static_cast<double>(ci) : 1e9;
        AVPacket* pkt = av_packet_alloc();
        if (tc <= tv && tc <= ta) {
            const std::string text = "cue " + std::to_string(ci);
            REQUIRE(av_new_packet(pkt, static_cast<int>(text.size())) >= 0);
            std::copy(text.begin(), text.end(), pkt->data);
            pkt->stream_index = 3;
            pkt->pts = pkt->dts = static_cast<int64_t>(ci) * 1000;
            pkt->duration = 500;
            av_packet_rescale_ts(pkt, { 1, 1000 }, ss->time_base);   // the muxer's own
            ++ci;
        } else if (tv <= ta) {
            REQUIRE(av_new_packet(pkt, 16 * 8) >= 0);
            std::fill(pkt->data, pkt->data + pkt->size, static_cast<uint8_t>(vi));
            pkt->stream_index = 0;
            pkt->pts = pkt->dts = vi;
            pkt->duration = 1;
            av_packet_rescale_ts(pkt, { 1, kFps }, vs->time_base);
            ++vi;
        } else {
            for (int k = 0; k < 2; ++k) {
                REQUIRE(av_new_packet(pkt, kAudioFrame * 2) >= 0);
                std::fill(pkt->data, pkt->data + pkt->size, static_cast<uint8_t>(k + 1));
                pkt->stream_index = 1 + k;
                pkt->pts = pkt->dts = static_cast<int64_t>(ai) * kAudioFrame;
                pkt->duration = kAudioFrame;
                pkt->flags |= AV_PKT_FLAG_KEY;
                av_packet_rescale_ts(pkt, { 1, kRate }, as[k]->time_base);
                REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
            }
            ++ai;
            av_packet_free(&pkt);
            continue;
        }
        pkt->flags |= AV_PKT_FLAG_KEY;
        REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
        av_packet_free(&pkt);
    }
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    return path.string();
}

// Matroska with H.264 video (B-frames, a keyframe every 10 frames) and two
// mono PCM tracks: the keyframe a seek lands on comes without a dts, as in
// any H.264 .mkv. 20 s; v: stream 0, a0/a1: streams 1/2. Empty if there is
// no H.264 encoder (libx264).
std::string make_bframes_mkv()
{
    constexpr int kVidSecs = 20;
    const auto path = std::filesystem::temp_directory_path() / "mirage_test_bframes.mkv";
    const AVCodec* codec = avcodec_find_encoder_by_name("libx264");
    if (!codec) return {};
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    enc->width        = 64;
    enc->height       = 48;
    enc->pix_fmt      = AV_PIX_FMT_YUV420P;
    enc->time_base    = { 1, kFps };
    enc->framerate    = { kFps, 1 };
    enc->gop_size     = 10;
    enc->keyint_min   = 10;
    enc->max_b_frames = 2;
    enc->flags       |= AV_CODEC_FLAG_GLOBAL_HEADER;   // SPS/PPS in the track header
    AVDictionary* eo = nullptr;
    av_dict_set(&eo, "preset", "veryfast", 0);
    av_dict_set(&eo, "x264-params", "scenecut=0:bframes=2:b-adapt=0", 0);
    REQUIRE(avcodec_open2(enc, codec, &eo) >= 0);
    av_dict_free(&eo);

    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "matroska", path.c_str()) >= 0);
    AVStream* vs = avformat_new_stream(oc, nullptr);
    REQUIRE(avcodec_parameters_from_context(vs->codecpar, enc) >= 0);
    vs->time_base = enc->time_base;
    AVStream* as[2] = {};
    for (AVStream*& a : as) {
        a = avformat_new_stream(oc, nullptr);
        a->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
        a->codecpar->codec_id    = AV_CODEC_ID_PCM_S16LE;
        a->codecpar->sample_rate = kRate;
        a->codecpar->format      = AV_SAMPLE_FMT_S16;
        av_channel_layout_default(&a->codecpar->ch_layout, 1);
        a->time_base             = { 1, kRate };
    }
    REQUIRE(avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);

    AVPacket* pkt = av_packet_alloc();
    AVPacket* apkt = av_packet_alloc();
    int ai = 0;
    // Audio up to `t` seconds, both tracks.
    auto write_audio = [&](double t) {
        while (static_cast<double>(ai) * kAudioFrame / kRate < t && ai < kVidSecs * kRate / kAudioFrame) {
            for (int k = 0; k < 2; ++k) {
                REQUIRE(av_new_packet(apkt, kAudioFrame * 2) >= 0);
                std::memset(apkt->data, k + 1, static_cast<size_t>(apkt->size));
                apkt->stream_index = 1 + k;
                apkt->pts = apkt->dts = static_cast<int64_t>(ai) * kAudioFrame;
                apkt->duration = kAudioFrame;
                apkt->flags |= AV_PKT_FLAG_KEY;
                av_packet_rescale_ts(apkt, { 1, kRate }, as[k]->time_base);
                REQUIRE(av_interleaved_write_frame(oc, apkt) >= 0);
            }
            ++ai;
        }
    };
    auto drain_encoder = [&](AVFrame* f) {
        REQUIRE(avcodec_send_frame(enc, f) >= 0);
        while (avcodec_receive_packet(enc, pkt) == 0) {
            write_audio(static_cast<double>(pkt->dts) / kFps + 0.1);
            av_packet_rescale_ts(pkt, enc->time_base, vs->time_base);
            pkt->stream_index = 0;
            REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
        }
    };
    AVFrame* f = av_frame_alloc();
    f->format = enc->pix_fmt;
    f->width  = enc->width;
    f->height = enc->height;
    REQUIRE(av_frame_get_buffer(f, 0) >= 0);
    for (int i = 0; i < kVidSecs * kFps; ++i) {
        REQUIRE(av_frame_make_writable(f) >= 0);
        for (int y = 0; y < f->height; ++y)
            std::memset(f->data[0] + y * f->linesize[0], (i * 7 + y * 3) & 0xFF, static_cast<size_t>(f->width));
        for (int p = 1; p < 3; ++p)
            for (int y = 0; y < f->height / 2; ++y)
                std::memset(f->data[p] + y * f->linesize[p], 128, static_cast<size_t>(f->width / 2));
        f->pts = i;
        drain_encoder(f);
    }
    drain_encoder(nullptr);
    write_audio(kVidSecs + 1.0);
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    av_frame_free(&f);
    av_packet_free(&pkt);
    av_packet_free(&apkt);
    avcodec_free_context(&enc);
    return path.string();
}

// A packet as the decode threads see it; sentinel = flush sentinel.
struct Seen {
    bool   sentinel = false;
    int    stream   = -1;
    double pts      = 0.0;   // seconds
};

// Pops one queue on its own thread, recording what arrives.
struct Consumer {
    Queue<AVPacket*>&  q;
    const Demuxer&     d;
    std::mutex         mtx;
    std::vector<Seen>  seen;
    std::atomic<bool>  hold{ false };   // pause popping (the demuxer then fills the queue and blocks)
    std::jthread       th;

    // pace: per-packet delay — ~10x real time, so the file lasts long
    // enough for the switches and the demuxer reads ahead as in playback.
    // held: start paused (before the first packet).
    Consumer(Queue<AVPacket*>& q_, const Demuxer& d_, std::chrono::microseconds pace, bool held = false)
        : q(q_), d(d_)
    {
        hold = held;
        th = std::jthread([this, pace](std::stop_token st) {
            while (!st.stop_requested()) {
                if (hold.load()) { std::this_thread::sleep_for(std::chrono::milliseconds(1)); continue; }
                AVPacket* p = nullptr;
                if (!q.pop(p) || p == nullptr) break;
                Seen s;
                if (p == flush_sentinel()) {
                    s.sentinel = true;
                } else {
                    s.stream = p->stream_index;
                    s.pts    = p->pts * av_q2d(d.fmt_ctx()->streams[p->stream_index]->time_base);
                    av_packet_free(&p);
                }
                {
                    std::lock_guard lk(mtx);
                    seen.push_back(s);
                }
                // Sleep outside the lock: macOS mutexes are not fair, and a
                // consumer re-locking right away starved the test thread's
                // last_pts() polls (it then "saw" 2 s only at ~3-7 s).
                std::this_thread::sleep_for(pace);
            }
        });
    }
    std::vector<Seen> snapshot() { std::lock_guard lk(mtx); return seen; }
    double last_pts()
    {
        std::lock_guard lk(mtx);
        for (auto it = seen.rbegin(); it != seen.rend(); ++it)
            if (!it->sentinel) return it->pts;
        return -1.0;
    }
};

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

struct UnblockOnExit {
    Queue<AVPacket*>& v;
    Queue<AVPacket*>& a;
    Queue<AVPacket*>* s = nullptr;
    ~UnblockOnExit() { v.shutdown(); a.shutdown(); if (s) s->shutdown(); }
};

// Packets one after another, `step` seconds apart, from index `from` on:
// no gap, no duplicate.
void check_continuous(const std::vector<Seen>& v, std::size_t from, double step)
{
    for (std::size_t i = from + 1; i < v.size(); ++i) {
        REQUIRE_FALSE(v[i].sentinel);
        CHECK_THAT(v[i].pts - v[i - 1].pts, Catch::Matchers::WithinAbs(step, 1e-6));
    }
}

void free_queued(std::initializer_list<Queue<AVPacket*>*> qs)
{
    for (Queue<AVPacket*>* q : qs) {
        AVPacket* p = nullptr;
        while (q->try_pop(p))
            if (p && p != flush_sentinel()) av_packet_free(&p);
    }
}

// Index of the first sentinel at or after `from`, or -1.
int sentinel_after(const std::vector<Seen>& v, std::size_t from)
{
    for (std::size_t i = from; i < v.size(); ++i)
        if (v[i].sentinel) return static_cast<int>(i);
    return -1;
}

} // namespace

TEST_CASE("Demuxer: seamless video and audio switches on a local file", "[demux][switch]")
{
    const std::string path = make_multitrack_file();
    Demuxer d;
    REQUIRE(d.open(path));
    REQUIRE(d.video_tracks().size() == 2);
    REQUIRE(d.audio_tracks().size() == 2);
    const int v_old = d.video_stream_index();
    const int a_old = d.audio_stream_index();
    const int v_new = v_old == 0 ? 1 : 0;
    const int a_new = a_old == 2 ? 3 : 2;

    Queue<AVPacket*> videoq{ 64 }, audioq{ 128 };
    Consumer vc(videoq, d, std::chrono::microseconds(4000)), ac(audioq, d, std::chrono::microseconds(5000));
    std::jthread reader([&](std::stop_token st) { d.read_loop(st, videoq, audioq, nullptr); });
    // Destroyed before `reader`: a failed REQUIRE (consumers held, queues
    // full) must not leave read_loop blocked in push() while it is joined.
    UnblockOnExit unblock{ videoq, audioq };

    // ── Video: seamless switch, keep the old track until 3.0 s ───────────────
    // Freeze "playback" at ~2 s so the consumers cannot race past the
    // take-over point before the demuxer handles the request.
    REQUIRE(wait_for([&] { return vc.last_pts() >= 2.0; }));
    vc.hold = ac.hold = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));   // queues fill up
    const std::size_t v_mark = vc.snapshot().size();
    const double pos = vc.last_pts();
    REQUIRE(pos < 2.9);
    d.request_video_switch(v_new, pos, /*exact*/ false, /*keep_until*/ 3.0);
    const std::uint64_t serial = d.switch_requested();
    vc.hold = ac.hold = false;
    REQUIRE(wait_for([&] { return d.switch_settled() >= serial; }));
    REQUIRE(wait_for([&] {
        const auto v = vc.snapshot();
        const int s = sentinel_after(v, v_mark);
        return s >= 0 && v.size() > static_cast<std::size_t>(s) + 30;
    }));
    CHECK(d.video_stream_index() == v_new);
    CHECK(d.consume_video_reopen());
    CHECK(d.consume_video_seamless());
    const double resume = d.consume_video_resume();
    {
        const auto v = vc.snapshot();
        const int s = sentinel_after(v, v_mark);
        // Before the sentinel: only the old track, nothing at or past 3.0 s.
        double last_old = -1.0;
        for (int i = static_cast<int>(v_mark); i < s; ++i) {
            CHECK(v[i].stream == v_old);
            CHECK(v[i].pts < 3.0);
            last_old = v[i].pts;
        }
        // The new track takes over where the kept packets end (3.0 s here).
        CHECK_THAT(resume, Catch::Matchers::WithinAbs(last_old + 1.0 / kFps, 1e-6));
        // The demuxer had read well past 3.0 s; the rest was dropped.
        CHECK_THAT(resume, Catch::Matchers::WithinAbs(3.0, 1e-6));
        // After it: only the new track, starting at a keyframe at or before
        // the take-over point (rawvideo: every frame), then continuous.
        REQUIRE(static_cast<std::size_t>(s) + 1 < v.size());
        CHECK(v[s + 1].stream == v_new);
        CHECK(v[s + 1].pts <= resume + 1e-6);
        CHECK(v[s + 1].pts >= resume - 1.0);   // repositioned, not from the start
        for (std::size_t i = s + 2; i < v.size(); ++i) {
            CHECK(v[i].stream == v_new);
            CHECK_THAT(v[i].pts - v[i - 1].pts, Catch::Matchers::WithinAbs(1.0 / kFps, 1e-6));
        }
    }
    // Audio kept playing through the reposition: no gap, no duplicate.
    {
        const auto a = ac.snapshot();
        for (std::size_t i = 1; i < a.size(); ++i) {
            REQUIRE_FALSE(a[i].sentinel);
            CHECK(a[i].stream == a_old);
            CHECK_THAT(a[i].pts - a[i - 1].pts,
                       Catch::Matchers::WithinAbs(static_cast<double>(kAudioFrame) / kRate, 1e-6));
        }
    }

    // ── Audio: seamless switch, keep the old track until +0.5 s ──────────────
    vc.hold = ac.hold = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    const std::size_t a_mark = ac.snapshot().size();
    const std::size_t v_mark2 = vc.snapshot().size();
    const double apos = ac.last_pts();
    d.request_audio_switch(a_new, apos, /*exact*/ false, apos + 0.5);
    vc.hold = ac.hold = false;
    REQUIRE(wait_for([&] {
        const auto a = ac.snapshot();
        const int s = sentinel_after(a, a_mark);
        return s >= 0 && a.size() > static_cast<std::size_t>(s) + 20;
    }));
    CHECK(d.consume_audio_reopen());
    CHECK(d.consume_audio_seamless());
    {
        const auto a = ac.snapshot();
        const int s = sentinel_after(a, a_mark);
        double last_old = -1.0;
        for (int i = static_cast<int>(a_mark); i < s; ++i) {
            CHECK(a[i].stream == a_old);
            CHECK(a[i].pts < apos + 0.5);
            last_old = a[i].pts;
        }
        // New track continues exactly where the old one stops: the decoder
        // starts it there (audio_start), from packets a little before it
        // (its warm-up: 64 ms here, no fixed PCM frame size).
        const double step = static_cast<double>(kAudioFrame) / kRate;
        CHECK_THAT(d.audio_start().from, Catch::Matchers::WithinAbs(last_old + step, 1e-6));
        CHECK(a[s + 1].stream == a_new);
        CHECK(a[s + 1].pts <= last_old + step + 1e-6);
        CHECK(a[s + 1].pts >= last_old + step - 0.064 - step);
        for (std::size_t i = s + 2; i < a.size(); ++i) {
            CHECK(a[i].stream == a_new);
            CHECK_THAT(a[i].pts - a[i - 1].pts, Catch::Matchers::WithinAbs(step, 1e-6));
        }
    }
    // Video kept playing through the second reposition: continuous.
    {
        const auto v = vc.snapshot();
        for (std::size_t i = v_mark2 + 1; i < v.size(); ++i) {
            REQUIRE_FALSE(v[i].sentinel);
            CHECK(v[i].stream == v_new);
            CHECK_THAT(v[i].pts - v[i - 1].pts, Catch::Matchers::WithinAbs(1.0 / kFps, 1e-6));
        }
    }

    reader.request_stop();
    videoq.shutdown();
    audioq.shutdown();
    reader.join();
    vc.th.request_stop();
    ac.th.request_stop();
    vc.th.join();
    ac.th.join();
    for (Queue<AVPacket*>* q : { &videoq, &audioq }) {
        AVPacket* p = nullptr;
        while (q->try_pop(p))
            if (p && p != flush_sentinel()) av_packet_free(&p);
    }
    std::filesystem::remove(path);
}

TEST_CASE("Demuxer: exact (paused) video switch seeks to the position", "[demux][switch]")
{
    const std::string path = make_multitrack_file();
    Demuxer d;
    REQUIRE(d.open(path));
    const int v_new = d.video_stream_index() == 0 ? 1 : 0;

    Queue<AVPacket*> videoq{ 64 }, audioq{ 128 };
    Consumer vc(videoq, d, std::chrono::microseconds(4000)), ac(audioq, d, std::chrono::microseconds(5000));
    std::jthread reader([&](std::stop_token st) { d.read_loop(st, videoq, audioq, nullptr); });
    // Destroyed before `reader`: a failed REQUIRE (consumers held, queues
    // full) must not leave read_loop blocked in push() while it is joined.
    UnblockOnExit unblock{ videoq, audioq };

    REQUIRE(wait_for([&] { return vc.last_pts() >= 1.0; }));
    const std::size_t v_mark = vc.snapshot().size();
    d.request_video_switch(v_new, 5.0, /*exact*/ true);
    REQUIRE(wait_for([&] {
        const auto v = vc.snapshot();
        const int s = sentinel_after(v, v_mark);
        return s >= 0 && v.size() > static_cast<std::size_t>(s) + 5;
    }));
    CHECK(d.consume_video_reopen());
    CHECK_FALSE(d.consume_video_seamless());
    CHECK_THAT(d.consume_video_resume(), Catch::Matchers::WithinAbs(5.0, 1e-9));
    // The decode thread shows the new track from 5.0 s, not from where the
    // seek lands: the one sentinel carries that as the seek's show_from.
    CHECK_THAT(d.video_show_from().from, Catch::Matchers::WithinAbs(5.0, 1e-9));
    const auto v = vc.snapshot();
    // One sentinel, then the new track at (or just before) 5.0 s.
    const int s = sentinel_after(v, v_mark);
    CHECK(sentinel_after(v, s + 1) < 0);
    CHECK(v[s + 1].stream == v_new);
    CHECK(v[s + 1].pts <= 5.0 + 1e-6);
    CHECK(v[s + 1].pts >= 4.0);

    reader.request_stop();
    videoq.shutdown();
    audioq.shutdown();
    reader.join();
    vc.th.request_stop();
    ac.th.request_stop();
    vc.th.join();
    ac.th.join();
    for (Queue<AVPacket*>* q : { &videoq, &audioq }) {
        AVPacket* p = nullptr;
        while (q->try_pop(p))
            if (p && p != flush_sentinel()) av_packet_free(&p);
    }
    std::filesystem::remove(path);
}

TEST_CASE("Demuxer: at the end of the media read_loop idles and still serves a seek", "[demux][switch]")
{
    const std::string path = make_multitrack_file();
    Demuxer d;
    REQUIRE(d.open(path));
    // Queues large enough to take the whole file: read_loop reaches EOF at once.
    Queue<AVPacket*> videoq{ 8192 }, audioq{ 8192 };
    std::jthread reader([&](std::stop_token st) { d.read_loop(st, videoq, audioq, nullptr); });
    UnblockOnExit unblock{ videoq, audioq };
    auto drain = [](Queue<AVPacket*>& q, std::vector<Seen>& out, const Demuxer& dm) {
        AVPacket* p = nullptr;
        while (q.try_pop(p)) {
            Seen s;
            if (p == nullptr)              s.stream = -2;          // end-of-media marker
            else if (p == flush_sentinel()) s.sentinel = true;
            else {
                s.stream = p->stream_index;
                s.pts    = p->pts * av_q2d(dm.fmt_ctx()->streams[p->stream_index]->time_base);
                av_packet_free(&p);
            }
            out.push_back(s);
        }
    };

    REQUIRE(wait_for([&] { return d.exhausted(); }));
    CHECK(d.ended());
    CHECK(d.last_error().empty());
    std::vector<Seen> v, a;
    drain(videoq, v, d);
    drain(audioq, a, d);
    REQUIRE_FALSE(v.empty());
    CHECK(v.back().stream == -2);       // EOF marker after the last packet
    CHECK(a.back().stream == -2);

    // A seek after the end is served: not exhausted while it is pending,
    // then a flush sentinel and packets from the target onward.
    d.request_seek(10.0);
    CHECK_FALSE(d.exhausted());
    REQUIRE(wait_for([&] { return d.exhausted(); }));
    v.clear();
    drain(videoq, v, d);
    REQUIRE(v.size() > 10);
    CHECK(v.front().sentinel);
    CHECK(v[1].stream >= 0);
    CHECK(v[1].pts <= 10.0 + 1e-6);
    CHECK(v[1].pts >= 9.0);
    CHECK(v.back().stream == -2);

    reader.request_stop();
    videoq.shutdown();
    audioq.shutdown();
    reader.join();
    for (Queue<AVPacket*>* q : { &videoq, &audioq }) {
        AVPacket* p = nullptr;
        while (q->try_pop(p))
            if (p && p != flush_sentinel()) av_packet_free(&p);
    }
    std::filesystem::remove(path);
}

TEST_CASE("Demuxer: a seek with show_from skips the audio before it and hands it to the video decoder",
          "[demux][seek]")
{
    const std::string path = make_multitrack_file();
    Demuxer d;
    REQUIRE(d.open(path));
    // Queues large enough to take the whole file: read_loop reaches EOF at once.
    Queue<AVPacket*> videoq{ 8192 }, audioq{ 8192 };
    std::jthread reader([&](std::stop_token st) { d.read_loop(st, videoq, audioq, nullptr); });
    UnblockOnExit unblock{ videoq, audioq };
    auto drain = [](Queue<AVPacket*>& q, std::vector<Seen>& out, const Demuxer& dm) {
        AVPacket* p = nullptr;
        while (q.try_pop(p)) {
            Seen s;
            if (p == nullptr)              s.stream = -2;          // end-of-media marker
            else if (p == flush_sentinel()) s.sentinel = true;
            else {
                s.stream = p->stream_index;
                s.pts    = p->pts * av_q2d(dm.fmt_ctx()->streams[p->stream_index]->time_base);
                av_packet_free(&p);
            }
            out.push_back(s);
        }
    };
    std::vector<Seen> v, a;
    auto seek_and_read = [&](double secs, double show_from) {
        d.request_seek(secs, false, show_from);
        CHECK_FALSE(d.exhausted());
        REQUIRE(wait_for([&] { return d.exhausted(); }));
        v.clear();
        a.clear();
        drain(videoq, v, d);
        drain(audioq, a, d);
        REQUIRE(v.size() > 2);
        REQUIRE(a.size() > 2);
        CHECK(v.front().sentinel);
        CHECK(a.front().sentinel);
    };
    REQUIRE(wait_for([&] { return d.exhausted(); }));
    drain(videoq, v, d);
    drain(audioq, a, d);

    SECTION("show_from: audio starts with the packet holding it, video where the seek lands") {
        // Lands at or before 9 s; playback resumes at 10.52 s, inside the
        // 50 ms audio packet 10.50-10.55.
        const std::uint64_t gen0 = d.video_show_from().gen;
        seek_and_read(9.0, 10.52);
        CHECK(v[1].pts <= 9.0 + 1e-6);   // the decode thread drops the frames before 10.52 ...
        CHECK_THAT(d.video_show_from().from, Catch::Matchers::WithinAbs(10.52, 1e-9));   // ... told with the sentinel
        CHECK(d.video_show_from().gen == gen0 + 1);   // one generation per seek: applied once
        // The audio starts there too (audio_start), its packets from 64 ms
        // before (the decoder's warm-up): the one ending after 10.456.
        CHECK_THAT(d.audio_start().from, Catch::Matchers::WithinAbs(10.52, 1e-9));
        CHECK_THAT(a[1].pts, Catch::Matchers::WithinAbs(10.45, 1e-6));
        for (std::size_t i = 2; i < a.size() && a[i].stream >= 0; ++i)
            CHECK_THAT(a[i].pts - a[i - 1].pts, Catch::Matchers::WithinAbs(0.05, 1e-6));
    }
    SECTION("a plain seek keeps every packet from where it lands") {
        const std::uint64_t serial = d.request_seek(9.0);
        REQUIRE(wait_for([&] { return d.exhausted(); }));
        CHECK(d.video_seek_serial() == serial);   // the frames after the sentinel carry it
        seek_and_read(9.0, -1.0);
        CHECK(v[1].pts <= 9.0 + 1e-6);
        CHECK(a[1].pts <= 9.0 + 1e-6);
        CHECK(d.video_show_from().from < 0.0);
        CHECK(d.video_seek_serial() == serial + 1);
    }
    SECTION("the newest request's show_from wins") {
        d.request_seek(9.0, false, 10.52);
        seek_and_read(9.0, -1.0);
        CHECK(a[1].pts <= 9.0 + 1e-6);
        CHECK(d.video_show_from().from < 0.0);
    }

    reader.request_stop();
    videoq.shutdown();
    audioq.shutdown();
    reader.join();
    for (Queue<AVPacket*>* q : { &videoq, &audioq }) {
        AVPacket* p = nullptr;
        while (q->try_pop(p))
            if (p && p != flush_sentinel()) av_packet_free(&p);
    }
    std::filesystem::remove(path);
}

TEST_CASE("Demuxer: a paused audio switch is served while the queues are full", "[demux][switch]")
{
    const std::string path = make_multitrack_file();
    Demuxer d;
    REQUIRE(d.open(path));
    const int a_new = d.audio_stream_index() == 2 ? 3 : 2;
    std::atomic<int> wakes{ 0 };
    d.set_on_audio_flush([&] { ++wakes; });

    Queue<AVPacket*> videoq{ 64 }, audioq{ 128 };
    Consumer vc(videoq, d, std::chrono::microseconds(4000)), ac(audioq, d, std::chrono::microseconds(5000));
    std::jthread reader([&](std::stop_token st) { d.read_loop(st, videoq, audioq, nullptr); });
    UnblockOnExit unblock{ videoq, audioq };

    // Paused at ~1 s: nothing drains, and read_loop waits for room in the
    // video queue (64 packets: 2.6 s, it fills before the audio one).
    REQUIRE(wait_for([&] { return vc.last_pts() >= 1.0; }));
    vc.hold = ac.hold = true;
    REQUIRE(wait_for([&] { return videoq.size() == 64; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const double pos = vc.last_pts();   // the frame on screen
    const std::size_t a_mark = ac.snapshot().size();

    d.request_audio_switch(a_new, pos, /*exact*/ true);
    // Served while paused: the queued audio dropped, the audio thread woken
    // (it may be blocked on a stopped device), the new track from pos on.
    REQUIRE(wait_for([&] { return wakes.load() == 1; }));
    ac.hold = false;   // the audio thread decodes into the ring
    REQUIRE(wait_for([&] { return ac.snapshot().size() > a_mark + 20; }));
    CHECK(videoq.size() == 64);   // the video is still paused
    CHECK(d.audio_stream_index() == a_new);
    CHECK(d.consume_audio_reopen());
    CHECK_FALSE(d.consume_audio_seamless());
    {
        const auto a = ac.snapshot();
        REQUIRE(a[a_mark].sentinel);
        const double step = static_cast<double>(kAudioFrame) / kRate;
        CHECK(a[a_mark + 1].stream == a_new);
        CHECK_THAT(d.audio_start().from, Catch::Matchers::WithinAbs(pos, 1e-9));   // the decoder starts it at pos
        CHECK(a[a_mark + 1].pts <= pos + 1e-6);                 // from a little before (warm-up)
        CHECK(a[a_mark + 1].pts >= pos - 0.064 - step);
        for (std::size_t i = a_mark + 1; i < a.size(); ++i) CHECK(a[i].stream == a_new);
        check_continuous(a, a_mark + 1, step);
    }
    // Resumed: the video goes on from where read_loop waited — the packet it
    // held back was read again after the reposition, nothing twice.
    const std::size_t v_now = vc.snapshot().size();
    vc.hold = false;
    REQUIRE(wait_for([&] { return vc.snapshot().size() > v_now + 100; }));
    check_continuous(vc.snapshot(), 0, 1.0 / kFps);

    reader.request_stop();
    videoq.shutdown();
    audioq.shutdown();
    reader.join();
    vc.th.request_stop();
    ac.th.request_stop();
    vc.th.join();
    ac.th.join();
    free_queued({ &videoq, &audioq });
    std::filesystem::remove(path);
}

TEST_CASE("Demuxer: at the end of the media a paused audio switch is served while the end marker waits",
          "[demux][switch]")
{
    const std::string path = make_multitrack_file();
    Demuxer d;
    REQUIRE(d.open(path));
    const int a_new = d.audio_stream_index() == 2 ? 3 : 2;
    std::atomic<int> wakes{ 0 };
    d.set_on_audio_flush([&] { ++wakes; });
    // The video queue takes exactly the routed video stream: read_loop gets
    // to the end, and the video end marker waits for room (paused).
    const int vframes = static_cast<int>(kSecs * kFps);
    Queue<AVPacket*> videoq{ static_cast<std::size_t>(vframes) }, audioq{ 8192 };
    Consumer vc(videoq, d, std::chrono::microseconds(0), /*held*/ true);
    Consumer ac(audioq, d, std::chrono::microseconds(0), /*held*/ true);
    std::jthread reader([&](std::stop_token st) { d.read_loop(st, videoq, audioq, nullptr); });
    UnblockOnExit unblock{ videoq, audioq };

    REQUIRE(wait_for([&] { return d.ended(); }));
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    CHECK(videoq.size() == vframes);   // no room for the marker

    d.request_audio_switch(a_new, 20.0, /*exact*/ true);
    REQUIRE(wait_for([&] { return wakes.load() == 1; }));
    ac.hold = false;
    const int new_packets = static_cast<int>(20.0 * kRate / kAudioFrame);   // 20.0 .. 39.95 s
    REQUIRE(wait_for([&] { return ac.snapshot().size() >= static_cast<std::size_t>(1 + new_packets); }));
    {
        const auto a = ac.snapshot();
        REQUIRE(a[0].sentinel);
        CHECK(a[1].stream == a_new);
        CHECK_THAT(d.audio_start().from, Catch::Matchers::WithinAbs(20.0, 1e-9));
        CHECK_THAT(a[1].pts, Catch::Matchers::WithinAbs(19.95, 1e-6));   // 64 ms of warm-up before it
        check_continuous(a, 1, static_cast<double>(kAudioFrame) / kRate);
    }
    // Resumed: all the video, then the end of the media.
    vc.hold = false;
    REQUIRE(wait_for([&] { return d.exhausted() && vc.snapshot().size() == static_cast<std::size_t>(vframes); }));
    {
        const auto v = vc.snapshot();
        CHECK(v.front().pts == 0.0);
        check_continuous(v, 0, 1.0 / kFps);
    }

    reader.request_stop();
    videoq.shutdown();
    audioq.shutdown();
    reader.join();
    vc.th.request_stop();
    ac.th.request_stop();
    vc.th.join();
    ac.th.join();
    free_queued({ &videoq, &audioq });
    std::filesystem::remove(path);
}

TEST_CASE("Demuxer: a subtitle track enabled behind the read position is read from the position",
          "[demux][switch]")
{
    const std::string path = make_subtitle_file();
    Demuxer d;
    REQUIRE(d.open(path));
    REQUIRE(d.subtitle_tracks().size() == 1);
    const int s_idx = d.subtitle_tracks()[0].stream_idx;

    Queue<AVPacket*> videoq{ 64 }, audioq{ 128 }, subq{ 64 };
    Consumer vc(videoq, d, std::chrono::microseconds(4000)), ac(audioq, d, std::chrono::microseconds(5000));
    Consumer sc(subq, d, std::chrono::microseconds(0));
    std::jthread reader([&](std::stop_token st) { d.read_loop(st, videoq, audioq, &subq); });
    UnblockOnExit unblock{ videoq, audioq, &subq };

    // At ~2 s, with the video read 2.6 s further (64 packets queued).
    REQUIRE(wait_for([&] { return vc.last_pts() >= 2.0; }));
    vc.hold = ac.hold = true;
    REQUIRE(wait_for([&] { return videoq.size() == 64; }));
    const double pos = vc.last_pts();
    REQUIRE(d.read_ahead_pts() > pos + 2.0);
    d.request_subtitle_switch(s_idx, pos);
    vc.hold = ac.hold = false;
    REQUIRE(wait_for([&] { return sc.snapshot().size() >= 4; }));
    {
        // The next cue after pos, not the first one past the read position.
        const auto c = sc.snapshot();
        REQUIRE(c[0].sentinel);
        CHECK(c[1].stream == s_idx);
        CHECK(c[1].pts > pos - 1.0);
        CHECK(c[1].pts <= pos + 1.0);
        check_continuous(c, 1, 1.0);
    }
    // Video and audio played on through the reposition: nothing twice or lost.
    const std::size_t v_now = vc.snapshot().size();
    REQUIRE(wait_for([&] { return vc.snapshot().size() > v_now + 80; }));
    check_continuous(vc.snapshot(), 0, 1.0 / kFps);
    check_continuous(ac.snapshot(), 0, static_cast<double>(kAudioFrame) / kRate);

    reader.request_stop();
    videoq.shutdown();
    audioq.shutdown();
    subq.shutdown();
    reader.join();
    for (Consumer* c : { &vc, &ac, &sc }) {
        c->th.request_stop();
        c->th.join();
    }
    free_queued({ &videoq, &audioq, &subq });
    std::filesystem::remove(path);
}

TEST_CASE("Demuxer: a subtitle track preselected before reading starts from the first cue", "[demux][switch]")
{
    const std::string path = make_subtitle_file();
    Demuxer d;
    REQUIRE(d.open(path));
    REQUIRE(d.subtitle_tracks().size() == 1);
    const int s_idx = d.subtitle_tracks()[0].stream_idx;
    d.preselect_subtitle(d.video_stream_index());   // not a subtitle stream: ignored
    CHECK(d.subtitle_stream_index() == -1);
    d.preselect_subtitle(s_idx);
    CHECK(d.subtitle_stream_index() == s_idx);

    Queue<AVPacket*> videoq{ 64 }, audioq{ 128 }, subq{ 64 };
    Consumer vc(videoq, d, std::chrono::microseconds(4000)), ac(audioq, d, std::chrono::microseconds(5000));
    Consumer sc(subq, d, std::chrono::microseconds(0));
    std::jthread reader([&](std::stop_token st) { d.read_loop(st, videoq, audioq, &subq); });
    UnblockOnExit unblock{ videoq, audioq, &subq };

    REQUIRE(wait_for([&] { return sc.snapshot().size() >= 3; }));
    const auto c = sc.snapshot();
    CHECK_FALSE(c[0].sentinel);   // no switch
    CHECK(c[0].stream == s_idx);
    CHECK(c[0].pts == 0.0);
    check_continuous(c, 0, 1.0);

    reader.request_stop();
    videoq.shutdown();
    audioq.shutdown();
    subq.shutdown();
    reader.join();
    for (Consumer* cs : { &vc, &ac, &sc }) {
        cs->th.request_stop();
        cs->th.join();
    }
    free_queued({ &videoq, &audioq, &subq });
    std::filesystem::remove(path);
}

TEST_CASE("Demuxer: a subtitle track re-enabled after a paused reposition is read from the position",
          "[demux][switch]")
{
    // Paused, an audio switch repositions while the subtitles are on: the
    // re-read stops (no room for video) before it caught up on the cues.
    // Subtitles off and on again must not inherit that re-read's drop point.
    const std::string path = make_subtitle_file();
    Demuxer d;
    REQUIRE(d.open(path));
    REQUIRE(d.subtitle_tracks().size() == 1);
    REQUIRE(d.audio_tracks().size() == 2);
    const int s_idx = d.subtitle_tracks()[0].stream_idx;
    const int a_new = d.audio_stream_index() == 1 ? 2 : 1;
    d.preselect_subtitle(s_idx);

    Queue<AVPacket*> videoq{ 64 }, audioq{ 128 }, subq{ 64 };
    Consumer vc(videoq, d, std::chrono::microseconds(4000)), ac(audioq, d, std::chrono::microseconds(5000));
    Consumer sc(subq, d, std::chrono::microseconds(0));
    std::jthread reader([&](std::stop_token st) { d.read_loop(st, videoq, audioq, &subq); });
    UnblockOnExit unblock{ videoq, audioq, &subq };

    REQUIRE(wait_for([&] { return vc.last_pts() >= 2.0; }));
    vc.hold = ac.hold = true;
    REQUIRE(wait_for([&] { return videoq.size() == 64; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const double pos = vc.last_pts();
    d.request_audio_switch(a_new, pos, /*exact*/ true);
    ac.hold = false;
    REQUIRE(wait_for([&] { return ac.snapshot().size() > 60 && d.audio_stream_index() == a_new; }));
    d.request_subtitle_switch(-1);
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const std::size_t s_mark = sc.snapshot().size();
    d.request_subtitle_switch(s_idx, pos);
    REQUIRE(wait_for([&] {
        const auto c = sc.snapshot();
        const int s = sentinel_after(c, s_mark);
        return s >= 0 && c.size() > static_cast<std::size_t>(s) + 1;
    }));
    {
        const auto c = sc.snapshot();
        const int s = sentinel_after(c, s_mark);
        CHECK(c[s + 1].stream == s_idx);
        CHECK(c[s + 1].pts > pos - 1.0);
        CHECK(c[s + 1].pts <= pos + 1.0);
    }
    vc.hold = false;
    const std::size_t v_now = vc.snapshot().size();
    REQUIRE(wait_for([&] { return vc.snapshot().size() > v_now + 80; }));
    check_continuous(vc.snapshot(), 0, 1.0 / kFps);

    reader.request_stop();
    videoq.shutdown();
    audioq.shutdown();
    subq.shutdown();
    reader.join();
    for (Consumer* c : { &vc, &ac, &sc }) {
        c->th.request_stop();
        c->th.join();
    }
    free_queued({ &videoq, &audioq, &subq });
    std::filesystem::remove(path);
}

TEST_CASE("Demuxer: a paused audio switch on an H.264 Matroska file queues no video twice",
          "[demux][switch]")
{
    // The keyframe the reposition lands on has no dts: it was taken for a
    // new packet, queued again — the video replayed from there, and paused
    // the duplicate waited for room before any audio of the new track.
    const std::string path = make_bframes_mkv();
    if (path.empty()) SKIP("no H.264 encoder (libx264)");
    Demuxer d;
    REQUIRE(d.open(path));
    REQUIRE(d.audio_tracks().size() == 2);
    const int a_new = d.audio_stream_index() == 1 ? 2 : 1;
    std::atomic<int> wakes{ 0 };
    d.set_on_audio_flush([&] { ++wakes; });

    Queue<AVPacket*> videoq{ 64 }, audioq{ 128 };
    Consumer vc(videoq, d, std::chrono::microseconds(4000)), ac(audioq, d, std::chrono::microseconds(5000));
    std::jthread reader([&](std::stop_token st) { d.read_loop(st, videoq, audioq, nullptr); });
    UnblockOnExit unblock{ videoq, audioq };

    REQUIRE(wait_for([&] { return vc.last_pts() >= 3.0; }));
    vc.hold = ac.hold = true;
    REQUIRE(wait_for([&] { return videoq.size() == 64; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    const double pos = vc.last_pts();
    const std::size_t a_mark = ac.snapshot().size();
    d.request_audio_switch(a_new, pos, /*exact*/ true);
    REQUIRE(wait_for([&] { return wakes.load() == 1; }));
    ac.hold = false;
    // The new track arrives while the video is still paused.
    REQUIRE(wait_for([&] { return ac.snapshot().size() > a_mark + 20; }));
    {
        const auto a = ac.snapshot();
        REQUIRE(a[a_mark].sentinel);
        CHECK(a[a_mark + 1].stream == a_new);
        CHECK(a[a_mark + 1].pts >= pos - static_cast<double>(kAudioFrame) / kRate);
    }
    vc.hold = false;
    const std::size_t v_now = vc.snapshot().size();
    REQUIRE(wait_for([&] { return vc.snapshot().size() > v_now + 80; }));
    // Every frame once (decode order differs from display order: sort).
    auto v = vc.snapshot();
    std::vector<double> pts;
    for (const Seen& x : v) {
        REQUIRE_FALSE(x.sentinel);
        pts.push_back(x.pts);
    }
    std::sort(pts.begin(), pts.end());
    CHECK(std::adjacent_find(pts.begin(), pts.end(),
                             [](double a, double b) { return std::abs(a - b) < 1e-6; }) == pts.end());
    CHECK_THAT(pts.front(), Catch::Matchers::WithinAbs(0.0, 1e-6));
    // No frame missing up to the B-frames still on their way.
    std::size_t gaps = 0;
    for (std::size_t i = 1; i + 3 < pts.size(); ++i)
        gaps += std::abs(pts[i] - pts[i - 1] - 1.0 / kFps) > 1e-6;
    CHECK(gaps == 0);

    reader.request_stop();
    videoq.shutdown();
    audioq.shutdown();
    reader.join();
    vc.th.request_stop();
    ac.th.request_stop();
    vc.th.join();
    ac.th.join();
    free_queued({ &videoq, &audioq });
    std::filesystem::remove(path);
}
