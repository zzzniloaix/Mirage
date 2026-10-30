#pragma once

// How the VMAF analyzer brings both frames of a pair to what FFmpeg 8.1.1's
// filter graph of the section 5 comparison,
//
//   [0:v]scale=W:H:flags=<scaler>,format=yuv420p[d];[1:v]format=yuv420p[r];[d][r]libvmaf
//
// gives libvmaf (DECISIONS.md D-016, D-021, D-022). libavfilter negotiates one
// color matrix and one range for both inputs of the libvmaf filter, and the
// distorted input's scale filter scales the distorted frames and converts them
// to those tags. A yuv420p reference passes unconverted, and the tags are its
// own as far as it has tags (target()). For a reference in another pixel
// format, libavfilter puts a scale filter of its own before the reference's
// format filter, which converts the reference to yuv420p and to the
// negotiated tags; those are then the distorted frames' own, as far as they
// have tags (negotiated()). Each conversion is one sws_scale_frame() call, as
// in FFmpeg's scale filter (convert()). FFmpeg's logs of the committed cases
// are in eval/consistency/color-cases-before/ and format-cases-before/;
// eval/consistency/README.md sums them up. tests/test_vmaf_color.cpp runs the
// graph through libavfilter and compares the bytes libvmaf gets.

extern "C" {
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

#include <span>

struct AVFrame;

namespace vmafcolor {

// A frame's YCbCr matrix and range tags (AVFrame::colorspace, color_range).
struct Tags {
    AVColorSpace space = AVCOL_SPC_UNSPECIFIED;
    AVColorRange range = AVCOL_RANGE_UNSPECIFIED;

    bool operator==(const Tags&) const = default;
};

Tags tags_of(const AVFrame& frame);

// What the negotiation goes by: a frame's pixel format and tags.
struct Input {
    AVPixelFormat format = AV_PIX_FMT_NONE;
    Tags          tags;

    bool operator==(const Input&) const = default;
};

Input input_of(const AVFrame& frame);

// Whether FFmpeg's graph converts the reference's frames as well: its format
// filter takes yuv420p only, so for any other pixel format (10-bit, 4:2:2,
// 4:4:4, the yuvj420p of a full range H.264 stream, ...) libavfilter inserts
// a scale filter before it.
bool converts_reference(AVPixelFormat ref_format);

// The flags of that inserted scale filter: `ffmpeg -lavfi` gives it no
// options, so it has swscale's default flags, bicubic's (flags:0x00000004 in
// the logs of eval/consistency/format-cases-before/).
inline constexpr unsigned kInsertedScaleFlags = SWS_BICUBIC;

// The matrix and range libavfilter negotiates for both inputs of the libvmaf
// filter, from both frames' pixel formats and tags (libavfilter/buffersrc.c,
// formats.c and avfiltergraph.c of FFmpeg 8.1.1). A buffer source offers its
// frames' matrix tag, and their range tag, or unspecified and limited when
// they have none; for a yuvj format, full range only; for RGB, gray and the
// other formats that aren't plain YUV, every matrix and every range.
//
//   - A yuv420p reference: its own lists reach the libvmaf filter. The
//     matrix is the reference's; the range is the reference's, or, when it
//     has no range tag, limited if the distorted input's buffer source offers
//     limited only, else unspecified.
//   - Any other reference: the inserted scale filter cuts the reference's
//     lists off, so both inputs share a list of every matrix swscale writes
//     and one of every range. libavfilter reduces each to the entry of the
//     first filter whose input offers just one, going through the filters in
//     the graph's order: the distorted input's scale filter, then the
//     inserted one (reduce_formats). So the matrix and the range are each
//     the distorted input's when its buffer source offers just one, else the
//     reference's when its buffer source does, else unspecified, the shared
//     list's first entry (pick_format).
//
// tests/test_vmaf_color.cpp checks this against libavfilter.
Tags negotiated(const Input& ref, const Input& dis);

// The tags the analyzer gives the distorted frames for a yuv420p reference,
// as implemented since D-016 (DECISIONS.md D-021): the reference's matrix,
// unspecified included, and the reference's range. When the reference has no
// range tag, the distorted frames keep a limited or unspecified range, and a
// full range becomes unspecified. negotiated() gives the same, but for
// distorted frames tagged limited in a yuvj format or one that isn't plain
// YUV, whose buffer source offers more than limited: there FFmpeg's graph
// tags them unspecified where this keeps limited, and libswscale converts
// both alike, so the pixels are the same (tests/test_vmaf_color.cpp).
Tags target(const Tags& ref, const Tags& dis);

// Whether libswscale converts from and to this matrix (sws_test_colorspace).
// FFmpeg can't build the graph when either input has another one, the other
// input's matrix being supported or the same (tests/test_vmaf_color.cpp;
// eval/consistency/color-cases-before/refused/, format-cases-before/refused/).
bool supported(AVColorSpace space);

// Whether the analyzer's conversion of frames in this pixel format is checked
// to give the bytes FFmpeg's graph gives libvmaf: tests/test_vmaf_color.cpp
// compares every format of verified_formats(), as the reference and as the
// distorted input, with the graph run through libavfilter on its default
// threads. NV12 and NV21 aren't among them: libswscale converts them
// differently with more than one thread when the matrix changes (the same
// test), and the analyzer converts on one.
bool verified(AVPixelFormat format);
std::span<const AVPixelFormat> verified_formats();

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

// Converts `in`, a decoded frame, into `out` as FFmpeg's scale filter does
// with its default options: `in` loses its chroma location and interlaced
// flag (in_chroma_loc=auto, interl=0), `out` gets `in`'s properties and side
// data, then the tags `to`, and sws_scale_frame() converts with `sws`, a
// context from sws_alloc_context() with the scale filter's flags set. `out`
// must have its size and pixel format set and no buffers; they are allocated
// here. Returns 0 or a negative AVERROR.
int convert(SwsContext* sws, AVFrame& in, AVFrame& out, const Tags& to);

} // namespace vmafcolor
