# m02_bt709_vs_bt709: Mirage vs FFmpeg's libvmaf filter

**PASS**

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/clips/ref_bt709-tv.mkv
- Distorted: <tmp>/clips/dis_bt709-tv.mkv
- Frames: the first 10 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 65.633871 | 65.633871 | 0.000000 |
| pooled min (libvmaf) | 63.890689 | 63.890689 | 0.000000 |
| mean of the compared frames | 65.633871 | 65.633871 | 0.000000 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| exit status | 0 | 0 | |
| wall time, s | 0.2 | 0.2 | |

Per frame, over the 10 compared frames (index 0-9):
- max \|delta\|: 0.000000 (index 8)
- mean \|delta\|: 0.000000
- mean delta: 0.000000
- frames with \|delta\| > 0.01: 0

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 8 | 65.757996 | 65.757996 | 0.000000 |
| 6 | 65.768707 | 65.768707 | 0.000000 |
| 9 | 65.788991 | 65.788991 | 0.000000 |
| 4 | 65.827968 | 65.827968 | 0.000000 |
| 3 | 65.795106 | 65.795106 | 0.000000 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:bt709 range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_1: 640x360 yuv420p csp:bt709 range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000002
- Converts: nothing besides the size

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- bt709/tv -> bt709/tv: matrix and range unchanged; 10 pairs from index 0

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuv420p | yuv420p |
| color_space | bt709 | bt709 |
| color_range | tv | tv |
| color_primaries | None | None |
| color_trc | None | None |
| time_base | 1/1000 | 1/1000 |

mirage_vmaf: model vmaf_v0.6.1, scaler bilinear, n_threads 4, n_subsample 1, libvmaf package 3.1.0; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 0.

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
