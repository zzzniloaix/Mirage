# gear2_vs_gear5: Mirage vs FFmpeg's libvmaf filter

**PASS**

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear5/prog_index.m3u8
- Distorted: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear2/prog_index.m3u8
- Frames: the first 300 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 300 | 300 |  |
| pooled mean (libvmaf) | 19.498402 | 19.498402 | 0.000000 |
| pooled min (libvmaf) | 13.478223 | 13.478223 | 0.000000 |
| mean of the compared frames | 19.498402 | 19.498402 | 0.000000 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| libvmaf package version | 3.1.0 | 3.1.0 | |
| exit status | 0 | 0 | |
| wall time, s | 5.0 | 5.0 | |

Per frame, over the 300 compared frames (index 0-299):
- max \|delta\|: 0.000000 (index 273)
- mean \|delta\|: 0.000000
- mean delta: 0.000000
- frames with \|delta\| > 0.01: 0

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 273 | 19.866989 | 19.866989 | 0.000000 |
| 110 | 19.602041 | 19.602041 | 0.000000 |
| 233 | 19.875157 | 19.875157 | 0.000000 |
| 2 | 19.611525 | 19.611525 | 0.000000 |
| 230 | 19.666495 | 19.666495 | 0.000000 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:smpte170m range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_1: 640x360 yuv420p csp:smpte170m range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000002
- Converts: Parsed_scale_1: color_space smpte170m -> bt709

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- smpte170m/tv -> bt709/tv: matrix changed; 300 pairs from index 0

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuv420p | yuv420p |
| color_space | bt709 | smpte170m |
| color_range | tv | tv |
| color_primaries | bt709 | smpte170m |
| color_trc | bt709 | bt709 |
| time_base | 1/90000 | 1/90000 |

mirage_vmaf: model vmaf_v0.6.1neg (built-in), scaler bilinear, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 10.000000 to 19.976644 s; pairs with different PTS: 0.

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
