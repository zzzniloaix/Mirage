// seek_probe.cpp: what FFmpeg's hls demuxer does when the player seeks
// into a media playlist with EXT-X-DISCONTINUITY tags. Library APIs only
// (libavformat, libavcodec, libavutil); no ffmpeg or ffprobe binary.
//
// It writes two small HLS VOD playlists of three 2 s MPEG-TS segments each
// (MPEG-2 video, 25 fps, 320x192, a keyframe every 25 frames, no B-frames):
//   reset:      seg0 PTS 10..12, seg1 (the "ad") PTS 0..2,   seg2 PTS 12..14
//   continuous: seg0 PTS 10..12, seg1 (the "ad") PTS 12..14, seg2 PTS 14..16
// each with an EXT-X-DISCONTINUITY before seg1 and before seg2. Then it opens
// each playlist, reads it through once (the PTS of each run of frames), and
// seeks it to a list of targets two ways:
//   ffmpeg: av_seek_frame(fc, -1, t * AV_TIME_BASE, AVSEEK_FLAG_BACKWARD), as
//           <repo>/src/Demuxer.cpp:505 calls it for a plain seek;
//   player: the same after Demuxer.cpp:487-495's clamp of the target to
//           [start_time, start_time + duration - 0.5] and :510's second try
//           at start_time + 1 s;
// and prints the first video packets av_read_frame() returns after each seek.
//
// Usage: seek_probe <dir>   (writes <dir>/reset and <dir>/continuous)

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/log.h>
}

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kFps = 25, kSegFrames = 50, kW = 320, kH = 192;

bool fail(const char* what, int r)
{
    char b[AV_ERROR_MAX_STRING_SIZE];
    av_strerror(r, b, sizeof b);
    std::printf("error: %s: %s\n", what, b);
    return false;
}

// One MPEG-TS segment of kSegFrames frames whose first PTS is first_s seconds.
bool write_segment(const std::string& path, double first_s, int luma)
{
    AVFormatContext* oc = nullptr;
    int r = avformat_alloc_output_context2(&oc, nullptr, "mpegts", path.c_str());
    if (r < 0) return fail("avformat_alloc_output_context2", r);
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
    if (!codec) return fail("avcodec_find_encoder", AVERROR_ENCODER_NOT_FOUND);
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    enc->width = kW;
    enc->height = kH;
    enc->pix_fmt = AV_PIX_FMT_YUV420P;
    enc->time_base = { 1, kFps };
    enc->framerate = { kFps, 1 };
    enc->gop_size = kFps;
    enc->max_b_frames = 0;
    enc->bit_rate = 400000;
    if ((r = avcodec_open2(enc, codec, nullptr)) < 0) return fail("avcodec_open2", r);
    AVStream* st = avformat_new_stream(oc, nullptr);
    st->time_base = { 1, 90000 };
    avcodec_parameters_from_context(st->codecpar, enc);
    if ((r = avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE)) < 0) return fail("avio_open", r);
    if ((r = avformat_write_header(oc, nullptr)) < 0) return fail("avformat_write_header", r);

    AVFrame* f = av_frame_alloc();
    f->format = enc->pix_fmt;
    f->width = kW;
    f->height = kH;
    av_frame_get_buffer(f, 0);
    AVPacket* pkt = av_packet_alloc();
    auto drain = [&] {
        while (avcodec_receive_packet(enc, pkt) == 0) {
            av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
            pkt->stream_index = st->index;
            av_interleaved_write_frame(oc, pkt);
        }
    };
    const int64_t first = std::llround(first_s * kFps);
    for (int i = 0; i < kSegFrames; ++i) {
        av_frame_make_writable(f);
        for (int y = 0; y < kH; ++y) std::memset(f->data[0] + y * f->linesize[0], luma + i % 8, kW);
        for (int y = 0; y < kH / 2; ++y) {
            std::memset(f->data[1] + y * f->linesize[1], 128, kW / 2);
            std::memset(f->data[2] + y * f->linesize[2], 128, kW / 2);
        }
        f->pts = first + i;
        avcodec_send_frame(enc, f);
        drain();
    }
    avcodec_send_frame(enc, nullptr);
    drain();
    av_write_trailer(oc);
    avio_closep(&oc->pb);
    av_packet_free(&pkt);
    av_frame_free(&f);
    avcodec_free_context(&enc);
    avformat_free_context(oc);
    return true;
}

bool write_case(const std::string& dir, const double (&starts)[3])
{
    std::filesystem::create_directories(dir);
    static constexpr int kLuma[3] = { 60, 200, 120 };
    for (int s = 0; s < 3; ++s)
        if (!write_segment(dir + "/seg" + std::to_string(s) + ".ts", starts[s], kLuma[s])) return false;
    std::ofstream m(dir + "/index.m3u8");
    m << "#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:2\n#EXT-X-MEDIA-SEQUENCE:0\n"
         "#EXT-X-PLAYLIST-TYPE:VOD\n"
         "#EXTINF:2.000,\nseg0.ts\n"
         "#EXT-X-DISCONTINUITY\n#EXTINF:2.000,\nseg1.ts\n"
         "#EXT-X-DISCONTINUITY\n#EXTINF:2.000,\nseg2.ts\n"
         "#EXT-X-ENDLIST\n";
    return static_cast<bool>(m);
}

