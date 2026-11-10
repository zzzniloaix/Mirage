// The SW path of VideoRenderer, as build_sws() and upload() set it up
// (src/VideoRenderer.cpp:526-564, 615-617): sws_getContext(SWS_BILINEAR) to
// RGB24 (SDR) or RGB48LE (HDR), sws_setColorspaceDetails(stream matrix,
// src range, BT.709/BT.2020 table, dst full, 0, 1<<16, 1<<16).
// Feeds the first frame of a raw planar file and prints the bar centres.
// Usage: sws_check <raw> <pix_fmt> <sdr|hdr> <709|2020> [tv|pc]
//        sws_check --vt      (is AV_PIX_FMT_VIDEOTOOLBOX a supported sws input?)
//        sws_check --time W H <pix_fmt> <sdr|hdr>   (context build cost)
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/frame.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void)
{
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int g_flags = SWS_BILINEAR;
static struct SwsContext* build(int w, int h, enum AVPixelFormat src, int hdr, int cs, int full)
{
    const enum AVPixelFormat dst = hdr ? AV_PIX_FMT_RGB48LE : AV_PIX_FMT_RGB24;
    if (getenv("SWSF")) g_flags = (int)strtol(getenv("SWSF"), NULL, 0);
    struct SwsContext* s = sws_getContext(w, h, src, w, h, dst, g_flags, NULL, NULL, NULL);
    if (!s) return NULL;
    int r = sws_setColorspaceDetails(s, sws_getCoefficients(cs), full, sws_getCoefficients(hdr ? SWS_CS_BT2020 : SWS_CS_ITU709),
                                     1, 0, 1 << 16, 1 << 16);
    if (r < 0) fprintf(stderr, "sws_setColorspaceDetails: %d\n", r);
    return s;
}

int main(int argc, char** argv)
{
    if (argc >= 2 && !strcmp(argv[1], "--vt")) {
        printf("sws_isSupportedInput(AV_PIX_FMT_VIDEOTOOLBOX) = %d\n", sws_isSupportedInput(AV_PIX_FMT_VIDEOTOOLBOX));
        struct SwsContext* s = sws_getContext(1280, 720, AV_PIX_FMT_VIDEOTOOLBOX, 1280, 720, AV_PIX_FMT_RGB24, SWS_BILINEAR, NULL, NULL, NULL);
        printf("sws_getContext(VIDEOTOOLBOX -> RGB24) = %p\n", (void*)s);
        return 0;
    }
    if (argc >= 6 && !strcmp(argv[1], "--time")) {
        const int w = atoi(argv[2]), h = atoi(argv[3]);
        const enum AVPixelFormat src = av_get_pix_fmt(argv[4]);
        const int hdr = !strcmp(argv[5], "hdr");
        AVFrame* in = av_frame_alloc(); in->format = src; in->width = w; in->height = h; av_frame_get_buffer(in, 0);
        AVFrame* out = av_frame_alloc(); out->format = hdr ? AV_PIX_FMT_RGB48LE : AV_PIX_FMT_RGB24; out->width = w; out->height = h; av_frame_get_buffer(out, 1);
        for (int i = 0; i < 5; ++i) {
            double t0 = now_ms();
            struct SwsContext* s = build(w, h, src, hdr, hdr ? SWS_CS_BT2020 : SWS_CS_ITU709, 0);
            double t1 = now_ms();
            sws_scale(s, (const uint8_t* const*)in->data, in->linesize, 0, h, out->data, out->linesize);
            double t2 = now_ms();
            sws_setColorspaceDetails(s, sws_getCoefficients(SWS_CS_ITU709), 0, sws_getCoefficients(SWS_CS_ITU709), 1, 0, 1 << 16, 1 << 16);
            double t3 = now_ms();
            printf("%dx%d %s -> %s: getContext+setColorspaceDetails %.2f ms, first sws_scale %.2f ms, setColorspaceDetails again %.3f ms\n",
                   w, h, argv[4], hdr ? "rgb48le" : "rgb24", t1 - t0, t2 - t1, t3 - t2);
            sws_freeContext(s);
        }
        return 0;
    }
    if (argc < 5) { fprintf(stderr, "usage\n"); return 2; }
    const int W = 448, H = 128;
    const enum AVPixelFormat src = av_get_pix_fmt(argv[2]);
    const int hdr = !strcmp(argv[3], "hdr");
    const int cs  = !strcmp(argv[4], "2020") ? SWS_CS_BT2020 : SWS_CS_ITU709;
    const int full = argc > 5 && !strcmp(argv[5], "pc");
    if (getenv("SWSF")) g_flags = (int)strtol(getenv("SWSF"), NULL, 0);
    printf("flags=0x%x ", g_flags);
    AVFrame* in = av_frame_alloc(); in->format = src; in->width = W; in->height = H;
    av_frame_get_buffer(in, 1);
    FILE* fp = fopen(argv[1], "rb");
    const int need = av_image_get_buffer_size(src, W, H, 1);
    uint8_t* buf = malloc(need);
    if (fread(buf, 1, need, fp) != (size_t)need) { fprintf(stderr, "short read\n"); return 1; }
    av_image_fill_arrays(in->data, in->linesize, buf, src, W, H, 1);
    struct SwsContext* s = build(W, H, src, hdr, cs, full);
    if (!s) { printf("sws_getContext failed\n"); return 1; }
    AVFrame* out = av_frame_alloc(); out->format = hdr ? AV_PIX_FMT_RGB48LE : AV_PIX_FMT_RGB24; out->width = W; out->height = H;
    av_frame_get_buffer(out, 1);
    sws_scale(s, (const uint8_t* const*)in->data, in->linesize, 0, H, out->data, out->linesize);
    printf("%s -> %s (matrix %s, %s range in):\n", argv[2], hdr ? "rgb48le" : "rgb24", argv[4], full ? "full" : "limited");
    for (int b = 0; b < 7; ++b) {
        const int x = 64 * b + 32, y = H / 2;
        if (hdr) {
            const uint16_t* p = (const uint16_t*)(out->data[0] + y * out->linesize[0]) + 3 * x;
            printf("  bar %d: %5u %5u %5u  (/65535: %.6f %.6f %.6f)\n", b, p[0], p[1], p[2], p[0] / 65535.0, p[1] / 65535.0, p[2] / 65535.0);
        } else {
            const uint8_t* p = out->data[0] + y * out->linesize[0] + 3 * x;
            printf("  bar %d: %3u %3u %3u\n", b, p[0], p[1], p[2]);
        }
    }
    return 0;
}
