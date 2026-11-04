#pragma once

// CPU access to the code values of a decoded video frame — what the stream
// carries, before any conversion for display: a software frame as decoded, a
// VideoToolbox frame mapped (its pixel buffer locked for reading). Feeds the
// pixel probe (X) and the scopes (S). On the SW path the renderer's swscale
// output is full-range RGB, which has already clipped sub-black and
// super-white and cut 10-bit to 8; this reads the frame before that.
//
// Y′CbCr formats only (planar or semi-planar, any subsampling, 8–16 bits,
// either endianness); RGB, paletted and bitstream formats are refused.

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
}

#include <cstdint>

class PixelView {
public:
    PixelView() = default;
    ~PixelView();
    PixelView(const PixelView&)            = delete;
    PixelView& operator=(const PixelView&) = delete;

    // Map `f` (a reference is held, so the caller may free it). False when
    // the format has no CPU view here; the view is then empty.
    bool map(const AVFrame* f);
    void reset();

    [[nodiscard]] bool valid()  const { return frame_ != nullptr; }
    [[nodiscard]] int  width()  const { return frame_ ? frame_->width : 0; }
    [[nodiscard]] int  height() const { return frame_ ? frame_->height : 0; }
    [[nodiscard]] int  depth()  const { return desc_ ? desc_->comp[0].depth : 0; }   // bits per Y′ sample
    [[nodiscard]] int  components() const { return desc_ ? desc_->nb_components : 0; }   // 1 = grey
    [[nodiscard]] int  chroma_shift_x() const { return desc_ ? desc_->log2_chroma_w : 0; }
    [[nodiscard]] int  chroma_shift_y() const { return desc_ ? desc_->log2_chroma_h : 0; }
    // The CPU format read (a VideoToolbox frame's pixel buffer format).
    [[nodiscard]] AVPixelFormat format() const { return frame_ ? static_cast<AVPixelFormat>(frame_->format) : AV_PIX_FMT_NONE; }

    // Component c (0 = Y′, 1 = Cb, 2 = Cr) at (x, y) of that component's own
    // sample grid (chroma is subsampled: x >> chroma_shift_x() for a pixel x).
    [[nodiscard]] int sample(int c, int x, int y) const;
    // w values of component c from (x, y) of its grid into dst.
    void read(int c, int x, int y, int w, std::uint16_t* dst) const;

    // Y′, Cb, Cr of coded pixel (x, y): chroma from the sample covering it.
    struct Codes { int y = 0, cb = 0, cr = 0; };
    [[nodiscard]] Codes at(int x, int y) const;

private:
    AVFrame*                  frame_ = nullptr;   // SW: a reference; VT: the mapped frame
    const AVPixFmtDescriptor* desc_  = nullptr;
    bool                      be_    = false;     // big-endian samples
};

// What a Y′CbCr code triple means, for the probe. Normalised as the range
// says: limited range scales by 219 / 224 · 2^(depth−8) around 16 / 128,
// full range by 2^depth − 1.
struct PixelValues {
    double y = 0.0, cb = 0.0, cr = 0.0;   // Y′ 0..1 (black..white level), Cb/Cr −0.5..0.5
    double r = 0.0, g = 0.0, b = 0.0;     // R′G′B′, unclamped: < 0 or > 1 is outside the RGB cube
    bool   has_nits = false;               // PQ or HLG
    double nits_r = 0.0, nits_g = 0.0, nits_b = 0.0, nits_y = 0.0;   // display light, cd/m²
    bool   below_black = false, above_white = false, chroma_out = false;   // limited range only
};

// cs / range: the interpretation to apply (VideoRenderer::interpretation()
// for what the screen shows). HLG nits use the BT.2100 reference OOTF for a
// 1,000 cd/m² display.
[[nodiscard]] PixelValues pixel_values(int y, int cb, int cr, int depth, AVColorSpace cs, AVColorRange range,
                                       AVColorTransferCharacteristic trc);

// Luma coefficients (Kr, Kb) of a matrix: BT.709, BT.2020, else BT.601.
void luma_coefficients(AVColorSpace cs, double& kr, double& kb);
