// Startup watchdog: the once-only timer, the state line and the diagnosis it
// logs, and the state Demuxer::read_loop publishes for it (on a generated
// local file: rawvideo + PCM, no encoder needed).

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "PipelineState.h"
#include "Demuxer.h"

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

extern "C" {
#include <libavformat/avformat.h>
}

using Catch::Matchers::ContainsSubstring;

TEST_CASE("StartupWatchdog fires once after the timeout without a frame", "[pipeline_state]")
{
    StartupWatchdog w(10.0);
    CHECK_FALSE(w.due(5.0));          // not started
    w.start(100.0);
    CHECK_FALSE(w.due(100.0));
    CHECK_FALSE(w.due(109.9));
    CHECK(w.due(110.0));
    CHECK_FALSE(w.due(111.0));        // only once
    CHECK_FALSE(w.due(500.0));

    w.start(0.0);                     // a new session re-arms it
    CHECK(w.due(12.0));
}

TEST_CASE("StartupWatchdog stays quiet once a frame is up", "[pipeline_state]")
{
    StartupWatchdog w(10.0);
    w.start(0.0);
    CHECK_FALSE(w.due(3.0));
    w.on_frame();
    CHECK_FALSE(w.due(10.0));
    CHECK_FALSE(w.due(60.0));
}

namespace {

// The state of the original "hang before the first frame": Space reached the
// freshly focused window before the first frame, so the render loop stopped
// taking frames — frameq full, the decoder waits for room, the demuxer for room
// in videoq, the audio thread for ring space (the paused device does not drain).
PipelineSnapshot paused_before_first_frame()
{
    PipelineSnapshot s;
    s.since_start_s = 10.0;
    s.paused = true;
    s.videoq = 64; s.videoq_cap = 64;
    s.audioq = 30; s.audioq_cap = 128;
    s.frameq = 8;  s.frameq_cap = 8;
    s.demux = DemuxState::PushVideo;
    s.video_pkts = 90; s.audio_pkts = 55;
    s.video = DecodeState::WaitOutput;
    s.video_in = 26; s.video_out = 9; s.hw = true;
    s.has_audio = true;
    s.audio = DecodeState::WaitOutput;
    s.audio_in = 25;
    s.audio_device = true;
    s.audio_callbacks = 40;
    s.audio_buffered_s = 0.5;
    return s;
}

} // namespace

TEST_CASE("diagnose: paused before the first frame", "[pipeline_state]")
{
    const PipelineSnapshot s = paused_before_first_frame();
    const std::string d = diagnose_no_frame(s);
    CHECK_THAT(d, ContainsSubstring("8 decoded frame(s) wait in the frame queue"));
    CHECK_THAT(d, ContainsSubstring("paused"));
    CHECK_THAT(d, !ContainsSubstring("render loop is not taking them"));

    const std::string line = describe_pipeline(s);
    for (const char* f : { "t=10.0s", "paused=1", "demux=push-video", "videoq=64/64", "frameq=8/8",
                           "video=wait-output in=26 out=9 hw=1", "audio=wait-output", "callbacks=40" })
        CHECK_THAT(line, ContainsSubstring(f));
}

TEST_CASE("diagnose: frames queued while playing points at the render loop", "[pipeline_state]")
{
    PipelineSnapshot s = paused_before_first_frame();
    s.paused = false;
    CHECK_THAT(diagnose_no_frame(s), ContainsSubstring("the render loop is not taking them"));
    s.scrubbing = true;
    CHECK_THAT(diagnose_no_frame(s), ContainsSubstring("seek-bar drag"));
}

TEST_CASE("diagnose: decoder starved by a demuxer stuck on a full audio queue", "[pipeline_state]")
{
    // The audio device never runs: the ring fills, the audio thread waits in
    // push(), audioq fills and the demuxer blocks before the decoder has a frame.
    PipelineSnapshot s;
    s.audioq = 128; s.audioq_cap = 128; s.videoq_cap = 64; s.frameq_cap = 8;
    s.demux = DemuxState::PushAudio;
    s.video_pkts = 3; s.audio_pkts = 153;
    s.video = DecodeState::WaitPacket; s.video_in = 3;
    s.has_audio = true; s.audio = DecodeState::WaitOutput; s.audio_in = 25;
    s.audio_device = true; s.audio_callbacks = 0;
    const std::string d = diagnose_no_frame(s);
    CHECK_THAT(d, ContainsSubstring("3 packet(s) and no frame yet"));
    CHECK_THAT(d, ContainsSubstring("blocked on a full audio queue (128/128)"));
    CHECK_THAT(d, ContainsSubstring("device is not draining it"));
    CHECK_THAT(d, ContainsSubstring("audio device has not called back yet"));
}

