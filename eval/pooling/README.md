# Pooling definitions (work order §1.5)

Work order §1.5 asks for `mean`, `harmonic_mean`, `min` and `p1` / `p5` / `p10` / `p25` / `p50`. How they are defined is the owner's decision #6 (`docs/DECISIONS.md` D-004), so Mirage's pooling is unchanged: `mirage_vmaf` reports libvmaf's pooled mean and min, and the player's analyzer also computes a `vmaf_p5` itself. `pooling.py` measures what the candidate definitions give on per-frame scores already committed in the repository. The decision package is in `docs/PHASE1_DECISIONS_PENDING.md`.

## Running it

```
python3 -I -B eval/pooling/pooling.py (--gui-report <report.vmaf.json> | --delta-run <run_delta output dir>)... --out <prefix>
python3 -I -B -m unittest discover -s eval/pooling/tests -v
```

- `--gui-report`: a report the player's analyzer writes (`write_json`), whose per-frame scores have 4 decimals.
- `--delta-run`: an `eval/consistency/run_delta.sh` output directory. For each pair it reads `mirage_vmaf`'s report (scores at full precision, libvmaf's pooled mean and min) and FFmpeg's libvmaf log (scores at 6 decimals; libvmaf's pooled min, max, mean and harmonic mean; each frame's features).
- It writes `<prefix>.json` and `<prefix>.md`, with the SHA-256 of every input and the git HEAD and working tree state at the start and end of the run. Python 3 standard library only.

For every series of per-frame scores it gives:

- the mean, the minimum, the first index at the minimum, how many frames score the minimum, the first frame's score, and the minimum without the first frame;
- p1, p5, p10, p25 and p50 under three definitions: **today**, `sorted[floor(n*p/100)]`, the rule of the analyzer's `vmaf_p5` (`src/VMAFAnalyzer.cpp`); **nearest-rank**, `sorted[ceil(n*p/100) - 1]`; **linear**, interpolation between the closest ranks with `h = (n - 1) * p / 100` (Hyndman and Fan's definition 7, also `statistics.quantiles(..., method="inclusive")`);
- the harmonic mean in two forms, `n / sum(1 / (x + 1)) - 1` (**shifted**) and `n / sum(1 / x)` (**plain**, undefined when a frame scores 0), next to libvmaf's own harmonic mean where FFmpeg's log has it;
- from FFmpeg's log, the first frame's `integer_motion2` and `integer_motion`, and how many frames have `integer_motion2` 0.

## `bipbop16x9/`: the Phase 0 baseline and the 300 frames after D-016

```
python3 -I -B eval/pooling/pooling.py --gui-report eval/baseline/bipbop16x9/report.vmaf.json --delta-run eval/consistency/diag-after-300/plain --out eval/pooling/bipbop16x9/pooling
```

Two sources, both in `pooling.md` (made at commit `85229be`, working tree clean):

- the full-length per-frame scores of Phase 0's run of the player's analyzer (53946 frames per rung, 4 decimals). The scores of 640x360 and 416x234 are from before D-016 and differ from FFmpeg's (`eval/consistency/README.md`), and none of them is paper data (D-005); here they only give realistic series to apply the definitions to;
- the first 300 frames of each rung after D-016 (`eval/consistency/diag-after-300/plain`), on both sides.

What `pooling.md` shows:

- **The minimum is the first frame's score.** In every series of both sources the first index at the minimum is 0. For 1280x720, 960x540 and 640x360 the first frame is the only frame at the minimum; for 416x234 the minimum is 0, scored by 5976 frames of the full length and by 294 of the first 300. Without the first frame, the full-length minima are 73.1728 (index 7878), 48.4413 (25920) and 22.1403 (27720) for 1280x720, 960x540 and 640x360, where the minima are 69.7469, 45.1244 and 20.0523. In FFmpeg's logs the first frame's `integer_motion2` and `integer_motion` are 0, and it is the only frame of the first 300 with `integer_motion2` 0, in every rung.
- **Percentile definitions.** On the full length the three definitions differ by at most 0.0003 (960x540, p1, written with the data's 4 decimals). On Mirage's series of 300 frames they differ by up to 0.093440 (gear2, p1), 0.082396 (gear3, p1) and 0.071134 (gear3, p5).
- **Harmonic mean.** For all four rungs, libvmaf's harmonic mean in FFmpeg's log equals the shifted form, `n / sum(1 / (x + 1)) - 1`, at the log's 6 decimals ("Checks" in `pooling.md`), e.g. 28.434599 for gear2, where the plain form gives 28.428817. With frames at 0, as in 416x234, the plain form is undefined while the shifted form is not (0.849321 on the full length, 0.004591 on the first 300 frames).
- **Today's p5.** The analyzer's recorded `vmaf_p5` of each rung equals the **today** definition computed here at 4 decimals, and its recorded min equals the series' minimum.

When the full-length §5 run of D-018 is committed, the same command with `--delta-run` on it gives these figures for every frame after D-016 (MEASUREMENT PENDING).
