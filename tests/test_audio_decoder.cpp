// AudioDecoder::start_at: where a track switch's new audio takes over, or a
// seek's target — to the sample. PCM 48 kHz stereo in and out, so nothing
// is resampled in between; each sample's value is its index.
// And the downmix to stereo: FFmpeg's default levels for 5.1 PCM, the levels
// an AC-3 stream carries (against aresample's output for the same frames),
// a change of them in mid-stream, and what the resampler held at a change.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "AudioDecoder.h"
#include "AudioPlayer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <numbers>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
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

// ── The downmix to stereo ─────────────────────────────────────────────────────

namespace {

const AVChannelLayout kLayout51 = AV_CHANNEL_LAYOUT_5POINT1;   // FL FR FC LFE SL SR

// What the device would play of what is buffered now, appended to `out`.
void drain_into(AudioPlayer& player, std::vector<float>& out)
{
    float buf[2 * 512];
    for (;;) {
        const int avail = static_cast<int>(std::lround(player.buffered_seconds() * player.sample_rate()));
        if (avail <= 0) break;
        const int n = std::min(avail, 512);
        player.fill_buffer(buf, n);
        out.insert(out.end(), buf, buf + 2 * n);
    }
}

// `n` 10 ms packets of 5.1 float PCM through AudioDecoder and AudioPlayer:
// channel `ch` at `v`, the others silent.
std::vector<float> play_pcm51(int ch, float v, int n)
{
    AVCodecParameters* par = avcodec_parameters_alloc();
    par->codec_type  = AVMEDIA_TYPE_AUDIO;
    par->codec_id    = AV_CODEC_ID_PCM_F32LE;
    par->sample_rate = kRate;
    par->format      = AV_SAMPLE_FMT_FLT;
    REQUIRE(av_channel_layout_copy(&par->ch_layout, &kLayout51) >= 0);
    AudioPlayer player;
    player.init_ring(kRate);
    AudioDecoder dec;
    REQUIRE(dec.open(par, player, { 1, kRate }));
    std::vector<float> out;
    for (int i = 0; i < n; ++i) {
        AVPacket* pkt = av_packet_alloc();
        REQUIRE(av_new_packet(pkt, kPkt * 6 * 4) >= 0);
        auto* s = reinterpret_cast<float*>(pkt->data);
        for (int k = 0; k < kPkt * 6; ++k) s[k] = (k % 6 == ch) ? v : 0.0f;
        pkt->pts = pkt->dts = i * kPkt;
        pkt->duration = kPkt;
        dec.decode(pkt, player);
        av_packet_free(&pkt);
        drain_into(player, out);
    }
    avcodec_parameters_free(&par);
    return out;
}

// 5.1 AC-3 from FFmpeg's encoder: a 1 kHz sine at 0.5 in the centre, a
// 400 Hz one at 0.5 in the left surround, the other channels silent;
// `options` for the encoder (its mix levels). Pts follow on from `first`.
struct Ac3 {
    AVCodecParameters*     par = avcodec_parameters_alloc();
    std::vector<AVPacket*> pkts;
    std::int64_t           end = 0;   // pts after the last packet
    ~Ac3()
    {
        for (AVPacket*& p : pkts) av_packet_free(&p);
        avcodec_parameters_free(&par);
    }
};
bool encode_ac3(Ac3& a, int rate, int frames, const char* options, std::int64_t first = 0)
{
    const AVCodec* codec = avcodec_find_encoder_by_name("ac3");
    if (!codec) return false;
    AVCodecContext* enc = avcodec_alloc_context3(codec);
    REQUIRE(enc);
    enc->sample_rate = rate;
    enc->sample_fmt  = AV_SAMPLE_FMT_FLTP;
    enc->bit_rate    = 448000;
    enc->time_base   = { 1, rate };
    REQUIRE(av_channel_layout_copy(&enc->ch_layout, &kLayout51) >= 0);
    AVDictionary* opts = nullptr;
    REQUIRE(av_dict_parse_string(&opts, options, "=", ":", 0) >= 0);
    REQUIRE(avcodec_open2(enc, codec, &opts) >= 0);
    av_dict_free(&opts);

    AVFrame* f = av_frame_alloc();
    f->format      = enc->sample_fmt;
    f->sample_rate = rate;
    f->nb_samples  = enc->frame_size;
    REQUIRE(av_channel_layout_copy(&f->ch_layout, &kLayout51) >= 0);
    REQUIRE(av_frame_get_buffer(f, 0) >= 0);
    AVPacket* pkt = av_packet_alloc();
    auto take = [&](const AVFrame* in) {
        REQUIRE(avcodec_send_frame(enc, in) >= 0);
        while (avcodec_receive_packet(enc, pkt) == 0) {
            pkt->pts = pkt->dts = first + pkt->pts;
            a.end = pkt->pts + pkt->duration;
            a.pkts.push_back(av_packet_clone(pkt));
            av_packet_unref(pkt);
        }
    };
    for (int n = 0; n < frames; ++n) {
        REQUIRE(av_frame_make_writable(f) >= 0);
        for (int ch = 0; ch < 6; ++ch) {
            auto* d = reinterpret_cast<float*>(f->data[ch]);
            for (int i = 0; i < f->nb_samples; ++i) {
                const double t = static_cast<double>(n * f->nb_samples + i) / rate;
                d[i] = ch == 2 ? static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 1000.0 * t))
                     : ch == 4 ? static_cast<float>(0.5 * std::sin(2.0 * std::numbers::pi * 400.0 * t))
                     : 0.0f;
            }
        }
        f->pts = static_cast<std::int64_t>(n) * f->nb_samples;
        take(f);
    }
    take(nullptr);
    REQUIRE(avcodec_parameters_from_context(a.par, enc) >= 0);
    av_packet_free(&pkt);
    av_frame_free(&f);
    avcodec_free_context(&enc);
    return true;
}

// The packets decoded by a decoder of their own: the frames AudioDecoder
// gets, side data included.
std::vector<AVFrame*> decode_frames(const AVCodecParameters* par, const std::vector<AVPacket*>& pkts)
{
    const AVCodec* codec = avcodec_find_decoder(par->codec_id);
    REQUIRE(codec);
    AVCodecContext* dec = avcodec_alloc_context3(codec);
    REQUIRE(avcodec_parameters_to_context(dec, par) >= 0);
    REQUIRE(avcodec_open2(dec, codec, nullptr) >= 0);
    std::vector<AVFrame*> frames;
    AVFrame* f = av_frame_alloc();
    for (AVPacket* p : pkts) {
        REQUIRE(avcodec_send_packet(dec, p) >= 0);
        while (avcodec_receive_frame(dec, f) == 0) frames.push_back(av_frame_clone(f));
    }
    av_frame_free(&f);
    avcodec_free_context(&dec);
    return frames;
}

void free_frames(std::vector<AVFrame*>& frames)
{
    for (AVFrame*& f : frames) av_frame_free(&f);
}

// The packets through AudioDecoder and AudioPlayer (out at 48 kHz).
std::vector<float> play(const AVCodecParameters* par, const std::vector<AVPacket*>& pkts)
{
    AudioPlayer player;
    player.init_ring(kRate);
    AudioDecoder dec;
    REQUIRE(dec.open(const_cast<AVCodecParameters*>(par), player, { 1, par->sample_rate }));
    std::vector<float> out;
    for (AVPacket* p : pkts) {
        dec.decode(p, player);
        drain_into(player, out);
    }
    return out;
}

const AVDownmixInfo* downmix_info(const AVFrame* f)
{
    const AVFrameSideData* sd = av_frame_get_side_data(f, AV_FRAME_DATA_DOWNMIX_INFO);
    return sd ? reinterpret_cast<const AVDownmixInfo*>(sd->data) : nullptr;
}

// Lo/Ro from 5.1 at the frame's own levels, as swr mixes it: L = FL + c·FC +
// s·SL, R = FR + c·FC + s·SR (no LFE), interleaved.
std::vector<float> lo_ro(const std::vector<AVFrame*>& frames)
{
    std::vector<float> out;
    for (const AVFrame* f : frames) {
        const AVDownmixInfo* di = downmix_info(f);
        REQUIRE(di);
        const auto ch = [f](int c) { return reinterpret_cast<const float*>(f->data[c]); };
        for (int i = 0; i < f->nb_samples; ++i) {
            const double c = di->center_mix_level * ch(2)[i];
            out.push_back(static_cast<float>(ch(0)[i] + c + di->surround_mix_level * ch(4)[i]));
            out.push_back(static_cast<float>(ch(1)[i] + c + di->surround_mix_level * ch(5)[i]));
        }
    }
    return out;
}

