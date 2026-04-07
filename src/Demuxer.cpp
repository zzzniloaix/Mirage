#include "Demuxer.h"
#include "Logger.h"

#include <cstring>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/pixdesc.h>
}

void Demuxer::request_seek(double seconds)
{
    seek_target_.store(seconds);
}

void Demuxer::read_loop(std::stop_token st,
                        Queue<AVPacket*>& videoq,
                        Queue<AVPacket*>& audioq)
{
    AVPacket* pkt = av_packet_alloc();

    while (!st.stop_requested()) {
        // ── Handle a pending seek request ─────────────────────────────────
        double seek_secs = seek_target_.exchange(-1.0);
        if (seek_secs >= 0.0) {
            int64_t ts = static_cast<int64_t>(seek_secs * AV_TIME_BASE);
            av_seek_frame(fmt_ctx_, -1, ts, AVSEEK_FLAG_BACKWARD);

            // Drain and free all stale packets still sitting in the queues
            AVPacket* stale;
            while (videoq.try_pop(stale))
                if (stale && stale != flush_sentinel()) av_packet_free(&stale);
            while (audioq.try_pop(stale))
                if (stale && stale != flush_sentinel()) av_packet_free(&stale);

            // Tell decode threads to flush their codec buffers
            videoq.push(flush_sentinel());
            if (audio_idx_ >= 0) audioq.push(flush_sentinel());
        }

        int ret = av_read_frame(fmt_ctx_, pkt);
        if (ret < 0) {
            // EOF or read error — push a null sentinel so decoders know to flush
            videoq.push(nullptr);
            if (audio_idx_ >= 0) audioq.push(nullptr);
            break;
        }

        if (pkt->stream_index == video_idx_) {
            AVPacket* copy = av_packet_clone(pkt);
            if (!videoq.push(copy))
                av_packet_free(&copy);
        } else if (pkt->stream_index == audio_idx_) {
            AVPacket* copy = av_packet_clone(pkt);
            if (!audioq.push(copy))
                av_packet_free(&copy);
        }

        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
}

Demuxer::~Demuxer()
{
    close();
}

bool Demuxer::open(const std::string& url_or_path)
{
    if (is_network_url(url_or_path))
        avformat_network_init();

    AVDictionary* opts = nullptr;
    if (is_network_url(url_or_path)) {
        av_dict_set(&opts, "timeout",            "5000000", 0);
        av_dict_set(&opts, "reconnect",          "1",       0);
        av_dict_set(&opts, "reconnect_streamed", "1",       0);
        av_dict_set(&opts, "buffer_size",        "1048576", 0);
    }

    int ret = avformat_open_input(&fmt_ctx_, url_or_path.c_str(), nullptr, &opts);
    av_dict_free(&opts);

    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        logger::error("avformat_open_input failed: {}", errbuf);
        return false;
    }

    ret = avformat_find_stream_info(fmt_ctx_, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        logger::error("avformat_find_stream_info failed: {}", errbuf);
        return false;
    }

    // Find best video stream
    const AVCodec* video_codec = nullptr;
    video_idx_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_VIDEO, -1, -1, &video_codec, 0);
    if (video_idx_ < 0)
        logger::warn("No video stream found");

    // Find best audio stream (prefer one linked to the video stream)
    const AVCodec* audio_codec = nullptr;
    audio_idx_ = av_find_best_stream(fmt_ctx_, AVMEDIA_TYPE_AUDIO, -1, video_idx_, &audio_codec, 0);
    if (audio_idx_ < 0)
        logger::warn("No audio stream found");

    print_stream_info();
    return true;
}

void Demuxer::close()
{
    if (fmt_ctx_) {
        avformat_close_input(&fmt_ctx_);
        fmt_ctx_ = nullptr;
    }
    video_idx_ = -1;
    audio_idx_ = -1;
}

AVCodecParameters* Demuxer::video_codecpar() const
{
    if (video_idx_ < 0) return nullptr;
    return fmt_ctx_->streams[video_idx_]->codecpar;
}

AVCodecParameters* Demuxer::audio_codecpar() const
{
    if (audio_idx_ < 0) return nullptr;
    return fmt_ctx_->streams[audio_idx_]->codecpar;
}

AVRational Demuxer::video_time_base() const
{
    if (video_idx_ < 0) return {1, 1};
    return fmt_ctx_->streams[video_idx_]->time_base;
}

AVRational Demuxer::audio_time_base() const
{
    if (audio_idx_ < 0) return {1, 1};
    return fmt_ctx_->streams[audio_idx_]->time_base;
}

double Demuxer::duration() const
{
    if (!fmt_ctx_ || fmt_ctx_->duration == AV_NOPTS_VALUE) return -1.0;
    return static_cast<double>(fmt_ctx_->duration) / AV_TIME_BASE;
}

bool Demuxer::is_network_url(const std::string& url) const
{
    return url.starts_with("http://")  ||
           url.starts_with("https://") ||
           url.starts_with("rtmp://")  ||
           url.starts_with("rtsp://");
}

void Demuxer::print_stream_info() const
{
    logger::info("Container: {}", fmt_ctx_->iformat->long_name);

    if (int64_t dur = fmt_ctx_->duration; dur != AV_NOPTS_VALUE) {
        int total_s = static_cast<int>(dur / AV_TIME_BASE);
        logger::info("Duration:  {:02d}:{:02d}:{:02d}",
            total_s / 3600, (total_s % 3600) / 60, total_s % 60);
    }

    if (video_idx_ >= 0) {
        AVCodecParameters* par = fmt_ctx_->streams[video_idx_]->codecpar;
        const AVCodecDescriptor* desc = avcodec_descriptor_get(par->codec_id);
        AVRational fps = fmt_ctx_->streams[video_idx_]->avg_frame_rate;

        logger::info("Video:     {} {}×{} @ {:.3f} fps  pixel_fmt={}",
            desc ? desc->name : "unknown",
            par->width, par->height,
            av_q2d(fps),
            av_get_pix_fmt_name(static_cast<AVPixelFormat>(par->format)));
    }

    if (audio_idx_ >= 0) {
        AVCodecParameters* par = fmt_ctx_->streams[audio_idx_]->codecpar;
        const AVCodecDescriptor* desc = avcodec_descriptor_get(par->codec_id);

        logger::info("Audio:     {} {} Hz  {} ch  fmt={}",
            desc ? desc->name : "unknown",
            par->sample_rate,
            par->ch_layout.nb_channels,
            av_get_sample_fmt_name(static_cast<AVSampleFormat>(par->format)));
    }
}
