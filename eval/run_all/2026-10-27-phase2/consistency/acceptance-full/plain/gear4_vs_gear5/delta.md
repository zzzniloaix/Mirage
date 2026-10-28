# gear4_vs_gear5: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- FRAME COUNT MISMATCH: Mirage scored 53217 frame pairs, FFmpeg 53946; only the 53191 pairs both scored are compared
- PAIRING MISMATCH: 26 of Mirage's 53217 pairs are not among FFmpeg's, and 755 of FFmpeg's 53946 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- 1 of 53191 frames have \|delta\| > 0.01

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear5/prog_index.m3u8
- Distorted: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear4/prog_index.m3u8
- Frames: every frame pair.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 53217 | 53946 | **MISMATCH** |
| pooled mean (libvmaf) | 74.562022 | 74.563207 | -0.001185 |
| pooled min (libvmaf) | 45.502224 | 69.746945 | -24.244721 |
| pooled harmonic mean (libvmaf) | 74.559377 | 74.561179 | -0.001802 |
| mean of the compared frames | 74.565083 | 74.565624 | -0.000541 |
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
| wall time, s | 1434.1 | 1285.8 | |

Pairing by timestamp (docs/DECISIONS.md D-028; eval/consistency/README.md): a pair of one side is a pair of the other when it has the same reference and distorted frames, by their positions in the inputs, and the same timestamps; the tolerance comes from the reference's frame rate in mirage_vmaf's alignment.

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) |
|---|---|---|
| rule | reference-timeline, grid: reference | framesync: each distorted frame with the reference frame at or before its time; the last frame of an input that has ended repeated |
| times from each input's start (s) | reference 10.000000, distorted 10.000000; the report's alignment, start_from container | reference 10.000000, distorted 10.000000; its log's input starts |
| scored pairs | 53217 | 53946 |
| pairs both scored | 53191 | 53191 |
| pairs only this side scored | 26 | 755 |
| pairs both scored at other timestamps | 0 | 0 |
| reference frames in no pair | 487 | 0 |
| reference frames in more than one pair | 0 | 0 |
| distorted frames in no pair | 516 | 0 |
| distorted frames in more than one pair | 0 | 0 |
| pairs whose frames are more than the tolerance, 0.016683 s, apart | 0 | 0 |
| frames off the coarser grid (reference / distorted) | 0 / 0 | - |
| distorted frames passed on unscored (no reference frame yet) | - | 0 |
| pairs a reference frame made after the distorted input ended | - | 0 |
| timestamp discontinuities | 4 found, none corrected | 0 corrected (its log) |
| libvmaf contexts | 2 | 1 |
| frames counted as failed | 1003 (unmatched_reference 63, after_discontinuity 940); failure rate 0.018499, above the threshold 0.01 | none: FFmpeg counts no failures |

Pairs only Mirage scored (its pair index):
- 53191: reference 53254 (1776.910244 s) with distorted 53191 (1776.910244 s)
- 53192: reference 53255 (1776.943611 s) with distorted 53192 (1776.943611 s)
- 53193: reference 53256 (1776.976978 s) with distorted 53193 (1776.976978 s)
- 53194: reference 53257 (1777.010344 s) with distorted 53194 (1777.010344 s)
- 53195: reference 53258 (1777.043711 s) with distorted 53195 (1777.043711 s)
- 53196: reference 53259 (1777.077078 s) with distorted 53196 (1777.077078 s)
- 53197: reference 53260 (1777.110444 s) with distorted 53197 (1777.110444 s)
- 53198: reference 53261 (1777.143811 s) with distorted 53198 (1777.143811 s)
- 53199: reference 53262 (1777.177178 s) with distorted 53199 (1777.177178 s)
- 53200: reference 53263 (1777.210544 s) with distorted 53200 (1777.210544 s)
- 53201: reference 53264 (1777.243911 s) with distorted 53201 (1777.243911 s)
- 53202: reference 53265 (1777.277278 s) with distorted 53202 (1777.277278 s)
- 53203: reference 53266 (1777.310644 s) with distorted 53203 (1777.310644 s)
- 53204: reference 53267 (1777.344011 s) with distorted 53204 (1777.344011 s)
- 53205: reference 53268 (1777.377378 s) with distorted 53205 (1777.377378 s)
- 53206: reference 53269 (1777.410744 s) with distorted 53206 (1777.410744 s)
- 53207: reference 53270 (1777.444111 s) with distorted 53207 (1777.444111 s)
- 53208: reference 53271 (1777.477478 s) with distorted 53208 (1777.477478 s)
- 53209: reference 53272 (1777.510844 s) with distorted 53209 (1777.510844 s)
- 53210: reference 53273 (1777.544211 s) with distorted 53210 (1777.544211 s)
- ... (6 more; all of them in delta.json)

