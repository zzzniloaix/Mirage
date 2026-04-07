#pragma once

extern "C" {
#include <libavcodec/avcodec.h>
}

class Decoder {
public:
    Decoder() = default;
    ~Decoder();

    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    [[nodiscard]] bool open(AVCodecParameters* par);
    void push(AVPacket* pkt);   // avcodec_send_packet
    bool pull(AVFrame* frame);  // avcodec_receive_frame; true = got frame
    void flush();               // avcodec_flush_buffers (call after seek)

    int             width()        const;
    int             height()       const;
    AVPixelFormat   pixel_format() const;
    AVColorSpace    colorspace()   const;
    AVColorRange    color_range()  const;

private:
    AVCodecContext* ctx_ = nullptr;
};
