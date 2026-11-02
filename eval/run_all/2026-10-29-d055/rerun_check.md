# Rerun of the committed Phase 1 measurements, against the committed outputs

`eval/run_all.py`, 2026-10-29T22:35:27Z to 2026-10-29T22:37:17Z; repository at e006f814c28315f514f334335d8f3a32b4ed1a0b, working tree clean at the start and end.

**Everything compared is as committed.**

| step | ran | as committed | compared |
|---|---|---|---|
| defaults-300 | yes | yes | each run's verdict, and every pair's verdict and per-frame scores on both sides |
| ladder-300 | yes | yes | the check's verdict, the ladder report's master and plan, and every per-frame score of the ladder's pairs and of the pairs scored one by one |
| pooling-defaults-300 | yes | yes | every series and skipped entry of pooling.json |

- mirage_vmaf `head` (every run made after D-016 but format-cases-after and align-cases-before): built from commit e006f814c28315f514f334335d8f3a32b4ed1a0b, sha256 `454db2ed6be26319975df2130b53fc4fd5a60ed6660cbdfef6278ba88dc8435b`.

## defaults-300

mirage_vmaf sha256 `454db2ed6be26319975df2130b53fc4fd5a60ed6660cbdfef6278ba88dc8435b`; the committed runs' (their build.txt): `85535c5c0ea56865816a71ea9aef837753e0a995d4892be51b863943f6854c5f`, a different build; the table shows whether it scores alike.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| plain | gear4_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| plain | gear3_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| plain | gear2_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |
| plain | gear1_vs_gear5 | PASS / PASS | 300 / 300 | yes | 300 / 300 | yes |

## ladder-300

mirage_vmaf sha256 `454db2ed6be26319975df2130b53fc4fd5a60ed6660cbdfef6278ba88dc8435b`; the committed runs' (their build.txt): `eab1651cc09237fce5451fd1de0c236c2d589b2d3b2d2848065b675e8e35a4c7`, a different build; the table shows whether it scores alike.

| run | pair | verdict, rerun / committed | Mirage frames, rerun / committed | Mirage scores the same | FFmpeg frames | FFmpeg metrics the same |
|---|---|---|---|---|---|---|
| top-rung | ladder pair 0 (entries 0) | scored / scored | 300 / 300 | yes | - | - |
| top-rung | ladder pair 1 (entries 1) | scored / scored | 300 / 300 | yes | - | - |
| top-rung | ladder pair 2 (entries 2) | scored / scored | 300 / 300 | yes | - | - |
| top-rung | ladder pair 3 (entries 3) | scored / scored | 300 / 300 | yes | - | - |
| top-rung | gear4_vs_gear5 | SAME / SAME | 300 / 300 | yes | - | - |
| top-rung | gear3_vs_gear5 | SAME / SAME | 300 / 300 | yes | - | - |
| top-rung | gear2_vs_gear5 | SAME / SAME | 300 / 300 | yes | - | - |
| top-rung | gear1_vs_gear5 | SAME / SAME | 300 / 300 | yes | - | - |
