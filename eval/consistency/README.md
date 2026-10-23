# Section 5 consistency check

`run_delta.sh` compares Mirage's per-frame VMAF scores with those of FFmpeg's libvmaf filter, over the same frames of the same inputs, and writes a delta report per pair and for the whole run (work order §5; `docs/DECISIONS.md` D-017 to D-019). Mirage's side is `mirage_vmaf` (D-017). FFmpeg's side is the `ffmpeg` program of ffmpeg-full (D-009), used here only as the reference to check against.

## Running it

```
eval/consistency/run_delta.sh <mirage_vmaf> <out_dir> [--frames N] [--jobs J] [--pairs <file>]
    [--scaler NAME] [--model NAME | --model-file FILE] [--threads N] [--threshold X]
    [--diagnostic-relabel] [--ffmpeg <ffmpeg>] [--ffprobe <ffprobe>] [--timeout S]
```

- `<mirage_vmaf>`: the program to check, such as `build/macos-release/mirage_vmaf`. For outputs that are to be committed, build it from a committed, clean tree: the run records the commit, whether the working tree was clean at its start and end, and the program's SHA-256.
- `<out_dir>`: a new or empty directory.
- Defaults: the four pairs of `pairs/bipbop16x9.txt` (each lower bipbop 16x9 rung against gear5, D-018), every frame, model `vmaf_v0.6.1`, scaler `bilinear`, 4 libvmaf threads, threshold 0.01 (D-019), `/opt/homebrew/opt/ffmpeg-full/bin/ffmpeg` with the `ffprobe` next to it, one scoring process at a time, and a limit of 6 hours per process. `--help` lists the options.
- It needs only Python 3 and its standard library; `run_delta.sh` runs it with `python3 -I -B` (`-B`: no `__pycache__` in the working tree, whose state the run records). Inputs given as URLs are fetched again on every run.
- Exit status: 0 every pair passed; 1 at least one pair failed (the reports say why); 2 bad arguments, or the run could not start; 3 an output still contains a local path or name after the placeholders, and must not be committed.
- A pairs file has one `<label> <reference> <distorted>` per line; `#` starts a comment line. The label names the pair's directory.

`--scaler`, `--model`, `--model-file` and `--threads` set FFmpeg's side: the `flags=` of its scale filter, the `model=version=NAME` or `model=path=FILE` of its libvmaf filter, and its `n_threads`. `mirage_vmaf` is given `--scaler`, `--model` and `--model-file` only if its `--help` lists them, as it does since commits `6a9aca2` (`--scaler`) and `44cd4b5` (`--model`, `--model-file`); one that lists none of them scores with its fixed settings (bilinear, `vmaf_v0.6.1`). Its 4 libvmaf threads are fixed. A pair fails with `SETTINGS MISMATCH` when `mirage_vmaf`'s report has other settings than FFmpeg was given; with `--model-file`, the report must give `model_source` `file` and the SHA-256 the tool computes of the file. The path of a model file may not contain characters that mean something in a filter graph, such as `:` or `,`, or white space.

## What it runs for each pair

