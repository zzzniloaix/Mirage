# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter

**Overall: PASS**: all 12 pairs pass.

- Variant: plain FFmpeg: the section 5 comparison.
- Frames: the first 10 frame pairs of each pair (--frames 10).
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, used the same settings, scored the same number of frames and no frame fails.
- Settings given to FFmpeg: model vmaf_v0.6.1, scaler bilinear, n_threads 4.
- mirage_vmaf: sha256 2220ce3a0997d53f750b34a770110f5c65f5f870cfb9a0e6347256733bcf09ea; libvmaf 3.0.0 at run time, package 3.1.0.
- FFmpeg libraries at run time: mirage_vmaf libavutil 60.26.101, libavcodec 62.28.101, libavformat 62.12.101, libswscale 9.5.101, libavfilter 11.14.101; FFmpeg libavutil 60.26.101, libavcodec 62.28.101, libavformat 62.12.101, libavdevice 62.3.101, libavfilter 11.14.101, libswscale 9.5.101, libswresample 6.3.101.
- FFmpeg: ffmpeg version 8.1.1 Copyright (c) 2000-2026 the FFmpeg developers; libvmaf 3.0.0 at run time (its libvmaf logs), package 3.1.0.
- Machine: macOS 26.2 (25C56), Apple M3 Pro.
- Code: commit 3ea02813df24e1b3a8675e44af902fac6a540524; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-23T21:41:24Z to 2026-10-23T21:41:34Z (UTC).

| pair | status | frame pairs (Mirage / FFmpeg) | max \|delta\| | mean \|delta\| | frames over | pooled mean Mirage | pooled mean FFmpeg | difference | FFmpeg's scale: distorted csp | Mirage: distorted csp |
|---|---|---|---|---|---|---|---|---|---|---|
| f01_10bit_bt709_vs_smpte170m | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 84.836351 | 84.836351 | 0.000000 | smpte170m -> smpte170m | smpte170m -> smpte170m |
| f02_10bit_smpte170m_vs_bt709 | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 34.306556 | 34.306556 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| f03_10bit_nomatrix_vs_bt709 | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 34.306556 | 34.306556 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| f04_10bit_bt709_vs_bt709 | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| f05_422_bt709_vs_smpte170m | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 86.170624 | 86.170624 | 0.000000 | smpte170m -> smpte170m | smpte170m -> smpte170m |
| f06_422_bt709_vs_bt709 | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| f07_444_bt709_vs_smpte170m | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 84.440183 | 84.440183 | 0.000000 | smpte170m -> smpte170m | smpte170m -> smpte170m |
| f08_444_bt709_vs_bt709 | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| j01_yuvj_pc_vs_tv | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 88.790620 | 88.790620 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| j02_yuvj_pc_vs_pc | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| d01_bt709_vs_10bit_smpte170m | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 75.682132 | 75.682132 | 0.000000 | smpte170m -> bt709 | smpte170m -> bt709 |
| d02_bt709tv_vs_yuvj_pc | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 44.750501 | 44.750501 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |

Each pair's directory has its delta report (delta.md, delta.json), the commands (cmd.txt) and both sides' raw outputs. environment.txt records the machine, the programs and the git state; SHA256SUMS covers every file.