TEST_CASE("diagnose: nothing read yet, a stopped demuxer, a stuck codec", "[pipeline_state]")
{
    PipelineSnapshot s;
    s.demux = DemuxState::Reading;
    CHECK_THAT(diagnose_no_frame(s), ContainsSubstring("no video packet read yet"));

    s.demux = DemuxState::Retrying;
    CHECK_THAT(diagnose_no_frame(s), ContainsSubstring("network reads are failing"));

    s.demux = DemuxState::Stopped;
    s.demux_error = "Connection reset by peer";
    CHECK_THAT(diagnose_no_frame(s), ContainsSubstring("the demuxer stopped: Connection reset by peer"));
    CHECK_THAT(describe_pipeline(s), ContainsSubstring("error=\"Connection reset by peer\""));

    PipelineSnapshot c;
    c.demux = DemuxState::Reading;
    c.video_pkts = 5; c.video_in = 5; c.video = DecodeState::Decoding; c.hw = true;
    CHECK_THAT(diagnose_no_frame(c), ContainsSubstring("in the codec with 5 packet(s) in and no frame out (VideoToolbox)"));

    PipelineSnapshot q;
    q.video_pkts = 4; q.videoq = 4; q.video = DecodeState::WaitPacket;
    CHECK_THAT(diagnose_no_frame(q), ContainsSubstring("4 video packet(s) are queued but the decode thread has not taken one"));

    PipelineSnapshot n;
    CHECK_THAT(describe_pipeline(n), ContainsSubstring("audio=none"));
}

// ── Demuxer state on a real read_loop ────────────────────────────────────────

namespace {

constexpr int kFps = 25, kRate = 8000, kAudioFrame = 400;   // 50 ms PCM packets

// 2 s of 16x8 gray rawvideo + mono PCM, interleaved: 50 video + 40 audio packets.
std::string make_av_file()
{
    const auto path = std::filesystem::temp_directory_path() / "mirage_test_pipeline_state.nut";
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
    AVStream* as = avformat_new_stream(oc, nullptr);
    as->codecpar->codec_type  = AVMEDIA_TYPE_AUDIO;
    as->codecpar->codec_id    = AV_CODEC_ID_PCM_S16LE;
    as->codecpar->sample_rate = kRate;
    as->codecpar->format      = AV_SAMPLE_FMT_S16;
    av_channel_layout_default(&as->codecpar->ch_layout, 1);
    as->time_base             = { 1, kRate };
    REQUIRE(avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);
    int vi = 0, ai = 0;
    while (vi < 2 * kFps || ai < 2 * kRate / kAudioFrame) {
        const bool video = vi < 2 * kFps && (ai >= 2 * kRate / kAudioFrame
                           || static_cast<double>(vi) / kFps <= static_cast<double>(ai) * kAudioFrame / kRate);
        AVPacket* pkt = av_packet_alloc();
        REQUIRE(av_new_packet(pkt, video ? 16 * 8 : kAudioFrame * 2) >= 0);
        pkt->stream_index = video ? 0 : 1;
        pkt->pts = pkt->dts = video ? vi : static_cast<int64_t>(ai) * kAudioFrame;
        pkt->duration = video ? 1 : kAudioFrame;
        pkt->flags |= AV_PKT_FLAG_KEY;
        av_packet_rescale_ts(pkt, video ? AVRational{ 1, kFps } : AVRational{ 1, kRate },
                             oc->streams[pkt->stream_index]->time_base);
        REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
        av_packet_free(&pkt);
        (video ? vi : ai)++;
    }
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    return path.string();
}

template <typename Pred>
bool wait_for(Pred pred, int ms = 3000)
{
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (!pred()) {
        if (std::chrono::steady_clock::now() > until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return true;
}

void drain(Queue<AVPacket*>& q)
{
    AVPacket* p = nullptr;
    while (q.try_pop(p))
        if (p && p != flush_sentinel()) av_packet_free(&p);
}

} // namespace

TEST_CASE("Demuxer publishes a wait on a full audio queue and its routed packets", "[pipeline_state][demux]")
{
    const std::string path = make_av_file();
    Demuxer d;
    REQUIRE(d.open(path));
    CHECK(d.state() == DemuxState::Starting);

    // Nobody drains audio: the demuxer must end up waiting for room in audioq.
    Queue<AVPacket*> videoq(1000), audioq(3);
    std::jthread th([&](std::stop_token st) { d.read_loop(st, videoq, audioq); });
    REQUIRE(wait_for([&] { return d.state() == DemuxState::PushAudio; }));
    CHECK(d.routed_audio() == 3);
    CHECK(audioq.size() == 3);
    CHECK(audioq.capacity() == 3);
    CHECK(d.routed_video() == videoq.size());
    CHECK(d.routed_video() > 0);

    th.request_stop();
    videoq.shutdown();
    audioq.shutdown();
    th.join();
    CHECK(d.state() == DemuxState::Stopped);
    drain(videoq);
    drain(audioq);
}

TEST_CASE("Demuxer idles at the end of the media with every packet routed", "[pipeline_state][demux]")
{
    const std::string path = make_av_file();
    Demuxer d;
    REQUIRE(d.open(path));
    Queue<AVPacket*> videoq(1000), audioq(1000);
    std::jthread th([&](std::stop_token st) { d.read_loop(st, videoq, audioq); });
    REQUIRE(wait_for([&] { return d.state() == DemuxState::AtEnd; }));
    CHECK(d.ended());
    CHECK(d.routed_video() == 2 * kFps);
    CHECK(d.routed_audio() == 2 * kRate / kAudioFrame);
    CHECK(d.routed_subtitle() == 0);

    th.request_stop();
    videoq.shutdown();
    audioq.shutdown();
    th.join();
    CHECK(d.state() == DemuxState::Stopped);
    drain(videoq);
    drain(audioq);
}
