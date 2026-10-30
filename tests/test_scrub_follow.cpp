// Following the playing video stream after a switch, against FFmpeg's hls
// demuxer on a generated local master: two video variants (64x48, 32x32),
// 2 s MPEG-TS segments with a keyframe every 0.4 s and B-frames (pts != dts,
// first frame at the muxer's ~1.4 s offset, as bipbop's at 10 s).
//  * Demuxer reports the keyframes it reads, and a forward seek to a
//    keyframe's dts lands on it even inside a segment, where a backward seek
//    lands on the segment's start.
//  * ScrubDecoder::retarget: frames come from the new stream (in place, or
//    through the new variant's playlist), a request not yet answered is
//    served again from it, and frames of older requests are not returned.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "Demuxer.h"
#include "FfOpen.h"
#include "HlsVariants.h"
#include "KeyframeIndex.h"
#include "ManifestParser.h"
#include "ScrubDecoder.h"
#include "TestTemp.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

namespace fs = std::filesystem;
using Catch::Matchers::WithinAbs;

namespace {

constexpr int kFps  = 25;
constexpr int kGop  = 10;    // keyframe every 0.4 s
constexpr int kSecs = 6;     // three 2 s segments

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

// ts_offset (seconds) shifts every timestamp, as bipbop's start at 10 s.
void make_video_playlist(const fs::path& dir, int w, int h, double ts_offset = 0.0)
{
    fs::create_directories(dir);
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
    REQUIRE(codec);
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    enc->width        = w;
    enc->height       = h;
    enc->pix_fmt      = AV_PIX_FMT_YUV420P;
    enc->time_base    = { 1, kFps };
    enc->framerate    = { kFps, 1 };
    enc->gop_size     = kGop;
    enc->max_b_frames = 2;
    enc->bit_rate     = 200000;
    enc->flags       |= AV_CODEC_FLAG_CLOSED_GOP;   // every keyframe starts its GOP in display order too
    AVDictionary* eo = nullptr;
    av_dict_set(&eo, "sc_threshold", "1000000000", 0);   // closed GOPs need scene-change detection off
    REQUIRE(avcodec_open2(enc, codec, &eo) >= 0);
    av_dict_free(&eo);

    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "hls", (dir / "index.m3u8").c_str()) >= 0);
    AVStream* st = avformat_new_stream(oc, nullptr);
    REQUIRE(avcodec_parameters_from_context(st->codecpar, enc) >= 0);
    st->time_base = enc->time_base;
    AVDictionary* o = nullptr;
    av_dict_set(&o, "hls_time", "2", 0);
    av_dict_set(&o, "hls_playlist_type", "vod", 0);
    av_dict_set(&o, "hls_segment_filename", (dir / "seg%d.ts").c_str(), 0);
    oc->output_ts_offset = static_cast<int64_t>(ts_offset * AV_TIME_BASE);
    REQUIRE(avformat_write_header(oc, &o) >= 0);
    av_dict_free(&o);

    AVFrame* f = av_frame_alloc();
    f->format = enc->pix_fmt;
    f->width  = w;
    f->height = h;
    REQUIRE(av_frame_get_buffer(f, 0) >= 0);
    for (int i = 0; i < kSecs * kFps; ++i) {
        REQUIRE(av_frame_make_writable(f) >= 0);
        for (int y = 0; y < h; ++y)
            std::memset(f->data[0] + y * f->linesize[0], (i * 7 + y * 3) & 0xFF, static_cast<size_t>(w));
        for (int p = 1; p < 3; ++p)
            for (int y = 0; y < h / 2; ++y)
                std::memset(f->data[p] + y * f->linesize[p], 128, static_cast<size_t>(w / 2));
        f->pts = i;
        write_frames(enc, oc, st, f);
    }
    write_frames(enc, oc, st, nullptr);
    REQUIRE(av_write_trailer(oc) >= 0);
    avformat_free_context(oc);
    av_frame_free(&f);
    avcodec_free_context(&enc);
}

