#include "LoudnessTap.h"

extern "C" {
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}

#include <array>
#include <cstdio>
#include <format>

LoudnessTap::~LoudnessTap()
{
    swr_free(&to_planar_);
    av_channel_layout_uninit(&layout_);
}

void LoudnessTap::set_enabled(bool on)
{
    // A fresh start: nothing was measured while off. The feed goes now, not
    // at the next decoded frame — paused, that may be long after, and the
    // card would show the last block from before it was closed.
    if (on && !enabled_.load()) {
        restart_.store(true);
        feed_.clear();
    }
    enabled_.store(on);
}

LoudnessTap::Layout LoudnessTap::layout() const
{
    std::lock_guard lk(info_mtx_);
    return info_;
}

void LoudnessTap::set_since(double pts)
{
    std::lock_guard lk(info_mtx_);
    info_.since = pts;
}

bool LoudnessTap::configure(const AVFrame* f)
{
    meter_.reset();
    swr_free(&to_planar_);
    av_channel_layout_uninit(&layout_);
    rate_   = f->sample_rate;
    format_ = f->format;
    if (av_channel_layout_copy(&layout_, &f->ch_layout) < 0) return false;

    Layout info;
    info.rate = rate_;
    const int nch = f->ch_layout.nb_channels;
    // A layout without channel order (PCM in Matroska, ...): FFmpeg's default
    // for the count — what the ffmpeg command line guesses too — else every
    // channel would weigh 1.0 and an LFE would count.
    AVChannelLayout use{};
    const bool guessed = f->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC && nch > 0;
    if (guessed) av_channel_layout_default(&use, nch);
    else if (av_channel_layout_copy(&use, &f->ch_layout) < 0) return false;
    char name[128] = {};
    av_channel_layout_describe(&use, name, sizeof name);
    info.name = guessed ? std::format("{} (guessed for {} channels)", name, nch) : std::string(name);
    auto fail = [&](std::string why) {
        av_channel_layout_uninit(&use);
        info.error = std::move(why);
        std::lock_guard lk(info_mtx_);
        info_ = std::move(info);
        return false;
    };
    if (nch <= 0 || nch > loudness::kMaxChannels)
        return fail(std::format("{} channels: the meter takes 1 to {}", nch, loudness::kMaxChannels));
    if (rate_ < 8000) return fail(std::format("{} Hz: the meter needs 8 kHz or more", rate_));

    for (int i = 0; i < nch; ++i) {
        const AVChannel ch = av_channel_layout_channel_from_index(&use, static_cast<unsigned>(i));
        char label[32] = {};
        if (ch < 0 || ch == AV_CHAN_UNUSED || ch == AV_CHAN_UNKNOWN
            || av_channel_name(label, sizeof label, ch) < 0 || label[0] == '\0')
            std::snprintf(label, sizeof label, "Ch%d", i + 1);
        info.labels.emplace_back(label);
        switch (ch) {
        case AV_CHAN_LOW_FREQUENCY:
        case AV_CHAN_LOW_FREQUENCY_2:
            info.weights.push_back(loudness::Weight::Excluded);
            break;
        case AV_CHAN_SIDE_LEFT:   case AV_CHAN_SIDE_RIGHT:
        case AV_CHAN_BACK_LEFT:   case AV_CHAN_BACK_RIGHT:  case AV_CHAN_BACK_CENTER:
        case AV_CHAN_SURROUND_DIRECT_LEFT: case AV_CHAN_SURROUND_DIRECT_RIGHT:
        case AV_CHAN_TOP_BACK_LEFT: case AV_CHAN_TOP_BACK_RIGHT: case AV_CHAN_TOP_BACK_CENTER:
            info.weights.push_back(loudness::Weight::Surround);
            break;
        default:
            info.weights.push_back(loudness::Weight::Normal);
            break;
        }
    }

    if (format_ != AV_SAMPLE_FMT_FLTP) {
        if (swr_alloc_set_opts2(&to_planar_, &f->ch_layout, AV_SAMPLE_FMT_FLTP, rate_, &f->ch_layout,
                                static_cast<AVSampleFormat>(format_), rate_, 0, nullptr) < 0
            || swr_init(to_planar_) < 0) {
            swr_free(&to_planar_);
            return fail(std::format("can't read {} samples", av_get_sample_fmt_name(static_cast<AVSampleFormat>(format_))));
        }
    }
    av_channel_layout_uninit(&use);
    meter_ = std::make_unique<loudness::Meter>(rate_, info.weights);
    planes_.assign(static_cast<std::size_t>(nch), nullptr);
    std::lock_guard lk(info_mtx_);
    info_ = std::move(info);
    return true;
}

void LoudnessTap::on_frame(const AVFrame* f, AVRational time_base)
{
    if (!enabled_.load(std::memory_order_relaxed) || !f || f->nb_samples <= 0) return;
    // A new layout, rate or format: a meter for it (or, if it can't be
    // measured, nothing until the next one: no retry on every frame).
    if (f->sample_rate != rate_ || f->format != format_
        || av_channel_layout_compare(&f->ch_layout, &layout_) != 0) {
        unusable_ = !configure(f);
        restart_.store(true);
    }
    if (unusable_ || !meter_) return;

    const int n = f->nb_samples;
    double pts = (f->pts != AV_NOPTS_VALUE && time_base.den > 0) ? f->pts * av_q2d(time_base) : next_pts_;
    if (restart_.exchange(false)) {
        meter_->reset();
        feed_.clear();
        reset_req_.store(false);
        set_since(pts);
    }
    if (reset_req_.exchange(false)) {
        meter_->reset_integrated();
        set_since(pts);
    }

    const float* const* data = nullptr;
    if (!to_planar_) {
        data = reinterpret_cast<const float* const*>(f->extended_data);
    } else {
        const int nch = f->ch_layout.nb_channels;
        buf_.resize(static_cast<std::size_t>(nch) * n);
        std::array<std::uint8_t*, loudness::kMaxChannels> out{};
        for (int c = 0; c < nch; ++c) {
            out[static_cast<std::size_t>(c)]    = reinterpret_cast<std::uint8_t*>(buf_.data() + static_cast<std::size_t>(c) * n);
            planes_[static_cast<std::size_t>(c)] = buf_.data() + static_cast<std::size_t>(c) * n;
        }
        const int got = swr_convert(to_planar_, out.data(), n, const_cast<const std::uint8_t**>(f->extended_data), n);
        if (got <= 0) return;
        data = planes_.data();
        next_pts_ = pts + static_cast<double>(got) / rate_;
        meter_->push(data, got, pts, [this](const loudness::Block& b) { feed_.publish(b); });
        return;
    }
    next_pts_ = pts + static_cast<double>(n) / rate_;
    meter_->push(data, n, pts, [this](const loudness::Block& b) { feed_.publish(b); });
}
