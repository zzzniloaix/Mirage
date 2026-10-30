# d01_bt709_vs_10bit_smpte170m: Mirage vs FFmpeg's libvmaf filter

**PASS**

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/format_clips/ref_bt709-tv.mkv
- Distorted: <tmp>/format_clips/dis_ffv1-yuv420p10le_smpte170m-tv.mkv
- Frames: the first 10 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 75.682132 | 75.682132 | 0.000000 |
| pooled min (libvmaf) | 72.457011 | 72.457011 | 0.000000 |
| mean of the compared frames | 75.682132 | 75.682132 | 0.000000 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| libvmaf package version | 3.1.0 | 3.1.0 | |
| exit status | 0 | 0 | |
| wall time, s | 0.3 | 0.3 | |

Per frame, over the 10 compared frames (index 0-9):
- max \|delta\|: 0.000000 (index 4)
- mean \|delta\|: 0.000000
- mean delta: 0.000000
- frames with \|delta\| > 0.01: 0

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 4 | 76.407120 | 76.407120 | 0.000000 |
| 8 | 76.689511 | 76.689511 | 0.000000 |
| 1 | 74.783382 | 74.783382 | 0.000000 |
| 3 | 74.954278 | 74.954278 | 0.000000 |
| 2 | 74.627709 | 74.627709 | 0.000000 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:smpte170m range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_1: 640x360 yuv420p10le csp:smpte170m range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000002
- Converts: Parsed_scale_1: color_space smpte170m -> bt709, Parsed_scale_1: pix_fmt yuv420p10le -> yuv420p

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- smpte170m/tv -> bt709/tv: matrix changed; 10 pairs from index 0

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuv420p | yuv420p10le |
| color_space | bt709 | smpte170m |
| color_range | tv | tv |
| color_primaries | None | None |
| color_trc | None | None |
| time_base | 1/1000 | 1/1000 |

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bilinear, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 0.

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
