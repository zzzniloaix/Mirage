#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "YuvMatrix.h"

#include <array>

using Catch::Matchers::WithinAbs;

// Apply the same math the GLSL shader does:
//     rgb = mat3(m) * (vec3(y, cb, cr) - offset)
// where m is column-major.
static std::array<float, 3>
apply(const YuvMat& mat, float y, float cb, float cr)
{
    float dy = y  - mat.offset[0];
    float du = cb - mat.offset[1];
    float dv = cr - mat.offset[2];
    return {
        mat.m[0] * dy + mat.m[3] * du + mat.m[6] * dv,
        mat.m[1] * dy + mat.m[4] * du + mat.m[7] * dv,
        mat.m[2] * dy + mat.m[5] * du + mat.m[8] * dv,
    };
}

// ── Reference coefficients ────────────────────────────────────────────────────
// Values below come straight from the ITU-R spec inverse matrices. They are the
// "textbook" numbers that would appear in any well-known Wikipedia/standards
// table for YCbCr→RGB. If make_yuv_matrix() drifts from these, saturated colors
// will be visibly wrong on-screen — grays keep working because chroma is ~0.

TEST_CASE("BT.709 limited-range 8-bit: matrix matches ITU-R tabulated values",
          "[yuv][bt709]")
{
    YuvMat m = make_yuv_matrix(AVCOL_SPC_BT709, AVCOL_RANGE_MPEG);

    // Column 0 (Y): 255/219 = 1.16438
    REQUIRE_THAT(m.m[0], WithinAbs(1.16438f, 1e-4f));
    REQUIRE_THAT(m.m[1], WithinAbs(1.16438f, 1e-4f));
    REQUIRE_THAT(m.m[2], WithinAbs(1.16438f, 1e-4f));

    // Column 1 (Cb): [0, -0.213, 2.112] — R has no Cb, G/B have limited-range-scaled coeffs
    REQUIRE_THAT(m.m[3], WithinAbs( 0.00000f, 1e-4f));
    REQUIRE_THAT(m.m[4], WithinAbs(-0.21325f, 1e-3f));
    REQUIRE_THAT(m.m[5], WithinAbs( 2.11240f, 1e-3f));

    // Column 2 (Cr): [1.793, -0.533, 0]
    REQUIRE_THAT(m.m[6], WithinAbs( 1.79274f, 1e-3f));
    REQUIRE_THAT(m.m[7], WithinAbs(-0.53291f, 1e-3f));
    REQUIRE_THAT(m.m[8], WithinAbs( 0.00000f, 1e-4f));

    // Offset: subtracts 16/255 (Y black) and 128/255 ≈ 0.5 (Cb/Cr neutral)
    REQUIRE_THAT(m.offset[0], WithinAbs(16.0f / 255.0f, 1e-5f));
    REQUIRE_THAT(m.offset[1], WithinAbs(0.5f, 1e-5f));
    REQUIRE_THAT(m.offset[2], WithinAbs(0.5f, 1e-5f));
}

TEST_CASE("BT.601 limited-range 8-bit: matrix matches ITU-R tabulated values",
          "[yuv][bt601]")
{
    YuvMat m = make_yuv_matrix(AVCOL_SPC_BT470BG, AVCOL_RANGE_MPEG);

    // Y column
    REQUIRE_THAT(m.m[0], WithinAbs(1.16438f, 1e-4f));

    // Standard limited-range 8-bit BT.601 coefficients (Wikipedia YCbCr):
    // Cb column: [0, -0.392, 2.017]
    REQUIRE_THAT(m.m[3], WithinAbs( 0.00000f, 1e-4f));
    REQUIRE_THAT(m.m[4], WithinAbs(-0.39176f, 1e-3f));
    REQUIRE_THAT(m.m[5], WithinAbs( 2.01723f, 1e-3f));
    // Cr column: [1.596, -0.813, 0]
    REQUIRE_THAT(m.m[6], WithinAbs( 1.59603f, 1e-3f));
    REQUIRE_THAT(m.m[7], WithinAbs(-0.81297f, 1e-3f));
}

