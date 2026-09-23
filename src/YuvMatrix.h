#pragma once

extern "C" {
#include <libavutil/pixfmt.h>
}

// YUV → RGB conversion matrix + offset, precomputed for the GPU zero-copy path.
//
// The GLSL shader does:
//     rgb = mat3(m) * (vec3(y, cb, cr) - offset)
//
// where (y, cb, cr) are GL-normalized samples straight out of the Y and CbCr
// planar textures ([0,1] range). offset removes the Y' black level and the 0.5
// bias on Cb/Cr; the matrix rolls in both the BT.601/709/2020 luma coefficients
// AND the limited-range scaling factors (255/219 for Y, 255/224 for Cb/Cr).
//
// mat3 is column-major to match OpenGL's `glUniformMatrix3fv(..., GL_FALSE, ...)`.
// The 8-bit vs 10-bit sample renormalization (P010's ~0.999 factor from
// MSB-alignment) is not applied — <0.1% error, invisible.
struct YuvMat {
    float m[9];
    float offset[3];
};

inline YuvMat make_yuv_matrix(AVColorSpace cs, AVColorRange range)
{
    // Luma coefficients (Kr, Kb) — Kg = 1 - Kr - Kb. BT.2020 constant-luminance
    // (CL) technically applies the matrix in a different domain than NCL
    // (post-EOTF vs pre-EOTF), so treating CL as NCL is slightly wrong for
    // CL-tagged content. Accepted trade-off: CL content is vanishingly rare in
    // the wild — nearly all HDR/BT.2020 is NCL.
    float Kr, Kb;
    switch (cs) {
        case AVCOL_SPC_BT709:                 Kr = 0.2126f; Kb = 0.0722f; break;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL:             Kr = 0.2627f; Kb = 0.0593f; break;
        case AVCOL_SPC_SMPTE170M:
        case AVCOL_SPC_BT470BG:
        default:                              Kr = 0.299f;  Kb = 0.114f;  break;
    }
    float Kg = 1.0f - Kr - Kb;

    // Inverse Y'CbCr → R'G'B' (both sides gamma-encoded), with Cb, Cr in [-0.5, 0.5]:
    //   R = Y' + 2*(1-Kr) * Cr
    //   G = Y' - 2*Kb*(1-Kb)/Kg * Cb - 2*Kr*(1-Kr)/Kg * Cr
    //   B = Y' + 2*(1-Kb) * Cb
    float R_v = 2.0f * (1.0f - Kr);
    float G_u = -2.0f * Kb * (1.0f - Kb) / Kg;
    float G_v = -2.0f * Kr * (1.0f - Kr) / Kg;
    float B_u = 2.0f * (1.0f - Kb);

    float y_scale, uv_scale, y_off;
    if (range == AVCOL_RANGE_JPEG) {
        y_scale  = 1.0f;
        uv_scale = 1.0f;
        y_off    = 0.0f;
    } else {
        // Limited range 8-bit: Y [16,235] → 219 steps, UV [16,240] → 224 steps.
        y_scale  = 255.0f / 219.0f;
        uv_scale = 255.0f / 224.0f;
        y_off    = 16.0f  / 255.0f;
    }

    YuvMat out{};
    // Column 0 (Y coefficients)
    out.m[0] = y_scale;
    out.m[1] = y_scale;
    out.m[2] = y_scale;
    // Column 1 (Cb coefficients)
    out.m[3] = 0.0f;
    out.m[4] = G_u * uv_scale;
    out.m[5] = B_u * uv_scale;
    // Column 2 (Cr coefficients)
    out.m[6] = R_v * uv_scale;
    out.m[7] = G_v * uv_scale;
    out.m[8] = 0.0f;

    out.offset[0] = y_off;
    out.offset[1] = 0.5f;
    out.offset[2] = 0.5f;
    return out;
}
