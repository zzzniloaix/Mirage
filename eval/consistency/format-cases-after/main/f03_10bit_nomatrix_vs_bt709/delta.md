# f03_10bit_nomatrix_vs_bt709: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- 10 of 10 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/format_clips/ref_ffv1-yuv420p10le_unknown-tv.mkv
- Distorted: <tmp>/format_clips/dis_bt709-tv.mkv
- Frames: the first 10 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 40.078382 | 34.306556 | 5.771826 |
| pooled min (libvmaf) | 39.295176 | 33.596836 | 5.698340 |
| mean of the compared frames | 40.078382 | 34.306556 | 5.771827 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| libvmaf package version | 3.1.0 | 3.1.0 | |
| exit status | 0 | 0 | |
| wall time, s | 0.4 | 0.3 | |

Per frame, over the 10 compared frames (index 0-9):
- max \|delta\|: 7.026388 (index 0)
- mean \|delta\|: 5.771827
- mean delta: 5.771827
- frames with \|delta\| > 0.01: 10 (index 0-9)

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 0 | 40.623224 | 33.596836 | 7.026388 |
| 1 | 41.895905 | 35.419362 | 6.476543 |
| 2 | 40.807095 | 34.963967 | 5.843128 |
| 3 | 40.528598 | 34.827859 | 5.700739 |
| 9 | 39.339610 | 33.759786 | 5.579824 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:bt709 range:tv
- filter graph input 1 (reference): csp:unknown range:tv
- Parsed_scale_1: 640x360 yuv420p csp:bt709 range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000002
- auto_scale_0: 1920x1080 yuv420p10le csp:unknown range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000004
- Converts: auto_scale_0: color_space unknown -> bt709, auto_scale_0: pix_fmt yuv420p10le -> yuv420p

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- bt709/tv -> unknown/tv: matrix changed; 10 pairs from index 0

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuv420p10le | yuv420p |
| color_space | None | bt709 |
| color_range | tv | tv |
| color_primaries | None | None |
| color_trc | None | None |
| time_base | 1/1000 | 1/1000 |

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bilinear, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 0.

Warnings:
- mirage_vmaf: the reference's frames are yuv420p10le, not yuv420p: FFmpeg's section 5 graph then converts the reference to the distorted input's color matrix and range, where Mirage converts the distorted frames to the reference's, so the scores can differ from FFmpeg's (docs/PHASE1_DECISIONS_PENDING.md)
- the distorted frames' color conversions differ: mirage_vmaf bt709/tv -> unknown/tv, FFmpeg's scale filter bt709/tv -> bt709/tv

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
