# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter

**Overall: FAIL**: 2 of 4 pairs fail.

- Variant: plain FFmpeg: the section 5 comparison.
- Frames: the first 300 frame pairs of each pair (--frames 300).
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, used the same settings, scored the same number of frames and no frame fails.
- Settings given to FFmpeg: model vmaf_v0.6.1, scaler bilinear, n_threads 4.
- mirage_vmaf: sha256 4bb0fc2bec6bd240463e3f183204c426bbff1652eadd342733917d8fc58f84c0; libvmaf 3.0.0 at run time, package 3.1.0.
- FFmpeg: ffmpeg version 8.1.1 Copyright (c) 2000-2026 the FFmpeg developers; libvmaf package 3.1.0.
- Machine: macOS 26.2 (25C56), Apple M3 Pro.
- Code: commit ff98a1320b7f258a0856c3c8ba341b63c750d85a; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-23T10:44:33Z to 2026-10-23T10:45:18Z (UTC).

| pair | status | frame pairs (Mirage / FFmpeg) | max \|delta\| | mean \|delta\| | frames over | pooled mean Mirage | pooled mean FFmpeg | difference | FFmpeg's scale: distorted csp |
|---|---|---|---|---|---|---|---|---|---|
| gear4_vs_gear5 | PASS | 300 / 300 | 0.000000 | 0.000000 | 0 | 74.640607 | 74.640607 | 0.000000 | bt709 -> bt709 |
| gear3_vs_gear5 | PASS | 300 / 300 | 0.000000 | 0.000000 | 0 | 53.729092 | 53.729092 | 0.000000 | bt709 -> bt709 |
| gear2_vs_gear5 | FAIL | 300 / 300 | 5.674685 | 5.279836 | 300 | 33.832772 | 28.552935 | 5.279837 | smpte170m -> bt709 |
| gear1_vs_gear5 | FAIL | 300 / 300 | 3.883766 | 1.878387 | 290 | 1.884503 | 0.006117 | 1.878386 | smpte170m -> bt709 |

Why pairs fail:
- gear2_vs_gear5: 300 of 300 frames have \|delta\| > 0.01
- gear1_vs_gear5: 290 of 300 frames have \|delta\| > 0.01

Each pair's directory has its delta report (delta.md, delta.json), the commands (cmd.txt) and both sides' raw outputs. environment.txt records the machine, the programs and the git state; SHA256SUMS covers every file.
