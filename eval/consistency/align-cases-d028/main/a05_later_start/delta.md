# a05_later_start: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- FRAME COUNT MISMATCH: Mirage scored 7 frame pairs, FFmpeg 10; only the 7 pairs both scored are compared
- PAIRING MISMATCH: 0 of Mirage's 7 pairs are not among FFmpeg's, and 3 of FFmpeg's 10 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- 1 of 7 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/align_clips/ref.mkv
- Distorted: <tmp>/align_clips/dis_from3.mkv
- Frames: every frame pair.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 7 | 10 | **MISMATCH** |
| pooled mean (libvmaf) | 21.983465 | 27.715595 | -5.732130 |
| pooled min (libvmaf) | 20.954422 | 20.954422 | 0.000000 |
| pooled harmonic mean (libvmaf) | 21.939623 | 24.148577 | -2.208954 |
| mean of the compared frames | 21.983465 | 21.977437 | 0.006027 |
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
| wall time, s | 0.2 | 0.2 | |

Pairing by timestamp (docs/DECISIONS.md D-028; eval/consistency/README.md): a pair of one side is a pair of the other when it has the same reference and distorted frames, by their positions in the inputs, and the same timestamps; the tolerance comes from the reference's frame rate in mirage_vmaf's alignment.

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) |
|---|---|---|
| rule | reference-timeline, grid: reference | framesync: each distorted frame with the reference frame at or before its time; the last frame of an input that has ended repeated |
| times from each input's start (s) | reference 0.000000, distorted 0.100000; the report's alignment, start_from container | reference 0.000000, distorted 0.100000; its log's input starts |
| scored pairs | 7 | 10 |
| pairs both scored | 7 | 7 |
| pairs only this side scored | 0 | 3 |
| pairs both scored at other timestamps | 0 | 0 |
| reference frames in no pair | 3 | 0 |
| reference frames in more than one pair | 0 | 0 |
| distorted frames in no pair | 0 | 0 |
| distorted frames in more than one pair | 0 | 1 |
| pairs whose frames are more than the tolerance, 0.016683 s, apart | 0 | 3 |
| frames off the coarser grid (reference / distorted) | 0 / 0 | - |
| distorted frames passed on unscored (no reference frame yet) | - | 0 |
| pairs a reference frame made after the distorted input ended | - | 3 |
| timestamp discontinuities | 0 found, none corrected | 0 corrected (its log) |
| libvmaf contexts | 1 | 1 |
| frames counted as failed | 3 (unmatched_reference 3); failure rate 0.300000, above the threshold 0.01 | none: FFmpeg counts no failures |

Pairs only FFmpeg scored (its frame):
- 7: reference 7 (0.234000 s) with distorted 6 (0.200000 s), made by the reference frame after the distorted input ended
- 8: reference 8 (0.267000 s) with distorted 6 (0.200000 s), made by the reference frame after the distorted input ended
- 9: reference 9 (0.300000 s) with distorted 6 (0.200000 s), made by the reference frame after the distorted input ended

Frames the two sides treat differently, Mirage's failures included (times from each input's start):

| input | frame | time (s) | Mirage | FFmpeg |
|---|---|---|---|---|
| reference | 7 | 0.234000 | failed: unmatched_reference | paired with distorted 6 |
| reference | 8 | 0.267000 | failed: unmatched_reference | paired with distorted 6 |
| reference | 9 | 0.300000 | failed: unmatched_reference | paired with distorted 6 |
| distorted | 6 | 0.200000 | paired with reference 6 | paired with reference 6, 7, 8, 9 (in 4 pairs) |

Per frame, over the 7 frame pairs both sides scored (by Mirage's pair index):
- max \|delta\|: 0.042191 (index 6)
- mean \|delta\|: 0.006028
- mean delta: 0.006027
- frames with \|delta\| > 0.01: 1 (index 6)

Largest \|delta\|:

| index | FFmpeg's frame | Mirage | FFmpeg | delta |
|---|---|---|---|---|
| 6 | 6 | 21.515361 | 21.473170 | 0.042191 |
| 2 | 2 | 22.301316 | 22.301316 | 0.000000 |
| 3 | 3 | 20.970506 | 20.970506 | 0.000000 |
| 1 | 1 | 23.246672 | 23.246672 | 0.000000 |
| 5 | 5 | 21.268703 | 21.268703 | 0.000000 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:bt709 range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_0: 640x360 yuv420p csp:bt709 range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000004
- Converts: nothing besides the size

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- bt709/tv -> bt709/tv: matrix and range unchanged; 7 pairs from index 0

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

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bicubic, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 0.000000 to 0.200000 s; pairs with different PTS: 7.

Warnings:
- mirage_vmaf: 30.00% of the frames failed to pair by timestamp (3 of 10: 3 unmatched reference), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 7 frame pairs that paired
- the two sides' pairing treats 4 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
