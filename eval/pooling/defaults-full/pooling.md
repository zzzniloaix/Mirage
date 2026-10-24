# Pooling definitions on committed per-frame scores

Work order section 1.5; the owner's decision #6 (D-004, made by D-027). Made by `eval/pooling/pooling.py` at commit 8e61d9567b0b42c89ed7b2778355828fa4ba57ba, working tree clean at the start and end of the run.

Percentile definitions (n frames, scores sorted ascending):
- **today**: sorted[floor(n*p/100)], counting from 0: the rule of the analyzer's vmaf_p5 until D-027 (src/VMAFAnalyzer.cpp, sorted[n * 5 / 100]).
- **nearest-rank**: sorted[ceil(n*p/100) - 1], counting from 0: the nearest-rank method.
- **linear**: sorted[k] + (h - k) * (sorted[k + 1] - sorted[k]) with h = (n - 1) * p / 100 and k = floor(h), counting from 0: linear interpolation between the closest ranks (Hyndman and Fan's definition 7).

Harmonic mean, two forms:
- **shifted**: n / sum(1 / (x + 1)) - 1.
- **plain**: n / sum(1 / x), undefined when a frame scores 0.

The pooled values of D-027, as mirage_vmaf computes them:
- **mean**: sum / n, summed in frame order (libvmaf's VMAF_POOL_METHOD_MEAN).
- **harmonic_mean**: n / sum(1 / (x + 1)) - 1, summed in frame order (libvmaf's VMAF_POOL_METHOD_HARMONIC_MEAN).
- **min**: the smallest score (libvmaf's VMAF_POOL_METHOD_MIN).
- **min_without_first_frame**: the smallest score of frames 1 to n - 1; null for one frame.
- **p1**: the linear definition.
- **p5**: the linear definition.
- **p10**: the linear definition.
- **p25**: the linear definition.
- **p50**: the linear definition.

## The run_delta.sh run eval/consistency/defaults-full/plain

- input: `eval/consistency/defaults-full/plain/delta.json` (sha256 1cc72b972fb3321d396f903fb84e9216829ae405c19d76c0a53303dcf03933eb)
- input: `eval/consistency/defaults-full/plain/gear4_vs_gear5/mirage.json.xz` (sha256 b213902a2db60fe1450bd3a09db3d16842770aa5d058d48d63e454d1ec4554f0)
- input: `eval/consistency/defaults-full/plain/gear4_vs_gear5/ffmpeg.json.xz` (sha256 c892d04b82fb0effa742e4943b985d6b25ca05794b5ed318e7a1b4f44f11c158)
- input: `eval/consistency/defaults-full/plain/gear3_vs_gear5/mirage.json.xz` (sha256 714b2351db15da404699c33a31d2c2a8fb52a0090c1bc3af00c038eb1e679c42)
- input: `eval/consistency/defaults-full/plain/gear3_vs_gear5/ffmpeg.json.xz` (sha256 63984282e2d3f7a014aca74f77c51d4d6b9ee2a54bace513de5e488714e922e7)
- input: `eval/consistency/defaults-full/plain/gear2_vs_gear5/mirage.json.xz` (sha256 00c91c1f168b2bbb9b290d7368f98fbe7e1bd8e619abe810008b90b1fb44336c)
- input: `eval/consistency/defaults-full/plain/gear2_vs_gear5/ffmpeg.json.xz` (sha256 52db5058d91e6394f38ee8037eec365ab08c16c73a5ba87c14e98fb768063f7a)
- input: `eval/consistency/defaults-full/plain/gear1_vs_gear5/mirage.json.xz` (sha256 03f531b1ef1de91feb3c32ec9a7a69f2d91652644c864f1035b35b1d66859e3b)
- input: `eval/consistency/defaults-full/plain/gear1_vs_gear5/ffmpeg.json.xz` (sha256 3abc6717049286db6ad3c5bb686dca29244974d804e3b19dbbbdc9b1ac11b3ae)

### Minimum and the first frame

| series | frames | mean | min | first index at the min | frames at the min | first frame's score | min without the first frame (index) |
|---|---|---|---|---|---|---|---|
| gear4_vs_gear5, Mirage | 53946 | 84.215790 | 79.613837 | 0 | 1 | 79.613837 | 82.494129 (8480) |
| gear4_vs_gear5, FFmpeg | 53946 | 84.215790 | 79.613837 | 0 | 1 | 79.613837 | 82.494129 (8480) |
| gear3_vs_gear5, Mirage | 53946 | 64.205911 | 56.844534 | 0 | 1 | 56.844534 | 60.167983 (52920) |
| gear3_vs_gear5, FFmpeg | 53946 | 64.205911 | 56.844534 | 0 | 1 | 56.844534 | 60.167983 (52920) |
| gear2_vs_gear5, Mirage | 53946 | 35.811040 | 23.488692 | 0 | 1 | 23.488692 | 25.172073 (7860) |
| gear2_vs_gear5, FFmpeg | 53946 | 35.811040 | 23.488692 | 0 | 1 | 23.488692 | 25.172073 (7860) |
| gear1_vs_gear5, Mirage | 53946 | 5.863899 | 0.000000 | 0 | 1799 | 0.000000 | 0.000000 (30) |
| gear1_vs_gear5, FFmpeg | 53946 | 5.863899 | 0.000000 | 0 | 1799 | 0.000000 | 0.000000 (30) |

### Percentiles

| series | percentile | today | nearest-rank | linear | largest difference |
|---|---|---|---|---|---|
| gear4_vs_gear5, Mirage | p1 | 83.086967 | 83.086967 | 83.087109 | 0.000142 |
| gear4_vs_gear5, Mirage | p5 | 83.402988 | 83.402988 | 83.403003 | 0.000015 |
| gear4_vs_gear5, Mirage | p10 | 83.581947 | 83.581947 | 83.581955 | 0.000008 |
| gear4_vs_gear5, Mirage | p25 | 83.882513 | 83.882513 | 83.882525 | 0.000013 |
| gear4_vs_gear5, Mirage | p50 | 84.230079 | 84.230050 | 84.230064 | 0.000029 |
| gear4_vs_gear5, FFmpeg | p1 | 83.086967 | 83.086967 | 83.087109 | 0.000142 |
| gear4_vs_gear5, FFmpeg | p5 | 83.402988 | 83.402988 | 83.403002 | 0.000015 |
| gear4_vs_gear5, FFmpeg | p10 | 83.581947 | 83.581947 | 83.581955 | 0.000008 |
| gear4_vs_gear5, FFmpeg | p25 | 83.882513 | 83.882513 | 83.882526 | 0.000013 |
| gear4_vs_gear5, FFmpeg | p50 | 84.230079 | 84.230050 | 84.230064 | 0.000029 |
| gear3_vs_gear5, Mirage | p1 | 61.216494 | 61.216494 | 61.216593 | 0.000099 |
| gear3_vs_gear5, Mirage | p5 | 62.826349 | 62.826349 | 62.826456 | 0.000108 |
| gear3_vs_gear5, Mirage | p10 | 63.221827 | 63.221827 | 63.221833 | 0.000005 |
| gear3_vs_gear5, Mirage | p25 | 63.754856 | 63.754856 | 63.754915 | 0.000059 |
| gear3_vs_gear5, Mirage | p50 | 64.281126 | 64.281100 | 64.281113 | 0.000026 |
| gear3_vs_gear5, FFmpeg | p1 | 61.216494 | 61.216494 | 61.216593 | 0.000099 |
| gear3_vs_gear5, FFmpeg | p5 | 62.826349 | 62.826349 | 62.826457 | 0.000108 |
| gear3_vs_gear5, FFmpeg | p10 | 63.221827 | 63.221827 | 63.221832 | 0.000006 |
| gear3_vs_gear5, FFmpeg | p25 | 63.754856 | 63.754856 | 63.754915 | 0.000059 |
| gear3_vs_gear5, FFmpeg | p50 | 64.281126 | 64.281100 | 64.281113 | 0.000026 |
| gear2_vs_gear5, Mirage | p1 | 26.860667 | 26.860667 | 26.861827 | 0.001160 |
| gear2_vs_gear5, Mirage | p5 | 33.982518 | 33.982518 | 33.982522 | 0.000005 |
| gear2_vs_gear5, Mirage | p10 | 34.620874 | 34.620874 | 34.621018 | 0.000144 |
| gear2_vs_gear5, Mirage | p25 | 35.353186 | 35.353186 | 35.353215 | 0.000029 |
| gear2_vs_gear5, Mirage | p50 | 36.064831 | 36.064739 | 36.064785 | 0.000092 |
| gear2_vs_gear5, FFmpeg | p1 | 26.860667 | 26.860667 | 26.861827 | 0.001160 |
| gear2_vs_gear5, FFmpeg | p5 | 33.982518 | 33.982518 | 33.982523 | 0.000005 |
| gear2_vs_gear5, FFmpeg | p10 | 34.620874 | 34.620874 | 34.621018 | 0.000144 |
| gear2_vs_gear5, FFmpeg | p25 | 35.353186 | 35.353186 | 35.353215 | 0.000029 |
| gear2_vs_gear5, FFmpeg | p50 | 36.064831 | 36.064739 | 36.064785 | 0.000092 |
| gear1_vs_gear5, Mirage | p1 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| gear1_vs_gear5, Mirage | p5 | 4.010107 | 4.010107 | 4.010176 | 0.000069 |
| gear1_vs_gear5, Mirage | p10 | 4.601197 | 4.601197 | 4.601259 | 0.000062 |
| gear1_vs_gear5, Mirage | p25 | 5.340533 | 5.340533 | 5.340558 | 0.000024 |
| gear1_vs_gear5, Mirage | p50 | 6.043060 | 6.043049 | 6.043055 | 0.000011 |
| gear1_vs_gear5, FFmpeg | p1 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| gear1_vs_gear5, FFmpeg | p5 | 4.010107 | 4.010107 | 4.010176 | 0.000069 |
| gear1_vs_gear5, FFmpeg | p10 | 4.601197 | 4.601197 | 4.601259 | 0.000062 |
| gear1_vs_gear5, FFmpeg | p25 | 5.340533 | 5.340533 | 5.340558 | 0.000025 |
| gear1_vs_gear5, FFmpeg | p50 | 6.043060 | 6.043049 | 6.043055 | 0.000011 |

### Means

| series | mean | libvmaf's pooled mean | harmonic, shifted | harmonic, plain | frames at or below 0 | libvmaf's harmonic mean |
|---|---|---|---|---|---|---|
| gear4_vs_gear5, Mirage | 84.215790 | 84.215790 | 84.213049 | 84.213016 | 0 | - |
| gear4_vs_gear5, FFmpeg | 84.215790 | 84.215790 | 84.213049 | 84.213016 | 0 | 84.213049 |
| gear3_vs_gear5, Mirage | 64.205911 | 64.205911 | 64.194902 | 64.194728 | 0 | - |
| gear3_vs_gear5, FFmpeg | 64.205911 | 64.205911 | 64.194902 | 64.194728 | 0 | 64.194902 |
| gear2_vs_gear5, Mirage | 35.811040 | 35.811040 | 35.705189 | 35.701575 | 0 | - |
| gear2_vs_gear5, FFmpeg | 35.811040 | 35.811040 | 35.705189 | 35.701575 | 0 | 35.705189 |
| gear1_vs_gear5, Mirage | 5.863899 | 5.863899 | 4.785794 | undefined | 1799 | - |
| gear1_vs_gear5, FFmpeg | 5.863899 | 5.863899 | 4.785794 | undefined | 1799 | 4.785794 |

### Motion of the first frame (FFmpeg's libvmaf log)

| series | integer_motion2, frame 0 | integer_motion, frame 0 | frames with integer_motion2 = 0 |
|---|---|---|---|
| gear4_vs_gear5, FFmpeg | 0.000000 | 0.000000 | 1 |
| gear3_vs_gear5, FFmpeg | 0.000000 | 0.000000 | 1 |
| gear2_vs_gear5, FFmpeg | 0.000000 | 0.000000 | 1 |
| gear1_vs_gear5, FFmpeg | 0.000000 | 0.000000 | 1 |

### The pooled values of D-027: mirage_vmaf's report against this script

Each value as mirage_vmaf computes it (src/VMAFPool.h), from the report's per-frame scores, against the report's own, both written in full (the shortest digits that read back as the same double).

| series | value | pooling.py | mirage_vmaf's report | equal |
|---|---|---|---|---|
| gear4_vs_gear5, Mirage | mean | 84.21578954441404 | 84.21578954441404 | yes |
| gear4_vs_gear5, Mirage | harmonic_mean | 84.21304882008998 | 84.21304882008998 | yes |
| gear4_vs_gear5, Mirage | min | 79.61383719363732 | 79.61383719363732 | yes |
| gear4_vs_gear5, Mirage | min_without_first_frame | 82.49412864741171 | 82.49412864741171 | yes |
| gear4_vs_gear5, Mirage | p1 | 83.08710894795334 | 83.08710894795334 | yes |
| gear4_vs_gear5, Mirage | p5 | 83.40300252315254 | 83.40300252315254 | yes |
| gear4_vs_gear5, Mirage | p10 | 83.58195547692783 | 83.58195547692783 | yes |
| gear4_vs_gear5, Mirage | p25 | 83.88252544511847 | 83.88252544511847 | yes |
| gear4_vs_gear5, Mirage | p50 | 84.23006443937152 | 84.23006443937152 | yes |
| gear3_vs_gear5, Mirage | mean | 64.20591109842762 | 64.20591109842762 | yes |
| gear3_vs_gear5, Mirage | harmonic_mean | 64.1949022900204 | 64.1949022900204 | yes |
| gear3_vs_gear5, Mirage | min | 56.84453353023001 | 56.84453353023001 | yes |
| gear3_vs_gear5, Mirage | min_without_first_frame | 60.16798310098584 | 60.16798310098584 | yes |
| gear3_vs_gear5, Mirage | p1 | 61.21659291994881 | 61.21659291994881 | yes |
| gear3_vs_gear5, Mirage | p5 | 62.82645646932036 | 62.82645646932036 | yes |
| gear3_vs_gear5, Mirage | p10 | 63.221832626720406 | 63.221832626720406 | yes |
| gear3_vs_gear5, Mirage | p25 | 63.75491458481124 | 63.75491458481124 | yes |
| gear3_vs_gear5, Mirage | p50 | 64.28111296367184 | 64.28111296367184 | yes |
| gear2_vs_gear5, Mirage | mean | 35.81103960566848 | 35.81103960566848 | yes |
| gear2_vs_gear5, Mirage | harmonic_mean | 35.70518858430351 | 35.70518858430351 | yes |
| gear2_vs_gear5, Mirage | min | 23.48869176425057 | 23.48869176425057 | yes |
| gear2_vs_gear5, Mirage | min_without_first_frame | 25.17207314947476 | 25.17207314947476 | yes |
| gear2_vs_gear5, Mirage | p1 | 26.861827464596292 | 26.861827464596292 | yes |
| gear2_vs_gear5, Mirage | p5 | 33.98252238537738 | 33.98252238537738 | yes |
| gear2_vs_gear5, Mirage | p10 | 34.6210181309377 | 34.6210181309377 | yes |
| gear2_vs_gear5, Mirage | p25 | 35.353215035207555 | 35.353215035207555 | yes |
| gear2_vs_gear5, Mirage | p50 | 36.06478506509353 | 36.06478506509353 | yes |
| gear1_vs_gear5, Mirage | mean | 5.863899146838388 | 5.863899146838388 | yes |
| gear1_vs_gear5, Mirage | harmonic_mean | 4.785793985391388 | 4.785793985391388 | yes |
| gear1_vs_gear5, Mirage | min | 0.0 | 0.0 | yes |
| gear1_vs_gear5, Mirage | min_without_first_frame | 0.0 | 0.0 | yes |
| gear1_vs_gear5, Mirage | p1 | 0.0 | 0.0 | yes |
| gear1_vs_gear5, Mirage | p5 | 4.010176382624578 | 4.010176382624578 | yes |
| gear1_vs_gear5, Mirage | p10 | 4.601259206576151 | 4.601259206576151 | yes |
| gear1_vs_gear5, Mirage | p25 | 5.340557614029432 | 5.340557614029432 | yes |
| gear1_vs_gear5, Mirage | p50 | 6.043054783321292 | 6.043054783321292 | yes |

### Checks

- gear4_vs_gear5, Mirage: recorded min equals min: yes
- gear4_vs_gear5, Mirage: mirage vmaf pooled values equal d027 at full precision: yes
- gear4_vs_gear5, FFmpeg: shifted harmonic mean equals libvmaf at 6 decimals: yes
- gear4_vs_gear5, FFmpeg: recorded min equals min: yes
- gear3_vs_gear5, Mirage: recorded min equals min: yes
- gear3_vs_gear5, Mirage: mirage vmaf pooled values equal d027 at full precision: yes
- gear3_vs_gear5, FFmpeg: shifted harmonic mean equals libvmaf at 6 decimals: yes
- gear3_vs_gear5, FFmpeg: recorded min equals min: yes
- gear2_vs_gear5, Mirage: recorded min equals min: yes
- gear2_vs_gear5, Mirage: mirage vmaf pooled values equal d027 at full precision: yes
- gear2_vs_gear5, FFmpeg: shifted harmonic mean equals libvmaf at 6 decimals: yes
- gear2_vs_gear5, FFmpeg: recorded min equals min: yes
- gear1_vs_gear5, Mirage: recorded min equals min: yes
- gear1_vs_gear5, Mirage: mirage vmaf pooled values equal d027 at full precision: yes
- gear1_vs_gear5, FFmpeg: shifted harmonic mean equals libvmaf at 6 decimals: yes
- gear1_vs_gear5, FFmpeg: recorded min equals min: yes
