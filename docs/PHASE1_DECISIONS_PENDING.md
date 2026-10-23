# Phase 1: decisions pending for the owner

The design decisions of Phase 1 (§5, §1 and §8) that are still open, each as a package: the question, what the code does today, what has been measured and where the outputs are, the options, and, marked as such, the agent's recommendation. The decisions are the owner's (hard rule 3 of the work order, `docs/DECISIONS.md` D-003, D-004, D-007). Every number below comes from a committed output, named next to it; where there is none, it says MEASUREMENT PENDING. Numbered items (#3 to #7, #11) are those of the gap report's "Decisions needed from the owner" (`docs/VMAF_GAP_REPORT.md`).

What the same work added without needing a decision, keeping every default as it was: `mirage_vmaf --scaler` (bilinear, bicubic, lanczos), `--model` and `--model-file` with the model's identifier in the report, a `reference_mode` field, both libvmaf versions of both sides in the §5 delta reports (D-006), and the measurements below. The process choices behind them are P-016 to P-019 in `docs/DECISIONS.md`.

The §5 report of D-018 passes with today's defaults over every frame of the four pairs (`eval/consistency/acceptance-full/`; `docs/PHASE1_STATUS.md`). A decision below that changes a default means making that report again. The state of each section of Phase 1 is in `docs/PHASE1_STATUS.md`.

## D-016: untagged streams, and matrices libswscale can't convert (§5)

**Question.** D-016 has Mirage convert the distorted frames' color matrix and range to the reference's, as FFmpeg's filter graph does, with untagged streams handled the way FFmpeg handles them, and asks the agent to report what that means in practice for the owner to confirm. Is the behavior below what D-016 means?

**Today** (`src/VMAFColor.h`, P-014). For a yuv420p reference, Mirage does what FFmpeg 8.1.1's §5 graph does in the color tag cases (`eval/consistency/README.md`, "Color tag cases"; `eval/consistency/color-cases-before/`). This is what the code does now, in `mirage_vmaf` and in the player, whose VMAF panel runs the same analyzer; a decision other than option 1 and (a) below changes that code, and the committed runs after D-016 that it affects would have to be made again. For a reference that isn't yuv420p, FFmpeg's graph does something else: next package.

- The distorted frames get the reference's matrix tag, a missing tag included.
- They get the reference's range tag. When the reference has none, they keep a limited or missing range, and a full range becomes unspecified.
- In the pixels, libswscale takes a missing matrix as BT.601's and a missing range as limited. So an untagged distorted input against a BT.709 reference is converted from BT.601 to BT.709, a BT.709 distorted input against an untagged reference is converted to BT.601, and a full range distorted input against a reference without a range tag is converted to limited range.
- The primaries and the transfer are never converted; the chroma siting is dropped, and interlaced frames are scaled as progressive.
- A matrix libswscale has no coefficients for, such as YCgCo, on either input ends the analysis with an error, in the player too; `mirage_vmaf` then writes no report and exits with status 1. FFmpeg can't build its graph for such a pair: it exits with status 178 with YCgCo on either input (`color-cases-before/refused/delta.md`) and on both (`format-cases-before/refused/delta.md`). Before D-016, Mirage scored such inputs without converting.

**Measured.**

- Before D-016, Mirage failed 9 of the 18 color tag cases FFmpeg scores (`color-cases-before/main/delta.md`); after it, all 18 pass, every frame agreeing at the 6 decimals of FFmpeg's log, and both tools refuse the 2 YCgCo cases (`color-cases-after/checks.txt`, checks 2 and 5).
- On bipbop, gear2 and gear1 (smpte170m against gear5's bt709) failed over the first 300 frames before D-016 and pass after it (`eval/consistency/diag-before-300/`, `diag-after-300/`). Over every frame of the four pairs, both tools convert gear2 and gear1 the same way and agree on every frame (`eval/consistency/acceptance-full/plain/delta.md`; `checks.txt`, checks 2 and 5).
- Both tools refuse YCgCo on both inputs too (`format-cases-after/checks.txt`, check 9).
- Not measured: RGB and gray inputs, and FFmpeg versions other than 8.1.1 (P-013's limits). References in other YUV formats are measured in the next package; 10-bit and yuvj420p distorted inputs against a yuv420p reference agree after D-016 (`format-cases-after/`, `d01` and `d02`).

**Options.**

1. Confirm: FFmpeg 8.1.1's behavior for a yuv420p reference, as implemented and as the code, the player's included, does today.
2. Deviate for untagged inputs, for example by converting nothing when either input lacks a tag. Plain FFmpeg would then disagree on those pairs, and §5 would need the relabel workaround for them (P-010). The code changes, and the runs after D-016 with untagged inputs would be made again.
3. Make the handling of untagged inputs configurable, with option 1 as the default.

For a matrix libswscale lacks: (a) refuse, as the code, the player's included, does today; (b) score without converting and warn in the report, as before D-016, which changes the code and the refused runs after D-016.

**Also open.**

- **The FFmpeg libraries' versions in the report.** The rules are those of FFmpeg 8.1.1's libavfilter and libswscale, and Homebrew's current ffmpeg-full is a major version newer (`eval/build_check/macos-2026-10-23-6a6661f.txt`, "installed vs current stable"). `mirage_vmaf`'s report records libvmaf's versions (D-006) but not FFmpeg's; the delta runs record `ffmpeg -version` in `environment.txt`. Options: record the run-time libswscale and libavfilter versions in the report; leave them to the delta runs; also warn when FFmpeg's major version isn't 8.
- **D-016's Background** says the §5 work would reproduce the effect with committed outputs (MEASUREMENT PENDING). They exist now: `diag-before-300/`, `diag-after-300/`, `color-cases-before/`, `color-cases-after/` and `acceptance-full/`. D-016 is the owner's entry and wasn't edited; a note citing them, as D-001 has its correction, is the owner's to add.

**Agent's recommendation.** Option 1, and (a) for matrices libswscale lacks. D-016's rationale is matching a plain `ffmpeg -lavfi libvmaf` run, which the committed runs show Mirage doing frame for frame for yuv420p references, and FFmpeg gives no score to match for a pair it refuses. Record the libswscale and libavfilter versions in the report, and rerun the color tag cases after any FFmpeg upgrade. The paper should say that an untagged input is taken as BT.601 with limited range.

## D-016: references that aren't yuv420p (§5, #5)

**Question.** D-016 has Mirage convert the distorted frames to the reference's color matrix and range, "as FFmpeg's filter graph does". For a reference whose frames aren't yuv420p, FFmpeg 8.1.1's §5 graph does something else: it converts the reference to the distorted frames' matrix and range, with a scale filter it inserts itself, and leaves the distorted frames' tags alone. Mirage converts the distorted frames as it does for a yuv420p reference, so its scores differ from FFmpeg's for such a reference wherever the two inputs' tags differ, and, because of how it converts the reference, for a full range yuvj420p reference even with the same tags. Should Mirage follow FFmpeg for such references, or D-016's wording?

**Today** (`src/VMAFColor.h`, `src/VMAFAnalyzer.cpp`; `mirage_vmaf` and the player alike):

- Mirage converts the reference to 8-bit yuv420p with a legacy `sws_getContext()` call and bilinear flags, without converting its matrix; for a yuvj420p reference that call also turns full range into limited range (`tests/test_vmaf_color.cpp`). The distorted frames are converted to the reference's tags, as `vmafcolor::target()` derives them.
- Since `0ff3178`, `mirage_vmaf`'s report has a warning for a reference that isn't yuv420p, and the delta reports list it; no score changed (P-030).
- FFmpeg's graph puts `auto_scale_0` before the reference's `format=yuv420p`, with bicubic flags (`0x00000004`) where the distorted input's scale filter has bilinear's (`0x00000002`), and it converts the reference to the distorted frames' matrix and range; the distorted input's scale filter keeps their tags (`eval/consistency/format-cases-before/checks.txt`, check 2). The FFmpeg 8.1.1 sources explain the logs: once a scale filter is inserted on the reference's side, the matrix and range both inputs share are free to choose, and libavfilter reduces them to the tags of the distorted input's scale filter's input, which comes earlier in the graph (`libavfilter/avfiltergraph.c`, `query_formats`, `reduce_formats`).

**Measured** (`eval/consistency/format-cases-before/`, `format-cases-after/`: synthetic clips, 10 frames each; P-029; the table in `eval/consistency/README.md`):

- A 10-bit, 4:2:2 or 4:4:4 reference with another matrix than the distorted input: Mirage fails every case after D-016, by up to 10.931963 (`f05`; `f01`, `f02`, `f03`, `f05`, `f07` in `format-cases-after/main/delta.md`). Before D-016 they failed too, by 20.072068 to 32.029205 (`format-cases-before/main/delta.md`).
- The same references with the same tags as the distorted input: Mirage agrees with FFmpeg on every frame (`f04`, `f06`, `f08`; `format-cases-after/checks.txt`, check 4).
- A lossless full range H.264 reference, which FFmpeg's decoder gives as yuvj420p:
  - against a limited range distorted input (`j01`), Mirage agreed with FFmpeg before D-016 and fails after it, by up to 13.274013: since D-016 it converts the distorted frames to full range, where FFmpeg converts the reference to limited range;
  - against a full range distorted input (`j02`), it fails before and after D-016, by up to 23.673382: its conversion of the reference makes it limited range, where FFmpeg keeps it full. Mirage's scores of `j02` are FFmpeg's of `j01` on every frame at 6 decimals (`format-cases-after/checks.txt`, check 8).
- 10-bit and yuvj420p distorted inputs against a yuv420p reference agree after D-016 (`d01`, `d02`); `d01` failed before it.
- No committed bipbop run is affected: every bipbop rung is yuv420p (`eval/baseline/diagnostics/ffprobe_rungs.txt`).
- Not measured: RGB and gray inputs, 12-bit and 4:4:4 distorted inputs, distorted inputs without a range tag against such references, and real streams in these formats (MEASUREMENT PENDING).

**Options.**

1. Follow FFmpeg: when the reference isn't yuv420p, convert the reference to the distorted frames' matrix and range through `sws_scale_frame()` with the flags of FFmpeg's inserted scale filter (bicubic), and leave the distorted frames' color as it is, still scaled with `--scaler`. Every case of `format-cases-after/` would then be expected to pass, which its rerun step in `eval/run_all.sh` would check, `j01` with its scores before D-016. It changes scores for such references, and for 10-bit references it decides how the reference is brought to 8 bits, which belongs with #5.
2. Follow D-016's wording: keep converting the distorted frames to the reference's tags, and only make the reference's conversion keep a yuvj420p reference's range (`j02`). Plain FFmpeg then disagrees whenever such a reference's tags differ from the distorted input's, and the §5 comparison would need a workaround on FFmpeg's side for those pairs, which no committed run has tried.
3. Refuse such references, as matrices libswscale lacks are refused, until option 1 or 2 is built; or refuse them only when the tags differ or the reference is yuvj420p.
4. Keep today's behavior, with the warning.

**Agent's recommendation.** Option 1, decided together with #5. D-016's rationale is matching a plain `ffmpeg -lavfi libvmaf` run, and for these references FFmpeg's conversion is of the reference; the committed cases are its acceptance test. Until then, option 4: the warning keeps the wrong scores from being silent without changing any score. Before the paper uses any such reference, the decision has to be made and the cases rerun.

## D-019: a frame exactly at the threshold (§5)

**Question.** The work order's §5 target is a per-frame difference "< 0.01". D-019 fails a frame if |Mirage − FFmpeg| > 0.01, so a frame at exactly 0.01 passes D-019 but misses the work order's wording. Which holds?

**Today.** The delta tool implements D-019, and a test pins the boundary (`test_threshold_is_exclusive` in `eval/consistency/tests/test_vmafdelta.py`). In the committed runs made after D-016 that scored frames with a yuv420p reference, the largest |delta| is 0.000000 at the 6 decimals of FFmpeg's log (the runs' `delta.md` in `diag-after-300/`, `color-cases-after/main/`, `scalers-300/`, `models-300/` and `acceptance-full/`, and the pairs `d01` and `d02` of `format-cases-after/main/`), so no frame has come near the boundary. The other cases of `format-cases-after/main/` either agree at 0.000000 or fail far above it (previous package).

**Options.**

1. Keep D-019, and state the boundary in the method text.
2. Fail a frame when |delta| ≥ 0.01, as the work order's wording has it.

**Agent's recommendation.** Option 1: the two differ only for a frame at exactly 0.01.

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
- FFmpeg's scale filter given no `flags=` uses the flags of `flags=bicubic` (`eval/consistency/ffmpeg-scale-default/checks.txt`). So a plain `ffmpeg -lavfi` graph that scales without naming a scaler runs with the swscale flags of `--scaler bicubic`, and is expected to score as Mirage does with it, not as with today's default; no such graph was scored here.
- Not measured: the effect on a ladder encoded from one source (D-011's clips), and over every frame (MEASUREMENT PENDING).

**A difference from the work order** (hard rule 4; nothing was edited to hide it): §1.3 suggests bicubic as the default "so that the §5 consistency check can pass". The runs show that §5 passes with each of the three scalers when both sides use the same one; the scaler decides the scores, not whether the two tools agree.

**Options.**

1. Bilinear: today's default, and the setting of the committed §5 runs other than the scaler runs.
2. Bicubic: the work order's suggestion, and what FFmpeg's scale filter does when no scaler is named.
3. Lanczos.

Whatever the choice, the §5 runs give FFmpeg the same `flags=` (`run_delta.sh --scaler`), and every report states the scaler. The full-length §5 report of D-018 was made with today's default, bilinear (`eval/consistency/acceptance-full/`); another default means making it again.

**Also open, with the scaler.** `--scaler` sets the distorted frames' scaler only; the reference keeps its size and is only converted to 8-bit yuv420p, with bilinear flags whatever `--scaler` says (P-017). Every bipbop reference is yuv420p. For a reference in another format, FFmpeg's §5 graph converts it with a scale filter it inserts itself, to the distorted frames' color and with bicubic flags, where the distorted input's scale filter had bilinear's (`eval/consistency/format-cases-before/checks.txt`, check 2; no other `flags=` was run with such a reference); Mirage's scores then differ from FFmpeg's where the tags differ (the package "D-016: references that aren't yuv420p").

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

**Today.** Every input becomes 8-bit yuv420p before scoring (`src/VMAFAnalyzer.cpp`, `kScorePixFmt` and `kScoreBpc`), and the report states it (`scored_as`). FFmpeg's §5 graph does the same with `format=yuv420p` on both inputs, so the §5 comparison is of 8-bit scoring on both sides. A 10-bit input is reduced to 8 bits by swscale on both sides: the distorted frames in the same step as their scaling, and the reference in Mirage's own conversion call and on FFmpeg's side in a scale filter FFmpeg inserts. bipbop 16x9 is yuv420p (`eval/baseline/diagnostics/ffprobe_rungs.txt`); the synthetic reference format cases have 10-bit inputs (`eval/consistency/format-cases-after/`, P-029):

- a 10-bit distorted input against a yuv420p reference: Mirage agrees with FFmpeg on every frame (`d01`);
- a 10-bit reference with the distorted input's tags: it agrees on every frame (`f04`);
- a 10-bit reference with other tags: it doesn't, because FFmpeg converts the reference's color as well (the package "D-016: references that aren't yuv420p").

**What the libraries take.** FFmpeg's libvmaf filter takes higher bit depths than 8 as well, in 4:2:0, 4:2:2 and 4:4:4 (its format list in `libavfilter/vf_libvmaf.c` of the FFmpeg 8.1.1 sources, read, not run), and libvmaf's `vmaf_picture_alloc()` takes a bit depth.

**Options.**

1. 8 bits for every input (today).
2. The inputs' own bit depth when both have more than 8 bits (the §5 graph would then use the matching `format=` on both sides).
3. Configurable, with 1 or 2 as the default.

HDR content (PQ or HLG transfer) is a further question: the models above are SDR models, and nothing in this work handles HDR.

**What a measurement needs.** A ladder of 10-bit SDR rungs encoded from one 10-bit source, which D-011's self-encoded ladders can provide, and each rung scored at 8 and at its own bit depth, on both sides. No suitable public stream was at hand on 2026-10-23 (the streams this repository uses are 8-bit), so option 2 is not measured (MEASUREMENT PENDING). The synthetic 10-bit check of the §5 comparison exists now: `color_cases.py --set formats` (above).

**Agent's recommendation.** Keep 8 bits until D-011's ladders exist, then measure option 2 against option 1 on them. Decide together with the package "D-016: references that aren't yuv420p", which is about how a 10-bit reference is converted.

## #6 Pooling definitions (D-004)

**Question.** How are `mean`, `harmonic_mean`, `min` and `p1` / `p5` / `p10` / `p25` / `p50` (work order §1.5) defined?

**Today.** `mirage_vmaf` reports libvmaf's pooled mean and min (`vmaf_score_pooled`); the player's analyzer also computes `vmaf_p5` as `sorted[n * 5 / 100]`, which `mirage_vmaf` leaves out (P-006). Nothing was changed.

**Measured** (`eval/pooling/bipbop16x9/pooling.md`, made by `eval/pooling/pooling.py` from the Phase 0 baseline's full-length per-frame scores and from both sides of `eval/consistency/diag-after-300/plain`; P-018; summary in `eval/pooling/README.md`):

- **The minimum is the first frame's score** in every series, as the gap report observed. In 1280x720, 960x540 and 640x360 the first frame is the only frame at the minimum; in 416x234 the minimum is 0, which 5976 frames of the full length and 294 of the first 300 score. Without the first frame the full-length minima are 73.1728, 48.4413 and 22.1403, where the minima are 69.7469, 45.1244 and 20.0523. In FFmpeg's logs the first frame's `integer_motion2` and `integer_motion` are 0, and in each rung it is the only frame of the first 300 with `integer_motion2` 0.
- **Percentile definitions**: today's rule (`sorted[floor(n*p/100)]`), nearest rank (`sorted[ceil(n*p/100) - 1]`) and linear interpolation (Hyndman and Fan's definition 7) differ by at most 0.0003 on the full-length series (4 decimals), and by up to 0.093440 (gear2, p1), 0.082396 (gear3, p1) and 0.071134 (gear3, p5) on 300 frames.
- **Harmonic mean**: libvmaf's harmonic mean in FFmpeg's logs is `n / sum(1 / (x + 1)) - 1` for all four rungs, at the log's 6 decimals; the plain `n / sum(1 / x)` differs (gear2: 28.434599 against 28.428817), and it is undefined when a frame scores 0, as 416x234's frames do.
- The analyzer's recorded `vmaf_p5` is today's rule, and its recorded min is the series' minimum, on every rung.
- **Every frame after D-016** (`eval/pooling/acceptance-full/pooling.md`, from both sides of the committed §5 report; P-027): the minimum is again the first frame's score in every series, and for gear4, gear3 and gear2 the only frame at it; without the first frame, gear2's minimum is 18.126492 where its minimum is 16.070202. gear1's minimum is 0, which 53790 of its 53946 frames score, so its plain harmonic mean is undefined. The three percentile definitions differ by at most 0.000276 (gear3, p1). libvmaf's harmonic mean in FFmpeg's logs is the shifted form for all four pairs, e.g. 27.751821 for gear2, where the plain form gives 27.746387.

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

## D-011: the source clips of the self-encoded ladders

**Question.** D-011 has the evaluation encode its own HLS ladders from public source clips, each clip serving as the mezzanine, and leaves the clips and the encoding parameters to the owner when encoding starts. Which clips, and which parameters (rungs, sizes, codec and bitrates, segment length, color tags)?

**What waits for them.**

- §1.2's acceptance: both reference modes on one ladder (above).
- How much the scaler and the model change the scores of a ladder encoded from one source (#3, #4): `scalers-300/` and `models-300/` measure bipbop's rungs, which were rendered separately.
- #5's measurement, which needs a 10-bit SDR ladder.
- §1.4's tests with dropped and duplicated frames and discontinuities, which can be cut from such a ladder (`docs/plans/frame-alignment.md`).
- Every score meant for the paper: the bipbop scores only compare the tools (D-005, D-011).

**What Phase 1's runs say about the choice.**

- Clips with explicit color matrix and range tags stay clear of the untagged rules of D-016 above, under which an untagged input is taken as BT.601 with limited range.
- Each side of each pair of the full-length §5 run took from 1134.6 to 1396.7 s for 53946 frames against a 1920x1080 reference, four processes running at a time (the pairs' `delta.md` in `eval/consistency/acceptance-full/plain/`). The clips' length sets how long a rerun of `eval/run_all.sh` takes.
- §8 needs the clips listed in `eval/streams.md` with a way to fetch them (`eval/streams/fetch_streams.py` fetches HLS playlists only, P-023), so their terms have to let others download them.

No candidate clip was examined in Phase 1 (`eval/streams.md`, MEASUREMENT PENDING).

**Agent's recommendation.** Choose the clips before the ladder mode of §1.2 is built, so that its acceptance can run on them; a 10-bit SDR source would also serve #5.

## The config file (the work order's engineering constraints)

The work order asks for every configurable item as a command line option and in a config file. `mirage_vmaf`'s options are all on its command line: `--ref`, `--dis`, `--out`, `--frames`, `--scaler`, `--model`, `--model-file`. Fixed in the code, not configurable: libvmaf's threads and subsampling, the 8-bit yuv420p scoring format (#5), and pairing in decode order (#7). The player gets no new flags.

**A design for the owner to choose from** (not built: the format is the owner's call):

- `mirage_vmaf --config FILE`, with the same names as the long options, one setting per line (`scaler = bicubic`), read through the same option table as the command line, so that both accept exactly the same settings; or JSON; or TOML, which needs a parser the repository doesn't have.
- The command line overrides the file.
- The report records the file's path and SHA-256 next to the effective settings, which it already records.
- For `run_delta.sh`, the same, or a waiver for the eval tooling, since `run.cmd.txt` records every option of a run.

**Agent's recommendation**: the `name = value` lines through the option table, with the command line overriding the file and the file's SHA-256 in the report: no new dependency, and nothing that can be set one way but not the other.

## #11 The rest of the build strategy (§8, D-009)

D-009 standardized FFmpeg on ffmpeg-full and left two parts of #11 open: whether glad's generated output is committed, and the scope of Linux support. The work order's §8 also asks for a Docker or Nix environment to reproduce the evaluation, which needs both answers. Nothing of this is implemented. What the §8 work that needed no decision did is in `docs/BUILD.md`, `eval/streams.md` and `eval/README.md`; the process choices behind it are P-020 to P-025 in `docs/DECISIONS.md`.

### #11a Is glad's generated output committed?

**Today.** glad 2.0.8 (`CMakeLists.txt`, pinned by tag) generates the OpenGL 4.1 core loader while the player builds, with the Python interpreter CMake found when it configured (`glad_add_library(glad_gl_core_41 STATIC REPRODUCIBLE API gl:core=4.1)`). On the machine of the build checks, Homebrew's Python 3.14.7 can't import `pyexpat`, so the build stops at that step with the default Python, and the venv of `docs/BUILD.md` step 3 (Python 3.13 with jinja2 and MarkupSafe pinned) avoids it (P-003; both records in `eval/build_check/`). `mirage_vmaf` and `mirage_tests` don't use OpenGL and build without the venv.

**Measured** (`eval/build_check/macos-2026-10-23-6a6661f.txt`, "Generated glad files, release vs debug"):

- glad generates three files: `include/KHR/khrplatform.h` (11131 bytes), `include/glad/gl.h` (872844 bytes) and `src/gl.c` (623188 bytes), with their SHA-256 in the record.
- The debug and the release build generate identical files, and P-003 found them identical to the main working tree's build output.
- `gl.h` says `Loader generated by glad 2.0.8 on -` (no date, because of `REPRODUCIBLE`) and `SPDX-License-Identifier: (WTFPL OR CC0-1.0) AND Apache-2.0`.

**Options.**

1. Keep generating at build time, with the venv documented (today). Every build of the player needs Python with jinja2, and a Homebrew Python update can break the step again as Python 3.14.7 did.
2. Commit the three generated files, for example under `third_party/glad/`, build them as a plain static library, and keep a script that regenerates them with glad 2.0.8 in the pinned venv and compares the bytes. The build then runs no Python. The repository gains the three files (sizes above), and their license terms (WTFPL or CC0-1.0, and Apache-2.0 for what comes from the Khronos specification, as `gl.h` states) have to be stated next to them; how that sits with GPL-3.0-or-later belongs with D-008's open question, whether legal or the company's open-source process has to confirm the license.
3. Pin the generation environment instead: have CMake create the venv itself at configure time with the pinned packages, or refuse a Python newer than one known to work. The build stays dependent on Python and, for a venv, on PyPI at configure time.

**Agent's recommendation.** Option 2. The glad step is the only build step that failed in either build check, the files it generates are the same in every build so far, and committing them removes Python from the player's build.

### #11b How far does Linux support go?

**Today.** Only macOS on Apple Silicon is built and documented:

- Both presets of `CMakePresets.json` put `/opt/homebrew/opt/ffmpeg-full/lib/pkgconfig` on `PKG_CONFIG_PATH`.
- `CMakeLists.txt` configures every target at once: OpenGL and GLFW are required, and nativefiledialog-extended v1.2.1 requires GTK on Linux, or D-Bus with its portal option (its `src/CMakeLists.txt` in the fetched sources, read, not built), even when only `mirage_vmaf` is to be built. Apple frameworks (CoreVideo, IOSurface) are linked under `if(APPLE)`.
- The player has macOS-specific code: VideoToolbox decoding (`src/Decoder.cpp`) and the IOSurface texture path (`src/VideoRenderer.cpp`, under `#ifdef __APPLE__`).
- The libvmaf package version that D-006 records is read from a Homebrew Cellar path (`CMakeLists.txt`, `eval/consistency/run_delta.py`); elsewhere it would read `unknown`.
- The eval scripts take the FFmpeg, ffprobe and model directory as options, with Homebrew paths as defaults, and `run_delta.py` finds linked libraries with `otool -L` or `ldd`.

Nothing has been built or run on Linux, so whether the code compiles there, which FFmpeg 8 and libvmaf 3 builds a distribution offers, and whether the scores equal those measured on Apple Silicon are all MEASUREMENT PENDING.

**Options.**

1. macOS only, stated in the paper. This differs from §8, which asks for build instructions for Linux and macOS.
2. Headless Linux: `mirage_vmaf`, `mirage_tests` and the eval tooling on Linux; the player stays macOS-only. Needs a CMake option that configures without the player's dependencies (OpenGL, GLFW, glad, Dear ImGui, nativefiledialog-extended), a Linux preset, Linux instructions in `docs/BUILD.md`, and a §5 run on Linux.
3. Full Linux support, the player included: an OpenGL 4.1 core context (for example through Mesa), GLFW, nativefiledialog-extended with GTK or its portal backend, and software decoding where VideoToolbox is missing.

**Agent's recommendation.** Option 2. Every committed score comes from `mirage_vmaf` and the eval tooling, which use only FFmpeg and libvmaf; the player isn't needed to reproduce a number, and a headless build is what a Docker image (#11c) can run.

### #11c Docker or Nix?

**Today.** Neither exists. §8 asks for one of them to reproduce the evaluation.

**What the build checks show.** Homebrew can't install the versions the measurements used (`docs/BUILD.md`, step 1, and the help of `brew pin` and `brew extract` in `eval/build_check/macos-2026-10-23-6a6661f.txt`), and its current ffmpeg-full, libvmaf and GLFW are already newer than those (the same record, "installed vs current stable").

**Options.**

1. A Docker image for Linux: a base image pinned by digest; FFmpeg 8.1.1 and libvmaf built from release sources checked by SHA-256, the versions of the committed measurements; `mirage_vmaf` built from the repository; `eval/run_all.sh` as the command it runs, whose comparison says whether the container reproduces the committed scores. Needs #11b option 2 or 3, and doesn't cover the macOS build or the player.
2. A Nix flake: every dependency pinned through one nixpkgs revision, on macOS (aarch64-darwin) and Linux, so the macOS build could be reproduced as well. Needs a nixpkgs revision, or overrides, with the FFmpeg and libvmaf versions wanted. Not examined here.
3. Both.
4. Neither: the versions recorded in `docs/BUILD.md`, with `eval/run_all.sh` to show whether another environment reproduces the scores.

**Agent's recommendation.** Option 1, with #11b option 2. A container can build FFmpeg and libvmaf at exactly the versions of the measurements, which Homebrew can't, and `eval/run_all.sh` already reruns and compares everything without a display. Whether the container's scores equal those measured on Apple Silicon would be its first measurement.

## README.md beyond the brew line

**Question.** The §8 work changed `README.md` only where it had to: the brew line now installs ffmpeg-full (D-009), a note points to `docs/BUILD.md`, and a License section states GPL-3.0-or-later (D-008). Other parts describe an older state of the repository. Should they be brought up to date, and how?

**What differs from the repository today.**

- "Running tests" counts 34 tests covering `NetworkLogger` and `ManifestParser`, run with `ctest`; `mirage_tests` is now built from 26 test files (`CMakeLists.txt`), and `docs/BUILD.md` step 4 runs it without the `[audio]` tests, as the build checks do.
- "First configure takes ~1–2 minutes on a fast connection" rests on no committed record. The configures of the verification run of `eval/run_all.sh` took 83.3 s and 97.0 s ("cmake --preset macos-release" in `eval/run_all/2026-10-23/run_all.md`), on one machine and connection.
- "libvmaf model not found at runtime" says the model is loaded from a file under Homebrew's prefix; the analyzer loads built-in models by version (`vmaf_model_load` in `src/VMAFAnalyzer.cpp`) and reads a file only with `--model-file`.
- The Python note suggests `pip3 install jinja2 --break-system-packages`, which installs into the Python that Homebrew manages; the build checks used the venv of `docs/BUILD.md` step 3.
- The project tree lists two test files, and neither `tools/`, `docs/` nor `eval/`.
- The FetchContent list leaves out stb, and gives miniaudio without the commit `CMakeLists.txt` now pins it to (P-021).

**Options.**

1. Correct these parts from the repository's files and the committed records.
2. Make the README's build part a short pointer to `docs/BUILD.md`, and correct the rest.
3. Leave the README as it is.

**Agent's recommendation.** Option 2: one place for the build steps that the build checks verify, and a README whose facts match the repository.

## Smaller items found in Phase 1

- **The subtitle test's fixture.** In a clean checkout `SidecarSubtitles — decodes data/sub_test/test.srt` fails: it reads `data/sub_test/test.srt`, which `.gitignore` leaves out of the repository (`eval/build_check/macos-2026-10-23-6a6661f.txt`, "Unit tests"); in the main working tree the file exists and the test passes (`eval/test_runs/macos-2026-10-23-8aca01a.txt`). Options: commit the file with a `.gitignore` exception; move it to a tracked test directory; have the test write it to a temporary file, as the other media tests generate their inputs; leave it, as `docs/BUILD.md` says. **Agent's recommendation**: have the test write it, a change to the test only.
- **An intermittent test.** `Demuxer: seamless video and audio switches on a local file` (`tests/test_demuxer_switch.cpp`) fails now and then, at the commit Phase 1 started from as on this branch: run alone 650 times, a build of `37472fd` failed 5 times and one of `855a71b` 4 times, each time at one of the same two assertions (`eval/test_runs/demuxer-switch-150-2026-10-23.txt` and `demuxer-switch-500-2026-10-23.txt`, P-031; summed up in `eval/test_runs/README.md`). The test and `src/Demuxer.*` are the same at both commits. It passed in the test records at `8aca01a` and `9855319` (`eval/test_runs/`). A fix may touch `Demuxer`, which is on the decode path (D-007). Options: leave it and rerun on failure; a plan that finds out whether the test's timing or the track switch is at fault; leave it out of unattended runs. **Agent's recommendation**: the plan, since a race in track switching would matter for §3 and §4.
- **D-009's alternatives.** D-009 says that standardizing on `ffmpeg` would need a separately installed FFmpeg with libvmaf, but Homebrew's `ffmpeg` 8.1.1 on the machine of the measurements was built with `--enable-libvmaf` as well (`eval/baseline/environment.txt`, line 163). The decision stands on its other reasons. D-009 is the owner's entry and wasn't edited; options: a correction note, as D-001 has; leave it.
- **Server addresses in the committed logs.** The delta runs keep FFmpeg's verbose logs, xz-compressed, and these include FFmpeg's TCP connection lines with the IP addresses of Apple's CDN servers: public addresses, not local information, so the runs' privacy scan doesn't flag them (P-011). Options: keep the logs as FFmpeg wrote them; filter the network lines out of the stored logs, which would then no longer be raw; lower the log level of FFmpeg's network layer only. **Agent's recommendation**: keep them raw.
- **D-008's open question**: whether legal or the company's open-source process has to confirm GPL-3.0-or-later before the repository is made public again. `LICENSE` is committed as D-008 decided (P-020). This is the owner's to check.
- **`vcpkg.json`.** The repository's root `vcpkg.json` lists FFmpeg and GLFW, without libvmaf or versions, and the build doesn't use it (`docs/VMAF_GAP_REPORT.md`, "Specific gaps for §8"); `docs/BUILD.md` doesn't mention it. Options: remove it; make it a second, pinned way to get the dependencies, libvmaf included, together with #11b and #11c; leave it. **Agent's recommendation**: remove it, unless #11b or #11c chooses vcpkg.

## Process choices to confirm

P-006 to P-032 in `docs/DECISIONS.md` are the agent's measurement and process choices of Phase 1, each pending the owner's confirmation. The alternatives the agent pointed out for each, as its entry lists them:

| Entries | What | Alternatives worth a look |
|---|---|---|
| P-006 | what `mirage_vmaf` records, and when it writes no report | the first decoded frame's color tags as well as the decoder's; libvmaf's package version read at run time from the loaded library; a report with an error field instead of none |
| P-007, P-022 | the build checks in clean worktrees | a virtual machine, another macOS account or a hosted runner, starting without the Homebrew packages |
| P-008 | FFmpeg's side of the delta tool | Phase 0's `-frames:v` instead of `trim` (FFmpeg's scores were the same over 300 frames, `diag-before-300/checks.txt`, check 1) |
| P-009 | how a pair passes | make some warnings failures, such as different libvmaf versions on the two sides or pairs with different PTS |
| P-010 | the relabel diagnostic | relabel the color matrix only |
| P-011 | what a delta run records | uncompressed logs; absolute paths |
| P-012, P-015, P-016, P-026 | the runs, with the release build of the main working tree at a clean HEAD | builds in clean worktrees; the scaler and model runs over every frame |
| P-013 | the synthetic color tag cases | H.264 clips encoded without tags |
| P-014 | how Mirage converts | the tags of the decoders' stream parameters or of the first pair instead of each frame's; as many swscale threads as FFmpeg's scale filter |
| P-015 | the delta tool's warning when the two sides' conversions differ | a failure instead |
| P-017 | `--scaler`, `--model`, `--model-file`, `reference_mode` | `--scaler` for the reference's conversion too |
| P-018 | the pooling measurement | more percentile definitions |
| P-019 | ladder scoring and the config file not built yet | build them now (their open choices are above) |
| P-020 | the `LICENSE` text | SPDX license notices in every source file |
| P-021 | the pins | the five tagged dependencies pinned by commit as well; stb at `003501f6`, which the main working tree's Phase 1 builds fetched |
| P-023 | the stream fetch keeps playlists only | mirror the media for runs without network, after checking that a copy scores as the URLs do |
| P-024, P-025 | `eval/run_all.sh` and its verification run | commit the rerun outputs; rerun the runs after D-016 with the programs they were made with |
| P-027 | the full-length steps of `run_all.sh`, and pooling over every frame | leave the full-length run out of `run_all.sh` |
| P-028 | the record of the test runs behind the PR description | the outputs in the PR description only |
| P-029 | the reference format cases, synthetic and lossless, before and after D-016 | real streams in these formats; more formats, such as RGB, gray or 12-bit |
| P-030 | a warning, not a refusal, for a reference that isn't yuv420p, until the owner decides | refuse such references; warn only where the tags differ |
| P-031 | repeated runs of the intermittent Demuxer test at the start and the head of Phase 1 | the whole suite repeated; one series only |
| P-032 | the unit test record and a rerun of every measurement after the review fixes' code changes | keep citing the earlier records; a rerun without the full-length steps |

**Agent's recommendation**: confirm them all.

## Differences between the work order and the implementation found in this work (hard rule 4)

Reported, not edited:

1. §1.3 gives §5 passing as the reason for bicubic; §5 passes with all three scalers (#3).
2. §7's `reference_mode` has only `mezzanine` and `top-rung`; `mirage_vmaf` writes `explicit-pair` (§1.2 above).
3. §5's command, `ffmpeg -i <distorted> -i <reference> -lavfi libvmaf=model=<same>`, has no scale filter, and FFmpeg's libvmaf filter refuses inputs of different sizes (`input width must match`, `eval/consistency/ffmpeg-libvmaf-filter/checks.txt`, check 4). The §5 tool adds `scale=W:H:flags=<scaler>` to the distorted input, as Phase 0's diagnostic did (P-005, P-008).
4. §8 asks for FFmpeg 8.x, libvmaf 3.x, OpenGL 4.1, miniaudio and GLFW to be pinned, with their exact versions recorded. The versions are recorded (`docs/BUILD.md`, "Versions"), miniaudio is pinned by commit, and the generated loader fixes OpenGL at 4.1 core; FFmpeg, libvmaf and GLFW come from Homebrew, which can't pin them (#11c).
5. §8 asks for build instructions for Linux and macOS, Apple Silicon included. Only macOS on Apple Silicon is documented and checked (#11b).
6. §8's acceptance is a clean machine building as the README says. On the machine of the build checks, the release preset stops at glad with the default Python, and the README's quick start (`macos-debug`) runs the same glad step; the README now points to `docs/BUILD.md`, whose steps build both presets; and the check ran in a clean worktree, not on a clean machine (P-022).
7. §8 has `eval/run_all.sh` rerun all the paper's numbers and figures. There are none yet (D-005), so it reruns the committed Phase 1 measurements (P-024).
8. §8 asks for `eval/streams.md` with a fetch script, and lists DASH-IF test vectors and Unified Streaming demo streams. `eval/streams/fetch_streams.py` fetches the playlists and probes the streams but keeps no media (P-023), and the DASH-IF and Unified Streaming streams were not examined (`eval/streams.md`).
9. §1.1 to §1.3 ask the output to carry the libvmaf version and the model identifier, the `reference_mode` and the scaler. `mirage_vmaf`'s report does (`src/VMAFReport.h`). The player's own JSON export (`VMAFAnalyzer::write_json` in `src/VMAFAnalyzer.cpp`) still has its Phase 0 layout, which a test pins, without any of them, and the player has no scaler or model option. Options: give `write_json` those fields; have the player write `mirage_vmaf`'s report format; leave the player's export until §7's schema.
10. §5's target is a per-frame difference "< 0.01"; D-019 passes a frame at exactly 0.01 (D-019 above).
11. §5's target, under 0.01 per frame with the default configuration, is met for yuv420p references, and not for every reference in another pixel format: not where its tags differ from the distorted input's, nor for the full range yuvj420p reference with the same tags. The reason is the color conversion, which §5 asks the documents to state ("D-016: references that aren't yuv420p" above; `eval/consistency/README.md`).
