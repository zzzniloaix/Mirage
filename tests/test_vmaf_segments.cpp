// The files of an HLS input that the analyzer's hls demuxer goes on without
// (DECISIONS.md D-055): a segment it can't open is tried again as the
// player's demuxer tries it, and a segment it still goes on without, or the
// rest of a file whose read fails, ends the input as a read error does, so
// that the pair isn't complete. The streams are FFmpeg's hls muxer's, of a
// generated clip, read from a local directory and from a local HTTP server
// that can hold a request or cut a body short (tests/HttpFileServer.h).

#include <catch2/catch_test_macros.hpp>

#include "HttpFileServer.h"
#include "TestTemp.h"
#include "VMAFAnalyzer.h"
#include "VMAFReport.h"
#include "VMAFSegments.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <format>
#include <string>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

namespace fs = std::filesystem;

namespace {

constexpr int kFps            = 25;
constexpr int kSegmentFrames  = 10;   // hls_time 0.4 s at 25 fps
constexpr int kSegments       = 3;
constexpr int kFrames         = kSegments * kSegmentFrames;

// An HLS stream in `dir`, made by FFmpeg's hls muxer: index.m3u8, a VOD
// playlist of the MPEG-TS segments seg0.ts, seg1.ts and seg2.ts, each of
// kSegmentFrames frames of w x h at 25 fps: a gray diagonal ramp that moves
// every frame, MPEG-2 coded, every frame a key frame at a fixed quantizer.
// Returns the playlist's path.
fs::path make_stream(const fs::path& dir, int w, int h)
{
    fs::create_directories(dir);
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
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
    enc->colorspace     = AVCOL_SPC_BT709;
    enc->color_range    = AVCOL_RANGE_MPEG;
    REQUIRE(avcodec_open2(enc, codec, nullptr) >= 0);

    const fs::path playlist = dir / "index.m3u8";
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, "hls", playlist.c_str()) >= 0);
    AVStream* st = avformat_new_stream(oc, nullptr);
    REQUIRE(avcodec_parameters_from_context(st->codecpar, enc) >= 0);
    st->time_base = enc->time_base;
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "hls_time", "0.4", 0);
    av_dict_set(&opts, "hls_list_size", "0", 0);
    av_dict_set(&opts, "hls_playlist_type", "vod", 0);
    av_dict_set(&opts, "hls_segment_filename", (dir / "seg%d.ts").c_str(), 0);
    const int header = avformat_write_header(oc, &opts);
    av_dict_free(&opts);
    REQUIRE(header >= 0);

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
    for (int k = 0; k < kFrames; ++k) {
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
    avformat_free_context(oc);
    avcodec_free_context(&enc);

    for (int s = 0; s < kSegments; ++s) REQUIRE(fs::exists(dir / std::format("seg{}.ts", s)));
    REQUIRE_FALSE(fs::exists(dir / std::format("seg{}.ts", kSegments)));
    return playlist;
}

// The reference and the distorted stream, made once in testtemp::dir():
// 192x108 and 96x54.
struct Streams {
    fs::path ref_dir, dis_dir;
    fs::path ref, dis;   // their playlists
};

const Streams& streams()
{
    static const Streams s = [] {
        Streams x;
        x.ref_dir = testtemp::path("mirage_test_segments_ref");
        x.dis_dir = testtemp::path("mirage_test_segments_dis");
        x.ref     = make_stream(x.ref_dir, 192, 108);
        x.dis     = make_stream(x.dis_dir, 96, 54);
        return x;
    }();
    return s;
}

// A copy of the distorted stream in testtemp::dir()/`name`, but its segment
// `without`. Returns the copy's playlist.
fs::path dis_without(const std::string& name, const std::string& without)
{
    const fs::path dir = testtemp::path(name);
    fs::create_directories(dir);
    for (const fs::directory_entry& e : fs::directory_iterator(streams().dis_dir))
        if (e.path().filename() != without)
            fs::copy_file(e.path(), dir / e.path().filename(), fs::copy_options::overwrite_existing);
    REQUIRE_FALSE(fs::exists(dir / without));
    return dir / "index.m3u8";
}

double seconds_since(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

} // namespace

// ── The watch (src/VMAFSegments.h) ───────────────────────────────────────────

