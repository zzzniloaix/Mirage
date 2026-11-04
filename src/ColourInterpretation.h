#pragma once

extern "C" {
#include <libavutil/pixfmt.h>
}

// The colour interpretation VideoRenderer converts frames with, and how it
// follows the frames' own tags. Pure functions — no GL; VideoRenderer::init()
// interprets the stream's tags, and upload() applies follow_tags() and
// next_interpretation() to each frame before converting it.
//
// The frames' tags are followed, not the decoder context's: on a stream whose
// tags switch, the context changes before the frames' own do
// (plan/engineer_tools_runs/investigate-2026-11-03/render/results.txt [C7]).

// Colour tags as a stream or a frame carries them; any may be unspecified.
struct ColourTags {
    AVColorSpace                  cs        = AVCOL_SPC_UNSPECIFIED;
    AVColorRange                  range     = AVCOL_RANGE_UNSPECIFIED;
    AVColorTransferCharacteristic trc       = AVCOL_TRC_UNSPECIFIED;
    AVColorPrimaries              primaries = AVCOL_PRI_UNSPECIFIED;
};

// What frames are converted with: a matrix (never unspecified), full or
// limited range, the transfer (PQ and HLG take the HDR path, any other the
// SDR one) and the primaries (reported, not used).
struct ColourInterpretation {
    AVColorSpace                  cs        = AVCOL_SPC_BT709;
    AVColorRange                  range     = AVCOL_RANGE_MPEG;   // JPEG or MPEG
    AVColorTransferCharacteristic trc       = AVCOL_TRC_UNSPECIFIED;
    AVColorPrimaries              primaries = AVCOL_PRI_UNSPECIFIED;

    bool operator==(const ColourInterpretation&) const = default;
};

// 0 = PQ, 1 = HLG, -1 = SDR.
[[nodiscard]] inline int hdr_mode_of(AVColorTransferCharacteristic trc)
{
    switch (trc) {
        case AVCOL_TRC_SMPTE2084:    return 0;   // PQ
        case AVCOL_TRC_ARIB_STD_B67: return 1;   // HLG
        default:                     return -1;
    }
}

// The tags followed once a frame tagged `frame` arrives: each of its specified
// tags wins, an unspecified one keeps `cur`'s (the stream's at open, or an
// earlier frame's).
[[nodiscard]] inline ColourTags follow_tags(const ColourTags& cur, const ColourTags& frame)
{
    ColourTags t = cur;
    if (frame.cs        != AVCOL_SPC_UNSPECIFIED)   t.cs        = frame.cs;
    if (frame.range     != AVCOL_RANGE_UNSPECIFIED) t.range     = frame.range;
    if (frame.trc       != AVCOL_TRC_UNSPECIFIED)   t.trc       = frame.trc;
    if (frame.primaries != AVCOL_PRI_UNSPECIFIED)   t.primaries = frame.primaries;
    return t;
}

// `tags` as a width × height frame is converted, at open (VideoRenderer::init()).
// A matrix no tag has given is derived for the HDR state and the size:
// BT.2020 NCL on the HDR path; on the SDR one BT.709 from 1280×720 up, BT.601
// below. Any range but full is limited.
[[nodiscard]] inline ColourInterpretation interpret(const ColourTags& tags, int width, int height)
{
    ColourInterpretation i;
    i.cs = tags.cs;
    if (i.cs == AVCOL_SPC_UNSPECIFIED) {
        i.cs = hdr_mode_of(tags.trc) >= 0
            ? AVCOL_SPC_BT2020_NCL
            : ((width >= 1280 || height >= 720) ? AVCOL_SPC_BT709 : AVCOL_SPC_BT470BG);
    }
    i.range     = tags.range == AVCOL_RANGE_JPEG ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    i.trc       = tags.trc;
    i.primaries = tags.primaries;
    return i;
}

// The interpretation `cur` gives way to for a width × height frame, `tags`
// being the tags followed once it arrived (follow_tags()). A matrix no tag
// has given keeps cur's: an untagged ladder whose variants cross 1280×720
// keeps one matrix. It is derived again, as at open (interpret()), only when
// the frame flips the HDR state between SDR and PQ / HLG, so that untagged PQ
// after untagged SDR is converted with BT.2020.
[[nodiscard]] inline ColourInterpretation next_interpretation(const ColourInterpretation& cur,
                                                              const ColourTags& tags, int width, int height)
{
    ColourInterpretation i = interpret(tags, width, height);
    if (tags.cs == AVCOL_SPC_UNSPECIFIED && (hdr_mode_of(tags.trc) >= 0) == (hdr_mode_of(cur.trc) >= 0))
        i.cs = cur.cs;
    return i;
}

// What going from one interpretation to another asks of the renderer:
//   None     — nothing;
//   Uniforms — the matrix, offset and HDR-mode uniforms and sws's
//              coefficients (another matrix or range, PQ ↔ HLG, a transfer
//              or primaries of the same path);
//   Program  — besides, the HDR mode flips between SDR and HDR: other
//              programs, another staging format (RGB24 / RGB48LE), another
//              sws destination.
enum class InterpretationChange { None, Uniforms, Program };

[[nodiscard]] inline InterpretationChange classify_change(const ColourInterpretation& from,
                                                          const ColourInterpretation& to)
{
    if (from == to) return InterpretationChange::None;
    if ((hdr_mode_of(from.trc) >= 0) != (hdr_mode_of(to.trc) >= 0)) return InterpretationChange::Program;
    return InterpretationChange::Uniforms;
}
