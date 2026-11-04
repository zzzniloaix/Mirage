#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "YuvMatrix.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <initializer_list>

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

    // Offset: subtracts 16/255 (Y black) and 128/255 (Cb/Cr neutral)
    REQUIRE_THAT(m.offset[0], WithinAbs(16.0f / 255.0f, 1e-5f));
    REQUIRE_THAT(m.offset[1], WithinAbs(128.0f / 255.0f, 1e-5f));
    REQUIRE_THAT(m.offset[2], WithinAbs(128.0f / 255.0f, 1e-5f));
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

    // Y=16, Cb=Cr=128 → RGB(0,0,0). The Cb/Cr zero is 128/255, so no chroma
    // leaks into R/G/B.
    auto blk = apply(m, 16.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f);
    REQUIRE_THAT(blk[0], WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(blk[1], WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(blk[2], WithinAbs(0.0f, 1e-5f));

    // Y=235, Cb=Cr=128 → RGB(1,1,1)
    auto wht = apply(m, 235.0f / 255.0f, 128.0f / 255.0f, 128.0f / 255.0f);
    REQUIRE_THAT(wht[0], WithinAbs(1.0f, 1e-5f));
    REQUIRE_THAT(wht[1], WithinAbs(1.0f, 1e-5f));
    REQUIRE_THAT(wht[2], WithinAbs(1.0f, 1e-5f));
}

// ── Container and sample depth ────────────────────────────────────────────────
// VideoToolbox's 10-bit buffers ('x420' = P010, 'x422', 'x444') hold code × 64
// in 16-bit words, read as R16 / RG16 (word / 65535); its 'sv44' ProRes
// buffers 16-bit codes. The constants come from the container's code values.

// A code of `bits` significant bits, MSB-aligned in a 16-bit word, as R16 reads it.
static float word16(int code, int bits)
{
    return static_cast<float>(code << (16 - bits)) / 65535.0f;
}

TEST_CASE("P010 limited: codes 64 / 940 / 512 are black / white / neutral",
          "[yuv][p010]")
{
    for (AVColorSpace cs : { AVCOL_SPC_BT709, AVCOL_SPC_BT2020_NCL }) {
        YuvMat m = make_yuv_matrix(cs, AVCOL_RANGE_MPEG, 16, 10);
        for (float c : apply(m, word16(64, 10), word16(512, 10), word16(512, 10)))
            REQUIRE_THAT(c, WithinAbs(0.0f, 1e-5f));
        for (float c : apply(m, word16(940, 10), word16(512, 10), word16(512, 10)))
            REQUIRE_THAT(c, WithinAbs(1.0f, 1e-5f));
    }
    // Every 10-bit grey at (code - 64) / 876, on every channel.
    YuvMat m = make_yuv_matrix(AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, 16, 10);
    for (int code : { 65, 100, 300, 509, 723, 1019 })
        for (float c : apply(m, word16(code, 10), word16(512, 10), word16(512, 10)))
            REQUIRE_THAT(c, WithinAbs((code - 64) / 876.0f, 1e-5f));
}

TEST_CASE("8-bit limited: Cb = Cr = 128 is exactly neutral", "[yuv][anchors]")
{
    YuvMat m = make_yuv_matrix(AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, 8, 8);
    for (int y : { 16, 64, 126, 128, 180, 235 }) {
        auto rgb = apply(m, y / 255.0f, 128 / 255.0f, 128 / 255.0f);
        // The offset cancels the chroma exactly: the three channels are one value.
        CHECK(rgb[0] == rgb[1]);
        CHECK(rgb[1] == rgb[2]);
        REQUIRE_THAT(rgb[1], WithinAbs((y - 16) / 219.0f, 1e-6f));
    }
}

TEST_CASE("'sv44' (P416) limited: 4096 / 60160 are black / white", "[yuv][p416]")
{
    YuvMat m = make_yuv_matrix(AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, 16, 16);
    for (float c : apply(m, 4096 / 65535.0f, 32768 / 65535.0f, 32768 / 65535.0f))
        REQUIRE_THAT(c, WithinAbs(0.0f, 1e-5f));
    for (float c : apply(m, 60160 / 65535.0f, 32768 / 65535.0f, 32768 / 65535.0f))
        REQUIRE_THAT(c, WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("P010 full range ('xf20'): codes 0 / 1023 are black / white", "[yuv][p010]")
{
    YuvMat m = make_yuv_matrix(AVCOL_SPC_BT2020_NCL, AVCOL_RANGE_JPEG, 16, 10);
    for (float c : apply(m, 0.0f, word16(512, 10), word16(512, 10)))
        REQUIRE_THAT(c, WithinAbs(0.0f, 1e-5f));
    for (float c : apply(m, word16(1023, 10), word16(512, 10), word16(512, 10)))
        REQUIRE_THAT(c, WithinAbs(1.0f, 1e-5f));
    // 8-bit full range: 0 / 255, the Cb/Cr zero at 128/255.
    YuvMat m8 = make_yuv_matrix(AVCOL_SPC_BT709, AVCOL_RANGE_JPEG, 8, 8);
    REQUIRE_THAT(m8.offset[1], WithinAbs(128.0f / 255.0f, 1e-7f));
    for (float c : apply(m8, 255 / 255.0f, 128 / 255.0f, 128 / 255.0f))
        REQUIRE_THAT(c, WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("P010 limited round-trip: saturated BT.2020 red survives encode→decode",
          "[yuv][p010][roundtrip]")
{
    // BT.2020 RGB(1, 0, 0): Y'=0.2627, Cb=-0.2627/1.8814, Cr=0.5, as 10-bit
    // limited codes: Y' → 64 + Y'*876, Cb/Cr → 512 + Cb*896.
    const double Y = 0.2627, Cb = -0.2627 / 1.8814, Cr = 0.5;
    const int y  = static_cast<int>(std::lround(64.0  + Y  * 876.0));
    const int cb = static_cast<int>(std::lround(512.0 + Cb * 896.0));
    const int cr = static_cast<int>(std::lround(512.0 + Cr * 896.0));

    YuvMat m = make_yuv_matrix(AVCOL_SPC_BT2020_NCL, AVCOL_RANGE_MPEG, 16, 10);
    auto rgb = apply(m, word16(y, 10), word16(cb, 10), word16(cr, 10));

    // 10-bit quantization noise only.
    REQUIRE_THAT(rgb[0], WithinAbs(1.0f, 2e-3f));
    REQUIRE_THAT(rgb[1], WithinAbs(0.0f, 2e-3f));
    REQUIRE_THAT(rgb[2], WithinAbs(0.0f, 2e-3f));
}

// ── How the HW path binds VideoToolbox's buffers ──────────────────────────────
// What VideoToolbox returned for the generated bars of
// plan/engineer_tools_runs/investigate-2026-11-03/render/results.txt [C1]:
// each buffer's format, its pool's software format, its plane count and its
// IOSurface planes' bytes per element ('y416' is packed: one element of 8
// bytes, no planes).

namespace {
struct VtBuffer {
    const char*   fourcc;
    const char*   clips;
    AVPixelFormat sw_format;
    std::size_t   planes, bytes_y, bytes_cbcr;
    int           bits;          // the binding: 8, 16, 0 = refused
    int           sample_bits;   // of the bound samples
};

const VtBuffer kMeasured[] = {
    { "x420", "bars_pq.mp4, bars_sdr10.mp4", AV_PIX_FMT_P010LE,   2, 2, 4, 16, 10 },
    { "420v", "bars8_hevc.mp4",              AV_PIX_FMT_NV12,     2, 1, 2,  8,  8 },
    { "x422", "bars_prores422hq.mov, bars_hevc422.mp4", AV_PIX_FMT_P210LE, 2, 2, 4, 16, 10 },
    { "sv44", "bars_prores4444.mov",         AV_PIX_FMT_P416LE,   2, 2, 4, 16, 16 },
    { "y416", "bars_prores4444a.mov",        AV_PIX_FMT_AYUV64LE, 0, 8, 0,  0,  0 },
    { "x444", "bars_hevc444.mp4",            AV_PIX_FMT_P410LE,   2, 2, 4, 16, 10 },
    { "422v", "bars_h264_422.mp4",           AV_PIX_FMT_NV16,     2, 1, 2,  8,  8 },
    { "444v", "bars_hevc444_8.mp4",          AV_PIX_FMT_NV24,     2, 1, 2,  8,  8 },
};
} // namespace

TEST_CASE("HW binding: VideoToolbox's buffers by planes and bytes per element",
          "[yuv][hw_binding]")
{
    for (const VtBuffer& b : kMeasured) {
        INFO(b.fourcc << " (" << b.clips << ")");
        CHECK(hw_container_bits(b.planes, b.bytes_y, b.bytes_cbcr) == b.bits);
        if (b.bits == 0) continue;
        CHECK(hw_sample_bits(b.sw_format, b.bits) == b.sample_bits);

        // Bound, the buffer's limited-range black and white (16 and 235 in
        // 8-bit code steps of its container) map to 0 and 1.
        const YuvMat m = make_yuv_matrix(AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, b.bits, b.sample_bits);
        const float unit = static_cast<float>(1 << (b.bits - 8));
        const float max  = static_cast<float>((1 << b.bits) - 1);
        for (float c : apply(m, 16 * unit / max, 128 * unit / max, 128 * unit / max))
            CHECK_THAT(c, WithinAbs(0.0f, 1e-5f));
        for (float c : apply(m, 235 * unit / max, 128 * unit / max, 128 * unit / max))
            CHECK_THAT(c, WithinAbs(1.0f, 1e-5f));
    }
}

TEST_CASE("HW binding: any other layout is refused", "[yuv][hw_binding]")
{
    CHECK(hw_container_bits(1, 1, 0) == 0);   // one plane
    CHECK(hw_container_bits(3, 1, 1) == 0);   // three planes (I420-like)
    CHECK(hw_container_bits(2, 1, 1) == 0);
    CHECK(hw_container_bits(2, 2, 2) == 0);
    CHECK(hw_container_bits(2, 4, 8) == 0);
    CHECK(hw_container_bits(2, 2, 1) == 0);
    // The bytes of a bound layout, but not two planes.
    CHECK(hw_container_bits(0, 1, 2) == 0);
    CHECK(hw_container_bits(1, 2, 4) == 0);
    CHECK(hw_container_bits(3, 1, 2) == 0);
}

TEST_CASE("HW binding: sample depth from the software format", "[yuv][hw_binding]")
{
    CHECK(hw_sample_bits(AV_PIX_FMT_P010LE, 16) == 10);
    CHECK(hw_sample_bits(AV_PIX_FMT_P416LE, 16) == 16);
    CHECK(hw_sample_bits(AV_PIX_FMT_NV12, 8)    == 8);
    CHECK(hw_sample_bits(AV_PIX_FMT_NONE, 16)   == 16);   // unknown: the container's
    CHECK(hw_sample_bits(AV_PIX_FMT_P010LE, 8)  == 8);    // deeper than the container
}
