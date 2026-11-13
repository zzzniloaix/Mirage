// FFmpeg's hls seek behaviour that the panel's seek decision rests on, on playlists the test
// writes (eval/phase3-survey/design.md §9.1, eval/phase3-survey/ui.md §5.2): written by WP12.
//
// eval/phase3-survey/ui.md §2.4's probe (its experiments/hls-seek-probe/) as
// test cases: HLS VOD playlists of three 2 s MPEG-TS segments, written here
// with FFmpeg's libraries (25 fps, one keyframe at each segment's start, each
// segment a flat luma of its own, so that a decoded frame names its
// segment), with an EXT-X-DISCONTINUITY before the second segment and before
// the third:
//   continuous  the segments' PTS from 10, 12 and 14 s, as their places in
//               the playlist;
//   reset       the second, the ad, restarting at 1 s, and the third going on
//               from the first, at 12 s: the probe's shape, its ad from 1 s
//               rather than from 0 so that FFmpeg shifts none of its
//               timestamps (the probe's ad read back from 0.040 s);
//   origin      the ad restarting at the main content's origin, 10 s, as
//               D-069's ad does, and the third where it would be without
//               the ad, at 14 s, as D-069's main content comes back
//               (eval/phase3-survey/testgen.md §2.3).
// Each shape is written with FFmpeg's MPEG-2 encoder without B-frames, as
// the probe's are, and with libx264 as D-069's ladders are encoded where the
// build has it (D-080; tests/SyntheticLadder.cpp), B-frames at x264's
// defaults. Each is opened as a local playlist and sought as the player
// seeks (src/Demuxer.cpp): the target clamped into [start, start + duration
// - 0.5], av_seek_frame() on stream -1 with AVSEEK_FLAG_BACKWARD and a
// second try at start + 1 s; then the first frame decoded whose PTS is at or
// after the target is the one shown (MediaSession's show_from), or the first
// after a target clamped from before the start. The cases pin ui.md §2.4's
// measured rules and what the seek decision (src/TimelineMatch.h) adds to
// them:
// - the first packet's DTS leads its PTS by the decoder's reorder delay,
//   AVCodecParameters::video_delay: a frame for MPEG-2, two for libx264;
// - continuous: a frame is reached by its PTS, from its segment's start;
//   but a segment's last frame isn't, nor, with libx264, the one before it,
//   the DTS lead carrying the position the demuxer looks the segment up by
//   into the next segment;
// - reset: content before the ad, and after it, is reached by its PTS; an
//   ad's frame by its PTS makes av_seek_frame() fail, and the player's clamp
//   restarts the stream; the ad by its place in the playlist is skipped, the
//   third segment shown from its start; the third segment's last frame by
//   its PTS shows nothing, every packet left being dropped;
// - origin, which ui.md §2.4 didn't probe: an ad's frame by its PTS shows the
//   main content's frame of that PTS, not the ad's; the ad by its place is
//   skipped; content after the ad is reached by its PTS;
// and, at every target, that simulate_seek() on the playlists' frames and
// segments predicts the frame the player shows, so that an FFmpeg whose
// hls_read_seek behaves otherwise fails here rather than in the panel
// (ui.md §5.2); and at every frame of each playlist, that decide_seek(),
// given what the panel has (the stream's start, duration and video_delay),
// seeks only a frame the player's seek shows and offers only a segment start
// the player's seek shows (D-114 (b)), which frames it offers being pinned.
// Nothing is downloaded and no program is run.

#include <catch2/catch_test_macros.hpp>

#include "TestTemp.h"
#include "TimelineMatch.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/log.h>
#include <libavutil/opt.h>
}

