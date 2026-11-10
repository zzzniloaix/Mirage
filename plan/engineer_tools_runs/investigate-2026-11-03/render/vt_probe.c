// What VideoToolbox hands Mirage's renderer, decoded the way src/Decoder.cpp
// does it (HW-capable decoder, VT device, get_format picks VIDEOTOOLBOX):
// the CVPixelBuffer's format, IOSurface, planes, and the raw words at the
// bars of make_bars.py. Also prints each frame's own colour tags.
// Usage: vt_probe <file> [max_frames] [every_n]
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <CoreVideo/CoreVideo.h>
#include <IOSurface/IOSurface.h>
#include <stdio.h>
#include <stdlib.h>

static enum AVPixelFormat get_hw_format_cb(AVCodecContext* ctx, const enum AVPixelFormat* p)
{
    for (; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == AV_PIX_FMT_VIDEOTOOLBOX) return *p;
    return avcodec_default_get_format(ctx, p);
}

static const AVCodec* find_hw_capable_decoder(enum AVCodecID id)
{
    void* opaque = NULL;
    const AVCodec* codec;
    while ((codec = av_codec_iterate(&opaque)) != NULL) {
        if (codec->id != id || !av_codec_is_decoder(codec)) continue;
        for (int i = 0;; ++i) {
            const AVCodecHWConfig* cfg = avcodec_get_hw_config(codec, i);
            if (!cfg) break;
            if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
                cfg->device_type == AV_HWDEVICE_TYPE_VIDEOTOOLBOX)
                return codec;
        }
    }
    return NULL;
}

static void fourcc(OSType t, char out[5])
{
    out[0] = (char)(t >> 24); out[1] = (char)(t >> 16); out[2] = (char)(t >> 8); out[3] = (char)t; out[4] = 0;
}

static void dump_vt(const AVFrame* f)
{
    CVPixelBufferRef pb = (CVPixelBufferRef)f->data[3];
    OSType pf = CVPixelBufferGetPixelFormatType(pb);
    char cc[5]; fourcc(pf, cc);
    IOSurfaceRef ios = CVPixelBufferGetIOSurface(pb);
    size_t np = CVPixelBufferGetPlaneCount(pb);
    const char* sw = "?";
    if (f->hw_frames_ctx) {
        const AVHWFramesContext* fc = (const AVHWFramesContext*)f->hw_frames_ctx->data;
        sw = av_get_pix_fmt_name(fc->sw_format);
    }
    printf("  CVPixelBuffer '%s' (0x%08x)  hw_frames sw_format=%s  IOSurface=%s  planes=%zu\n",
           cc, (unsigned)pf, sw ? sw : "?", ios ? "yes" : "NO", np);
    for (size_t p = 0; p < np; ++p) {
        printf("    plane %zu: %zux%zu  bytesPerRow=%zu", p, CVPixelBufferGetWidthOfPlane(pb, p),
               CVPixelBufferGetHeightOfPlane(pb, p), CVPixelBufferGetBytesPerRowOfPlane(pb, p));
        if (ios) printf("  IOSurface bytesPerElement=%zu elementWidth=%zu", IOSurfaceGetBytesPerElementOfPlane(ios, p),
                        IOSurfaceGetElementWidthOfPlane(ios, p));
        printf("\n");
    }
    if (np == 0) {
        printf("    (packed) %zux%zu bytesPerRow=%zu", CVPixelBufferGetWidth(pb), CVPixelBufferGetHeight(pb),
               CVPixelBufferGetBytesPerRow(pb));
        if (ios) printf("  IOSurface bytesPerElement=%zu", IOSurfaceGetBytesPerElement(ios));
        printf("\n");
    }
    if (np == 2 && ios && IOSurfaceGetBytesPerElementOfPlane(ios, 0) == 1) {
        CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
        const size_t h = CVPixelBufferGetHeightOfPlane(pb, 0), w = CVPixelBufferGetWidthOfPlane(pb, 0);
        const size_t wc = CVPixelBufferGetWidthOfPlane(pb, 1), hc = CVPixelBufferGetHeightOfPlane(pb, 1);
        const uint8_t* y = CVPixelBufferGetBaseAddressOfPlane(pb, 0);
        const uint8_t* uv = CVPixelBufferGetBaseAddressOfPlane(pb, 1);
        const size_t by = CVPixelBufferGetBytesPerRowOfPlane(pb, 0), buv = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
        printf("    Y bytes:");
        for (int b = 0; b < 7; ++b) printf(" %u", y[(h / 2) * by + 64 * b + 32]);
        printf("\n    CbCr bytes:");
        for (int b = 0; b < 7; ++b) { size_t x = (64 * b + 32) * wc / w; printf(" (%u,%u)", uv[(hc / 2) * buv + 2 * x], uv[(hc / 2) * buv + 2 * x + 1]); }
        printf("\n");
        CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
    }
    // Raw words at the bar centres (16-bit containers only, bi-planar).
    if (np == 2 && ios && IOSurfaceGetBytesPerElementOfPlane(ios, 0) == 2) {
        CVPixelBufferLockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
        const size_t h  = CVPixelBufferGetHeightOfPlane(pb, 0);
        const size_t w  = CVPixelBufferGetWidthOfPlane(pb, 0);
        const size_t wc = CVPixelBufferGetWidthOfPlane(pb, 1);
        const size_t hc = CVPixelBufferGetHeightOfPlane(pb, 1);
        const uint8_t* y  = CVPixelBufferGetBaseAddressOfPlane(pb, 0);
        const uint8_t* uv = CVPixelBufferGetBaseAddressOfPlane(pb, 1);
        const size_t by = CVPixelBufferGetBytesPerRowOfPlane(pb, 0), buv = CVPixelBufferGetBytesPerRowOfPlane(pb, 1);
        printf("    Y  words:");
        for (int b = 0; b < 7; ++b) {
            size_t x = 64 * b + 32; if (x >= w) break;
            uint16_t v = *(const uint16_t*)(y + (h / 2) * by + 2 * x);
            printf(" 0x%04x(>>6=%u, low6=%u)", v, v >> 6, v & 63);
        }
        printf("\n    CbCr words:");
        for (int b = 0; b < 7; ++b) {
            size_t x = (64 * b + 32) * wc / w; if (x >= wc) break;
            const uint16_t* q = (const uint16_t*)(uv + (hc / 2) * buv + 4 * x);
            printf(" (0x%04x,0x%04x)", q[0], q[1]);
        }
        printf("\n");
        CVPixelBufferUnlockBaseAddress(pb, kCVPixelBufferLock_ReadOnly);
    }
}

