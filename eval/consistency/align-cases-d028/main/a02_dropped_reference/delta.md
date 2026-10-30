# a02_dropped_reference: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- FRAME COUNT MISMATCH: Mirage scored 9 frame pairs, FFmpeg 10; only the 9 pairs both scored are compared
- PAIRING MISMATCH: 0 of Mirage's 9 pairs are not among FFmpeg's, and 1 of FFmpeg's 10 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- 1 of 9 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/align_clips/ref_drop5.mkv
- Distorted: <tmp>/align_clips/dis.mkv
- Frames: every frame pair.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 9 | 10 | **MISMATCH** |
| pooled mean (libvmaf) | 74.618239 | 69.066872 | 5.551367 |
| pooled min (libvmaf) | 72.687777 | 21.255587 | 51.432190 |
| pooled harmonic mean (libvmaf) | 74.611893 | 59.847467 | 14.764426 |
| mean of the compared frames | 74.618239 | 74.379237 | 0.239002 |
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

Pairing by timestamp (docs/DECISIONS.md D-028; eval/consistency/README.md): a pair of one side is a pair of the other when it has the same reference and distorted frames, by their positions in the inputs, and the same timestamps; the tolerance comes from the reference's frame rate in mirage_vmaf's alignment.

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) |
|---|---|---|
| rule | reference-timeline, grid: reference | framesync: each distorted frame with the reference frame at or before its time; the last frame of an input that has ended repeated |
| times from each input's start (s) | reference 0.000000, distorted 0.000000; the report's alignment, start_from container | reference 0.000000, distorted 0.000000; its log's input starts |
| scored pairs | 9 | 10 |
| pairs both scored | 9 | 9 |
| pairs only this side scored | 0 | 1 |
| pairs both scored at other timestamps | 0 | 0 |
| reference frames in no pair | 0 | 0 |
| reference frames in more than one pair | 0 | 1 |
| distorted frames in no pair | 1 | 0 |
| distorted frames in more than one pair | 0 | 0 |
| pairs whose frames are more than the tolerance, 0.016683 s, apart | 0 | 1 |
| frames off the coarser grid (reference / distorted) | 0 / 0 | - |
| distorted frames passed on unscored (no reference frame yet) | - | 0 |
| pairs a reference frame made after the distorted input ended | - | 0 |
| timestamp discontinuities | 0 found, none corrected | 0 corrected (its log) |
| libvmaf contexts | 1 | 1 |
| frames counted as failed | 1 (extra_distorted 1); failure rate 0.100000, above the threshold 0.01 | none: FFmpeg counts no failures |

Pairs only FFmpeg scored (its frame):
- 5: reference 4 (0.133000 s) with distorted 5 (0.167000 s)

Frames the two sides treat differently, Mirage's failures included (times from each input's start):

| input | frame | time (s) | Mirage | FFmpeg |
|---|---|---|---|---|
| reference | 4 | 0.133000 | paired with distorted 4 | paired with distorted 4, 5 (in 2 pairs) |
| distorted | 5 | 0.167000 | failed: extra_distorted | paired with reference 4 |

Per frame, over the 9 frame pairs both sides scored (by Mirage's pair index):
- max \|delta\|: 2.151021 (index 4)
- mean \|delta\|: 0.239002
- mean delta: 0.239002
- frames with \|delta\| > 0.01: 1 (index 4)

Largest \|delta\|:

| index | FFmpeg's frame | Mirage | FFmpeg | delta |
|---|---|---|---|---|
| 4 | 4 | 74.932232 | 72.781211 | 2.151021 |
| 2 | 2 | 74.940056 | 74.940056 | 0.000000 |
| 3 | 3 | 74.912785 | 74.912785 | 0.000000 |
| 0 | 0 | 72.687777 | 72.687777 | 0.000000 |
| 6 | 7 | 74.824743 | 74.824743 | 0.000000 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:bt709 range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_0: 640x360 yuv420p csp:bt709 range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000004
- Converts: nothing besides the size

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- bt709/tv -> bt709/tv: matrix and range unchanged; 9 pairs from index 0

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

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bicubic, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 0.000000 to 0.300000 s; pairs with different PTS: 0.

Warnings:
- mirage_vmaf: 10.00% of the frames failed to pair by timestamp (1 of 10: 1 extra distorted), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 9 frame pairs that paired
- the two sides' pairing treats 2 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
