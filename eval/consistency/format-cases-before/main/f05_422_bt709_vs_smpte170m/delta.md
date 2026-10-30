# f05_422_bt709_vs_smpte170m: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- 10 of 10 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/format_clips/ref_ffv1-yuv422p_bt709-tv.mkv
- Distorted: <tmp>/format_clips/dis_smpte170m-tv.mkv
- Frames: the first 10 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 65.633871 | 86.170624 | -20.536753 |
| pooled min (libvmaf) | 63.890689 | 82.485574 | -18.594885 |
| mean of the compared frames | 65.633871 | 86.170624 | -20.536753 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| libvmaf package version | 3.1.0 | 3.1.0 | |
| exit status | 0 | 0 | |
| wall time, s | 0.3 | 0.2 | |

Per frame, over the 10 compared frames (index 0-9):
- max \|delta\|: 22.084801 (index 9)
- mean \|delta\|: 20.536753
- mean delta: -20.536753
- frames with \|delta\| > 0.01: 10 (index 0-9)

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 9 | 65.788991 | 87.873792 | -22.084801 |
| 8 | 65.757996 | 87.275462 | -21.517466 |
| 7 | 65.776658 | 86.988597 | -21.211939 |
| 5 | 65.988740 | 87.030759 | -21.042019 |
| 6 | 65.768707 | 86.773477 | -21.004770 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:smpte170m range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_1: 640x360 yuv420p csp:smpte170m range:tv -> 1920x1080 yuv420p csp:smpte170m range:tv flags:0x00000002
- auto_scale_0: 1920x1080 yuv422p csp:bt709 range:tv -> 1920x1080 yuv420p csp:smpte170m range:tv flags:0x00000004
- Converts: auto_scale_0: color_space bt709 -> smpte170m, auto_scale_0: pix_fmt yuv422p -> yuv420p

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- not in the report: this mirage_vmaf does not record a conversion

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuv422p | yuv420p |
| color_space | bt709 | smpte170m |
| color_range | tv | tv |
| color_primaries | None | None |
| color_trc | None | None |
| time_base | 1/1000 | 1/1000 |

mirage_vmaf: model vmaf_v0.6.1, scaler bilinear, n_threads 4, n_subsample 1, libvmaf package 3.1.0; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 0.

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
