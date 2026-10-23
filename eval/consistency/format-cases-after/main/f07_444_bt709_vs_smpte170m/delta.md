# f07_444_bt709_vs_smpte170m: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- 10 of 10 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/format_clips/ref_ffv1-yuv444p_bt709-tv.mkv
- Distorted: <tmp>/format_clips/dis_smpte170m-tv.mkv
- Frames: the first 10 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 76.168071 | 84.440183 | -8.272112 |
| pooled min (libvmaf) | 72.182909 | 80.876901 | -8.693992 |
| mean of the compared frames | 76.168071 | 84.440183 | -8.272112 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| libvmaf package version | 3.1.0 | 3.1.0 | |
| exit status | 0 | 0 | |
| wall time, s | 0.3 | 0.3 | |

Per frame, over the 10 compared frames (index 0-9):
- max \|delta\|: 8.973356 (index 2)
- mean \|delta\|: 8.272112
- mean delta: -8.272112
- frames with \|delta\| > 0.01: 10 (index 0-9)

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 2 | 75.087576 | 84.060932 | -8.973356 |
| 9 | 76.941829 | 85.861059 | -8.919230 |
| 0 | 72.182909 | 80.876901 | -8.693992 |
| 5 | 76.601925 | 85.062465 | -8.460540 |
| 6 | 76.778194 | 85.217517 | -8.439323 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:smpte170m range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_1: 640x360 yuv420p csp:smpte170m range:tv -> 1920x1080 yuv420p csp:smpte170m range:tv flags:0x00000002
- auto_scale_0: 1920x1080 yuv444p csp:bt709 range:tv -> 1920x1080 yuv420p csp:smpte170m range:tv flags:0x00000004
- Converts: auto_scale_0: color_space bt709 -> smpte170m, auto_scale_0: pix_fmt yuv444p -> yuv420p

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- smpte170m/tv -> bt709/tv: matrix changed; 10 pairs from index 0

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuv444p | yuv420p |
| color_space | bt709 | smpte170m |
| color_range | tv | tv |
| color_primaries | None | None |
| color_trc | None | None |
| time_base | 1/1000 | 1/1000 |

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bilinear, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 0.

Warnings:
- mirage_vmaf: the reference's frames are yuv444p, not yuv420p: FFmpeg's section 5 graph then converts the reference to the distorted input's color matrix and range, where Mirage converts the distorted frames to the reference's, so the scores can differ from FFmpeg's (docs/PHASE1_DECISIONS_PENDING.md)
- the distorted frames' color conversions differ: mirage_vmaf smpte170m/tv -> bt709/tv, FFmpeg's scale filter smpte170m/tv -> smpte170m/tv

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
