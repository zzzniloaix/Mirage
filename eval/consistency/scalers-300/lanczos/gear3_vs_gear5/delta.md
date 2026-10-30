# gear3_vs_gear5: Mirage vs FFmpeg's libvmaf filter

**PASS**

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear5/prog_index.m3u8
- Distorted: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear3/prog_index.m3u8
- Frames: the first 300 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 300 | 300 |  |
| pooled mean (libvmaf) | 67.063929 | 67.063929 | 0.000000 |
| pooled min (libvmaf) | 59.048892 | 59.048892 | 0.000000 |
| mean of the compared frames | 67.063929 | 67.063929 | 0.000000 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| libvmaf package version | 3.1.0 | 3.1.0 | |
| exit status | 0 | 0 | |
| wall time, s | 4.8 | 4.7 | |

Per frame, over the 300 compared frames (index 0-299):
- max \|delta\|: 0.000000 (index 33)
- mean \|delta\|: 0.000000
- mean delta: 0.000000
- frames with \|delta\| > 0.01: 0

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 33 | 67.945333 | 67.945333 | 0.000000 |
| 124 | 65.922263 | 65.922263 | 0.000000 |
| 237 | 67.532223 | 67.532223 | 0.000000 |
| 228 | 67.298024 | 67.298024 | 0.000000 |
| 262 | 65.995472 | 65.995472 | 0.000000 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:bt709 range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_1: 960x540 yuv420p csp:bt709 range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000200
- Converts: nothing besides the size

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- bt709/tv -> bt709/tv: matrix and range unchanged; 300 pairs from index 0

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 960 |
| height | 1080 | 540 |
| pix_fmt | yuv420p | yuv420p |
| color_space | bt709 | bt709 |
| color_range | tv | tv |
| color_primaries | bt709 | bt709 |
| color_trc | bt709 | bt709 |
| time_base | 1/90000 | 1/90000 |

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler lanczos, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 10.000000 to 19.976644 s; pairs with different PTS: 0.

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
