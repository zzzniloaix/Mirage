# Rerun of the committed Phase 1 measurements, against the committed outputs

`eval/run_all.py`, 2026-10-23T14:29:36Z to 2026-10-23T15:14:43Z; repository at e0beb49d42f3331681fad2d76a282ddf95fd8d7e, working tree clean at the start and end.

**Everything compared is as committed.**

| step | ran | as committed | compared |
|---|---|---|---|
| acceptance-full | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| pooling-full | yes | yes | every series and skipped entry of pooling.json |

- mirage_vmaf `head` (the runs made after D-016): built from commit e0beb49d42f3331681fad2d76a282ddf95fd8d7e, sha256 `3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c`.

## acceptance-full

mirage_vmaf sha256 `3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c`; the committed runs' (their build.txt): `3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c`, the same program.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| plain | gear4_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |
| plain | gear3_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |
| plain | gear2_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |
| plain | gear1_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |
