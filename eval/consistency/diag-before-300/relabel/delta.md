# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter

**Overall: PASS**: all 4 pairs pass.

- Variant: DIAGNOSTIC, not the section 5 comparison: FFmpeg with the distorted input relabeled to the reference's color_space and color_range tags where they differ (setparams before scale), so that FFmpeg does not convert them.
- Frames: the first 300 frame pairs of each pair (--frames 300).
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, used the same settings, scored the same number of frames and no frame fails.
- Settings given to FFmpeg: model vmaf_v0.6.1, scaler bilinear, n_threads 4.
- mirage_vmaf: sha256 4bb0fc2bec6bd240463e3f183204c426bbff1652eadd342733917d8fc58f84c0; libvmaf 3.0.0 at run time, package 3.1.0.
- FFmpeg: ffmpeg version 8.1.1 Copyright (c) 2000-2026 the FFmpeg developers; libvmaf package 3.1.0.
- Machine: macOS 26.2 (25C56), Apple M3 Pro.
- Code: commit ff98a1320b7f258a0856c3c8ba341b63c750d85a; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-23T10:45:18Z to 2026-10-23T10:46:02Z (UTC).

| pair | status | frame pairs (Mirage / FFmpeg) | max \|delta\| | mean \|delta\| | frames over | pooled mean Mirage | pooled mean FFmpeg | difference | FFmpeg's scale: distorted csp |
|---|---|---|---|---|---|---|---|---|---|
| gear4_vs_gear5 | PASS | 300 / 300 | 0.000000 | 0.000000 | 0 | 74.640607 | 74.640607 | 0.000000 | bt709 -> bt709 |
| gear3_vs_gear5 | PASS | 300 / 300 | 0.000000 | 0.000000 | 0 | 53.729092 | 53.729092 | 0.000000 | bt709 -> bt709 |
| gear2_vs_gear5 | PASS | 300 / 300 | 0.000000 | 0.000000 | 0 | 33.832772 | 33.832772 | 0.000000 | bt709 -> bt709 |
| gear1_vs_gear5 | PASS | 300 / 300 | 0.000000 | 0.000000 | 0 | 1.884503 | 1.884503 | 0.000000 | bt709 -> bt709 |

Relabels inserted before FFmpeg's scale:
- gear2_vs_gear5: `setparams=colorspace=bt709`
- gear1_vs_gear5: `setparams=colorspace=bt709`

Each pair's directory has its delta report (delta.md, delta.json), the commands (cmd.txt) and both sides' raw outputs. environment.txt records the machine, the programs and the git state; SHA256SUMS covers every file.
