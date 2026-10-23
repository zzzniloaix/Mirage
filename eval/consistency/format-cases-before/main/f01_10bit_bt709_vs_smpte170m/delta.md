# f01_10bit_bt709_vs_smpte170m: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- 10 of 10 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/format_clips/ref_ffv1-yuv420p10le_bt709-tv.mkv
- Distorted: <tmp>/format_clips/dis_smpte170m-tv.mkv
- Frames: the first 10 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 65.633871 | 84.836351 | -19.202480 |
| pooled min (libvmaf) | 63.890689 | 81.286138 | -17.395449 |
| mean of the compared frames | 65.633871 | 84.836351 | -19.202479 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| libvmaf package version | 3.1.0 | 3.1.0 | |
| exit status | 0 | 0 | |
| wall time, s | 0.4 | 0.3 | |

Per frame, over the 10 compared frames (index 0-9):
- max \|delta\|: 20.552759 (index 9)
- mean \|delta\|: 19.202479
- mean delta: -19.202479
- frames with \|delta\| > 0.01: 10 (index 0-9)

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 9 | 65.788991 | 86.341750 | -20.552759 |
| 8 | 65.757996 | 86.132702 | -20.374706 |
| 7 | 65.776658 | 85.717998 | -19.941340 |
| 6 | 65.768707 | 85.474709 | -19.706002 |
| 5 | 65.988740 | 85.527155 | -19.538415 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:smpte170m range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_1: 640x360 yuv420p csp:smpte170m range:tv -> 1920x1080 yuv420p csp:smpte170m range:tv flags:0x00000002
- auto_scale_0: 1920x1080 yuv420p10le csp:bt709 range:tv -> 1920x1080 yuv420p csp:smpte170m range:tv flags:0x00000004
- Converts: auto_scale_0: color_space bt709 -> smpte170m, auto_scale_0: pix_fmt yuv420p10le -> yuv420p

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- not in the report: this mirage_vmaf does not record a conversion

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuv420p10le | yuv420p |
| color_space | bt709 | smpte170m |
| color_range | tv | tv |
| color_primaries | None | None |
| color_trc | None | None |
| time_base | 1/1000 | 1/1000 |

mirage_vmaf: model vmaf_v0.6.1, scaler bilinear, n_threads 4, n_subsample 1, libvmaf package 3.1.0; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 0.

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
