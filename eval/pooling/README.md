# Pooling definitions (work order §1.5)

Work order §1.5 asks for `mean`, `harmonic_mean`, `min` and `p1` / `p5` / `p10` / `p25` / `p50`. How they are defined was the owner's decision #6 (`docs/DECISIONS.md` D-004). `pooling.py` measured what the candidate definitions give on per-frame scores already committed in the repository (`bipbop16x9/`, `acceptance-full/` below; the package in `docs/PHASE1_DECISIONS_PENDING.md`), and D-027 decided:

- the mean, the harmonic mean, `n / sum(1 / (x + 1)) - 1`, and the minimum as libvmaf pools them; the minimum without the first frame under its own name; and p1, p5, p10, p25 and p50 by linear interpolation, Hyndman and Fan's definition 7, the **linear** definition below.

Since then `mirage_vmaf`'s report gives all of them (`pooled`, `src/VMAFReport.h`), and the player's p5 is the interpolated one. The analyzer computes them from the per-frame scores (`src/VMAFPool.h`; P-039): the mean, harmonic mean and minimum with libvmaf's formulas, summed frame by frame in index order as libvmaf sums them, so that they are `vmaf_score_pooled()`'s values to the last bit for the scores of one libvmaf context (`tests/test_vmaf_pool.cpp`), and the percentiles with the arithmetic of `pooling.py`'s linear definition. With `--check-pooled`, `pooling.py` computes the same values and checks them against those of `mirage_vmaf`'s reports (`defaults-300/` and `defaults-full/` below).

## Running it

```
python3 -I -B eval/pooling/pooling.py (--gui-report <report.vmaf.json> | --delta-run <run_delta output dir>)... [--check-pooled] --out <prefix>
python3 -I -B -m unittest discover -s eval/pooling/tests -v
```

- `--gui-report`: a report the player's analyzer writes (`write_json`), whose per-frame scores have 4 decimals.
- `--delta-run`: an `eval/consistency/run_delta.sh` output directory. For each pair it reads `mirage_vmaf`'s report (scores at full precision, libvmaf's pooled mean and min) and FFmpeg's libvmaf log (scores at 6 decimals; libvmaf's pooled min, max, mean and harmonic mean; each frame's features).
- `--check-pooled`: for Mirage's series of each `--delta-run`, also the pooled values of D-027 as `mirage_vmaf` computes them (`POOLED_D027`) and those of its report, with a check that each pair of them is equal at full precision. Without it the output is what it was before D-027, which the reruns of `bipbop16x9/` and `acceptance-full/` by `eval/run_all.sh` rely on.
- It writes `<prefix>.json` and `<prefix>.md`, with the SHA-256 of every input and the git HEAD and working tree state at the start and end of the run. Python 3 standard library only.

For every series of per-frame scores it gives:

- the mean, the minimum, the first index at the minimum, how many frames score the minimum, the first frame's score, and the minimum without the first frame;
- p1, p5, p10, p25 and p50 under three definitions: **today**, `sorted[floor(n*p/100)]`, the rule of the analyzer's `vmaf_p5` until D-027; **nearest-rank**, `sorted[ceil(n*p/100) - 1]`; **linear**, interpolation between the closest ranks with `h = (n - 1) * p / 100` (Hyndman and Fan's definition 7, also `statistics.quantiles(..., method="inclusive")`);
- the harmonic mean in two forms, `n / sum(1 / (x + 1)) - 1` (**shifted**) and `n / sum(1 / x)` (**plain**, undefined when a frame scores 0), next to libvmaf's own harmonic mean where FFmpeg's log has it. The script's mean and harmonic means sum the scores exactly (`math.fsum`), where libvmaf, and so Mirage since D-027, sums them in frame order, and the two can differ in the last bits: in `defaults-300/pooling.json`, gear4's mean is 84.2580603478746 summed exactly and 84.25806034787456 in frame order, the value of `mirage_vmaf`'s report. The D-027 values of `--check-pooled` sum in frame order;
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

## `acceptance-full/`: every frame after D-016

```
python3 -I -B eval/pooling/pooling.py --delta-run eval/consistency/acceptance-full/plain --out eval/pooling/acceptance-full/pooling
```

One source, in `pooling.md` (made at commit `dfadf66`, working tree clean; P-027): both sides of every frame of the four pairs of the committed §5 report, `eval/consistency/acceptance-full/plain` (D-018), 53946 frames per pair, after D-016. Mirage's series have full precision, FFmpeg's the 6 decimals of its log; the two give the same figures, apart from the sixth decimal of a few percentiles.

What `pooling.md` shows:

