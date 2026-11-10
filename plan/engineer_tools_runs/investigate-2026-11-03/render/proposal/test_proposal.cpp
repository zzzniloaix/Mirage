#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "YuvMatrix.h"
#include <array>
#include <cmath>
#include <initializer_list>
using Catch::Matchers::WithinAbs;
static std::array<float, 3> app(const YuvMat& m, float y, float cb, float cr)
{
    const float dy = y - m.offset[0], du = cb - m.offset[1], dv = cr - m.offset[2];
    return { m.m[0] * dy + m.m[3] * du + m.m[6] * dv, m.m[1] * dy + m.m[4] * du + m.m[7] * dv, m.m[2] * dy + m.m[5] * du + m.m[8] * dv };
}
static float w16(int code, int bits) { return float(code << (16 - bits)) / 65535.0f; }   // MSB-aligned word, as GL_R16 reads it
TEST_CASE("P010 limited: 64 / 940 / 512 map to black, white, neutral", "[yuv][p010]")
{
    const YuvMat m = make_yuv_matrix(AVCOL_SPC_BT2020_NCL, AVCOL_RANGE_MPEG, 16, 10);
    for (float c : app(m, w16(64, 10), w16(512, 10), w16(512, 10)))  REQUIRE_THAT(c, WithinAbs(0.0f, 1e-5f));
    for (float c : app(m, w16(940, 10), w16(512, 10), w16(512, 10))) REQUIRE_THAT(c, WithinAbs(1.0f, 1e-5f));
}
TEST_CASE("P010 limited: a 10-bit code maps to (code - 64) / 876", "[yuv][p010]")
{
    const YuvMat m = make_yuv_matrix(AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, 16, 10);
    for (int code : { 100, 300, 509, 723, 1019 })
        REQUIRE_THAT(app(m, w16(code, 10), w16(512, 10), w16(512, 10))[1], WithinAbs((code - 64) / 876.0f, 1e-5f));
}
TEST_CASE("P416 ('sv44') limited: 4096 / 60160 are black / white", "[yuv][p416]")
{
    const YuvMat m = make_yuv_matrix(AVCOL_SPC_BT709, AVCOL_RANGE_MPEG, 16, 16);
    REQUIRE_THAT(app(m, 4096 / 65535.0f, 32768 / 65535.0f, 32768 / 65535.0f)[0], WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(app(m, 60160 / 65535.0f, 32768 / 65535.0f, 32768 / 65535.0f)[0], WithinAbs(1.0f, 1e-5f));
}
TEST_CASE("P010 full range: 0 / 1023 are black / white", "[yuv][p010]")
{
    const YuvMat m = make_yuv_matrix(AVCOL_SPC_BT2020_NCL, AVCOL_RANGE_JPEG, 16, 10);
    for (float c : app(m, w16(1023, 10), w16(512, 10), w16(512, 10))) REQUIRE_THAT(c, WithinAbs(1.0f, 1e-5f));
    for (float c : app(m, 0.0f, w16(512, 10), w16(512, 10)))           REQUIRE_THAT(c, WithinAbs(0.0f, 1e-5f));
}
TEST_CASE("8-bit limited: Cb = Cr = 128 is neutral, exactly", "[yuv][anchors]")
{
    const YuvMat m = make_yuv_matrix(AVCOL_SPC_BT709, AVCOL_RANGE_MPEG);
    for (int y : { 16, 126, 235 })
        for (float c : app(m, y / 255.0f, 128 / 255.0f, 128 / 255.0f)) REQUIRE_THAT(c, WithinAbs((y - 16) / 219.0f, 1e-5f));
}
TEST_CASE("P010 limited round trip: saturated BT.2020 red", "[yuv][p010][roundtrip]")
{
    // R=1,G=B=0 in BT.2020: Y'=0.2627, Cb=-0.2627/1.8814, Cr=0.5 → 10-bit limited codes, then MSB-aligned.
    const double Y = 0.2627, Cb = -0.2627 / 1.8814, Cr = 0.5;
    const int y = int(std::lround((16 + 219 * Y) * 4)), cb = int(std::lround((128 + 224 * Cb) * 4)), cr = int(std::lround((128 + 224 * Cr) * 4));
    const YuvMat m = make_yuv_matrix(AVCOL_SPC_BT2020_NCL, AVCOL_RANGE_MPEG, 16, 10);
    const auto rgb = app(m, w16(y, 10), w16(cb, 10), w16(cr, 10));
    REQUIRE_THAT(rgb[0], WithinAbs(1.0f, 2e-3f));
    REQUIRE_THAT(rgb[1], WithinAbs(0.0f, 2e-3f));
    REQUIRE_THAT(rgb[2], WithinAbs(0.0f, 2e-3f));
}
