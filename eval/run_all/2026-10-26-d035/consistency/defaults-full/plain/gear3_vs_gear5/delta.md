# gear3_vs_gear5: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- FRAME COUNT MISMATCH: Mirage scored 52442 frame pairs, FFmpeg 53946; only the 52439 pairs both scored are compared
- PAIRING MISMATCH: 3 of Mirage's 52442 pairs are not among FFmpeg's, and 1507 of FFmpeg's 53946 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- 1 of 52439 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear5/prog_index.m3u8
- Distorted: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear3/prog_index.m3u8
- Frames: every frame pair.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 52442 | 53946 | **MISMATCH** |
| pooled mean (libvmaf) | 64.215437 | 64.205911 | 0.009526 |
| pooled min (libvmaf) | 25.515636 | 56.844534 | -31.328898 |
| pooled harmonic mean (libvmaf) | 64.203129 | 64.194902 | 0.008227 |
| mean of the compared frames | 64.216882 | 64.216881 | 0.000001 |
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
| wall time, s | 1583.9 | 1418.5 | |

Pairing by timestamp (docs/DECISIONS.md D-028; eval/consistency/README.md): a pair of one side is a pair of the other when it has the same reference and distorted frames, by their positions in the inputs, and the same timestamps; the tolerance comes from the reference's frame rate in mirage_vmaf's alignment.

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) |
|---|---|---|
| rule | reference-timeline, grid: reference | framesync: each distorted frame with the reference frame at or before its time; the last frame of an input that has ended repeated |
| times from each input's start (s) | reference 10.000000, distorted 10.000000; the report's alignment, start_from container | reference 10.000000, distorted 10.000000; its log's input starts |
| scored pairs | 52442 | 53946 |
| pairs both scored | 52439 | 52439 |
| pairs only this side scored | 3 | 1507 |
| pairs both scored at other timestamps | 0 | 0 |
| reference frames in no pair | 1048 | 0 |
| reference frames in more than one pair | 0 | 0 |
| distorted frames in no pair | 1056 | 0 |
| distorted frames in more than one pair | 0 | 0 |
| pairs whose frames are more than the tolerance, 0.016683 s, apart | 0 | 0 |
| frames off the coarser grid (reference / distorted) | 0 / 0 | - |
| distorted frames passed on unscored (no reference frame yet) | - | 0 |
| pairs a reference frame made after the distorted input ended | - | 0 |
| timestamp discontinuities | 2 found, none corrected | 0 corrected (its log) |
| libvmaf contexts | 3 | 1 |
| frames counted as failed | 2104 (unmatched_reference 43, extra_distorted 51, after_discontinuity 2010); failure rate 0.038573, above the threshold 0.01 | none: FFmpeg counts no failures |

Pairs only Mirage scored (its pair index):
- 52439: reference 52439 (1749.749744 s) with distorted 52440 (1749.749744 s)
- 52440: reference 52460 (1756.556556 s) with distorted 52491 (1756.556556 s)
- 52441: reference 52484 (1766.433100 s) with distorted 52492 (1766.433100 s)

Pairs only FFmpeg scored (its frame):
- 52439: reference 52439 (1749.716378 s) with distorted 52439 (1749.716378 s)
- 52440: reference 52440 (1749.749744 s) with distorted 52440 (1749.749744 s)
- 52441: reference 52441 (1749.783111 s) with distorted 52441 (1749.783111 s)
- 52442: reference 52442 (1749.816478 s) with distorted 52442 (1749.816478 s)
- 52443: reference 52443 (1749.849844 s) with distorted 52443 (1749.849844 s)
- 52444: reference 52444 (1749.883211 s) with distorted 52444 (1749.883211 s)
- 52445: reference 52445 (1749.916578 s) with distorted 52445 (1749.916578 s)
- 52446: reference 52446 (1749.949944 s) with distorted 52446 (1749.949944 s)
- 52447: reference 52447 (1749.983311 s) with distorted 52447 (1749.983311 s)
- 52448: reference 52448 (1750.016689 s) with distorted 52448 (1750.016689 s)
- 52449: reference 52449 (1750.050056 s) with distorted 52449 (1750.050056 s)
- 52450: reference 52450 (1750.083422 s) with distorted 52450 (1750.083422 s)
- 52451: reference 52451 (1750.116789 s) with distorted 52451 (1750.116789 s)
- 52452: reference 52452 (1750.150156 s) with distorted 52452 (1750.150156 s)
- 52453: reference 52453 (1750.183522 s) with distorted 52453 (1750.183522 s)
- 52454: reference 52454 (1750.216889 s) with distorted 52454 (1750.216889 s)
- 52455: reference 52455 (1750.250256 s) with distorted 52455 (1750.250256 s)
- 52456: reference 52456 (1750.283622 s) with distorted 52456 (1750.283622 s)
- 52457: reference 52457 (1750.316989 s) with distorted 52457 (1750.316989 s)
- 52458: reference 52458 (1750.350356 s) with distorted 52458 (1750.350356 s)
- ... (1487 more; the first 1000 in delta.json)

