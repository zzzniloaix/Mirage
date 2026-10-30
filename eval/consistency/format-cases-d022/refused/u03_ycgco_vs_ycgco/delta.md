# u03_ycgco_vs_ycgco: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- mirage_vmaf exited with status 1
- FFmpeg exited with status 178

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/format_clips/ref_ycgco-tv.mkv
- Distorted: <tmp>/format_clips/dis_ycgco-tv.mkv
- Frames: the first 10 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | - | - |  |
| pooled mean (libvmaf) | - | - | - |
| pooled min (libvmaf) | - | - | - |
| mean of the compared frames | - | - | - |
| libvmaf version (run time) | - | - | |
| libvmaf package version | - | - | |
| exit status | - | - | |
| wall time, s | - | - | |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:ycgco range:tv
- filter graph input 1 (reference): csp:ycgco range:tv
- Converts: nothing besides the size

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- not in the report: this mirage_vmaf does not record a conversion

Mirage's color handling of the reference frames (mirage_vmaf's report, ref_conversion; DECISIONS.md D-022):
- not in the report: this mirage_vmaf does not record one

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuv420p | yuv420p |
| color_space | ycgco | ycgco |
| color_range | tv | tv |
| color_primaries | None | None |
| color_trc | None | None |
| time_base | 1/1000 | 1/1000 |

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
