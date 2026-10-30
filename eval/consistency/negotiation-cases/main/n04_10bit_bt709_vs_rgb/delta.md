# n04_10bit_bt709_vs_rgb: Mirage vs FFmpeg's libvmaf filter

**PASS**

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/negotiation_clips/ref_ffv1-yuv420p10le_bt709-tv.mkv
- Distorted: <tmp>/negotiation_clips/dis_ffv1-bgr0_gbr-pc.mkv
- Frames: the first 10 frame pairs.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 95.975142 | 95.975142 | 0.000000 |
| pooled min (libvmaf) | 92.195791 | 92.195791 | 0.000000 |
| pooled harmonic mean (libvmaf) | 95.948866 | 95.948866 | 0.000000 |
| mean of the compared frames | 95.975142 | 95.975142 | 0.000000 |
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
| wall time, s | 0.4 | 0.3 | |

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
- max \|delta\|: 0.000000 (index 1)
- mean \|delta\|: 0.000000
- mean delta: 0.000000
- frames with \|delta\| > 0.01: 0

Largest \|delta\|:

| index | FFmpeg's frame | Mirage | FFmpeg | delta |
|---|---|---|---|---|
| 1 | 1 | 94.774124 | 94.774124 | 0.000000 |
| 0 | 0 | 92.195791 | 92.195791 | 0.000000 |
| 5 | 5 | 96.389039 | 96.389039 | 0.000000 |
| 9 | 9 | 97.942032 | 97.942032 | 0.000000 |
| 8 | 8 | 96.987445 | 96.987445 | 0.000000 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:gbr range:pc
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_1: 640x360 bgr0 csp:gbr range:pc -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000004
- auto_scale_0: 1920x1080 yuv420p10le csp:bt709 range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000004
- Converts: Parsed_scale_1: color_space gbr -> bt709, Parsed_scale_1: color_range pc -> tv, Parsed_scale_1: pix_fmt bgr0 -> yuv420p, auto_scale_0: pix_fmt yuv420p10le -> yuv420p

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- gbr/pc -> bt709/tv: matrix and range changed; 10 pairs from index 0

Mirage's color handling of the reference frames (mirage_vmaf's report, ref_conversion; DECISIONS.md D-022):
- bt709/tv -> bt709/tv: matrix and range unchanged; 10 pairs from index 0

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuv420p10le | bgr0 |
| color_space | bt709 | gbr |
| color_range | tv | pc |
| color_primaries | None | None |
| color_trc | None | None |
| time_base | 1/1000 | 1/1000 |

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bicubic, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 0.

Warnings:
- mirage_vmaf: the distorted input's frames are bgr0, a pixel format whose conversion isn't checked against FFmpeg's section 5 graph (vmafcolor::verified), so the scores can differ from FFmpeg's

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