TEST_CASE("vmafsegments::SegmentWatch: retries, a file gone without, read errors", "[vmaf][segments]")
{
    const int not_found = AVERROR_HTTP_NOT_FOUND;
    SECTION("an open that succeeds after failed ones loses nothing") {
        vmafsegments::SegmentWatch w;
        for (int k = 0; k < vmafsegments::kSegMaxRetry; ++k) w.opened("seg1.ts", not_found);
        w.opened("seg1.ts", 0);
        w.closed("seg1.ts", 0);
        w.ended();
        CHECK(w.lost().empty());
    }
    SECTION("1 + kSegMaxRetry failed opens: the demuxer goes on without the file") {
        vmafsegments::SegmentWatch w;
        for (int k = 0; k < vmafsegments::kSegMaxRetry; ++k) w.opened("seg1.ts", not_found);
        CHECK(w.lost().empty());
        w.opened("seg1.ts", not_found);
        CHECK(w.lost() == "the hls demuxer went on without seg1.ts, which it couldn't open in 11 "
                          "tries: Server returned 404 Not Found");
        // The first file lost is the one kept.
        w.closed("seg2.ts", AVERROR(EIO));
        w.ended();
        CHECK(w.lost().starts_with("the hls demuxer went on without seg1.ts,"));
    }
    SECTION("at the input's end, a file whose opens all failed was gone without") {
        vmafsegments::SegmentWatch w;
        w.opened("seg0.ts", 0);
        w.opened("seg1.ts", not_found);
        CHECK(w.lost().empty());
        w.ended();
        CHECK(w.lost() == "the hls demuxer went on without seg1.ts, which it couldn't open in 1 "
                          "try: Server returned 404 Not Found");
    }
    SECTION("a read error loses the rest of the file") {
        vmafsegments::SegmentWatch w;
        w.opened("seg1.ts", 0);
        w.closed("seg1.ts", AVERROR(EIO));
        CHECK(w.lost() == std::format("the hls demuxer went on without the rest of seg1.ts after "
                                      "a read error: {}",
                                      vmafsegments::error_text(AVERROR(EIO))));
    }
    SECTION("an open or a read a cancel ended loses nothing") {
        vmafsegments::SegmentWatch w;
        for (int k = 0; k <= vmafsegments::kSegMaxRetry; ++k) w.opened("seg1.ts", AVERROR_EXIT);
        w.opened("seg2.ts", 0);
        w.closed("seg2.ts", AVERROR_EXIT);
        w.ended();
        CHECK(w.lost().empty());
    }
}

TEST_CASE("vmafsegments::SegmentWatch::watch: a context's opens and closes, a byte range by its start",
          "[vmaf][segments]")
{
    const fs::path dir = testtemp::path("mirage_test_segments_watch");
    fs::create_directories(dir);
    const fs::path there = dir / "there.ts";
    { std::FILE* f = std::fopen(there.c_str(), "wb"); REQUIRE(f); std::fputs("data", f); std::fclose(f); }
    const fs::path gone = dir / "gone.ts";

    // A context as avformat_open_input() leaves it for the demuxer: FFmpeg's
    // io_open compares each URL with the input's.
    const auto context = [&] {
        AVFormatContext* c = avformat_alloc_context();
        REQUIRE(c);
        c->url = av_strdup((dir / "index.m3u8").c_str());
        REQUIRE(c->url);
        return c;
    };
    AVFormatContext* ctx = context();
    vmafsegments::SegmentWatch w;
    w.watch(ctx);
    CHECK(ctx->opaque == &w);

    // An open through the context: the context's own io_open, and the watch told.
    AVIOContext* pb = nullptr;
    REQUIRE(ctx->io_open(ctx, &pb, there.c_str(), AVIO_FLAG_READ, nullptr) >= 0);
    REQUIRE(pb);
    unsigned char buf[16];
    CHECK(avio_read(pb, buf, sizeof buf) == 4);
    pb->error = AVERROR(EIO);   // as a read that failed leaves it
    CHECK(ctx->io_close2(ctx, pb) == AVERROR(EIO));   // avio_close() returns it; hls.c ignores it
    CHECK(w.lost() == std::format("the hls demuxer went on without the rest of {} after a read "
                                  "error: {}",
                                  there.string(), vmafsegments::error_text(AVERROR(EIO))));

    avformat_free_context(ctx);

    // A byte range of a file is named by its start, the open's offset option.
    ctx = context();
    vmafsegments::SegmentWatch ranges;
    ranges.watch(ctx);
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "offset", "188", 0);
    pb = nullptr;
    CHECK(ctx->io_open(ctx, &pb, gone.c_str(), AVIO_FLAG_READ, &opts) < 0);
    av_dict_free(&opts);
    ranges.ended();
    CHECK(ranges.lost() == std::format("the hls demuxer went on without {} (from byte 188), which "
                                       "it couldn't open in 1 try: {}",
                                       gone.string(), vmafsegments::error_text(AVERROR(ENOENT))));
    avformat_free_context(ctx);
}

TEST_CASE("vmafsegments::set_hls_options: the player's retries, one connection per file",
          "[vmaf][segments]")
{
    AVDictionary* opts = nullptr;
    vmafsegments::set_hls_options(&opts);
    const auto value = [&](const char* key) {
        const AVDictionaryEntry* e = av_dict_get(opts, key, nullptr, 0);
        return e ? std::string(e->value) : std::string("(none)");
    };
    CHECK(value("seg_max_retry") == "10");
    CHECK(value("http_persistent") == "0");
    CHECK(value("http_multiple") == "0");
    CHECK(av_dict_count(opts) == 3);
    av_dict_free(&opts);
}

