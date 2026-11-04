#include "PixelView.h"
#include "FrameInfo.h"   // pq_to_nits

extern "C" {
#include <libavutil/hwcontext.h>
}

#include <algorithm>
#include <cmath>

PixelView::~PixelView() { reset(); }

void PixelView::reset()
{
    av_frame_free(&frame_);
    desc_ = nullptr;
    be_   = false;
}

bool PixelView::map(const AVFrame* f)
{
    reset();
    if (!f || f->width <= 0 || f->height <= 0) return false;

    AVFrame* v = av_frame_alloc();
    if (!v) return false;
    int ret;
    if (f->hw_frames_ctx) {
        v->format = AV_PIX_FMT_NONE;   // the pool's sw_format (NV12 / P010 for VideoToolbox)
        ret = av_hwframe_map(v, f, AV_HWFRAME_MAP_READ);
    } else {
        ret = av_frame_ref(v, f);
    }
    const AVPixFmtDescriptor* d = ret >= 0 ? av_pix_fmt_desc_get(static_cast<AVPixelFormat>(v->format)) : nullptr;
    constexpr std::uint64_t kRefused = AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_BITSTREAM | AV_PIX_FMT_FLAG_HWACCEL
                                     | AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_FLOAT | AV_PIX_FMT_FLAG_XYZ;
    if (!d || (d->flags & kRefused) || (d->nb_components != 1 && d->nb_components < 3)
        || d->comp[0].depth > 16) {
        av_frame_free(&v);
        return false;
    }
    frame_ = v;
    desc_  = d;
    be_    = (d->flags & AV_PIX_FMT_FLAG_BE) != 0;
    return true;
}

int PixelView::sample(int c, int x, int y) const
{
    const AVComponentDescriptor& cd = desc_->comp[c];
    const std::uint8_t* p = frame_->data[cd.plane] + static_cast<std::ptrdiff_t>(y) * frame_->linesize[cd.plane]
                          + static_cast<std::ptrdiff_t>(x) * cd.step + cd.offset;
    const unsigned v = (cd.depth + cd.shift <= 8) ? p[0]
                     : be_ ? (static_cast<unsigned>(p[0]) << 8 | p[1])
                           : (static_cast<unsigned>(p[1]) << 8 | p[0]);
    return static_cast<int>((v >> cd.shift) & ((1u << cd.depth) - 1u));
}

void PixelView::read(int c, int x, int y, int w, std::uint16_t* dst) const
{
    for (int i = 0; i < w; ++i) dst[i] = static_cast<std::uint16_t>(sample(c, x + i, y));
}

PixelView::Codes PixelView::at(int x, int y) const
{
    Codes out;
    if (!valid()) return out;
    x = std::clamp(x, 0, width() - 1);
    y = std::clamp(y, 0, height() - 1);
    out.y = sample(0, x, y);
    if (desc_->nb_components >= 3) {
        out.cb = sample(1, x >> desc_->log2_chroma_w, y >> desc_->log2_chroma_h);
        out.cr = sample(2, x >> desc_->log2_chroma_w, y >> desc_->log2_chroma_h);
    } else {
        out.cb = out.cr = 1 << (desc_->comp[0].depth - 1);   // grey: neutral chroma
    }
    return out;
}

void luma_coefficients(AVColorSpace cs, double& kr, double& kb)
{
    switch (cs) {
    case AVCOL_SPC_BT709:      kr = 0.2126; kb = 0.0722; break;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:  kr = 0.2627; kb = 0.0593; break;
    default:                   kr = 0.299;  kb = 0.114;  break;
    }
}

namespace {

// BT.2100 HLG inverse OETF: non-linear signal → scene light, both 0..1.
double hlg_inverse_oetf(double e)
{
    constexpr double a = 0.17883277, b = 0.28466892, c = 0.55991073;   // BT.2100: b = 1 − 4a, c = 0.5 − a·ln(4a)
    e = std::clamp(e, 0.0, 1.0);
    return e <= 0.5 ? e * e / 3.0 : (std::exp((e - c) / a) + b) / 12.0;
}

} // namespace

PixelValues pixel_values(int y, int cb, int cr, int depth, AVColorSpace cs, AVColorRange range,
                         AVColorTransferCharacteristic trc)
{
    PixelValues v;
    const double s   = std::ldexp(1.0, depth - 8);   // 2^(depth−8)
    const double max = std::ldexp(1.0, depth) - 1.0;
    if (range == AVCOL_RANGE_JPEG) {
        v.y  = y / max;
        v.cb = (cb - std::ldexp(1.0, depth - 1)) / max;
        v.cr = (cr - std::ldexp(1.0, depth - 1)) / max;
    } else {
        v.y  = (y - 16.0 * s) / (219.0 * s);
        v.cb = (cb - 128.0 * s) / (224.0 * s);
        v.cr = (cr - 128.0 * s) / (224.0 * s);
        v.below_black = y < 16.0 * s;
        v.above_white = y > 235.0 * s;
        v.chroma_out  = cb < 16.0 * s || cb > 240.0 * s || cr < 16.0 * s || cr > 240.0 * s;
    }
    double kr, kb;
    luma_coefficients(cs, kr, kb);
    const double kg = 1.0 - kr - kb;
    v.r = v.y + 2.0 * (1.0 - kr) * v.cr;
    v.b = v.y + 2.0 * (1.0 - kb) * v.cb;
    v.g = (v.y - kr * v.r - kb * v.b) / kg;

    if (trc == AVCOL_TRC_SMPTE2084) {
        v.has_nits = true;
        v.nits_r = frameinfo::pq_to_nits(v.r);
        v.nits_g = frameinfo::pq_to_nits(v.g);
        v.nits_b = frameinfo::pq_to_nits(v.b);
        v.nits_y = kr * v.nits_r + kg * v.nits_g + kb * v.nits_b;
    } else if (trc == AVCOL_TRC_ARIB_STD_B67) {
        constexpr double kLw = 1000.0, kGamma = 1.2;   // nominal display peak; system gamma at 1,000 cd/m²
        const double rs = hlg_inverse_oetf(v.r), gs = hlg_inverse_oetf(v.g), bs = hlg_inverse_oetf(v.b);
        const double ys = kr * rs + kg * gs + kb * bs;
        const double k  = ys > 0.0 ? kLw * std::pow(ys, kGamma - 1.0) : 0.0;
        v.has_nits = true;
        v.nits_r = k * rs;
        v.nits_g = k * gs;
        v.nits_b = k * bs;
        v.nits_y = kLw * std::pow(std::max(ys, 0.0), kGamma);
    }
    return v;
}
