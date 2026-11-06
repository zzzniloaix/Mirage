#pragma once
// SCRATCH PROPOSAL (not the repo's file): make_yuv_matrix with the container's
// code values. container_bits: 8 (R8/RG8, value/255) or 16 (R16/RG16,
// value/65535); sample_bits: significant bits, MSB-aligned (only full range
// depends on it). Limited range: black 16, white 235, chroma zero 128, scaled
// by 2^(container_bits-8) — for any MSB-aligned depth.
extern "C" {
#include <libavutil/pixfmt.h>
}
struct YuvMat {
    float m[9];
    float offset[3];
};
inline YuvMat make_yuv_matrix(AVColorSpace cs, AVColorRange range, int container_bits = 8, int sample_bits = 8)
{
    double Kr, Kb;
    switch (cs) {
        case AVCOL_SPC_BT709:                 Kr = 0.2126; Kb = 0.0722; break;
        case AVCOL_SPC_BT2020_NCL:
        case AVCOL_SPC_BT2020_CL:             Kr = 0.2627; Kb = 0.0593; break;
        default:                              Kr = 0.299;  Kb = 0.114;  break;
    }
    const double Kg = 1.0 - Kr - Kb;
    const double R_v = 2.0 * (1.0 - Kr), G_u = -2.0 * Kb * (1.0 - Kb) / Kg, G_v = -2.0 * Kr * (1.0 - Kr) / Kg, B_u = 2.0 * (1.0 - Kb);
    const double maxv = double((1 << container_bits) - 1);        // 255 or 65535
    double y_scale, uv_scale, y_off, c_off;
    if (range == AVCOL_RANGE_JPEG) {
        const double white = double((1 << sample_bits) - 1) * double(1 << (container_bits - sample_bits));
        y_scale = uv_scale = maxv / white;
        y_off   = 0.0;
        c_off   = double(1 << (container_bits - 1)) / maxv;
    } else {
        const double unit = double(1 << (container_bits - 8));    // one 8-bit code step in container units
        y_scale  = maxv / (219.0 * unit);
        uv_scale = maxv / (224.0 * unit);
        y_off    = 16.0 * unit / maxv;
        c_off    = 128.0 * unit / maxv;
    }
    YuvMat out{};
    out.m[0] = out.m[1] = out.m[2] = float(y_scale);
    out.m[3] = 0.0f;               out.m[4] = float(G_u * uv_scale); out.m[5] = float(B_u * uv_scale);
    out.m[6] = float(R_v * uv_scale); out.m[7] = float(G_v * uv_scale); out.m[8] = 0.0f;
    out.offset[0] = float(y_off);
    out.offset[1] = out.offset[2] = float(c_off);
    return out;
}
