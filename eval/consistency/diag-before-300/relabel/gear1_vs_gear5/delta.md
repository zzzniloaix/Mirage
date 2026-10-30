# gear1_vs_gear5: Mirage vs FFmpeg's libvmaf filter

**PASS**

- Variant: DIAGNOSTIC, not the section 5 comparison: FFmpeg with the distorted input relabeled to the reference's color_space and color_range tags where they differ (setparams before scale), so that FFmpeg does not convert them.
- Relabel inserted before FFmpeg's scale: `setparams=colorspace=bt709`
- Reference: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear5/prog_index.m3u8
- Distorted: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear1/prog_index.m3u8
- Frames: the first 300 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 300 | 300 |  |
| pooled mean (libvmaf) | 1.884503 | 1.884503 | 0.000000 |
| pooled min (libvmaf) | 0 | 0.000000 | 0.000000 |
| mean of the compared frames | 1.884503 | 1.884503 | 0.000000 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| exit status | 0 | 0 | |
| wall time, s | 4.7 | 4.7 | |

Per frame, over the 300 compared frames (index 0-299):
- max \|delta\|: 0.000000 (index 34)
- mean \|delta\|: 0.000000
- mean delta: 0.000000
- frames with \|delta\| > 0.01: 0

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 34 | 2.798991 | 2.798991 | 0.000000 |
| 104 | 2.934321 | 2.934321 | 0.000000 |
| 63 | 1.384573 | 1.384573 | 0.000000 |
| 167 | 2.786644 | 2.786644 | 0.000000 |
| 189 | 1.422140 | 1.422140 | 0.000000 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:smpte170m range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_2: 416x234 yuv420p csp:bt709 range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000002
- Converts: nothing besides the size

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
