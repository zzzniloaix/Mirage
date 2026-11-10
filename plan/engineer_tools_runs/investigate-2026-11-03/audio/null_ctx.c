// What Decoder::push / pull / flush do after a failed Decoder::open (ctx_ == nullptr
// when no decoder is found: Decoder.cpp:59 frees it, :86-89 return before alloc),
// and with a context whose avcodec_open2 failed.
#include <libavcodec/avcodec.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
static void try_(const char* what, int which)
{
    pid_t p = fork();
    if (p == 0) {
        AVPacket* pkt = av_packet_alloc(); av_new_packet(pkt, 16); memset(pkt->data, 0, 16);
        AVFrame* f = av_frame_alloc();
        int r = 0;
        if (which == 0) r = avcodec_send_packet(NULL, pkt);
        if (which == 1) r = avcodec_receive_frame(NULL, f);
        if (which == 2) avcodec_flush_buffers(NULL);
        if (which >= 3) {   // allocated, never opened (avcodec_open2 failed or not called)
            AVCodecContext* c = avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_H264));
            if (which == 3) r = avcodec_send_packet(c, pkt);
            if (which == 4) r = avcodec_receive_frame(c, f);
            if (which == 5) avcodec_flush_buffers(c);
        }
        char e[64] = "ok"; if (r) av_strerror(r, e, sizeof e);
        printf("  %-48s returned %d (%s)\n", what, r, e); fflush(stdout);
        _exit(0);
    }
    int st; waitpid(p, &st, 0);
    if (WIFSIGNALED(st)) printf("  %-48s killed by signal %d (%s)\n", what, WTERMSIG(st), strsignal(WTERMSIG(st)));
}
int main(void)
{
    try_("avcodec_send_packet(NULL, pkt)", 0);
    try_("avcodec_receive_frame(NULL, frame)", 1);
    try_("avcodec_flush_buffers(NULL)", 2);
    try_("avcodec_send_packet(unopened ctx, pkt)", 3);
    try_("avcodec_receive_frame(unopened ctx, frame)", 4);
    try_("avcodec_flush_buffers(unopened ctx)", 5);
    return 0;
}