In `<out_dir>/<pair>/`, in this order (the exact commands are in that directory's `cmd.txt`):

1. `ffprobe` on both inputs, for the reference's size (FFmpeg scales the distorted input to it) and both inputs' color tags.
2. `mirage_vmaf --ref <reference> --dis <distorted> --out mirage.json [--frames N]`, plus `--scaler`, and `--model` or `--model-file`, as above.
3. FFmpeg's libvmaf filter, with the filter graph of Phase 0's diagnostic comparison (`eval/baseline/diagnostics/ffmpeg_libvmaf_gear2_vs_gear5_first300.cmd.txt`, P-005). With `--frames N`:

   ```
   <ffmpeg> -hide_banner -nostdin -nostats -v verbose -i <distorted> -i <reference> -an -lavfi
       '[0:v]trim=end_frame=N,scale=W:H:flags=bilinear,format=yuv420p[d];[1:v]trim=end_frame=N,format=yuv420p[r];
        [d][r]libvmaf=model=version=vmaf_v0.6.1:n_threads=4:log_fmt=json:log_path=ffmpeg.json' -f null -
   ```

   (one line in `cmd.txt`; without `--frames` there is no `trim`). How this differs from Phase 0's command, and why, is P-008: the reference's size in place of a fixed `1920:1080`, `trim` in place of `-frames:v`, no audio, and a verbose log.

Then it compares the two sides frame by frame and writes the reports.

## How a pair passes (P-009)

Frame i of `mirage_vmaf`'s report is compared with frame i of FFmpeg's libvmaf log, and delta = Mirage − FFmpeg. A pair passes only if

- both programs exited with status 0 and left their outputs;
- `mirage_vmaf`'s report has the settings FFmpeg was given: model, scaler, threads, no subsampling, the frame limit, yuv420p at 8 bits, the reference's size;
- both sides scored the same number of frames, and more than none (a mismatch shows as `FRAME COUNT MISMATCH`);
- no frame has |delta| greater than the threshold.

The reports also give the largest and mean |delta|, the mean delta, the frames over the threshold, the frames with the largest |delta|, both sides' pooled mean and min as libvmaf computes them, both sides' libvmaf versions at run time and their package versions (D-006: `mirage_vmaf`'s from its report, FFmpeg's from its libvmaf log and from the library its libavfilter links), how each side converted the distorted frames' color matrix and range (FFmpeg's scale filter lines, and the `dis_conversion` of `mirage_vmaf`'s report where it has one, D-016), and the warnings: checks that don't fail a pair, such as different libvmaf versions on the two sides, Mirage pairs whose two frames have different PTS, tags in FFmpeg's log that differ from ffprobe's, or a `dis_conversion` whose tags differ from those of FFmpeg's scale filter.

FFmpeg's side pairs frames by timestamp, Mirage's in the order they are decoded, so a comparison by position is only right while both sides pair the same frames; the PTS warning checks that from Mirage's side.

## The diagnostic relabel variant (P-010)

With `--diagnostic-relabel`, FFmpeg's distorted input gets the reference's `color_space` and `color_range` tags with `setparams` before its scale filter, for each tag that differs between the two inputs (ffprobe's values), so that FFmpeg's scale filter converts nothing but the size. For pairs whose tags are the same, the FFmpeg command is the plain one. Mirage's side doesn't change. This variant is a diagnostic, not the §5 comparison: its reports say so in their first lines.

## Files of a run (P-011)

| File | Contents |
|---|---|
| `run.cmd.txt` | the command of the run |
| `pairs.txt` | the pairs file it read |
| `environment.txt` | git HEAD and `git status --porcelain` at the start and end of the run; OS, CPU, kernel and Python; `ffmpeg -version` and `ffprobe -version`; the libvmaf library FFmpeg's libavfilter links, with the package version in its real path; `mirage_vmaf`'s path, SHA-256 and size, the libvmaf it links and the first line of its `--help` |
| `delta.md`, `delta.json` | the report of the whole run: a table of all pairs, why pairs fail, the warnings, and the parameters, environment and git state |
| `<pair>/cmd.txt` | every command run for the pair |
| `<pair>/ffprobe_ref.json`, `ffprobe_dis.json` | ffprobe's stream parameters of both inputs |
| `<pair>/mirage.json`, `mirage.stdout.txt`, `mirage.stderr.log.xz` | `mirage_vmaf`'s report (`src/VMAFReport.h`), output and log |
| `<pair>/ffmpeg.json`, `ffmpeg.stderr.log.xz` | FFmpeg's libvmaf log and its verbose log |
| `<pair>/mirage.json.xz`, `ffmpeg.json.xz` | in place of `mirage.json` or `ffmpeg.json` when that is larger than 1 MiB, as in full-length runs |
| `<pair>/ffmpeg_colorspace_lines.txt` | the lines of FFmpeg's verbose log that state color spaces: the input and output video streams, the filter graph's inputs, the scale filters' input and output, swscale's notes, the libvmaf filter's warning that its inputs' frames have different color ranges, and FFmpeg's error when it finds no conversion between two filters' formats |
| `<pair>/delta.md`, `delta.json` | the pair's delta report |
| `SHA256SUMS` | SHA-256 of every file; check with `shasum -a 256 -c SHA256SUMS` in the directory |

- The `.xz` files are the logs compressed with xz; `xz -dc` restores them. Empty outputs aren't kept.
- Local paths in every file are replaced by `<out_dir>`, `<repo>`, `<home>` and `<tmp>`, and the programs appear as `<mirage_vmaf>`, `<ffmpeg>` and `<ffprobe>`; `environment.txt` says which programs those were. `delta.json` counts the replacements per file under `redactions`.

## Tests

```
python3 -I -B -m unittest discover -s eval/consistency/tests -v
```

`tests/test_vmafdelta.py` covers the delta math, the commands, the parsing of FFmpeg's log and the reports. `tests/test_run_delta.py` runs the whole tool with stand-ins for `mirage_vmaf`, `ffmpeg` and `ffprobe` (`tests/fake_tools.py`), without network. `tests/test_color_cases.py` covers `color_cases.py` without running FFmpeg.

## Color tag cases (D-016)

`color_cases.py` makes the inputs that show what FFmpeg's §5 filter graph does with the distorted input's color matrix and range when the two inputs' tags differ, or are missing (D-016):

```
python3 -I -B eval/consistency/color_cases.py <clips_dir> [--ffmpeg <ffmpeg>] [--ffprobe <ffprobe>] [--frames N]
```

- The clips are FFmpeg's `testsrc2` pattern, 10 frames by default at 30000/1001 fps, yuv420p, uncompressed in Matroska: references at 1920x1080, distorted inputs scaled to 640x360 (like bipbop's gear5 and gear2). `setparams` gives each clip its tags; the pixels of all clips of one size are the same, which the script checks.
- `<clips_dir>/pairs.txt` lists the cases FFmpeg's graph scores, `<clips_dir>/pairs_refused.txt` those it refuses (a YCgCo matrix, which libswscale doesn't have). Both are pairs files for `run_delta.sh --pairs`; each case's label names the reference's tags, then the distorted input's, and the file's comment lines say what each case is.
- `<clips_dir>/clips.txt` records, for every clip, the command that made it, its stream parameters as ffprobe reads them back, and the MD5 of its decoded frames, with `<clips_dir>`, `<ffmpeg>` and `<ffprobe>` in place of local paths.
- Exit status: 0 the clips have the intended tags and pixels; 1 a program failed or a clip is not as intended; 2 bad arguments.

