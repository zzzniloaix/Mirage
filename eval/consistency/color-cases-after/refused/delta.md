# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter

**Overall: FAIL**: 2 of 2 pairs fail.

- Variant: plain FFmpeg: the section 5 comparison.
- Frames: the first 10 frame pairs of each pair (--frames 10).
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, used the same settings, scored the same number of frames and no frame fails.
- Settings given to FFmpeg: model vmaf_v0.6.1, scaler bilinear, n_threads 4.
- mirage_vmaf: sha256 420b8776e9708329b2f32cf6d2b0f924ee67db6f82f3e05d8e172d28cd243acd; libvmaf None at run time, package None.
- FFmpeg: ffmpeg version 8.1.1 Copyright (c) 2000-2026 the FFmpeg developers; libvmaf package 3.1.0.
- Machine: macOS 26.2 (25C56), Apple M3 Pro.
- Code: commit 5263061c3dfa7b3134763669443977b436ab1003; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-23T11:28:01Z to 2026-10-23T11:28:02Z (UTC).

| pair | status | frame pairs (Mirage / FFmpeg) | max \|delta\| | mean \|delta\| | frames over | pooled mean Mirage | pooled mean FFmpeg | difference | FFmpeg's scale: distorted csp | Mirage: distorted csp |
|---|---|---|---|---|---|---|---|---|---|---|
| u01_bt709_vs_ycgco | FAIL | - / - | - | - | - | - | - | - | - | - |
| u02_ycgco_vs_bt709 | FAIL | - / - | - | - | - | - | - | - | - | - |

Why pairs fail:
- u01_bt709_vs_ycgco: mirage_vmaf exited with status 1
- u01_bt709_vs_ycgco: FFmpeg exited with status 178
- u02_ycgco_vs_bt709: mirage_vmaf exited with status 1
- u02_ycgco_vs_bt709: FFmpeg exited with status 178

Each pair's directory has its delta report (delta.md, delta.json), the commands (cmd.txt) and both sides' raw outputs. environment.txt records the machine, the programs and the git state; SHA256SUMS covers every file.