namespace {

constexpr int kFps       = 25;
constexpr int kSegFrames = 50;                      // 2 s a segment
constexpr int kTicks     = 90000 / kFps;            // a frame in MPEG-TS's 1/90000
constexpr int kLuma[3]   = { 60, 200, 120 };        // each segment's, which names it

// The encoders the cases write with.
enum class SegmentEncoder : std::uint8_t {
    Mpeg2,   // FFmpeg's MPEG-2 encoder without B-frames, the probe's
    X264,    // libx264 as D-069's ladders are encoded (D-080; tests/SyntheticLadder.cpp's open_encoder())
};

// Holds FFmpeg's log quiet while it lives: the MPEG-TS demuxer logs the last
// packets of a segment before a discontinuity as corrupt, which is no error
// here (the probe's README).
struct QuietLog {
    int level = av_log_get_level();
    QuietLog() { av_log_set_level(AV_LOG_QUIET); }
    ~QuietLog() { av_log_set_level(level); }
};

// One MPEG-TS segment of kSegFrames frames of luma `luma`, its first PTS
// `first_s` seconds, encoded by `which`; false without that encoder.
bool write_segment(const std::filesystem::path& path, double first_s, int luma, SegmentEncoder which)
{
    const AVCodec* codec = which == SegmentEncoder::X264 ? avcodec_find_encoder_by_name("libx264")
                                                         : avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
    if (!codec) return false;
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "mpegts", path.c_str()) >= 0);
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    REQUIRE(enc);
    enc->width     = 64;
    enc->height    = 64;
    enc->pix_fmt   = AV_PIX_FMT_YUV420P;
    enc->time_base = { 1, kFps };
    enc->framerate = { kFps, 1 };
    enc->gop_size  = kSegFrames;   // one keyframe, at the segment's start
    if (which == SegmentEncoder::X264) {
        // A closed GOP of the segment's frames, one thread, bit-exact, x264's
        // veryfast preset at D-069's normal quantizer, no scene cuts.
        enc->keyint_min   = kSegFrames;
        enc->thread_count = 1;
        enc->flags       |= AV_CODEC_FLAG_BITEXACT | AV_CODEC_FLAG_CLOSED_GOP;
        REQUIRE(av_opt_set(enc->priv_data, "preset", "veryfast", 0) >= 0);
        REQUIRE(av_opt_set_int(enc->priv_data, "qp", 22, 0) >= 0);
        REQUIRE(av_opt_set(enc->priv_data, "x264-params", "scenecut=0", 0) >= 0);
    } else {
        enc->max_b_frames = 0;
        enc->bit_rate     = 200000;
    }
    REQUIRE(avcodec_open2(enc, codec, nullptr) >= 0);
    AVStream* st = avformat_new_stream(oc, nullptr);
    REQUIRE(st);
    st->time_base = { 1, 90000 };
    REQUIRE(avcodec_parameters_from_context(st->codecpar, enc) >= 0);
    REQUIRE(avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);

    AVFrame* f = av_frame_alloc();
    f->format  = enc->pix_fmt;
    f->width   = enc->width;
    f->height  = enc->height;
    REQUIRE(av_frame_get_buffer(f, 0) >= 0);
    AVPacket* pkt = av_packet_alloc();
    auto drain = [&] {
        while (avcodec_receive_packet(enc, pkt) == 0) {
            av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
            pkt->stream_index = st->index;
            REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
        }
    };
    const std::int64_t first = std::llround(first_s * kFps);
    for (int i = 0; i < kSegFrames; ++i) {
        REQUIRE(av_frame_make_writable(f) >= 0);
        for (int y = 0; y < f->height; ++y) std::memset(f->data[0] + y * f->linesize[0], luma, 64);
        for (int p = 1; p < 3; ++p)
            for (int y = 0; y < f->height / 2; ++y) std::memset(f->data[p] + y * f->linesize[p], 128, 32);
        f->pts = first + i;
        REQUIRE(avcodec_send_frame(enc, f) >= 0);
        drain();
    }
    REQUIRE(avcodec_send_frame(enc, nullptr) >= 0);
    drain();
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    av_packet_free(&pkt);
    av_frame_free(&f);
    avcodec_free_context(&enc);
    avformat_free_context(oc);
    return true;
}

// The playlist `name` under the test's directory, its segments' first PTS
// `starts` (seconds), encoded by `which`; empty without that encoder.
std::string write_playlist(const std::string& name, const double (&starts)[3],
                           SegmentEncoder which = SegmentEncoder::Mpeg2)
{
    const std::filesystem::path dir = testtemp::path("hls_seek_" + name);
    std::filesystem::create_directories(dir);
    for (int s = 0; s < 3; ++s)
        if (!write_segment(dir / ("seg" + std::to_string(s) + ".ts"), starts[s], kLuma[s], which)) return {};
    std::ofstream m(dir / "index.m3u8");
    m << "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:0\n"
         "#EXT-X-PLAYLIST-TYPE:VOD\n"
         "#EXTINF:2.000,\nseg0.ts\n"
         "#EXT-X-DISCONTINUITY\n#EXTINF:2.000,\nseg1.ts\n"
         "#EXT-X-DISCONTINUITY\n#EXTINF:2.000,\nseg2.ts\n"
         "#EXT-X-ENDLIST\n";
    REQUIRE(m);
    return (dir / "index.m3u8").string();
}

