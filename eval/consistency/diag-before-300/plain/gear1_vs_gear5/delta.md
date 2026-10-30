# gear1_vs_gear5: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- 290 of 300 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear5/prog_index.m3u8
- Distorted: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear1/prog_index.m3u8
- Frames: the first 300 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 300 | 300 |  |
| pooled mean (libvmaf) | 1.884503 | 0.006117 | 1.878386 |
| pooled min (libvmaf) | 0 | 0.000000 | 0.000000 |
| mean of the compared frames | 1.884503 | 0.006117 | 1.878387 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| exit status | 0 | 0 | |
| wall time, s | 4.6 | 4.6 | |

Per frame, over the 300 compared frames (index 0-299):
- max \|delta\|: 3.883766 (index 161)
- mean \|delta\|: 1.878387
- mean delta: 1.878387
- frames with \|delta\| > 0.01: 290 (index 1-29, 31-59, 61-89, 91-119, 121-149, 151-179, 181-209, 211-239, 241-269, 271-299)

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 161 | 4.122470 | 0.238704 | 3.883766 |
| 101 | 3.810466 | 0.119688 | 3.690778 |
| 41 | 3.962596 | 0.291416 | 3.671180 |
| 160 | 4.122167 | 0.479576 | 3.642591 |
| 100 | 3.877631 | 0.350023 | 3.527608 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:smpte170m range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_1: 416x234 yuv420p csp:smpte170m range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000002
- Converts: Parsed_scale_1: color_space smpte170m -> bt709

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 416 |
| height | 1080 | 234 |
| pix_fmt | yuv420p | yuv420p |
| color_space | bt709 | smpte170m |
| color_range | tv | tv |
| color_primaries | bt709 | smpte170m |
| color_trc | bt709 | bt709 |
| time_base | 1/90000 | 1/90000 |

mirage_vmaf: model vmaf_v0.6.1, scaler bilinear, n_threads 4, n_subsample 1, libvmaf package 3.1.0; reference PTS 10.000000 to 19.976644 s; pairs with different PTS: 0.

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
