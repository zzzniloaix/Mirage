// av_evc.mov: <in.mp4> copied plus an EVC video stream (made-up packets, no decoder).
// stream, AC-4 (codec id only, made-up packets: no decoder in this FFmpeg),
// 2048-sample packets at 48 kHz over the same duration.
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <stdio.h>
#include <string.h>
int main(int argc, char** argv)
{
    AVFormatContext* ic = NULL;
    if (argc < 3 || avformat_open_input(&ic, argv[1], NULL, NULL) < 0 || avformat_find_stream_info(ic, NULL) < 0) return 1;
    AVFormatContext* oc = NULL;
    avformat_alloc_output_context2(&oc, NULL, "mov", argv[2]);
    int map[8]; for (int i = 0; i < 8; ++i) map[i] = -1;
    for (unsigned i = 0; i < ic->nb_streams && i < 8; ++i) {
        AVStream* o = avformat_new_stream(oc, NULL);
        avcodec_parameters_copy(o->codecpar, ic->streams[i]->codecpar);
        o->codecpar->codec_tag = 0;
        o->time_base = ic->streams[i]->time_base;
        av_dict_copy(&o->metadata, ic->streams[i]->metadata, 0);
        map[i] = o->index;
    }
    AVStream* a4 = avformat_new_stream(oc, NULL);
    a4->codecpar->codec_type = AVMEDIA_TYPE_VIDEO;
    a4->codecpar->codec_id = AV_CODEC_ID_EVC;
    a4->codecpar->width = 640; a4->codecpar->height = 360;
    
    a4->time_base = (AVRational){1, 30};
    
    if (avio_open(&oc->pb, argv[2], AVIO_FLAG_WRITE) < 0 || avformat_write_header(oc, NULL) < 0) return 2;
    const int64_t dur = av_rescale_q(ic->duration, AV_TIME_BASE_Q, (AVRational){1, 30});
    int64_t next4 = 0;   // next AC-4 packet's pts
    AVPacket* p = av_packet_alloc();
    while (av_read_frame(ic, p) >= 0) {
        if (map[p->stream_index] < 0) { av_packet_unref(p); continue; }
        // AC-4 packets up to this packet's time, interleaved
        const int64_t t48 = av_rescale_q(p->dts == AV_NOPTS_VALUE ? p->pts : p->dts, ic->streams[p->stream_index]->time_base, (AVRational){1, 30});
        while (next4 <= t48 && next4 < dur) {
            AVPacket* q = av_packet_alloc();
            av_new_packet(q, 96); memset(q->data, 0xAC, 96);
            q->pts = q->dts = next4; q->duration = 1; q->flags = (next4 % 30 == 0) ? AV_PKT_FLAG_KEY : 0; q->stream_index = a4->index;
            av_packet_rescale_ts(q, (AVRational){1, 30}, a4->time_base);
            av_interleaved_write_frame(oc, q); av_packet_free(&q);
            next4 += 1;
        }
        const int si = p->stream_index;
        p->stream_index = map[si];
        av_packet_rescale_ts(p, ic->streams[si]->time_base, oc->streams[map[si]]->time_base);
        av_interleaved_write_frame(oc, p);
    }
    av_write_trailer(oc); avio_closep(&oc->pb); avformat_free_context(oc); avformat_close_input(&ic); av_packet_free(&p);
    return 0;
}
