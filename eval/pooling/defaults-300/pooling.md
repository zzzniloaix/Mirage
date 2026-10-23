# Pooling definitions on committed per-frame scores

Work order section 1.5; the owner's decision #6 (D-004, made by D-027). Made by `eval/pooling/pooling.py` at commit a0a905813d712d2a70d166547d49dbfc4d328278, working tree clean at the start and end of the run.

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

## The run_delta.sh run eval/consistency/defaults-300/plain

- input: `eval/consistency/defaults-300/plain/delta.json` (sha256 c9a61fc29438822adab88a49b9b53a657d6cc98fe22123ca46c04bd6e13e2ae5)
- input: `eval/consistency/defaults-300/plain/gear4_vs_gear5/mirage.json` (sha256 52a4bbe5b5e906db85dccc8362d49fa6cb0b6fe4768876c68def58325623cf2d)
- input: `eval/consistency/defaults-300/plain/gear4_vs_gear5/ffmpeg.json` (sha256 7c925bd4416158cf87c8b54145d6098afbd657118097212c8a2c8c3c5f00d185)
- input: `eval/consistency/defaults-300/plain/gear3_vs_gear5/mirage.json` (sha256 8e0edc2e7dfa80fd97842466bf355b47bf865b61aafb978995ced08e6e0dbce3)
- input: `eval/consistency/defaults-300/plain/gear3_vs_gear5/ffmpeg.json` (sha256 dc3a06dbe22025f0e1dc5d7f286abb7bfcf2eecfb5b00cfe8d247e41e5a39417)
- input: `eval/consistency/defaults-300/plain/gear2_vs_gear5/mirage.json` (sha256 bb05d059bc38b98c87ba33081829ffa132ff19e1b424601f6495a59219c74bac)
- input: `eval/consistency/defaults-300/plain/gear2_vs_gear5/ffmpeg.json` (sha256 0ffae81ee5db27b35bf3691bac6353401a30a853aa258467b46424922ec1197e)
- input: `eval/consistency/defaults-300/plain/gear1_vs_gear5/mirage.json` (sha256 08e50e04db8581e3a4e85ef0cd005b3db617d504c3c1cbdeb80f1ac69f70a34c)
- input: `eval/consistency/defaults-300/plain/gear1_vs_gear5/ffmpeg.json` (sha256 5f9b6611830cab12297baf759837a11ca17f5febbcc24c8a7bca5eb4be086958)

### Minimum and the first frame

| series | frames | mean | min | first index at the min | frames at the min | first frame's score | min without the first frame (index) |
|---|---|---|---|---|---|---|---|
| gear4_vs_gear5, Mirage | 300 | 84.258060 | 79.613837 | 0 | 1 | 79.613837 | 82.969001 (82) |
| gear4_vs_gear5, FFmpeg | 300 | 84.258060 | 79.613837 | 0 | 1 | 79.613837 | 82.969001 (82) |
| gear3_vs_gear5, Mirage | 300 | 64.843002 | 56.844534 | 0 | 1 | 56.844534 | 61.370429 (120) |
| gear3_vs_gear5, FFmpeg | 300 | 64.843002 | 56.844534 | 0 | 1 | 56.844534 | 61.370429 (120) |
| gear2_vs_gear5, Mirage | 300 | 36.817663 | 23.488692 | 0 | 1 | 23.488692 | 27.360344 (120) |
| gear2_vs_gear5, FFmpeg | 300 | 36.817663 | 23.488692 | 0 | 1 | 23.488692 | 27.360344 (120) |
| gear1_vs_gear5, Mirage | 300 | 6.781918 | 0.000000 | 0 | 10 | 0.000000 | 0.000000 (30) |
| gear1_vs_gear5, FFmpeg | 300 | 6.781918 | 0.000000 | 0 | 10 | 0.000000 | 0.000000 (30) |

### Percentiles