Frames the two sides treat differently, Mirage's failures included (times from each input's start):

| input | frame | time (s) | Mirage | FFmpeg |
|---|---|---|---|---|
| reference | 52439 | 1749.716378 | paired with distorted 52440 | paired with distorted 52439 |
| reference | 52440 | 1749.749744 | failed: unmatched_reference | paired with distorted 52440 |
| reference | 52441 | 1749.783111 | failed: unmatched_reference | paired with distorted 52441 |
| reference | 52442 | 1749.816478 | failed: unmatched_reference | paired with distorted 52442 |
| reference | 52443 | 1749.849844 | failed: unmatched_reference | paired with distorted 52443 |
| reference | 52444 | 1749.883211 | failed: unmatched_reference | paired with distorted 52444 |
| reference | 52445 | 1749.916578 | failed: unmatched_reference | paired with distorted 52445 |
| reference | 52446 | 1749.949944 | failed: unmatched_reference | paired with distorted 52446 |
| reference | 52447 | 1749.983311 | failed: unmatched_reference | paired with distorted 52447 |
| reference | 52448 | 1750.016689 | failed: unmatched_reference | paired with distorted 52448 |
| reference | 52449 | 1750.050056 | failed: unmatched_reference | paired with distorted 52449 |
| reference | 52450 | 1750.083422 | failed: unmatched_reference | paired with distorted 52450 |
| reference | 52451 | 1750.116789 | failed: unmatched_reference | paired with distorted 52451 |
| reference | 52452 | 1750.150156 | failed: unmatched_reference | paired with distorted 52452 |
| reference | 52453 | 1750.183522 | failed: unmatched_reference | paired with distorted 52453 |
| reference | 52454 | 1750.216889 | failed: unmatched_reference | paired with distorted 52454 |
| reference | 52455 | 1750.250256 | failed: unmatched_reference | paired with distorted 52455 |
| reference | 52456 | 1750.283622 | failed: unmatched_reference | paired with distorted 52456 |
| reference | 52457 | 1750.316989 | failed: unmatched_reference | paired with distorted 52457 |
| reference | 52458 | 1750.350356 | failed: unmatched_reference | paired with distorted 52458 |
- ... (2994 more; the first 1000 in delta.json)

Per frame, over the 52439 frame pairs both sides scored (by Mirage's pair index):
- max \|delta\|: 0.063371 (index 52438)
- mean \|delta\|: 0.000001
- mean delta: 0.000001
- frames with \|delta\| > 0.01: 1 (index 52438)

Largest \|delta\|:

| index | FFmpeg's frame | Mirage | FFmpeg | delta |
|---|---|---|---|---|
| 52438 | 52438 | 64.069528 | 64.006157 | 0.063371 |
| 39489 | 39489 | 63.686730 | 63.686730 | 0.000000 |
| 37197 | 37197 | 64.349155 | 64.349155 | 0.000000 |
| 22891 | 22891 | 63.657382 | 63.657382 | 0.000000 |
| 47103 | 47103 | 64.021790 | 64.021790 | 0.000000 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:bt709 range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_0: 960x540 yuv420p csp:bt709 range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000004
- Converts: nothing besides the size

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- bt709/tv -> bt709/tv: matrix and range unchanged; 52442 pairs from index 0

Mirage's color handling of the reference frames (mirage_vmaf's report, ref_conversion; DECISIONS.md D-022):
- none: the reference's frames are used as they are

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 960 |
| height | 1080 | 540 |
| pix_fmt | yuv420p | yuv420p |
| color_space | bt709 | bt709 |
| color_range | tv | tv |
| color_primaries | bt709 | bt709 |
| color_trc | bt709 | bt709 |
| time_base | 1/90000 | 1/90000 |

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bicubic, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 10.000000 to 1776.433100 s; pairs with different PTS: 0.

Warnings:
- mirage_vmaf: 3.86% of the frames failed to pair by timestamp (2104 of 54546: 43 unmatched reference, 51 extra distorted, 2010 after a discontinuity), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 52442 frame pairs that paired
- the two sides' pairing treats 3014 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