fs::path make_master()
{
    const fs::path root = testtemp::path("mirage_test_scrub_follow");
    static bool made = false;   // once per test run, never a stale fixture
    if (made) return root;
    made = true;
    fs::remove_all(root);
    make_video_playlist(root / "v0", 64, 48);
    make_video_playlist(root / "v1", 32, 32);
    std::ofstream m(root / "master.m3u8");
    m << "#EXTM3U\n#EXT-X-VERSION:3\n"
         "#EXT-X-STREAM-INF:BANDWIDTH=300000,RESOLUTION=64x48\nv0/index.m3u8\n"
         "#EXT-X-STREAM-INF:BANDWIDTH=150000,RESOLUTION=32x32\nv1/index.m3u8\n";
    return root;
}

// One variant whose timestamps start at ~10 s.
fs::path make_offset_master()
{
    const fs::path root = testtemp::path("mirage_test_seek_offset");
    static bool made = false;
    if (made) return root;
    made = true;
    fs::remove_all(root);
    make_video_playlist(root / "v0", 64, 48, 10.0);
    std::ofstream m(root / "master.m3u8");
    m << "#EXTM3U\n#EXT-X-VERSION:3\n"
         "#EXT-X-STREAM-INF:BANDWIDTH=300000,RESOLUTION=64x48\nv0/index.m3u8\n";
    return root;
}

// The master's video streams, as playback's context has them.
struct Layout {
    int        s0 = -1, s1 = -1;   // 64x48, 32x32
    AVRational tb0{ 1, 1 }, tb1{ 1, 1 };
    double     start0 = 0.0;       // first frame of s0 (seconds)
    ffopen::VariantInput in0, in1;
};

