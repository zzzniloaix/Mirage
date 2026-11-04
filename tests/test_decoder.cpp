// Decoder: the decode errors it counts, the drain at the end of the stream
// past a frame that failed (software decoding, frame threads), the errors
// under VideoToolbox counted as its own, and a decoder that could not be
// opened. Inputs from TestEncode.h, damaged here on purpose.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include "Decoder.h"
#include "TestEncode.h"
#include "TestTemp.h"

#include <cstdint>
#include <filesystem>
#include <set>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
}

using Catch::Matchers::ContainsSubstring;

namespace {

// The video packets of a file, in file order, and its codec parameters.
struct Packets {
    AVFormatContext*       fmt = nullptr;
    AVCodecParameters*     par = nullptr;
    std::vector<AVPacket*> pkts;

    explicit Packets(const std::filesystem::path& path)
    {
        REQUIRE(avformat_open_input(&fmt, path.c_str(), nullptr, nullptr) >= 0);
        REQUIRE(avformat_find_stream_info(fmt, nullptr) >= 0);
        const int vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        REQUIRE(vi >= 0);
        par = fmt->streams[vi]->codecpar;
        AVPacket* p = av_packet_alloc();
        while (av_read_frame(fmt, p) >= 0) {
            if (p->stream_index == vi) pkts.push_back(av_packet_clone(p));
            av_packet_unref(p);
        }
        av_packet_free(&p);
    }
    ~Packets()
    {
        for (AVPacket*& p : pkts) av_packet_free(&p);
        avformat_close_input(&fmt);
    }
};

// Frames out of `d` for every packet, then the drain, in the video decode
// thread's way (MediaSession.cpp): push one packet, pull until false; at
// the end push(nullptr), pull until false.
struct Decoded {
    int          frames = 0;
    int          drained = 0;          // frames out of the drain
    int          after_drain_error = 0;   // of those, after an error in the drain
};
Decoded decode_all(Decoder& d, const std::vector<AVPacket*>& pkts)
{
    Decoded r;
    AVFrame* f = av_frame_alloc();
    for (AVPacket* p : pkts) {
        d.push(p);
        while (d.pull(f)) { ++r.frames; av_frame_unref(f); }
    }
    const std::int64_t before = d.errors().errors;
    d.push(nullptr);
    while (d.pull(f)) {
        ++r.frames;
        ++r.drained;
        if (d.errors().errors > before) ++r.after_drain_error;
        av_frame_unref(f);
    }
    av_frame_free(&f);
    return r;
}

// H.264 in MP4: each packet is length-prefixed NAL units, so one cut to a
// third of its size is rejected whole (AVERROR_INVALIDDATA), and the frames
// after it decode (with their reference missing). P-frames of a 48-frame
// clip, one near the end: with frame threads its error comes out of the
// drain, ahead of the last frames (the loop that stopped at the first error
// lost them).
const std::set<std::size_t> kDamaged = { 5, 17, 29, 46 };

void cut_short(Packets& in)
{
    REQUIRE(in.pkts.size() == 48);
    for (std::size_t i : kDamaged) {
        AVPacket* p = in.pkts[i];
        REQUIRE_FALSE(p->flags & AV_PKT_FLAG_KEY);
        REQUIRE(av_packet_make_writable(p) >= 0);
        av_shrink_packet(p, p->size / 3);
    }
}

} // namespace

TEST_CASE("Decoder: one error per damaged packet, and the drain gives every frame (software)", "[decoder]")
{
    const auto path = testtemp::path("decoder_damage.mp4");
    testencode::Encode e;
    e.format = "mp4";
    e.frames = 48;
    e.gop    = 12;
    if (!testencode::encode(path, e)) SKIP("libx264 is not available");
    Packets in(path);
    cut_short(in);
    const std::set<std::size_t>& damaged = kDamaged;

    Decoder d;
    REQUIRE(d.open(in.par, false));   // software: frame + slice threads, one per core
    const Decoded r = decode_all(d, in.pkts);
    const Decoder::Errors errs = d.errors();
    CHECK(errs.errors == static_cast<std::int64_t>(damaged.size()));
    CHECK(errs.last == AVERROR_INVALIDDATA);
    CHECK(errs.hw_run_max == 0);   // no VideoToolbox here
    CHECK(r.frames == 48 - static_cast<int>(damaged.size()));
    INFO("frames out of the drain: " << r.drained << ", after an error in it: " << r.after_drain_error);
    if (r.after_drain_error == 0)
        WARN("no error came out of the drain (one frame thread?): the drain past an error was not exercised");

    const std::string text = describe_decode_errors(errs);
    CHECK_THAT(text, ContainsSubstring("4 decode errors (last: Invalid data found when processing input)"));
    CHECK_THAT(text, !ContainsSubstring("--no-hw"));
}

#ifndef MIRAGE_HEADLESS
// Not in the headless configuration (MIRAGE_HEADLESS, CMakeLists.txt): it
// builds where there may be no VideoToolbox. Tagged, to be picked or left
// out in the others.
TEST_CASE("Decoder: VideoToolbox's errors on damaged packets are counted as its own", "[decoder][videotoolbox]")
{
    AVBufferRef* dev = nullptr;
    if (av_hwdevice_ctx_create(&dev, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, nullptr, nullptr, 0) < 0)
        SKIP("no VideoToolbox device");
    av_buffer_unref(&dev);
    const auto path = testtemp::path("decoder_damage_vt.mp4");
    testencode::Encode e;
    e.format = "mp4";
    e.frames = 48;
    e.gop    = 12;
    e.width  = 320;
    e.height = 240;
    if (!testencode::encode(path, e)) SKIP("libx264 is not available");
    Packets in(path);
    cut_short(in);

    Decoder d;
    REQUIRE(d.open(in.par, true));
    const Decoded r = decode_all(d, in.pkts);
    if (!d.is_hw_active()) SKIP("VideoToolbox decoded none of this clip's frames");
    const Decoder::Errors errs = d.errors();
    INFO(r.frames << " frames; " << describe_decode_errors(errs));
    CHECK(errs.errors >= static_cast<std::int64_t>(kDamaged.size()));
    CHECK(errs.hw_run_max > 0);
    CHECK(errs.hw_run == 0);   // frames came after the last error
    CHECK_THAT(describe_decode_errors(errs), ContainsSubstring("--no-hw decodes in software"));
}
#endif

