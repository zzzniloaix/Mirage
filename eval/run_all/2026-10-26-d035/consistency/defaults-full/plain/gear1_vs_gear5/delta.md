# gear1_vs_gear5: Mirage vs FFmpeg's libvmaf filter

**FAIL**:
- FRAME COUNT MISMATCH: Mirage scored 15639 frame pairs, FFmpeg 52184; only the 2462 pairs both scored are compared
- PAIRING MISMATCH: 13177 of Mirage's 15639 pairs are not among FFmpeg's, and 49722 of FFmpeg's 52184 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- TIMESTAMP MISMATCH: 1 of the pairs both scored have other timestamps on the two sides

- Variant: plain FFmpeg: the section 5 comparison.
- Reference: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear5/prog_index.m3u8
- Distorted: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear1/prog_index.m3u8
- Frames: every frame pair.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019).

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) | Mirage - FFmpeg |
|---|---|---|---|
| frame pairs scored | 15639 | 52184 | **MISMATCH** |
| pooled mean (libvmaf) | 6.044270 | 1.534324 | 4.509946 |
| pooled min (libvmaf) | 0 | 0.000000 | 0.000000 |
| pooled harmonic mean (libvmaf) | 4.782745 | 0.271656 | 4.511089 |
| mean of the compared frames | 6.167442 | 6.167442 | 0.000000 |
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
| wall time, s | 439.2 | 936.3 | |

Pairing by timestamp (docs/DECISIONS.md D-028; eval/consistency/README.md): a pair of one side is a pair of the other when it has the same reference and distorted frames, by their positions in the inputs, and the same timestamps; the tolerance comes from the reference's frame rate in mirage_vmaf's alignment.

| | Mirage (mirage_vmaf) | FFmpeg (libvmaf filter) |
|---|---|---|
| rule | reference-timeline, grid: reference | framesync: each distorted frame with the reference frame at or before its time; the last frame of an input that has ended repeated |
| times from each input's start (s) | reference 10.000000, distorted 10.000000; the report's alignment, start_from container | reference 10.000000, distorted 10.000000; its log's input starts |
| scored pairs | 15639 | 52184 |
| pairs both scored | 2462 | 2462 |
| pairs only this side scored | 13177 | 49722 |
| pairs both scored at other timestamps | 1 | 1 |
| reference frames in no pair | 17745 | 41 |
| reference frames in more than one pair | 0 | 14 |
| distorted frames in no pair | 1537 | 0 |
| distorted frames in more than one pair | 0 | 0 |
| pairs whose frames are more than the tolerance, 0.016683 s, apart | 0 | 39027 |
| frames off the coarser grid (reference / distorted) | 0 / 0 | - |
| distorted frames passed on unscored (no reference frame yet) | - | 0 |
| pairs a reference frame made after the distorted input ended | - | 0 |
| timestamp discontinuities | 7 found, none corrected | 0 corrected (its log) |
| libvmaf contexts | 2 | 1 |
| frames counted as failed | 19282 (unmatched_reference 1, extra_distorted 941, after_discontinuity 18340); failure rate 0.552161, above the threshold 0.01 | none: FFmpeg counts no failures |