Layout layout_of(const std::string& master)
{
    AVFormatContext* fc = nullptr;
    REQUIRE(avformat_open_input(&fc, master.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(avformat_find_stream_info(fc, nullptr) >= 0);
    Layout l;
    for (unsigned i = 0; i < fc->nb_streams; ++i) {
        const AVStream* s = fc->streams[i];
        if (s->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) continue;
        const ffopen::VariantInput in{ "", static_cast<int>(s->codecpar->codec_id), s->codecpar->width,
                                       s->codecpar->height };
        if (s->codecpar->width == 64) {
            l.s0 = static_cast<int>(i);
            l.tb0 = s->time_base;
            l.start0 = s->start_time * av_q2d(s->time_base);
            l.in0 = in;
        } else {
            l.s1 = static_cast<int>(i);
            l.tb1 = s->time_base;
            l.in1 = in;
        }
    }
    avformat_close_input(&fc);
    const fs::path root = fs::path(master).parent_path();
    l.in0.url = (root / "v0" / "index.m3u8").string();
    l.in1.url = (root / "v1" / "index.m3u8").string();
    REQUIRE(l.s0 >= 0);
    REQUIRE(l.s1 >= 0);
    return l;
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

// The next frame ScrubDecoder posts for requests from `serial` on.
struct Polled {
    AVFrame* f      = nullptr;
    int      stream = -1;
    double   pts    = -1.0;
    ~Polled() { av_frame_free(&f); }
};
void poll(ScrubDecoder& sd, std::uint64_t serial, Polled& out)
{
    REQUIRE(wait_for([&] { return (out.f = sd.poll_frame(serial, &out.stream)) != nullptr; }));
    REQUIRE(out.f->pts != AV_NOPTS_VALUE);
    REQUIRE(out.f->time_base.num > 0);
    out.pts = out.f->pts * av_q2d(out.f->time_base);
}

// Packets the demuxer routes to a queue: sentinel or (pts, key).
struct Seen {
    bool   sentinel = false;
    double pts      = 0.0;
    bool   key      = false;
};
struct Collector {
    Queue<AVPacket*>& q;
    const Demuxer&    d;
    std::mutex        mtx;
    std::vector<Seen> seen;
    std::jthread      th;
    Collector(Queue<AVPacket*>& q_, const Demuxer& d_) : q(q_), d(d_)
    {
        th = std::jthread([this](std::stop_token st) {
            while (!st.stop_requested()) {
                AVPacket* p = nullptr;
                if (!q.pop(p)) break;
                if (p == nullptr) continue;   // end of media: seeks still come
                Seen s;
                if (p == flush_sentinel()) {
                    s.sentinel = true;
                } else {
                    s.pts = p->pts * av_q2d(d.fmt_ctx()->streams[p->stream_index]->time_base);
                    s.key = (p->flags & AV_PKT_FLAG_KEY) != 0;
                    av_packet_free(&p);
                }
                std::lock_guard lk(mtx);
                seen.push_back(s);
            }
        });
    }
    std::vector<Seen> snapshot() { std::lock_guard lk(mtx); return seen; }
};

} // namespace

TEST_CASE("Demuxer reports keyframes read; a forward seek lands on one inside an HLS segment",
          "[scrub_follow][keyframes][ffmpeg]")
{
    const fs::path root = make_master();
    const std::string master = (root / "master.m3u8").string();
    const Layout l = layout_of(master);

    Demuxer d;
    REQUIRE(d.open(master));
    REQUIRE(d.is_hls());
    const int v = d.video_stream_index();
    REQUIRE(v == l.s0);   // best stream: the larger one

    KeyframeIndex idx;
    idx.set_hls(true);
    const AVStream* vs = d.fmt_ctx()->streams[v];
    idx.set_stream(v, vs->start_time * av_q2d(vs->time_base), 1.0 / kFps);
    std::string text;
    REQUIRE(ManifestParser::fetch(l.in0.url, text));
    const hls::MediaPlaylist pl = hls::parse_media_playlist(text);
    REQUIRE(pl.vod);
    REQUIRE(pl.durations.size() == 3);
    idx.set_segments(v, pl.durations);
    d.set_keyframe_index(&idx);

    Queue<AVPacket*> videoq{ 512 }, audioq{ 16 };
    Collector vc(videoq, d);
    std::jthread reader([&](std::stop_token st) { d.read_loop(st, videoq, audioq, nullptr); });
    struct Unblock { Queue<AVPacket*>& v; Queue<AVPacket*>& a; ~Unblock() { v.shutdown(); a.shutdown(); } } unblock{ videoq, audioq };

    // The whole file read once: every keyframe, every 0.4 s from the start.
    REQUIRE(wait_for([&] { return vc.snapshot().size() >= static_cast<std::size_t>(kSecs * kFps); }));
    const auto kfs = idx.list(v);
    REQUIRE(kfs.size() == static_cast<std::size_t>(kSecs * kFps / kGop));
    for (std::size_t k = 0; k < kfs.size(); ++k) {
        CHECK_THAT(kfs[k].pts, WithinAbs(l.start0 + 0.4 * static_cast<double>(k), 1e-6));
        CHECK(kfs[k].at_or_after);
        CHECK(kfs[k].seek_s < kfs[k].pts);   // B-frames: dts before pts
        CHECK_FALSE(kfs[k].predicted);
    }
    // The playlist's segment starts (every 2 s) were all read where predicted.
    const auto st = idx.status(v);
    CHECK(st.trusted);
    CHECK(st.confirmed == 2);
    CHECK(st.contradicted == 0);

    // Keyframe 7 is inside segment 1 (2.8 s into the stream, segment 1
    // starts at 2.0 s).
    const KeyframeIndex::Keyframe k7 = kfs[7];
    auto first_after_seek = [&](std::size_t mark) {
        double pts = -1.0;
        REQUIRE(wait_for([&] {
            const auto s = vc.snapshot();
            for (std::size_t i = mark; i < s.size(); ++i)
                if (s[i].sentinel && i + 1 < s.size()) { pts = s[i + 1].pts; return true; }
            return false;
        }));
        return pts;
    };
    SECTION("forward to its dts: lands on it") {
        const std::size_t mark = vc.snapshot().size();
        d.request_seek(k7.seek_s, /*key_at_or_after*/ true);
        CHECK_THAT(first_after_seek(mark), WithinAbs(k7.pts, 1e-6));
    }
    SECTION("backward to its pts: lands on the segment's start (FFmpeg 8.1 hls_read_seek)") {
        const std::size_t mark = vc.snapshot().size();
        d.request_seek(k7.pts);
        CHECK_THAT(first_after_seek(mark), WithinAbs(kfs[5].pts, 1e-6));
    }

    reader.request_stop();
    videoq.shutdown();
    audioq.shutdown();
    reader.join();
    vc.th.request_stop();
    vc.th.join();
    AVPacket* p = nullptr;
    while (videoq.try_pop(p))
        if (p && p != flush_sentinel()) av_packet_free(&p);
}

TEST_CASE("ScrubDecoder follows a retarget", "[scrub_follow][ffmpeg]")
{
    const fs::path root = make_master();
    const std::string master = (root / "master.m3u8").string();
    const Layout l = layout_of(master);
    const double t = l.start0 + 2.5;   // inside segment 1

    SECTION("in place: a context opened like playback has every stream") {
        ScrubDecoder sd;
        sd.open(master, l.s0, l.tb0);
        Polled a;
        poll(sd, sd.request(t), a);
        CHECK(a.f->width == 64);
        CHECK(a.stream == l.s0);
        CHECK(a.pts >= t - 0.001);
        CHECK(a.pts < t + 1.0 / kFps);

        sd.retarget(l.s1, l.tb1);
        CHECK(sd.stream() == l.s1);
        Polled b;
        poll(sd, sd.request(t), b);
        CHECK(b.f->width == 32);
        CHECK(b.stream == l.s1);
        CHECK(av_cmp_q(b.f->time_base, l.tb1) == 0);
        CHECK_THAT(b.pts, WithinAbs(a.pts, 1e-6));   // same timeline
    }
    SECTION("through the variant playlists") {
        ScrubDecoder sd;
        sd.open(master, l.s0, l.tb0, l.in0);
        Polled a;
        poll(sd, sd.request(t), a);
        CHECK(a.f->width == 64);
        sd.retarget(l.s1, l.tb1, l.in1);
        Polled b;
        poll(sd, sd.request(t + 1.0), b);
        CHECK(b.f->width == 32);
        CHECK(b.stream == l.s1);
        CHECK(b.pts >= t + 1.0 - 0.001);
        CHECK(b.pts < t + 1.0 + 1.0 / kFps);
        // And back.
        sd.retarget(l.s0, l.tb0, l.in0);
        Polled c;
        poll(sd, sd.request(t), c);
        CHECK(c.f->width == 64);
        CHECK(c.stream == l.s0);
    }
    SECTION("a request not answered yet is decoded again from the new stream") {
        ScrubDecoder sd;
        sd.open(master, l.s0, l.tb0, l.in0);
        Polled warm;
        poll(sd, sd.request(t), warm);   // opened
        const std::uint64_t serial = sd.request(t - 1.0);
        sd.retarget(l.s1, l.tb1, l.in1);   // before its frame is polled
        Polled b;
        poll(sd, serial, b);
        CHECK(b.f->width == 32);
        CHECK(b.pts >= t - 1.0 - 0.001);
        CHECK(b.pts < t - 1.0 + 1.0 / kFps);
    }
    SECTION("frames of older requests are not returned") {
        ScrubDecoder sd;
        sd.open(master, l.s0, l.tb0);
        Polled warm;
        poll(sd, sd.request(t), warm);
        sd.request(t - 1.5);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));   // its frame is posted
        const std::uint64_t serial = sd.request(t + 1.0);
        Polled b;
        poll(sd, serial, b);
        CHECK(b.pts >= t + 1.0 - 0.001);
        CHECK(b.pts < t + 1.0 + 1.0 / kFps);
    }
}