`run_delta.sh` shows local paths under `TMPDIR` as `<tmp>`, so the clips belong under `TMPDIR` (or `TMPDIR` set to a directory that holds them) when a run's outputs are to be committed.

## Committed runs

### `diag-before-300/`: the diagnosis before D-016, first 300 frames (P-012)

Two runs over the first 300 frame pairs of the four pairs of D-018, with the `mirage_vmaf` that doesn't convert color yet, at commit `ff98a1320b7f258a0856c3c8ba341b63c750d85a`:

```
eval/consistency/run_delta.sh build/macos-release/mirage_vmaf <out>/plain --frames 300
eval/consistency/run_delta.sh build/macos-release/mirage_vmaf <out>/relabel --frames 300 --diagnostic-relabel
```

| File | Contents |
|---|---|
| `build.txt` | the `mirage_vmaf` of both runs, checked right before them: HEAD, a clean working tree, no change under `src`, `tools`, `tests` or the CMake files since stage 1 built it at `83c8ed6`, `ninja: no work to do`, and its SHA-256, which both runs' `environment.txt` repeat |
| `plain/` | the run with plain FFmpeg, the §5 comparison |
| `relabel/` | the run with `--diagnostic-relabel` |
| `checks.txt` | checks made on both runs afterwards, each command with its output: against Phase 0's outputs, at FFmpeg's 6 decimals, the frames of gear1 that pass, and the checksums |
| `SHA256SUMS` | SHA-256 of `build.txt`, `checks.txt` and the two runs' own `SHA256SUMS` |

Results, from `plain/delta.md` and `relabel/delta.md` (|delta| = |Mirage − FFmpeg| per frame, 6 decimals):

| pair | plain | max \|delta\| | mean \|delta\| | frames over 0.01 | FFmpeg's scale, distorted color space | relabel | max \|delta\| | frames over 0.01 |
|---|---|---|---|---|---|---|---|---|
| gear4_vs_gear5 | PASS | 0.000000 | 0.000000 | 0 | bt709 -> bt709 | PASS | 0.000000 | 0 |
| gear3_vs_gear5 | PASS | 0.000000 | 0.000000 | 0 | bt709 -> bt709 | PASS | 0.000000 | 0 |
| gear2_vs_gear5 | FAIL | 5.674685 | 5.279836 | 300 | smpte170m -> bt709 | PASS | 0.000000 | 0 |
| gear1_vs_gear5 | FAIL | 3.883766 | 1.878387 | 290 | smpte170m -> bt709 | PASS | 0.000000 | 0 |

What they show:

