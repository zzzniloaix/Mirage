# f02_10bit_smpte170m_vs_bt709: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- 10 of 10 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/format_clips/ref_ffv1-yuv420p10le_smpte170m-tv.mkv
- Distorted: <tmp>/format_clips/dis_bt709-tv.mkv
- Frames: the first 10 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 65.633871 | 34.306556 | 31.327315 |
| pooled min (libvmaf) | 63.890689 | 33.596836 | 30.293853 |
| mean of the compared frames | 65.633871 | 34.306556 | 31.327315 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| libvmaf package version | 3.1.0 | 3.1.0 | |
| exit status | 0 | 0 | |
| wall time, s | 0.4 | 0.3 | |

Per frame, over the 10 compared frames (index 0-9):
- max \|delta\|: 32.029205 (index 9)
- mean \|delta\|: 31.327315
- mean delta: 31.327315
- frames with \|delta\| > 0.01: 10 (index 0-9)

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 9 | 65.788991 | 33.759786 | 32.029205 |
| 8 | 65.757996 | 33.875761 | 31.882235 |
| 6 | 65.768707 | 33.937661 | 31.831046 |
| 5 | 65.988740 | 34.188404 | 31.800336 |
| 7 | 65.776658 | 34.121643 | 31.655015 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:bt709 range:tv
- filter graph input 1 (reference): csp:smpte170m range:tv
- Parsed_scale_1: 640x360 yuv420p csp:bt709 range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000002
- auto_scale_0: 1920x1080 yuv420p10le csp:smpte170m range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000004
- Converts: auto_scale_0: color_space smpte170m -> bt709, auto_scale_0: pix_fmt yuv420p10le -> yuv420p

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- not in the report: this mirage_vmaf does not record a conversion

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuv420p10le | yuv420p |
| color_space | smpte170m | bt709 |
| color_range | tv | tv |
| color_primaries | None | None |
| color_trc | None | None |
| time_base | 1/1000 | 1/1000 |

mirage_vmaf: model vmaf_v0.6.1, scaler bilinear, n_threads 4, n_subsample 1, libvmaf package 3.1.0; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 0.

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