| series | percentile | today | nearest-rank | linear | largest difference |
|---|---|---|---|---|---|
| gear4_vs_gear5, Mirage | p1 | 83.063809 | 83.015146 | 83.063322 | 0.048663 |
| gear4_vs_gear5, Mirage | p5 | 83.434353 | 83.421597 | 83.433715 | 0.012756 |
| gear4_vs_gear5, Mirage | p10 | 83.639752 | 83.631022 | 83.638879 | 0.008729 |
| gear4_vs_gear5, Mirage | p25 | 83.959014 | 83.912095 | 83.947284 | 0.046919 |
| gear4_vs_gear5, Mirage | p50 | 84.341770 | 84.324656 | 84.333213 | 0.017114 |
| gear4_vs_gear5, FFmpeg | p1 | 83.063809 | 83.015146 | 83.063322 | 0.048663 |
| gear4_vs_gear5, FFmpeg | p5 | 83.434353 | 83.421597 | 83.433715 | 0.012756 |
| gear4_vs_gear5, FFmpeg | p10 | 83.639752 | 83.631022 | 83.638879 | 0.008730 |
| gear4_vs_gear5, FFmpeg | p25 | 83.959014 | 83.912095 | 83.947284 | 0.046919 |
| gear4_vs_gear5, FFmpeg | p50 | 84.341770 | 84.324656 | 84.333213 | 0.017114 |
| gear3_vs_gear5, Mirage | p1 | 61.563144 | 61.545450 | 61.562967 | 0.017694 |
| gear3_vs_gear5, Mirage | p5 | 63.882209 | 63.859727 | 63.881085 | 0.022481 |
| gear3_vs_gear5, Mirage | p10 | 64.067368 | 64.044346 | 64.065066 | 0.023021 |
| gear3_vs_gear5, Mirage | p25 | 64.434036 | 64.433156 | 64.433816 | 0.000881 |
| gear3_vs_gear5, Mirage | p50 | 64.901025 | 64.898474 | 64.899749 | 0.002551 |
| gear3_vs_gear5, FFmpeg | p1 | 61.563144 | 61.545450 | 61.562967 | 0.017694 |
| gear3_vs_gear5, FFmpeg | p5 | 63.882209 | 63.859727 | 63.881085 | 0.022482 |
| gear3_vs_gear5, FFmpeg | p10 | 64.067368 | 64.044346 | 64.065066 | 0.023022 |
| gear3_vs_gear5, FFmpeg | p25 | 64.434036 | 64.433156 | 64.433816 | 0.000880 |
| gear3_vs_gear5, FFmpeg | p50 | 64.901025 | 64.898474 | 64.899749 | 0.002551 |
| gear2_vs_gear5, Mirage | p1 | 27.562687 | 27.479643 | 27.561856 | 0.083044 |
| gear2_vs_gear5, Mirage | p5 | 35.381663 | 35.356847 | 35.380422 | 0.024815 |
| gear2_vs_gear5, Mirage | p10 | 35.856538 | 35.815672 | 35.852451 | 0.040866 |
| gear2_vs_gear5, Mirage | p25 | 36.433393 | 36.396685 | 36.424216 | 0.036708 |
| gear2_vs_gear5, Mirage | p50 | 37.072260 | 37.059194 | 37.065727 | 0.013067 |
| gear2_vs_gear5, FFmpeg | p1 | 27.562687 | 27.479643 | 27.561857 | 0.083044 |
| gear2_vs_gear5, FFmpeg | p5 | 35.381663 | 35.356847 | 35.380422 | 0.024816 |
| gear2_vs_gear5, FFmpeg | p10 | 35.856538 | 35.815672 | 35.852451 | 0.040866 |
| gear2_vs_gear5, FFmpeg | p25 | 36.433393 | 36.396685 | 36.424216 | 0.036708 |
| gear2_vs_gear5, FFmpeg | p50 | 37.072260 | 37.059194 | 37.065727 | 0.013066 |
| gear1_vs_gear5, Mirage | p1 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| gear1_vs_gear5, Mirage | p5 | 5.409786 | 5.393481 | 5.408971 | 0.016306 |
| gear1_vs_gear5, Mirage | p10 | 5.739043 | 5.707371 | 5.735876 | 0.031672 |
| gear1_vs_gear5, Mirage | p25 | 6.253595 | 6.249531 | 6.252579 | 0.004065 |
| gear1_vs_gear5, Mirage | p50 | 7.063352 | 7.060765 | 7.062059 | 0.002588 |
| gear1_vs_gear5, FFmpeg | p1 | 0.000000 | 0.000000 | 0.000000 | 0.000000 |
| gear1_vs_gear5, FFmpeg | p5 | 5.409786 | 5.393481 | 5.408971 | 0.016305 |
| gear1_vs_gear5, FFmpeg | p10 | 5.739043 | 5.707371 | 5.735876 | 0.031672 |
| gear1_vs_gear5, FFmpeg | p25 | 6.253595 | 6.249531 | 6.252579 | 0.004064 |
| gear1_vs_gear5, FFmpeg | p50 | 7.063352 | 7.060765 | 7.062059 | 0.002587 |

### Means