TEST_CASE("Decoder: an undamaged stream counts nothing, and EOF after the drain is not an error", "[decoder]")
{
    const auto path = testtemp::path("decoder_clean.mp4");
    testencode::Encode e;
    e.format = "mp4";
    e.frames = 24;
    if (!testencode::encode(path, e)) SKIP("libx264 is not available");
    Packets in(path);

    Decoder d;
    REQUIRE(d.open(in.par, false));
    const Decoded r = decode_all(d, in.pkts);
    CHECK(r.frames == 24);
    CHECK(d.push(nullptr) == AVERROR_EOF);       // a second drain packet
    CHECK(d.push(in.pkts[0]) == AVERROR_EOF);    // a packet before flush()
    CHECK(d.errors().errors == 0);
    CHECK(d.errors().corrupt == 0);
    CHECK(describe_decode_errors(d.errors()).empty());

    d.flush();                                   // a seek: packets are taken again
    CHECK(d.push(in.pkts[0]) == 0);
}

TEST_CASE("Decoder::open fails without a decoder; push, pull and flush then do nothing", "[decoder]")
{
    AVPacket* pkt = av_packet_alloc();
    REQUIRE(av_new_packet(pkt, 64) >= 0);
    AVFrame* f = av_frame_alloc();
    auto unusable = [&](Decoder& d) {
        CHECK_FALSE(d.is_open());
        CHECK(d.push(pkt) < 0);
        CHECK(d.push(nullptr) < 0);
        CHECK_FALSE(d.pull(f));
        d.flush();
        d.skip_unreferenced(true);
        CHECK(d.width() == 0);
        CHECK(d.errors().errors == 0);   // nothing was decoded, so nothing failed
    };

    SECTION("no decoder for the codec (a track switch to AC-4)") {
        // AC-4 and MPEG-H 3D Audio have no decoder in the FFmpeg the plan's
        // records used (supplement-2026-11-03.txt); AV_CODEC_ID_NONE, in none.
        int tried = 0;
        for (const AVCodecID id : { AV_CODEC_ID_AC4, AV_CODEC_ID_MPEGH_3D_AUDIO, AV_CODEC_ID_NONE }) {
            if (avcodec_find_decoder(id)) continue;
            AVCodecParameters* par = avcodec_parameters_alloc();
            par->codec_type = AVMEDIA_TYPE_AUDIO;
            par->codec_id   = id;
            Decoder d;
            CHECK_FALSE(d.open(par));
            unusable(d);
            avcodec_parameters_free(&par);
            ++tried;
        }
        CHECK(tried > 0);
    }
    SECTION("video, VideoToolbox asked for") {
        AVCodecParameters* par = avcodec_parameters_alloc();
        par->codec_type = AVMEDIA_TYPE_VIDEO;
        par->codec_id   = AV_CODEC_ID_NONE;
        Decoder d;
        CHECK_FALSE(d.open(par, true));
        unusable(d);
        avcodec_parameters_free(&par);
    }
    SECTION("a decoder that fails to open (FFmpeg's flush crashes on such a context)") {
        AVCodecParameters* par = avcodec_parameters_alloc();
        par->codec_type  = AVMEDIA_TYPE_AUDIO;
        par->codec_id    = AV_CODEC_ID_PCM_S16LE;
        par->sample_rate = 48000;   // no channels: avcodec_open2 refuses it
        Decoder d;
        CHECK_FALSE(d.open(par));
        unusable(d);
        avcodec_parameters_free(&par);
    }
    SECTION("a failed reopen after a good one") {
        AVCodecParameters* par = avcodec_parameters_alloc();
        par->codec_type  = AVMEDIA_TYPE_AUDIO;
        par->codec_id    = AV_CODEC_ID_PCM_S16LE;
        par->sample_rate = 48000;
        av_channel_layout_default(&par->ch_layout, 2);
        Decoder d;
        REQUIRE(d.open(par));
        CHECK(d.is_open());
        par->codec_id = AV_CODEC_ID_NONE;
        CHECK_FALSE(d.open(par));
        unusable(d);
        avcodec_parameters_free(&par);
    }

    av_frame_free(&f);
    av_packet_free(&pkt);
}

TEST_CASE("describe_decode_errors: counts in words, --no-hw after VideoToolbox errors", "[decoder]")
{
    Decoder::Errors e;
    CHECK(describe_decode_errors(e).empty());

    e.corrupt = 1;
    CHECK(describe_decode_errors(e) == "1 corrupt frame");

    e.errors     = 25;
    e.last       = AVERROR_EXTERNAL;
    e.hw_run_max = 25;
    e.corrupt    = 0;
    const std::string t = describe_decode_errors(e);
    CHECK_THAT(t, ContainsSubstring("25 decode errors (last: " + av_error_text(AVERROR_EXTERNAL) + ")"));
    CHECK_THAT(t, ContainsSubstring("up to 25 in a row under VideoToolbox"));
    CHECK_THAT(t, ContainsSubstring("--no-hw decodes in software"));
}
