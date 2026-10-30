# Score shifts against the bilinear run

Each run below is an `eval/consistency/run_delta.sh` run over the same pairs; a shift is that run's score minus the base run's, per frame, over the frames both scored. Mirage's side are `mirage_vmaf`'s reports (full precision), FFmpeg's its libvmaf logs (6 decimals). Made by `eval/consistency/compare_runs.py`.

| run | directory | status (Mirage vs FFmpeg) | scaler |
|---|---|---|---|
| bilinear (base) | `bilinear` | PASS | bilinear |
| bicubic | `bicubic` | PASS | bicubic |
| lanczos | `lanczos` | PASS | lanczos |

Parameters all runs share: variant plain, frames 300, threshold 0.01, model vmaf_v0.6.1, model_file -, threads 4.
mirage_vmaf: sha256 3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c; commit 8cc06a7c6939660e53097b948f6a78c068e8faee.

## Mirage against FFmpeg in each run

From each run's own `delta.json` (\|delta\| = \|Mirage - FFmpeg\| per frame).

| pair | run | status | max \|delta\| | frames over the threshold |
|---|---|---|---|---|
| gear4_vs_gear5 | bilinear | PASS | 0.000000 | 0 |
| gear4_vs_gear5 | bicubic | PASS | 0.000000 | 0 |
| gear4_vs_gear5 | lanczos | PASS | 0.000000 | 0 |
| gear3_vs_gear5 | bilinear | PASS | 0.000000 | 0 |
| gear3_vs_gear5 | bicubic | PASS | 0.000000 | 0 |
| gear3_vs_gear5 | lanczos | PASS | 0.000000 | 0 |
| gear2_vs_gear5 | bilinear | PASS | 0.000000 | 0 |
| gear2_vs_gear5 | bicubic | PASS | 0.000000 | 0 |
| gear2_vs_gear5 | lanczos | PASS | 0.000000 | 0 |
| gear1_vs_gear5 | bilinear | PASS | 0.000000 | 0 |
| gear1_vs_gear5 | bicubic | PASS | 0.000000 | 0 |
| gear1_vs_gear5 | lanczos | PASS | 0.000000 | 0 |

## How far Mirage's scores move from the bilinear run's

| pair | run | frames compared | frames up / same / down | max \|shift\| (index) | mean shift | mean \|shift\| | pooled mean, base | pooled mean, run | pooled mean shift |
|---|---|---|---|---|---|---|---|---|---|
| gear4_vs_gear5 | bicubic | 300 | 300 / 0 / 0 | 10.070505 (240) | 9.617453 | 9.617453 | 74.640607 | 84.258060 | 9.617453 |
| gear4_vs_gear5 | lanczos | 300 | 300 / 0 / 0 | 10.105211 (240) | 9.202973 | 9.202973 | 74.640607 | 83.843581 | 9.202973 |
| gear3_vs_gear5 | bicubic | 300 | 300 / 0 / 0 | 12.066728 (30) | 11.113910 | 11.113910 | 53.729092 | 64.843002 | 11.113910 |
| gear3_vs_gear5 | lanczos | 300 | 300 / 0 / 0 | 14.121453 (30) | 13.334836 | 13.334836 | 53.729092 | 67.063929 | 13.334836 |
| gear2_vs_gear5 | bicubic | 300 | 300 / 0 / 0 | 8.804682 (101) | 8.264728 | 8.264728 | 28.552935 | 36.817663 | 8.264728 |
| gear2_vs_gear5 | lanczos | 300 | 300 / 0 / 0 | 11.215920 (101) | 10.471491 | 10.471491 | 28.552935 | 39.024426 | 10.471491 |
| gear1_vs_gear5 | bicubic | 300 | 290 / 10 / 0 | 9.142560 (41) | 6.775801 | 6.775801 | 0.006117 | 6.781918 | 6.775801 |
| gear1_vs_gear5 | lanczos | 300 | 291 / 9 / 0 | 11.607837 (41) | 8.874908 | 8.874908 | 0.006117 | 8.881025 | 8.874908 |

## How far FFmpeg's scores move from the bilinear run's

| pair | run | frames compared | frames up / same / down | max \|shift\| (index) | mean shift | mean \|shift\| | pooled mean, base | pooled mean, run | pooled mean shift |
|---|---|---|---|---|---|---|---|---|---|
| gear4_vs_gear5 | bicubic | 300 | 300 / 0 / 0 | 10.070506 (240) | 9.617453 | 9.617453 | 74.640607 | 84.258060 | 9.617453 |
| gear4_vs_gear5 | lanczos | 300 | 300 / 0 / 0 | 10.105212 (240) | 9.202973 | 9.202973 | 74.640607 | 83.843581 | 9.202974 |
| gear3_vs_gear5 | bicubic | 300 | 300 / 0 / 0 | 12.066729 (30) | 11.113910 | 11.113910 | 53.729092 | 64.843002 | 11.113910 |
| gear3_vs_gear5 | lanczos | 300 | 300 / 0 / 0 | 14.121454 (30) | 13.334836 | 13.334836 | 53.729092 | 67.063929 | 13.334837 |
| gear2_vs_gear5 | bicubic | 300 | 300 / 0 / 0 | 8.804682 (101) | 8.264728 | 8.264728 | 28.552935 | 36.817663 | 8.264728 |
| gear2_vs_gear5 | lanczos | 300 | 300 / 0 / 0 | 11.215920 (101) | 10.471491 | 10.471491 | 28.552935 | 39.024426 | 10.471491 |
| gear1_vs_gear5 | bicubic | 300 | 290 / 10 / 0 | 9.142560 (41) | 6.775801 | 6.775801 | 0.006117 | 6.781918 | 6.775801 |
| gear1_vs_gear5 | lanczos | 300 | 291 / 9 / 0 | 11.607836 (41) | 8.874908 | 8.874908 | 0.006117 | 8.881025 | 8.874908 |