- **Plain FFmpeg.** The two pairs whose distorted rung has the reference's color tags (gear4 and gear3, bt709 like gear5) pass: written with the 6 decimals of FFmpeg's log, Mirage's score of every frame is FFmpeg's (`checks.txt`, check 3), and FFmpeg's scale filter converts nothing but the size. The two pairs whose distorted rung is tagged smpte170m fail, gear2 on all 300 frames and gear1 on 290. In both, the mean delta equals the mean |delta| (`plain/gear2_vs_gear5/delta.json`, `plain/gear1_vs_gear5/delta.json`), so Mirage's score is never below FFmpeg's. The 10 gear1 frames within the threshold, index 0, 30, ..., 270, score 0 on both sides (check 4). The pooled means of gear2 are 33.832772 (Mirage) and 28.552935 (FFmpeg), those of gear1 1.884503 and 0.006117 (`plain/delta.md`).
- **What FFmpeg converts.** For gear2 and gear1, FFmpeg's verbose log (`plain/gear2_vs_gear5/ffmpeg_colorspace_lines.txt`, and the same file for gear1) shows the filter graph's distorted input as `csp:smpte170m`, the scale filter converting it `csp:smpte170m range:tv -> ... csp:bt709 range:tv`, and swscale's note `YUV color matrix differs for YUV->YUV, using intermediate RGB to convert`. The output stream is tagged `yuv420p(tv, bt709/smpte170m/bt709, progressive)`: the color matrix tag is now the reference's, while the primaries (smpte170m) and transfer (bt709) tags are still the distorted input's. For gear4 and gear3 the scale line has `csp:bt709` on both sides, and swscale has no note.
- **The relabel run.** With `setparams=colorspace=bt709` before FFmpeg's scale for gear2 and gear1 (of the two tags the relabel looks at, only `color_space` differs from gear5's; `color_range` is `tv` for every rung, `ffprobe_*.json`), all four pairs pass, and every frame of every pair agrees to FFmpeg's 6 decimals (check 3). The scale lines of gear2 and gear1 then read `csp:bt709 -> csp:bt709`, without swscale's note (`relabel/*/ffmpeg_colorspace_lines.txt`). FFmpeg's pooled means of gear2 and gear1 become Mirage's, 33.832772 and 1.884503 (`relabel/delta.md`). For gear4 and gear3 nothing is relabeled, and the results are the plain run's.
- **Against Phase 0.** FFmpeg's side of the plain run has every libvmaf metric of all 300 frames of gear2 and gear1 equal to Phase 0's FFmpeg diagnostic (check 1), although Phase 0 stopped FFmpeg with `-frames:v 300` and these runs use `trim` (P-008). `mirage_vmaf`'s scores equal those of Phase 0's run of the player's analyzer at the 4 decimals that run wrote, on all 300 frames of each rung (check 2). The plain run's gear2 and gear1 figures agree with `eval/baseline/diagnostics/compare_first300.txt` (max |delta| 5.6747 and 3.8838, mean |delta| 5.2798 and 1.8784, 300 and 290 frames over 0.01) at those 4 decimals.

So in these 300 frames, the whole difference between Mirage and FFmpeg on gear2 and gear1 comes with FFmpeg's conversion of the distorted frames from the smpte170m matrix to the reference's bt709 matrix, which Mirage doesn't make: without it, the two agree to the precision of FFmpeg's log. D-016 has Mirage convert as FFmpeg does. The §5 report itself, over every frame of the four rungs (D-018), comes after D-016 is implemented: MEASUREMENT PENDING.

Limits:

- Only the first 300 frames (P-012), and only the bilinear scaler on both sides; D-003's comparison of the scalers, with a `mirage_vmaf` that takes one, is in `scalers-300/`.
- The inputs are Apple's streams as fetched during the runs; they aren't kept here.
- These scores only compare the two tools, like Phase 0's (D-005). bipbop's rungs were rendered separately (D-011), so the scores say nothing about the rungs' quality.

### `color-cases-before/`, `color-cases-after/`: what FFmpeg's graph converts, and Mirage before and after D-016 (P-013, P-015)

D-016 has Mirage convert the distorted frames' color matrix and range as FFmpeg's filter graph does, untagged streams included. These runs show what FFmpeg 8.1.1 does, on the synthetic clips of `color_cases.py` (10 frames each; `color-cases-before/clips.txt` records how each clip was made, its tags as ffprobe reads them back, and the MD5 of its decoded frames, the same for all clips of one size):

```
python3 -I -B eval/consistency/color_cases.py <tmp>/clips
TMPDIR=<tmp> eval/consistency/run_delta.sh build/macos-release/mirage_vmaf <out>/main --pairs <tmp>/clips/pairs.txt --frames 10
TMPDIR=<tmp> eval/consistency/run_delta.sh build/macos-release/mirage_vmaf <out>/refused --pairs <tmp>/clips/pairs_refused.txt --frames 10
```

| Directory | `mirage_vmaf` | Contents |
|---|---|---|
| `color-cases-before/` | built at `83c8ed6`, which converts no color; checked at `5e412a6` (`build.txt`) | `clips.txt`; `main/`, the 18 cases FFmpeg scores; `refused/`, the 2 it refuses; `checks.txt` |
| `color-cases-after/` | the one of `diag-after-300/`, built at `5263061` (`build.txt`) | the same runs on the same clips; `checks.txt` |

Per case, from each case's `ffmpeg_colorspace_lines.txt` and `delta.md` in `color-cases-before/main/`, and `color-cases-after/main/delta.md` (tags as color matrix/range, `unknown` = no tag; "via RGB" = swscale's note `YUV color matrix differs for YUV->YUV, using intermediate RGB to convert`):

| case | reference | distorted | FFmpeg's scale filter, distorted frames | via RGB | before D-016 | max \|delta\| | frames over 0.01 | after D-016 | max \|delta\| |
|---|---|---|---|---|---|---|---|---|---|
| `m01_bt709_vs_smpte170m` | bt709/tv | smpte170m/tv | smpte170m/tv -> bt709/tv | yes | FAIL | 11.884643 | 10 | PASS | 0.000000 |
| `m02_bt709_vs_bt709` | bt709/tv | bt709/tv | bt709/tv -> bt709/tv | no | PASS | 0.000000 | 0 | PASS | 0.000000 |
| `m03_bt709_vs_untagged` | bt709/tv | unknown/unknown | unknown/unknown -> bt709/tv | yes | FAIL | 11.884643 | 10 | PASS | 0.000000 |
| `m04_untagged_vs_bt709` | unknown/unknown | bt709/tv | bt709/tv -> unknown/tv | yes | FAIL | 26.462820 | 10 | PASS | 0.000000 |
| `m05_untagged_vs_smpte170m` | unknown/unknown | smpte170m/tv | smpte170m/tv -> unknown/tv | no | PASS | 0.000000 | 0 | PASS | 0.000000 |
| `m06_untagged_vs_untagged` | unknown/unknown | unknown/unknown | unknown/unknown -> unknown/unknown | no | PASS | 0.000000 | 0 | PASS | 0.000000 |
| `m07_smpte170m_vs_bt470bg` | smpte170m/tv | bt470bg/tv | bt470bg/tv -> smpte170m/tv | no | PASS | 0.000000 | 0 | PASS | 0.000000 |
| `m08_bt2020nc_vs_bt709` | bt2020nc/tv | bt709/tv | bt709/tv -> bt2020nc/tv | yes | FAIL | 3.971636 | 10 | PASS | 0.000000 |
| `r01_tv_vs_pc` | bt709/tv | bt709/pc | bt709/pc -> bt709/tv | no | FAIL | 20.969975 | 10 | PASS | 0.000000 |
| `r02_pc_vs_tv` | bt709/pc | bt709/tv | bt709/tv -> bt709/pc | no | FAIL | 22.748280 | 10 | PASS | 0.000000 |
| `r03_norange_vs_pc` | bt709/unknown | bt709/pc | bt709/pc -> bt709/unknown | no | FAIL | 20.969975 | 10 | PASS | 0.000000 |
| `r04_pc_vs_norange` | bt709/pc | bt709/unknown | bt709/unknown -> bt709/pc | no | FAIL | 22.748280 | 10 | PASS | 0.000000 |
| `r05_norange_vs_tv` | bt709/unknown | bt709/tv | bt709/tv -> bt709/tv | no | PASS | 0.000000 | 0 | PASS | 0.000000 |
| `r06_tv_vs_norange` | bt709/tv | bt709/unknown | bt709/unknown -> bt709/tv | no | PASS | 0.000000 | 0 | PASS | 0.000000 |
| `x01_bt709tv_vs_smpte170mpc` | bt709/tv | smpte170m/pc | smpte170m/pc -> bt709/tv | yes | FAIL | 3.846347 | 10 | PASS | 0.000000 |
| `x02_primaries_transfer` | bt709/tv | bt709/tv, primaries and transfer bt470bg | bt709/tv -> bt709/tv | no | PASS | 0.000000 | 0 | PASS | 0.000000 |
| `x03_chroma_topleft` | bt709/tv | bt709/tv, chroma top-left | bt709/tv -> bt709/tv | no | PASS | 0.000000 | 0 | PASS | 0.000000 |
| `x04_interlaced_tff` | bt709/tv | bt709/tv, interlaced | bt709/tv -> bt709/tv | no | PASS | 0.000000 | 0 | PASS | 0.000000 |

In `refused/`, FFmpeg exits with status 178 for both YCgCo cases, with `Impossible to convert between the formats supported by the filter ...` (`u01_bt709_vs_ycgco/ffmpeg_colorspace_lines.txt`, and the same file for `u02`). Before D-016 `mirage_vmaf` scored both; after it, it exits with status 1 and names the matrix (`color-cases-after/checks.txt`, check 5).

What "convert like FFmpeg" therefore means in FFmpeg 8.1.1, for the graph of the §5 comparison:

- **Matrix**: the distorted frames are converted to the reference's matrix tag, whatever it is, a missing tag included (`m04`, `m05`: `-> unknown`).
- **Range**: they are converted to the reference's range tag. When the reference has none, FFmpeg keeps the distorted frames' range if that is limited or missing (`r05`, `m06`) and picks none for a full range (`r03`).
- **Missing tags in the pixels**: libswscale treats a missing matrix as BT.601's and a missing range as limited. So an untagged distorted input against a BT.709 reference is converted from BT.601 to BT.709 and scores as a smpte170m one does (FFmpeg's scores of `m03` are those of `m01`, `color-cases-before/checks.txt`, check 1), a BT.709 distorted input against an untagged reference is converted to BT.601 (`m04`), and a full range input against a reference without a range tag is converted to limited (`r03` scores as `r01`, and `r04` as `r02`, check 1).
- **Tags that only rename**: smpte170m, bt470bg and a missing matrix have the same coefficients; between them only the tag changes, swscale converts no pixels, and the scores are those of no conversion (FFmpeg's scores of `m05` and `m07` are those of `m02`, check 1).
- **Not converted**: the primaries and the transfer (the output keeps the distorted input's: `bt470bg/bt470bg` in `x02`'s output stream line, check 2), the chroma siting (`x03`'s output stream line has none) and interlacing (the scale filter scales interlaced frames as progressive). FFmpeg's scores of `x02`, `x03` and `x04` are those of `m02` (check 1).
- **Refused**: a matrix libswscale has no coefficients for, such as YCgCo, on either input.

The FFmpeg 8.1.1 sources (`ffmpeg-8.1.1.tar.xz` from ffmpeg.org, whose SHA-256 is the one in Homebrew's ffmpeg-full formula) explain the logs: the libvmaf filter declares no color properties, so libavfilter makes both its inputs and its output share one list of matrices and one of ranges (`libavfilter/formats.c`, `ff_default_query_formats`); a buffer source offers only its stream's matrix, and its range plus limited when it has none (`libavfilter/buffersrc.c`); where the shared list allows it, the scale filter's output keeps its input's range, else the list's first entry is taken (`libavfilter/avfiltergraph.c`, `reduce_formats`, `pick_format`); and the scale filter clears the frame's chroma location and interlaced flag, copies its properties to the output, sets the negotiated matrix and range and calls `sws_scale_frame()` (`libavfilter/vf_scale.c`, `scale_frame`). In libswscale, the coefficients come from `sws_getCoefficients()` (`libswscale/yuv2rgb.c`), and a YUV-to-YUV conversion between different coefficients goes through BGR24 at the smaller of the two frame sizes (`libswscale/utils.c`, `sws_setColorspaceDetails`).

Mirage does the same (`src/VMAFColor.h`, P-014): its scores agree with FFmpeg's at the 6 decimals of FFmpeg's log on every frame of every case after D-016, its `dis_conversion` has the tags of FFmpeg's scale filter in every case, and it reports a matrix change exactly where FFmpeg's log has swscale's RGB note (`color-cases-after/checks.txt`, checks 2 and 3). `tests/test_vmaf_color.cpp` compares Mirage's converted frames with those of the same graph run through libavfilter, byte for byte, for the tags of these cases.

Limits: synthetic 8-bit yuv420p clips at two sizes, 10 frames each, and only the libvmaf graph of the §5 comparison. The scores of these clips only compare the two tools.

### `diag-after-300/`: the first 300 frames after D-016 (P-015)

The plain run of `diag-before-300/` again, with the `mirage_vmaf` that converts color like FFmpeg, built at `5263061c3dfa7b3134763669443977b436ab1003` from the clean main working tree (`build.txt`):

```
eval/consistency/run_delta.sh build/macos-release/mirage_vmaf <out>/plain --frames 300
```

| File | Contents |
|---|---|
| `build.txt` | the build of `mirage_vmaf` right before the run: HEAD, a clean working tree, the build's output, a second build with nothing to do, and its SHA-256, which `plain/environment.txt` repeats |
| `plain/` | the run, with plain FFmpeg: the §5 comparison |
| `checks.txt` | checks made on the run afterwards, each command with its output |
| `SHA256SUMS` | SHA-256 of `build.txt`, `checks.txt` and `plain/SHA256SUMS` |

Before and after D-016, from `diag-before-300/plain/delta.md` and `diag-after-300/plain/delta.md` (|delta| = |Mirage − FFmpeg| per frame, 6 decimals; pooled means as libvmaf computes them):

| pair | before | max \|delta\| | mean \|delta\| | frames over 0.01 | Mirage's pooled mean | after | max \|delta\| | mean \|delta\| | frames over 0.01 | Mirage's pooled mean | FFmpeg's pooled mean | conversion, both sides |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| gear4_vs_gear5 | PASS | 0.000000 | 0.000000 | 0 | 74.640607 | PASS | 0.000000 | 0.000000 | 0 | 74.640607 | 74.640607 | bt709 -> bt709 |
| gear3_vs_gear5 | PASS | 0.000000 | 0.000000 | 0 | 53.729092 | PASS | 0.000000 | 0.000000 | 0 | 53.729092 | 53.729092 | bt709 -> bt709 |
| gear2_vs_gear5 | FAIL | 5.674685 | 5.279836 | 300 | 33.832772 | PASS | 0.000000 | 0.000000 | 0 | 28.552935 | 28.552935 | smpte170m -> bt709 |
| gear1_vs_gear5 | FAIL | 3.883766 | 1.878387 | 290 | 1.884503 | PASS | 0.000000 | 0.000000 | 0 | 0.006117 | 0.006117 | smpte170m -> bt709 |

What they show:

- All four pairs pass D-019 with plain FFmpeg. Written with the 6 decimals of FFmpeg's log, Mirage's score of every one of the 300 frames of every pair is FFmpeg's (`checks.txt`, check 1). Mirage's `dis_conversion` and FFmpeg's scale filter both convert gear2 and gear1 from smpte170m/tv to bt709/tv and change nothing for gear4 and gear3 (check 4, and the last two columns of `plain/delta.md`), with no warning.
- For gear4 and gear3, Mirage's scores are those of the run before D-016 at full precision on all 300 frames (check 2): going through `sws_scale_frame()` changed nothing where the tags already match. For gear2 no frame keeps its score; for gear1, 10 frames do.
- FFmpeg's side has every libvmaf metric of every frame equal to that of the run before D-016 (check 3), so the change is Mirage's alone. Mirage's pooled means of gear2 and gear1 are now FFmpeg's, 28.552935 and 0.006117, where they were 33.832772 and 1.884503.

Limits:

- Only the first 300 frames. The committed §5 report covers every frame of the four rungs (D-018): MEASUREMENT PENDING.
- Only the bilinear scaler on both sides (D-003), and the inputs as fetched during the run.
- These scores only compare the two tools (D-005, D-011).

### `scalers-300/`: the three scalers on both sides, first 300 frames (D-003, P-016)

D-003 has bilinear and bicubic measured on both sides before the owner picks the default scaler. These are the plain runs of `diag-after-300/` once per scaler, `--scaler` going to both sides (FFmpeg's `flags=`, `mirage_vmaf --scaler`), with the `mirage_vmaf` that takes `--scaler` and `--model`, built at `8cc06a7c6939660e53097b948f6a78c068e8faee` from the clean main working tree (`build.txt`):

```
eval/consistency/run_delta.sh build/macos-release/mirage_vmaf <out>/bilinear --frames 300 --scaler bilinear
eval/consistency/run_delta.sh build/macos-release/mirage_vmaf <out>/bicubic --frames 300 --scaler bicubic
eval/consistency/run_delta.sh build/macos-release/mirage_vmaf <out>/lanczos --frames 300 --scaler lanczos
python3 -I -B eval/consistency/compare_runs.py --base bilinear=eval/consistency/scalers-300/bilinear --run bicubic=eval/consistency/scalers-300/bicubic --run lanczos=eval/consistency/scalers-300/lanczos --out eval/consistency/scalers-300/scaler_effect
```

| File | Contents |
|---|---|
| `build.txt` | the build of `mirage_vmaf` right before the runs: HEAD, a clean working tree, the build's output, a second build with nothing to do, and its SHA-256, which every run's `environment.txt` repeats |
| `bilinear/`, `bicubic/`, `lanczos/` | the three runs |
| `scaler_effect.md`, `scaler_effect.json` | `compare_runs.py`: per pair, how far each side's per-frame scores move from the bilinear run's |
| `checks.txt` | checks made on the runs afterwards, each command with its output |
| `SHA256SUMS` | SHA-256 of `build.txt`, `checks.txt`, `scaler_effect.*` and the runs' own `SHA256SUMS` |

From `scaler_effect.md`, Mirage's side (shift = the scaler's score minus bilinear's, per frame; 6 decimals):

| pair | pooled mean, bilinear | pooled mean, bicubic | pooled mean, lanczos | mean shift, bicubic | mean shift, lanczos | max \|shift\|, bicubic (index) | max \|shift\|, lanczos (index) |
|---|---|---|---|---|---|---|---|
| gear4_vs_gear5 | 74.640607 | 84.258060 | 83.843581 | 9.617453 | 9.202973 | 10.070505 (240) | 10.105211 (240) |
| gear3_vs_gear5 | 53.729092 | 64.843002 | 67.063929 | 11.113910 | 13.334836 | 12.066728 (30) | 14.121453 (30) |
| gear2_vs_gear5 | 28.552935 | 36.817663 | 39.024426 | 8.264728 | 10.471491 | 8.804682 (101) | 11.215920 (101) |
| gear1_vs_gear5 | 0.006117 | 6.781918 | 8.881025 | 6.775801 | 8.874908 | 9.142560 (41) | 11.607837 (41) |

What they show:

- **Mirage matches FFmpeg with every scaler.** All three runs pass on all four pairs, and every frame of every pair agrees at the 6 decimals of FFmpeg's log (`checks.txt`, check 3). FFmpeg's scale filter ran with the flags of the scaler asked for, those of swscale's `SWS_BILINEAR`, `SWS_BICUBIC` and `SWS_LANCZOS` (check 5).
- **The default is unchanged.** The bilinear run has, on both sides, the scores of `diag-after-300/` at full precision on every frame (check 4).
- **The scaler moves the scores a lot.** With bicubic and with lanczos no frame of any pair scores lower than with bilinear: every frame of gear4, gear3 and gear2 scores higher, and so does every frame of gear1 but 10 with bicubic and 9 with lanczos, which keep their score (`scaler_effect.md`, frames up / same / down). On average the scores rise by 6.775801 to 11.113910 points with bicubic and by 8.874908 to 13.334836 with lanczos, depending on the pair. FFmpeg's scores move the same way (the FFmpeg table of `scaler_effect.md`), so the scaler is no source of disagreement between the two tools, but it changes the scores themselves.

Limits: the first 300 frames only; bipbop's rungs were rendered separately (D-011), so how much a scaler changes the scores of a ladder encoded from one source is not measured here (MEASUREMENT PENDING, after D-011's clips); the scores only compare settings and tools (D-005).

### `models-300/`: the model option, gear2, first 300 frames (§1.1, P-016)

Work order §1.1's acceptance: a change of model gives observably different scores, and the output carries the model identifier. One pair, gear2 against gear5 (`pairs/bipbop16x9_gear2.txt`), bilinear, with each built-in model the work order names and with a model file, the libvmaf package's `vmaf_v0.6.1.json`; the same `mirage_vmaf` as `scalers-300/` (`build.txt`):

```
eval/consistency/run_delta.sh build/macos-release/mirage_vmaf <out>/vmaf_v0.6.1 --frames 300 --pairs eval/consistency/pairs/bipbop16x9_gear2.txt --model vmaf_v0.6.1
eval/consistency/run_delta.sh build/macos-release/mirage_vmaf <out>/vmaf_v0.6.1neg --frames 300 --pairs eval/consistency/pairs/bipbop16x9_gear2.txt --model vmaf_v0.6.1neg
eval/consistency/run_delta.sh build/macos-release/mirage_vmaf <out>/vmaf_4k_v0.6.1 --frames 300 --pairs eval/consistency/pairs/bipbop16x9_gear2.txt --model vmaf_4k_v0.6.1
eval/consistency/run_delta.sh build/macos-release/mirage_vmaf <out>/file_vmaf_v0.6.1 --frames 300 --pairs eval/consistency/pairs/bipbop16x9_gear2.txt --model-file /opt/homebrew/opt/libvmaf/share/libvmaf/model/vmaf_v0.6.1.json
python3 -I -B eval/consistency/compare_runs.py --base vmaf_v0.6.1=eval/consistency/models-300/vmaf_v0.6.1 --run vmaf_v0.6.1neg=eval/consistency/models-300/vmaf_v0.6.1neg --run vmaf_4k_v0.6.1=eval/consistency/models-300/vmaf_4k_v0.6.1 --run file_vmaf_v0.6.1=eval/consistency/models-300/file_vmaf_v0.6.1 --out eval/consistency/models-300/model_effect
```

The files are laid out as in `scalers-300/`, with `model_effect.*` in place of `scaler_effect.*`. From `model_effect.md`, Mirage's side:

| run | pooled mean | mean shift from vmaf_v0.6.1 | max \|shift\| (index) |
|---|---|---|---|
| vmaf_v0.6.1 | 28.552935 | | |
| vmaf_v0.6.1neg | 19.498402 | -9.054533 | 10.384560 (31) |
| vmaf_4k_v0.6.1 | 47.442375 | 18.889440 | 22.251285 (0) |
| file_vmaf_v0.6.1 | 28.552935 | 0.000000 | 0.000000 (0) |

What they show:

- **Observably different scores.** With vmaf_v0.6.1neg every one of the 300 frames scores lower than with vmaf_v0.6.1, with vmaf_4k_v0.6.1 every frame scores higher (`model_effect.md`, frames up / same / down), on both sides alike.
- **The model identifier.** Each `mirage_vmaf` report names its model: `model`, `model_source` (`built-in` or `file`), and for the file its SHA-256, the value `shasum -a 256` computes (`checks.txt`, check 4). FFmpeg's log for vmaf_v0.6.1neg has that model's features (names ending in `_egl_1`, check 5).
- **Mirage matches FFmpeg with every model**: every frame of every run agrees at FFmpeg's 6 decimals (check 3).
- **A model file works like the built-in model**: the run with the package's `vmaf_v0.6.1.json` keeps every score of the built-in vmaf_v0.6.1 on both sides, at full precision on Mirage's side and in every metric of FFmpeg's log (check 6), and the vmaf_v0.6.1 run has those of `scalers-300/bilinear` for gear2 (check 6).

Limits: one pair, the first 300 frames; which model suits which content is not evaluated here.
