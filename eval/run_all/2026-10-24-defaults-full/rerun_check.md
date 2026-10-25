# Rerun of the committed Phase 1 measurements, against the committed outputs

`eval/run_all.py`, 2026-10-25T00:08:21Z to 2026-10-25T01:00:48Z; repository at 5908e3b7391944899a3328a0dabdbcca7f77e7b9, working tree clean at the start and end.

**Everything compared is as committed.**

| step | ran | as committed | compared |
|---|---|---|---|
| defaults-full | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| pooling-defaults-full | yes | yes | every series and skipped entry of pooling.json |

- mirage_vmaf `head` (every run made after D-016 but format-cases-after and align-cases-before): built from commit 5908e3b7391944899a3328a0dabdbcca7f77e7b9, sha256 `9f04085f73440a9c9969f9bf6a86767eaf9ade89ae8ca393f8b6c99bb78b9b06`.

## defaults-full

mirage_vmaf sha256 `9f04085f73440a9c9969f9bf6a86767eaf9ade89ae8ca393f8b6c99bb78b9b06`; the committed runs' (their build.txt): `9f04085f73440a9c9969f9bf6a86767eaf9ade89ae8ca393f8b6c99bb78b9b06`, the same program.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| plain | gear4_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |
| plain | gear3_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |
| plain | gear2_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |
| plain | gear1_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |
