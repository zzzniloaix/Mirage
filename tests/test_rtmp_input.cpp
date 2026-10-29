// rtmp inputs (DECISIONS.md D-056): the analyzer, the player's demuxer and
// its helpers' opens connect to an rtmp URL, with FFmpeg's rw_timeout alone,
// where FFmpeg's timeout would make them listen on it. The server is FFmpeg's
// own rtmp protocol in its listen mode, in this process: it waits for one
// client to connect and play, sends it the packets of a local FLV file
// coded by FFmpeg's own Sorenson H.263 encoder, and closes the connection.

#include <catch2/catch_test_macros.hpp>

#include "Demuxer.h"
#include "FfOpen.h"
#include "SilentServer.h"
#include "TestTemp.h"
#include "VMAFAnalyzer.h"
#include "VMAFReport.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <netinet/tcp_fsm.h>
#endif

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <stop_token>
#include <string>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace fs = std::filesystem;

namespace {

constexpr int kFps = 25;

// A clip of `frames` frames of w x h at 25 fps in FLV: a gray diagonal ramp
// that moves every frame, every frame a key frame at a fixed quantizer.
std::string make_flv_clip(const char* name, int w, int h, int frames)
{
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_FLV1);
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
    REQUIRE(avcodec_open2(enc, codec, nullptr) >= 0);

    const fs::path path = testtemp::path(name);
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "flv", path.c_str()) >= 0);
    AVStream* st = avformat_new_stream(oc, nullptr);
    REQUIRE(avcodec_parameters_from_context(st->codecpar, enc) >= 0);
    st->time_base = { 1, 1000 };
    REQUIRE(avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0);
    REQUIRE(avformat_write_header(oc, nullptr) >= 0);

    const auto drain = [&] {
        AVPacket* pkt = av_packet_alloc();
        while (avcodec_receive_packet(enc, pkt) == 0) {
            pkt->stream_index = 0;
            av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
            REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
        }
        av_packet_free(&pkt);
    };
    AVFrame* f = av_frame_alloc();
    f->format = AV_PIX_FMT_YUV420P;
    f->width  = w;
    f->height = h;
    REQUIRE(av_frame_get_buffer(f, 0) >= 0);
    for (int k = 0; k < frames; ++k) {
        REQUIRE(av_frame_make_writable(f) >= 0);
        for (int row = 0; row < h; ++row)
            for (int col = 0; col < w; ++col)
                f->data[0][row * f->linesize[0] + col] =
                    static_cast<uint8_t>((col * 256 / w + row * 128 / h + 3 * k) & 0xff);
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
    REQUIRE(av_write_trailer(oc) >= 0);
    avio_closep(&oc->pb);
    avformat_free_context(oc);
    avcodec_free_context(&enc);
    return path.string();
}

const std::string& flv_clip()
{
    static const std::string p = make_flv_clip("mirage_test_rtmp.flv", 96, 54, 36);
    return p;
}

// Whether `fd` is a TCP socket that listens: TCP_CONNECTION_INFO's state on
// macOS, which has no SO_ACCEPTCONN to read, SO_ACCEPTCONN on Linux.
bool listens(int fd)
{
#if defined(__APPLE__)
    tcp_connection_info info{};
    socklen_t n = sizeof info;
    return ::getsockopt(fd, IPPROTO_TCP, TCP_CONNECTION_INFO, &info, &n) == 0 &&
           info.tcpi_state == TCPS_LISTEN;
#else
    int listening = 0;
    socklen_t n = sizeof listening;
    return ::getsockopt(fd, SOL_SOCKET, SO_ACCEPTCONN, &listening, &n) == 0 && listening;
#endif
}

// A port of 127.0.0.1 the system had free a moment ago; 0 if none.
int free_port()
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof addr;
    const bool ok = fd >= 0 &&
                    ::bind(fd, reinterpret_cast<const sockaddr*>(&addr), sizeof addr) == 0 &&
                    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0;
    if (fd >= 0) ::close(fd);
    return ok ? ntohs(addr.sin_port) : 0;
}