// The frames through FFmpeg's aresample=ochl=stereo:osf=flt, given the
// first frame's side data as the command line gives it (aresample reads the
// downmix info there when it configures its resampler).
std::vector<float> aresample_stereo(const std::vector<AVFrame*>& frames)
{
    AVFilterGraph* g = avfilter_graph_alloc();
    AVFilterContext* src = avfilter_graph_alloc_filter(g, avfilter_get_by_name("abuffer"), "in");
    REQUIRE(src);
    AVBufferSrcParameters* bp = av_buffersrc_parameters_alloc();
    bp->format       = frames[0]->format;
    bp->sample_rate  = frames[0]->sample_rate;
    bp->time_base    = { 1, frames[0]->sample_rate };
    bp->side_data    = frames[0]->side_data;
    bp->nb_side_data = frames[0]->nb_side_data;
    REQUIRE(av_channel_layout_copy(&bp->ch_layout, &frames[0]->ch_layout) >= 0);
    REQUIRE(av_buffersrc_parameters_set(src, bp) >= 0);
    av_channel_layout_uninit(&bp->ch_layout);
    av_free(bp);
    REQUIRE(avfilter_init_str(src, nullptr) >= 0);
    AVFilterContext* rs   = nullptr;
    AVFilterContext* sink = nullptr;
    REQUIRE(avfilter_graph_create_filter(&rs, avfilter_get_by_name("aresample"), "rs",
                                         "ochl=stereo:osf=flt", nullptr, g) >= 0);
    REQUIRE(avfilter_graph_create_filter(&sink, avfilter_get_by_name("abuffersink"), "out",
                                         nullptr, nullptr, g) >= 0);
    REQUIRE(avfilter_link(src, 0, rs, 0) >= 0);
    REQUIRE(avfilter_link(rs, 0, sink, 0) >= 0);
    REQUIRE(avfilter_graph_config(g, nullptr) >= 0);

    std::vector<float> out;
    AVFrame* o = av_frame_alloc();
    auto pull = [&] {
        while (av_buffersink_get_frame(sink, o) >= 0) {
            REQUIRE(o->format == AV_SAMPLE_FMT_FLT);
            REQUIRE(o->ch_layout.nb_channels == 2);
            const auto* d = reinterpret_cast<const float*>(o->data[0]);
            out.insert(out.end(), d, d + 2 * o->nb_samples);
            av_frame_unref(o);
        }
    };
    for (AVFrame* f : frames) {
        REQUIRE(av_buffersrc_add_frame_flags(src, f, AV_BUFFERSRC_FLAG_KEEP_REF) >= 0);
        pull();
    }
    REQUIRE(av_buffersrc_add_frame(src, nullptr) >= 0);
    pull();
    av_frame_free(&o);
    avfilter_graph_free(&g);
    return out;
}

float max_diff(const std::vector<float>& a, const std::vector<float>& b)
{
    REQUIRE(a.size() == b.size());
    float m = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i) m = std::max(m, std::abs(a[i] - b[i]));
    return m;
}

} // namespace

TEST_CASE("AudioDecoder: 5.1 PCM to stereo — the LFE left out, the centre at 0.707107", "[audio]")
{
    const std::vector<float> lfe = play_pcm51(3, 0.5f, 4);
    REQUIRE(lfe.size() == 4 * kPkt * 2);
    CHECK(*std::max_element(lfe.begin(), lfe.end()) == 0.0f);
    CHECK(*std::min_element(lfe.begin(), lfe.end()) == 0.0f);

    const std::vector<float> fc = play_pcm51(2, 0.5f, 4);
    REQUIRE(fc.size() == 4 * kPkt * 2);
    CHECK_THAT(*std::max_element(fc.begin(), fc.end()), WithinAbs(0.5 * 0.707107, 1e-6));
    CHECK_THAT(*std::min_element(fc.begin(), fc.end()), WithinAbs(0.5 * 0.707107, 1e-6));
}

TEST_CASE("AudioDecoder: an AC-3 stream's downmix levels, as aresample applies them", "[audio]")
{
    Ac3 a;
    if (!encode_ac3(a, kRate, 40, "")) SKIP("FFmpeg's AC-3 encoder is not available");
    std::vector<AVFrame*> frames = decode_frames(a.par, a.pkts);
    REQUIRE_FALSE(frames.empty());
    const AVDownmixInfo* di = downmix_info(frames[0]);
    if (!di) SKIP("this FFmpeg's AC-3 frames carry no downmix side data");
    // The encoder's default levels (-4.5 dB, -6 dB), not swr's 0.707107.
    INFO("centre " << di->center_mix_level << ", surround " << di->surround_mix_level);
    CHECK(std::abs(di->center_mix_level - 0.707107) > 0.01);
    CHECK(std::abs(di->surround_mix_level - 0.707107) > 0.01);

    const std::vector<float> out = play(a.par, a.pkts);
    CHECK(max_diff(out, lo_ro(frames)) < 1e-6f);
    CHECK(max_diff(out, aresample_stereo(frames)) < 1e-6f);
    free_frames(frames);
}

TEST_CASE("AudioDecoder: downmix levels that change in mid-stream are followed", "[audio]")
{
    // Two AC-3 encodes one after the other, the second with other levels.
    Ac3 a, b;
    if (!encode_ac3(a, kRate, 20, "")) SKIP("FFmpeg's AC-3 encoder is not available");
    REQUIRE(encode_ac3(b, kRate, 20, "center_mixlev=0.5:surround_mixlev=0", a.end));
    std::vector<AVPacket*> both = a.pkts;
    both.insert(both.end(), b.pkts.begin(), b.pkts.end());
    std::vector<AVFrame*> frames = decode_frames(a.par, both);
    REQUIRE(frames.size() == both.size());
    const AVDownmixInfo* first = downmix_info(frames.front());
    const AVDownmixInfo* last  = downmix_info(frames.back());
    if (!first || !last) SKIP("this FFmpeg's AC-3 frames carry no downmix side data");
    REQUIRE(first->center_mix_level != last->center_mix_level);
    REQUIRE(first->surround_mix_level != last->surround_mix_level);

    CHECK(max_diff(play(a.par, both), lo_ro(frames)) < 1e-6f);
    free_frames(frames);
}

