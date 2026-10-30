# j01_yuvj_pc_vs_tv: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- 10 of 10 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/format_clips/ref_libx264-yuvj420p_bt709-pc.mkv
- Distorted: <tmp>/format_clips/dis_bt709-tv.mkv
- Frames: the first 10 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 100 | 88.790620 | 11.209380 |
| pooled min (libvmaf) | 100 | 86.725987 | 13.274013 |
| mean of the compared frames | 100.000000 | 88.790620 | 11.209380 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| libvmaf package version | 3.1.0 | 3.1.0 | |
| exit status | 0 | 0 | |
| wall time, s | 0.3 | 0.3 | |

Per frame, over the 10 compared frames (index 0-9):
- max \|delta\|: 13.274013 (index 0)
- mean \|delta\|: 11.209380
- mean delta: 11.209380
- frames with \|delta\| > 0.01: 10 (index 0-9)

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 0 | 100.000000 | 86.725987 | 13.274013 |
| 6 | 100.000000 | 88.644548 | 11.355452 |
| 7 | 100.000000 | 88.704108 | 11.295892 |
| 1 | 100.000000 | 88.812829 | 11.187171 |
| 8 | 100.000000 | 88.903390 | 11.096610 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:bt709 range:tv
- filter graph input 1 (reference): csp:bt709 range:pc
- Parsed_scale_1: 640x360 yuv420p csp:bt709 range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000002
- auto_scale_0: 1920x1080 yuvj420p csp:bt709 range:pc -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000004
- Converts: auto_scale_0: color_range pc -> tv, auto_scale_0: pix_fmt yuvj420p -> yuv420p

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- bt709/tv -> bt709/pc: range changed; 10 pairs from index 0

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuvj420p | yuv420p |
| color_space | bt709 | bt709 |
| color_range | pc | tv |
| color_primaries | None | None |
| color_trc | None | None |
| time_base | 1/1000 | 1/1000 |

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bilinear, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 0.

Warnings:
- mirage_vmaf: the reference's frames are yuvj420p, not yuv420p: FFmpeg's section 5 graph then converts the reference to the distorted input's color matrix and range, where Mirage converts the distorted frames to the reference's, so the scores can differ from FFmpeg's (docs/PHASE1_DECISIONS_PENDING.md)
- the distorted frames' color conversions differ: mirage_vmaf bt709/tv -> bt709/pc, FFmpeg's scale filter bt709/tv -> bt709/tv

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