- **The minimum is the first frame's score.** In every series the first index at the minimum is 0. For gear4, gear3 and gear2 the first frame is the only frame at the minimum, and without it the minima are 73.172758 (index 7878), 48.441289 (25920) and 18.126492 (28320), where the minima are 69.746945, 45.124432 and 16.070202. gear1's minimum is 0, which 53790 of its 53946 frames score. In FFmpeg's logs the first frame's `integer_motion2` and `integer_motion` are 0, and in every pair it is the only frame with `integer_motion2` 0.
- **Percentile definitions.** The three definitions differ by at most 0.000276 (gear3, p1) on these series; for gear1, p1 to p50 are 0 under all three.
- **Harmonic mean.** For all four pairs, libvmaf's harmonic mean in FFmpeg's log equals the shifted form at the log's 6 decimals ("Checks" in `pooling.md`), e.g. 27.751821 for gear2, where the plain form gives 27.746387. For gear1 the plain form is undefined; the shifted form is 0.000574.
- **Against `bipbop16x9/`.** gear4 and gear3 have the scores of Phase 0's series at its 4 decimals (`eval/consistency/acceptance-full/checks.txt`, check 4), and their minima without the first frame are where the baseline's are (indices 7878 and 25920). gear2 and gear1 changed with D-016: gear2's minimum without the first frame is now at index 28320, and gear1 has 53790 frames at 0 where the baseline had 5976.

## `defaults-300/`: `mirage_vmaf`'s pooled values against this script (D-027)

```
python3 -I -B eval/pooling/pooling.py --delta-run eval/consistency/defaults-300/plain --check-pooled --out eval/pooling/defaults-300/pooling
```

One source, in `pooling.md` (made at commit `a0a9058`, working tree clean; P-041): both sides of the first 300 frames of the four pairs with the defaults of D-024 and D-027 (`eval/consistency/defaults-300/plain`), whose `mirage_vmaf` reports the pooled values of D-027.

What `pooling.md` shows:

- **Every pooled value of every report is `pooling.py`'s.** For each of the four pairs, the mean, harmonic mean, minimum, minimum without the first frame and p1, p5, p10, p25 and p50 of `mirage_vmaf`'s report equal the values `pooling.py` computes from the report's per-frame scores by `POOLED_D027`, at full precision ("The pooled values of D-027" in `pooling.md`, which writes both in full).
- The other checks hold as on the earlier sources: libvmaf's harmonic mean in FFmpeg's logs is the shifted form at the log's 6 decimals, and each recorded minimum is the series' minimum ("Checks").
- The comparisons with libvmaf's `vmaf_score_pooled()` and FFmpeg's log are `eval/consistency/defaults-300/checks.txt`, checks 5 and 6.

## `defaults-full/`: every frame with the defaults (D-024, D-027, P-059)

```
python3 -I -B eval/pooling/pooling.py --delta-run eval/consistency/defaults-full/plain --check-pooled --out eval/pooling/defaults-full/pooling
```

One source, in `pooling.md` (made at commit `8e61d95`, working tree clean; P-059): both sides of every frame of the four pairs of the full-length §5 report with the defaults (`eval/consistency/defaults-full/plain`), 53946 frames per pair, bicubic, with a `mirage_vmaf` that reports the pooled values of D-027.

What `pooling.md` shows:

- **Every pooled value of every report is `pooling.py`'s**, at full precision, as on the first 300 frames ("The pooled values of D-027").
- **The minimum is the first frame's score.** In every series of both sides the first index at the minimum is 0. For gear4, gear3 and gear2 the first frame is the only frame at the minimum, and without it the minima are 82.494129 (index 8480), 60.167983 (52920) and 25.172073 (7860), where the minima are 79.613837, 56.844534 and 23.488692. gear1's minimum is 0, which 1799 of its 53946 frames score, the minimum without the first frame too (index 30). In FFmpeg's logs the first frame's `integer_motion2` and `integer_motion` are 0, and in every pair it is the only frame with `integer_motion2` 0.
- **Percentile definitions.** The three definitions differ by at most 0.001160 (gear2, p1) on these series, where they differ by at most 0.000276 on the series with bilinear (`acceptance-full/` above).
- **Harmonic mean.** For all four pairs, libvmaf's harmonic mean in FFmpeg's log equals the shifted form at the log's 6 decimals, e.g. 35.705189 for gear2, where the plain form gives 35.701575. For gear1 the plain form is undefined, with 1799 frames at 0; the shifted form is 4.785794.
- The comparison with FFmpeg's log is `eval/consistency/defaults-full/checks.txt`, check 9.
