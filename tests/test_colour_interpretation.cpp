// The colour interpretation VideoRenderer::upload() works out for each frame
// (ColourInterpretation.h): the tags it follows, the defaults it derives, and
// what a change asks of the renderer (nothing, the uniforms, the programs).

#include <catch2/catch_test_macros.hpp>

#include "ColourInterpretation.h"

namespace {

// The renderer at open: the stream's tags, interpreted for its size.
struct Followed {
    ColourTags           tags;
    ColourInterpretation interp;
};

Followed opened(const ColourTags& stream, int w, int h)
{
    return { stream, interpret(stream, w, h) };
}

// One frame through the resolver, as upload() runs it: the change it makes.
InterpretationChange next_frame(Followed& f, const ColourTags& frame, int w, int h)
{
    f.tags = follow_tags(f.tags, frame);
    const ColourInterpretation next = interpret(f.tags, w, h);
    const InterpretationChange change = classify_change(f.interp, next);
    f.interp = next;
    return change;
}

const ColourTags kSdr709 = { AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, AVCOL_TRC_BT709, AVCOL_PRI_BT709 };
const ColourTags kPq     = { AVCOL_SPC_BT2020_NCL, AVCOL_RANGE_MPEG, AVCOL_TRC_SMPTE2084, AVCOL_PRI_BT2020 };
const ColourTags kHlg    = { AVCOL_SPC_BT2020_NCL, AVCOL_RANGE_MPEG, AVCOL_TRC_ARIB_STD_B67, AVCOL_PRI_BT2020 };
const ColourTags kNone   = {};

} // namespace

TEST_CASE("follow_tags: a frame's specified tag wins, an unspecified one keeps the current",
          "[colour_interp]")
{
    const ColourTags t = follow_tags(kSdr709, { AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_JPEG,
                                                AVCOL_TRC_UNSPECIFIED, AVCOL_PRI_BT2020 });
    CHECK(t.cs        == AVCOL_SPC_BT709);
    CHECK(t.range     == AVCOL_RANGE_JPEG);
    CHECK(t.trc       == AVCOL_TRC_BT709);
    CHECK(t.primaries == AVCOL_PRI_BT2020);

    const ColourTags u = follow_tags(kSdr709, kNone);
    CHECK(u.cs == kSdr709.cs);
    CHECK(u.range == kSdr709.range);
    CHECK(u.trc == kSdr709.trc);
    CHECK(u.primaries == kSdr709.primaries);
}

TEST_CASE("interpret: an untagged matrix is derived for the HDR state and the size",
          "[colour_interp]")
{
    CHECK(interpret(kNone, 1920, 1080).cs == AVCOL_SPC_BT709);
    CHECK(interpret(kNone, 1280, 720).cs  == AVCOL_SPC_BT709);
    CHECK(interpret(kNone, 720, 576).cs   == AVCOL_SPC_BT470BG);
    ColourTags pq_only;
    pq_only.trc = AVCOL_TRC_SMPTE2084;
    CHECK(interpret(pq_only, 720, 576).cs == AVCOL_SPC_BT2020_NCL);
    pq_only.trc = AVCOL_TRC_ARIB_STD_B67;
    CHECK(interpret(pq_only, 1920, 1080).cs == AVCOL_SPC_BT2020_NCL);
    // A tagged matrix is kept, whatever the transfer and the size.
    ColourTags tagged = pq_only;
    tagged.cs = AVCOL_SPC_BT709;
    CHECK(interpret(tagged, 720, 576).cs == AVCOL_SPC_BT709);

    // Any range but full is limited.
    CHECK(interpret(kNone, 1920, 1080).range == AVCOL_RANGE_MPEG);
    ColourTags full;
    full.range = AVCOL_RANGE_JPEG;
    CHECK(interpret(full, 1920, 1080).range == AVCOL_RANGE_JPEG);
}

TEST_CASE("classify_change: SDR to PQ needs the program, PQ to HLG the uniforms, "
          "an untagged frame nothing", "[colour_interp]")
{
    {
        Followed f = opened(kSdr709, 1920, 1080);
        CHECK(next_frame(f, kPq, 1920, 1080) == InterpretationChange::Program);
        CHECK(f.interp.cs  == AVCOL_SPC_BT2020_NCL);
        CHECK(f.interp.trc == AVCOL_TRC_SMPTE2084);
    }
    {
        Followed f = opened(kPq, 3840, 2160);
        CHECK(next_frame(f, kHlg, 3840, 2160) == InterpretationChange::Uniforms);
        CHECK(hdr_mode_of(f.interp.trc) == 1);
    }
    {
        Followed f = opened(kPq, 3840, 2160);
        const ColourInterpretation before = f.interp;
        CHECK(next_frame(f, kNone, 3840, 2160) == InterpretationChange::None);
        CHECK(f.interp == before);
    }
    // Back from PQ to SDR: the program again.
    {
        Followed f = opened(kPq, 1920, 1080);
        CHECK(next_frame(f, kSdr709, 1920, 1080) == InterpretationChange::Program);
    }
}

