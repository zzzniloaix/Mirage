#include "AudioDecoder.h"
#include "Logger.h"

#include <cmath>
#include <algorithm>

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/opt.h>
#include <libavutil/imgutils.h>
#include <libavutil/samplefmt.h>
}

AudioDecoder::~AudioDecoder()
{
    free_filter_graph();
    if (swr_)       swr_free(&swr_);
    if (frame_)     av_frame_free(&frame_);
    if (swr_frame_) av_frame_free(&swr_frame_);
    av_channel_layout_uninit(&out_layout_);
}

bool AudioDecoder::open(AVCodecParameters* par, AudioPlayer& player, AVRational time_base)
{
    if (!decoder_.open(par))
        return false;

    out_rate_ = player.sample_rate();
    av_channel_layout_default(&out_layout_, 2);  // stereo

    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;

    int ret = swr_alloc_set_opts2(
        &swr_,
        &stereo,                    // output: stereo
        AV_SAMPLE_FMT_FLT,          // output: float32
        out_rate_,                  // output: sample rate
        &par->ch_layout,            // input layout from codec
        static_cast<AVSampleFormat>(par->format),
        par->sample_rate,
        0, nullptr
    );
    if (ret < 0 || !swr_) {
        logger::error("AudioDecoder: swr_alloc_set_opts2 failed");
        return false;
    }

    ret = swr_init(swr_);
    if (ret < 0) {
        logger::error("AudioDecoder: swr_init failed");
        return false;
    }

    time_base_ = time_base;
    frame_     = av_frame_alloc();
    swr_frame_ = av_frame_alloc();

    logger::info("AudioDecoder: {} Hz {}ch → {} Hz stereo f32",
        par->sample_rate, par->ch_layout.nb_channels, out_rate_);
    return true;
}

// ── Filter graph ──────────────────────────────────────────────────────────────

void AudioDecoder::free_filter_graph()
{
    if (filter_graph_) {
        avfilter_graph_free(&filter_graph_);
        filter_graph_ = nullptr;
        filt_src_     = nullptr;
        filt_sink_    = nullptr;
    }
}

bool AudioDecoder::build_filter_graph(double speed)
{
    free_filter_graph();

    // speed == 1.0 → passthrough, no graph needed
    if (speed == 1.0)
        return true;

    // atempo accepts [0.5, 2.0] per stage.
    // For [0.25, 4.0] we chain two stages:  s1 = sqrt(speed) clamped, s2 = speed/s1.
    double s1 = std::clamp(std::sqrt(speed), 0.5, 2.0);
    double s2 = std::clamp(speed / s1,       0.5, 2.0);

    // Build filter string: one or two atempo stages
    char filter_str[128];
    if (std::abs(s2 - 1.0) < 1e-6)
        std::snprintf(filter_str, sizeof(filter_str), "atempo=%.6f", s1);
    else
        std::snprintf(filter_str, sizeof(filter_str), "atempo=%.6f,atempo=%.6f", s1, s2);

    filter_graph_ = avfilter_graph_alloc();
    if (!filter_graph_) {
        logger::error("AudioDecoder: avfilter_graph_alloc failed");
        return false;
    }

    // abuffer source: float stereo at out_rate_
    char src_args[256];
    std::snprintf(src_args, sizeof(src_args),
        "sample_rate=%d:sample_fmt=flt:channel_layout=stereo:time_base=1/%d",
        out_rate_, out_rate_);

    const AVFilter* abuffer  = avfilter_get_by_name("abuffer");
    const AVFilter* abuffersink = avfilter_get_by_name("abuffersink");
    if (!abuffer || !abuffersink) {
        logger::error("AudioDecoder: abuffer/abuffersink not found");
        avfilter_graph_free(&filter_graph_);
        filter_graph_ = nullptr;
        return false;
    }

    int ret = avfilter_graph_create_filter(&filt_src_, abuffer, "in",
                                           src_args, nullptr, filter_graph_);
    if (ret < 0) {
        logger::error("AudioDecoder: avfilter_graph_create_filter(abuffer) failed: {}", ret);
        free_filter_graph();
        return false;
    }

    ret = avfilter_graph_create_filter(&filt_sink_, abuffersink, "out",
                                       nullptr, nullptr, filter_graph_);
    if (ret < 0) {
        logger::error("AudioDecoder: avfilter_graph_create_filter(abuffersink) failed: {}", ret);
        free_filter_graph();
        return false;
    }

    // Parse the atempo chain and connect: src → atempo(s) → sink
    AVFilterInOut* outputs = avfilter_inout_alloc();
    AVFilterInOut* inputs  = avfilter_inout_alloc();
    if (!outputs || !inputs) {
        avfilter_inout_free(&outputs);
        avfilter_inout_free(&inputs);
        free_filter_graph();
        return false;
    }

    outputs->name       = av_strdup("in");
    outputs->filter_ctx = filt_src_;
    outputs->pad_idx    = 0;
    outputs->next       = nullptr;

    inputs->name        = av_strdup("out");
    inputs->filter_ctx  = filt_sink_;
    inputs->pad_idx     = 0;
    inputs->next        = nullptr;

    ret = avfilter_graph_parse_ptr(filter_graph_, filter_str, &inputs, &outputs, nullptr);
    avfilter_inout_free(&outputs);
    avfilter_inout_free(&inputs);

    if (ret < 0) {
        logger::error("AudioDecoder: avfilter_graph_parse_ptr failed: {}", ret);
        free_filter_graph();
        return false;
    }

    ret = avfilter_graph_config(filter_graph_, nullptr);
    if (ret < 0) {
        logger::error("AudioDecoder: avfilter_graph_config failed: {}", ret);
        free_filter_graph();
        return false;
    }

    logger::info("AudioDecoder: atempo filter graph built ({})", filter_str);
    return true;
}

