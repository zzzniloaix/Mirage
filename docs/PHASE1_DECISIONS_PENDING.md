# Phase 1: decisions pending for the owner

The design decisions of Phase 1's §1 work that are still open, each as a package: the question, what the code does today, what has been measured and where the outputs are, the options, and, marked as such, the agent's recommendation. The decisions are the owner's (hard rule 3 of the work order, `docs/DECISIONS.md` D-003, D-004, D-007). Every number below comes from a committed output, named next to it; where there is none, it says MEASUREMENT PENDING. Numbered items (#3 to #7, #11) are those of the gap report's "Decisions needed from the owner" (`docs/VMAF_GAP_REPORT.md`).

What the same work added without needing a decision, keeping every default as it was: `mirage_vmaf --scaler` (bilinear, bicubic, lanczos), `--model` and `--model-file` with the model's identifier in the report, a `reference_mode` field, both libvmaf versions of both sides in the §5 delta reports (D-006), and the measurements below. The process choices behind them are P-016 to P-019 in `docs/DECISIONS.md`.

## #3 The default scaler (D-003)

**Question.** Which swscale scaler brings the distorted frames to the reference's size by default, in the GUI and in `mirage_vmaf`, and so in the paper's scores?

**Today.** Bilinear, as since before Phase 0; every committed §5 run used bilinear on both sides, except the bicubic and lanczos runs of `scalers-300/` below. `--scaler` now chooses bilinear, bicubic or lanczos, and the report records it.

**Measured** (`eval/consistency/scalers-300/`, first 300 frames of the four pairs of D-018, each scaler on both sides; P-016):

- Mirage matches FFmpeg with each of the three scalers: every frame of every pair agrees at the 6 decimals of FFmpeg's log (`checks.txt`, check 3), and all three runs pass D-019.
- The scaler changes the scores a lot, on both sides alike. Pooled means, Mirage's side, from `scaler_effect.md`:

  | pair | bilinear | bicubic | lanczos |
  |---|---|---|---|
  | gear4_vs_gear5 | 74.640607 | 84.258060 | 83.843581 |
  | gear3_vs_gear5 | 53.729092 | 64.843002 | 67.063929 |
  | gear2_vs_gear5 | 28.552935 | 36.817663 | 39.024426 |
  | gear1_vs_gear5 | 0.006117 | 6.781918 | 8.881025 |

  No frame of any pair scores lower with bicubic or lanczos than with bilinear; on average the scores rise by 6.775801 to 11.113910 points with bicubic and 8.874908 to 13.334836 with lanczos, depending on the pair (`scaler_effect.md`).
- FFmpeg's scale filter given no `flags=` uses the flags of `flags=bicubic` (`eval/consistency/ffmpeg-scale-default/checks.txt`). So a plain `ffmpeg -lavfi` graph that scales without naming a scaler scores as Mirage does with `--scaler bicubic`, and differs from today's default.
- Not measured: the effect on a ladder encoded from one source (D-011's clips), and over every frame (MEASUREMENT PENDING).

**A difference from the work order** (hard rule 4; nothing was edited to hide it): §1.3 suggests bicubic as the default "so that the §5 consistency check can pass". The runs show that §5 passes with each of the three scalers when both sides use the same one; the scaler decides the scores, not whether the two tools agree.

**Options.**

1. Bilinear: today's default, and the setting of the committed §5 runs other than the scaler runs.
2. Bicubic: the work order's suggestion, and what FFmpeg's scale filter does when no scaler is named.
3. Lanczos.

Whatever the choice, the §5 runs give FFmpeg the same `flags=` (`run_delta.sh --scaler`), and every report states the scaler. The full-length §5 report of D-018 runs with whatever the default is when it is made.

**Also open, with the scaler.** `--scaler` sets the distorted frames' scaler only; the reference keeps its size and is only converted to 8-bit yuv420p, with bilinear flags whatever `--scaler` says (P-017). Every reference so far is already yuv420p. For a reference in another format, FFmpeg's §5 graph converts it with an automatically inserted scale filter; whether Mirage's conversion gives the same pixels is part of #5 and untested.

**Agent's recommendation.** Bicubic. It is the work order's suggestion and FFmpeg's own default when a scale filter names no scaler, so the paper's scores would be what a reader gets from a plain `ffmpeg` command with a plain `scale` filter. The continuity argument for bilinear is weak, because no score so far is paper data (D-005). Whichever is chosen, the paper should say which scaler, since the measured effect is several points.

## #4 The default model (D-004)

**Question.** Which libvmaf model scores pairs by default? The gap report also asks whether choosing the model from the resolution counts as a default.

**Today.** The built-in vmaf_v0.6.1. `--model` takes any built-in model of the linked libvmaf (this one has vmaf_v0.6.1, vmaf_v0.6.1neg, vmaf_4k_v0.6.1 among them), `--model-file` a model file, and the report gives `model`, `model_source` and `model_sha256` (P-017).

**Measured** (`eval/consistency/models-300/`, gear2 against gear5, first 300 frames, bilinear, each model on both sides; P-016):

| model | pooled mean (Mirage's side, `model_effect.md`) | frames up / same / down against vmaf_v0.6.1 |
|---|---|---|
| vmaf_v0.6.1 (built-in) | 28.552935 | |
| vmaf_v0.6.1neg (built-in) | 19.498402 | 0 / 0 / 300 |
| vmaf_4k_v0.6.1 (built-in) | 47.442375 | 300 / 0 / 0 |
| the package's `vmaf_v0.6.1.json` (`--model-file`) | 28.552935 | 0 / 300 / 0 |

- Every frame agrees between Mirage and FFmpeg at FFmpeg's 6 decimals with every model (`checks.txt`, check 3), and the model file scores as the built-in model does (check 6). This meets §1.1's acceptance: changing the model gives observably different scores, and the output names the model.
- FFmpeg's libvmaf filter's own default model is `version=vmaf_v0.6.1` (`eval/consistency/ffmpeg-libvmaf-filter/checks.txt`).
- Not measured: the models on other rungs, other content, or every frame.

**Options.**

1. vmaf_v0.6.1: today's default and FFmpeg's libvmaf filter default.
2. vmaf_v0.6.1neg: the variant whose features limit enhancement gain (FFmpeg's log names its features with `_egl_1`, `models-300/checks.txt`, check 5); on gear2 it scores every frame lower.
3. vmaf_4k_v0.6.1.
4. A model chosen from the reference's resolution. That needs a rule, with a resolution limit the owner sets, and the report would have to state which model each pair got.

**Agent's recommendation.** vmaf_v0.6.1 as the default: FFmpeg's libvmaf filter uses it unless told otherwise, and every committed §5 run used it apart from the other models' runs in `models-300/`. The other models stay options, recorded in each report; a rule by resolution can come later as an option, once the owner has set its resolution limit.

## #5 Bit depth

**Question.** At what bit depth are frames handed to libvmaf?

**Today.** Every input becomes 8-bit yuv420p before scoring (`src/VMAFAnalyzer.cpp`, `kScorePixFmt` and `kScoreBpc`), and the report states it (`scored_as`). FFmpeg's §5 graph does the same with `format=yuv420p` on both inputs, so the §5 comparison is of 8-bit scoring on both sides. A 10-bit input would be reduced to 8 bits by swscale on both sides: the distorted frames in the same step as their scaling, and the reference in Mirage's own conversion call and on FFmpeg's side in an automatically inserted scale filter. Whether the two sides then give the same bytes is not tested: no input scored so far has more than 8 bits (bipbop 16x9 is yuv420p, `eval/baseline/diagnostics/ffprobe_rungs.txt`).

**What the libraries take.** FFmpeg 8.1.1's libvmaf filter accepts 8-, 10-, 12- and 16-bit planar 4:2:0, 4:2:2 and 4:4:4 (`libavfilter/vf_libvmaf.c`, its format list), and libvmaf's `vmaf_picture_alloc()` takes a bit depth.

**Options.**

1. 8 bits for every input (today).
2. The inputs' own bit depth when both have more than 8 bits (the §5 graph would then use the matching `format=` on both sides).
3. Configurable, with 1 or 2 as the default.

HDR content (PQ or HLG transfer) is a further question: the models above are SDR models, and nothing in this work handles HDR.

**What a measurement needs.** A ladder of 10-bit SDR rungs encoded from one 10-bit source, which D-011's self-encoded ladders can provide; each rung scored at 8 and at its own bit depth, on both sides; and, for the §5 comparison, synthetic 10-bit clips run through `run_delta.sh` the way `eval/consistency/color_cases.py` makes 8-bit ones, to check the reference's conversion. No suitable public stream was at hand tonight (the streams this repository uses are 8-bit), so nothing was measured (MEASUREMENT PENDING).

**Agent's recommendation.** Keep 8 bits until D-011's ladders exist, then measure option 2 against option 1 on them; add the synthetic 10-bit check to the §5 work before any 10-bit input is scored.

## #6 Pooling definitions (D-004)

**Question.** How are `mean`, `harmonic_mean`, `min` and `p1` / `p5` / `p10` / `p25` / `p50` (work order §1.5) defined?

**Today.** `mirage_vmaf` reports libvmaf's pooled mean and min (`vmaf_score_pooled`); the player's analyzer also computes `vmaf_p5` as `sorted[n * 5 / 100]`, which `mirage_vmaf` leaves out (P-006). Nothing was changed.

**Measured** (`eval/pooling/bipbop16x9/pooling.md`, made by `eval/pooling/pooling.py` from the Phase 0 baseline's full-length per-frame scores and from both sides of `eval/consistency/diag-after-300/plain`; P-018; summary in `eval/pooling/README.md`):

- **The minimum is the first frame's score** in every series, as the gap report observed. In 1280x720, 960x540 and 640x360 the first frame is the only frame at the minimum; in 416x234 the minimum is 0, which 5976 frames of the full length and 294 of the first 300 score. Without the first frame the full-length minima are 73.1728, 48.4413 and 22.1403, where the minima are 69.7469, 45.1244 and 20.0523. In FFmpeg's logs the first frame's `integer_motion2` and `integer_motion` are 0, and in each rung it is the only frame of the first 300 with `integer_motion2` 0.
- **Percentile definitions**: today's rule (`sorted[floor(n*p/100)]`), nearest rank (`sorted[ceil(n*p/100) - 1]`) and linear interpolation (Hyndman and Fan's definition 7) differ by at most 0.0003 on the full-length series (4 decimals), and by up to 0.093440 (gear2, p1), 0.082396 (gear3, p1) and 0.071134 (gear3, p5) on 300 frames.
- **Harmonic mean**: libvmaf's harmonic mean in FFmpeg's logs is `n / sum(1 / (x + 1)) - 1` for all four rungs, at the log's 6 decimals; the plain `n / sum(1 / x)` differs (gear2: 28.434599 against 28.428817), and it is undefined when a frame scores 0, as 416x234's frames do.
- The analyzer's recorded `vmaf_p5` is today's rule, and its recorded min is the series' minimum, on every rung.
- Not measured: every frame after D-016 (the full-length §5 run of D-018; MEASUREMENT PENDING; the same script runs on it).

**Options.**

- Percentiles: today's rule; nearest rank; linear interpolation. libvmaf has no percentile pooling, so Mirage computes them in every option.
- Harmonic mean: libvmaf's (`VMAF_POOL_METHOD_HARMONIC_MEAN`, the shifted form, which FFmpeg's log also reports); the plain form, with a rule for frames at 0.
- Minimum: libvmaf's, over every frame (then it is the first frame's score, as above); or also, as a separately named value, the minimum without the first frame.
- Mean: libvmaf's mean, which the reports already carry and FFmpeg's log shows too.

**Agent's recommendation.** Linear interpolation for the percentiles, the default of common analysis tools, so readers recomputing them from the per-frame scores get the same values; libvmaf's harmonic mean, which is defined with frames at 0 and is what FFmpeg reports; libvmaf's minimum for parity with FFmpeg, with the first-frame effect stated in the paper and, if the owner wants a minimum that means something, a second value without the first frame, under its own name.

## #7 Frame alignment, and approval of the plan (§1.4, D-007)

The plan is `docs/plans/frame-alignment.md`. It asks for approval of pairing by timestamp in the analyzer that the GUI and `mirage_vmaf` share (D-007), and lists the choices it needs: the matching rule (the reference's timeline, or FFmpeg's framesync rule, whose defaults are in `eval/consistency/ffmpeg-libvmaf-filter/checks.txt`), the timestamp origin, frame-rate mismatches, discontinuities, what a gap does to libvmaf's motion feature, the report's fields, and #7 itself: the failure-rate definition, the threshold (the work order suggests 1%) and whether exceeding it warns or refuses. The plan ends with the agent's recommendation. Nothing of it is implemented; its acceptance test, the work order's stream with dropped frames, is part of the plan.

## §1.2 Reference modes

- **`reference_mode` of `mirage_vmaf`**: `explicit-pair`, because the command line names both inputs (P-017). The work order's §7 lists only `mezzanine` and `top-rung`; confirm the name, or choose another, such as letting the user declare one of the two.
- **Ladder scoring in `mirage_vmaf`** (top rung by default per D-002, a mezzanine optionally) is not built (P-019). It needs the owner's or §2's choices: which `EXT-X-STREAM-INF` entries are scored (audio-only entries, entries that share a video playlist, equal bandwidths), what a failed rung does to the run and its exit status given P-006, and how one report holds several pairs while keeping the two modes apart before §7's schema. **Agent's recommendation**: score each video playlist once, skip entries without video and list them as skipped, let one failed rung fail the run as P-006 does for a pair, and write one report per reference mode with a `pairs` list, so that mode A and mode B never share a report.
- **§1.2's acceptance**, both modes on one ladder with the difference documented, needs a ladder with a mezzanine: D-011's source clips (MEASUREMENT PENDING).

## The config file (the work order's engineering constraints)

The work order asks for every configurable item as a command line option and in a config file. `mirage_vmaf`'s options are all on its command line: `--ref`, `--dis`, `--out`, `--frames`, `--scaler`, `--model`, `--model-file`. Fixed in the code, not configurable: libvmaf's threads and subsampling, the 8-bit yuv420p scoring format (#5), and pairing in decode order (#7). The player gets no new flags.

**A design for the owner to choose from** (not built: the format is the owner's call):

- `mirage_vmaf --config FILE`, with the same names as the long options, one setting per line (`scaler = bicubic`), read through the same option table as the command line, so that both accept exactly the same settings; or JSON; or TOML, which needs a parser the repository doesn't have.
- The command line overrides the file.
- The report records the file's path and SHA-256 next to the effective settings, which it already records.
- For `run_delta.sh`, the same, or a waiver for the eval tooling, since `run.cmd.txt` records every option of a run.

**Agent's recommendation**: the `name = value` lines through the option table, with the command line overriding the file and the file's SHA-256 in the report: no new dependency, and nothing that can be set one way but not the other.

## Differences between the work order and the implementation found in this work (hard rule 4)

Reported, not edited:

1. §1.3 gives §5 passing as the reason for bicubic; §5 passes with all three scalers (#3).
2. §7's `reference_mode` has only `mezzanine` and `top-rung`; `mirage_vmaf` writes `explicit-pair` (§1.2 above).
3. §5's command, `ffmpeg -i <distorted> -i <reference> -lavfi libvmaf=model=<same>`, has no scale filter, and FFmpeg's libvmaf filter refuses inputs of different sizes (`input width must match`, `eval/consistency/ffmpeg-libvmaf-filter/checks.txt`, check 4). The §5 tool adds `scale=W:H:flags=<scaler>` to the distorted input, as Phase 0's diagnostic did (P-005, P-008).
