# n01_yuvj_pc_vs_norange: Mirage vs FFmpeg's libvmaf filter

**PASS**

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/negotiation_clips/ref_libx264-yuvj420p_bt709-pc.mkv
- Distorted: <tmp>/negotiation_clips/dis_bt709-unknown.mkv
- Frames: the first 10 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 98.122911 | 98.122911 | 0.000000 |
| pooled min (libvmaf) | 95.726103 | 95.726103 | 0.000000 |
| pooled harmonic mean (libvmaf) | 98.116144 | 98.116144 | 0.000000 |
| mean of the compared frames | 98.122911 | 98.122912 | 0.000000 |
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
| wall time, s | 0.3 | 0.3 | |

Pairing by timestamp (docs/DECISIONS.md D-028; eval/consistency/README.md): a pair of one side is a pair of the other when it has the same reference and distorted frames, by their positions in the inputs, and the same timestamps; the tolerance comes from the reference's frame rate in mirage_vmaf's alignment.

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) |
|---|---|---|
| rule | reference-timeline, grid: reference | framesync: each distorted frame with the reference frame at or before its time; the last frame of an input that has ended repeated |
| times from each input's start (s) | reference 0.000000, distorted 0.000000; the report's alignment, start_from container | reference 0.000000, distorted 0.000000; its log's input starts |
| scored pairs | 10 | 10 |
| pairs both scored | 10 | 10 |
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

Per frame, over the 10 frame pairs both sides scored (by Mirage's pair index):
- max \|delta\|: 0.000000 (index 6)
- mean \|delta\|: 0.000000
- mean delta: 0.000000
- frames with \|delta\| > 0.01: 0

Largest \|delta\|:

| index | FFmpeg's frame | Mirage | FFmpeg | delta |
|---|---|---|---|---|
| 6 | 6 | 98.146221 | 98.146221 | 0.000000 |
| 7 | 7 | 98.455760 | 98.455760 | 0.000000 |
| 2 | 2 | 98.256284 | 98.256284 | 0.000000 |
| 5 | 5 | 98.566345 | 98.566345 | 0.000000 |
| 1 | 1 | 98.250334 | 98.250334 | 0.000000 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:bt709 range:unknown
- filter graph input 1 (reference): csp:bt709 range:pc
- Parsed_scale_1: 640x360 yuv420p csp:bt709 range:unknown -> 1920x1080 yuv420p csp:bt709 range:pc flags:0x00000004
- auto_scale_0: 1920x1080 yuvj420p csp:bt709 range:pc -> 1920x1080 yuv420p csp:bt709 range:pc flags:0x00000004
- Converts: Parsed_scale_1: color_range unknown -> pc, auto_scale_0: pix_fmt yuvj420p -> yuv420p

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- bt709/unknown -> bt709/pc: range changed; 10 pairs from index 0

Mirage's color handling of the reference frames (mirage_vmaf's report, ref_conversion; DECISIONS.md D-022):
- bt709/pc -> bt709/pc: matrix and range unchanged; 10 pairs from index 0

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuvj420p | yuv420p |
| color_space | bt709 | bt709 |
| color_range | pc | None |
| color_primaries | None | None |
| color_trc | None | None |
| time_base | 1/1000 | 1/1000 |

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bicubic, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 0.

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