void AudioDecoder::set_speed(double speed)
{
    if (speed == speed_) return;
    speed_ = speed;
    if (!build_filter_graph(speed))
        logger::warn("AudioDecoder: failed to build filter graph for speed {:.2g}x — disabling atempo", speed);
}

// ── Flush ─────────────────────────────────────────────────────────────────────

void AudioDecoder::flush()
{
    decoder_.flush();
    out_media_pts_ = 0.0;
    pts_inited_    = false;

    // Drain and discard any frames in the filter graph by sending a null frame.
    if (filter_graph_ && filt_src_) {
        (void)av_buffersrc_add_frame(filt_src_, nullptr);
        AVFrame* tmp = av_frame_alloc();
        while (av_buffersink_get_frame(filt_sink_, tmp) >= 0)
            av_frame_unref(tmp);
        av_frame_free(&tmp);
        // Rebuild graph to restore clean state
        (void)build_filter_graph(speed_);
    }
}

// ── Decode ────────────────────────────────────────────────────────────────────

void AudioDecoder::push_to_filter(AVFrame* frame, AudioPlayer& player)
{
    // frame is a float stereo AVFrame with correct sample_rate.
    // Push into the filter graph (or passthrough if no graph).
    if (!filter_graph_) {
        // Passthrough: media pts comes directly from the frame.
        double pts = (frame->pts != AV_NOPTS_VALUE)
            ? static_cast<double>(frame->pts) / out_rate_
            : out_media_pts_;
        out_media_pts_ = pts + static_cast<double>(frame->nb_samples) / out_rate_;
        player.push(reinterpret_cast<const float*>(frame->data[0]),
                    frame->nb_samples, pts);
        return;
    }

    // Push into abuffer source
    int ret = av_buffersrc_add_frame_flags(filt_src_, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0) {
        logger::warn("AudioDecoder: av_buffersrc_add_frame failed: {}", ret);
        return;
    }

    // Pull all available output frames from abuffersink
    AVFrame* out = av_frame_alloc();
    while ((ret = av_buffersink_get_frame(filt_sink_, out)) >= 0) {
        // atempo output frame pts is in the abuffersink's time_base (1/sample_rate).
        // We track our own advancing media PTS so the clock is meaningful.
        double pts = out_media_pts_;
        out_media_pts_ += static_cast<double>(out->nb_samples) * speed_ / out_rate_;
        player.push(reinterpret_cast<const float*>(out->data[0]),
                    out->nb_samples, pts);
        av_frame_unref(out);
    }
    av_frame_free(&out);
}

void AudioDecoder::decode(AVPacket* pkt, AudioPlayer& player)
{
    decoder_.push(pkt);

    while (decoder_.pull(frame_)) {
        // Resample to float stereo
        int out_samples = swr_get_out_samples(swr_, frame_->nb_samples);
        if (out_samples <= 0) {
            av_frame_unref(frame_);
            continue;
        }

        // Set up swr_frame_ for output
        av_frame_unref(swr_frame_);
        swr_frame_->format      = AV_SAMPLE_FMT_FLT;
        swr_frame_->sample_rate = out_rate_;
        av_channel_layout_default(&swr_frame_->ch_layout, 2);
        swr_frame_->nb_samples  = out_samples;
        if (av_frame_get_buffer(swr_frame_, 0) < 0) {
            av_frame_unref(frame_);
            continue;
        }

        int converted = swr_convert(
            swr_,
            swr_frame_->data, out_samples,
            const_cast<const uint8_t**>(frame_->data), frame_->nb_samples
        );

        if (converted > 0) {
            swr_frame_->nb_samples = converted;

            // Carry the media PTS (in sample_rate units) into the filter graph.
            // Use stream time_base to get media seconds, then convert to sample units.
            if (frame_->pts != AV_NOPTS_VALUE) {
                double media_sec = frame_->pts * av_q2d(time_base_);
                swr_frame_->pts = static_cast<int64_t>(media_sec * out_rate_);
                // Re-seed our running tracker from the actual stream PTS on the
                // first frame after open or flush (including post-seek frames).
                if (!pts_inited_) {
                    out_media_pts_ = media_sec;
                    pts_inited_    = true;
                }
            } else {
                swr_frame_->pts = AV_NOPTS_VALUE;
            }

            push_to_filter(swr_frame_, player);
        }

        av_frame_unref(frame_);
        av_frame_unref(swr_frame_);
    }
}
