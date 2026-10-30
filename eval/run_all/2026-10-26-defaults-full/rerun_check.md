# Rerun of the committed Phase 1 measurements, against the committed outputs

`eval/run_all.py`, 2026-10-26T21:20:40Z to 2026-10-26T22:06:00Z; repository at 1f7d42d23dc5fefa33409d5624941247e00e9d67, working tree clean at the start and end.

**Everything compared is as committed.**

| step | ran | as committed | compared |
|---|---|---|---|
| defaults-full | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| pooling-defaults-full | yes | yes | every series and skipped entry of pooling.json |

- mirage_vmaf `head` (every run made after D-016 but format-cases-after and align-cases-before): built from commit 1f7d42d23dc5fefa33409d5624941247e00e9d67, sha256 `217cc2948bf361aa11a37f4aea467ffe1caa30ab251943ba18b72c55005c6f10`.

## defaults-full

mirage_vmaf sha256 `217cc2948bf361aa11a37f4aea467ffe1caa30ab251943ba18b72c55005c6f10`; the committed runs' (their build.txt): `9f04085f73440a9c9969f9bf6a86767eaf9ade89ae8ca393f8b6c99bb78b9b06`, a different build; the table shows whether it scores alike.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| plain | gear4_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |
| plain | gear3_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |
| plain | gear2_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |
| plain | gear1_vs_gear5 | PASS / PASS | 53946 / 53946 | yes | 53946 / 53946 | yes |
