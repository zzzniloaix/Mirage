// make_fixture: the synthetic HLS fixture of the Phase 3 source survey, made
// with FFmpeg's libraries only (no ffmpeg binary), as tests/test_vmaf_segments.cpp
// makes its streams.
//
//   make_fixture <dir>
//   make_fixture --gop <dir>     only <dir>/index.m3u8: see main()
//
// Writes, under <dir>:
//   ref/index.m3u8 + ref/seg0.ts..seg3.ts   192x108, the reference
//   dis/index.m3u8 + dis/seg0.ts..seg3.ts   96x54, the distorted variant
//   master.m3u8                             a master naming both (BANDWIDTH, RESOLUTION)
//   dis/tagged.m3u8                         dis/index.m3u8 with timeline tags written in:
//       EXT-X-PROGRAM-DATE-TIME, EXT-X-DISCONTINUITY (twice), EXT-X-DATERANGE with
//       SCTE35-OUT / SCTE35-IN (the example bytes of RFC 8216 section 8.10, verbatim),
//       #EXT-OATCLS-SCTE35 (the SCTE35-OUT bytes in base64), EXT-X-CUE-OUT,
//       EXT-X-CUE-OUT-CONT, EXT-X-CUE-IN and a made-up vendor tag.
// Each stream: 40 frames at 25 fps, a moving gray ramp, MPEG-2, every frame a
// key frame at a fixed quantizer, cut by FFmpeg's hls muxer with hls_time 0.4.
// The timestamps are continuous across the tags: the tags are only text.

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/base64.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
}

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr int kFps    = 25;
constexpr int kFrames = 40;

[[noreturn]] void die(const char* what, int err = 0)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    if (err) av_strerror(err, buf, sizeof buf);
    std::fprintf(stderr, "make_fixture: %s %s\n", what, buf);
    std::exit(1);
}

void make_stream(const fs::path& dir, int w, int h, int gop = 1, int b_frames = 0)
{
    fs::create_directories(dir);
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MPEG2VIDEO);
    if (!codec) die("no MPEG-2 encoder");
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    enc->width          = w;
    enc->height         = h;
    enc->pix_fmt        = AV_PIX_FMT_YUV420P;
    enc->time_base      = { 1, kFps };
    enc->framerate      = { kFps, 1 };
    enc->gop_size       = gop;
    enc->max_b_frames   = b_frames;
    enc->flags         |= AV_CODEC_FLAG_QSCALE;
    enc->global_quality = FF_QP2LAMBDA * 2;
    enc->colorspace     = AVCOL_SPC_BT709;
    enc->color_range    = AVCOL_RANGE_MPEG;
    if (int r = avcodec_open2(enc, codec, nullptr); r < 0) die("avcodec_open2", r);

    const std::string playlist = (dir / "index.m3u8").string();
    AVFormatContext* oc = nullptr;
    if (int r = avformat_alloc_output_context2(&oc, nullptr, "hls", playlist.c_str()); r < 0)
        die("avformat_alloc_output_context2", r);
    AVStream* st = avformat_new_stream(oc, nullptr);
    avcodec_parameters_from_context(st->codecpar, enc);
    st->time_base = enc->time_base;
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "hls_time", "0.4", 0);
    av_dict_set(&opts, "hls_list_size", "0", 0);
    av_dict_set(&opts, "hls_playlist_type", "vod", 0);
    av_dict_set(&opts, "hls_segment_filename", (dir / "seg%d.ts").string().c_str(), 0);
    if (int r = avformat_write_header(oc, &opts); r < 0) die("avformat_write_header", r);
    av_dict_free(&opts);

    AVPacket* pkt = av_packet_alloc();
    const auto drain = [&] {
        while (avcodec_receive_packet(enc, pkt) == 0) {
            pkt->stream_index = 0;
            av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
            if (int r = av_interleaved_write_frame(oc, pkt); r < 0) die("write", r);
        }
    };
    AVFrame* f = av_frame_alloc();
    f->format = AV_PIX_FMT_YUV420P;
    f->width  = w;
    f->height = h;
    av_frame_get_buffer(f, 0);
    for (int k = 0; k < kFrames; ++k) {
        av_frame_make_writable(f);
        for (int row = 0; row < h; ++row)
            for (int col = 0; col < w; ++col)
                f->data[0][row * f->linesize[0] + col] =
                    static_cast<uint8_t>((col * 256 / w + row * 128 / h + 3 * k) & 0xff);
        for (int p = 1; p < 3; ++p)
            for (int row = 0; row < (h + 1) / 2; ++row)
                std::fill_n(f->data[p] + row * f->linesize[p], (w + 1) / 2, static_cast<uint8_t>(128));
        f->pts = k;
        if (int r = avcodec_send_frame(enc, f); r < 0) die("avcodec_send_frame", r);
        drain();
    }
    avcodec_send_frame(enc, nullptr);
    drain();
    av_frame_free(&f);
    av_packet_free(&pkt);
    if (int r = av_write_trailer(oc); r < 0) die("av_write_trailer", r);
    avformat_free_context(oc);
    avcodec_free_context(&enc);
}