// ── Local streams ────────────────────────────────────────────────────────────

TEST_CASE("VMAFAnalyzer: a local HLS stream without a segment isn't complete", "[vmaf][segments]")
{
    const Streams& s = streams();
    const VMAFResult whole = VMAFAnalyzer::analyze(s.ref.string(), s.dis.string());
    INFO(vmafreport::incomplete(whole));
    CHECK(vmafreport::incomplete(whole).empty());
    CHECK(whole.per_frame.size() == kFrames);
    CHECK(whole.alignment.failed == 0);

    for (int k = 0; k < kSegments; ++k) {
        const std::string seg = std::format("seg{}.ts", k);
        const fs::path dis = dis_without(std::format("mirage_test_segments_without{}", k), seg);
        INFO(seg);
        const VMAFResult r = VMAFAnalyzer::analyze(s.ref.string(), dis.string());
        CHECK(r.error.empty());
        CHECK(r.ref_info.end_error.empty());
        CHECK(r.dis_info.ended);
        CHECK(r.dis_info.end_error ==
              std::format("the hls demuxer went on without {}, which it couldn't open in 11 tries: "
                          "{}",
                          (dis.parent_path() / seg).string(),
                          vmafsegments::error_text(AVERROR(ENOENT))));
        CHECK(vmafreport::incomplete(r) ==
              "reading the distorted input stopped on an error: " + r.dis_info.end_error);
        CHECK(r.per_frame.size() < kFrames);
    }
}

// ── Over HTTP (tests/HttpFileServer.h) ───────────────────────────────────────

TEST_CASE("VMAFAnalyzer: an HLS segment whose first request waits is tried again, and scores as the local stream",
          "[vmaf][segments][vmafnet]")
{
    const Streams& s = streams();
    const VMAFResult local = VMAFAnalyzer::analyze(s.ref.string(), s.dis.string());
    REQUIRE(vmafreport::incomplete(local).empty());

    // The first request for seg1.ts gets no answer: it fails at the 5 s read
    // limit (D-036), and the demuxer's next try is answered.
    HttpFileServer server(s.dis_dir, { .path = "seg1.ts", .hold_first = 1 });
    REQUIRE(server.ok());
    const auto t0 = std::chrono::steady_clock::now();
    const VMAFResult r = VMAFAnalyzer::analyze(s.ref.string(), server.url("index.m3u8"));
    const double secs = seconds_since(t0);
    INFO(vmafreport::incomplete(r) << ", after " << secs << " s");
    CHECK(vmafreport::incomplete(r).empty());
    CHECK(server.requests("seg1.ts") == 2);
    CHECK(server.requests("seg0.ts") == 1);
    CHECK(server.requests("seg2.ts") == 1);
    CHECK(secs >= 4.0);
    CHECK(r.per_frame == local.per_frame);
}

TEST_CASE("VMAFAnalyzer: an HLS segment cut short ends the input with its read error",
          "[vmaf][segments][vmafnet]")
{
    const Streams& s = streams();
    const std::size_t size = fs::file_size(s.dis_dir / "seg1.ts");
    HttpFileServer server(s.dis_dir, { .path = "seg1.ts", .cut_after = size / 2 });
    REQUIRE(server.ok());
    const VMAFResult r = VMAFAnalyzer::analyze(s.ref.string(), server.url("index.m3u8"));
    INFO("seg1.ts has " << size << " bytes");
    CHECK(r.error.empty());
    CHECK(r.dis_info.end_error.starts_with(
        std::format("the hls demuxer went on without the rest of {} after a read error: ",
                    server.url("seg1.ts"))));
    CHECK(vmafreport::incomplete(r) ==
          "reading the distorted input stopped on an error: " + r.dis_info.end_error);
    // FFmpeg's hls demuxer tries again only a segment that it can't open.
    CHECK(server.requests("seg1.ts") == 1);
}

TEST_CASE("VMAFAnalyzer: an HLS segment the server doesn't have is tried 11 times",
          "[vmaf][segments][vmafnet]")
{
    const Streams& s = streams();
    const fs::path dir = dis_without("mirage_test_segments_http404", "seg1.ts").parent_path();
    HttpFileServer server(dir);
    REQUIRE(server.ok());
    const VMAFResult r = VMAFAnalyzer::analyze(s.ref.string(), server.url("index.m3u8"));
    CHECK(server.requests("seg1.ts") == 1 + vmafsegments::kSegMaxRetry);
    CHECK(r.dis_info.end_error ==
          std::format("the hls demuxer went on without {}, which it couldn't open in 11 tries: {}",
                      server.url("seg1.ts"), vmafsegments::error_text(AVERROR_HTTP_NOT_FOUND)));
    CHECK(vmafreport::incomplete(r) ==
          "reading the distorted input stopped on an error: " + r.dis_info.end_error);
}
