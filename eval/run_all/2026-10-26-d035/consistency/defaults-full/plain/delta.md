# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter

**Overall: FAIL**: 3 of 4 pairs fail.

- Variant: plain FFmpeg: the section 5 comparison.
- Frames: every frame pair.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, used the same settings, scored the same frame pairs at the same timestamps (D-028) and no frame fails.
- Settings given to FFmpeg: model vmaf_v0.6.1, scaler bicubic, n_threads 4.
- mirage_vmaf: sha256 aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3; libvmaf 3.0.0 at run time, package 3.1.0.
- FFmpeg libraries at run time: mirage_vmaf libavutil 60.26.101, libavcodec 62.28.101, libavformat 62.12.101, libswscale 9.5.101, libavfilter 11.14.101; FFmpeg libavutil 60.26.101, libavcodec 62.28.101, libavformat 62.12.101, libavdevice 62.3.101, libavfilter 11.14.101, libswscale 9.5.101, libswresample 6.3.101.
- FFmpeg: ffmpeg version 8.1.1 Copyright (c) 2000-2026 the FFmpeg developers; libvmaf 3.0.0 at run time (its libvmaf logs), package 3.1.0.
- Machine: macOS 26.2 (25C56), Apple M3 Pro.
- Code: commit bbc669dfeae9febd6f609f5f1e7b4f1489a57182; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-26T23:33:35Z to 2026-10-27T17:27:14Z (UTC).

| pair | status | frame pairs (Mirage / FFmpeg) | pairs both scored / only Mirage / only FFmpeg | Mirage's alignment failures (rate) | max \|delta\| | mean \|delta\| | frames over | pooled mean Mirage | pooled mean FFmpeg | difference | FFmpeg's scale: distorted csp | Mirage: distorted csp |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| gear4_vs_gear5 | PASS | 53946 / 53946 | 53946 / 0 / 0 | 0 (0) | 0.000000 | 0.000000 | 0 | 84.215790 | 84.215790 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| gear3_vs_gear5 | FAIL | 52442 / 53946 **MISMATCH** | 52439 / 3 / 1507 | 2104 (0.038573) | 0.063371 | 0.000001 | 1 | 64.215437 | 64.205911 | 0.009526 | bt709 -> bt709 | bt709 -> bt709 |
| gear2_vs_gear5 | FAIL | - / - | - | - | - | - | - | - | - | - | smpte170m -> bt709 | smpte170m -> bt709 |
| gear1_vs_gear5 | FAIL | 15639 / 52184 **MISMATCH** | 2462 / 13177 / 49722 | 19282 (0.552161) | 0.000000 | 0.000000 | 0 | 6.044270 | 1.534324 | 4.509946 | smpte170m -> bt709 | smpte170m -> bt709 |

Why pairs fail:
- gear3_vs_gear5: FRAME COUNT MISMATCH: Mirage scored 52442 frame pairs, FFmpeg 53946; only the 52439 pairs both scored are compared
- gear3_vs_gear5: PAIRING MISMATCH: 3 of Mirage's 52442 pairs are not among FFmpeg's, and 1507 of FFmpeg's 53946 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- gear3_vs_gear5: 1 of 52439 frames have \|delta\| > 0.01
- gear2_vs_gear5: FFmpeg exited with status 255
- gear1_vs_gear5: FRAME COUNT MISMATCH: Mirage scored 15639 frame pairs, FFmpeg 52184; only the 2462 pairs both scored are compared
- gear1_vs_gear5: PAIRING MISMATCH: 13177 of Mirage's 15639 pairs are not among FFmpeg's, and 49722 of FFmpeg's 52184 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- gear1_vs_gear5: TIMESTAMP MISMATCH: 1 of the pairs both scored have other timestamps on the two sides

Warnings:
- gear3_vs_gear5: mirage_vmaf: 3.86% of the frames failed to pair by timestamp (2104 of 54546: 43 unmatched reference, 51 extra distorted, 2010 after a discontinuity), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 52442 frame pairs that paired
- gear3_vs_gear5: the two sides' pairing treats 3014 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")
- gear2_vs_gear5: mirage_vmaf: 95.70% of the frames failed to pair by timestamp (98489 of 102914: 195 extra distorted, 98294 after a discontinuity), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 4425 frame pairs that paired
- gear1_vs_gear5: mirage_vmaf: 55.22% of the frames failed to pair by timestamp (19282 of 34921: 1 unmatched reference, 941 extra distorted, 18340 after a discontinuity), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 15639 frame pairs that paired
- gear1_vs_gear5: the two sides' pairing treats 80645 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")

Each pair's directory has its delta report (delta.md, delta.json), the commands (cmd.txt) and both sides' raw outputs. environment.txt records the machine, the programs and the git state; SHA256SUMS covers every file.
