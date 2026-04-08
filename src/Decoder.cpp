#include "Decoder.h"
#include "Logger.h"

extern "C" {
#include <libavutil/avutil.h>
}

Decoder::~Decoder()
{
    close();
}

void Decoder::close()
{
    if (ctx_)
        avcodec_free_context(&ctx_);
}

bool Decoder::open(AVCodecParameters* par)
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

    ret = avcodec_open2(ctx_, codec, nullptr);
    if (ret < 0) {
        char errbuf[256];
        av_strerror(ret, errbuf, sizeof(errbuf));
        logger::error("avcodec_open2 failed: {}", errbuf);
        return false;
    }

    logger::info("Decoder opened: {} ({}×{})",
        codec->name, ctx_->width, ctx_->height);
    return true;
}

void Decoder::push(AVPacket* pkt)
{
    avcodec_send_packet(ctx_, pkt);
}

bool Decoder::pull(AVFrame* frame)
{
    return avcodec_receive_frame(ctx_, frame) == 0;
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
