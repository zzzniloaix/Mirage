# Section 5 consistency check

`run_delta.sh` compares Mirage's per-frame VMAF scores with those of FFmpeg's libvmaf filter, over the same frames of the same inputs, and writes a delta report per pair and for the whole run (work order §5; `docs/DECISIONS.md` D-017 to D-019). Mirage's side is `mirage_vmaf` (D-017). FFmpeg's side is the `ffmpeg` program of ffmpeg-full (D-009), used here only as the reference to check against.

## Running it

```
eval/consistency/run_delta.sh <mirage_vmaf> <out_dir> [--frames N] [--jobs J] [--pairs <file>]
    [--scaler NAME] [--model NAME] [--threads N] [--threshold X] [--diagnostic-relabel]
    [--ffmpeg <ffmpeg>] [--ffprobe <ffprobe>] [--timeout S]
```

- `<mirage_vmaf>`: the program to check, such as `build/macos-release/mirage_vmaf`. For outputs that are to be committed, build it from a committed, clean tree: the run records the commit, whether the working tree was clean at its start and end, and the program's SHA-256.
- `<out_dir>`: a new or empty directory.
- Defaults: the four pairs of `pairs/bipbop16x9.txt` (each lower bipbop 16x9 rung against gear5, D-018), every frame, model `vmaf_v0.6.1`, scaler `bilinear`, 4 libvmaf threads, threshold 0.01 (D-019), `/opt/homebrew/opt/ffmpeg-full/bin/ffmpeg` with the `ffprobe` next to it, one scoring process at a time, and a limit of 6 hours per process. `--help` lists the options.
- It needs only Python 3 and its standard library; `run_delta.sh` runs it with `python3 -I -B` (`-B`: no `__pycache__` in the working tree, whose state the run records). Inputs given as URLs are fetched again on every run.
- Exit status: 0 every pair passed; 1 at least one pair failed (the reports say why); 2 bad arguments, or the run could not start; 3 an output still contains a local path or name after the placeholders, and must not be committed.
- A pairs file has one `<label> <reference> <distorted>` per line; `#` starts a comment line. The label names the pair's directory.

`--scaler`, `--model` and `--threads` set FFmpeg's side. `mirage_vmaf` is given `--scaler` and `--model` only if its `--help` lists them. The current one lists neither, so it scores with its fixed settings (bilinear, `vmaf_v0.6.1`, 4 threads), and a pair fails with `SETTINGS MISMATCH` when FFmpeg was given other settings.

## What it runs for each pair

In `<out_dir>/<pair>/`, in this order (the exact commands are in that directory's `cmd.txt`):

1. `ffprobe` on both inputs, for the reference's size (FFmpeg scales the distorted input to it) and both inputs' color tags.
2. `mirage_vmaf --ref <reference> --dis <distorted> --out mirage.json [--frames N]`.
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

The reports also give the largest and mean |delta|, the mean delta, the frames over the threshold, the frames with the largest |delta|, both sides' pooled mean and min as libvmaf computes them, and the warnings: checks that don't fail a pair, such as different libvmaf versions on the two sides, Mirage pairs whose two frames have different PTS, or tags in FFmpeg's log that differ from ffprobe's.

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
| `<pair>/ffmpeg_colorspace_lines.txt` | the lines of FFmpeg's verbose log that state color spaces: the input and output video streams, the filter graph's inputs, the scale filters' input and output, and swscale's notes |
| `<pair>/delta.md`, `delta.json` | the pair's delta report |
| `SHA256SUMS` | SHA-256 of every file; check with `shasum -a 256 -c SHA256SUMS` in the directory |

- The `.xz` files are the logs compressed with xz; `xz -dc` restores them. Empty outputs aren't kept.
- Local paths in every file are replaced by `<out_dir>`, `<repo>`, `<home>` and `<tmp>`, and the programs appear as `<mirage_vmaf>`, `<ffmpeg>` and `<ffprobe>`; `environment.txt` says which programs those were. `delta.json` counts the replacements per file under `redactions`.

## Tests

```
python3 -I -B -m unittest discover -s eval/consistency/tests -v
```

`tests/test_vmafdelta.py` covers the delta math, the commands, the parsing of FFmpeg's log and the reports. `tests/test_run_delta.py` runs the whole tool with stand-ins for `mirage_vmaf`, `ffmpeg` and `ffprobe` (`tests/fake_tools.py`), without network.

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

- Only the first 300 frames (P-012), and only the bilinear scaler on both sides; D-003's comparison of bilinear and bicubic needs a `mirage_vmaf` that takes a scaler.
- The inputs are Apple's streams as fetched during the runs; they aren't kept here.
- These scores only compare the two tools, like Phase 0's (D-005). bipbop's rungs were rendered separately (D-011), so the scores say nothing about the rungs' quality.