namespace {

// Keyframes (pts, dts in seconds) of `url`'s video stream, read packet by packet.
std::vector<std::pair<double, double>> keyframes_of(const std::string& url)
{
    AVFormatContext* fc = nullptr;
    REQUIRE(avformat_open_input(&fc, url.c_str(), nullptr, nullptr) >= 0);
    REQUIRE(avformat_find_stream_info(fc, nullptr) >= 0);
    const int v = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    REQUIRE(v >= 0);
    const AVRational tb = fc->streams[v]->time_base;
    std::vector<std::pair<double, double>> out;
    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(fc, pkt) >= 0) {
        if (pkt->stream_index == v && (pkt->flags & AV_PKT_FLAG_KEY))
            out.emplace_back(pkt->pts * av_q2d(tb), pkt->dts * av_q2d(tb));
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&fc);
    return out;
}

} // namespace

TEST_CASE("ScrubDecoder decodes from the keyframe read before the target", "[scrub_follow][keyframes][ffmpeg]")
{
    const fs::path root = make_master();
    const std::string master = (root / "master.m3u8").string();
    const Layout l = layout_of(master);
    // 0.1 s after keyframe 7 (2.8 s): its segment (from 2.0 s) starts 0.9 s earlier.
    const double t = l.start0 + 2.9;

    KeyframeIndex idx;
    idx.set_hls(true);
    idx.set_stream(l.s0, l.start0, 1.0 / kFps);
    const auto kfs = keyframes_of(l.in0.url);
    REQUIRE(kfs.size() == static_cast<std::size_t>(kSecs * kFps / kGop));
    const std::uint32_t run = idx.new_run();
    for (const auto& [p, d] : kfs) idx.observe(l.s0, p, d, true, run, false);

    SECTION("lands on it with a forward seek to its dts") {
        ScrubDecoder sd;
        sd.set_keyframes(&idx);
        sd.open(master, l.s0, l.tb0, l.in0);
        Polled a;
        poll(sd, sd.request(t), a);
        CHECK(a.pts >= t - 0.001);
        CHECK(a.pts < t + 1.0 / kFps);
        CHECK(sd.counters().from_key == 1);
        CHECK(sd.counters().missed == 0);
        // Before any keyframe listed: the decoder's own seek.
        Polled b;
        poll(sd, sd.request(l.start0 - 0.5), b);
        CHECK(sd.counters().from_seek == 1);
    }
    SECTION("a keyframe that is not where listed: falls back to the decoder's seek") {
        KeyframeIndex bad;
        bad.set_hls(true);
        bad.set_stream(l.s0, l.start0, 1.0 / kFps);
        // Between keyframes 7 and 8: the forward seek lands on 8, past t.
        bad.observe(l.s0, l.start0 + 2.86, kfs[7].second + 0.06, true, bad.new_run(), false);
        ScrubDecoder sd;
        sd.set_keyframes(&bad);
        sd.open(master, l.s0, l.tb0, l.in0);
        Polled a;
        poll(sd, sd.request(t), a);
        CHECK(a.pts >= t - 0.001);
        CHECK(a.pts < t + 1.0 / kFps);
        CHECK(sd.counters().missed == 1);
    }
}

