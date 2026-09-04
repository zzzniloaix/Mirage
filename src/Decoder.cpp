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
static AVPixelFormat get_hw_format_cb(AVCodecContext* ctx,
                                      const AVPixelFormat* pix_fmts)
{
    for (const AVPixelFormat* p = pix_fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == AV_PIX_FMT_VIDEOTOOLBOX)
            return *p;
    }
    return avcodec_default_get_format(ctx, pix_fmts);
}

Decoder::~Decoder()
{
    close();
}

void Decoder::close()
{
    if (ctx_)            avcodec_free_context(&ctx_);
    if (hw_frame_)       av_frame_free(&hw_frame_);
    if (hw_device_ctx_)  av_buffer_unref(&hw_device_ctx_);
    hw_requested_ = false;
    hw_active_    = false;
}

bool Decoder::open(AVCodecParameters* par, bool try_hw)
{
    const AVCodec* codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        logger::error("No decoder found for codec id {}", static_cast<int>(par->codec_id));
        return false;
    }

    ctx_ = avcodec_alloc_context3(codec);
    if (!ctx_) {
        logger::error("avcodec_alloc_context3 failed");
        return false;
    }

    int ret = avcodec_parameters_to_context(ctx_, par);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        logger::error("avcodec_parameters_to_context failed: {}", errbuf);
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
            ctx_->get_format    = get_hw_format_cb;
            hw_frame_           = av_frame_alloc();
            if (!hw_frame_) {
                logger::error("av_frame_alloc for HW scratch failed");
                return false;
            }
        }
    }

    ret = avcodec_open2(ctx_, codec, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        logger::error("avcodec_open2 failed: {}", errbuf);
        return false;
    }

    logger::info("Decoder opened: {} ({}×{}){}",
        codec->name, ctx_->width, ctx_->height,
        hw_requested_ ? "  [HW: VideoToolbox requested]" : "");
    return true;
}

void Decoder::push(AVPacket* pkt)
{
    avcodec_send_packet(ctx_, pkt);
}

bool Decoder::pull(AVFrame* frame)
{
    // SW-only fast path: receive directly into the caller's frame.
    if (!hw_requested_) {
        return avcodec_receive_frame(ctx_, frame) == 0;
    }

    // HW path: receive into scratch. If the frame is actually HW-backed,
    // transfer to the caller's frame (system memory); otherwise (codec fell
    // back to SW despite our request), move directly.
    if (avcodec_receive_frame(ctx_, hw_frame_) != 0) {
        return false;
    }

    if (hw_frame_->format == AV_PIX_FMT_VIDEOTOOLBOX) {
        av_frame_unref(frame);
        int ret = av_hwframe_transfer_data(frame, hw_frame_, 0);
        if (ret < 0) {
            char errbuf[256];
            av_strerror(ret, errbuf, sizeof(errbuf));
            logger::error("av_hwframe_transfer_data failed: {}", errbuf);
            av_frame_unref(hw_frame_);
            return false;
        }
        // Transfer clears props; copy pts/time_base/etc from source.
        av_frame_copy_props(frame, hw_frame_);
        av_frame_unref(hw_frame_);
        if (!hw_active_) {
            hw_active_ = true;
            logger::info("Decoder: VideoToolbox HW decode active (sw_fmt={})",
                av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format)));
        }
    } else {
        av_frame_move_ref(frame, hw_frame_);
    }
    return true;
}

void Decoder::flush()
{
    avcodec_flush_buffers(ctx_);
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
