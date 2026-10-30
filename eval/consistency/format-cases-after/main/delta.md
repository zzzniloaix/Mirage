# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter

**Overall: FAIL**: 7 of 12 pairs fail.

- Variant: plain FFmpeg: the section 5 comparison.
- Frames: the first 10 frame pairs of each pair (--frames 10).
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, used the same settings, scored the same number of frames and no frame fails.
- Settings given to FFmpeg: model vmaf_v0.6.1, scaler bilinear, n_threads 4.
- mirage_vmaf: sha256 b225a60efd146d27fbb59335176324fa9529b1a86921f8814a1b64e34d162803; libvmaf 3.0.0 at run time, package 3.1.0.
- FFmpeg: ffmpeg version 8.1.1 Copyright (c) 2000-2026 the FFmpeg developers; libvmaf 3.0.0 at run time (its libvmaf logs), package 3.1.0.
- Machine: macOS 26.2 (25C56), Apple M3 Pro.
- Code: commit d993686add8b2af709e1200437e9d643aed26238; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-23T16:20:08Z to 2026-10-23T16:20:18Z (UTC).

| pair | status | frame pairs (Mirage / FFmpeg) | max \|delta\| | mean \|delta\| | frames over | pooled mean Mirage | pooled mean FFmpeg | difference | FFmpeg's scale: distorted csp | Mirage: distorted csp |
|---|---|---|---|---|---|---|---|---|---|---|
| f01_10bit_bt709_vs_smpte170m | FAIL | 10 / 10 | 9.399921 | 8.668279 | 10 | 76.168071 | 84.836351 | -8.668280 | smpte170m -> smpte170m | smpte170m -> bt709 |
| f02_10bit_smpte170m_vs_bt709 | FAIL | 10 / 10 | 7.026388 | 5.771827 | 10 | 40.078382 | 34.306556 | 5.771826 | bt709 -> bt709 | bt709 -> smpte170m |
| f03_10bit_nomatrix_vs_bt709 | FAIL | 10 / 10 | 7.026388 | 5.771827 | 10 | 40.078382 | 34.306556 | 5.771826 | bt709 -> bt709 | bt709 -> unknown |
| f04_10bit_bt709_vs_bt709 | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| f05_422_bt709_vs_smpte170m | FAIL | 10 / 10 | 10.931963 | 10.002553 | 10 | 76.168071 | 86.170624 | -10.002553 | smpte170m -> smpte170m | smpte170m -> bt709 |
| f06_422_bt709_vs_bt709 | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| f07_444_bt709_vs_smpte170m | FAIL | 10 / 10 | 8.973356 | 8.272112 | 10 | 76.168071 | 84.440183 | -8.272112 | smpte170m -> smpte170m | smpte170m -> bt709 |
| f08_444_bt709_vs_bt709 | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| j01_yuvj_pc_vs_tv | FAIL | 10 / 10 | 13.274013 | 11.209380 | 10 | 100 | 88.790620 | 11.209380 | bt709 -> bt709 | bt709 -> bt709 |
| j02_yuvj_pc_vs_pc | FAIL | 10 / 10 | 23.673382 | 23.156749 | 10 | 88.790620 | 65.633871 | 23.156749 | bt709 -> bt709 | bt709 -> bt709 |
| d01_bt709_vs_10bit_smpte170m | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 75.682132 | 75.682132 | 0.000000 | smpte170m -> bt709 | smpte170m -> bt709 |
| d02_bt709tv_vs_yuvj_pc | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 44.750501 | 44.750501 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |

Why pairs fail:
- f01_10bit_bt709_vs_smpte170m: 10 of 10 frames have \|delta\| > 0.01
- f02_10bit_smpte170m_vs_bt709: 10 of 10 frames have \|delta\| > 0.01
- f03_10bit_nomatrix_vs_bt709: 10 of 10 frames have \|delta\| > 0.01
- f05_422_bt709_vs_smpte170m: 10 of 10 frames have \|delta\| > 0.01
- f07_444_bt709_vs_smpte170m: 10 of 10 frames have \|delta\| > 0.01
- j01_yuvj_pc_vs_tv: 10 of 10 frames have \|delta\| > 0.01
- j02_yuvj_pc_vs_pc: 10 of 10 frames have \|delta\| > 0.01

