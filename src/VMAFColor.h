#pragma once

// How the VMAF analyzer brings a distorted frame to the reference's size,
// color matrix and range (DECISIONS.md D-016): the way FFmpeg 8.1.1's filter
// graph of the section 5 comparison,
//
//   [0:v]scale=W:H:flags=bilinear,format=yuv420p[d];[1:v]format=yuv420p[r];[d][r]libvmaf
//
// does it. The libvmaf filter makes its two inputs share one matrix and one
// range, the reference's frames pass unconverted, and the scale filter scales
// and converts the distorted frames to what was negotiated, in one
// sws_scale_frame() call. eval/consistency/README.md ("Color tag cases")
// has FFmpeg's logs for each case.

extern "C" {
#include <libavutil/pixfmt.h>
}

struct AVFrame;
struct SwsContext;

namespace vmafcolor {

// A frame's YCbCr matrix and range tags (AVFrame::colorspace, color_range).
struct Tags {
    AVColorSpace space = AVCOL_SPC_UNSPECIFIED;
    AVColorRange range = AVCOL_RANGE_UNSPECIFIED;

    bool operator==(const Tags&) const = default;
};

Tags tags_of(const AVFrame& frame);

// The tags FFmpeg's graph gives the distorted frames at the output of their
// scale filter, from both inputs' tags: the reference's matrix, unspecified
// included, and the reference's range. When the reference has no range tag,
// the graph keeps the distorted frames' range if that is limited or
// unspecified, and picks unspecified for a full range.
Tags target(const Tags& ref, const Tags& dis);

// Whether libswscale converts from and to this matrix (sws_test_colorspace).
// FFmpeg can't build the graph when either input has another one.
bool supported(AVColorSpace space);

// What libswscale changes in the pixels when it converts `from` to `to`:
// the matrix when the two have different coefficients (sws_getCoefficients;
// an unspecified matrix has BT.601's), the range when one is full range and
// the other is not (unspecified counts as limited).
struct Change {
    bool matrix = false;
    bool range  = false;

    bool operator==(const Change&) const = default;
};

Change change(const Tags& from, const Tags& to);

// Scales and converts `in`, a decoded distorted frame, into `out` as FFmpeg's
// scale filter does with its default options: `in` loses its chroma location
// and interlaced flag (in_chroma_loc=auto, interl=0), `out` gets `in`'s
// properties and side data, then the tags `to`, and sws_scale_frame()
// converts with `sws`, a context from sws_alloc_context() with the scaler's
// flags set. `out` must have its size and pixel format set and no buffers;
// they are allocated here. Returns 0 or a negative AVERROR.
int convert(SwsContext* sws, AVFrame& in, AVFrame& out, const Tags& to);

} // namespace vmafcolor