Pairs only Mirage scored (its pair index):
- 2461: reference 2461 (82.148811 s) with distorted 2462 (82.148811 s)
- 2462: reference 2462 (90.090089 s) with distorted 2683 (90.090089 s)
- 2463: reference 2463 (90.123456 s) with distorted 2684 (90.123456 s)
- 2464: reference 2464 (90.156822 s) with distorted 2685 (90.156822 s)
- 2465: reference 2465 (90.190189 s) with distorted 2686 (90.190189 s)
- 2466: reference 2466 (90.223556 s) with distorted 2687 (90.223556 s)
- 2467: reference 2467 (90.256922 s) with distorted 2688 (90.256922 s)
- 2468: reference 2468 (90.290289 s) with distorted 2689 (90.290289 s)
- 2469: reference 2469 (90.323656 s) with distorted 2690 (90.323656 s)
- 2470: reference 2470 (90.357022 s) with distorted 2691 (90.357022 s)
- 2471: reference 2471 (90.390389 s) with distorted 2692 (90.390389 s)
- 2472: reference 2472 (90.423756 s) with distorted 2693 (90.423756 s)
- 2473: reference 2473 (90.457122 s) with distorted 2694 (90.457122 s)
- 2474: reference 2474 (90.490489 s) with distorted 2695 (90.490489 s)
- 2475: reference 2475 (90.523856 s) with distorted 2696 (90.523856 s)
- 2476: reference 2476 (90.557222 s) with distorted 2697 (90.557222 s)
- 2477: reference 2477 (90.590589 s) with distorted 2698 (90.590589 s)
- 2478: reference 2478 (90.623956 s) with distorted 2699 (90.623956 s)
- 2479: reference 2479 (90.690689 s) with distorted 2701 (90.690689 s)
- 2480: reference 2480 (99.733067 s) with distorted 2972 (99.733067 s)
- ... (13157 more; the first 1000 in delta.json)

Pairs only FFmpeg scored (its frame):
- 2461: reference 2461 (82.115444 s) with distorted 2461 (82.115444 s)
- 2462: reference 2462 (82.148811 s) with distorted 2462 (82.148811 s)
- 2463: reference 2463 (82.182178 s) with distorted 2463 (82.182178 s)
- 2464: reference 2464 (82.215544 s) with distorted 2464 (82.215544 s)
- 2465: reference 2465 (82.248911 s) with distorted 2465 (82.248911 s)
- 2466: reference 2466 (82.282278 s) with distorted 2466 (82.282278 s)
- 2467: reference 2467 (82.315644 s) with distorted 2467 (82.315644 s)
- 2468: reference 2468 (82.349011 s) with distorted 2468 (82.349011 s)
- 2469: reference 2469 (82.382378 s) with distorted 2469 (82.382378 s)
- 2470: reference 2470 (82.415744 s) with distorted 2470 (82.415744 s)
- 2471: reference 2471 (82.449111 s) with distorted 2471 (82.449111 s)
- 2472: reference 2472 (82.482478 s) with distorted 2472 (82.482478 s)
- 2473: reference 2473 (82.515844 s) with distorted 2473 (82.515844 s)
- 2474: reference 2474 (82.549211 s) with distorted 2474 (82.549211 s)
- 2475: reference 2475 (82.582578 s) with distorted 2475 (82.582578 s)
- 2476: reference 2476 (82.615944 s) with distorted 2476 (82.615944 s)
- 2477: reference 2477 (82.649311 s) with distorted 2477 (82.649311 s)
- 2478: reference 2478 (82.682678 s) with distorted 2478 (82.682678 s)
- 2479: reference 2479 (82.716044 s) with distorted 2479 (82.716044 s)
- 2480: reference 2480 (82.749411 s) with distorted 2480 (82.749411 s)
- ... (49702 more; the first 1000 in delta.json)

Pairs both scored at other timestamps (Mirage's pair index / FFmpeg's frame: the reference frame's and the distorted frame's times on each side):
- 12060 / 12553, reference 12060 with distorted 12553: Mirage 419.419422 / 419.419422 s, FFmpeg 420.353689 / 423.823822 s

Frames the two sides treat differently, Mirage's failures included (times from each input's start):

