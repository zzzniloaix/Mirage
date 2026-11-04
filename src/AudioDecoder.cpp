#include "AudioDecoder.h"
#include "Logger.h"

#include <cmath>
#include <cstring>
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
    free_resampler();
    if (frame_)     av_frame_free(&frame_);
    if (swr_frame_) av_frame_free(&swr_frame_);
    av_channel_layout_uninit(&out_layout_);
}

bool AudioDecoder::open(AVCodecParameters* par, AudioPlayer& player, AVRational time_base)
{
    // Set without a decoder too: push_silence() works from them.
    out_rate_  = player.sample_rate();
    time_base_ = time_base;
    if (!frame_)     frame_     = av_frame_alloc();
    if (!swr_frame_) swr_frame_ = av_frame_alloc();

    if (!decoder_.open(par))
        return false;

    av_channel_layout_default(&out_layout_, 2);  // stereo

    // The resampler comes with the first decoded frame (update_resampler):
    // the codec parameters can differ from what the decoder puts out.
    logger::info("AudioDecoder: {} Hz {}ch → {} Hz stereo f32",
        par->sample_rate, par->ch_layout.nb_channels, out_rate_);
    return true;
}

// ── Resampler ─────────────────────────────────────────────────────────────────

AudioDecoder::MixLevels AudioDecoder::stream_mix_levels(const AVDownmixInfo& di)
{
    // AVDownmixInfo's levels are linear factors, and so are swr's options,
    // although ffmpeg-resampler(1) gives them in dB.
    MixLevels lv;
    lv.from_stream = true;
    switch (di.preferred_downmix_type) {
        case AV_DOWNMIX_TYPE_LTRT:
            lv.matrix   = AV_MATRIX_ENCODING_DOLBY;
            lv.center   = di.center_mix_level_ltrt;
            lv.surround = di.surround_mix_level_ltrt;
            break;
        case AV_DOWNMIX_TYPE_DPLII:
            lv.matrix   = AV_MATRIX_ENCODING_DPLII;
            lv.center   = di.center_mix_level_ltrt;
            lv.surround = di.surround_mix_level_ltrt;
            break;
        default:
            lv.center   = di.center_mix_level;
            lv.surround = di.surround_mix_level;
            break;
    }
    lv.lfe = di.lfe_mix_level > 0.0 ? di.lfe_mix_level : 0.0;
    return lv;
}

void AudioDecoder::free_resampler()
{
    if (swr_) swr_free(&swr_);
    av_channel_layout_uninit(&swr_layout_);
    swr_built_ = false;
}

void AudioDecoder::drain_resampler(AudioPlayer& player)
{
    // What swr holds: its filter's delay when it changes the rate (nothing
    // when it does not). Played before the new resampler's first samples,
    // except before start_at_ (dropped there) or with nothing pushed since a
    // flush (audio from before it).
    const int n = swr_get_out_samples(swr_, 0);
    if (n <= 0) return;
    av_frame_unref(swr_frame_);
    swr_frame_->format      = AV_SAMPLE_FMT_FLT;
    swr_frame_->sample_rate = out_rate_;
    av_channel_layout_default(&swr_frame_->ch_layout, 2);
    swr_frame_->nb_samples  = n;
    if (av_frame_get_buffer(swr_frame_, 0) < 0) return;
    const int got = swr_convert(swr_, swr_frame_->data, n, nullptr, 0);
    if (got > 0 && start_at_ < 0.0 && pts_inited_) {
        swr_frame_->nb_samples = got;
        swr_frame_->pts        = AV_NOPTS_VALUE;   // right after the audio pushed last
        push_to_filter(swr_frame_, player);
    }
    av_frame_unref(swr_frame_);
}

