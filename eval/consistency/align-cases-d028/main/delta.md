# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter

**Overall: FAIL**: 7 of 9 pairs fail.

- Variant: plain FFmpeg: the section 5 comparison.
- Frames: every frame pair.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, used the same settings, scored the same frame pairs at the same timestamps (D-028) and no frame fails.
- Settings given to FFmpeg: model vmaf_v0.6.1, scaler bicubic, n_threads 4.
- mirage_vmaf: sha256 7fb455e9e1349c85e25afe54d5fd47d714c463ecebea1b03b84ca1b594d27741; libvmaf 3.0.0 at run time, package 3.1.0.
- FFmpeg libraries at run time: mirage_vmaf libavutil 60.26.101, libavcodec 62.28.101, libavformat 62.12.101, libswscale 9.5.101, libavfilter 11.14.101; FFmpeg libavutil 60.26.101, libavcodec 62.28.101, libavformat 62.12.101, libavdevice 62.3.101, libavfilter 11.14.101, libswscale 9.5.101, libswresample 6.3.101.
- FFmpeg: ffmpeg version 8.1.1 Copyright (c) 2000-2026 the FFmpeg developers; libvmaf 3.0.0 at run time (its libvmaf logs), package 3.1.0.
- Machine: macOS 26.2 (25C56), Apple M3 Pro.
- Code: commit d9b1f51d87ee558a270ddbde0b7c67ec5400f09c; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-24T00:56:32Z to 2026-10-24T00:56:38Z (UTC).

| pair | status | frame pairs (Mirage / FFmpeg) | pairs both scored / only Mirage / only FFmpeg | Mirage's alignment failures (rate) | max \|delta\| | mean \|delta\| | frames over | pooled mean Mirage | pooled mean FFmpeg | difference | FFmpeg's scale: distorted csp | Mirage: distorted csp |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| a00_aligned | PASS | 10 / 10 | 10 / 0 / 0 | 0 (0) | 0.000000 | 0.000000 | 0 | 74.658620 | 74.658620 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| a01_dropped_distorted | FAIL | 9 / 9 | 9 / 0 / 0 | 1 (0.100000) | 2.074907 | 0.230545 | 1 | 74.387694 | 74.618239 | -0.230545 | bt709 -> bt709 | bt709 -> bt709 |
| a02_dropped_reference | FAIL | 9 / 10 **MISMATCH** | 9 / 0 / 1 | 1 (0.100000) | 2.151021 | 0.239002 | 1 | 74.618239 | 69.066872 | 5.551367 | bt709 -> bt709 | bt709 -> bt709 |
| a03_duplicated_timestamp | FAIL | 10 / 11 **MISMATCH** | 10 / 0 / 1 | 1 (0.090909) | 2.124530 | 0.212453 | 1 | 74.658620 | 74.307152 | 0.351468 | bt709 -> bt709 | bt709 -> bt709 |
| a04_half_rate | FAIL | 5 / 6 **MISMATCH** | 5 / 0 / 1 | 0 (0) | 0.072752 | 0.014551 | 1 | 74.493542 | 66.173476 | 8.320066 | bt709 -> bt709 | bt709 -> bt709 |
| a05_later_start | FAIL | 7 / 10 **MISMATCH** | 7 / 0 / 3 | 3 (0.300000) | 0.042191 | 0.006028 | 1 | 21.983465 | 27.715595 | -5.732130 | bt709 -> bt709 | bt709 -> bt709 |
| a06_later_start_audio | PASS | 7 / 7 | 7 / 0 / 0 | 3 (0.300000) | 0.000000 | 0.000000 | 0 | 74.578076 | 74.578076 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| a07_restart | FAIL | 5 / 10 **MISMATCH** | 5 / 0 / 5 | 10 (0.666667) | 0.019371 | 0.003874 | 1 | 72.399419 | 72.601226 | -0.201807 | bt709 -> bt709 | bt709 -> bt709 |
| a08_ms_vs_90khz | FAIL | 10 / 10 | 6 / 4 / 4 | 0 (0) | 2.131170 | 1.064375 | 5 | 72.079617 | 51.260499 | 20.819118 | bt709 -> bt709 | bt709 -> bt709 |

