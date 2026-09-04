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

    // try_hw=true attempts VideoToolbox HW decode on macOS; silently falls back
    // to software if the codec/OS doesn't support it. HW frames are transferred
    // to system memory transparently inside pull() so the caller always sees a
    // regular AVFrame (NV12 or P010LE from VT).
    [[nodiscard]] bool open(AVCodecParameters* par, bool try_hw = false);
    void close();               // free context so open() can be called again
    void push(AVPacket* pkt);   // avcodec_send_packet
    bool pull(AVFrame* frame);  // avcodec_receive_frame; true = got frame
    void flush();               // avcodec_flush_buffers (call after seek)

    int                            width()                const;
    int                            height()               const;
    AVPixelFormat                  pixel_format()         const;
    AVColorSpace                   colorspace()           const;
    AVColorRange                   color_range()          const;
    AVColorPrimaries               color_primaries()      const;
    AVColorTransferCharacteristic  color_transfer()       const;
    AVRational                     sample_aspect_ratio()  const;  // {0,1} means "unspecified / square"

    // HW decode status (populated after the first pull() call — before then,
    // is_hw_active() reflects only whether we *asked* for HW).
    [[nodiscard]] bool        is_hw_active()  const { return hw_active_; }
    [[nodiscard]] const char* hw_type_name()  const;   // "VideoToolbox" or "Software"

private:
    AVCodecContext* ctx_            = nullptr;
    AVBufferRef*    hw_device_ctx_  = nullptr;
    AVFrame*        hw_frame_       = nullptr;   // scratch for HW→SW transfer
    bool            hw_requested_   = false;
    bool            hw_active_      = false;     // set true after first HW frame
};
