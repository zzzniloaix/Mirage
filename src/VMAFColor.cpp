#include "VMAFColor.h"

#include <cstring>

extern "C" {
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

namespace vmafcolor {

Tags tags_of(const AVFrame& frame)
{
    return { .space = frame.colorspace, .range = frame.color_range };
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

bool converts_reference(AVPixelFormat ref_format)
{
    // The graph's format filter takes yuv420p only.
    return ref_format != AV_PIX_FMT_YUV420P;
}

bool supported(AVColorSpace space)
{
    return sws_test_colorspace(space, 0) > 0 && sws_test_colorspace(space, 1) > 0;
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
