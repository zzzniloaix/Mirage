# m03_bt709_vs_untagged: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- 10 of 10 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/clips/ref_bt709-tv.mkv
- Distorted: <tmp>/clips/dis_unknown-unknown.mkv
- Frames: the first 10 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 65.633871 | 76.168071 | -10.534200 |
| pooled min (libvmaf) | 63.890689 | 72.182909 | -8.292220 |
| mean of the compared frames | 65.633871 | 76.168071 | -10.534200 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| exit status | 0 | 0 | |
| wall time, s | 0.2 | 0.2 | |

Per frame, over the 10 compared frames (index 0-9):
- max \|delta\|: 11.884643 (index 7)
- mean \|delta\|: 10.534200
- mean delta: -10.534200
- frames with \|delta\| > 0.01: 10 (index 0-9)

Largest \|delta\|:

| index | Mirage | FFmpeg | delta |
|---|---|---|---|
| 7 | 65.776658 | 77.661301 | -11.884643 |
| 8 | 65.757996 | 77.316887 | -11.558891 |
| 4 | 65.827968 | 77.105363 | -11.277395 |
| 9 | 65.788991 | 76.941829 | -11.152838 |
| 6 | 65.768707 | 76.778194 | -11.009487 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:unknown range:unknown
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_1: 640x360 yuv420p csp:unknown range:unknown -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000002
- Converts: Parsed_scale_1: color_space unknown -> bt709, Parsed_scale_1: color_range unknown -> tv

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuv420p | yuv420p |
| color_space | bt709 | None |
| color_range | tv | None |
| color_primaries | None | None |
| color_trc | None | None |
| time_base | 1/1000 | 1/1000 |

mirage_vmaf: model vmaf_v0.6.1, scaler bilinear, n_threads 4, n_subsample 1, libvmaf package 3.1.0; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 0.

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