// Prints the hls demuxer's segment requests (hls.c logs them at
// AV_LOG_VERBOSE) and every warning or error, without the context pointer.
void on_log(void* avcl, int level, const char* fmt, va_list vl)
{
    if (level > AV_LOG_VERBOSE) return;
    char line[1024];
    int prefix = 0;
    av_log_format_line2(avcl, level, fmt, vl, line, sizeof line, &prefix);
    if (level > AV_LOG_WARNING && !std::strstr(line, "HLS request for url")) return;
    const char* text = line;
    if (line[0] == '[') {
        if (const char* close = std::strchr(line, ']')) text = close + 1;
        while (*text == ' ') ++text;
    }
    std::printf("    log: %s", text);
    if (!*text || text[std::strlen(text) - 1] != '\n') std::printf("\n");
}

void first_packets(AVFormatContext* fc, int vi, int want)
{
    AVPacket* p = av_packet_alloc();
    const AVRational tb = fc->streams[vi]->time_base;
    for (int got = 0; got < want; ) {
        const int r = av_read_frame(fc, p);
        if (r < 0) {
            std::printf("    read: %s\n", r == AVERROR_EOF ? "end of file" : "error");
            break;
        }
        if (p->stream_index == vi) {
            std::printf("    packet pts %.3f dts %.3f%s\n", p->pts * av_q2d(tb), p->dts * av_q2d(tb),
                        (p->flags & AV_PKT_FLAG_KEY) ? " key" : "");
            ++got;
        }
        av_packet_unref(p);
    }
    av_packet_free(&p);
}

void seek_and_read(AVFormatContext* fc, int vi, double t, const char* how)
{
    int r = av_seek_frame(fc, -1, static_cast<int64_t>(t * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
    char b[AV_ERROR_MAX_STRING_SIZE] = "ok";
    if (r < 0) av_strerror(r, b, sizeof b);
    std::printf("  %s: av_seek_frame(%.3f s) -> %s\n", how, t, b);
    if (r >= 0) first_packets(fc, vi, 2);
}

bool probe(const std::string& playlist, const std::vector<double>& targets)
{
    std::printf("== %s\n", playlist.c_str());
    AVFormatContext* fc = nullptr;
    int r = avformat_open_input(&fc, playlist.c_str(), nullptr, nullptr);
    if (r < 0) return fail("avformat_open_input", r);
    if ((r = avformat_find_stream_info(fc, nullptr)) < 0) return fail("avformat_find_stream_info", r);
    const int vi = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (vi < 0) return fail("av_find_best_stream", vi);
    const double start = fc->start_time != AV_NOPTS_VALUE ? static_cast<double>(fc->start_time) / AV_TIME_BASE : 0.0;
    const double dur = fc->duration > 0 ? static_cast<double>(fc->duration) / AV_TIME_BASE : 0.0;
    std::printf("  start_time %.3f s, duration %.3f s, video time base %d/%d\n", start, dur,
                fc->streams[vi]->time_base.num, fc->streams[vi]->time_base.den);

    // Read through once: each run of frames whose PTS go up by one frame.
    std::printf("  read through from the start:\n");
    {
        AVPacket* p = av_packet_alloc();
        const AVRational tb = fc->streams[vi]->time_base;
        double run_first = NAN, prev = NAN;
        int run_n = 0;
        auto flush = [&] {
            if (run_n > 0) std::printf("    run of %d video packets, PTS %.3f .. %.3f\n", run_n, run_first, prev);
        };
        while (av_read_frame(fc, p) >= 0) {
            if (p->stream_index == vi && p->pts != AV_NOPTS_VALUE) {
                const double pts = p->pts * av_q2d(tb);
                if (run_n > 0 && std::fabs(pts - prev - 1.0 / kFps) > 0.5 / kFps) {
                    flush();
                    run_n = 0;
                }
                if (run_n == 0) run_first = pts;
                prev = pts;
                ++run_n;
            }
            av_packet_unref(p);
        }
        flush();
        av_packet_free(&p);
    }

    for (const double t : targets) {
        std::printf(" target %.3f s\n", t);
        seek_and_read(fc, vi, t, "ffmpeg");
        // src/Demuxer.cpp:487-495 and :510.
        double s = t;
        if (s < start) s = start;
        else if (dur > 0.0 && s > start + dur - 0.5) s = std::max(start, start + dur - 0.5);
        if (s != t) std::printf("  player: clamped to %.3f s\n", s);
        r = av_seek_frame(fc, -1, static_cast<int64_t>(s * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
        if (r < 0 && s < start + 1.0) {
            std::printf("  player: first try failed, second try at %.3f s\n", start + 1.0);
            s = start + 1.0;
        }
        seek_and_read(fc, vi, s, "player");
    }
    avformat_close_input(&fc);
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc != 2) {
        std::printf("usage: seek_probe <dir>\n");
        return 2;
    }
    const std::string dir = argv[1];
    std::printf("libavformat %s, libavcodec %s, FFmpeg %s\n", AV_STRINGIFY(LIBAVFORMAT_VERSION),
                AV_STRINGIFY(LIBAVCODEC_VERSION), av_version_info());
    av_log_set_callback(on_log);
    if (!write_case(dir + "/reset", { 10.0, 0.0, 12.0 })) return 1;
    if (!write_case(dir + "/continuous", { 10.0, 12.0, 14.0 })) return 1;
    std::printf("wrote %s/reset and %s/continuous\n", dir.c_str(), dir.c_str());
    // reset: a content frame before the ad (11.0), a frame of the ad by its
    // own PTS (0.5) and by its playlist position, first_timestamp + 2 s + 0.5
    // (12.5), content after the ad by its PTS (13.0) and by its playlist
    // position, first_timestamp + 4 s + 0.5 (14.5).
    bool ok = probe(dir + "/reset/index.m3u8", { 11.0, 0.5, 12.5, 13.0, 14.5 });
    // continuous: PTS and playlist position agree.
    ok = probe(dir + "/continuous/index.m3u8", { 11.0, 12.5, 14.5 }) && ok;
    return ok ? 0 : 1;
}
