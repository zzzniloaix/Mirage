// AudioDecoder::start_at: where a track switch's new audio takes over, or a
// seek's target — to the sample. PCM 48 kHz stereo in and out, so nothing
// is resampled in between; each sample's value is its index.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "AudioDecoder.h"
#include "AudioPlayer.h"

#include <cstdint>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
}

using Catch::Matchers::WithinAbs;

namespace {

constexpr int kRate = 48000;
constexpr int kPkt  = 480;   // 10 ms

// Packets from sample `first` on, `n` of them, pts in samples (1/48000).
void feed(AudioDecoder& dec, AudioPlayer& player, int first, int n)
{
    for (int i = 0; i < n; ++i) {
        AVPacket* pkt = av_packet_alloc();
        REQUIRE(av_new_packet(pkt, kPkt * 2 * 2) >= 0);
        auto* s = reinterpret_cast<std::int16_t*>(pkt->data);
        for (int k = 0; k < kPkt; ++k) {
            const auto v = static_cast<std::int16_t>((first + i * kPkt + k) % 32768);
            s[2 * k] = s[2 * k + 1] = v;
        }
        pkt->pts = pkt->dts = first + i * kPkt;
        pkt->duration = kPkt;
        dec.decode(pkt, player);
        av_packet_free(&pkt);
    }
}

// The sample index of the next frame the device would play.
int next_sample(AudioPlayer& player)
{
    float out[2] = {};
    player.fill_buffer(out, 1);
    return static_cast<int>(out[0] * 32768.0f + 0.5f);
}

struct Pcm {
    AVCodecParameters* par = avcodec_parameters_alloc();
    Pcm()
    {
        par->codec_type  = AVMEDIA_TYPE_AUDIO;
        par->codec_id    = AV_CODEC_ID_PCM_S16LE;
        par->sample_rate = kRate;
        par->format      = AV_SAMPLE_FMT_S16;
        av_channel_layout_default(&par->ch_layout, 2);
    }
    ~Pcm() { avcodec_parameters_free(&par); }
};

} // namespace

TEST_CASE("AudioDecoder::start_at — the audio starts at that sample", "[audio]")
{
    Pcm pcm;
    AudioPlayer player;
    player.init_ring(kRate);
    AudioDecoder dec;
    REQUIRE(dec.open(pcm.par, player, { 1, kRate }));

    SECTION("inside a packet: the ones before dropped, that one trimmed") {
        dec.start_at(0.025);
        feed(dec, player, 0, 4);   // 0 .. 40 ms
        CHECK_THAT(player.buffered_seconds(), WithinAbs(0.015, 1e-9));
        CHECK(next_sample(player) == 1200);   // 25 ms
        CHECK(next_sample(player) == 1201);
    }
    SECTION("a flush (the switch / seek sentinel) keeps it") {
        dec.start_at(0.025);
        dec.flush();
        feed(dec, player, 0, 4);
        CHECK(next_sample(player) == 1200);
    }
    SECTION("before the first packet, or none: everything") {
        dec.start_at(0.0);
        feed(dec, player, 960, 2);   // 20 .. 40 ms
        CHECK(next_sample(player) == 960);
        player.flush();
        dec.start_at(-1.0);
        feed(dec, player, 0, 1);
        CHECK(next_sample(player) == 0);
    }
    SECTION("taken once: a later packet before it is not trimmed again") {
        dec.start_at(0.015);
        feed(dec, player, 0, 3);   // 0 .. 30 ms
        CHECK_THAT(player.buffered_seconds(), WithinAbs(0.015, 1e-9));
        feed(dec, player, 0, 1);   // (a stream that steps back: played, not dropped)
        CHECK_THAT(player.buffered_seconds(), WithinAbs(0.025, 1e-9));
    }
}
