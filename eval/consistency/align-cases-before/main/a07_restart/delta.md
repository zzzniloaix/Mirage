# a07_restart: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- TIMESTAMP MISMATCH: 5 of the pairs both scored have other timestamps on the two sides

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: <tmp>/align_clips/ref_ts.ts
- Distorted: <tmp>/align_clips/dis_ts_restart5.ts
- Frames: every frame pair.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 10 | 10 |  |
| pooled mean (libvmaf) | 72.601226 | 72.601226 | 0.000000 |
| pooled min (libvmaf) | 70.623789 | 70.623789 | 0.000000 |
| pooled harmonic mean (libvmaf) | 72.594944 | 72.594944 | 0.000000 |
| mean of the compared frames | 72.601226 | 72.601226 | 0.000000 |
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

Pairing by timestamp (docs/DECISIONS.md D-028; eval/consistency/README.md): a pair of one side is a pair of the other when it has the same reference and distorted frames, by their positions in the inputs, and the same timestamps; the tolerance comes from ffprobe's r_frame_rate of the reference.

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) |
|---|---|---|
| rule | decode order (a mirage_vmaf from before D-028, whose report has no alignment) | framesync: each distorted frame with the reference frame at or before its time; the last frame of an input that has ended repeated |
| times from each input's start (s) | reference 1.433367, distorted 1.433367; FFmpeg's log, for a report without an alignment | reference 1.433367, distorted 1.433367; its log's input starts |
| scored pairs | 10 | 10 |
| pairs both scored | 10 | 10 |
| pairs only this side scored | 0 | 0 |
| pairs both scored at other timestamps | 5 | 5 |
| reference frames in no pair | - | 0 |
| reference frames in more than one pair | 0 | 0 |
| distorted frames in no pair | - | 0 |
| distorted frames in more than one pair | 0 | 0 |
| pairs whose frames are more than the tolerance, 0.016683 s, apart | 5 | 0 |
| frames off the coarser grid (reference / distorted) | - / - | - |
| distorted frames passed on unscored (no reference frame yet) | - | 0 |
| pairs a reference frame made after the distorted input ended | - | 0 |
| timestamp discontinuities | - found, none corrected | 1 corrected (its log) |
| libvmaf contexts | - | 1 |
| frames counted as failed | - (no alignment in the report) | none: FFmpeg counts no failures |

Pairs both scored at other timestamps (Mirage's pair index / FFmpeg's frame: the reference frame's and the distorted frame's times on each side):
- 5 / 5, reference 5 with distorted 5: Mirage 0.166833 / 0.000000 s, FFmpeg 0.166833 / 0.166833 s
- 6 / 6, reference 6 with distorted 6: Mirage 0.200200 / 0.033367 s, FFmpeg 0.200200 / 0.200200 s
- 7 / 7, reference 7 with distorted 7: Mirage 0.233567 / 0.066733 s, FFmpeg 0.233567 / 0.233567 s
- 8 / 8, reference 8 with distorted 8: Mirage 0.266933 / 0.100100 s, FFmpeg 0.266933 / 0.266933 s
- 9 / 9, reference 9 with distorted 9: Mirage 0.300300 / 0.133467 s, FFmpeg 0.300300 / 0.300300 s

Per frame, over the 10 frame pairs both sides scored (by Mirage's pair index):
- max \|delta\|: 0.000000 (index 5)
- mean \|delta\|: 0.000000
- mean delta: 0.000000
- frames with \|delta\| > 0.01: 0

Largest \|delta\|:

| index | FFmpeg's frame | Mirage | FFmpeg | delta |
|---|---|---|---|---|
| 5 | 5 | 73.052044 | 73.052044 | 0.000000 |
| 1 | 1 | 72.815857 | 72.815857 | 0.000000 |
| 7 | 7 | 72.567642 | 72.567642 | 0.000000 |
| 4 | 4 | 72.926359 | 72.926359 | 0.000000 |
| 2 | 2 | 72.929893 | 72.929893 | 0.000000 |

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
| time_base | 1/90000 | 1/90000 |

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bicubic, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 1.433367 to 1.733667 s; pairs with different PTS: 5.

Warnings:
- FFmpeg corrected 1 timestamp discontinuity of its inputs (its log), which mirage_vmaf doesn't (D-028)

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
