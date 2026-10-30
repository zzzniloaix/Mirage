# gear2_vs_gear5: Mirage vs FFmpeg's libvmaf filter

**PASS**

- Variant: DIAGNOSTIC, not the section 5 comparison: FFmpeg with the distorted input relabeled to the reference's color_space and color_range tags where they differ (setparams before scale), so that FFmpeg does not convert them.
- Relabel inserted before FFmpeg's scale: `setparams=colorspace=bt709`
- Reference: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear5/prog_index.m3u8
- Distorted: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear2/prog_index.m3u8
- Frames: the first 300 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 300 | 300 |  |
| pooled mean (libvmaf) | 33.832772 | 33.832772 | 0.000000 |
| pooled min (libvmaf) | 20.052322 | 20.052322 | 0.000000 |
| mean of the compared frames | 33.832772 | 33.832772 | 0.000000 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| exit status | 0 | 0 | |
| wall time, s | 4.7 | 4.7 | |

Per frame, over the 300 compared frames (index 0-299):
- max \|delta\|: 0.000000 (index 59)
- mean \|delta\|: 0.000000
- mean delta: 0.000000
- frames with \|delta\| > 0.01: 0

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 59 | 33.455606 | 33.455606 | 0.000000 |
| 284 | 34.321622 | 34.321622 | 0.000000 |
| 60 | 24.237764 | 24.237764 | 0.000000 |
| 277 | 35.184687 | 35.184687 | 0.000000 |
| 20 | 33.182738 | 33.182738 | 0.000000 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:smpte170m range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_2: 640x360 yuv420p csp:bt709 range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000002
- Converts: nothing besides the size

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
