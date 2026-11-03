// Finding 1, analytically: what the HW shader computes for P010 (VT 'x420')
// limited-range samples with the matrix the code builds (src/YuvMatrix.h,
// included as is), versus the exact 10-bit decode, and what PQ does to it.
//
// GPU model: GL_R16 / GL_UNSIGNED_SHORT normalises a 16-bit word v as v/65535;
// a P010 word is code << 6 (assumption, checked separately by vt_probe on a
// real VideoToolbox buffer). Shader math is float32, as in GLSL.
#include "YuvMatrix.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

static float pq_eotf(float e)   // copy of kFragHdrHw's pq_eotf (one channel)
{
    const float m1 = 2610.0f / 16384.0f, m2 = 2523.0f / 4096.0f * 128.0f;
    const float c1 = 3424.0f / 4096.0f, c2 = 2413.0f / 4096.0f * 32.0f, c3 = 2392.0f / 4096.0f * 32.0f;
    float ep  = std::pow(std::fmax(e, 0.0f), 1.0f / m2);
    float num = std::fmax(ep - c1, 0.0f);
    float den = c2 - c3 * ep;
    return 10000.0f * std::pow(num / den, 1.0f / m1);
}
static float hable(float x)
{
    const float A = 0.15f, B = 0.50f, C = 0.10f, D = 0.20f, E = 0.02f, F = 0.30f;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}
static float srgb_oetf(float x)
{
    return x < 0.0031308f ? 12.92f * x : 1.055f * std::pow(std::fmax(x, 0.0f), 1.0f / 2.4f) - 0.055f;
}
// Grey (R=G=B) through kFragHdrHw after the matrix: PQ → /1000 → Hable → (BT.2020→709 on grey) → sRGB.
static float hdr_display(float e)
{
    float lin   = pq_eotf(e) / 1000.0f;
    float toned = hable(lin) / hable(11.2f);
    // kBt2020ToBt709 row sums (grey in, grey out): 1.6605-0.5876-0.0728 etc.
    float r = std::fmin(std::fmax((1.6605f - 0.5876f - 0.0728f) * toned, 0.0f), 1.0f);
    return srgb_oetf(r);
}

static void apply(const YuvMat& m, float y, float cb, float cr, float out[3])
{
    float dy = y - m.offset[0], du = cb - m.offset[1], dv = cr - m.offset[2];
    out[0] = m.m[0] * dy + m.m[3] * du + m.m[6] * dv;
    out[1] = m.m[1] * dy + m.m[4] * du + m.m[7] * dv;
    out[2] = m.m[2] * dy + m.m[5] * du + m.m[8] * dv;
}

// Candidate fix: the same matrix, with the limited-range constants of the
// container (MSB-aligned 16-bit words: black 16·256, white 235·256, chroma
// zero 128·256, out of 65535).
static YuvMat fixed16(AVColorSpace cs)
{
    YuvMat m = make_yuv_matrix(cs, AVCOL_RANGE_JPEG);    // unscaled luma/chroma coefficients
    const double ys = 65535.0 / (219.0 * 256.0), cs_ = 65535.0 / (224.0 * 256.0);
    for (int i = 0; i < 3; ++i) m.m[i] = (float)ys;
    for (int i = 3; i < 9; ++i) m.m[i] = (float)(m.m[i] * cs_);
    m.offset[0] = (float)(16.0 * 256.0 / 65535.0);
    m.offset[1] = m.offset[2] = (float)(128.0 * 256.0 / 65535.0);
    return m;
}

