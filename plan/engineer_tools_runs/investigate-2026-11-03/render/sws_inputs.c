#include <libswscale/swscale.h>
#include <libavutil/pixdesc.h>
#include <stdio.h>
int main(void) {
    const enum AVPixelFormat f[] = { AV_PIX_FMT_NV12, AV_PIX_FMT_P010LE, AV_PIX_FMT_NV16, AV_PIX_FMT_P210LE, AV_PIX_FMT_P216LE,
        AV_PIX_FMT_NV24, AV_PIX_FMT_P410LE, AV_PIX_FMT_P416LE, AV_PIX_FMT_AYUV64LE, AV_PIX_FMT_UYVY422, AV_PIX_FMT_BGRA, AV_PIX_FMT_NONE };
    for (int i = 0; f[i] != AV_PIX_FMT_NONE; ++i)
        printf("%-10s sws input: %d\n", av_get_pix_fmt_name(f[i]), sws_isSupportedInput(f[i]));
    return 0;
}
