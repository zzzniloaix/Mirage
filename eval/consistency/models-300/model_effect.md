# Score shifts against the vmaf_v0.6.1 run

Each run below is an `eval/consistency/run_delta.sh` run over the same pairs; a shift is that run's score minus the base run's, per frame, over the frames both scored. Mirage's side are `mirage_vmaf`'s reports (full precision), FFmpeg's its libvmaf logs (6 decimals). Made by `eval/consistency/compare_runs.py`.

| run | directory | status (Mirage vs FFmpeg) | model | model_file |
|---|---|---|---|---|
| vmaf_v0.6.1 (base) | `vmaf_v0.6.1` | PASS | vmaf_v0.6.1 | - |
| vmaf_v0.6.1neg | `vmaf_v0.6.1neg` | PASS | vmaf_v0.6.1neg | - |
| vmaf_4k_v0.6.1 | `vmaf_4k_v0.6.1` | PASS | vmaf_4k_v0.6.1 | - |
| file_vmaf_v0.6.1 | `file_vmaf_v0.6.1` | PASS | - | /opt/homebrew/opt/libvmaf/share/libvmaf/model/vmaf_v0.6.1.json (sha256 5950d61fa1f861bd45d8149d80539ed9f3376cfc2495b8f0fa8e9f57cb131ee3) |

Parameters all runs share: variant plain, frames 300, threshold 0.01, scaler bilinear, threads 4.
mirage_vmaf: sha256 3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c; commit 8cc06a7c6939660e53097b948f6a78c068e8faee.

## Mirage against FFmpeg in each run

From each run's own `delta.json` (\|delta\| = \|Mirage - FFmpeg\| per frame).

| pair | run | status | max \|delta\| | frames over the threshold |
|---|---|---|---|---|
| gear2_vs_gear5 | vmaf_v0.6.1 | PASS | 0.000000 | 0 |
| gear2_vs_gear5 | vmaf_v0.6.1neg | PASS | 0.000000 | 0 |
| gear2_vs_gear5 | vmaf_4k_v0.6.1 | PASS | 0.000000 | 0 |
| gear2_vs_gear5 | file_vmaf_v0.6.1 | PASS | 0.000000 | 0 |

## How far Mirage's scores move from the vmaf_v0.6.1 run's

| pair | run | frames compared | frames up / same / down | max \|shift\| (index) | mean shift | mean \|shift\| | pooled mean, base | pooled mean, run | pooled mean shift |
|---|---|---|---|---|---|---|---|---|---|
| gear2_vs_gear5 | vmaf_v0.6.1neg | 300 | 0 / 0 / 300 | 10.384560 (31) | -9.054533 | 9.054533 | 28.552935 | 19.498402 | -9.054533 |
| gear2_vs_gear5 | vmaf_4k_v0.6.1 | 300 | 300 / 0 / 0 | 22.251285 (0) | 18.889440 | 18.889440 | 28.552935 | 47.442375 | 18.889440 |
| gear2_vs_gear5 | file_vmaf_v0.6.1 | 300 | 0 / 300 / 0 | 0.000000 (0) | 0.000000 | 0.000000 | 28.552935 | 28.552935 | 0.000000 |

## How far FFmpeg's scores move from the vmaf_v0.6.1 run's

| pair | run | frames compared | frames up / same / down | max \|shift\| (index) | mean shift | mean \|shift\| | pooled mean, base | pooled mean, run | pooled mean shift |
|---|---|---|---|---|---|---|---|---|---|
| gear2_vs_gear5 | vmaf_v0.6.1neg | 300 | 0 / 0 / 300 | 10.384559 (31) | -9.054533 | 9.054533 | 28.552935 | 19.498402 | -9.054533 |
| gear2_vs_gear5 | vmaf_4k_v0.6.1 | 300 | 300 / 0 / 0 | 22.251285 (0) | 18.889440 | 18.889440 | 28.552935 | 47.442375 | 18.889440 |
| gear2_vs_gear5 | file_vmaf_v0.6.1 | 300 | 0 / 300 / 0 | 0.000000 (0) | 0.000000 | 0.000000 | 28.552935 | 28.552935 | 0.000000 |
