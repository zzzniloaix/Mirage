# m08_bt2020nc_vs_bt709: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- 10 of 10 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/clips/ref_bt2020nc-tv.mkv
- Distorted: <tmp>/clips/dis_bt709-tv.mkv
- Frames: the first 10 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 65.633871 | 61.983224 | 3.650647 |
| pooled min (libvmaf) | 63.890689 | 60.980126 | 2.910563 |
| mean of the compared frames | 65.633871 | 61.983224 | 3.650647 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| exit status | 0 | 0 | |
| wall time, s | 0.2 | 0.2 | |

Per frame, over the 10 compared frames (index 0-9):
- max \|delta\|: 3.971636 (index 6)
- mean \|delta\|: 3.650647
- mean delta: 3.650647
- frames with \|delta\| > 0.01: 10 (index 0-9)

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 6 | 65.768707 | 61.797071 | 3.971636 |
| 7 | 65.776658 | 61.829649 | 3.947009 |
| 8 | 65.757996 | 61.834828 | 3.923168 |
| 9 | 65.788991 | 61.929453 | 3.859538 |
| 5 | 65.988740 | 62.142368 | 3.846372 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:bt709 range:tv
- filter graph input 1 (reference): csp:bt2020nc range:tv
- Parsed_scale_1: 640x360 yuv420p csp:bt709 range:tv -> 1920x1080 yuv420p csp:bt2020nc range:tv flags:0x00000002
- Converts: Parsed_scale_1: color_space bt709 -> bt2020nc

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
