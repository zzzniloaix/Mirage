#include "Decoder.h"
#include "Logger.h"

extern "C" {
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

// get_format callback: called by libavcodec after codec_open2, with the list
// of pixel formats the codec can output. Return VIDEOTOOLBOX if offered
// (means the codec has a VT hwaccel and our hw_device_ctx is compatible);
// otherwise fall back to the default SW format so decode still works.
// Called again without VIDEOTOOLBOX in the list when the hwaccel fails to
// start: the decoder then knows its errors are no longer VideoToolbox's.
AVPixelFormat Decoder::get_hw_format(AVCodecContext* ctx,
                                     const AVPixelFormat* pix_fmts)
{
    auto* self = static_cast<Decoder*>(ctx->opaque);
    for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == AV_PIX_FMT_VIDEOTOOLBOX) {
            if (self) self->hw_chosen_ = true;
            return *p;
        }
    }
    if (self) self->hw_chosen_ = false;
    return avcodec_default_get_format(ctx, pix_fmts);
}

// Find a decoder for this codec_id that advertises VideoToolbox hwaccel
// support. Needed because avcodec_find_decoder() returns the "default"
// decoder, which for AV1 is libdav1d (software-only, no hwaccel). Iterating
// lets us pick the native `av1` decoder instead when HW is requested.
//
// Caveat: on M1/M2 (which lack AV1 hardware blocks), VideoToolbox will accept
// the AV1 stream but silently decode it in its own software path — slower
// than libdav1d. `--no-hw` skips this helper and restores libdav1d.
// M3+ has real AV1 hardware and is the intended target.
static const AVCodec* find_hw_capable_decoder(AVCodecID id)
{
    void* opaque = nullptr;
    const AVCodec* codec;
    while ((codec = av_codec_iterate(&opaque)) != nullptr) {
        if (codec->id != id)             continue;
        if (!av_codec_is_decoder(codec)) continue;
        for (int i = 0; ; ++i) {
            const AVCodecHWConfig* cfg = avcodec_get_hw_config(codec, i);
            if (!cfg) break;
            if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) &&
                cfg->device_type == AV_HWDEVICE_TYPE_VIDEOTOOLBOX) {
                return codec;
            }
        }
    }
    return nullptr;
}

Decoder::~Decoder()
{
    close();
}

void Decoder::close()
{
    log_errors(true);   // what the rate limit held back
    if (ctx_)            avcodec_free_context(&ctx_);
    if (hw_device_ctx_)  av_buffer_unref(&hw_device_ctx_);
    hw_requested_ = false;
    hw_active_    = false;
    hw_chosen_    = false;
    draining_     = false;
    open_         = false;
    hw_run_.store(0, std::memory_order_relaxed);   // a new context has failed nothing yet
}

