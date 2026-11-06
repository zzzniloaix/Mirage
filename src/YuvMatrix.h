#pragma once

extern "C" {
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
}

#include <cstddef>

// YUV → RGB conversion matrix + offset, precomputed for the GPU zero-copy path.
//
// The GLSL shader does:
//     rgb = mat3(m) * (vec3(y, cb, cr) - offset)
//
// where (y, cb, cr) are GL-normalized samples straight out of the Y and CbCr
// planar textures: a code c of an N-bit texel (R8 / RG8: N = 8, R16 / RG16:
// N = 16) reads as c / (2^N − 1). offset removes the Y' black level and the
// Cb/Cr zero; the matrix rolls in both the BT.601/709/2020 luma coefficients
// AND the range's scaling. Both come from the container's code values, not
// from 8-bit ones: VideoToolbox's 10-bit buffers hold code × 64 in 16-bit
// words ('x420' = P010), its 'sv44' ProRes buffers 16-bit codes, each sample
// MSB-aligned.
//
// mat3 is column-major to match OpenGL's `glUniformMatrix3fv(..., GL_FALSE, ...)`.
struct YuvMat {
    float m[9];
    float offset[3];
};

// container_bits: the texel's bits, 8 or 16 (hw_container_bits()). sample_bits:
// the significant bits of each sample, MSB-aligned in the texel; only full
// range depends on them. The defaults are an 8-bit buffer's.
inline YuvMat make_yuv_matrix(AVColorSpace cs, AVColorRange range,
                              int container_bits = 8, int sample_bits = 8)
{
    if (container_bits != 16) container_bits = 8;
    if (sample_bits < 8 || sample_bits > container_bits) sample_bits = container_bits;

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

    const float max_code = static_cast<float>((1 << container_bits) - 1);   // 255 or 65535
    float y_scale, uv_scale, y_off, c_off;
    if (range == AVCOL_RANGE_JPEG) {
        // Full range: white is the samples' largest code, 2^S − 1, shifted to
        // the top of the container; the Cb/Cr zero is 2^(N−1).
        const float white = static_cast<float>((1 << sample_bits) - 1)
                          * static_cast<float>(1 << (container_bits - sample_bits));
        y_scale  = max_code / white;
        uv_scale = max_code / white;
        y_off    = 0.0f;
        c_off    = static_cast<float>(1 << (container_bits - 1)) / max_code;
    } else {
        // Limited range: Y [16,235] → 219 steps, UV [16,240] → 224 steps, the
        // Cb/Cr zero at 128, in units of u = 2^(N−8) container codes (an
        // 8-bit code step) for samples of any depth.
        const float u = static_cast<float>(1 << (container_bits - 8));
        y_scale  = max_code / (219.0f * u);
        uv_scale = max_code / (224.0f * u);
        y_off    = 16.0f  * u / max_code;
        c_off    = 128.0f * u / max_code;
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
    out.offset[1] = c_off;
    out.offset[2] = c_off;
    return out;
}

// How the HW path binds a VideoToolbox buffer, from its plane count and its
// IOSurface planes' bytes per element (IOSurfaceGetBytesPerElementOfPlane):
// two planes of 1 and 2 bytes (Y, interleaved CbCr: '420v', '422v', '444v')
// as R8 / RG8 → 8; two planes of 2 and 4 bytes ('x420', 'x422', 'x444',
// 'sv44', ...) as R16 / RG16 → 16. Any other buffer → 0: refused, before it
// is referenced. 'y416' (ProRes 4444 with alpha) is packed, with no planes.
[[nodiscard]] inline int hw_container_bits(std::size_t planes, std::size_t bytes_y, std::size_t bytes_cbcr)
{
    if (planes != 2) return 0;
    if (bytes_y == 1 && bytes_cbcr == 2) return 8;
    if (bytes_y == 2 && bytes_cbcr == 4) return 16;
    return 0;
}

// The significant bits of a bound buffer's samples, from its software format
// (the frame's AVHWFramesContext::sw_format: P010 10, P416 16, NV12 8); the
// container's when the format says nothing.
[[nodiscard]] inline int hw_sample_bits(AVPixelFormat sw_format, int container_bits)
{
    const AVPixFmtDescriptor* d = av_pix_fmt_desc_get(sw_format);
    const int bits = d ? d->comp[0].depth : 0;
    return (bits >= 8 && bits <= container_bits) ? bits : container_bits;
}
