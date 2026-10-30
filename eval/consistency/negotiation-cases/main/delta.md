# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter

**Overall: PASS**: all 4 pairs pass.

- Variant: plain FFmpeg: the section 5 comparison.
- Frames: the first 10 frame pairs of each pair (--frames 10).
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, used the same settings, scored the same frame pairs at the same timestamps (D-028) and no frame fails.
- Settings given to FFmpeg: model vmaf_v0.6.1, scaler bicubic, n_threads 4.
- mirage_vmaf: sha256 aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3; libvmaf 3.0.0 at run time, package 3.1.0.
- FFmpeg libraries at run time: mirage_vmaf libavutil 60.26.101, libavcodec 62.28.101, libavformat 62.12.101, libswscale 9.5.101, libavfilter 11.14.101; FFmpeg libavutil 60.26.101, libavcodec 62.28.101, libavformat 62.12.101, libavdevice 62.3.101, libavfilter 11.14.101, libswscale 9.5.101, libswresample 6.3.101.
- FFmpeg: ffmpeg version 8.1.1 Copyright (c) 2000-2026 the FFmpeg developers; libvmaf 3.0.0 at run time (its libvmaf logs), package 3.1.0.
- Machine: macOS 26.2 (25C56), Apple M3 Pro.
- Code: commit da0464c65b5ceaf8fea99ebc8caf25d61c10c6b4; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-26T22:35:00Z to 2026-10-26T22:35:04Z (UTC).

| pair | status | frame pairs (Mirage / FFmpeg) | pairs both scored / only Mirage / only FFmpeg | Mirage's alignment failures (rate) | max \|delta\| | mean \|delta\| | frames over | pooled mean Mirage | pooled mean FFmpeg | difference | FFmpeg's scale: distorted csp | Mirage: distorted csp |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| n01_yuvj_pc_vs_norange | PASS | 10 / 10 | 10 / 0 / 0 | 0 (0) | 0.000000 | 0.000000 | 0 | 98.122911 | 98.122911 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| n02_10bit_tv_vs_norange | PASS | 10 / 10 | 10 / 0 / 0 | 0 (0) | 0.000000 | 0.000000 | 0 | 74.658620 | 74.658620 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| n03_10bit_bt709_vs_gray | PASS | 10 / 10 | 10 / 0 / 0 | 0 (0) | 0.000000 | 0.000000 | 0 | 74.720178 | 74.720178 | 0.000000 | unknown -> bt709; bt709 -> bt709 | bt709 -> bt709 |
| n04_10bit_bt709_vs_rgb | PASS | 10 / 10 | 10 / 0 / 0 | 0 (0) | 0.000000 | 0.000000 | 0 | 95.975142 | 95.975142 | 0.000000 | gbr -> bt709 | gbr -> bt709 |

Warnings:
- n03_10bit_bt709_vs_gray: the distorted frames' color conversions differ: mirage_vmaf bt709/tv -> bt709/tv, FFmpeg's scale filter bt709/tv -> bt709/tv, unknown/pc -> bt709/tv
- n04_10bit_bt709_vs_rgb: mirage_vmaf: the distorted input's frames are bgr0, a pixel format whose conversion isn't checked against FFmpeg's section 5 graph (vmafcolor::verified), so the scores can differ from FFmpeg's

Each pair's directory has its delta report (delta.md, delta.json), the commands (cmd.txt) and both sides' raw outputs. environment.txt records the machine, the programs and the git state; SHA256SUMS covers every file.