bool Decoder::open(AVCodecParameters* par, bool try_hw)
{
    close();  // reopen after a track switch must not leak the old context / HW device

    // When HW is requested, prefer a decoder that advertises VT support
    // (e.g., native `av1` over libdav1d). Falls through to the default
    // decoder if no HW-capable one is registered.
    const AVCodec* codec = nullptr;
    if (try_hw && par->codec_type == AVMEDIA_TYPE_VIDEO) {
        codec = find_hw_capable_decoder(par->codec_id);
        if (codec)
            logger::info("Selected HW-capable decoder: {} (default was {})",
                codec->name,
                avcodec_find_decoder(par->codec_id)
                    ? avcodec_find_decoder(par->codec_id)->name : "?");
    }
    // No VT-capable decoder → this is a pure software decode regardless of try_hw.
    if (!codec) {
        try_hw = false;
        codec  = avcodec_find_decoder(par->codec_id);
    }
    if (!codec) {
        logger::error("No decoder found for {} (codec id {})",
                      avcodec_get_name(par->codec_id), static_cast<int>(par->codec_id));
        return false;
    }

    ctx_ = avcodec_alloc_context3(codec);
    if (!ctx_) {
        logger::error("avcodec_alloc_context3 failed");
        return false;
    }

    // A context that failed to open is freed: FFmpeg's calls on it are not
    // all safe (avcodec_flush_buffers on one crashes).
    int ret = avcodec_parameters_to_context(ctx_, par);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        logger::error("avcodec_parameters_to_context failed: {}", errbuf);
        close();
        return false;
    }

    hw_requested_ = try_hw;
    if (try_hw) {
        ret = av_hwdevice_ctx_create(&hw_device_ctx_,
                                     AV_HWDEVICE_TYPE_VIDEOTOOLBOX,
                                     nullptr, nullptr, 0);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            logger::warn("HW decode requested but av_hwdevice_ctx_create failed: {} — falling back to software", errbuf);
            hw_requested_ = false;
        } else {
            ctx_->hw_device_ctx = av_buffer_ref(hw_device_ctx_);
            ctx_->get_format    = get_hw_format;
            ctx_->opaque        = this;
        }
    }

    // Software video decode: let libavcodec pick a thread count (one per core)
    // and use frame + slice threading. Single-threaded SW decode can't keep up
    // with 4K, or with 1080p60 at 2–4x speed. The VideoToolbox path is left
    // alone — the hwaccel does the work and frame threads would only add
    // per-frame latency (and copies) on top of it. Checked after the device
    // setup so a failed VT device also gets threads.
    if (!hw_requested_ && par->codec_type == AVMEDIA_TYPE_VIDEO) {
        ctx_->thread_count = 0;
        ctx_->thread_type  = FF_THREAD_FRAME | FF_THREAD_SLICE;
    }

    ret = avcodec_open2(ctx_, codec, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        logger::error("avcodec_open2 failed: {}", errbuf);
        close();
        return false;
    }
    name_ = codec->name;
    open_ = true;

    logger::info("Decoder opened: {} ({}×{}){}",
        codec->name, ctx_->width, ctx_->height,
        hw_requested_ ? "  [HW: VideoToolbox requested]"
        : (par->codec_type == AVMEDIA_TYPE_VIDEO
               // libdav1d etc. keep thread_count 0 (= auto, own pool) after open
               ? (ctx_->thread_count > 0 ? std::format("  [SW: {} threads]", ctx_->thread_count)
                                          : std::string("  [SW: auto threads]"))
               : std::string{}));
    return true;
}

int Decoder::push(AVPacket* pkt)
{
    if (!ctx_) return AVERROR(EINVAL);
    if (!pkt) draining_ = true;
    const int ret = avcodec_send_packet(ctx_, pkt);
    // Not decode errors: EAGAIN (frames to receive first), and EOF once the
    // decoder was drained (it takes nothing more until flush()).
    if (ret < 0 && ret != AVERROR(EAGAIN) && !(ret == AVERROR_EOF && draining_))
        note_error(ret);
    return ret;
}

bool Decoder::pull(AVFrame* frame)
{
    if (!ctx_) return false;
    // Zero-copy path: receive directly into caller's frame. When HW is active,
    // frame->format = AV_PIX_FMT_VIDEOTOOLBOX and frame->data[3] holds a
    // CVPixelBufferRef; VideoRenderer binds its IOSurface as a GL texture.
    // When HW isn't in use (SW decode, or codec fell back to SW), the frame
    // is a normal system-memory AVFrame (yuv420p / nv12 / etc.).
    for (int failed = 0;;) {
        const int ret = avcodec_receive_frame(ctx_, frame);
        if (ret == 0) break;
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) return false;
        // A packet that failed: the next frame may be there all the same.
        note_error(ret);
        if (++failed >= kMaxErrorsPerPull) return false;
    }
    hw_run_.store(0, std::memory_order_relaxed);
    if (frame->decode_error_flags || (frame->flags & AV_FRAME_FLAG_CORRUPT)) {
        corrupt_.fetch_add(1, std::memory_order_relaxed);
        log_errors(false);
    }

    if (!hw_active_ && frame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
        hw_active_ = true;
        logger::info("Decoder: VideoToolbox HW decode active (zero-copy IOSurface path)");
    }
    return true;
}

void Decoder::flush()
{
    if (!ctx_) return;
    avcodec_flush_buffers(ctx_);
    draining_ = false;
}

