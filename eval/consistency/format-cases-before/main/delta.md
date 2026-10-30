# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter

**Overall: FAIL**: 7 of 12 pairs fail.

- Variant: plain FFmpeg: the section 5 comparison.
- Frames: the first 10 frame pairs of each pair (--frames 10).
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, used the same settings, scored the same number of frames and no frame fails.
- Settings given to FFmpeg: model vmaf_v0.6.1, scaler bilinear, n_threads 4.
- mirage_vmaf: sha256 4bb0fc2bec6bd240463e3f183204c426bbff1652eadd342733917d8fc58f84c0; libvmaf 3.0.0 at run time, package 3.1.0.
- FFmpeg: ffmpeg version 8.1.1 Copyright (c) 2000-2026 the FFmpeg developers; libvmaf 3.0.0 at run time (its libvmaf logs), package 3.1.0.
- Machine: macOS 26.2 (25C56), Apple M3 Pro.
- Code: commit d993686add8b2af709e1200437e9d643aed26238; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-23T16:19:57Z to 2026-10-23T16:20:07Z (UTC).

| pair | status | frame pairs (Mirage / FFmpeg) | max \|delta\| | mean \|delta\| | frames over | pooled mean Mirage | pooled mean FFmpeg | difference | FFmpeg's scale: distorted csp | Mirage: distorted csp |
|---|---|---|---|---|---|---|---|---|---|---|
| f01_10bit_bt709_vs_smpte170m | FAIL | 10 / 10 | 20.552759 | 19.202479 | 10 | 65.633871 | 84.836351 | -19.202480 | smpte170m -> smpte170m | - |
| f02_10bit_smpte170m_vs_bt709 | FAIL | 10 / 10 | 32.029205 | 31.327315 | 10 | 65.633871 | 34.306556 | 31.327315 | bt709 -> bt709 | - |
| f03_10bit_nomatrix_vs_bt709 | FAIL | 10 / 10 | 32.029205 | 31.327315 | 10 | 65.633871 | 34.306556 | 31.327315 | bt709 -> bt709 | - |
| f04_10bit_bt709_vs_bt709 | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 | - |
| f05_422_bt709_vs_smpte170m | FAIL | 10 / 10 | 22.084801 | 20.536753 | 10 | 65.633871 | 86.170624 | -20.536753 | smpte170m -> smpte170m | - |
| f06_422_bt709_vs_bt709 | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 | - |
| f07_444_bt709_vs_smpte170m | FAIL | 10 / 10 | 20.072068 | 18.806312 | 10 | 65.633871 | 84.440183 | -18.806312 | smpte170m -> smpte170m | - |
| f08_444_bt709_vs_bt709 | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 | - |
| j01_yuvj_pc_vs_tv | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 88.790620 | 88.790620 | 0.000000 | bt709 -> bt709 | - |
| j02_yuvj_pc_vs_pc | FAIL | 10 / 10 | 23.673382 | 23.156749 | 10 | 88.790620 | 65.633871 | 23.156749 | bt709 -> bt709 | - |
| d01_bt709_vs_10bit_smpte170m | FAIL | 10 / 10 | 11.674191 | 10.239115 | 10 | 65.443017 | 75.682132 | -10.239115 | smpte170m -> bt709 | - |
| d02_bt709tv_vs_yuvj_pc | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 44.750501 | 44.750501 | 0.000000 | bt709 -> bt709 | - |

Why pairs fail:
- f01_10bit_bt709_vs_smpte170m: 10 of 10 frames have \|delta\| > 0.01
- f02_10bit_smpte170m_vs_bt709: 10 of 10 frames have \|delta\| > 0.01
- f03_10bit_nomatrix_vs_bt709: 10 of 10 frames have \|delta\| > 0.01
- f05_422_bt709_vs_smpte170m: 10 of 10 frames have \|delta\| > 0.01
- f07_444_bt709_vs_smpte170m: 10 of 10 frames have \|delta\| > 0.01
- j02_yuvj_pc_vs_pc: 10 of 10 frames have \|delta\| > 0.01
- d01_bt709_vs_10bit_smpte170m: 10 of 10 frames have \|delta\| > 0.01

Each pair's directory has its delta report (delta.md, delta.json), the commands (cmd.txt) and both sides' raw outputs. environment.txt records the machine, the programs and the git state; SHA256SUMS covers every file.