// An rtmp server on 127.0.0.1 for one client: FFmpeg's rtmp protocol in its
// listen mode, which binds the port itself, sending the packets of `flv` to
// the client that connects and plays, as fast as it reads them. A wait for
// the client ends when the server is destroyed.
class RtmpServer {
public:
    explicit RtmpServer(std::string flv) : flv_(std::move(flv)), port_(free_port())
    {
        if (port_ > 0) thread_ = std::jthread([this](std::stop_token st) { serve(st); });
    }

    RtmpServer(const RtmpServer&)            = delete;
    RtmpServer& operator=(const RtmpServer&) = delete;

    std::string url() const { return std::format("rtmp://127.0.0.1:{}/live/clip", port_); }
    bool served() const { return played_.load(); }   // a client connected and played

    // Waits until FFmpeg listens on the port, at most `limit`: until one of
    // this process's descriptors is a socket listening on it.
    bool wait_listening(std::chrono::milliseconds limit) const
    {
        const auto end = std::chrono::steady_clock::now() + limit;
        while (port_ > 0 && std::chrono::steady_clock::now() < end) {
            for (int fd = 0; fd < 4096; ++fd) {
                sockaddr_in addr{};
                socklen_t len = sizeof addr;
                if (listens(fd) &&
                    ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) == 0 &&
                    addr.sin_family == AF_INET && ntohs(addr.sin_port) == port_)
                    return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        return false;
    }

private:
    static int stop_requested(void* opaque)
    {
        return static_cast<const std::stop_token*>(opaque)->stop_requested() ? 1 : 0;
    }

    void serve(std::stop_token st)
    {
        AVFormatContext* in = nullptr;
        if (avformat_open_input(&in, flv_.c_str(), nullptr, nullptr) < 0) return;
        AVFormatContext* out = nullptr;
        if (avformat_find_stream_info(in, nullptr) >= 0 &&
            avformat_alloc_output_context2(&out, nullptr, "flv", url().c_str()) >= 0) {
            bool ok = true;
            for (unsigned i = 0; i < in->nb_streams && ok; ++i) {
                AVStream* os = avformat_new_stream(out, nullptr);
                ok = os && avcodec_parameters_copy(os->codecpar, in->streams[i]->codecpar) >= 0;
                if (ok) {
                    os->codecpar->codec_tag = 0;
                    os->time_base           = in->streams[i]->time_base;
                }
            }
            AVDictionary* opts = nullptr;
            av_dict_set(&opts, "listen", "1", 0);
            const AVIOInterruptCB interrupt{ stop_requested, &st };
            ok = ok && avio_open2(&out->pb, url().c_str(), AVIO_FLAG_WRITE, &interrupt, &opts) >= 0;
            av_dict_free(&opts);
            if (ok) {
                played_ = true;
                if (avformat_write_header(out, nullptr) >= 0) {
                    AVPacket* pkt = av_packet_alloc();
                    while (!st.stop_requested() && av_read_frame(in, pkt) >= 0) {
                        av_packet_rescale_ts(pkt, in->streams[pkt->stream_index]->time_base,
                                             out->streams[pkt->stream_index]->time_base);
                        pkt->pos = -1;
                        const int r = av_interleaved_write_frame(out, pkt);
                        av_packet_unref(pkt);
                        if (r < 0) break;   // the client went away
                    }
                    av_packet_free(&pkt);
                    av_write_trailer(out);
                }
                avio_closep(&out->pb);
            }
        }
        if (out) avformat_free_context(out);
        avformat_close_input(&in);
    }

    std::string       flv_;
    int               port_ = 0;
    std::atomic<bool> played_{ false };
    std::jthread      thread_;   // last: started after, and stopped before, the rest
};

double seconds_since(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

TEST_CASE("VMAFAnalyzer: an rtmp input from a local server scores as the file it sends",
          "[vmaf][vmafnet][rtmp]")
{
    const std::string clip = flv_clip();

    // Its first 12 frame pairs, before the stream ends.
    const VMAFOptions first{ .max_frames = 12, .record_pts = true };
    const VMAFResult local = VMAFAnalyzer::analyze(clip, clip, first);
    REQUIRE(local.error.empty());
    REQUIRE(local.per_frame.size() == 12);
    {
        RtmpServer server(clip);
        REQUIRE(server.wait_listening(std::chrono::seconds(5)));
        const VMAFResult r = VMAFAnalyzer::analyze(clip, server.url(), first);
        INFO(r.error << r.dis_info.end_error);
        CHECK(server.served());
        REQUIRE(r.error.empty());
        CHECK(vmafreport::incomplete(r).empty());
        CHECK(r.per_frame == local.per_frame);   // ==: to the last bit
        CHECK(r.dis_pts == local.dis_pts);
        CHECK(r.alignment.failed == 0);
    }

    // All of it: every frame pairs, with the file's scores. FFmpeg's rtmp
    // client then ends the stream on an I/O error, at this server's close of
    // the connection, so the pair isn't complete.
    const VMAFResult whole = VMAFAnalyzer::analyze(clip, clip, { .record_pts = true });
    REQUIRE(whole.per_frame.size() == 36);
    RtmpServer server(clip);
    REQUIRE(server.wait_listening(std::chrono::seconds(5)));
    const VMAFResult r = VMAFAnalyzer::analyze(clip, server.url(), { .record_pts = true });
    INFO(r.error);
    REQUIRE(r.error.empty());
    CHECK(r.per_frame == whole.per_frame);
    CHECK(r.dis_pts == whole.dis_pts);
    CHECK(r.alignment.failed == 0);
    CHECK(r.dis_info.end_error == "Input/output error");
    CHECK(vmafreport::incomplete(r) ==
          "reading the distorted input stopped on an error: Input/output error");
}

TEST_CASE("VMAFAnalyzer: an rtmp input connects, and fails at the read limit when nothing comes",
          "[vmaf][vmafnet][rtmp]")
{
    // The server takes the connection and sends nothing, so the rtmp
    // handshake's first read waits: it gives up after rw_timeout's 5 s. With
    // FFmpeg's timeout the input would listen on the server's port instead.
    SilentServer server;
    REQUIRE(server.ok());
    const std::string url = std::format("rtmp://127.0.0.1:{}/live/clip", server.port());
    const auto t0 = std::chrono::steady_clock::now();
    const VMAFResult r = VMAFAnalyzer::analyze(flv_clip(), url);
    const double s = seconds_since(t0);
    INFO(r.error << ", after " << s << " s");
    CHECK(r.error == "cannot open distorted: " + url);
    CHECK(server.accepted() >= 1);
    CHECK(s >= 4.0);
    CHECK(s < 15.0);
}

TEST_CASE("The player's demuxer and its helpers' opens connect to an rtmp server",
          "[vmafnet][rtmp]")
{
    {
        RtmpServer server(flv_clip());
        REQUIRE(server.wait_listening(std::chrono::seconds(5)));
        Demuxer d;
        REQUIRE(d.open(server.url()));
        CHECK(server.served());
        REQUIRE(d.video_codecpar());
        CHECK(d.video_codecpar()->codec_id == AV_CODEC_ID_FLV1);
        CHECK(d.video_codecpar()->width == 96);
        d.close();
    }
    {
        RtmpServer server(flv_clip());
        REQUIRE(server.wait_listening(std::chrono::seconds(5)));
        std::stop_source stop;
        AVFormatContext* ctx = nullptr;
        REQUIRE(ffopen::open_input(&ctx, server.url(), stop.get_token()) == 0);
        CHECK(server.served());
        REQUIRE(ctx);
        CHECK(std::string(ctx->iformat->name) == "flv");
        avformat_close_input(&ctx);
    }
}