// ── Keyframe probes ──────────────────────────────────────────────────────────
// The fixture's segments (2 s) hold five keyframes each (every 0.4 s):
// stepping back from a segment's first frame into a segment not read, only
// that segment's start is known. A probe reads it; the step then goes to the
// keyframe 0.4 s back, not to the segment start 2 s back.

namespace {

// Index for stream s0 of the fixture, with its playlist's segments, and the
// open's read of segment 0 up to segment 1's first keyframe (confirming the
// segment starts). Then playback lands on segment 2's start (`t`).
struct ProbeFixture {
    Layout                                 l;
    std::string                            master;
    std::vector<std::pair<double, double>> kfs;   // pts, dts
    KeyframeIndex                          idx;
    double                                 t = 0.0;   // segment 2's first frame

    explicit ProbeFixture(bool with_segments)
    {
        const fs::path root = make_master();
        master = (root / "master.m3u8").string();
        l      = layout_of(master);
        kfs    = keyframes_of(l.in0.url);
        REQUIRE(kfs.size() == static_cast<std::size_t>(kSecs * kFps / kGop));
        idx.set_hls(true);
        idx.set_stream(l.s0, l.start0, 1.0 / kFps);
        if (with_segments) {
            std::string text;
            REQUIRE(ManifestParser::fetch(l.in0.url, text));
            idx.set_segments(l.s0, hls::parse_media_playlist(text).durations);
            const std::uint32_t open = idx.new_run();
            for (int k = 0; k <= 5; ++k) idx.observe(l.s0, kfs[k].first, kfs[k].second, true, open, k == 0);
            REQUIRE(idx.status(l.s0).trusted);
        }
        t = kfs[10].first;
        idx.observe(l.s0, kfs[10].first, kfs[10].second, true, idx.new_run(), true);
    }
};

ScrubDecoder::ProbeState wait_probe(const ScrubDecoder& sd, std::uint64_t serial)
{
    ScrubDecoder::ProbeState s = ScrubDecoder::ProbeState::Pending;
    REQUIRE(wait_for([&] { return (s = sd.probe_state(serial)) != ScrubDecoder::ProbeState::Pending; }));
    return s;
}

} // namespace

