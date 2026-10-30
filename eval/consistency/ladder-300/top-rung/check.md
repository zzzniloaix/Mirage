# Ladder check: mirage_vmaf --ladder against the pairs scored one by one

**PASS**: every pair of the ladder has the values of the same pair scored on its own.

- Ladder: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8; reference mode top-rung.
- Pairs file: eval/consistency/pairs/bipbop16x9.txt (sha256 7f772689e4e111d1c82d8dcc83aa2b5ac953a68a2d088c9a4a5ab3cf20f18b77).
- Settings given to every run: --frames 300 --scaler bicubic --model vmaf_v0.6.1.
- Compared, value for value, for each pair: scored_as, dis_conversion, ref_conversion, warnings, frames_scored, pooled, alignment, ref, dis, frames; and of each run: tool, libvmaf_version, libvmaf_package_version, ffmpeg_libraries, config_file, model, model_source, model_sha256, scaler, n_threads, n_subsample, frame_limit.
- mirage_vmaf: sha256 eab1651cc09237fce5451fd1de0c236c2d589b2d3b2d2848065b675e8e35a4c7.
- Code: commit 7fbaa8c56cf830471e9dc6ec802619a8390a072c; the working tree was clean at the start of the run and clean at its end (environment.txt).
- Run: 2026-10-24T02:03:24Z to 2026-10-24T02:04:11Z (UTC).

## The pairs

| pair | ladder pair | entries | frames, ladder / alone | the same | differing | frames that differ |
|---|---|---|---|---|---|---|
| gear4_vs_gear5 | 3 | 3 | 300 / 300 | SAME | - | 0 |
| gear3_vs_gear5 | 2 | 2 | 300 / 300 | SAME | - | 0 |
| gear2_vs_gear5 | 1 | 1 | 300 / 300 | SAME | - | 0 |
| gear1_vs_gear5 | 0 | 0 | 300 / 300 | SAME | - | 0 |

## The ladder

Master playlist https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8, read from https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8, sha256 abccb34314f7228107ed03077c5150424a3aecff0275767a6b7f64c0fd99ff36. Reference (top-rung): https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear5/prog_index.m3u8.

| entry | line | URI | BANDWIDTH | RESOLUTION | CODECS | video | role |
|---|---|---|---|---|---|---|---|
| 0 | 17 | gear1/prog_index.m3u8 | 263851 | 416x234 | mp4a.40.2, avc1.4d400d | yes | pair 0 |
| 1 | 21 | gear2/prog_index.m3u8 | 577610 | 640x360 | mp4a.40.2, avc1.4d401e | yes | pair 1 |
| 2 | 25 | gear3/prog_index.m3u8 | 915905 | 960x540 | mp4a.40.2, avc1.4d401f | yes | pair 2 |
| 3 | 29 | gear4/prog_index.m3u8 | 1030138 | 1280x720 | mp4a.40.2, avc1.4d401f | yes | pair 3 |
| 4 | 33 | gear5/prog_index.m3u8 | 1924009 | 1920x1080 | mp4a.40.2, avc1.4d401f | yes | reference |
| 5 | 37 | gear0/prog_index.m3u8 | 41457 | - | mp4a.40.2 | no | skipped: no video |

| ladder pair | entries | distorted | frames scored | pooled mean |
|---|---|---|---|---|
| 0 | 0 | https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear1/prog_index.m3u8 | 300 | 6.781918 |
| 1 | 1 | https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear2/prog_index.m3u8 | 300 | 36.817663 |
| 2 | 2 | https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear3/prog_index.m3u8 | 300 | 64.843002 |
| 3 | 3 | https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear4/prog_index.m3u8 | 300 | 84.258060 |

Warnings of the ladder report: none.

Each run's directory has its command (cmd.txt), mirage_vmaf's report (mirage.json) and its output; environment.txt records the machine, the program and the git state; SHA256SUMS covers every file.