TEST_CASE("BT.2020 full-range: no limited-range scaling applied",
          "[yuv][bt2020]")
{
    YuvMat m = make_yuv_matrix(AVCOL_SPC_BT2020_NCL, AVCOL_RANGE_JPEG);

    // Y scale = 1 (no limited-range compression)
    REQUIRE_THAT(m.m[0], WithinAbs(1.0f, 1e-5f));
    // Y offset = 0 (no black-level bias)
    REQUIRE_THAT(m.offset[0], WithinAbs(0.0f, 1e-5f));

    // BT.2020 full range: 2*(1-Kr)=1.4746, 2*(1-Kb)=1.8814
    REQUIRE_THAT(m.m[5], WithinAbs(1.88140f, 1e-3f));   // B from Cb
    REQUIRE_THAT(m.m[6], WithinAbs(1.47460f, 1e-3f));   // R from Cr
    REQUIRE_THAT(m.m[4], WithinAbs(-0.16455f, 1e-3f));  // G from Cb
    REQUIRE_THAT(m.m[7], WithinAbs(-0.57135f, 1e-3f));  // G from Cr
}

// ── Encode-then-decode round-trips ────────────────────────────────────────────
// The bug that motivated these tests was a missing factor of 2 on the chroma
// coefficients. Grays still worked (chroma → 0), so a decoded-pixel round-trip
// test with a saturated color is the sharpest way to catch it.

TEST_CASE("BT.709 limited-range round-trip: saturated red survives encode→decode",
          "[yuv][bt709][roundtrip]")
{
    // Encode BT.709 RGB(1, 0, 0) into 8-bit limited Y'CbCr, then decode via
    // the matrix and confirm we get the original RGB back.
    //     Y'  = Kr*R + Kg*G + Kb*B = 0.2126
    //     Cb  = (B - Y')/1.8556 = -0.11457   (signed, -0.5..0.5)
    //     Cr  = (R - Y')/1.5748 =  0.5
    // Encode to 8-bit limited: Y' → 16 + Y'*219, Cb/Cr → 128 + Cb*224.
    const float y_enc  = (16.0f  + 0.2126f    * 219.0f) / 255.0f;
    const float cb_enc = (128.0f + (-0.11457f) * 224.0f) / 255.0f;
    const float cr_enc = (128.0f + 0.5f       * 224.0f) / 255.0f;

    YuvMat m = make_yuv_matrix(AVCOL_SPC_BT709, AVCOL_RANGE_MPEG);
    auto rgb = apply(m, y_enc, cb_enc, cr_enc);

    // Round-trip tolerance: ~1% is fine — 8-bit quantization noise dominates.
    REQUIRE_THAT(rgb[0], WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(rgb[1], WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(rgb[2], WithinAbs(0.0f, 0.01f));
}

TEST_CASE("BT.709 limited-range round-trip: saturated blue survives encode→decode",
          "[yuv][bt709][roundtrip]")
{
    // BT.709 RGB(0, 0, 1): Y'=0.0722, Cb=0.5, Cr=-0.04592.
    const float y_enc  = (16.0f  + 0.0722f    * 219.0f) / 255.0f;
    const float cb_enc = (128.0f + 0.5f       * 224.0f) / 255.0f;
    const float cr_enc = (128.0f + (-0.04592f) * 224.0f) / 255.0f;

    YuvMat m = make_yuv_matrix(AVCOL_SPC_BT709, AVCOL_RANGE_MPEG);
    auto rgb = apply(m, y_enc, cb_enc, cr_enc);

    REQUIRE_THAT(rgb[0], WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(rgb[1], WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(rgb[2], WithinAbs(1.0f, 0.01f));
}

TEST_CASE("Limited-range black and white anchors map exactly",
          "[yuv][anchors]")
{
    YuvMat m = make_yuv_matrix(AVCOL_SPC_BT709, AVCOL_RANGE_MPEG);

    // Y=16, Cb=Cr=128 → RGB(0,0,0). 128/255 ≠ 0.5 exactly, so a ~0.4% chroma
    // leak into R/G/B is expected — matches real 8-bit YCbCr quantization.
    auto blk = apply(m, 16.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f);
    REQUIRE_THAT(blk[0], WithinAbs(0.0f, 5e-3f));
    REQUIRE_THAT(blk[1], WithinAbs(0.0f, 5e-3f));
    REQUIRE_THAT(blk[2], WithinAbs(0.0f, 5e-3f));

    // Y=235, Cb=Cr=128 → RGB(1,1,1)
    auto wht = apply(m, 235.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f);
    REQUIRE_THAT(wht[0], WithinAbs(1.0f, 5e-3f));
    REQUIRE_THAT(wht[1], WithinAbs(1.0f, 5e-3f));
    REQUIRE_THAT(wht[2], WithinAbs(1.0f, 5e-3f));
}
