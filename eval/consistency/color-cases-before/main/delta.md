# VMAF consistency check: Mirage vs FFmpeg's libvmaf filter

**Overall: FAIL**: 9 of 18 pairs fail.

- Variant: plain FFmpeg: the section 5 comparison.
- Frames: the first 10 frame pairs of each pair (--frames 10).
- A frame fails if \|delta\| > 0.01, with delta = Mirage - FFmpeg (docs/DECISIONS.md D-019). A pair passes only if both sides ran to the end, used the same settings, scored the same number of frames and no frame fails.
- Settings given to FFmpeg: model vmaf_v0.6.1, scaler bilinear, n_threads 4.
- mirage_vmaf: sha256 4bb0fc2bec6bd240463e3f183204c426bbff1652eadd342733917d8fc58f84c0; libvmaf 3.0.0 at run time, package 3.1.0.
- FFmpeg: ffmpeg version 8.1.1 Copyright (c) 2000-2026 the FFmpeg developers; libvmaf package 3.1.0.
- Machine: macOS 26.2 (25C56), Apple M3 Pro.
- Code: commit 5e412a698130555640e9d70f0f89d68f7fb892bc; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-23T11:12:53Z to 2026-10-23T11:13:04Z (UTC).

| pair | status | frame pairs (Mirage / FFmpeg) | max \|delta\| | mean \|delta\| | frames over | pooled mean Mirage | pooled mean FFmpeg | difference | FFmpeg's scale: distorted csp |
|---|---|---|---|---|---|---|---|---|---|
| m01_bt709_vs_smpte170m | FAIL | 10 / 10 | 11.884643 | 10.534200 | 10 | 65.633871 | 76.168071 | -10.534200 | smpte170m -> bt709 |
| m02_bt709_vs_bt709 | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 |
| m03_bt709_vs_untagged | FAIL | 10 / 10 | 11.884643 | 10.534200 | 10 | 65.633871 | 76.168071 | -10.534200 | unknown -> bt709 |
| m04_untagged_vs_bt709 | FAIL | 10 / 10 | 26.462820 | 25.555489 | 10 | 65.633871 | 40.078382 | 25.555489 | bt709 -> unknown |
| m05_untagged_vs_smpte170m | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | smpte170m -> unknown |
| m06_untagged_vs_untagged | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | unknown -> unknown |
| m07_smpte170m_vs_bt470bg | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt470bg -> smpte170m |
| m08_bt2020nc_vs_bt709 | FAIL | 10 / 10 | 3.971636 | 3.650647 | 10 | 65.633871 | 61.983224 | 3.650647 | bt709 -> bt2020nc |
| r01_tv_vs_pc | FAIL | 10 / 10 | 20.969975 | 20.883371 | 10 | 65.633871 | 44.750501 | 20.883370 | bt709 -> bt709 |
| r02_pc_vs_tv | FAIL | 10 / 10 | 22.748280 | 22.400261 | 10 | 65.633871 | 88.034133 | -22.400262 | bt709 -> bt709 |
| r03_norange_vs_pc | FAIL | 10 / 10 | 20.969975 | 20.883371 | 10 | 65.633871 | 44.750501 | 20.883370 | bt709 -> bt709 |
| r04_pc_vs_norange | FAIL | 10 / 10 | 22.748280 | 22.400261 | 10 | 65.633871 | 88.034133 | -22.400262 | bt709 -> bt709 |
| r05_norange_vs_tv | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 |
| r06_tv_vs_norange | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 |
| x01_bt709tv_vs_smpte170mpc | FAIL | 10 / 10 | 3.846347 | 2.768631 | 10 | 65.633871 | 62.865240 | 2.768631 | smpte170m -> bt709 |
| x02_primaries_transfer | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 |
| x03_chroma_topleft | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 |
| x04_interlaced_tff | PASS | 10 / 10 | 0.000000 | 0.000000 | 0 | 65.633871 | 65.633871 | 0.000000 | bt709 -> bt709 |

Why pairs fail:
- m01_bt709_vs_smpte170m: 10 of 10 frames have \|delta\| > 0.01
- m03_bt709_vs_untagged: 10 of 10 frames have \|delta\| > 0.01
- m04_untagged_vs_bt709: 10 of 10 frames have \|delta\| > 0.01
- m08_bt2020nc_vs_bt709: 10 of 10 frames have \|delta\| > 0.01
- r01_tv_vs_pc: 10 of 10 frames have \|delta\| > 0.01
- r02_pc_vs_tv: 10 of 10 frames have \|delta\| > 0.01
- r03_norange_vs_pc: 10 of 10 frames have \|delta\| > 0.01
- r04_pc_vs_norange: 10 of 10 frames have \|delta\| > 0.01
- x01_bt709tv_vs_smpte170mpc: 10 of 10 frames have \|delta\| > 0.01

Each pair's directory has its delta report (delta.md, delta.json), the commands (cmd.txt) and both sides' raw outputs. environment.txt records the machine, the programs and the git state; SHA256SUMS covers every file.
