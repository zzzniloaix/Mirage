# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter

**Overall: FAIL**: 3 of 4 pairs fail.

- Variant: plain FFmpeg: the section 5 comparison.
- Frames: every frame pair.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, used the same settings, scored the same frame pairs at the same timestamps (D-028) and no frame fails.
- Settings given to FFmpeg: model vmaf_v0.6.1, scaler bilinear, n_threads 4.
- mirage_vmaf: sha256 6348ea3a5fd022e55603b0731b2af2f3a72c29d1b901eb1f133c76c0b7d86f40; libvmaf 3.0.0 at run time, package 3.1.0.
- FFmpeg libraries at run time: mirage_vmaf libavutil 60.26.101, libavcodec 62.28.101, libavformat 62.12.101, libswscale 9.5.101, libavfilter 11.14.101; FFmpeg libavutil 60.26.101, libavcodec 62.28.101, libavformat 62.12.101, libavdevice 62.3.101, libavfilter 11.14.101, libswscale 9.5.101, libswresample 6.3.101.
- FFmpeg: ffmpeg version 8.1.1 Copyright (c) 2000-2026 the FFmpeg developers; libvmaf 3.0.0 at run time (its libvmaf logs), package 3.1.0.
- Machine: macOS 26.2 (25C56), Apple M3 Pro.
- Code: commit 4587c2d65ef8796977b4bafae58922ea986380ea; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-27T22:43:07Z to 2026-10-28T03:26:05Z (UTC).

| pair | status | frame pairs (Mirage / FFmpeg) | pairs both scored / only Mirage / only FFmpeg | Mirage's alignment failures (rate) | max \|delta\| | mean \|delta\| | frames over | pooled mean Mirage | pooled mean FFmpeg | difference | FFmpeg's scale: distorted csp | Mirage: distorted csp |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| gear4_vs_gear5 | FAIL | 53217 / 53946 **MISMATCH** | 53191 / 26 / 755 | 1003 (0.018499) | 28.782902 | 0.000541 | 1 | 74.562022 | 74.563207 | -0.001185 | bt709 -> bt709 | bt709 -> bt709 |
| gear3_vs_gear5 | PASS | 53946 / 53946 | 53946 / 0 / 0 | 0 (0) | 0.000000 | 0.000000 | 0 | 53.222012 | 53.222012 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| gear2_vs_gear5 | FAIL | - / - | - | - | - | - | - | - | - | - | smpte170m -> bt709 | smpte170m -> bt709 |
| gear1_vs_gear5 | FAIL | - / - | - | - | - | - | - | - | - | - | smpte170m -> bt709 | smpte170m -> bt709 |

Why pairs fail:
- gear4_vs_gear5: FRAME COUNT MISMATCH: Mirage scored 53217 frame pairs, FFmpeg 53946; only the 53191 pairs both scored are compared
- gear4_vs_gear5: PAIRING MISMATCH: 26 of Mirage's 53217 pairs are not among FFmpeg's, and 755 of FFmpeg's 53946 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- gear4_vs_gear5: 1 of 53191 frames have \|delta\| > 0.01
- gear2_vs_gear5: FFmpeg exited with status 255
- gear1_vs_gear5: FFmpeg exited with status 255

Warnings:
- gear4_vs_gear5: mirage_vmaf: 1.85% of the frames failed to pair by timestamp (1003 of 54220: 63 unmatched reference, 940 after a discontinuity), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 53217 frame pairs that paired
- gear4_vs_gear5: the two sides' pairing treats 1510 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")
- gear2_vs_gear5: mirage_vmaf: 82.89% of the frames failed to pair by timestamp (30420 of 36701: 117 extra distorted, 30303 after a discontinuity), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 6281 frame pairs that paired
- gear1_vs_gear5: mirage_vmaf: 10.66% of the frames failed to pair by timestamp (564 of 5289: 564 extra distorted), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 4725 frame pairs that paired

Each pair's directory has its delta report (delta.md, delta.json), the commands (cmd.txt) and both sides' raw outputs. environment.txt records the machine, the programs and the git state; SHA256SUMS covers every file.
