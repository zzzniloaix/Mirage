# a03_duplicated_timestamp: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- FRAME COUNT MISMATCH: Mirage scored 10 frame pairs, FFmpeg 11; only the 6 pairs both scored are compared
- PAIRING MISMATCH: 4 of Mirage's 10 pairs are not among FFmpeg's, and 5 of FFmpeg's 11 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- 1 of 6 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/align_clips/ref.mkv
- Distorted: <tmp>/align_clips/dis_dup5.mkv
- Frames: every frame pair.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 11 | **MISMATCH** |
| pooled mean (libvmaf) | 54.205682 | 74.307152 | -20.101470 |
| pooled min (libvmaf) | 22.973227 | 72.687777 | -49.714550 |
| pooled harmonic mean (libvmaf) | 40.403083 | 74.296232 | -33.893149 |
| mean of the compared frames | 74.545466 | 74.191378 | 0.354088 |
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
| wall time, s | 0.2 | 0.3 | |

Pairing by timestamp (docs/DECISIONS.md D-028; eval/consistency/README.md): a pair of one side is a pair of the other when it has the same reference and distorted frames, by their positions in the inputs, and the same timestamps; the tolerance comes from ffprobe's r_frame_rate of the reference.

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) |
|---|---|---|
| rule | decode order (a mirage_vmaf from before D-028, whose report has no alignment) | framesync: each distorted frame with the reference frame at or before its time; the last frame of an input that has ended repeated |
| times from each input's start (s) | reference 0.000000, distorted 0.000000; FFmpeg's log, for a report without an alignment | reference 0.000000, distorted 0.000000; its log's input starts |
| scored pairs | 10 | 11 |
| pairs both scored | 6 | 6 |
| pairs only this side scored | 4 | 5 |
| pairs both scored at other timestamps | 0 | 0 |
| reference frames in no pair | - | 0 |
| reference frames in more than one pair | 0 | 1 |
| distorted frames in no pair | - | 0 |
| distorted frames in more than one pair | 0 | 0 |
| pairs whose frames are more than the tolerance, 0.016683 s, apart | 4 | 0 |
| frames off the coarser grid (reference / distorted) | - / - | - |
| distorted frames passed on unscored (no reference frame yet) | - | 0 |
| pairs a reference frame made after the distorted input ended | - | 0 |
| timestamp discontinuities | - found, none corrected | 0 corrected (its log) |
| libvmaf contexts | - | 1 |
| frames counted as failed | - (no alignment in the report) | none: FFmpeg counts no failures |

Pairs only Mirage scored (its pair index):
- 6: reference 6 (0.200000 s) with distorted 6 (0.167000 s)
- 7: reference 7 (0.234000 s) with distorted 7 (0.200000 s)
- 8: reference 8 (0.267000 s) with distorted 8 (0.234000 s)
- 9: reference 9 (0.300000 s) with distorted 9 (0.267000 s)

Pairs only FFmpeg scored (its frame):
- 6: reference 5 (0.167000 s) with distorted 6 (0.167000 s)
- 7: reference 6 (0.200000 s) with distorted 7 (0.200000 s)
- 8: reference 7 (0.234000 s) with distorted 8 (0.234000 s)
- 9: reference 8 (0.267000 s) with distorted 9 (0.267000 s)
- 10: reference 9 (0.300000 s) with distorted 10 (0.300000 s)

Frames the two sides treat differently, Mirage's failures included (times from each input's start):

| input | frame | time (s) | Mirage | FFmpeg |
|---|---|---|---|---|
| reference | 5 | 0.167000 | paired with distorted 5 | paired with distorted 5, 6 (in 2 pairs) |
| reference | 6 | 0.200000 | paired with distorted 6 | paired with distorted 7 |
| reference | 7 | 0.234000 | paired with distorted 7 | paired with distorted 8 |
| reference | 8 | 0.267000 | paired with distorted 8 | paired with distorted 9 |
| reference | 9 | 0.300000 | paired with distorted 9 | paired with distorted 10 |
| distorted | 6 | 0.167000 | paired with reference 6 | paired with reference 5 |
| distorted | 7 | 0.200000 | paired with reference 7 | paired with reference 6 |
| distorted | 8 | 0.234000 | paired with reference 8 | paired with reference 7 |
| distorted | 9 | 0.267000 | paired with reference 9 | paired with reference 8 |
| distorted | 10 | 0.300000 | in no pair | paired with reference 9 |

Per frame, over the 6 frame pairs both sides scored (by Mirage's pair index):
- max \|delta\|: 2.124530 (index 5)
- mean \|delta\|: 0.354089
- mean delta: 0.354088
- frames with \|delta\| > 0.01: 1 (index 5)

Largest \|delta\|:

| index | FFmpeg's frame | Mirage | FFmpeg | delta |
|---|---|---|---|---|
| 5 | 5 | 75.041530 | 72.917000 | 2.124530 |
| 2 | 2 | 74.940056 | 74.940056 | 0.000000 |
| 3 | 3 | 74.912785 | 74.912785 | 0.000000 |
| 0 | 0 | 72.687777 | 72.687777 | 0.000000 |
| 4 | 4 | 74.912750 | 74.912750 | 0.000000 |

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
| time_base | 1/1000 | 1/1000 |

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bicubic, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 4.

Warnings:
- the two sides' pairing treats 10 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
