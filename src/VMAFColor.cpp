#include "VMAFColor.h"

#include <algorithm>
#include <cstring>
#include <optional>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
}

namespace vmafcolor {

namespace {

// libavfilter's ff_fmt_is_regular_yuv() (avfiltergraph.c): formats with three
// or more components that aren't RGB, palette, XYZ or floating point. Only
// for these does a buffer source offer its frames' own matrix and range.
bool plain_yuv(AVPixelFormat format)
{
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(format);
    if (!d || d->nb_components < 3) return false;
    return !(d->flags & (AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_XYZ |
                         AV_PIX_FMT_FLAG_FLOAT));
}

// ff_fmt_is_forced_full_range(): the deprecated yuvj formats.
bool forced_full_range(AVPixelFormat format)
{
    switch (format) {
    case AV_PIX_FMT_YUVJ420P:
    case AV_PIX_FMT_YUVJ422P:
    case AV_PIX_FMT_YUVJ444P:
    case AV_PIX_FMT_YUVJ440P:
    case AV_PIX_FMT_YUVJ411P:
        return true;
    default:
        return false;
    }
}

// The one matrix a buffer source offers for `in`, if it offers just one
// (libavfilter/buffersrc.c, query_formats).
std::optional<AVColorSpace> one_space(const Input& in)
{
    if (!plain_yuv(in.format)) return std::nullopt;
    return in.tags.space;
}

// The one range: full for a yuvj format, else the tag. An untagged range
// gets unspecified and limited, a format that isn't plain YUV every range.
std::optional<AVColorRange> one_range(const Input& in)
{
    if (!plain_yuv(in.format)) return std::nullopt;
    if (forced_full_range(in.format)) return AVCOL_RANGE_JPEG;
    if (in.tags.range == AVCOL_RANGE_UNSPECIFIED) return std::nullopt;
    return in.tags.range;
}

// The formats tests/test_vmaf_color.cpp checks; see verified().
constexpr AVPixelFormat kVerifiedFormats[] = {
    AV_PIX_FMT_YUV420P,     AV_PIX_FMT_YUVJ420P,    AV_PIX_FMT_YUV422P,     AV_PIX_FMT_YUVJ422P,
    AV_PIX_FMT_YUV444P,     AV_PIX_FMT_YUVJ444P,    AV_PIX_FMT_YUV420P10LE, AV_PIX_FMT_YUV422P10LE,
    AV_PIX_FMT_YUV444P10LE, AV_PIX_FMT_YUV420P12LE, AV_PIX_FMT_YUV422P12LE, AV_PIX_FMT_YUV444P12LE,
    AV_PIX_FMT_P010LE,      AV_PIX_FMT_YUVA420P,    AV_PIX_FMT_YUVA444P10LE, AV_PIX_FMT_GRAY8,
    AV_PIX_FMT_GRAY10LE,    AV_PIX_FMT_RGB24,       AV_PIX_FMT_RGBA,        AV_PIX_FMT_GBRP,
    AV_PIX_FMT_GBRP10LE,
};

} // namespace

Tags tags_of(const AVFrame& frame)
{
    return { .space = frame.colorspace, .range = frame.color_range };
}

Input input_of(const AVFrame& frame)
{
    return { .format = static_cast<AVPixelFormat>(frame.format), .tags = tags_of(frame) };
}

bool converts_reference(AVPixelFormat ref_format)
{
    return ref_format != AV_PIX_FMT_YUV420P;
}

Tags negotiated(const Input& ref, const Input& dis)
{
    Tags t;
    if (!converts_reference(ref.format)) {
        t.space = ref.tags.space;
        if (const auto r = one_range(ref))
            t.range = *r;
        else if (one_range(dis) == AVCOL_RANGE_MPEG)
            t.range = AVCOL_RANGE_MPEG;
        return t;
    }
    if (const auto s = one_space(dis))
        t.space = *s;
    else if (const auto s = one_space(ref))
        t.space = *s;
    if (const auto r = one_range(dis))
        t.range = *r;
    else if (const auto r = one_range(ref))
        t.range = *r;
    return t;
}

Tags target(const Tags& ref, const Tags& dis)
{
    // The reference's buffer source offers only its own matrix, and its own
    // range plus limited when it has none; the libvmaf filter shares that list
    // with the distorted input's scale filter output. libavfilter then keeps
    // the scale filter's input range where the list allows it, else takes the
    // list's first entry (avfiltergraph.c: reduce_formats, pick_format).
    Tags t{ .space = ref.space, .range = ref.range };
    if (ref.range == AVCOL_RANGE_UNSPECIFIED)
        t.range = dis.range == AVCOL_RANGE_MPEG ? AVCOL_RANGE_MPEG : AVCOL_RANGE_UNSPECIFIED;
    return t;
}

bool supported(AVColorSpace space)
{
    return sws_test_colorspace(space, 0) > 0 && sws_test_colorspace(space, 1) > 0;
}

bool verified(AVPixelFormat format)
{
    return std::ranges::find(kVerifiedFormats, format) != std::end(kVerifiedFormats);
}

std::span<const AVPixelFormat> verified_formats()
{
    return kVerifiedFormats;
}

Change change(const Tags& from, const Tags& to)
{
    const int* a = sws_getCoefficients(from.space);
    const int* b = sws_getCoefficients(to.space);
    return { .matrix = std::memcmp(a, b, 4 * sizeof(int)) != 0,
             .range  = (from.range == AVCOL_RANGE_JPEG) != (to.range == AVCOL_RANGE_JPEG) };
}

int convert(SwsContext* sws, AVFrame& in, AVFrame& out, const Tags& to)
{
    // libavfilter/vf_scale.c, scale_frame(), with the filter's default options.
    in.chroma_location = AVCHROMA_LOC_UNSPECIFIED;
    in.flags &= ~AV_FRAME_FLAG_INTERLACED;
    if (int r = av_frame_copy_props(&out, &in); r < 0) return r;
    out.colorspace  = to.space;
    out.color_range = to.range;
    if (int r = av_frame_get_buffer(&out, 0); r < 0) return r;
    const int r = sws_scale_frame(sws, &out, &in);
    return r < 0 ? r : 0;
}

} // namespace vmafcolor
