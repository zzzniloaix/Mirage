# j02_yuvj_pc_vs_pc: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- 10 of 10 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/format_clips/ref_libx264-yuvj420p_bt709-pc.mkv
- Distorted: <tmp>/format_clips/dis_bt709-pc.mkv
- Frames: the first 10 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 88.790620 | 65.633871 | 23.156749 |
| pooled min (libvmaf) | 86.725987 | 63.890689 | 22.835298 |
| mean of the compared frames | 88.790620 | 65.633871 | 23.156749 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| libvmaf package version | 3.1.0 | 3.1.0 | |
| exit status | 0 | 0 | |
| wall time, s | 0.3 | 0.3 | |

Per frame, over the 10 compared frames (index 0-9):
- max \|delta\|: 23.673382 (index 9)
- mean \|delta\|: 23.156749
- mean delta: 23.156749
- frames with \|delta\| > 0.01: 10 (index 0-9)

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 9 | 89.462373 | 65.788991 | 23.673382 |
| 4 | 89.376360 | 65.827968 | 23.548392 |
| 5 | 89.313192 | 65.988740 | 23.324452 |
| 3 | 89.026774 | 65.795106 | 23.231668 |
| 8 | 88.903390 | 65.757996 | 23.145394 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:bt709 range:pc
- filter graph input 1 (reference): csp:bt709 range:pc
- Parsed_scale_1: 640x360 yuv420p csp:bt709 range:pc -> 1920x1080 yuv420p csp:bt709 range:pc flags:0x00000002
- auto_scale_0: 1920x1080 yuvj420p csp:bt709 range:pc -> 1920x1080 yuv420p csp:bt709 range:pc flags:0x00000004
- Converts: auto_scale_0: pix_fmt yuvj420p -> yuv420p

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- bt709/pc -> bt709/pc: matrix and range unchanged; 10 pairs from index 0

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuvj420p | yuv420p |
| color_space | bt709 | bt709 |
| color_range | pc | pc |
| color_primaries | None | None |
| color_trc | None | None |
| time_base | 1/1000 | 1/1000 |

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bilinear, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 0.

Warnings:
- mirage_vmaf: the reference's frames are yuvj420p, not yuv420p: FFmpeg's section 5 graph then converts the reference to the distorted input's color matrix and range, where Mirage converts the distorted frames to the reference's, so the scores can differ from FFmpeg's (docs/PHASE1_DECISIONS_PENDING.md)

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