| series | mean | libvmaf's pooled mean | harmonic, shifted | harmonic, plain | frames at or below 0 | libvmaf's harmonic mean |
|---|---|---|---|---|---|---|
| gear4_vs_gear5, Mirage | 84.258060 | 84.258060 | 84.254740 | 84.254700 | 0 | - |
| gear4_vs_gear5, FFmpeg | 84.258060 | 84.258060 | 84.254740 | 84.254700 | 0 | 84.254740 |
| gear3_vs_gear5, Mirage | 64.843002 | 64.843002 | 64.830991 | 64.830796 | 0 | - |
| gear3_vs_gear5, FFmpeg | 64.843002 | 64.843002 | 64.830991 | 64.830796 | 0 | 64.830991 |
| gear2_vs_gear5, Mirage | 36.817663 | 36.817663 | 36.714504 | 36.710893 | 0 | - |
| gear2_vs_gear5, FFmpeg | 36.817663 | 36.817663 | 36.714504 | 36.710893 | 0 | 36.714504 |
| gear1_vs_gear5, Mirage | 6.781918 | 6.781918 | 5.439755 | undefined | 10 | - |
| gear1_vs_gear5, FFmpeg | 6.781918 | 6.781918 | 5.439755 | undefined | 10 | 5.439755 |

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
| gear4_vs_gear5, Mirage | mean | 84.25806034787456 | 84.25806034787456 | yes |
| gear4_vs_gear5, Mirage | harmonic_mean | 84.25474044599248 | 84.25474044599248 | yes |
| gear4_vs_gear5, Mirage | min | 79.61383719363732 | 79.61383719363732 | yes |
| gear4_vs_gear5, Mirage | min_without_first_frame | 82.96900123118027 | 82.96900123118027 | yes |
| gear4_vs_gear5, Mirage | p1 | 83.06332214560709 | 83.06332214560709 | yes |
| gear4_vs_gear5, Mirage | p5 | 83.43371510695411 | 83.43371510695411 | yes |
| gear4_vs_gear5, Mirage | p10 | 83.63887864587936 | 83.63887864587936 | yes |
| gear4_vs_gear5, Mirage | p25 | 83.94728432428214 | 83.94728432428214 | yes |
| gear4_vs_gear5, Mirage | p50 | 84.33321313174153 | 84.33321313174153 | yes |
| gear3_vs_gear5, Mirage | mean | 64.84300242620918 | 64.84300242620918 | yes |
| gear3_vs_gear5, Mirage | harmonic_mean | 64.83099106400168 | 64.83099106400168 | yes |
| gear3_vs_gear5, Mirage | min | 56.84453353023001 | 56.84453353023001 | yes |
| gear3_vs_gear5, Mirage | min_without_first_frame | 61.370428813368356 | 61.370428813368356 | yes |
| gear3_vs_gear5, Mirage | p1 | 61.562967176307566 | 61.562967176307566 | yes |
| gear3_vs_gear5, Mirage | p5 | 63.88108462155041 | 63.88108462155041 | yes |
| gear3_vs_gear5, Mirage | p10 | 64.06506553364387 | 64.06506553364387 | yes |
| gear3_vs_gear5, Mirage | p25 | 64.43381624055772 | 64.43381624055772 | yes |
| gear3_vs_gear5, Mirage | p50 | 64.89974942984854 | 64.89974942984854 | yes |
| gear2_vs_gear5, Mirage | mean | 36.81766319253449 | 36.81766319253449 | yes |
| gear2_vs_gear5, Mirage | harmonic_mean | 36.714504032332385 | 36.714504032332385 | yes |
| gear2_vs_gear5, Mirage | min | 23.48869176425057 | 23.48869176425057 | yes |
| gear2_vs_gear5, Mirage | min_without_first_frame | 27.36034402228314 | 27.36034402228314 | yes |
| gear2_vs_gear5, Mirage | p1 | 27.561856061293092 | 27.561856061293092 | yes |
| gear2_vs_gear5, Mirage | p5 | 35.380421992925115 | 35.380421992925115 | yes |
| gear2_vs_gear5, Mirage | p10 | 35.85245130499851 | 35.85245130499851 | yes |
| gear2_vs_gear5, Mirage | p25 | 36.42421603002845 | 36.42421603002845 | yes |
| gear2_vs_gear5, Mirage | p50 | 37.065727226214364 | 37.065727226214364 | yes |
| gear1_vs_gear5, Mirage | mean | 6.781917868878007 | 6.781917868878007 | yes |
| gear1_vs_gear5, Mirage | harmonic_mean | 5.4397552874537904 | 5.4397552874537904 | yes |
| gear1_vs_gear5, Mirage | min | 0.0 | 0.0 | yes |
| gear1_vs_gear5, Mirage | min_without_first_frame | 0.0 | 0.0 | yes |
| gear1_vs_gear5, Mirage | p1 | 0.0 | 0.0 | yes |
| gear1_vs_gear5, Mirage | p5 | 5.408971200598931 | 5.408971200598931 | yes |
| gear1_vs_gear5, Mirage | p10 | 5.735875609447621 | 5.735875609447621 | yes |
| gear1_vs_gear5, Mirage | p25 | 6.25257925520248 | 6.25257925520248 | yes |
| gear1_vs_gear5, Mirage | p50 | 7.062058614957513 | 7.062058614957513 | yes |

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