TEST_CASE("AudioDecoder: a new resampler in mid-stream plays what the old one held", "[audio]")
{
    // 44.1 kHz in, 48 kHz out: the resampler holds its filter's delay. The
    // levels change between the two encodes, so the resampler is made again
    // there; the old one drained first, the output is as long as that of one
    // resampler over all of it (which a rebuild without the drain falls
    // short of by that delay).
    constexpr int kIn = 44100;
    Ac3 a, b;
    if (!encode_ac3(a, kIn, 20, "")) SKIP("FFmpeg's AC-3 encoder is not available");
    REQUIRE(encode_ac3(b, kIn, 20, "center_mixlev=0.5", a.end));
    std::vector<AVPacket*> both = a.pkts;
    both.insert(both.end(), b.pkts.begin(), b.pkts.end());
    std::vector<AVFrame*> frames = decode_frames(a.par, both);
    REQUIRE(frames.size() == both.size());
    if (!downmix_info(frames.front())) SKIP("this FFmpeg's AC-3 frames carry no downmix side data");

    SwrContext* one = nullptr;
    const AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    REQUIRE(swr_alloc_set_opts2(&one, &stereo, AV_SAMPLE_FMT_FLT, kRate, &frames[0]->ch_layout,
                                static_cast<AVSampleFormat>(frames[0]->format), kIn, 0, nullptr) >= 0);
    REQUIRE(swr_init(one) >= 0);
    std::int64_t one_out = 0, held = -1;
    std::vector<float> buf;
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (i == a.pkts.size()) held = swr_get_delay(one, kRate);   // at the change
        const int n = swr_get_out_samples(one, frames[i]->nb_samples);
        buf.resize(2 * static_cast<std::size_t>(n));
        auto* o = reinterpret_cast<std::uint8_t*>(buf.data());
        const int got = swr_convert(one, &o, n, const_cast<const std::uint8_t**>(frames[i]->extended_data),
                                    frames[i]->nb_samples);
        REQUIRE(got >= 0);
        one_out += got;
    }
    swr_free(&one);

    constexpr std::int64_t kSlack = 2;   // frames: the rebuild restarts the resampler's phase
    INFO("one resampler: " << one_out << " frames, holding " << held << " at the change");
    REQUIRE(held > kSlack);
    const auto out = static_cast<std::int64_t>(play(a.par, both).size() / 2);
    CHECK(std::abs(out - one_out) <= kSlack);
    free_frames(frames);
}

TEST_CASE("AudioDecoder::stream_mix_levels: Lo/Ro, Lt/Rt, the LFE only when the stream gives it", "[audio]")
{
    AVDownmixInfo di{};
    di.center_mix_level        = 0.594604;
    di.surround_mix_level      = 0.5;
    di.center_mix_level_ltrt   = 0.707107;
    di.surround_mix_level_ltrt = 0.841395;
    di.lfe_mix_level           = 0.0;

    di.preferred_downmix_type = AV_DOWNMIX_TYPE_UNKNOWN;
    AudioDecoder::MixLevels lv = AudioDecoder::stream_mix_levels(di);
    CHECK(lv.from_stream);
    CHECK(lv.center == 0.594604);
    CHECK(lv.surround == 0.5);
    CHECK(lv.lfe == 0.0);
    CHECK(lv.matrix == AV_MATRIX_ENCODING_NONE);

    di.preferred_downmix_type = AV_DOWNMIX_TYPE_LORO;
    CHECK(AudioDecoder::stream_mix_levels(di) == lv);

    di.preferred_downmix_type = AV_DOWNMIX_TYPE_LTRT;
    lv = AudioDecoder::stream_mix_levels(di);
    CHECK(lv.center == 0.707107);
    CHECK(lv.surround == 0.841395);
    CHECK(lv.matrix == AV_MATRIX_ENCODING_DOLBY);

    di.preferred_downmix_type = AV_DOWNMIX_TYPE_DPLII;
    CHECK(AudioDecoder::stream_mix_levels(di).matrix == AV_MATRIX_ENCODING_DPLII);

    di.lfe_mix_level = 0.316228;   // E-AC-3's LFE mix level, -10 dB
    CHECK(AudioDecoder::stream_mix_levels(di).lfe == 0.316228);
}
