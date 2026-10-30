# Pooling definitions on committed per-frame scores

Work order section 1.5; the owner's decision #6 (D-004). Made by `eval/pooling/pooling.py` at commit 85229be227628710a7968c7912801864188cc98a, working tree clean at the start and end of the run.

Percentile definitions (n frames, scores sorted ascending):
- **today**: sorted[floor(n*p/100)], counting from 0: the rule of the analyzer's vmaf_p5 (src/VMAFAnalyzer.cpp, sorted[n * 5 / 100]).
- **nearest-rank**: sorted[ceil(n*p/100) - 1], counting from 0: the nearest-rank method.
- **linear**: sorted[k] + (h - k) * (sorted[k + 1] - sorted[k]) with h = (n - 1) * p / 100 and k = floor(h), counting from 0: linear interpolation between the closest ranks (Hyndman and Fan's definition 7).

Harmonic mean, two forms:
- **shifted**: n / sum(1 / (x + 1)) - 1.
- **plain**: n / sum(1 / x), undefined when a frame scores 0.

## The player's VMAF report eval/baseline/bipbop16x9/report.vmaf.json

- input: `eval/baseline/bipbop16x9/report.vmaf.json` (sha256 a7a05ab203962caf71fb88c49f80f1569b56755569add331ab0eebd1c1f268e2)
- skipped: '0x0  0.0 Mbps': cannot open distorted: https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/gear0/prog_index.m3u8

### Minimum and the first frame

| series | frames | mean | min | first index at the min | frames at the min | first frame's score | min without the first frame (index) |
|---|---|---|---|---|---|---|---|
| 1280x720  1.0 Mbps | 53946 | 74.5632 | 69.7469 | 0 | 1 | 69.7469 | 73.1728 (7878) |
| 960x540  0.9 Mbps | 53946 | 53.2220 | 45.1244 | 0 | 1 | 45.1244 | 48.4413 (25920) |
| 640x360  0.6 Mbps | 53946 | 33.0648 | 20.0523 | 0 | 1 | 20.0523 | 22.1403 (27720) |
| 416x234  0.3 Mbps | 53946 | 1.1455 | 0.0000 | 0 | 5976 | 0.0000 | 0.0000 (30) |

### Percentiles

| series | percentile | today | nearest-rank | linear | largest difference |
|---|---|---|---|---|---|
| 1280x720  1.0 Mbps | p1 | 73.6315 | 73.6315 | 73.6317 | 0.0002 |
| 1280x720  1.0 Mbps | p5 | 73.8895 | 73.8895 | 73.8895 | 0.0000 |
| 1280x720  1.0 Mbps | p10 | 74.0405 | 74.0405 | 74.0405 | 0.0000 |
| 1280x720  1.0 Mbps | p25 | 74.2958 | 74.2958 | 74.2958 | 0.0000 |
| 1280x720  1.0 Mbps | p50 | 74.5802 | 74.5802 | 74.5802 | 0.0000 |
| 960x540  0.9 Mbps | p1 | 49.4620 | 49.4620 | 49.4623 | 0.0003 |
| 960x540  0.9 Mbps | p5 | 52.0560 | 52.0560 | 52.0560 | 0.0000 |
| 960x540  0.9 Mbps | p10 | 52.4282 | 52.4282 | 52.4282 | 0.0001 |
| 960x540  0.9 Mbps | p25 | 52.8699 | 52.8699 | 52.8699 | 0.0000 |
| 960x540  0.9 Mbps | p50 | 53.3130 | 53.3129 | 53.3130 | 0.0001 |
| 640x360  0.6 Mbps | p1 | 23.5328 | 23.5328 | 23.5330 | 0.0002 |
| 640x360  0.6 Mbps | p5 | 31.4198 | 31.4198 | 31.4199 | 0.0001 |
| 640x360  0.6 Mbps | p10 | 32.0010 | 32.0010 | 32.0011 | 0.0001 |
| 640x360  0.6 Mbps | p25 | 32.6750 | 32.6750 | 32.6750 | 0.0000 |
| 640x360  0.6 Mbps | p50 | 33.3308 | 33.3308 | 33.3308 | 0.0000 |
| 416x234  0.3 Mbps | p1 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| 416x234  0.3 Mbps | p5 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| 416x234  0.3 Mbps | p10 | 0.0000 | 0.0000 | 0.0000 | 0.0000 |
| 416x234  0.3 Mbps | p25 | 0.5441 | 0.5441 | 0.5441 | 0.0000 |
| 416x234  0.3 Mbps | p50 | 1.1167 | 1.1167 | 1.1167 | 0.0000 |

### Means

| series | mean | libvmaf's pooled mean | harmonic, shifted | harmonic, plain | frames at or below 0 | libvmaf's harmonic mean |
|---|---|---|---|---|---|---|
| 1280x720  1.0 Mbps | 74.563207 | 74.563200 | 74.561179 | 74.561151 | 0 | - |
| 960x540  0.9 Mbps | 53.222012 | 53.222000 | 53.208585 | 53.208324 | 0 | - |
| 640x360  0.6 Mbps | 33.064821 | 33.064800 | 32.933315 | 32.928170 | 0 | - |
| 416x234  0.3 Mbps | 1.145503 | 1.145500 | 0.849321 | undefined | 5976 | - |

### Checks

- 1280x720  1.0 Mbps: recorded p5 equals today at 4 decimals: yes
- 1280x720  1.0 Mbps: recorded min equals min: yes
- 960x540  0.9 Mbps: recorded p5 equals today at 4 decimals: yes
- 960x540  0.9 Mbps: recorded min equals min: yes
- 640x360  0.6 Mbps: recorded p5 equals today at 4 decimals: yes
- 640x360  0.6 Mbps: recorded min equals min: yes
- 416x234  0.3 Mbps: recorded p5 equals today at 4 decimals: yes
- 416x234  0.3 Mbps: recorded min equals min: yes

## The run_delta.sh run eval/consistency/diag-after-300/plain

- input: `eval/consistency/diag-after-300/plain/delta.json` (sha256 4d04b15c648422935a383eddcd5e037f5e4802c6e4e0454fe62fc59155f9bf54)
- input: `eval/consistency/diag-after-300/plain/gear4_vs_gear5/mirage.json` (sha256 1ba78dcc29c088be3a44fa910d819e1f94a5d2054b4bce92b011a38cdb4793d1)
- input: `eval/consistency/diag-after-300/plain/gear4_vs_gear5/ffmpeg.json` (sha256 f183e4a48bd139b5a40f4bc6b6234701f3537cec206d2b2cd2f7695560631e99)
- input: `eval/consistency/diag-after-300/plain/gear3_vs_gear5/mirage.json` (sha256 38382095a808b00573cd8a66746cfca029ecf38a6385fdda6efeacd431c24fb0)
- input: `eval/consistency/diag-after-300/plain/gear3_vs_gear5/ffmpeg.json` (sha256 75a1dc57980c7250254ff6a28485cd4ffb504cc992fa9cdfb3c923f641f462ae)
- input: `eval/consistency/diag-after-300/plain/gear2_vs_gear5/mirage.json` (sha256 c04bc12c1c0c4a698983b33b6c26d01e627d01c6dcd3b819678fe94877e4c985)
- input: `eval/consistency/diag-after-300/plain/gear2_vs_gear5/ffmpeg.json` (sha256 3896480028daf975786fd7798072abdd35f06a072307954be427e471a6ac8201)
- input: `eval/consistency/diag-after-300/plain/gear1_vs_gear5/mirage.json` (sha256 b61d5e6cce6563f0123f40ac8635e22b74989f6e901cc9569054520690bf8393)
- input: `eval/consistency/diag-after-300/plain/gear1_vs_gear5/ffmpeg.json` (sha256 4d24aa927610e5f771a1375b062edd21537f45df1f9fc706253d8996345ef923)

### Minimum and the first frame

| series | frames | mean | min | first index at the min | frames at the min | first frame's score | min without the first frame (index) |
|---|---|---|---|---|---|---|---|
| gear4_vs_gear5, Mirage | 300 | 74.640607 | 69.746945 | 0 | 1 | 69.746945 | 73.669135 (82) |
| gear4_vs_gear5, FFmpeg | 300 | 74.640607 | 69.746945 | 0 | 1 | 69.746945 | 73.669135 (82) |
| gear3_vs_gear5, Mirage | 300 | 53.729092 | 45.124432 | 0 | 1 | 45.124432 | 49.706950 (120) |
| gear3_vs_gear5, FFmpeg | 300 | 53.729092 | 45.124432 | 0 | 1 | 45.124432 | 49.706950 (120) |
| gear2_vs_gear5, Mirage | 300 | 28.552935 | 16.070202 | 0 | 1 | 16.070202 | 19.906475 (120) |
| gear2_vs_gear5, FFmpeg | 300 | 28.552935 | 16.070202 | 0 | 1 | 16.070202 | 19.906475 (120) |
| gear1_vs_gear5, Mirage | 300 | 0.006117 | 0.000000 | 0 | 294 | 0.000000 | 0.000000 (1) |
| gear1_vs_gear5, FFmpeg | 300 | 0.006117 | 0.000000 | 0 | 294 | 0.000000 | 0.000000 (1) |

### Percentiles

| series | percentile | today | nearest-rank | linear | largest difference |
|---|---|---|---|---|---|
| gear4_vs_gear5, Mirage | p1 | 73.743339 | 73.727699 | 73.743183 | 0.015640 |
| gear4_vs_gear5, Mirage | p5 | 73.941519 | 73.935499 | 73.941218 | 0.006020 |
| gear4_vs_gear5, Mirage | p10 | 74.105106 | 74.098042 | 74.104400 | 0.007065 |
| gear4_vs_gear5, Mirage | p25 | 74.407214 | 74.403386 | 74.406257 | 0.003828 |
| gear4_vs_gear5, Mirage | p50 | 74.710834 | 74.706884 | 74.708859 | 0.003951 |
| gear4_vs_gear5, FFmpeg | p1 | 73.743339 | 73.727699 | 73.743183 | 0.015640 |
| gear4_vs_gear5, FFmpeg | p5 | 73.941519 | 73.935499 | 73.941218 | 0.006020 |
| gear4_vs_gear5, FFmpeg | p10 | 74.105106 | 74.098042 | 74.104400 | 0.007064 |
| gear4_vs_gear5, FFmpeg | p25 | 74.407214 | 74.403386 | 74.406257 | 0.003828 |
| gear4_vs_gear5, FFmpeg | p50 | 74.710834 | 74.706884 | 74.708859 | 0.003950 |
| gear3_vs_gear5, Mirage | p1 | 49.878380 | 49.795984 | 49.877556 | 0.082396 |
| gear3_vs_gear5, Mirage | p5 | 52.848080 | 52.776947 | 52.844524 | 0.071134 |
| gear3_vs_gear5, Mirage | p10 | 53.085068 | 53.084117 | 53.084973 | 0.000952 |
| gear3_vs_gear5, Mirage | p25 | 53.399721 | 53.399272 | 53.399609 | 0.000449 |
| gear3_vs_gear5, Mirage | p50 | 53.788941 | 53.788091 | 53.788516 | 0.000850 |
| gear3_vs_gear5, FFmpeg | p1 | 49.878380 | 49.795984 | 49.877556 | 0.082396 |
| gear3_vs_gear5, FFmpeg | p5 | 52.848080 | 52.776947 | 52.844523 | 0.071133 |
| gear3_vs_gear5, FFmpeg | p10 | 53.085068 | 53.084117 | 53.084973 | 0.000951 |
| gear3_vs_gear5, FFmpeg | p25 | 53.399721 | 53.399272 | 53.399609 | 0.000449 |
| gear3_vs_gear5, FFmpeg | p50 | 53.788941 | 53.788091 | 53.788516 | 0.000850 |
| gear2_vs_gear5, Mirage | p1 | 20.184416 | 20.090976 | 20.183481 | 0.093440 |
| gear2_vs_gear5, Mirage | p5 | 27.425721 | 27.425522 | 27.425711 | 0.000199 |
| gear2_vs_gear5, Mirage | p10 | 27.728431 | 27.701914 | 27.725780 | 0.026518 |
| gear2_vs_gear5, Mirage | p25 | 28.210361 | 28.202097 | 28.208295 | 0.008264 |
| gear2_vs_gear5, Mirage | p50 | 28.703177 | 28.702164 | 28.702670 | 0.001013 |
| gear2_vs_gear5, FFmpeg | p1 | 20.184416 | 20.090976 | 20.183482 | 0.093440 |
| gear2_vs_gear5, FFmpeg | p5 | 27.425721 | 27.425522 | 27.425711 | 0.000199 |
| gear2_vs_gear5, FFmpeg | p10 | 27.728431 | 27.701914 | 27.725779 | 0.026517 |
| gear2_vs_gear5, FFmpeg | p25 | 28.210361 | 28.202097 | 28.208295 | 0.008264 |
| gear2_vs_gear5, FFmpeg | p50 | 28.703177 | 28.702164 | 28.702670 | 0.001013 |
| gear1_vs_gear5, Mirage | p1 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| gear1_vs_gear5, Mirage | p5 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| gear1_vs_gear5, Mirage | p10 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| gear1_vs_gear5, Mirage | p25 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| gear1_vs_gear5, Mirage | p50 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| gear1_vs_gear5, FFmpeg | p1 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| gear1_vs_gear5, FFmpeg | p5 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| gear1_vs_gear5, FFmpeg | p10 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| gear1_vs_gear5, FFmpeg | p25 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| gear1_vs_gear5, FFmpeg | p50 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |

### Means

| series | mean | libvmaf's pooled mean | harmonic, shifted | harmonic, plain | frames at or below 0 | libvmaf's harmonic mean |
|---|---|---|---|---|---|---|
| gear4_vs_gear5, Mirage | 74.640607 | 74.640607 | 74.637697 | 74.637657 | 0 | - |
| gear4_vs_gear5, FFmpeg | 74.640607 | 74.640607 | 74.637697 | 74.637657 | 0 | 74.637697 |
| gear3_vs_gear5, Mirage | 53.729092 | 53.729092 | 53.714026 | 53.713721 | 0 | - |
| gear3_vs_gear5, FFmpeg | 53.729092 | 53.729092 | 53.714026 | 53.713721 | 0 | 53.714026 |
| gear2_vs_gear5, Mirage | 28.552935 | 28.552935 | 28.434599 | 28.428817 | 0 | - |
| gear2_vs_gear5, FFmpeg | 28.552935 | 28.552935 | 28.434599 | 28.428817 | 0 | 28.434599 |
| gear1_vs_gear5, Mirage | 0.006117 | 0.006117 | 0.004591 | undefined | 294 | - |
| gear1_vs_gear5, FFmpeg | 0.006117 | 0.006117 | 0.004591 | undefined | 294 | 0.004591 |

### Motion of the first frame (FFmpeg's libvmaf log)

| series | integer_motion2, frame 0 | integer_motion, frame 0 | frames with integer_motion2 = 0 |
|---|---|---|---|
| gear4_vs_gear5, FFmpeg | 0.000000 | 0.000000 | 1 |
| gear3_vs_gear5, FFmpeg | 0.000000 | 0.000000 | 1 |
| gear2_vs_gear5, FFmpeg | 0.000000 | 0.000000 | 1 |
| gear1_vs_gear5, FFmpeg | 0.000000 | 0.000000 | 1 |

### Checks

- gear4_vs_gear5, Mirage: recorded min equals min: yes
- gear4_vs_gear5, FFmpeg: shifted harmonic mean equals libvmaf at 6 decimals: yes
- gear4_vs_gear5, FFmpeg: recorded min equals min: yes
- gear3_vs_gear5, Mirage: recorded min equals min: yes
- gear3_vs_gear5, FFmpeg: shifted harmonic mean equals libvmaf at 6 decimals: yes
- gear3_vs_gear5, FFmpeg: recorded min equals min: yes
- gear2_vs_gear5, Mirage: recorded min equals min: yes
- gear2_vs_gear5, FFmpeg: shifted harmonic mean equals libvmaf at 6 decimals: yes
- gear2_vs_gear5, FFmpeg: recorded min equals min: yes
- gear1_vs_gear5, Mirage: recorded min equals min: yes
- gear1_vs_gear5, FFmpeg: shifted harmonic mean equals libvmaf at 6 decimals: yes
- gear1_vs_gear5, FFmpeg: recorded min equals min: yes
