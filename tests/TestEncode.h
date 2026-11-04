#pragma once

// Video encoded in the test process, for the conformance checks' tests: a
// moving pattern through an FFmpeg encoder (libx264 by default) into any
// muxer — a segment file, or a whole HLS or DASH package from FFmpeg's hls /
// dash muxers. Written under testtemp::dir() by the callers.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

namespace testencode {

struct Encode {
    const char* encoder = "libx264";
    const char* format  = "mpegts";
    int frames = 48, fps = 24, gop = 12, width = 64, height = 64;
    std::vector<std::pair<const char*, const char*>> codec_opts;
    std::vector<std::pair<const char*, const char*>> mux_opts;
};

// Encodes `e` into `path` with muxer `e.format`. False when the encoder is missing.
inline bool encode(const std::filesystem::path& path, const Encode& e)
{
    const AVCodec* codec = avcodec_find_encoder_by_name(e.encoder);
    if (!codec) return false;
    AVFormatContext* oc = nullptr;
    REQUIRE(avformat_alloc_output_context2(&oc, nullptr, e.format, path.c_str()) >= 0);
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    REQUIRE(enc);
    enc->width = e.width;
    enc->height = e.height;
    enc->pix_fmt = AV_PIX_FMT_YUV420P;
    enc->time_base = { 1, e.fps };
    enc->framerate = { e.fps, 1 };
    enc->gop_size = e.gop;
    enc->keyint_min = e.gop;
    enc->max_b_frames = 0;
    if (oc->oformat->flags & AVFMT_GLOBALHEADER) enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    AVDictionary* co = nullptr;
    for (const auto& [k, v] : e.codec_opts) av_dict_set(&co, k, v, 0);
    REQUIRE(avcodec_open2(enc, codec, &co) >= 0);
    av_dict_free(&co);
    AVStream* st = avformat_new_stream(oc, nullptr);
    REQUIRE(avcodec_parameters_from_context(st->codecpar, enc) >= 0);
    st->time_base = enc->time_base;
    if (!(oc->oformat->flags & AVFMT_NOFILE)) REQUIRE(avio_open(&oc->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0);
    AVDictionary* mo = nullptr;
    for (const auto& [k, v] : e.mux_opts) av_dict_set(&mo, k, v, 0);
    REQUIRE(avformat_write_header(oc, &mo) >= 0);
    av_dict_free(&mo);

    AVPacket* pkt = av_packet_alloc();
    auto send = [&](const AVFrame* f) {
        REQUIRE(avcodec_send_frame(enc, f) >= 0);
        while (avcodec_receive_packet(enc, pkt) == 0) {
            if (pkt->duration <= 0) pkt->duration = 1;
            av_packet_rescale_ts(pkt, enc->time_base, st->time_base);
            pkt->stream_index = 0;
            REQUIRE(av_interleaved_write_frame(oc, pkt) >= 0);
        }
    };
    AVFrame* f = av_frame_alloc();
    f->format = enc->pix_fmt;
    f->width = enc->width;
    f->height = enc->height;
    REQUIRE(av_frame_get_buffer(f, 0) >= 0);
    for (int i = 0; i < e.frames; ++i) {
        REQUIRE(av_frame_make_writable(f) >= 0);
        for (int y = 0; y < f->height; ++y)
            for (int x = 0; x < f->width; ++x)
                f->data[0][y * f->linesize[0] + x] = static_cast<std::uint8_t>((x * 2 + y * 3 + i * 5) & 0xFF);
        for (int p = 1; p < 3; ++p)
            for (int y = 0; y < f->height / 2; ++y)
                std::memset(f->data[p] + y * f->linesize[p], 128 + (i & 15), static_cast<std::size_t>(f->width / 2));
        f->pts = i;
        send(f);
    }
    send(nullptr);
    REQUIRE(av_write_trailer(oc) >= 0);
    if (!(oc->oformat->flags & AVFMT_NOFILE)) avio_closep(&oc->pb);
    avformat_free_context(oc);
    av_frame_free(&f);
    av_packet_free(&pkt);
    avcodec_free_context(&enc);
    return true;
}

inline std::string slurp(const std::filesystem::path& p)
{
    std::ifstream in(p, std::ios::binary);
    return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

} // namespace testencode
