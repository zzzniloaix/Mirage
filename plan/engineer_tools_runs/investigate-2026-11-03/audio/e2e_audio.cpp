// End to end through the player's own AudioDecoder + AudioPlayer (compiled
// from the worktree's src/, unmodified): demux a file's first audio stream,
// AudioDecoder::decode every packet (as MediaSession's audio thread does),
// drain the ring with AudioPlayer::fill_buffer (as the device callback does),
// then report the output level per channel and the W strip's buffer
// (AudioPlayer::copy_waveform, as main.cpp:3020-3022 reads it).
//
// usage: e2e_audio <file> [speed]

#include "AudioDecoder.h"
#include "AudioPlayer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
}

int main(int argc, char** argv)
{
    if (argc < 2) { std::fprintf(stderr, "usage: e2e_audio <file> [speed]\n"); return 2; }
    const double speed = argc > 2 ? std::atof(argv[2]) : 1.0;

    AVFormatContext* fmt = nullptr;
    if (avformat_open_input(&fmt, argv[1], nullptr, nullptr) < 0 || avformat_find_stream_info(fmt, nullptr) < 0)
        return 1;
    const int ai = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (ai < 0) return 1;
    AVStream* st = fmt->streams[ai];

    AudioPlayer player;
    player.init_ring(48000);              // MediaSession.cpp:506 opens the device at 48000
    AudioDecoder dec;
    if (!dec.open(st->codecpar, player, st->time_base)) return 1;
    dec.set_speed(speed);

    std::vector<float> out;               // everything the device would have played
    float buf[2 * 512];
    auto drain = [&] {   // exactly what is buffered: no zero padding between packets
        for (;;) {
            const int avail = static_cast<int>(std::lround(player.buffered_seconds() * 48000.0));
            if (avail <= 0) break;
            const int n = std::min(avail, 512);
            player.fill_buffer(buf, n);
            out.insert(out.end(), buf, buf + 2 * n);
        }
    };

    AVPacket* pkt = av_packet_alloc();
    while (av_read_frame(fmt, pkt) >= 0) {
        if (pkt->stream_index == ai) {
            dec.decode(pkt, player);
            drain();
        }
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);

    // Level per output channel over the middle (0.2 s in from either end).
    const std::size_t frames = out.size() / 2, skip = 48000 / 5;
    for (int c = 0; c < 2; ++c) {
        double ss = 0.0, peak = 0.0;
        std::size_t n = 0;
        for (std::size_t i = skip; i + skip < frames; ++i, ++n) {
            const double y = out[2 * i + c];
            ss += y * y;
            peak = std::max(peak, std::abs(y));
        }
        const double rms = n ? std::sqrt(ss / static_cast<double>(n)) : 0.0;
        std::printf("out %s: rms %s, peak %.4f (%zu frames, %.3f s output)\n", c ? "R" : "L",
                    rms > 1e-9 ? std::to_string(20 * std::log10(rms)).append(" dBFS").c_str() : "-inf dBFS",
                    peak, frames, frames / 48000.0);
    }

    // The W strip's data.
    std::vector<float> wave(AudioPlayer::kWaveCap);
    player.copy_waveform(wave.data(), AudioPlayer::kWaveCap);
    int nonzero = 0;
    float mx = 0.0f;
    for (float v : wave) { nonzero += v > 0.0f; mx = std::max(mx, v); }
    std::printf("W strip: %d entries x %d samples = %d samples = %.3f s at 48 kHz; non-zero %d; max %.4f; "
                "oldest %.4f, 1/4 %.4f, middle %.4f, 3/4 %.4f, newest %.4f\n",
                AudioPlayer::kWaveCap, AudioPlayer::kWaveStride, AudioPlayer::kWaveCap * AudioPlayer::kWaveStride,
                AudioPlayer::kWaveCap * AudioPlayer::kWaveStride / 48000.0, nonzero, mx,
                wave[0], wave[AudioPlayer::kWaveCap / 4], wave[AudioPlayer::kWaveCap / 2],
                wave[3 * AudioPlayer::kWaveCap / 4], wave[AudioPlayer::kWaveCap - 1]);
    avformat_close_input(&fmt);
    return 0;
}
