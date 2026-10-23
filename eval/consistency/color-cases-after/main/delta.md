# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter

**Overall: PASS**: all 18 pairs pass.

- Variant: plain FFmpeg: the section 5 comparison.
- Frames: the first 10 frame pairs of each pair (--frames 10).
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, used the same settings, scored the same number of frames and no frame fails.
- Settings given to FFmpeg: model vmaf_v0.6.1, scaler bilinear, n_threads 4.
- mirage_vmaf: sha256 420b8776e9708329b2f32cf6d2b0f924ee67db6f82f3e05d8e172d28cd243acd; libvmaf 3.0.0 at run time, package 3.1.0.
- FFmpeg: ffmpeg version 8.1.1 Copyright (c) 2000-2026 the FFmpeg developers; libvmaf package 3.1.0.
- Machine: macOS 26.2 (25C56), Apple M3 Pro.
- Code: commit 5263061c3dfa7b3134763669443977b436ab1003; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-23T11:27:50Z to 2026-10-23T11:28:01Z (UTC).

| pair | status | frame pairs (Mirage / FFmpeg) | max \|delta\| | mean \|delta\| | frames over | pooled mean Mirage | pooled mean FFmpeg | difference | FFmpeg's scale: distorted csp | Mirage: distorted csp |
|---|---|---|---|---|---|---|---|---|---|---|
| m01_bt709_vs_smpte170m | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 76.168071 | 76.168071 | 0.000000 | smpte170m -> bt709 | smpte170m -> bt709 |
| m02_bt709_vs_bt709 | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| m03_bt709_vs_untagged | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 76.168071 | 76.168071 | 0.000000 | unknown -> bt709 | unknown -> bt709 |
| m04_untagged_vs_bt709 | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 40.078382 | 40.078382 | 0.000000 | bt709 -> unknown | bt709 -> unknown |
| m05_untagged_vs_smpte170m | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | smpte170m -> unknown | smpte170m -> unknown |
| m06_untagged_vs_untagged | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | unknown -> unknown | unknown -> unknown |
| m07_smpte170m_vs_bt470bg | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt470bg -> smpte170m | bt470bg -> smpte170m |
| m08_bt2020nc_vs_bt709 | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 61.983224 | 61.983224 | 0.000000 | bt709 -> bt2020nc | bt709 -> bt2020nc |
| r01_tv_vs_pc | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 44.750501 | 44.750501 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| r02_pc_vs_tv | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 88.034133 | 88.034133 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| r03_norange_vs_pc | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 44.750501 | 44.750501 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| r04_pc_vs_norange | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 88.034133 | 88.034133 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| r05_norange_vs_tv | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| r06_tv_vs_norange | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| x01_bt709tv_vs_smpte170mpc | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 62.865240 | 62.865240 | 0.000000 | smpte170m -> bt709 | smpte170m -> bt709 |
| x02_primaries_transfer | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| x03_chroma_topleft | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |
| x04_interlaced_tff | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 | bt709 -> bt709 |

Each pair's directory has its delta report (delta.md, delta.json), the commands (cmd.txt) and both sides' raw outputs. environment.txt records the machine, the programs and the git state; SHA256SUMS covers every file.
