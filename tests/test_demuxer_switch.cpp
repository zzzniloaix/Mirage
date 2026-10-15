// Track switching in Demuxer::read_loop on a generated local file with two
// video and two audio streams (rawvideo + PCM: every packet is a keyframe,
// no encoder needed). Checks the seamless take-over contract the decode
// threads rely on: what stays queued from the old track, where the new one
// starts, and that tracks which keep playing see no duplicated packets after
// the container is repositioned.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Demuxer.h"

#include <atomic>
#include <cmath>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
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
    Consumer(Queue<AVPacket*>& q_, const Demuxer& d_, std::chrono::microseconds pace) : q(q_), d(d_)
    {
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
    ~UnblockOnExit() { v.shutdown(); a.shutdown(); }
};

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
        // New track continues exactly where the old one stops.
        const double step = static_cast<double>(kAudioFrame) / kRate;
        CHECK(a[s + 1].stream == a_new);
        CHECK_THAT(a[s + 1].pts, Catch::Matchers::WithinAbs(last_old + step, 1e-6));
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
    const auto v = vc.snapshot();
    // The last sentinel starts the new track at (or just before) 5.0 s.
    int s = sentinel_after(v, v_mark);
    while (sentinel_after(v, s + 1) >= 0) s = sentinel_after(v, s + 1);
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