int main(int argc, char** argv)
{
    if (argc > 1) {   // E' values given: PQ nits and the kFragHdr/kFragHdrHw displayed grey for each
        for (int i = 1; i < argc; ++i) {
            const float e = std::strtof(argv[i], nullptr);
            std::printf("E'=%s: PQ %.3f nits, displayed (x255) %.3f\n", argv[i], pq_eotf(e), 255.0f * hdr_display(e));
        }
        return 0;
    }
    const YuvMat cur = make_yuv_matrix(AVCOL_SPC_BT2020_NCL, AVCOL_RANGE_MPEG);
    const YuvMat fix = fixed16(AVCOL_SPC_BT2020_NCL);
    std::printf("current matrix: y_scale=%.7f y_off=%.7f c_off=%.7f uv_scale(Cr->R)=%.7f\n",
                cur.m[0], cur.offset[0], cur.offset[1], cur.m[6]);
    std::printf("fixed   matrix: y_scale=%.7f y_off=%.7f c_off=%.7f uv_scale(Cr->R)=%.7f\n\n",
                fix.m[0], fix.offset[0], fix.offset[1], fix.m[6]);

    const float c512 = (512 << 6) / 65535.0f;   // P010 neutral chroma, GL-normalised
    const int codes[] = { 64, 65, 100, 300, 509, 520, 723, 769, 940, 1019, 1023 };
    std::printf("P010 limited, BT.2020, neutral chroma (Cb=Cr=512)\n");
    std::printf("code | exact E'  | HW now E' (R)     err      | HW fixed E'  | PQ nits: exact     HW now   (ratio)   fixed\n");
    for (int c : codes) {
        const float y = (c << 6) / 65535.0f;
        float now[3], fx[3];
        apply(cur, y, c512, c512, now);
        apply(fix, y, c512, c512, fx);
        const double exact = (c - 64) / 876.0;
        const double n_ex = pq_eotf((float)exact), n_now = pq_eotf(now[0]), n_fx = pq_eotf(fx[0]);
        std::printf("%4d | %.6f | %.6f (G %.6f B %.6f) %+.6f | %.6f | %10.3f %10.3f (%.4f) %10.3f\n",
                    c, exact, now[0], now[1], now[2], now[0] - exact, fx[0], n_ex, n_now, n_now / n_ex, n_fx);
    }

    std::printf("\nDisplayed 8-bit sRGB through kFragHdrHw's chain (PQ/1000, Hable, sRGB), grey:\n");
    std::printf("code | exact      | HW now     | HW fixed\n");
    for (int c : codes) {
        const float y = (c << 6) / 65535.0f;
        float now[3], fx[3];
        apply(cur, y, c512, c512, now);
        apply(fix, y, c512, c512, fx);
        const float exact = (c - 64) / 876.0f;
        std::printf("%4d | %7.3f    | %7.3f    | %7.3f\n", c,
                    255.0f * hdr_display(exact), 255.0f * hdr_display(now[0]), 255.0f * hdr_display(fx[0]));
    }

    std::printf("\nSDR HW path (kFragSdrHw clamps, 8-bit framebuffer), P010 limited BT.709 grey:\n");
    const YuvMat cur709 = make_yuv_matrix(AVCOL_SPC_BT709, AVCOL_RANGE_MPEG);
    const YuvMat fix709 = fixed16(AVCOL_SPC_BT709);
    for (int c : { 64, 940 }) {
        const float y = (c << 6) / 65535.0f;
        float now[3], fx[3];
        apply(cur709, y, c512, c512, now);
        apply(fix709, y, c512, c512, fx);
        std::printf("  code %4d: now R=%.6f G=%.6f B=%.6f (x255 = %.2f)   fixed R=%.6f (x255 = %.2f)\n",
                    c, now[0], now[1], now[2], 255.0f * now[0], fx[0], 255.0f * fx[0]);
    }

    std::printf("\nP010 full range ('xf20'), current matrix (no limited scaling): code 1023 -> %.6f, code 0 -> %.6f\n",
                ((1023 << 6) / 65535.0f), 0.0f);

    std::printf("\n8-bit NV12 ('420v') limited BT.709 with the current offsets, grey Y codes, Cb=Cr=128:\n");
    for (int c : { 16, 126, 235 }) {
        float now[3];
        apply(cur709, c / 255.0f, 128 / 255.0f, 128 / 255.0f, now);
        const double exact = (c - 16) / 219.0;
        std::printf("  Y=%3d: exact %.6f (x255 %.2f) | now R %.6f G %.6f B %.6f (x255: %.2f %.2f %.2f)\n",
                    c, exact, 255 * exact, now[0], now[1], now[2], 255 * now[0], 255 * now[1], 255 * now[2]);
    }
    return 0;
}