Pairs only FFmpeg scored (its frame):
- 53191: reference 53191 (1774.808144 s) with distorted 53191 (1774.808144 s)
- 53192: reference 53192 (1774.841511 s) with distorted 53192 (1774.841511 s)
- 53193: reference 53193 (1774.874878 s) with distorted 53193 (1774.874878 s)
- 53194: reference 53194 (1774.908244 s) with distorted 53194 (1774.908244 s)
- 53195: reference 53195 (1774.941611 s) with distorted 53195 (1774.941611 s)
- 53196: reference 53196 (1774.974978 s) with distorted 53196 (1774.974978 s)
- 53197: reference 53197 (1775.008344 s) with distorted 53197 (1775.008344 s)
- 53198: reference 53198 (1775.041711 s) with distorted 53198 (1775.041711 s)
- 53199: reference 53199 (1775.075078 s) with distorted 53199 (1775.075078 s)
- 53200: reference 53200 (1775.108444 s) with distorted 53200 (1775.108444 s)
- 53201: reference 53201 (1775.141811 s) with distorted 53201 (1775.141811 s)
- 53202: reference 53202 (1775.175178 s) with distorted 53202 (1775.175178 s)
- 53203: reference 53203 (1775.208544 s) with distorted 53203 (1775.208544 s)
- 53204: reference 53204 (1775.241911 s) with distorted 53204 (1775.241911 s)
- 53205: reference 53205 (1775.275278 s) with distorted 53205 (1775.275278 s)
- 53206: reference 53206 (1775.308644 s) with distorted 53206 (1775.308644 s)
- 53207: reference 53207 (1775.342011 s) with distorted 53207 (1775.342011 s)
- 53208: reference 53208 (1775.375378 s) with distorted 53208 (1775.375378 s)
- 53209: reference 53209 (1775.408744 s) with distorted 53209 (1775.408744 s)
- 53210: reference 53210 (1775.442111 s) with distorted 53210 (1775.442111 s)
- ... (735 more; all of them in delta.json)

Frames the two sides treat differently, Mirage's failures included (times from each input's start):

| input | frame | time (s) | Mirage | FFmpeg |
|---|---|---|---|---|
| reference | 53191 | 1774.808144 | failed: unmatched_reference | paired with distorted 53191 |
| reference | 53192 | 1774.841511 | failed: unmatched_reference | paired with distorted 53192 |
| reference | 53193 | 1774.874878 | failed: unmatched_reference | paired with distorted 53193 |
| reference | 53194 | 1774.908244 | failed: unmatched_reference | paired with distorted 53194 |
| reference | 53195 | 1774.941611 | failed: unmatched_reference | paired with distorted 53195 |
| reference | 53196 | 1774.974978 | failed: unmatched_reference | paired with distorted 53196 |
| reference | 53197 | 1775.008344 | failed: unmatched_reference | paired with distorted 53197 |
| reference | 53198 | 1775.041711 | failed: unmatched_reference | paired with distorted 53198 |
| reference | 53199 | 1775.075078 | failed: unmatched_reference | paired with distorted 53199 |
| reference | 53200 | 1775.108444 | failed: unmatched_reference | paired with distorted 53200 |
| reference | 53201 | 1775.141811 | failed: unmatched_reference | paired with distorted 53201 |
| reference | 53202 | 1775.175178 | failed: unmatched_reference | paired with distorted 53202 |
| reference | 53203 | 1775.208544 | failed: unmatched_reference | paired with distorted 53203 |
| reference | 53204 | 1775.241911 | failed: unmatched_reference | paired with distorted 53204 |
| reference | 53205 | 1775.275278 | failed: unmatched_reference | paired with distorted 53205 |
| reference | 53206 | 1775.308644 | failed: unmatched_reference | paired with distorted 53206 |
| reference | 53207 | 1775.342011 | failed: unmatched_reference | paired with distorted 53207 |
| reference | 53208 | 1775.375378 | failed: unmatched_reference | paired with distorted 53208 |
| reference | 53209 | 1775.408744 | failed: unmatched_reference | paired with distorted 53209 |
| reference | 53210 | 1775.442111 | failed: unmatched_reference | paired with distorted 53210 |
- ... (1490 more; the first 1000 in delta.json)

Per frame, over the 53191 frame pairs both sides scored (by Mirage's pair index):
- max \|delta\|: 28.782902 (index 53190)
- mean \|delta\|: 0.000541
- mean delta: -0.000541
- frames with \|delta\| > 0.01: 1 (index 53190)

Largest \|delta\|:

| index | FFmpeg's frame | Mirage | FFmpeg | delta |
|---|---|---|---|---|
| 53190 | 53190 | 45.502224 | 74.285126 | -28.782902 |
| 38679 | 38679 | 74.687754 | 74.687754 | 0.000000 |
| 50262 | 50262 | 75.149452 | 75.149452 | 0.000000 |
| 22701 | 22701 | 74.325273 | 74.325273 | 0.000000 |
| 25486 | 25486 | 74.927071 | 74.927071 | 0.000000 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:bt709 range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_0: 1280x720 yuv420p csp:bt709 range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000002
- Converts: nothing besides the size

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- bt709/tv -> bt709/tv: matrix and range unchanged; 53217 pairs from index 0

Mirage's color handling of the reference frames (mirage_vmaf's report, ref_conversion; DECISIONS.md D-022):
- none: the reference's frames are used as they are

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 1280 |
| height | 1080 | 720 |
| pix_fmt | yuv420p | yuv420p |
| color_space | bt709 | bt709 |
| color_range | tv | tv |
| color_primaries | bt709 | bt709 |
| color_trc | bt709 | bt709 |
| time_base | 1/90000 | 1/90000 |

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bilinear, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 10.000000 to 1787.744411 s; pairs with different PTS: 0.

Warnings:
- mirage_vmaf: 1.85% of the frames failed to pair by timestamp (1003 of 54220: 63 unmatched reference, 940 after a discontinuity), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 53217 frame pairs that paired
- the two sides' pairing treats 1510 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