Why pairs fail:
- a01_dropped_distorted: 1 of 9 frames have \|delta\| > 0.01
- a02_dropped_reference: FRAME COUNT MISMATCH: Mirage scored 9 frame pairs, FFmpeg 10; only the 9 pairs both scored are compared
- a02_dropped_reference: PAIRING MISMATCH: 0 of Mirage's 9 pairs are not among FFmpeg's, and 1 of FFmpeg's 10 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- a02_dropped_reference: 1 of 9 frames have \|delta\| > 0.01
- a03_duplicated_timestamp: FRAME COUNT MISMATCH: Mirage scored 10 frame pairs, FFmpeg 11; only the 10 pairs both scored are compared
- a03_duplicated_timestamp: PAIRING MISMATCH: 0 of Mirage's 10 pairs are not among FFmpeg's, and 1 of FFmpeg's 11 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- a03_duplicated_timestamp: 1 of 10 frames have \|delta\| > 0.01
- a04_half_rate: FRAME COUNT MISMATCH: Mirage scored 5 frame pairs, FFmpeg 6; only the 5 pairs both scored are compared
- a04_half_rate: PAIRING MISMATCH: 0 of Mirage's 5 pairs are not among FFmpeg's, and 1 of FFmpeg's 6 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- a04_half_rate: 1 of 5 frames have \|delta\| > 0.01
- a05_later_start: FRAME COUNT MISMATCH: Mirage scored 7 frame pairs, FFmpeg 10; only the 7 pairs both scored are compared
- a05_later_start: PAIRING MISMATCH: 0 of Mirage's 7 pairs are not among FFmpeg's, and 3 of FFmpeg's 10 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- a05_later_start: 1 of 7 frames have \|delta\| > 0.01
- a07_restart: FRAME COUNT MISMATCH: Mirage scored 5 frame pairs, FFmpeg 10; only the 5 pairs both scored are compared
- a07_restart: PAIRING MISMATCH: 0 of Mirage's 5 pairs are not among FFmpeg's, and 5 of FFmpeg's 10 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- a07_restart: 1 of 5 frames have \|delta\| > 0.01
- a08_ms_vs_90khz: PAIRING MISMATCH: 4 of Mirage's 10 pairs are not among FFmpeg's, and 4 of FFmpeg's 10 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- a08_ms_vs_90khz: 5 of 6 frames have \|delta\| > 0.01

Warnings:
- a01_dropped_distorted: mirage_vmaf: 10.00% of the frames failed to pair by timestamp (1 of 10: 1 unmatched reference), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 9 frame pairs that paired
- a01_dropped_distorted: the two sides' pairing treats 1 frame differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")
- a02_dropped_reference: mirage_vmaf: 10.00% of the frames failed to pair by timestamp (1 of 10: 1 extra distorted), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 9 frame pairs that paired
- a02_dropped_reference: the two sides' pairing treats 2 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")
- a03_duplicated_timestamp: mirage_vmaf: 9.09% of the frames failed to pair by timestamp (1 of 11: 1 duplicate distorted), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 10 frame pairs that paired
- a03_duplicated_timestamp: the two sides' pairing treats 2 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")
- a04_half_rate: the two sides' pairing treats 2 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")
- a05_later_start: mirage_vmaf: 30.00% of the frames failed to pair by timestamp (3 of 10: 3 unmatched reference), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 7 frame pairs that paired
- a05_later_start: the two sides' pairing treats 4 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")
- a06_later_start_audio: mirage_vmaf: 30.00% of the frames failed to pair by timestamp (3 of 10: 3 unmatched reference), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 7 frame pairs that paired
- a06_later_start_audio: the two sides' pairing treats 3 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")
- a07_restart: mirage_vmaf: 66.67% of the frames failed to pair by timestamp (10 of 15: 10 after a discontinuity), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 5 frame pairs that paired
- a07_restart: the two sides' pairing treats 10 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")
- a07_restart: FFmpeg corrected 1 timestamp discontinuity of its inputs (its log), which mirage_vmaf doesn't (D-028)
- a08_ms_vs_90khz: the two sides' pairing treats 11 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")

Each pair's directory has its delta report (delta.md, delta.json), the commands (cmd.txt) and both sides' raw outputs. environment.txt records the machine, the programs and the git state; SHA256SUMS covers every file.
