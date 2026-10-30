# gear2_vs_gear5: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- mirage_vmaf exited with status -15

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear5/prog_index.m3u8
- Distorted: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear2/prog_index.m3u8
- Frames: every frame pair.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | - | - |  |
| pooled mean (libvmaf) | - | - | - |
| pooled min (libvmaf) | - | - | - |
| pooled harmonic mean (libvmaf) | - | - | - |
| mean of the compared frames | - | - | - |
| libvmaf version (run time) | - | 3.0.0 | |
| libvmaf package version | - | 3.1.0 | |
| libavutil version (run time) | - | 60.26.101 | |
| libavcodec version (run time) | - | 62.28.101 | |
| libavformat version (run time) | - | 62.12.101 | |
| libavdevice version (run time) | - | 62.3.101 | |
| libavfilter version (run time) | - | 11.14.101 | |
| libswscale version (run time) | - | 9.5.101 | |
| libswresample version (run time) | - | 6.3.101 | |
| exit status | - | 0 | |
| wall time, s | - | 772.3 | |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:smpte170m range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_0: 640x360 yuv420p csp:smpte170m range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000004
- Converts: Parsed_scale_0: color_space smpte170m -> bt709

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
| color_space | bt709 | smpte170m |
| color_range | tv | tv |
| color_primaries | bt709 | smpte170m |
| color_trc | bt709 | bt709 |
| time_base | 1/90000 | 1/90000 |

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