// An open playlist: its start, duration, video stream, how far its first
// packet's DTS leads that packet's PTS, and its decoder's reorder delay as
// the player's Demuxer has it after avformat_find_stream_info().
struct Opened {
    AVFormatContext* fc = nullptr;
    int    video = -1;
    double start = 0, duration = 0, lead = 0;
    int    video_delay = 0;
    ~Opened() { avformat_close_input(&fc); }
};

void open_playlist(const std::string& path, Opened& o)
{
    REQUIRE(avformat_open_input(&o.fc, path.c_str(), nullptr, nullptr) == 0);
    REQUIRE(avformat_find_stream_info(o.fc, nullptr) >= 0);
    o.video = av_find_best_stream(o.fc, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    REQUIRE(o.video >= 0);
    o.start       = static_cast<double>(o.fc->start_time) / AV_TIME_BASE;
    o.duration    = static_cast<double>(o.fc->duration) / AV_TIME_BASE;
    o.video_delay = o.fc->streams[o.video]->codecpar->video_delay;
    AVPacket* p = av_packet_alloc();
    while (av_read_frame(o.fc, p) >= 0) {
        const bool video = p->stream_index == o.video;
        if (video) o.lead = static_cast<double>(p->pts - p->dts) * av_q2d(o.fc->streams[o.video]->time_base);
        av_packet_unref(p);
        if (video) break;
    }
    av_packet_free(&p);
}

// What the player shows after its seek to `target`: the frame's PTS and the
// segment its luma names; nullopt when the seek fails or nothing is shown.
struct Shown {
    double pts = 0;
    int    segment = -1;
};

std::optional<Shown> player_seek(Opened& o, double target)
{
    // src/Demuxer.cpp's clamp and its second try.
    double sought = target, show_from = target;
    if (sought < o.start) {
        sought    = o.start;
        show_from = -1.0;
    } else if (o.duration > 0.0 && sought > o.start + o.duration - 0.5) {
        sought = std::max(o.start, o.start + o.duration - 0.5);
    }
    int r = av_seek_frame(o.fc, -1, static_cast<std::int64_t>(sought * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
    if (r < 0 && sought < o.start + 1.0)
        r = av_seek_frame(o.fc, -1, static_cast<std::int64_t>((o.start + 1.0) * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
    if (r < 0) return std::nullopt;

    const AVStream* st = o.fc->streams[o.video];
    const AVCodec*  dc = avcodec_find_decoder(st->codecpar->codec_id);
    REQUIRE(dc);
    AVCodecContext* dec = avcodec_alloc_context3(dc);
    REQUIRE(avcodec_parameters_to_context(dec, st->codecpar) >= 0);
    REQUIRE(avcodec_open2(dec, dc, nullptr) >= 0);
    AVPacket* p = av_packet_alloc();
    AVFrame*  f = av_frame_alloc();
    std::optional<Shown> out, last_skipped;
    auto shown = [&](const AVFrame* frame) {
        Shown s{ .pts = static_cast<double>(frame->pts) * av_q2d(st->time_base) };
        for (int k = 0; k < 3; ++k)
            if (std::abs(frame->data[0][0] - kLuma[k]) <= 2) s.segment = k;
        return s;
    };
    bool ended = false;
    while (!out && !ended) {
        const int rr = av_read_frame(o.fc, p);
        if (rr < 0) {
            ended = true;
            avcodec_send_packet(dec, nullptr);
        } else if (p->stream_index == o.video) {
            avcodec_send_packet(dec, p);
        }
        av_packet_unref(p);
        while (!out && avcodec_receive_frame(dec, f) == 0) {
            // MediaSession's show_from: a frame half a frame or more before
            // the target is dropped (1 ms without a duration).
            const double tol = f->duration > 0 ? 0.5 * static_cast<double>(f->duration) * av_q2d(st->time_base)
                                               : 0.001;
            if (show_from >= 0.0 && shown(f).pts + tol < show_from) {
                last_skipped = shown(f);
                continue;
            }
            out = shown(f);
        }
    }
    // Every frame before the target: the decode thread shows the last.
    if (!out) out = last_skipped;
    av_frame_free(&f);
    av_packet_free(&p);
    avcodec_free_context(&dec);
    return out;
}

// The clock the seek decision has of the same playlist: its 150 frames with
// their PTS, three segments of 2 s from 0, 2 and 4 s of playlist time.
timeline::VariantClock clock_of(const double (&starts)[3])
{
    timeline::VariantClock c;
    c.variant    = 0;
    c.playlist   = true;
    c.time_base  = { 1, 90000 };
    c.frame_rate = { kFps, 1 };
    for (int s = 0; s < 3; ++s) {
        for (int i = 0; i < kSegFrames; ++i)
            c.frames.push_back({ .index = s * kSegFrames + i, .ref_index = s * kSegFrames + i,
                                 .pts = std::llround(starts[s] * 90000) + i * kTicks, .sequence = s });
        c.segments.push_back({ .media_sequence = s, .discontinuity_sequence = s, .playlist_time = 2.0 * s,
                               .extinf = 2.0, .ref_index = s * kSegFrames, .ref_end = (s + 1) * kSegFrames,
                               .first = static_cast<std::size_t>(s * kSegFrames),
                               .last = static_cast<std::size_t>((s + 1) * kSegFrames) });
    }
    return c;
}

// What the panel has of the open stream: its start, duration and reorder
// delay, no lead in seconds.
timeline::SeekClock panel_clock(const Opened& o)
{
    return { .start = o.start, .duration = o.duration, .video_delay = o.video_delay };
}

// The frame simulate_seek() says the player shows after a seek to `target`,
// with the first packet's DTS lead measured.
std::optional<Shown> predicted(const timeline::VariantClock& c, const Opened& o, double target)
{
    const timeline::SeekLanding l = timeline::simulate_seek(c, panel_clock(o), target, o.lead);
    if (l.kind != timeline::SeekLanding::Kind::Shown) return std::nullopt;
    return Shown{ .pts = c.seconds(c.frames[l.frame]), .segment = static_cast<int>(l.frame) / kSegFrames };
}

// The player's seek to `target` shows the frame of PTS `pts` in segment
// `segment`, or nothing (segment -1), and simulate_seek() predicts the same.
void check_seek(Opened& o, const timeline::VariantClock& c, double target, double pts, int segment)
{
    INFO("target " << target << " s");
    const std::optional<Shown> got = player_seek(o, target);
    const std::optional<Shown> sim = predicted(c, o, target);
    if (segment < 0) {
        CHECK_FALSE(got);
        CHECK_FALSE(sim);
        return;
    }
    REQUIRE(got);
    CHECK(std::abs(got->pts - pts) < 1e-6);
    CHECK(got->segment == segment);
    REQUIRE(sim);
    CHECK(std::abs(sim->pts - got->pts) < 1e-6);
    CHECK(sim->segment == got->segment);
}

// decide_seek() at every frame of the playlist, on what the panel has
// (panel_clock()), against the player's seek: a Seek shows the frame, an
// Offer its segment's first frame (D-114 (b)), and no frame is left without
// either. The frames offered, in order.
std::vector<std::size_t> check_decisions(Opened& o, const timeline::VariantClock& c)
{
    std::vector<std::size_t> offered;
    for (std::size_t i = 0; i < c.frames.size(); ++i) {
        INFO("frame " << i);
        const timeline::SeekDecision d = timeline::decide_seek(c, panel_clock(o), c.frames[i].ref_index);
        REQUIRE(d.kind != timeline::SeekDecision::Kind::None);
        const bool seek = d.kind == timeline::SeekDecision::Kind::Seek;
        if (!seek) offered.push_back(i);
        REQUIRE((seek || d.segment));
        const std::size_t want = seek ? i : c.segments[*d.segment].first;
        CHECK(std::abs(d.target - c.seconds(c.frames[want])) < 1e-9);
        const std::optional<Shown> got = player_seek(o, d.target);
        REQUIRE(got);
        CHECK(std::abs(got->pts - c.seconds(c.frames[want])) < 1e-6);
        CHECK(got->segment == static_cast<int>(want) / kSegFrames);
    }
    return offered;
}

// The frames [first, last) and `more`, in order.
std::vector<std::size_t> frame_list(std::vector<std::size_t> more, std::size_t first = 0, std::size_t last = 0)
{
    for (std::size_t i = first; i < last; ++i) more.push_back(i);
    std::sort(more.begin(), more.end());
    return more;
}

bool same_seconds(double a, double b) { return std::abs(a - b) < 1e-6; }

} // namespace

TEST_CASE("hls seek behaviour: continuous timestamps, a frame reached from its segment's start, a "
          "segment's last frame carried into the next by the DTS lead",
          "[timeline][hls_seek_behaviour]")
{
    const QuietLog quiet;
    constexpr double kStarts[3] = { 10.0, 12.0, 14.0 };
    const std::string path = write_playlist("continuous", kStarts);
    if (path.empty()) SKIP("no MPEG-2 encoder in this FFmpeg");
    Opened o;
    open_playlist(path, o);
    CHECK(same_seconds(o.start, 10.0));
    CHECK(same_seconds(o.duration, 6.0));
    CHECK(same_seconds(o.lead, 0.04));   // the first packet's DTS a frame before its PTS
    CHECK(o.video_delay == 1);           // the decoder's reorder delay, that frame
    const timeline::VariantClock c = clock_of(kStarts);

    check_seek(o, c, 11.0, 11.0, 0);     // the probe's: from seg0's start
    check_seek(o, c, 11.92, 11.92, 0);
    check_seek(o, c, 12.52, 12.52, 1);   // from seg1's start
    check_seek(o, c, 14.52, 14.52, 2);
    // A segment's last frame: its PTS less the stream's start, plus the lead,
    // is the next segment's place in the playlist.
    check_seek(o, c, 11.96, 12.0, 1);
    // The stream's last frame: the clamp seeks into the last segment, and the
    // frame is still awaited.
    check_seek(o, c, 15.96, 15.96, 2);

    // Every frame by its PTS but the first two segments' last frames, which
    // are offered their own segment's start.
    CHECK(check_decisions(o, c) == frame_list({ 49, 99 }));
}

TEST_CASE("hls seek behaviour: an ad whose timestamps restart below the start, not reached by its "
          "PTS nor by its place; content before and after it reached by its PTS",
          "[timeline][hls_seek_behaviour]")
{
    const QuietLog quiet;
    constexpr double kStarts[3] = { 10.0, 1.0, 12.0 };
    const std::string path = write_playlist("reset", kStarts);
    if (path.empty()) SKIP("no MPEG-2 encoder in this FFmpeg");
    Opened o;
    open_playlist(path, o);
    CHECK(same_seconds(o.start, 10.0));
    CHECK(same_seconds(o.duration, 6.0));
    CHECK(same_seconds(o.lead, 0.04));
    CHECK(o.video_delay == 1);
    const timeline::VariantClock c = clock_of(kStarts);

    // The hls demuxer refuses an ad's frame by its PTS, before its first
    // packet's DTS (ui.md §2.4: "Operation not permitted").
    CHECK(av_seek_frame(o.fc, -1, static_cast<std::int64_t>(1.5 * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD) < 0);

    check_seek(o, c, 11.0, 11.0, 0);    // content before the ad
    check_seek(o, c, 1.52, 10.0, 0);    // the ad by its PTS: the clamp restarts the stream
    check_seek(o, c, 12.52, 12.52, 2);  // the ad by its place, 2.52 s on: skipped
    check_seek(o, c, 13.0, 13.0, 2);    // content after the ad, its PTS going on from before it
    check_seek(o, c, 13.96, 0, -1);     // its last frame: every packet dropped

    // The ad's frames offered the segment before it; the first segment's
    // last frame and the third's offered their own segment's start.
    CHECK(check_decisions(o, c) == frame_list({ 49, 149 }, 50, 100));
}

TEST_CASE("hls seek behaviour: an ad restarting at the main content's origin, as D-069's does, not "
          "reached: its PTS show the main content's frame",
          "[timeline][hls_seek_behaviour]")
{
    const QuietLog quiet;
    constexpr double kStarts[3] = { 10.0, 10.0, 14.0 };
    const std::string path = write_playlist("origin", kStarts);
    if (path.empty()) SKIP("no MPEG-2 encoder in this FFmpeg");
    Opened o;
    open_playlist(path, o);
    CHECK(same_seconds(o.start, 10.0));
    CHECK(same_seconds(o.duration, 6.0));
    CHECK(same_seconds(o.lead, 0.04));
    CHECK(o.video_delay == 1);
    const timeline::VariantClock c = clock_of(kStarts);

    check_seek(o, c, 10.52, 10.52, 0);  // an ad's frame by its PTS: the main content's of that PTS
    check_seek(o, c, 11.0, 11.0, 0);
    check_seek(o, c, 12.52, 14.0, 2);   // the ad by its place: skipped
    check_seek(o, c, 14.0, 14.0, 2);    // content after the ad
    check_seek(o, c, 14.52, 14.52, 2);

    // The ad's frames, and the first segment's last, offered the first
    // segment's start; the content after the ad by its PTS.
    CHECK(check_decisions(o, c) == frame_list({ 49 }, 50, 100));
}

TEST_CASE("hls seek behaviour: libx264 as D-069's ladders are encoded, its first DTS two frames ahead, "
          "carries a segment's last two frames into the next; each offered its own segment's start",
          "[timeline][hls_seek_behaviour]")
{
    const QuietLog quiet;
    constexpr double kContinuous[3] = { 10.0, 12.0, 14.0 };
    const std::string path = write_playlist("x264_continuous", kContinuous, SegmentEncoder::X264);
    if (path.empty()) SKIP("no libx264 in this FFmpeg");
    Opened o;
    open_playlist(path, o);
    CHECK(same_seconds(o.start, 10.0));
    CHECK(same_seconds(o.duration, 6.0));
    CHECK(same_seconds(o.lead, 0.08));   // the first packet's DTS two frames before its PTS
    CHECK(o.video_delay == 2);           // the decoder's reorder delay, those two frames
    const timeline::VariantClock c = clock_of(kContinuous);

    check_seek(o, c, 11.0, 11.0, 0);
    check_seek(o, c, 11.88, 11.88, 0);   // the third-to-last frame: its own
    // The second-to-last frame and the last: the next segment's first.
    check_seek(o, c, 11.92, 12.0, 1);
    check_seek(o, c, 11.96, 12.0, 1);
    check_seek(o, c, 13.92, 14.0, 2);
    check_seek(o, c, 15.92, 15.92, 2);   // the clamp: the last segment, the frame awaited

    // The panel's decision with the Demuxer's reorder delay: the
    // second-to-last frame offered its own segment's start, which a seek
    // shows, the one before it sought by its PTS.
    const timeline::SeekClock seek = panel_clock(o);
    timeline::SeekDecision d = timeline::decide_seek(c, seek, 48);
    CHECK(d.kind == timeline::SeekDecision::Kind::Offer);
    CHECK(d.segment == std::optional<std::size_t>{ 0 });
    CHECK(same_seconds(d.target, 10.0));
    CHECK(timeline::decide_seek(c, seek, 47).kind == timeline::SeekDecision::Kind::Seek);
    CHECK(check_decisions(o, c) == frame_list({ 48, 49, 98, 99 }));

    // The ad shapes on libx264: the ad restarting at the main content's
    // origin, D-069's, and from 1 s.
    constexpr double kOrigin[3] = { 10.0, 10.0, 14.0 };
    Opened origin;
    open_playlist(write_playlist("x264_origin", kOrigin, SegmentEncoder::X264), origin);
    CHECK(same_seconds(origin.lead, 0.08));
    CHECK(origin.video_delay == 2);
    const timeline::VariantClock oc = clock_of(kOrigin);
    check_seek(origin, oc, 10.52, 10.52, 0);   // an ad's frame by its PTS: the main content's
    check_seek(origin, oc, 11.92, 14.0, 2);    // the first segment's second-to-last: past the ad
    CHECK(check_decisions(origin, oc) == frame_list({ 48, 49 }, 50, 100));

    constexpr double kReset[3] = { 10.0, 1.0, 12.0 };
    Opened reset;
    open_playlist(write_playlist("x264_reset", kReset, SegmentEncoder::X264), reset);
    CHECK(same_seconds(reset.lead, 0.08));
    CHECK(reset.video_delay == 2);
    const timeline::VariantClock rc = clock_of(kReset);
    check_seek(reset, rc, 13.88, 13.88, 2);
    check_seek(reset, rc, 13.92, 0, -1);       // the third segment's second-to-last: nothing
    CHECK(check_decisions(reset, rc) == frame_list({ 48, 49, 148, 149 }, 50, 100));
}