TEST_CASE("ScrubDecoder::probe reads a segment not read: the step back lands on its last keyframe",
          "[scrub_follow][keyframes][ffmpeg]")
{
    SECTION("from the keyframe read before it (segment starts known)") {
        ProbeFixture f(true);
        KeyframeIndex::Step q = f.idx.step(f.l.s0, f.t, false);
        REQUIRE(q.kf);
        CHECK_FALSE(q.certain);
        CHECK_THAT(q.kf->pts, WithinAbs(f.kfs[5].first, 1e-6));   // segment 1's start, 2 s back

        ScrubDecoder sd;
        sd.set_keyframes(&f.idx);
        sd.open(f.master, f.l.s0, f.l.tb0, f.l.in0);
        CHECK(wait_probe(sd, sd.probe(f.l.s0, q.probe)) == ScrubDecoder::ProbeState::Done);

        q = f.idx.step(f.l.s0, f.t, false);
        REQUIRE(q.kf);
        CHECK(q.certain);
        CHECK_THAT(q.kf->pts, WithinAbs(f.kfs[9].first, 1e-6));   // 0.4 s back
        CHECK_THAT(q.kf->seek_s, WithinAbs(f.kfs[9].second, 1e-6));
        // Every keyframe of segment 1 is listed now, and on back is certain.
        for (int k = 9; k > 5; --k) {
            q = f.idx.step(f.l.s0, f.kfs[k].first, false);
            REQUIRE(q.kf);
            CHECK(q.certain);
            CHECK_THAT(q.kf->pts, WithinAbs(f.kfs[k - 1].first, 1e-6));
        }
        CHECK(f.idx.status(f.l.s0).contradicted == 0);
    }
    SECTION("nothing known before it: from where a backward seek before it lands") {
        ProbeFixture f(false);
        KeyframeIndex::Step q = f.idx.step(f.l.s0, f.t, false);
        CHECK_FALSE(q.kf);
        CHECK_FALSE(q.certain);

        ScrubDecoder sd;
        sd.set_keyframes(&f.idx);
        sd.open(f.master, f.l.s0, f.l.tb0, f.l.in0);
        CHECK(wait_probe(sd, sd.probe(f.l.s0, q.probe)) == ScrubDecoder::ProbeState::Done);

        q = f.idx.step(f.l.s0, f.t, false);
        REQUIRE(q.kf);
        CHECK(q.certain);
        CHECK_THAT(q.kf->pts, WithinAbs(f.kfs[9].first, 1e-6));
    }
    SECTION("a backward seek landing on the frame stepped from reads the segment before") {
        ProbeFixture f(false);
        KeyframeIndex::Probe p;
        p.seek_s        = f.t;   // lands on segment 2's start: t itself
        p.segment_start = true;
        p.until         = f.t;
        p.frame_s       = 1.0 / kFps;
        ScrubDecoder sd;
        sd.set_keyframes(&f.idx);
        sd.open(f.master, f.l.s0, f.l.tb0, f.l.in0);
        CHECK(wait_probe(sd, sd.probe(f.l.s0, p)) == ScrubDecoder::ProbeState::Done);
        const KeyframeIndex::Step q = f.idx.step(f.l.s0, f.t, false);
        REQUIRE(q.kf);
        CHECK(q.certain);
        CHECK_THAT(q.kf->pts, WithinAbs(f.kfs[9].first, 1e-6));
    }
}

TEST_CASE("ScrubDecoder::probe reads on to the next keyframe", "[scrub_follow][keyframes][ffmpeg]")
{
    ProbeFixture f(false);
    KeyframeIndex::Step q = f.idx.step(f.l.s0, f.t, true);
    CHECK_FALSE(q.kf);   // nothing read after the landing yet
    CHECK_FALSE(q.certain);
    CHECK(q.probe.at_or_after);   // from the keyframe on screen

    ScrubDecoder sd;
    sd.set_keyframes(&f.idx);
    sd.open(f.master, f.l.s0, f.l.tb0, f.l.in0);
    CHECK(wait_probe(sd, sd.probe(f.l.s0, q.probe)) == ScrubDecoder::ProbeState::Done);
    q = f.idx.step(f.l.s0, f.t, true);
    REQUIRE(q.kf);
    CHECK(q.certain);
    CHECK_THAT(q.kf->pts, WithinAbs(f.kfs[11].first, 1e-6));
}