std::string read_text(const fs::path& p)
{
    std::ifstream f(p, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

void write_text(const fs::path& p, const std::string& text)
{
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << text;
    if (!f) die("cannot write a playlist");
}

std::vector<unsigned char> from_hex(const std::string& hex)
{
    std::vector<unsigned char> out;
    for (std::size_t i = 2; i + 1 < hex.size(); i += 2)   // after "0x"
        out.push_back(static_cast<unsigned char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    return out;
}

} // namespace

int main(int argc, char** argv)
{
    // make_fixture --gop <dir>: one more stream only, <dir>/index.m3u8, 192x108
    // with a key frame every 10 frames and up to 2 B-frames between the others,
    // for the decoder's per-frame fields (pict_type, AV_FRAME_FLAG_KEY).
    if (argc == 3 && std::string(argv[1]) == "--gop") {
        make_stream(argv[2], 192, 108, 10, 2);
        std::printf("== index.m3u8 ==\n%s", read_text(fs::path(argv[2]) / "index.m3u8").c_str());
        return 0;
    }
    if (argc != 2) {
        std::fprintf(stderr, "usage: make_fixture <dir> | make_fixture --gop <dir>\n");
        return 2;
    }
    const fs::path root = argv[1];
    make_stream(root / "ref", 192, 108);
    make_stream(root / "dis", 96, 54);

    write_text(root / "master.m3u8",
               "#EXTM3U\n"
               "#EXT-X-VERSION:3\n"
               "#EXT-X-STREAM-INF:BANDWIDTH=2000000,RESOLUTION=192x108\n"
               "ref/index.m3u8\n"
               "#EXT-X-STREAM-INF:BANDWIDTH=500000,RESOLUTION=96x54\n"
               "dis/index.m3u8\n");

    // RFC 8216, section 8.10, its two example tags' SCTE-35 bytes, joined.
    const std::string out_hex =
        "<vector rfc8216-8.10-SCTE35-OUT>"
        "";
    const std::string in_hex =
        "<vector rfc8216-8.10-SCTE35-IN>";
    const std::vector<unsigned char> out_bytes = from_hex(out_hex);
    std::vector<char> b64(AV_BASE64_SIZE(out_bytes.size()));
    av_base64_encode(b64.data(), static_cast<int>(b64.size()), out_bytes.data(),
                     static_cast<int>(out_bytes.size()));

    // dis/index.m3u8 with the tags: the header, then segment k's EXTINF and URI.
    const std::string plain = read_text(root / "dis" / "index.m3u8");
    std::vector<std::string> header, extinf, uri;
    {
        std::istringstream ss(plain);
        std::string line;
        bool in_segments = false;
        while (std::getline(ss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("#EXTINF:", 0) == 0) { in_segments = true; extinf.push_back(line); continue; }
            if (line.rfind("#EXT-X-ENDLIST", 0) == 0 || line.empty()) continue;
            if (!in_segments) header.push_back(line);
            else if (line[0] != '#') uri.push_back(line);
        }
    }
    if (extinf.size() != 4 || uri.size() != 4) die("dis/index.m3u8 hasn't 4 segments");
    std::string t;
    for (const auto& h : header) t += h + "\n";
    t += "#EXT-X-PROGRAM-DATE-TIME:2014-03-05T11:14:59.600Z\n";
    t += extinf[0] + "\n" + uri[0] + "\n";
    t += "#EXT-X-DISCONTINUITY\n";
    t += "#EXT-X-DATERANGE:ID=\"splice-6FFFFFF0\",START-DATE=\"2014-03-05T11:15:00Z\","
         "PLANNED-DURATION=59.993,SCTE35-OUT=" + out_hex + "\n";
    t += std::string("#EXT-OATCLS-SCTE35:") + b64.data() + "\n";
    t += "#EXT-X-CUE-OUT:0.8\n";
    t += extinf[1] + "\n" + uri[1] + "\n";
    t += "#EXT-X-CUE-OUT-CONT:ElapsedTime=0.4,Duration=0.8\n";
    t += extinf[2] + "\n" + uri[2] + "\n";
    t += "#EXT-X-DISCONTINUITY\n";
    t += "#EXT-X-CUE-IN\n";
    t += "#EXT-X-DATERANGE:ID=\"splice-6FFFFFF0\",DURATION=59.993,SCTE35-IN=" + in_hex + "\n";
    t += "#EXT-X-VENDOR-EXAMPLE:KEY=\"value\"\n";
    t += extinf[3] + "\n" + uri[3] + "\n";
    t += "#EXT-X-ENDLIST\n";
    write_text(root / "dis" / "tagged.m3u8", t);

    for (const char* p : { "master.m3u8", "ref/index.m3u8", "dis/index.m3u8", "dis/tagged.m3u8" })
        std::printf("== %s ==\n%s", p, read_text(root / p).c_str());
    std::printf("== files ==\n");
    std::vector<fs::path> files;
    for (const auto& e : fs::recursive_directory_iterator(root))
        if (e.is_regular_file()) files.push_back(fs::relative(e.path(), root));
    std::sort(files.begin(), files.end());
    for (const auto& f : files)
        std::printf("%s %ju bytes\n", f.string().c_str(),
                    static_cast<std::uintmax_t>(fs::file_size(root / f)));
    return 0;
}
