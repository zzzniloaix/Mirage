// yuv420p10le bars -> a 16-bit RGB destination, renderer's flags and colour details; prints Y=940 (bar 4) and the colour bar.
#include <libswscale/swscale.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
#include <libavutil/frame.h>
#include <stdio.h>
#include <stdlib.h>
int main(int argc, char** argv) {
    const int W = 448, H = 128; const enum AVPixelFormat src = AV_PIX_FMT_YUV420P10LE, dst = av_get_pix_fmt(argv[2]);
    const int flags = argc > 3 ? (int)strtol(argv[3], NULL, 0) : SWS_BILINEAR;
    FILE* fp = fopen(argv[1], "rb"); const int need = av_image_get_buffer_size(src, W, H, 1); uint8_t* buf = malloc(need); fread(buf, 1, need, fp);
    uint8_t* d[4]; int ls[4]; av_image_fill_arrays(d, ls, buf, src, W, H, 1);
    struct SwsContext* s = sws_getContext(W, H, src, W, H, dst, flags, NULL, NULL, NULL);
    sws_setColorspaceDetails(s, sws_getCoefficients(SWS_CS_BT2020), 0, sws_getCoefficients(SWS_CS_BT2020), 1, 0, 1 << 16, 1 << 16);
    AVFrame* o = av_frame_alloc(); o->format = dst; o->width = W; o->height = H; av_frame_get_buffer(o, 1);
    sws_scale(s, (const uint8_t* const*)d, ls, 0, H, o->data, o->linesize);
    const AVPixFmtDescriptor* desc = av_pix_fmt_desc_get(dst);
    printf("%s flags 0x%x:", argv[2], flags);
    for (int b = 4; b <= 6; b += 2) {
        const int x = 64 * b + 32, y = H / 2; unsigned v[3];
        for (int c = 0; c < 3; ++c) {
            const AVComponentDescriptor* cd = &desc->comp[c];
            const uint8_t* p = o->data[cd->plane] + y * o->linesize[cd->plane] + x * cd->step + cd->offset;
            v[c] = ((p[0] | (p[1] << 8)) >> cd->shift) & ((1u << cd->depth) - 1);
        }
        printf("  bar %d R,G,B = %u,%u,%u", b, v[0], v[1], v[2]);
    }
    printf("\n");
    return 0;
}
