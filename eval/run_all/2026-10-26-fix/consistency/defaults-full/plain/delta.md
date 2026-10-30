# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter

**Overall: FAIL**: 2 of 4 pairs fail.

- Variant: plain FFmpeg: the section 5 comparison.
- Frames: every frame pair.
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, used the same settings, scored the same frame pairs at the same timestamps (D-028) and no frame fails.
- Settings given to FFmpeg: model vmaf_v0.6.1, scaler bicubic, n_threads 4.
- mirage_vmaf: sha256 217cc2948bf361aa11a37f4aea467ffe1caa30ab251943ba18b72c55005c6f10; libvmaf 3.0.0 at run time, package 3.1.0.
- FFmpeg libraries at run time: mirage_vmaf libavutil 60.26.101, libavcodec 62.28.101, libavformat 62.12.101, libswscale 9.5.101, libavfilter 11.14.101; FFmpeg libavutil 60.26.101, libavcodec 62.28.101, libavformat 62.12.101, libavdevice 62.3.101, libavfilter 11.14.101, libswscale 9.5.101, libswresample 6.3.101.
- FFmpeg: ffmpeg version 8.1.1 Copyright (c) 2000-2026 the FFmpeg developers; libvmaf 3.0.0 at run time (its libvmaf logs), package 3.1.0.
- Machine: macOS 26.2 (25C56), Apple M3 Pro.
- Code: commit a4692a204313fae5c6e6c472029eb5db75b80740; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-26T19:13:27Z to 2026-10-26T21:17:23Z (UTC).

| pair | status | frame pairs (Mirage / FFmpeg) | pairs both scored / only Mirage / only FFmpeg | Mirage's alignment failures (rate) | max \|delta\| | mean \|delta\| | frames over | pooled mean Mirage | pooled mean FFmpeg | difference | FFmpeg's scale: distorted csp | Mirage: distorted csp |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| gear4_vs_gear5 | PASS | 53946 / 53946 | 53946 / 0 / 0 | 0 (0) | 0.000000 | 0.000000 | 0 | 84.215790 | 84.215790 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| gear3_vs_gear5 | PASS | 53946 / 53946 | 53946 / 0 / 0 | 0 (0) | 0.000000 | 0.000000 | 0 | 64.205911 | 64.205911 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| gear2_vs_gear5 | FAIL | - / - | - | - | - | - | - | - | - | - | smpte170m -> bt709 | - |
| gear1_vs_gear5 | FAIL | - / - | - | - | - | - | - | - | - | - | smpte170m -> bt709 | - |

Why pairs fail:
- gear2_vs_gear5: mirage_vmaf exited with status -15
- gear2_vs_gear5: FFmpeg exited with status 255
- gear1_vs_gear5: mirage_vmaf exited with status -15
- gear1_vs_gear5: FFmpeg exited with status 255

Each pair's directory has its delta report (delta.md, delta.json), the commands (cmd.txt) and both sides' raw outputs. environment.txt records the machine, the programs and the git state; SHA256SUMS covers every file.