bool AudioDecoder::update_resampler(const AVFrame* f, AudioPlayer& player)
{
    MixLevels lv;
    if (const AVFrameSideData* sd = av_frame_get_side_data(f, AV_FRAME_DATA_DOWNMIX_INFO);
        sd && sd->size >= sizeof(AVDownmixInfo))
        lv = stream_mix_levels(*reinterpret_cast<const AVDownmixInfo*>(sd->data));
    if (swr_built_ && f->format == swr_format_ && f->sample_rate == swr_rate_ && lv == swr_levels_
        && av_channel_layout_compare(&f->ch_layout, &swr_layout_) == 0)
        return swr_ != nullptr;

    // A new input or new levels: a new resampler, after the old one's tail.
    if (swr_) drain_resampler(player);
    free_resampler();
    swr_built_  = true;
    swr_format_ = f->format;
    swr_rate_   = f->sample_rate;
    swr_levels_ = lv;
    char layout[128] = "?";
    if (av_channel_layout_copy(&swr_layout_, &f->ch_layout) < 0) {
        logger::error("AudioDecoder: av_channel_layout_copy failed");
        return false;
    }
    av_channel_layout_describe(&f->ch_layout, layout, sizeof(layout));
    const char* fmt = av_get_sample_fmt_name(static_cast<AVSampleFormat>(f->format));

    AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
    int ret = swr_alloc_set_opts2(
        &swr_,
        &stereo,                    // output: stereo
        AV_SAMPLE_FMT_FLT,          // output: float32
        out_rate_,                  // output: sample rate
        &f->ch_layout,              // input: as decoded
        static_cast<AVSampleFormat>(f->format),
        f->sample_rate,
        0, nullptr
    );
    if (ret >= 0 && lv.from_stream) {
        ret = av_opt_set_double(swr_, "center_mix_level", lv.center, 0);
        if (ret >= 0) ret = av_opt_set_double(swr_, "surround_mix_level", lv.surround, 0);
        if (ret >= 0 && lv.lfe > 0.0) ret = av_opt_set_double(swr_, "lfe_mix_level", lv.lfe, 0);
        if (ret >= 0) ret = av_opt_set_int(swr_, "matrix_encoding", lv.matrix, 0);
    }
    if (ret >= 0) ret = swr_init(swr_);
    if (ret < 0) {
        // Until the input changes again: its frames are dropped (silence).
        logger::error("AudioDecoder: no resampler for {} Hz {} {}: {} — dropping its audio",
                      f->sample_rate, layout, fmt ? fmt : "?", av_error_text(ret));
        swr_free(&swr_);
        return false;
    }
    logger::info("AudioDecoder: resampler {} Hz {} {} → {} Hz stereo flt, {}",
                 f->sample_rate, layout, fmt ? fmt : "?", out_rate_,
                 !lv.from_stream ? std::string("FFmpeg's default downmix levels")
                 : std::format("the stream's downmix levels: centre {:.6f}, surround {:.6f}, LFE {:.6f}{}",
                               lv.center, lv.surround, lv.lfe,
                               lv.matrix == AV_MATRIX_ENCODING_DOLBY ? " (Lt/Rt)"
                               : lv.matrix == AV_MATRIX_ENCODING_DPLII ? " (Pro Logic II)" : ""));
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
    tempo_ = 1.0;

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

    tempo_ = s1 * s2;
    if (std::abs(tempo_ - speed) > 1e-6)
        logger::warn("AudioDecoder: speed {:.2g}x exceeds atempo range — audio clamped to {:.2g}x",
                     speed, tempo_);
    logger::info("AudioDecoder: atempo filter graph built ({})", filter_str);
    return true;
}

void AudioDecoder::apply_pending_speed(AudioPlayer& player)
{
    double speed = requested_speed_.load();
    if (speed == speed_) return;

    // Flush the old graph so the audio atempo was still holding is played
    // (at the old tempo) instead of silently dropped.
    if (filter_graph_ && filt_src_) {
        (void)av_buffersrc_add_frame(filt_src_, nullptr);
        pull_filter_output(player);
    }

    speed_ = speed;
    if (!build_filter_graph(speed))
        logger::warn("AudioDecoder: failed to build filter graph for speed {:.2g}x — disabling atempo", speed);
}

// ── Flush ─────────────────────────────────────────────────────────────────────

void AudioDecoder::flush()
{
    if (tap_) tap_->on_flush();
    decoder_.flush();
    out_media_pts_ = 0.0;
    pts_inited_    = false;
    silent_        = false;

    // Drain and discard any frames in the filter graph by sending a null frame.
    if (filter_graph_ && filt_src_) {
        (void)av_buffersrc_add_frame(filt_src_, nullptr);
        AVFrame* tmp = av_frame_alloc();
        while (av_buffersink_get_frame(filt_sink_, tmp) >= 0)
            av_frame_unref(tmp);
        av_frame_free(&tmp);
    }
    // Rebuild graph to restore clean state, picking up any pending speed change.
    if (filter_graph_ || requested_speed_.load() != speed_) {
        speed_ = requested_speed_.load();
        (void)build_filter_graph(speed_);
    }
}