TEST_CASE("classify_change: another matrix, range or primaries on the same path needs the uniforms",
          "[colour_interp]")
{
    Followed f = opened(kSdr709, 1920, 1080);
    CHECK(next_frame(f, { AVCOL_SPC_SMPTE170M, AVCOL_RANGE_UNSPECIFIED, AVCOL_TRC_UNSPECIFIED,
                          AVCOL_PRI_UNSPECIFIED }, 1920, 1080) == InterpretationChange::Uniforms);
    CHECK(f.interp.cs == AVCOL_SPC_SMPTE170M);
    CHECK(next_frame(f, { AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_JPEG, AVCOL_TRC_UNSPECIFIED,
                          AVCOL_PRI_UNSPECIFIED }, 1920, 1080) == InterpretationChange::Uniforms);
    CHECK(f.interp.range == AVCOL_RANGE_JPEG);
    CHECK(next_frame(f, { AVCOL_SPC_UNSPECIFIED, AVCOL_RANGE_UNSPECIFIED, AVCOL_TRC_UNSPECIFIED,
                          AVCOL_PRI_BT2020 }, 1920, 1080) == InterpretationChange::Uniforms);
    // The same tags again: nothing.
    CHECK(next_frame(f, { AVCOL_SPC_SMPTE170M, AVCOL_RANGE_JPEG, AVCOL_TRC_BT709,
                          AVCOL_PRI_BT2020 }, 1920, 1080) == InterpretationChange::None);
    // A frame tagged limited where the stream said nothing: the same interpretation.
    Followed g = opened({ AVCOL_SPC_BT709, AVCOL_RANGE_UNSPECIFIED, AVCOL_TRC_BT709, AVCOL_PRI_BT709 },
                        1920, 1080);
    CHECK(next_frame(g, kSdr709, 1920, 1080) == InterpretationChange::None);
}

TEST_CASE("resolver: an untagged stream's matrix follows the HDR state and the size",
          "[colour_interp]")
{
    // Untagged HD SDR: BT.709 by size; a frame tagged PQ alone (no matrix):
    // BT.2020, on the HDR path.
    Followed f = opened(kNone, 1920, 1080);
    CHECK(f.interp.cs == AVCOL_SPC_BT709);
    ColourTags pq_only;
    pq_only.trc = AVCOL_TRC_SMPTE2084;
    CHECK(next_frame(f, pq_only, 1920, 1080) == InterpretationChange::Program);
    CHECK(f.interp.cs == AVCOL_SPC_BT2020_NCL);

    // Untagged SDR going from HD to SD frames: BT.709, then BT.601.
    Followed g = opened(kNone, 1920, 1080);
    CHECK(next_frame(g, kNone, 720, 576) == InterpretationChange::Uniforms);
    CHECK(g.interp.cs == AVCOL_SPC_BT470BG);

    // A matrix a tag gave stays when a PQ frame gives none.
    Followed h = opened({ AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, AVCOL_TRC_UNSPECIFIED, AVCOL_PRI_UNSPECIFIED },
                        1920, 1080);
    CHECK(next_frame(h, pq_only, 1920, 1080) == InterpretationChange::Program);
    CHECK(h.interp.cs == AVCOL_SPC_BT709);
}

TEST_CASE("resolver: the tag-switch stream changes once, at its first PQ frame",
          "[colour_interp]")
{
    // The frames of plan/engineer_tools_runs/investigate-2026-11-03/render/
    // results.txt [C7]: the stream's tags BT.709 throughout, frames 1 to 75
    // tagged BT.709, frames 76 to 150 PQ / BT.2020.
    Followed f = opened(kSdr709, 1280, 720);
    int programs = 0, others = 0, first = 0;
    for (int n = 1; n <= 150; ++n) {
        const InterpretationChange c = next_frame(f, n < 76 ? kSdr709 : kPq, 1280, 720);
        if (c == InterpretationChange::Program && ++programs == 1) first = n;
        if (c == InterpretationChange::Uniforms) ++others;
    }
    CHECK(programs == 1);
    CHECK(first == 76);
    CHECK(others == 0);
    CHECK(f.interp.cs == AVCOL_SPC_BT2020_NCL);
    CHECK(hdr_mode_of(f.interp.trc) == 0);
}