Warnings:
- f01_10bit_bt709_vs_smpte170m: mirage_vmaf: the reference's frames are yuv420p10le, not yuv420p: FFmpeg's section 5 graph then converts the reference to the distorted input's color matrix and range, where Mirage converts the distorted frames to the reference's, so the scores can differ from FFmpeg's (docs/PHASE1_DECISIONS_PENDING.md)
- f01_10bit_bt709_vs_smpte170m: the distorted frames' color conversions differ: mirage_vmaf smpte170m/tv -> bt709/tv, FFmpeg's scale filter smpte170m/tv -> smpte170m/tv
- f02_10bit_smpte170m_vs_bt709: mirage_vmaf: the reference's frames are yuv420p10le, not yuv420p: FFmpeg's section 5 graph then converts the reference to the distorted input's color matrix and range, where Mirage converts the distorted frames to the reference's, so the scores can differ from FFmpeg's (docs/PHASE1_DECISIONS_PENDING.md)
- f02_10bit_smpte170m_vs_bt709: the distorted frames' color conversions differ: mirage_vmaf bt709/tv -> smpte170m/tv, FFmpeg's scale filter bt709/tv -> bt709/tv
- f03_10bit_nomatrix_vs_bt709: mirage_vmaf: the reference's frames are yuv420p10le, not yuv420p: FFmpeg's section 5 graph then converts the reference to the distorted input's color matrix and range, where Mirage converts the distorted frames to the reference's, so the scores can differ from FFmpeg's (docs/PHASE1_DECISIONS_PENDING.md)
- f03_10bit_nomatrix_vs_bt709: the distorted frames' color conversions differ: mirage_vmaf bt709/tv -> unknown/tv, FFmpeg's scale filter bt709/tv -> bt709/tv
- f04_10bit_bt709_vs_bt709: mirage_vmaf: the reference's frames are yuv420p10le, not yuv420p: FFmpeg's section 5 graph then converts the reference to the distorted input's color matrix and range, where Mirage converts the distorted frames to the reference's, so the scores can differ from FFmpeg's (docs/PHASE1_DECISIONS_PENDING.md)
- f05_422_bt709_vs_smpte170m: mirage_vmaf: the reference's frames are yuv422p, not yuv420p: FFmpeg's section 5 graph then converts the reference to the distorted input's color matrix and range, where Mirage converts the distorted frames to the reference's, so the scores can differ from FFmpeg's (docs/PHASE1_DECISIONS_PENDING.md)
- f05_422_bt709_vs_smpte170m: the distorted frames' color conversions differ: mirage_vmaf smpte170m/tv -> bt709/tv, FFmpeg's scale filter smpte170m/tv -> smpte170m/tv
- f06_422_bt709_vs_bt709: mirage_vmaf: the reference's frames are yuv422p, not yuv420p: FFmpeg's section 5 graph then converts the reference to the distorted input's color matrix and range, where Mirage converts the distorted frames to the reference's, so the scores can differ from FFmpeg's (docs/PHASE1_DECISIONS_PENDING.md)
- f07_444_bt709_vs_smpte170m: mirage_vmaf: the reference's frames are yuv444p, not yuv420p: FFmpeg's section 5 graph then converts the reference to the distorted input's color matrix and range, where Mirage converts the distorted frames to the reference's, so the scores can differ from FFmpeg's (docs/PHASE1_DECISIONS_PENDING.md)
- f07_444_bt709_vs_smpte170m: the distorted frames' color conversions differ: mirage_vmaf smpte170m/tv -> bt709/tv, FFmpeg's scale filter smpte170m/tv -> smpte170m/tv
- f08_444_bt709_vs_bt709: mirage_vmaf: the reference's frames are yuv444p, not yuv420p: FFmpeg's section 5 graph then converts the reference to the distorted input's color matrix and range, where Mirage converts the distorted frames to the reference's, so the scores can differ from FFmpeg's (docs/PHASE1_DECISIONS_PENDING.md)
- j01_yuvj_pc_vs_tv: mirage_vmaf: the reference's frames are yuvj420p, not yuv420p: FFmpeg's section 5 graph then converts the reference to the distorted input's color matrix and range, where Mirage converts the distorted frames to the reference's, so the scores can differ from FFmpeg's (docs/PHASE1_DECISIONS_PENDING.md)
- j01_yuvj_pc_vs_tv: the distorted frames' color conversions differ: mirage_vmaf bt709/tv -> bt709/pc, FFmpeg's scale filter bt709/tv -> bt709/tv
- j02_yuvj_pc_vs_pc: mirage_vmaf: the reference's frames are yuvj420p, not yuv420p: FFmpeg's section 5 graph then converts the reference to the distorted input's color matrix and range, where Mirage converts the distorted frames to the reference's, so the scores can differ from FFmpeg's (docs/PHASE1_DECISIONS_PENDING.md)

Each pair's directory has its delta report (delta.md, delta.json), the commands (cmd.txt) and both sides' raw outputs. environment.txt records the machine, the programs and the git state; SHA256SUMS covers every file.