void Decoder::note_error(int err)
{
    errors_.fetch_add(1, std::memory_order_relaxed);
    last_error_.store(err, std::memory_order_relaxed);
    if (hw_chosen_) {
        const std::int64_t run = hw_run_.load(std::memory_order_relaxed) + 1;
        hw_run_.store(run, std::memory_order_relaxed);
        if (run > hw_run_max_.load(std::memory_order_relaxed))
            hw_run_max_.store(run, std::memory_order_relaxed);
    }
    log_errors(false);
}

void Decoder::log_errors(bool now)
{
    const Errors e = errors();
    if (e.errors == logged_errors_ && e.corrupt == logged_corrupt_) return;   // nothing new
    const auto t = std::chrono::steady_clock::now();
    if (!now && last_log_ != std::chrono::steady_clock::time_point{} && t - last_log_ < kErrorLogInterval)
        return;
    last_log_       = t;
    logged_errors_  = e.errors;
    logged_corrupt_ = e.corrupt;
    logger::warn("Decoder {}: {}", name_.empty() ? "?" : name_, describe_decode_errors(e));
}

Decoder::Errors Decoder::errors() const
{
    Errors e;
    e.errors     = errors_.load(std::memory_order_relaxed);
    e.last       = last_error_.load(std::memory_order_relaxed);
    e.corrupt    = corrupt_.load(std::memory_order_relaxed);
    e.hw_run     = hw_run_.load(std::memory_order_relaxed);
    e.hw_run_max = hw_run_max_.load(std::memory_order_relaxed);
    return e;
}

void Decoder::skip_unreferenced(bool on)
{
    if (!ctx_ || (ctx_->codec_id != AV_CODEC_ID_H264 && ctx_->codec_id != AV_CODEC_ID_HEVC)) return;
    ctx_->skip_frame = on ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;
}

int Decoder::width()  const { return ctx_ ? ctx_->width  : 0; }
int Decoder::height() const { return ctx_ ? ctx_->height : 0; }

AVPixelFormat Decoder::pixel_format() const
{
    return ctx_ ? ctx_->pix_fmt : AV_PIX_FMT_NONE;
}

AVColorSpace Decoder::colorspace() const
{
    return ctx_ ? ctx_->colorspace : AVCOL_SPC_UNSPECIFIED;
}

AVColorRange Decoder::color_range() const
{
    return ctx_ ? ctx_->color_range : AVCOL_RANGE_UNSPECIFIED;
}

AVColorPrimaries Decoder::color_primaries() const
{
    return ctx_ ? ctx_->color_primaries : AVCOL_PRI_UNSPECIFIED;
}

AVColorTransferCharacteristic Decoder::color_transfer() const
{
    return ctx_ ? ctx_->color_trc : AVCOL_TRC_UNSPECIFIED;
}

AVRational Decoder::sample_aspect_ratio() const
{
    if (!ctx_) return {0, 1};
    // Codec-level SAR (may be 0/1 = unspecified; caller should also check stream-level SAR).
    return ctx_->sample_aspect_ratio;
}

const char* Decoder::hw_type_name() const
{
    return hw_active_ ? "VideoToolbox" : "Software";
}

std::string av_error_text(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {};
    av_strerror(err, buf, sizeof(buf));
    return buf;
}

std::string describe_decode_errors(const Decoder::Errors& e)
{
    std::string out;
    auto add = [&out](const std::string& part) {
        if (!out.empty()) out += ", ";
        out += part;
    };
    if (e.errors > 0)
        add(std::format("{} decode error{} (last: {})", e.errors, e.errors == 1 ? "" : "s",
                        av_error_text(e.last)));
    if (e.hw_run_max > 0)
        add(std::format("up to {} in a row under VideoToolbox", e.hw_run_max));
    if (e.corrupt > 0)
        add(std::format("{} corrupt frame{}", e.corrupt, e.corrupt == 1 ? "" : "s"));
    if (e.hw_run_max > 0)
        out += "; --no-hw decodes in software";
    return out;
}
