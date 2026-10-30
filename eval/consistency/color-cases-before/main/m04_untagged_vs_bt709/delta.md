# m04_untagged_vs_bt709: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- 10 of 10 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/clips/ref_unknown-unknown.mkv
- Distorted: <tmp>/clips/dis_bt709-tv.mkv
- Frames: the first 10 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 65.633871 | 40.078382 | 25.555489 |
| pooled min (libvmaf) | 63.890689 | 39.295176 | 24.595513 |
| mean of the compared frames | 65.633871 | 40.078382 | 25.555489 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| exit status | 0 | 0 | |
| wall time, s | 0.2 | 0.2 | |

Per frame, over the 10 compared frames (index 0-9):
- max \|delta\|: 26.462820 (index 8)
- mean \|delta\|: 25.555489
- mean delta: 25.555489
- frames with \|delta\| > 0.01: 10 (index 0-9)

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 8 | 65.757996 | 39.295176 | 26.462820 |
| 9 | 65.788991 | 39.339610 | 26.449381 |
| 6 | 65.768707 | 39.346998 | 26.421709 |
| 7 | 65.776658 | 39.358093 | 26.418565 |
| 5 | 65.988740 | 39.706218 | 26.282522 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:bt709 range:tv
- filter graph input 1 (reference): csp:unknown range:unknown
- Parsed_scale_1: 640x360 yuv420p csp:bt709 range:tv -> 1920x1080 yuv420p csp:unknown range:tv flags:0x00000002
- Converts: Parsed_scale_1: color_space bt709 -> unknown

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuv420p | yuv420p |
| color_space | None | bt709 |
| color_range | None | tv |
| color_primaries | None | None |
| color_trc | None | None |
| time_base | 1/1000 | 1/1000 |

mirage_vmaf: model vmaf_v0.6.1, scaler bilinear, n_threads 4, n_subsample 1, libvmaf package 3.1.0; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 0.

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
