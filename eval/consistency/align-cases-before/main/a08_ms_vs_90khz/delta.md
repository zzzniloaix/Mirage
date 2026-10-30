# a08_ms_vs_90khz: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- PAIRING MISMATCH: 4 of Mirage's 10 pairs are not among FFmpeg's, and 4 of FFmpeg's 10 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- 5 of 6 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/align_clips/ref.mkv
- Distorted: <tmp>/align_clips/dis_ts.ts
- Frames: every frame pair.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 72.079617 | 51.260499 | 20.819118 |
| pooled min (libvmaf) | 70.119479 | 20.525705 | 49.593774 |
| pooled harmonic mean (libvmaf) | 72.073403 | 37.611608 | 34.461795 |
| mean of the compared frames | 71.899102 | 70.860707 | 1.038395 |
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
| wall time, s | 0.3 | 0.2 | |

Pairing by timestamp (docs/DECISIONS.md D-028; eval/consistency/README.md): a pair of one side is a pair of the other when it has the same reference and distorted frames, by their positions in the inputs, and the same timestamps; the tolerance comes from ffprobe's r_frame_rate of the reference.

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) |
|---|---|---|
| rule | decode order (a mirage_vmaf from before D-028, whose report has no alignment) | framesync: each distorted frame with the reference frame at or before its time; the last frame of an input that has ended repeated |
| times from each input's start (s) | reference 0.000000, distorted 1.433367; FFmpeg's log, for a report without an alignment | reference 0.000000, distorted 1.433367; its log's input starts |
| scored pairs | 10 | 10 |
| pairs both scored | 6 | 6 |
| pairs only this side scored | 4 | 4 |
| pairs both scored at other timestamps | 0 | 0 |
| reference frames in no pair | - | 3 |
| reference frames in more than one pair | 0 | 3 |
| distorted frames in no pair | - | 0 |
| distorted frames in more than one pair | 0 | 0 |
| pairs whose frames are more than the tolerance, 0.016683 s, apart | 0 | 4 |
| frames off the coarser grid (reference / distorted) | - / - | - |
| distorted frames passed on unscored (no reference frame yet) | - | 0 |
| pairs a reference frame made after the distorted input ended | - | 0 |
| timestamp discontinuities | - found, none corrected | 0 corrected (its log) |
| libvmaf contexts | - | 1 |
| frames counted as failed | - (no alignment in the report) | none: FFmpeg counts no failures |

Pairs only Mirage scored (its pair index):
- 2: reference 2 (0.067000 s) with distorted 2 (0.066733 s)
- 5: reference 5 (0.167000 s) with distorted 5 (0.166833 s)
- 7: reference 7 (0.234000 s) with distorted 7 (0.233567 s)
- 8: reference 8 (0.267000 s) with distorted 8 (0.266933 s)

Pairs only FFmpeg scored (its frame):
- 2: reference 1 (0.033000 s) with distorted 2 (0.066733 s)
- 5: reference 4 (0.133000 s) with distorted 5 (0.166833 s)
- 7: reference 6 (0.200000 s) with distorted 7 (0.233567 s)
- 8: reference 7 (0.234000 s) with distorted 8 (0.266933 s)

Frames the two sides treat differently, Mirage's failures included (times from each input's start):

| input | frame | time (s) | Mirage | FFmpeg |
|---|---|---|---|---|
| reference | 1 | 0.033000 | paired with distorted 1 | paired with distorted 1, 2 (in 2 pairs) |
| reference | 2 | 0.067000 | paired with distorted 2 | in no pair |
| reference | 4 | 0.133000 | paired with distorted 4 | paired with distorted 4, 5 (in 2 pairs) |
| reference | 5 | 0.167000 | paired with distorted 5 | in no pair |
| reference | 6 | 0.200000 | paired with distorted 6 | paired with distorted 6, 7 (in 2 pairs) |
| reference | 7 | 0.234000 | paired with distorted 7 | paired with distorted 8 |
| reference | 8 | 0.267000 | paired with distorted 8 | in no pair |
| distorted | 2 | 0.066733 | paired with reference 2 | paired with reference 1 |
| distorted | 5 | 0.166833 | paired with reference 5 | paired with reference 4 |
| distorted | 7 | 0.233567 | paired with reference 7 | paired with reference 6 |
| distorted | 8 | 0.266933 | paired with reference 8 | paired with reference 7 |

Per frame, over the 6 frame pairs both sides scored (by Mirage's pair index):
- max \|delta\|: 2.131170 (index 1)
- mean \|delta\|: 1.064375
- mean delta: 1.038395
- frames with \|delta\| > 0.01: 5 (index 1, 3-4, 6, 9)

Largest \|delta\|:

| index | FFmpeg's frame | Mirage | FFmpeg | delta |
|---|---|---|---|---|
| 1 | 1 | 72.294484 | 70.163314 | 2.131170 |
| 4 | 4 | 72.384209 | 70.267518 | 2.116691 |
| 6 | 6 | 72.255868 | 70.195420 | 2.060448 |
| 9 | 9 | 72.169808 | 72.230536 | -0.060728 |
| 3 | 3 | 72.170764 | 72.187975 | -0.017211 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:bt709 range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_0: 640x360 yuv420p csp:bt709 range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000004
- Converts: nothing besides the size

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- bt709/tv -> bt709/tv: matrix and range unchanged; 10 pairs from index 0

Mirage's color handling of the reference frames (mirage_vmaf's report, ref_conversion; DECISIONS.md D-022):
- none: the reference's frames are used as they are

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 640 |
| height | 1080 | 360 |
| pix_fmt | yuv420p | yuv420p |
| color_space | bt709 | bt709 |
| color_range | tv | tv |
| color_primaries | None | None |
| color_trc | None | None |
| time_base | 1/1000 | 1/90000 |

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bicubic, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 10.

Warnings:
- the two sides' pairing treats 11 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
