// Which return codes Mirage's video decode loop would see from
// avcodec_send_packet / avcodec_receive_frame on a damaged stream, per decoder
// set-up, with the loop shape of MediaSession.cpp:212-228 (send one packet,
// receive until non-zero) and the set-ups of Decoder::open (Decoder.cpp:65-149).
//
// usage: decode_errors <file> <mode> <damage>
//   mode:   sw1   software, 1 thread
//           swmt  software, thread_count 0, FRAME|SLICE  (Decoder.cpp:127-130)
//           vt    VideoToolbox as Decoder::open: a VT-capable decoder,
//                 av_hwdevice_ctx_create, get_format picking VIDEOTOOLBOX
//   damage: none | flip (XOR bytes past the first 24 of every 10th packet from #7)
//           | trunc (every 10th packet from #7 cut to a third)
//           | drop (every 10th packet from #7 not sent) | garbage (packets 7, 17 all random)
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static enum AVPixelFormat get_hw_format_cb(AVCodecContext* ctx, const enum AVPixelFormat* f)
{
    for (const enum AVPixelFormat* p = f; *p != AV_PIX_FMT_NONE; ++p)
        if (*p == AV_PIX_FMT_VIDEOTOOLBOX) return *p;
    return avcodec_default_get_format(ctx, f);
}

static const AVCodec* find_hw_capable_decoder(enum AVCodecID id)
{
    void* opaque = NULL;
    const AVCodec* c;
    while ((c = av_codec_iterate(&opaque))) {
        if (c->id != id || !av_codec_is_decoder(c)) continue;
        for (int i = 0;; ++i) {
            const AVCodecHWConfig* cfg = avcodec_get_hw_config(c, i);
            if (!cfg) break;
            if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
                cfg->device_type == AV_HWDEVICE_TYPE_VIDEOTOOLBOX)
                return c;
        }
    }
    return NULL;
}

// Return-code histogram.
typedef struct { int code; int n; } Hist;
static void hist_add(Hist* h, int* nh, int code)
{
    for (int i = 0; i < *nh; i++) if (h[i].code == code) { h[i].n++; return; }
    h[*nh].code = code; h[*nh].n = 1; (*nh)++;
}
static void hist_print(const char* what, Hist* h, int nh)
{
    printf("  %s:", what);
    for (int i = 0; i < nh; i++) {
        char e[AV_ERROR_MAX_STRING_SIZE] = "ok";
        if (h[i].code) av_strerror(h[i].code, e, sizeof e);
        printf("  [%d \"%s\"] x%d", h[i].code, e, h[i].n);
    }
    printf("\n");
}

static int log_errors = 0, log_warnings = 0;
static char first_err[8][256]; static int n_first = 0;
static void log_cb(void* avcl, int level, const char* fmt, va_list vl)
{
    if (getenv("LOGV") && level <= AV_LOG_VERBOSE) { char l[512]; int pp = 1; va_list c; va_copy(c, vl); av_log_format_line(avcl, level, fmt, c, l, sizeof l, &pp); va_end(c); printf("      log[%d] %s", level, l); }
    if (level > AV_LOG_WARNING) return;
    if (level <= AV_LOG_ERROR) log_errors++; else log_warnings++;
    if (n_first < 8 && level <= AV_LOG_ERROR) {
        char line[256]; int pp = 1;
        av_log_format_line(avcl, level, fmt, vl, line, sizeof line, &pp);
        line[strcspn(line, "\n")] = 0;
        for (int i = 0; i < n_first; i++) if (!strcmp(first_err[i], line)) return;
        snprintf(first_err[n_first++], 256, "%s", line);
    }
}

