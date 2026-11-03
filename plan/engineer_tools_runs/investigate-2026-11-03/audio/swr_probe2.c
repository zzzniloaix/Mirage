// Two traps next to the defaults: lfe_mix_level read as dB, and rematrix_maxval = 1.
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static double coef(const char* opt, double val, unsigned mask, double freq, int out_ch, double* peak)
{
    SwrContext* swr = NULL;
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO, in = AV_CHANNEL_LAYOUT_5POINT1;
    swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, 48000, &in, AV_SAMPLE_FMT_FLTP, 48000, 0, NULL);
    if (opt) av_opt_set_double(swr, opt, val, 0);
    if (swr_init(swr) < 0) { printf("swr_init failed\n"); exit(1); }
    float* planes[6]; for (int c = 0; c < 6; c++) planes[c] = malloc(48000 * sizeof(float));
    for (int c = 0; c < 6; c++) for (int k = 0; k < 48000; k++)
        planes[c][k] = (mask >> c & 1) ? (float)(0.5 * sin(2 * M_PI * freq * k / 48000.0)) : 0.f;
    float* out = malloc(2 * 50000 * sizeof(float));
    int got = swr_convert(swr, (uint8_t**)&out, 50000, (const uint8_t**)planes, 48000);
    double sx = 0, xx = 0; *peak = 0;
    for (int k = 0; k < got; k++) {
        double x = 0.5 * sin(2 * M_PI * freq * k / 48000.0), y = out[2 * k + out_ch];
        sx += x * y; xx += x * x; if (fabs(y) > *peak) *peak = fabs(y);
    }
    swr_free(&swr);
    return sx / xx;
}

int main(void)
{
    double pk;
    double c = coef("lfe_mix_level", -10.0, 1u << 3, 60.0, 0, &pk);
    printf("lfe_mix_level = -10 (read as dB): LFE->L coef %+.6f (%+.2f dB, polarity %s), out peak %.4f for in peak 0.5\n",
           c, 20 * log10(fabs(c)), c < 0 ? "inverted" : "kept", pk);
    c = coef(NULL, 0, (1u << 0) | (1u << 2) | (1u << 4), 1000.0, 0, &pk);
    printf("defaults, FL+FC+SL 0.5 each: L coef-sum %.6f, out peak %.4f\n", c, pk);
    c = coef("rematrix_maxval", 1.0, (1u << 0) | (1u << 2) | (1u << 4), 1000.0, 0, &pk);
    printf("rematrix_maxval = 1, FL+FC+SL 0.5 each: L coef-sum %.6f, out peak %.4f\n", c, pk);
    c = coef("rematrix_maxval", 1.0, 1u << 0, 1000.0, 0, &pk);
    printf("rematrix_maxval = 1, FL only: FL->L coef %.6f (%+.2f dB)\n", c, 20 * log10(c));
    return 0;
}