int main(int argc, char** argv)
{
    if (argc < 2) { fprintf(stderr, "usage: vt_probe <file> [max_frames] [every_n]\n"); return 2; }
    const int max_frames = argc > 2 ? atoi(argv[2]) : 1;
    const int every      = argc > 3 ? atoi(argv[3]) : 1;
    AVFormatContext* fmt = NULL;
    if (avformat_open_input(&fmt, argv[1], NULL, NULL) < 0) return 1;
    avformat_find_stream_info(fmt, NULL);
    int vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    AVCodecParameters* par = fmt->streams[vi]->codecpar;
    const AVCodec* codec = find_hw_capable_decoder(par->codec_id);
    printf("%s: codec %s, HW-capable decoder: %s, stream pix_fmt=%s\n", argv[1], avcodec_get_name(par->codec_id),
           codec ? codec->name : "none", av_get_pix_fmt_name(par->format));
    if (!codec) codec = avcodec_find_decoder(par->codec_id);
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, par);
    AVBufferRef* dev = NULL;
    if (av_hwdevice_ctx_create(&dev, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, NULL, NULL, 0) == 0) {
        ctx->hw_device_ctx = av_buffer_ref(dev);
        ctx->get_format    = get_hw_format_cb;
    }
    if (avcodec_open2(ctx, codec, NULL) < 0) { fprintf(stderr, "open failed\n"); return 1; }
    AVPacket* pkt = av_packet_alloc();
    AVFrame*  f   = av_frame_alloc();
    int n = 0, shown = 0;
    int flushing = 0;
    while (shown < max_frames) {
        if (!flushing) {
            int r = av_read_frame(fmt, pkt);
            if (r < 0) { avcodec_send_packet(ctx, NULL); flushing = 1; }
            else { if (pkt->stream_index == vi) avcodec_send_packet(ctx, pkt); av_packet_unref(pkt); }
        }
        int got = 0;
        while (avcodec_receive_frame(ctx, f) == 0) {
            got = 1;
            if (n % every == 0 && shown < max_frames) {
                const double t = f->pts != AV_NOPTS_VALUE ? f->pts * av_q2d(fmt->streams[vi]->time_base) : -1;
                printf("frame %d pts=%.3f format=%s  tags: range=%s matrix=%s trc=%s primaries=%s | ctx: matrix=%s trc=%s\n",
                       n, t, av_get_pix_fmt_name(f->format), av_color_range_name(f->color_range),
                       av_color_space_name(f->colorspace), av_color_transfer_name(f->color_trc),
                       av_color_primaries_name(f->color_primaries), av_color_space_name(ctx->colorspace),
                       av_color_transfer_name(ctx->color_trc));
                if (f->format == AV_PIX_FMT_VIDEOTOOLBOX) {
                    dump_vt(f);
                    if (getenv("TRANSFER")) {   // the CPU fallback a refused buffer would need
                        AVFrame* sw = av_frame_alloc();
                        int r = av_hwframe_transfer_data(sw, f, 0);
                        printf("  av_hwframe_transfer_data -> %d, format %s\n", r, r >= 0 ? av_get_pix_fmt_name(sw->format) : "-");
                        if (r >= 0) {
                            const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(sw->format);
                            printf("    Y codes (descriptor comp0 plane %d step %d offset %d shift %d depth %d):", d->comp[0].plane, d->comp[0].step, d->comp[0].offset, d->comp[0].shift, d->comp[0].depth);
                            for (int b = 0; b < 7; ++b) {
                                const uint8_t* p = sw->data[d->comp[0].plane] + (sw->height / 2) * sw->linesize[d->comp[0].plane] + (64 * b + 32) * d->comp[0].step + d->comp[0].offset;
                                unsigned v = p[0] | (p[1] << 8);
                                printf(" %u", (v >> d->comp[0].shift) & ((1u << d->comp[0].depth) - 1));
                            }
                            printf("\n");
                        }
                        av_frame_free(&sw);
                    }
                }
                ++shown;
            }
            ++n;
            av_frame_unref(f);
        }
        if (flushing && !got) break;
    }
    printf("decoded %d frames\n", n);
    av_frame_free(&f);
    av_packet_free(&pkt);
    avcodec_free_context(&ctx);
    av_buffer_unref(&dev);
    avformat_close_input(&fmt);
    return 0;
}
