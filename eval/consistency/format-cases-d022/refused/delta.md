# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter

**Overall: FAIL**: 1 of 1 pairs fail.

- Variant: plain FFmpeg: the section 5 comparison.
- Frames: the first 10 frame pairs of each pair (--frames 10).
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, used the same settings, scored the same number of frames and no frame fails.
- Settings given to FFmpeg: model vmaf_v0.6.1, scaler bilinear, n_threads 4.
- mirage_vmaf: sha256 2220ce3a0997d53f750b34a770110f5c65f5f870cfb9a0e6347256733bcf09ea; libvmaf None at run time, package None.
- FFmpeg libraries at run time: FFmpeg libavutil 60.26.101, libavcodec 62.28.101, libavformat 62.12.101, libavdevice 62.3.101, libavfilter 11.14.101, libswscale 9.5.101, libswresample 6.3.101.
- FFmpeg: ffmpeg version 8.1.1 Copyright (c) 2000-2026 the FFmpeg developers; libvmaf package 3.1.0.
- Machine: macOS 26.2 (25C56), Apple M3 Pro.
- Code: commit 3ea02813df24e1b3a8675e44af902fac6a540524; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-23T21:41:34Z to 2026-10-23T21:41:35Z (UTC).

| pair | status | frame pairs (Mirage / FFmpeg) | max \|delta\| | mean \|delta\| | frames over | pooled mean Mirage | pooled mean FFmpeg | difference | FFmpeg's scale: distorted csp | Mirage: distorted csp |
|---|---|---|---|---|---|---|---|---|---|---|
| u03_ycgco_vs_ycgco | FAIL | - / - | - | - | - | - | - | - | - | - |

Why pairs fail:
- u03_ycgco_vs_ycgco: mirage_vmaf exited with status 1
- u03_ycgco_vs_ycgco: FFmpeg exited with status 178

Each pair's directory has its delta report (delta.md, delta.json), the commands (cmd.txt) and both sides' raw outputs. environment.txt records the machine, the programs and the git state; SHA256SUMS covers every file.
