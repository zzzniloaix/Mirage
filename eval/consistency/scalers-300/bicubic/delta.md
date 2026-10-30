# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter

**Overall: PASS**: all 4 pairs pass.

- Variant: plain FFmpeg: the section 5 comparison.
- Frames: the first 300 frame pairs of each pair (--frames 300).
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, used the same settings, scored the same number of frames and no frame fails.
- Settings given to FFmpeg: model vmaf_v0.6.1, scaler bicubic, n_threads 4.
- mirage_vmaf: sha256 3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c; libvmaf 3.0.0 at run time, package 3.1.0.
- FFmpeg: ffmpeg version 8.1.1 Copyright (c) 2000-2026 the FFmpeg developers; libvmaf 3.0.0 at run time (its libvmaf logs), package 3.1.0.
- Machine: macOS 26.2 (25C56), Apple M3 Pro.
- Code: commit 8cc06a7c6939660e53097b948f6a78c068e8faee; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-23T12:04:35Z to 2026-10-23T12:05:19Z (UTC).

| pair | status | frame pairs (Mirage / FFmpeg) | max \|delta\| | mean \|delta\| | frames over | pooled mean Mirage | pooled mean FFmpeg | difference | FFmpeg's scale: distorted csp | Mirage: distorted csp |
|---|---|---|---|---|---|---|---|---|---|---|
| gear4_vs_gear5 | PASS | 300 / 300 | 0.000000 | 0.000000 | 0 | 84.258060 | 84.258060 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| gear3_vs_gear5 | PASS | 300 / 300 | 0.000000 | 0.000000 | 0 | 64.843002 | 64.843002 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| gear2_vs_gear5 | PASS | 300 / 300 | 0.000000 | 0.000000 | 0 | 36.817663 | 36.817663 | 0.000000 | smpte170m -> bt709 | smpte170m -> bt709 |
| gear1_vs_gear5 | PASS | 300 / 300 | 0.000000 | 0.000000 | 0 | 6.781918 | 6.781918 | 0.000000 | smpte170m -> bt709 | smpte170m -> bt709 |

Each pair's directory has its delta report (delta.md, delta.json), the commands (cmd.txt) and both sides' raw outputs. environment.txt records the machine, the programs and the git state; SHA256SUMS covers every file.
