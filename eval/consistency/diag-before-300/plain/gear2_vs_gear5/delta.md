# gear2_vs_gear5: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- 300 of 300 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear5/prog_index.m3u8
- Distorted: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear2/prog_index.m3u8
- Frames: the first 300 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 300 | 300 |  |
| pooled mean (libvmaf) | 33.832772 | 28.552935 | 5.279837 |
| pooled min (libvmaf) | 20.052322 | 16.070202 | 3.982120 |
| mean of the compared frames | 33.832772 | 28.552935 | 5.279836 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| exit status | 0 | 0 | |
| wall time, s | 4.7 | 4.5 | |

Per frame, over the 300 compared frames (index 0-299):
- max \|delta\|: 5.674685 (index 105)
- mean \|delta\|: 5.279836
- mean delta: 5.279836
- frames with \|delta\| > 0.01: 300 (index 0-299)

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 105 | 35.340352 | 29.665667 | 5.674685 |
| 45 | 35.102996 | 29.461384 | 5.641612 |
| 107 | 35.313237 | 29.679197 | 5.634040 |
| 41 | 36.180032 | 30.547382 | 5.632650 |
| 101 | 36.340508 | 30.714147 | 5.626361 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:smpte170m range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_1: 640x360 yuv420p csp:smpte170m range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000002
- Converts: Parsed_scale_1: color_space smpte170m -> bt709

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

mirage_vmaf: model vmaf_v0.6.1, scaler bilinear, n_threads 4, n_subsample 1, libvmaf package 3.1.0; reference PTS 10.000000 to 19.976644 s; pairs with different PTS: 0.

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
