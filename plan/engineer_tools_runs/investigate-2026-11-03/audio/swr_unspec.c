// AudioDecoder::open's swr call with an input layout that has no channel order
// (6 channels, AV_CHANNEL_ORDER_UNSPEC: e.g. PCM without a channel mask).
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
int main(void)
{
    AVChannelLayout in = { .order = AV_CHANNEL_ORDER_UNSPEC, .nb_channels = 6 };
    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    SwrContext* swr = NULL;
    int r = swr_alloc_set_opts2(&swr, &stereo, AV_SAMPLE_FMT_FLT, 48000, &in, AV_SAMPLE_FMT_S16, 48000, 0, NULL);
    printf("swr_alloc_set_opts2: %d\n", r);
    r = swr_init(swr);
    printf("swr_init: %d\n", r);
    if (r < 0) return 0;
    // channel 3 (LFE in a default 5.1) and channel 2 (FC) gains
    for (int ch = 2; ch <= 3; ch++) {
        int16_t* buf = calloc(6 * 4800, sizeof(int16_t));
        for (int k = 0; k < 4800; k++) buf[6 * k + ch] = (int16_t)(16384 * sin(2 * M_PI * 1000 * k / 48000.0));
        float out[2 * 5000];
        uint8_t* o = (uint8_t*)out; const uint8_t* i = (const uint8_t*)buf;
        int got = swr_convert(swr, &o, 5000, &i, 4800);
        double pk = 0; for (int k = 0; k < got; k++) if (fabs(out[2 * k]) > pk) pk = fabs(out[2 * k]);
        printf("input channel %d at peak 0.5: output L peak %.4f\n", ch, pk);
        free(buf);
    }
    swr_free(&swr);
    return 0;
}
