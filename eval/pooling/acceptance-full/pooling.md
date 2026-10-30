# Pooling definitions on committed per-frame scores

Work order section 1.5; the owner's decision #6 (D-004). Made by `eval/pooling/pooling.py` at commit dfadf66b331cdb0f8ba1e8d1f871a7c7d6f6b65a, working tree clean at the start and end of the run.

Percentile definitions (n frames, scores sorted ascending):
- **today**: sorted[floor(n*p/100)], counting from 0: the rule of the analyzer's vmaf_p5 (src/VMAFAnalyzer.cpp, sorted[n * 5 / 100]).
- **nearest-rank**: sorted[ceil(n*p/100) - 1], counting from 0: the nearest-rank method.
- **linear**: sorted[k] + (h - k) * (sorted[k + 1] - sorted[k]) with h = (n - 1) * p / 100 and k = floor(h), counting from 0: linear interpolation between the closest ranks (Hyndman and Fan's definition 7).

Harmonic mean, two forms:
- **shifted**: n / sum(1 / (x + 1)) - 1.
- **plain**: n / sum(1 / x), undefined when a frame scores 0.

## The run_delta.sh run eval/consistency/acceptance-full/plain

- input: `eval/consistency/acceptance-full/plain/delta.json` (sha256 4938b9a54ab94f2c1a6c36bf2445d67e73aa1488fcf7fcc3305b3b992de3d88f)
- input: `eval/consistency/acceptance-full/plain/gear4_vs_gear5/mirage.json.xz` (sha256 d4f8758364d088f0497e332ecf8aa4018eb961c88c8f2ba28452cd61ca8fa3cc)
- input: `eval/consistency/acceptance-full/plain/gear4_vs_gear5/ffmpeg.json.xz` (sha256 589d3f51dc6887b0c102976670f8b49b36f0dc3e2edf30ad326b8f3ff28fd00f)
- input: `eval/consistency/acceptance-full/plain/gear3_vs_gear5/mirage.json.xz` (sha256 ae1d2c96c101e855f582b4acaf6223eb1e92a56f6c7a178973abaf077763c032)
- input: `eval/consistency/acceptance-full/plain/gear3_vs_gear5/ffmpeg.json.xz` (sha256 8acd5ec015d6e24e4765004354a5d65469013b9b768a2a8045552ff91b1c27ca)
- input: `eval/consistency/acceptance-full/plain/gear2_vs_gear5/mirage.json.xz` (sha256 0bdfedb95ba15f4b2798f474ceea41279c5b289f7b7217fc1e755b7803bf6129)
- input: `eval/consistency/acceptance-full/plain/gear2_vs_gear5/ffmpeg.json.xz` (sha256 4aef79ea671b48c5fa069c66dfa483fe1c399d20fbd7c2fd381ddb39f821f361)
- input: `eval/consistency/acceptance-full/plain/gear1_vs_gear5/mirage.json.xz` (sha256 826715f09c8c0793d116905ece02f532492efd71a79baeee61b13bbb1dbacdcc)
- input: `eval/consistency/acceptance-full/plain/gear1_vs_gear5/ffmpeg.json.xz` (sha256 0cdcb524ebec7ca0a664a4742e824003d17cee336041bbc277d58352673efc20)

### Minimum and the first frame

| series | frames | mean | min | first index at the min | frames at the min | first frame's score | min without the first frame (index) |
|---|---|---|---|---|---|---|---|
| gear4_vs_gear5, Mirage | 53946 | 74.563207 | 69.746945 | 0 | 1 | 69.746945 | 73.172758 (7878) |
| gear4_vs_gear5, FFmpeg | 53946 | 74.563207 | 69.746945 | 0 | 1 | 69.746945 | 73.172758 (7878) |
| gear3_vs_gear5, Mirage | 53946 | 53.222012 | 45.124432 | 0 | 1 | 45.124432 | 48.441289 (25920) |
| gear3_vs_gear5, FFmpeg | 53946 | 53.222012 | 45.124432 | 0 | 1 | 45.124432 | 48.441289 (25920) |
| gear2_vs_gear5, Mirage | 53946 | 27.868640 | 16.070202 | 0 | 1 | 16.070202 | 18.126492 (28320) |
| gear2_vs_gear5, FFmpeg | 53946 | 27.868640 | 16.070202 | 0 | 1 | 16.070202 | 18.126492 (28320) |
| gear1_vs_gear5, Mirage | 53946 | 0.000827 | 0.000000 | 0 | 53790 | 0.000000 | 0.000000 (1) |
| gear1_vs_gear5, FFmpeg | 53946 | 0.000827 | 0.000000 | 0 | 53790 | 0.000000 | 0.000000 (1) |

### Percentiles

| series | percentile | today | nearest-rank | linear | largest difference |
|---|---|---|---|---|---|
| gear4_vs_gear5, Mirage | p1 | 73.631485 | 73.631485 | 73.631676 | 0.000191 |
| gear4_vs_gear5, Mirage | p5 | 73.889541 | 73.889541 | 73.889544 | 0.000003 |
| gear4_vs_gear5, Mirage | p10 | 74.040485 | 74.040485 | 74.040501 | 0.000016 |
| gear4_vs_gear5, Mirage | p25 | 74.295816 | 74.295816 | 74.295818 | 0.000002 |
| gear4_vs_gear5, Mirage | p50 | 74.580233 | 74.580230 | 74.580231 | 0.000003 |
| gear4_vs_gear5, FFmpeg | p1 | 73.631485 | 73.631485 | 73.631676 | 0.000191 |
| gear4_vs_gear5, FFmpeg | p5 | 73.889541 | 73.889541 | 73.889544 | 0.000003 |
| gear4_vs_gear5, FFmpeg | p10 | 74.040485 | 74.040485 | 74.040501 | 0.000016 |
| gear4_vs_gear5, FFmpeg | p25 | 74.295816 | 74.295816 | 74.295818 | 0.000002 |
| gear4_vs_gear5, FFmpeg | p50 | 74.580233 | 74.580230 | 74.580231 | 0.000003 |
| gear3_vs_gear5, Mirage | p1 | 49.461962 | 49.461962 | 49.462238 | 0.000276 |
| gear3_vs_gear5, Mirage | p5 | 52.056034 | 52.056034 | 52.056050 | 0.000016 |
| gear3_vs_gear5, Mirage | p10 | 52.428181 | 52.428181 | 52.428228 | 0.000047 |
| gear3_vs_gear5, Mirage | p25 | 52.869938 | 52.869938 | 52.869951 | 0.000014 |
| gear3_vs_gear5, Mirage | p50 | 53.312985 | 53.312932 | 53.312958 | 0.000053 |
| gear3_vs_gear5, FFmpeg | p1 | 49.461962 | 49.461962 | 49.462238 | 0.000276 |
| gear3_vs_gear5, FFmpeg | p5 | 52.056034 | 52.056034 | 52.056050 | 0.000016 |
| gear3_vs_gear5, FFmpeg | p10 | 52.428181 | 52.428181 | 52.428228 | 0.000047 |
| gear3_vs_gear5, FFmpeg | p25 | 52.869938 | 52.869938 | 52.869951 | 0.000014 |
| gear3_vs_gear5, FFmpeg | p50 | 53.312985 | 53.312932 | 53.312959 | 0.000053 |
| gear2_vs_gear5, Mirage | p1 | 19.607638 | 19.607638 | 19.607667 | 0.000029 |
| gear2_vs_gear5, Mirage | p5 | 26.339370 | 26.339370 | 26.339478 | 0.000107 |
| gear2_vs_gear5, Mirage | p10 | 26.888357 | 26.888357 | 26.888383 | 0.000026 |
| gear2_vs_gear5, Mirage | p25 | 27.500780 | 27.500780 | 27.500806 | 0.000026 |
| gear2_vs_gear5, Mirage | p50 | 28.098003 | 28.097961 | 28.097982 | 0.000042 |
| gear2_vs_gear5, FFmpeg | p1 | 19.607638 | 19.607638 | 19.607667 | 0.000029 |
| gear2_vs_gear5, FFmpeg | p5 | 26.339370 | 26.339370 | 26.339478 | 0.000108 |
| gear2_vs_gear5, FFmpeg | p10 | 26.888357 | 26.888357 | 26.888382 | 0.000025 |
| gear2_vs_gear5, FFmpeg | p25 | 27.500780 | 27.500780 | 27.500806 | 0.000026 |
| gear2_vs_gear5, FFmpeg | p50 | 28.098003 | 28.097961 | 28.097982 | 0.000042 |
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
| gear4_vs_gear5, Mirage | 74.563207 | 74.563207 | 74.561179 | 74.561151 | 0 | - |
| gear4_vs_gear5, FFmpeg | 74.563207 | 74.563207 | 74.561179 | 74.561151 | 0 | 74.561179 |
| gear3_vs_gear5, Mirage | 53.222012 | 53.222012 | 53.208585 | 53.208324 | 0 | - |
| gear3_vs_gear5, FFmpeg | 53.222012 | 53.222012 | 53.208585 | 53.208324 | 0 | 53.208585 |
| gear2_vs_gear5, Mirage | 27.868640 | 27.868640 | 27.751821 | 27.746387 | 0 | - |
| gear2_vs_gear5, FFmpeg | 27.868640 | 27.868640 | 27.751821 | 27.746387 | 0 | 27.751821 |
| gear1_vs_gear5, Mirage | 0.000827 | 0.000827 | 0.000574 | undefined | 53790 | - |
| gear1_vs_gear5, FFmpeg | 0.000827 | 0.000827 | 0.000574 | undefined | 53790 | 0.000574 |

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
