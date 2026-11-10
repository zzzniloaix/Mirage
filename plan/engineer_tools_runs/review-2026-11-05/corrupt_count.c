// The frames an H.264 decoder makes after a seek: seeks an input to <seek_s> (av_seek_frame, backward), decodes its
// video to the end with the given decoder flags (0, corrupt: AV_CODEC_FLAG_OUTPUT_CORRUPT, showall:
// AV_CODEC_FLAG2_SHOW_ALL), and counts the frames, those flagged AV_FRAME_FLAG_CORRUPT and those with
// decode_error_flags. One thread, as tests/test_demuxer_seek.cpp decodes. (plan/engineer_tools_player_fixes.md, D-093)
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s file seek_s flags(0|corrupt|showall)\n", argv[0]); return 2; }
    av_log_set_level(AV_LOG_QUIET);
    AVFormatContext* fc = NULL;
    if (avformat_open_input(&fc, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(fc, NULL) < 0) return 1;
    const AVCodec* dec = NULL;
    int vi = av_find_best_stream(fc, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    AVCodecContext* c = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(c, fc->streams[vi]->codecpar);
    c->thread_count = 1;
    if (!strcmp(argv[3], "corrupt")) c->flags |= AV_CODEC_FLAG_OUTPUT_CORRUPT;
    if (!strcmp(argv[3], "showall")) c->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;
    if (avcodec_open2(c, dec, NULL) < 0) return 1;
    av_seek_frame(fc, -1, (int64_t)(atof(argv[2]) * AV_TIME_BASE), AVSEEK_FLAG_BACKWARD);
    AVPacket* p = av_packet_alloc(); AVFrame* f = av_frame_alloc();
    long n = 0, corrupt = 0, errflags = 0; double first = -1, first_clean = -1;
    double tb = av_q2d(fc->streams[vi]->time_base);
    for (int eof = 0; !eof;) {
        if (av_read_frame(fc, p) < 0) { eof = 1; avcodec_send_packet(c, NULL); }
        else if (p->stream_index == vi) avcodec_send_packet(c, p);
        av_packet_unref(p);
        while (avcodec_receive_frame(c, f) == 0) {
            double t = f->best_effort_timestamp * tb;
            if (first < 0) first = t;
            n++;
            if (f->flags & AV_FRAME_FLAG_CORRUPT) corrupt++;
            else if (first_clean < 0) first_clean = t;
            if (f->decode_error_flags) errflags++;
            av_frame_unref(f);
        }
    }
    printf("flags=%s: %ld frames from %.3f, %ld flagged corrupt, %ld with decode_error_flags, first unflagged at %.3f\n",
           argv[3], n, first, corrupt, errflags, first_clean);
    return 0;
}