bool AudioDecoder::reopen(AVCodecParameters* par, AudioPlayer& player, AVRational time_base)
{
    if (tap_) tap_->on_flush();
    free_filter_graph();
    free_resampler();   // the old track's (what it still holds goes with it)
    decoder_.close();
    out_media_pts_ = 0.0;
    pts_inited_    = false;
    silent_        = false;
    speed_         = 1.0;   // no graph now; next decode() rebuilds for requested_speed_
    tempo_         = 1.0;
    return open(par, player, time_base);
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
                    frame->nb_samples, pts,
                    static_cast<double>(frame->nb_samples) / out_rate_);
        return;
    }

    // Push into abuffer source
    int ret = av_buffersrc_add_frame_flags(filt_src_, frame, AV_BUFFERSRC_FLAG_KEEP_REF);
    if (ret < 0) {
        logger::warn("AudioDecoder: av_buffersrc_add_frame failed: {}", ret);
        return;
    }

    pull_filter_output(player);
}

void AudioDecoder::pull_filter_output(AudioPlayer& player)
{
    AVFrame* out = av_frame_alloc();
    while (av_buffersink_get_frame(filt_sink_, out) >= 0) {
        // atempo output frame pts is in the abuffersink's time_base (1/sample_rate).
        // We track our own advancing media PTS so the clock is meaningful.
        // Each output sample covers tempo_ media samples.
        double pts       = out_media_pts_;
        double media_dur = static_cast<double>(out->nb_samples) * tempo_ / out_rate_;
        out_media_pts_ += media_dur;
        player.push(reinterpret_cast<const float*>(out->data[0]),
                    out->nb_samples, pts, media_dur);
        av_frame_unref(out);
    }
    av_frame_free(&out);
}

void AudioDecoder::push_silence(const AVPacket* pkt, AudioPlayer& player)
{
    if (!pkt || pkt->pts == AV_NOPTS_VALUE || !swr_frame_) return;
    const double start = pkt->pts * av_q2d(time_base_);
    const double end   = start + (pkt->duration > 0 ? pkt->duration * av_q2d(time_base_) : 0.0);
    double from = silent_ ? silent_until_ : start;
    if (start_at_ >= 0.0) from = std::max(from, start_at_);
    silent_until_ = silent_ ? std::max(silent_until_, end) : end;
    silent_       = true;
    const long long n = std::llround((end - from) * out_rate_);
    if (n <= 0) return;
    start_at_ = -1.0;   // reached

    av_frame_unref(swr_frame_);
    swr_frame_->format      = AV_SAMPLE_FMT_FLT;
    swr_frame_->sample_rate = out_rate_;
    av_channel_layout_default(&swr_frame_->ch_layout, 2);
    swr_frame_->nb_samples  = static_cast<int>(n);
    if (av_frame_get_buffer(swr_frame_, 0) < 0) return;
    std::memset(swr_frame_->data[0], 0, static_cast<std::size_t>(n) * 2 * sizeof(float));
    swr_frame_->pts = static_cast<int64_t>(from * out_rate_);
    if (!pts_inited_) {
        out_media_pts_ = from;
        pts_inited_    = true;
    }
    push_to_filter(swr_frame_, player);
    av_frame_unref(swr_frame_);
}

void AudioDecoder::decode(AVPacket* pkt, AudioPlayer& player)
{
    apply_pending_speed(player);
    if (!decoder_.is_open()) {
        push_silence(pkt, player);
        return;
    }
    decoder_.push(pkt);

    while (decoder_.pull(frame_)) {
        if (tap_) tap_->on_frame(frame_, time_base_);
        // Resample to float stereo, with a resampler made for this frame.
        if (!update_resampler(frame_, player)) {
            av_frame_unref(frame_);
            continue;
        }
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
                // Before start_at_: dropped; the frame holding it starts there.
                if (start_at_ >= 0.0) {
                    const long long skip = std::llround((start_at_ - media_sec) * out_rate_);
                    if (skip >= converted) {
                        av_frame_unref(frame_);
                        av_frame_unref(swr_frame_);
                        continue;
                    }
                    if (skip > 0) {
                        float* d = reinterpret_cast<float*>(swr_frame_->data[0]);   // packed stereo
                        std::memmove(d, d + skip * 2,
                                     static_cast<std::size_t>(converted - skip) * 2 * sizeof(float));
                        swr_frame_->nb_samples = static_cast<int>(converted - skip);
                        media_sec += static_cast<double>(skip) / out_rate_;
                    }
                    start_at_ = -1.0;
                }
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
