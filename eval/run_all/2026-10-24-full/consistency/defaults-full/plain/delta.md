# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter

**Overall: FAIL**: 2 of 4 pairs fail.

- Variant: plain FFmpeg: the section 5 comparison.
- Frames: every frame pair.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, used the same settings, scored the same frame pairs at the same timestamps (D-028) and no frame fails.
- Settings given to FFmpeg: model vmaf_v0.6.1, scaler bicubic, n_threads 4.
- mirage_vmaf: sha256 9f04085f73440a9c9969f9bf6a86767eaf9ade89ae8ca393f8b6c99bb78b9b06; libvmaf 3.0.0 at run time, package 3.1.0.
- FFmpeg libraries at run time: mirage_vmaf libavutil 60.26.101, libavcodec 62.28.101, libavformat 62.12.101, libswscale 9.5.101, libavfilter 11.14.101; FFmpeg libavutil 60.26.101, libavcodec 62.28.101, libavformat 62.12.101, libavdevice 62.3.101, libavfilter 11.14.101, libswscale 9.5.101, libswresample 6.3.101.
- FFmpeg: ffmpeg version 8.1.1 Copyright (c) 2000-2026 the FFmpeg developers; libvmaf 3.0.0 at run time (its libvmaf logs), package 3.1.0.
- Machine: macOS 26.2 (25C56), Apple M3 Pro.
- Code: commit 61241e211b8b5ad18eb0ba0667857a1dd3dd7fc2; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-24T19:43:40Z to 2026-10-24T23:43:05Z (UTC).

| pair | status | frame pairs (Mirage / FFmpeg) | pairs both scored / only Mirage / only FFmpeg | Mirage's alignment failures (rate) | max \|delta\| | mean \|delta\| | frames over | pooled mean Mirage | pooled mean FFmpeg | difference | FFmpeg's scale: distorted csp | Mirage: distorted csp |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| gear4_vs_gear5 | PASS | 53946 / 53946 | 53946 / 0 / 0 | 0 (0) | 0.000000 | 0.000000 | 0 | 84.215790 | 84.215790 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| gear3_vs_gear5 | PASS | 53946 / 53946 | 53946 / 0 / 0 | 0 (0) | 0.000000 | 0.000000 | 0 | 64.205911 | 64.205911 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| gear2_vs_gear5 | FAIL | - / - | - | - | - | - | - | - | - | - | smpte170m -> bt709 | - |
| gear1_vs_gear5 | FAIL | 28525 / 28704 **MISMATCH** | 28440 / 85 / 264 | 395 (0.013658) | 2.468787 | 0.000106 | 2 | 6.043396 | 5.988805 | 0.054591 | smpte170m -> bt709 | smpte170m -> bt709 |

Why pairs fail:
- gear2_vs_gear5: mirage_vmaf exited with status -15
- gear1_vs_gear5: FRAME COUNT MISMATCH: Mirage scored 28525 frame pairs, FFmpeg 28704; only the 28440 pairs both scored are compared
- gear1_vs_gear5: PAIRING MISMATCH: 85 of Mirage's 28525 pairs are not among FFmpeg's, and 264 of FFmpeg's 28704 not among Mirage's (the same pair: the same reference and distorted frames); only the pairs both scored are compared
- gear1_vs_gear5: 2 of 28440 frames have \|delta\| > 0.01

Warnings:
- gear1_vs_gear5: mirage_vmaf: 1.37% of the frames failed to pair by timestamp (395 of 28920: 1 unmatched reference, 46 extra distorted, 348 after a discontinuity), above the threshold of 1% (DECISIONS.md D-028); the scores are those of the 28525 frame pairs that paired
- gear1_vs_gear5: the two sides' pairing treats 566 frames differently, Mirage's failures included (the pair's delta.md, "Pairing by timestamp")

Each pair's directory has its delta report (delta.md, delta.json), the commands (cmd.txt) and both sides' raw outputs. environment.txt records the machine, the programs and the git state; SHA256SUMS covers every file.
