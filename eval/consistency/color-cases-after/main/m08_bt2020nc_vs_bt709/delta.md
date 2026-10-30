# m08_bt2020nc_vs_bt709: Mirage vs FFmpeg's libvmaf filter

**PASS**

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/clips/ref_bt2020nc-tv.mkv
- Distorted: <tmp>/clips/dis_bt709-tv.mkv
- Frames: the first 10 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 61.983224 | 61.983224 | 0.000000 |
| pooled min (libvmaf) | 60.980126 | 60.980126 | 0.000000 |
| mean of the compared frames | 61.983224 | 61.983224 | 0.000000 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| exit status | 0 | 0 | |
| wall time, s | 0.2 | 0.2 | |

Per frame, over the 10 compared frames (index 0-9):
- max \|delta\|: 0.000000 (index 7)
- mean \|delta\|: 0.000000
- mean delta: 0.000000
- frames with \|delta\| > 0.01: 0

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 7 | 61.829649 | 61.829649 | 0.000000 |
| 2 | 62.302222 | 62.302222 | 0.000000 |
| 9 | 61.929453 | 61.929453 | 0.000000 |
| 1 | 62.766654 | 62.766654 | 0.000000 |
| 3 | 62.192128 | 62.192128 | 0.000000 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:bt709 range:tv
- filter graph input 1 (reference): csp:bt2020nc range:tv
- Parsed_scale_1: 640x360 yuv420p csp:bt709 range:tv -> 1920x1080 yuv420p csp:bt2020nc range:tv flags:0x00000002
- Converts: Parsed_scale_1: color_space bt709 -> bt2020nc

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- bt709/tv -> bt2020nc/tv: matrix changed; 10 pairs from index 0

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuv420p | yuv420p |
| color_space | bt2020nc | bt709 |
| color_range | tv | tv |
| color_primaries | None | None |
| color_trc | None | None |
| time_base | 1/1000 | 1/1000 |

mirage_vmaf: model vmaf_v0.6.1, scaler bilinear, n_threads 4, n_subsample 1, libvmaf package 3.1.0; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 0.

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