int main(int argc, char** argv)
{
    if (argc < 4) { fprintf(stderr, "usage: decode_errors <file> <sw1|swmt|vt> <none|flip|trunc|drop|garbage>\n"); return 2; }
    const char *mode = argv[2], *damage = argv[3];
    av_log_set_callback(log_cb);

    AVFormatContext* fmt = NULL;
    if (avformat_open_input(&fmt, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fmt, NULL) < 0) return 1;
    const int vi = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    AVCodecParameters* par = fmt->streams[vi]->codecpar;

    const int hw = !strcmp(mode, "vt");
    const AVCodec* codec = hw ? find_hw_capable_decoder(par->codec_id) : NULL;
    if (!codec) codec = avcodec_find_decoder(par->codec_id);
    AVCodecContext* ctx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(ctx, par);
    AVBufferRef* dev = NULL;
    if (hw) {
        int r = av_hwdevice_ctx_create(&dev, AV_HWDEVICE_TYPE_VIDEOTOOLBOX, NULL, NULL, 0);
        if (r < 0) { printf("hwdevice create failed\n"); return 1; }
        ctx->hw_device_ctx = av_buffer_ref(dev);
        ctx->get_format = get_hw_format_cb;
    } else if (!strcmp(mode, "swmt")) {
        ctx->thread_count = 0;
        ctx->thread_type = FF_THREAD_FRAME | FF_THREAD_SLICE;
    } else {
        ctx->thread_count = 1;
    }
    if (avcodec_open2(ctx, codec, NULL) < 0) { printf("open failed\n"); return 1; }

    Hist hs[16], hr[16]; int nhs = 0, nhr = 0;
    int packets = 0, sent = 0, frames = 0, hwframes = 0, swframes = 0, errframes = 0, corrupt = 0;
    int flagbits[8] = { 0 };
    unsigned rng = 12345;
    AVPacket* pkt = av_packet_alloc();
    AVFrame* fr = av_frame_alloc();
    int eof = 0;
    while (!eof) {
        int r = av_read_frame(fmt, pkt);
        if (r < 0) { eof = 1; }
        else if (pkt->stream_index != vi) { av_packet_unref(pkt); continue; }
        int idx = packets++;
        const int hit = !eof && (!strcmp(damage, "flip1") ? idx == 7 : !strcmp(damage, "flipkey") ? idx == 30 : (idx >= 7 && (idx - 7) % 10 == 0));
        const int key = !eof && (pkt->flags & AV_PKT_FLAG_KEY);
        if (!eof && hit) {
            if (!strncmp(damage, "flip", 4)) {
                av_packet_make_writable(pkt);
                for (int i = 24; i < pkt->size; i += 3) pkt->data[i] ^= 0x5A;
            } else if (!strcmp(damage, "trunc")) {
                av_shrink_packet(pkt, pkt->size / 3);
            } else if (!strcmp(damage, "drop")) {
                av_packet_unref(pkt);
                continue;
            }
        }
        if (!eof && !strcmp(damage, "garbage") && (idx == 7 || idx == 17)) {
            av_packet_make_writable(pkt);
            for (int i = 0; i < pkt->size; i++) { rng = rng * 1103515245u + 12345u; pkt->data[i] = rng >> 16; }
        }
        int s = avcodec_send_packet(ctx, eof ? NULL : pkt);
        sent++;
        hist_add(hs, &nhs, s);
        if (s < 0 && s != AVERROR(EAGAIN) && n_first < 8 && 0) {}
        if (!eof) av_packet_unref(pkt);
        int rr;
        int errs_in_row = 0;   // CONTINUE=1: keep receiving after an error (as fftools does), not stop
        while ((rr = avcodec_receive_frame(ctx, fr)) == 0
               || (getenv("CONTINUE") && rr != AVERROR(EAGAIN) && rr != AVERROR_EOF && ++errs_in_row < 64
                   && (hist_add(hr, &nhr, rr), 1) && (rr = -1))) {
            if (rr == -1) continue;
            errs_in_row = 0;
            frames++;
            if (fr->format == AV_PIX_FMT_VIDEOTOOLBOX) hwframes++; else swframes++;
            if (fr->decode_error_flags || (fr->flags & AV_FRAME_FLAG_CORRUPT)) errframes++;
            if (fr->flags & AV_FRAME_FLAG_CORRUPT) corrupt++;
            for (int b = 0; b < 8; b++) if (fr->decode_error_flags & (1 << b)) flagbits[b]++;
            av_frame_unref(fr);
        }
        hist_add(hr, &nhr, rr);   // the code that ended the receive loop
        if (getenv("TRACE")) printf("    pkt %3d%s%s send %d, frames so far %d, loop end %d\n", idx, key ? " KEY" : "", hit ? " DAMAGED" : "", s, frames, rr);
    }
    printf("%s %s %s: decoder %s, %d packets read, %d sends, %d frames out (%d VT, %d SW), "
           "%d with decode_error_flags/CORRUPT (%d CORRUPT; flags: invalid_bitstream %d, missing_ref %d, concealment %d, slices %d)\n",
           argv[1], mode, damage, codec->name, packets - 1, sent, frames, hwframes, swframes, errframes, corrupt,
           flagbits[0], flagbits[1], flagbits[2], flagbits[3]);
    hist_print("send_packet returns", hs, nhs);
    hist_print("receive_frame loop ended with", hr, nhr);
    printf("  av_log: %d error lines, %d warning lines; first distinct errors:\n", log_errors, log_warnings);
    for (int i = 0; i < n_first; i++) printf("    %s\n", first_err[i]);
    avcodec_free_context(&ctx);
    av_buffer_unref(&dev);
    av_frame_free(&fr);
    av_packet_free(&pkt);
    avformat_close_input(&fmt);
    return 0;
}
