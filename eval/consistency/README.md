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