TEST_CASE("ScrubDecoder::probe fails rather than guess", "[scrub_follow][keyframes][ffmpeg]")
{
    ProbeFixture f(true);
    const KeyframeIndex::Step q = f.idx.step(f.l.s0, f.t, false);

    SECTION("no decoder thread") {
        ScrubDecoder sd;
        sd.set_keyframes(&f.idx);
        CHECK(sd.probe_state(sd.probe(f.l.s0, q.probe)) == ScrubDecoder::ProbeState::Failed);
    }
    SECTION("a stream it does not decode") {
        ScrubDecoder sd;
        sd.set_keyframes(&f.idx);
        sd.open(f.master, f.l.s0, f.l.tb0, f.l.in0);
        CHECK(wait_probe(sd, sd.probe(f.l.s1, q.probe)) == ScrubDecoder::ProbeState::Failed);
    }
    SECTION("superseded by a newer probe") {
        ScrubDecoder sd;
        sd.set_keyframes(&f.idx);
        sd.open(f.master, f.l.s0, f.l.tb0, f.l.in0);
        const std::uint64_t a = sd.probe(f.l.s0, q.probe);
        const std::uint64_t b = sd.probe(f.l.s0, q.probe);
        CHECK(wait_probe(sd, b) == ScrubDecoder::ProbeState::Done);
        CHECK(sd.probe_state(a) == ScrubDecoder::ProbeState::Failed);
    }
}

TEST_CASE("ScrubDecoder reports what its scrubs read", "[scrub_follow][keyframes][ffmpeg]")
{
    ProbeFixture f(true);
    ScrubDecoder sd;
    sd.set_keyframes(&f.idx);
    sd.open(f.master, f.l.s0, f.l.tb0, f.l.in0);
    // 0.1 s after keyframe 8, in segment 1 (not read): decoded from its start.
    const double target = f.kfs[8].first + 0.1;
    Polled a;
    poll(sd, sd.request(target), a);
    const KeyframeIndex::Step q = f.idx.step(f.l.s0, a.pts, false);
    REQUIRE(q.kf);
    CHECK(q.certain);
    CHECK_THAT(q.kf->pts, WithinAbs(f.kfs[8].first, 1e-6));
}

TEST_CASE("Demuxer: a seek before the first HLS segment or past the last stays inside the media",
          "[scrub_follow][seek][ffmpeg]")
{
    const fs::path root = make_offset_master();
    const std::string master = (root / "master.m3u8").string();
    double start0 = -1.0;
    {
        AVFormatContext* fc = nullptr;
        REQUIRE(avformat_open_input(&fc, master.c_str(), nullptr, nullptr) >= 0);
        REQUIRE(avformat_find_stream_info(fc, nullptr) >= 0);
        start0 = static_cast<double>(fc->start_time) / AV_TIME_BASE;
        avformat_close_input(&fc);
    }
    REQUIRE(start0 > 9.0);   // timestamps start late, as bipbop's do

    Demuxer d;
    REQUIRE(d.open(master));
    Queue<AVPacket*> videoq{ 512 }, audioq{ 16 };
    Collector vc(videoq, d);
    std::jthread reader([&](std::stop_token st) { d.read_loop(st, videoq, audioq, nullptr); });
    struct Unblock { Queue<AVPacket*>& v; Queue<AVPacket*>& a; ~Unblock() { v.shutdown(); a.shutdown(); } } unblock{ videoq, audioq };

    // Read up to the last segment, so a seek that failed would show by
    // packets continuing from there.
    REQUIRE(wait_for([&] {
        const auto s = vc.snapshot();
        return !s.empty() && s.back().pts > start0 + 4.5;
    }));
    // Packets after the newest sentinel: (first pts, smallest pts).
    auto after_seek = [&](std::size_t mark) {
        std::pair<double, double> out{ -1.0, 1e9 };
        REQUIRE(wait_for([&] {
            const auto s = vc.snapshot();
            std::size_t at = s.size();
            for (std::size_t i = mark; i < s.size(); ++i)
                if (s[i].sentinel) at = i;
            if (at + 3 >= s.size()) return false;
            out = { s[at + 1].pts, 1e9 };
            for (std::size_t i = at + 1; i < s.size(); ++i) out.second = std::min(out.second, s[i].pts);
            return true;
        }));
        return out;
    };

    SECTION("before the first segment: from its first frame") {
        const std::size_t mark = vc.snapshot().size();
        d.request_seek(0.0, false, 0.0);
        const auto [first, lowest] = after_seek(mark);
        CHECK(lowest < start0 + 0.5);   // back at the start, not on from where it was
        CHECK(first < start0 + 0.5);
        CHECK(d.video_show_from().from < 0.0);   // the first frame shows: nothing before it to skip
    }
    SECTION("past the last segment: into it, everything before the target") {
        const std::size_t mark = vc.snapshot().size();
        d.request_seek(start0 + 100.0, false, start0 + 100.0);
        const auto [first, lowest] = after_seek(mark);
        CHECK(first >= start0 + 4.0 - 1e-6);   // the last 2 s segment
        CHECK_THAT(d.video_show_from().from, WithinAbs(start0 + 100.0, 1e-9));
    }

    reader.request_stop();
    videoq.shutdown();
    audioq.shutdown();
    reader.join();
    vc.th.request_stop();
    vc.th.join();
    AVPacket* p = nullptr;
    while (videoq.try_pop(p))
        if (p && p != flush_sentinel()) av_packet_free(&p);
}