| input | frame | time (s) | Mirage | FFmpeg |
|---|---|---|---|---|
| reference | 2461 | 82.115444 | paired with distorted 2462 | paired with distorted 2461 |
| reference | 2462 | 82.148811 | paired with distorted 2683 | paired with distorted 2462 |
| reference | 2463 | 82.182178 | paired with distorted 2684 | paired with distorted 2463 |
| reference | 2464 | 82.215544 | paired with distorted 2685 | paired with distorted 2464 |
| reference | 2465 | 82.248911 | paired with distorted 2686 | paired with distorted 2465 |
| reference | 2466 | 82.282278 | paired with distorted 2687 | paired with distorted 2466 |
| reference | 2467 | 82.315644 | paired with distorted 2688 | paired with distorted 2467 |
| reference | 2468 | 82.349011 | paired with distorted 2689 | paired with distorted 2468 |
| reference | 2469 | 82.382378 | paired with distorted 2690 | paired with distorted 2469 |
| reference | 2470 | 82.415744 | paired with distorted 2691 | paired with distorted 2470 |
| reference | 2471 | 82.449111 | paired with distorted 2692 | paired with distorted 2471 |
| reference | 2472 | 82.482478 | paired with distorted 2693 | paired with distorted 2472 |
| reference | 2473 | 82.515844 | paired with distorted 2694 | paired with distorted 2473 |
| reference | 2474 | 82.549211 | paired with distorted 2695 | paired with distorted 2474 |
| reference | 2475 | 82.582578 | paired with distorted 2696 | paired with distorted 2475 |
| reference | 2476 | 82.615944 | paired with distorted 2697 | paired with distorted 2476 |
| reference | 2477 | 82.649311 | paired with distorted 2698 | paired with distorted 2477 |
| reference | 2478 | 82.682678 | paired with distorted 2699 | paired with distorted 2478 |
| reference | 2479 | 82.716044 | paired with distorted 2701 | paired with distorted 2479 |
| reference | 2480 | 82.749411 | paired with distorted 2972 | paired with distorted 2480 |
- ... (80625 more; the first 1000 in delta.json)

Per frame, over the 2462 frame pairs both sides scored (by Mirage's pair index):
- max \|delta\|: 0.000000 (index 1519)
- mean \|delta\|: 0.000000
- mean delta: 0.000000
- frames with \|delta\| > 0.01: 0

Largest \|delta\|:

| index | FFmpeg's frame | Mirage | FFmpeg | delta |
|---|---|---|---|---|
| 1519 | 1519 | 5.891100 | 5.891100 | 0.000000 |
| 2271 | 2271 | 5.945600 | 5.945600 | 0.000000 |
| 296 | 296 | 6.801676 | 6.801676 | 0.000000 |
| 846 | 846 | 4.951898 | 4.951898 | 0.000000 |
| 2355 | 2355 | 5.892031 | 5.892031 | 0.000000 |

FFmpeg's color handling (its verbose log; the lines are in ffmpeg_colorspace_lines.txt):
- filter graph input 0 (distorted): csp:smpte170m range:tv
- filter graph input 1 (reference): csp:bt709 range:tv
- Parsed_scale_0: 416x234 yuv420p csp:smpte170m range:tv -> 1920x1080 yuv420p csp:bt709 range:tv flags:0x00000004
- Converts: Parsed_scale_0: color_space smpte170m -> bt709

Mirage's color handling of the distorted frames (mirage_vmaf's report, dis_conversion; DECISIONS.md D-016):
- smpte170m/tv -> bt709/tv: matrix changed; 15639 pairs from index 0

Mirage's color handling of the reference frames (mirage_vmaf's report, ref_conversion; DECISIONS.md D-022):
- none: the reference's frames are used as they are

Inputs (ffprobe / mirage_vmaf's decoder):

| | reference | distorted |
|---|---|---|
| width | 1920 | 416 |
| height | 1080 | 234 |
| pix_fmt | yuv420p | yuv420p |
| color_space | bt709 | smpte170m |
| color_range | tv | tv |
| color_primaries | bt709 | smpte170m |
| color_trc | bt709 | bt709 |
| time_base | 1/90000 | 1/90000 |

mirage_vmaf: model vmaf_v0.6.1 (built-in), scaler bicubic, n_threads 4, n_subsample 1, libvmaf package 3.1.0, reference mode explicit-pair; reference PTS 10.000000 to 568.725389 s; pairs with different PTS: 0.

Warnings:
- mirage_vmaf: 55.22% of the frames failed to pair by timestamp (19282 of 34921: 1 unmatched reference, 941 extra distorted, 18340 after a discontinuity), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 15639 frame pairs that paired
- the two sides' pairing treats 80645 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")

The commands are in cmd.txt, the raw outputs next to this file (eval/consistency/README.md lists them).
