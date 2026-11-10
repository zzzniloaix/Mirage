// The SwrContext src/AudioDecoder.cpp:34-43 builds (stereo, FLT, 48 kHz out;
// the codec's layout / format / rate in; no options), measured: the gain from
// each input channel to output L and R, plus variants with lfe_mix_level set.
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_defaults(void)
{
    SwrContext* s = swr_alloc();
    const char* names[] = { "center_mix_level", "surround_mix_level", "lfe_mix_level",
                            "rematrix_volume", "rematrix_maxval" };
    for (int i = 0; i < 5; i++) {
        double v = NAN;
        av_opt_get_double(s, names[i], 0, &v);
        printf("default %-18s = %.6f\n", names[i], v);
    }
    int64_t me = -1;
    av_opt_get_int(s, "matrix_encoding", 0, &me);
    printf("default %-18s = %lld (AV_MATRIX_ENCODING_NONE = %d)\n", "matrix_encoding",
           (long long)me, AV_MATRIX_ENCODING_NONE);
    swr_free(&s);
}

// AudioDecoder::open's call; lfe >= -32 also sets lfe_mix_level first.
static SwrContext* build(const AVChannelLayout* in, int in_rate, double lfe)
{
    SwrContext* swr = NULL;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    if (swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, 48000, in, AV_SAMPLE_FMT_FLTP,
                            in_rate, 0, NULL) < 0)
        return NULL;
    if (lfe >= -32 && av_opt_set_double(swr, "lfe_mix_level", lfe, 0) < 0) {
        swr_free(&swr);
        return NULL;
    }
    if (swr_init(swr) < 0) { swr_free(&swr); return NULL; }
    return swr;
}

// 2 s of a sine (amp, freq) in the channels of `mask` (bit i = channel i),
// zeros elsewhere. Output RMS / peak per channel over 0.2 .. end, and the
// signed projection coefficient on the input sine (same rate only).
typedef struct { double rms[2], peak[2], coef[2]; } Meas;

static Meas run(SwrContext* swr, int nch, unsigned mask, double amp, double freq, int in_rate)
{
    Meas m = { { 0 }, { 0 }, { 0 } };
    const int chunk = 1024, total = 2 * in_rate;
    float* in[64] = { 0 };
    for (int c = 0; c < nch; c++) in[c] = calloc(chunk, sizeof(float));
    float* out = malloc(sizeof(float) * 2 * 8192);
    double ss[2] = { 0, 0 }, sx[2] = { 0, 0 }, xx = 0;
    long long n_out = 0, counted = 0;
    const long long skip = 48000 / 5;
    for (int pos = 0; pos < total + chunk; pos += chunk) {
        int n = (pos < total) ? chunk : 0;   // the last pass flushes
        for (int c = 0; c < nch; c++)
            for (int k = 0; k < n; k++)
                in[c][k] = (mask >> c & 1) ? (float)(amp * sin(2 * M_PI * freq * (pos + k) / in_rate)) : 0.f;
        int got = swr_convert(swr, (uint8_t**)&out, 8192, n ? (const uint8_t**)in : NULL, n);
        for (int k = 0; k < got; k++, n_out++) {
            if (n_out < skip) continue;
            double x = amp * sin(2 * M_PI * freq * n_out / 48000.0);   // the input, same rate only
            for (int c = 0; c < 2; c++) {
                double y = out[2 * k + c];
                ss[c] += y * y;
                sx[c] += y * x;
                if (fabs(y) > m.peak[c]) m.peak[c] = fabs(y);
            }
            xx += x * x;
            counted++;
        }
    }
    for (int c = 0; c < 2; c++) {
        m.rms[c]  = sqrt(ss[c] / (double)counted);
        m.coef[c] = (in_rate == 48000) ? sx[c] / xx : NAN;
    }
    for (int c = 0; c < nch; c++) free(in[c]);
    free(out);
    return m;
}

static const char* db(double ratio, char* buf)
{
    if (ratio < 1e-9) snprintf(buf, 32, "-inf dB");
    else snprintf(buf, 32, "%+.2f dB", 20 * log10(ratio));
    return buf;
}

static void per_channel(const AVChannelLayout* lay, int in_rate, double lfe, const char* label)
{
    char name[64];
    av_channel_layout_describe(lay, name, sizeof name);
    printf("\n== in %s fltp %d Hz -> stereo flt 48000 Hz, %s ==\n", name, in_rate, label);
    const double amp = 0.5;
    const double in_rms = amp / sqrt(2.0);
    for (int c = 0; c < lay->nb_channels; c++) {
        SwrContext* swr = build(lay, in_rate, lfe);
        if (!swr) { printf("build failed\n"); return; }
        enum AVChannel ch = av_channel_layout_channel_from_index(lay, c);
        char cn[32];
        av_channel_name(cn, sizeof cn, ch);
        const double f = (ch == AV_CHAN_LOW_FREQUENCY) ? 60.0 : 1000.0;
        Meas m = run(swr, lay->nb_channels, 1u << c, amp, f, in_rate);
        char b0[32], b1[32];
        printf("  %-4s (%4.0f Hz sine, peak %.2f): L rms gain %-9s coef %+.6f | R rms gain %-9s coef %+.6f | out peak L %.4f R %.4f\n",
               cn, f, amp, db(m.rms[0] / in_rms, b0), m.coef[0], db(m.rms[1] / in_rms, b1), m.coef[1],
               m.peak[0], m.peak[1]);
        swr_free(&swr);
    }
}

int main(void)
{
    printf("libswresample %s\n", LIBSWRESAMPLE_IDENT);
    print_defaults();

    AVChannelLayout l51side = AV_CHANNEL_LAYOUT_5POINT1;        // FL FR FC LFE SL SR
    AVChannelLayout l51back = AV_CHANNEL_LAYOUT_5POINT1_BACK;   // FL FR FC LFE BL BR
    AVChannelLayout l71     = AV_CHANNEL_LAYOUT_7POINT1;

    per_channel(&l51side, 48000, -100, "AudioDecoder::open defaults (no options)");
    per_channel(&l51back, 48000, -100, "AudioDecoder::open defaults (no options)");
    per_channel(&l71,     48000, -100, "AudioDecoder::open defaults (no options)");
    per_channel(&l51side, 44100, -100, "defaults, resampled 44.1 -> 48 kHz");

    // lfe_mix_level is linear: each of L and R gets lfe_mix_level * 0.7071.
    per_channel(&l51side, 48000, 1.0, "lfe_mix_level = 1.0");
    per_channel(&l51side, 48000, 0.447214, "lfe_mix_level = 0.447214 (= 0.316228 / 0.707107)");

    // No normalisation for float out: FL + FC + SL in phase, 0.5 each.
    {
        SwrContext* swr = build(&l51side, 48000, -100);
        Meas m = run(swr, 6, (1u << 0) | (1u << 2) | (1u << 4), 0.5, 1000.0, 48000);
        printf("\n== FL+FC+SL in phase, peak 0.5 each, defaults: out peak L %.4f (expected 0.5*(1+0.707107+0.707107) = %.4f) ==\n",
               m.peak[0], 0.5 * (1 + 2 * 0.707107));
        swr_free(&swr);
        // all six channels at 0.5
        swr = build(&l51side, 48000, -100);
        m = run(swr, 6, 0x3f, 0.5, 1000.0, 48000);
        printf("== all 6 channels in phase, peak 0.5 each (LFE at 1 kHz here), defaults: out peak L %.4f R %.4f ==\n",
               m.peak[0], m.peak[1]);
        swr_free(&swr);
    }
    return 0;
}
