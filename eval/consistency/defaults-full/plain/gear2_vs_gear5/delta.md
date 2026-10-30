# gear2_vs_gear5: Mirage vs FFmpeg's libvmaf filter

**PASS**

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear5/prog_index.m3u8
- Distorted: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear2/prog_index.m3u8
- Frames: every frame pair.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 53946 | 53946 |  |
| pooled mean (libvmaf) | 35.811040 | 35.811040 | 0.000000 |
| pooled min (libvmaf) | 23.488692 | 23.488692 | 0.000000 |
| pooled harmonic mean (libvmaf) | 35.705189 | 35.705189 | 0.000000 |
| mean of the compared frames | 35.811040 | 35.811040 | 0.000000 |
| libvmaf version (run time) | 3.0.0 | 3.0.0 | |
| libvmaf package version | 3.1.0 | 3.1.0 | |
| libavutil version (run time) | 60.26.101 | 60.26.101 | |
| libavcodec version (run time) | 62.28.101 | 62.28.101 | |
| libavformat version (run time) | 62.12.101 | 62.12.101 | |
| libswscale version (run time) | 9.5.101 | 9.5.101 | |
| libavfilter version (run time) | 11.14.101 | 11.14.101 | |
| libavdevice version (run time) | - | 62.3.101 | |
| libswresample version (run time) | - | 6.3.101 | |
| exit status | 0 | 0 | |
| wall time, s | 1251.3 | 1253.2 | |

Pairing by timestamp (docs/DECISIONS.md D-028; eval/consistency/README.md): a pair of one side is a pair of the other when it has the same reference and distorted frames, by their positions in the inputs, and the same timestamps; the tolerance comes from the reference's frame rate in mirage_vmaf's alignment.

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) |
|---|---|---|
| rule | reference-timeline, grid: reference | framesync: each distorted frame with the reference frame at or before its time; the last frame of an input that has ended repeated |
| times from each input's start (s) | reference 10.000000, distorted 10.000000; the report's alignment, start_from container | reference 10.000000, distorted 10.000000; its log's input starts |
| scored pairs | 53946 | 53946 |
| pairs both scored | 53946 | 53946 |
| pairs only this side scored | 0 | 0 |
| pairs both scored at other timestamps | 0 | 0 |
| reference frames in no pair | 0 | 0 |
| reference frames in more than one pair | 0 | 0 |
| distorted frames in no pair | 0 | 0 |
| distorted frames in more than one pair | 0 | 0 |
| pairs whose frames are more than the tolerance, 0.016683 s, apart | 0 | 0 |
| frames off the coarser grid (reference / distorted) | 0 / 0 | - |
| distorted frames passed on unscored (no reference frame yet) | - | 0 |
| pairs a reference frame made after the distorted input ended | - | 0 |
| timestamp discontinuities | 0 found, none corrected | 0 corrected (its log) |
| libvmaf contexts | 1 | 1 |
| frames counted as failed | 0; failure rate 0, not above the threshold 0.01 | none: FFmpeg counts no failures |

Per frame, over the 53946 frame pairs both sides scored (by Mirage's pair index):
- max \|delta\|: 0.000000 (index 18961)
- mean \|delta\|: 0.000000
- mean delta: 0.000000
- frames with \|delta\| > 0.01: 0

Largest \|delta\|:

| index | FFmpeg's frame | Mirage | FFmpeg | delta |
|---|---|---|---|---|
| 18961 | 18961 | 36.942254 | 36.942254 | 0.000000 |
| 19968 | 19968 | 37.016351 | 37.016351 | 0.000000 |
| 42553 | 42553 | 35.951540 | 35.951540 | 0.000000 |
| 9484 | 9484 | 35.430510 | 35.430510 | 0.000000 |
| 53820 | 53820 | 25.944249 | 25.944249 | 0.000000 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:smpte170m range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_0: 640x360 yuv420p csp:smpte170m range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000004
- Converts: Parsed_scale_0: color_space smpte170m -> bt709

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- smpte170m/tv -> bt709/tv: matrix changed; 53946 pairs from index 0

Mirage's color handling of the reference frames (mirage_vmaf's report, ref_conversion; DECISIONS.md D-022):
- none: the reference's frames are used as they are

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

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bicubic, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 10.000000 to 1809.966633 s; pairs with different PTS: 0.

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