TEST_CASE("ScrubDecoder: a scrub before the first HLS segment or past the last stays inside the media",
          "[scrub_follow][seek][keyframes][ffmpeg]")
{
    const fs::path root = make_offset_master();
    const std::string master = (root / "master.m3u8").string();
    int                  s0 = -1;
    AVRational           tb{ 1, 1 };
    double               start0 = -1.0;
    ffopen::VariantInput in;
    {
        AVFormatContext* fc = nullptr;
        REQUIRE(avformat_open_input(&fc, master.c_str(), nullptr, nullptr) >= 0);
        REQUIRE(avformat_find_stream_info(fc, nullptr) >= 0);
        for (unsigned i = 0; i < fc->nb_streams; ++i) {
            const AVStream* s = fc->streams[i];
            if (s->codecpar->codec_type != AVMEDIA_TYPE_VIDEO) continue;
            s0     = static_cast<int>(i);
            tb     = s->time_base;
            start0 = s->start_time * av_q2d(s->time_base);
            in     = { (root / "v0" / "index.m3u8").string(), static_cast<int>(s->codecpar->codec_id),
                       s->codecpar->width, s->codecpar->height };
        }
        avformat_close_input(&fc);
    }
    REQUIRE(s0 >= 0);
    REQUIRE(start0 > 9.0);   // timestamps start late, as bipbop's do

    // What the scrubs read goes to the index, as in playback; a read taken
    // for a landing on a segment start where there is none contradicts it.
    KeyframeIndex idx;
    idx.set_hls(true);
    idx.set_stream(s0, start0, 1.0 / kFps);
    std::string text;
    REQUIRE(ManifestParser::fetch(in.url, text));
    idx.set_segments(s0, hls::parse_media_playlist(text).durations);
    REQUIRE(idx.status(s0).aligned);

    ScrubDecoder sd;
    sd.set_keyframes(&idx);
    sd.open(master, s0, tb, in);
    // Into the last segment first: a seek that failed would show by
    // decoding on from there.
    Polled a;
    poll(sd, sd.request(start0 + 4.5), a);
    CHECK(a.pts >= start0 + 4.5 - 0.001);

    SECTION("before the first segment (the seek bar's 0): its first frame") {
        Polled b;
        poll(sd, sd.request(0.0), b);
        CHECK_THAT(b.pts, WithinAbs(start0, 0.001));
        // Stepping back from the first frame asks for a frame before it.
        Polled c;
        poll(sd, sd.request(start0 - 1.5 / kFps), c);
        CHECK_THAT(c.pts, WithinAbs(start0, 0.001));
    }
    SECTION("past the last frame: the last frame") {
        Polled b;
        poll(sd, sd.request(start0 + 100.0), b);
        CHECK_THAT(b.pts, WithinAbs(start0 + kSecs - 1.0 / kFps, 0.001));
        // And back inside: decoding goes on as before.
        Polled c;
        poll(sd, sd.request(start0 + 2.5), c);
        CHECK(c.pts >= start0 + 2.5 - 0.001);
        CHECK(c.pts < start0 + 2.5 + 1.0 / kFps);
    }
    CHECK(idx.status(s0).contradicted == 0);
}
