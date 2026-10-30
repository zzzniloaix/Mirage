# eval/

The evaluation's scripts and their committed outputs. The numbers in this repository's documents come from committed outputs, most of them here, and each document names the file (hard rule 2 of the work order).

| Path | What it is |
|---|---|
| `baseline/` | Phase 0: the player's existing scorer on bipbop 16x9, the environment, and the first diagnostic comparison with FFmpeg (`baseline/README.md`) |
| `build_check/` | the build of `docs/BUILD.md`, checked in clean worktrees, and its records (`build_check/README.md`) |
| `consistency/` | the §5 delta tool, Mirage against FFmpeg's libvmaf filter; the ladder check of `mirage_vmaf --ladder`; and their committed runs (`consistency/README.md`) |
| `pooling/` | the pooling definitions measured on committed per-frame scores (`pooling/README.md`) |
| `streams.md`, `streams/` | the public test streams, and the script that fetches their playlists |
| `run_all.sh`, `run_all.py`, `tests/` | one command that reruns every committed Phase 1 measurement and compares its outputs with the committed ones |
| `run_all/` | the records of `run_all.sh` runs |
| `test_runs/` | records of the unit test suites run at a commit, which the PR descriptions quote (`test_runs/README.md`) |

## Rerunning every measurement: run_all.sh

```
eval/run_all.sh <out_dir> [--mirage-vmaf PROG] [--mirage-vmaf-before PROG]
    [--mirage-vmaf-before-d022 PROG] [--mirage-vmaf-before-d028 PROG]
    [--skip-before] [--skip-full] [--only STEP[,STEP...]]
    [--jobs J] [--ffmpeg PROG] [--ffprobe PROG] [--ffmpeg-libvmaf-package VERSION]
    [--model-dir DIR] [--work-dir DIR] [--no-compare]
```

- `<out_dir>`: a new or empty directory outside the repository. The runs inside record the repository's commit and whether its working tree was clean, so run it from a clean tree when its outputs are to be cited.
- It needs what `docs/BUILD.md` installs, and the network: the streams come from Apple's CDN, and the builds fetch their dependencies from GitHub. Building HEAD runs no Python, since glad's loader is committed (D-031). The commits before it configure glad, which looks for a Python interpreter, but their `mirage_vmaf` target doesn't need glad's loader, so no glad venv is needed.
- Without `--mirage-vmaf`, `--mirage-vmaf-before`, `--mirage-vmaf-before-d022` and `--mirage-vmaf-before-d028`, it builds the programs the steps need itself, each in a temporary worktree of its commit (`cmake --preset macos-release`, then only the `mirage_vmaf` target), and keeps them in `<out_dir>/builds/`.
- With the steps over every frame it runs for hours and reads Apple's CDN all the while, so the machine has to stay awake and online: `caffeinate -i` keeps a Mac from sleeping when idle, not when its lid is closed (`run_all/2026-10-24-full/power_log.txt`), and with FFmpeg 8.1.1's defaults a read in `ffmpeg` on a connection that stays open without data has no time limit, so a step can wait instead of failing (read from FFmpeg's sources: "A network read without a time limit" in `docs/PHASE1_DECISIONS_PENDING.md`). Since D-036 `mirage_vmaf`'s reads fail after 5 s instead.
- **On Linux**, which nothing has been run on yet (MEASUREMENT PENDING): give it the program of the Linux build, `--mirage-vmaf build/linux-release/mirage_vmaf`, and FFmpeg and libvmaf's model directory with `--ffmpeg` and `--model-dir`, whose defaults are Homebrew's paths (`docs/BUILD.md`, step 6), and, where FFmpeg's libvmaf names no package version in its path, that version with `--ffmpeg-libvmaf-package` (D-038). It builds the other programs from their commits with their `macos-release` preset as on macOS; those commits' `CMakeLists.txt` configure the player too, so they need the player's configure-time dependencies. The Docker image of `docker/README.md` has them and runs `run_all.sh` this way as its command; it has not been built.

The steps, in order; each writes under `<out_dir>` the path its committed counterpart has under `eval/` (P-024):

| step | reruns | `mirage_vmaf` |
|---|---|---|
| `streams` | `streams/fetch_streams.py --probe`, against `streams/fetched-2026-10-23/` | |
| `clips` | `consistency/color_cases.py`, against `consistency/color-cases-before/clips.txt` | |
| `format-clips` | `consistency/color_cases.py --set formats`, against `consistency/format-cases-before/clips.txt` | |
| `align-clips` | `consistency/align_cases.py`, against `consistency/align-cases-before/clips.txt` | |
| `negotiation-clips` | `consistency/color_cases.py --set negotiation`, against `consistency/negotiation-cases/clips.txt` | |
| `diag-before-300` | `consistency/diag-before-300/`: plain and relabel | `83c8ed6`'s |
| `color-cases-before` | `consistency/color-cases-before/`: the scored and the refused cases | `83c8ed6`'s |
| `format-cases-before` | `consistency/format-cases-before/`: the scored and the refused cases | `83c8ed6`'s |
| `diag-after-300` | `consistency/diag-after-300/` | HEAD's |
| `color-cases-after` | `consistency/color-cases-after/` | HEAD's |
| `format-cases-after` | `consistency/format-cases-after/` | `d993686`'s |
| `format-cases-d022` | `consistency/format-cases-d022/` | HEAD's |
| `negotiation-cases` | `consistency/negotiation-cases/`: where FFmpeg's negotiation changes the distorted frames' color (D-035) | HEAD's |
| `scalers-300` | `consistency/scalers-300/`: three runs and `scaler_effect.*` | HEAD's |
| `models-300` | `consistency/models-300/`: four runs and `model_effect.*` | HEAD's |
| `defaults-300` | `consistency/defaults-300/`: the first 300 frames of the four pairs with the defaults of D-024 and D-027 | HEAD's |
| `ladder-300` | `consistency/ladder-300/`: `consistency/ladder_check.py`, `mirage_vmaf --ladder` on the bipbop master against the four pairs scored one by one, first 300 frames (D-029) | HEAD's |
| `align-cases-before` | `consistency/align-cases-before/`: the alignment cases | `e8b867f`'s |
| `align-cases-d028` | `consistency/align-cases-d028/`: the alignment cases | HEAD's |
| `acceptance-full` | `consistency/acceptance-full/`: every frame of the four pairs of D-018, the §5 report with the defaults of its day (bilinear) | HEAD's |
| `defaults-full` | `consistency/defaults-full/`: every frame of the four pairs of D-018, the §5 report with the defaults of D-022, D-024, D-025, D-027 and D-028 | HEAD's |
| `ffmpeg-scale-default` | the commands of `consistency/ffmpeg-scale-default/checks.txt` | |
| `ffmpeg-libvmaf-filter` | the commands of `consistency/ffmpeg-libvmaf-filter/checks.txt` | |
| `pooling` | `pooling/bipbop16x9/`, on the rerun of `diag-after-300` | |
| `pooling-defaults-300` | `pooling/defaults-300/`, `--check-pooled` on the rerun of `defaults-300` | |
| `pooling-full` | `pooling/acceptance-full/`, on the rerun of `acceptance-full` | |
| `pooling-defaults-full` | `pooling/defaults-full/`, `--check-pooled` on the rerun of `defaults-full` | |

- Every `run_delta.sh` run gets the options of its committed `run.cmd.txt`, the scaler, the model and the threads always named, which a test checks, and so does the ladder check, with its scaler and model: since D-024 `run_delta.sh`'s default scaler is bicubic, and the runs made before it with bilinear, the default then, are rerun with bilinear (P-038). The runs made before D-016 need the program that didn't convert color yet, the build of `83c8ed6`; `format-cases-after`, made after D-016 and before D-022, the build of `d993686`, which doesn't convert a reference that isn't yuv420p (P-034); `align-cases-before`, the build of `e8b867f`, which pairs the inputs' frames in decode order, before D-028 (P-049); the others use HEAD's. Whichever program a run checks, HEAD's delta tool compares it with FFmpeg, by timestamp since D-028 (P-047): the runs made before D-028 are rerun with that comparison too (P-049).
- `--only` runs some steps, and the steps they need (`pooling` needs `diag-after-300`, `pooling-defaults-300` needs `defaults-300`, `pooling-full` needs `acceptance-full`, `pooling-defaults-full` needs `defaults-full`, the color tag cases need `clips`, the three runs of the reference format cases `format-clips` and the two of the alignment cases `align-clips`; `ladder-300` needs none); `--skip-before` leaves out the three steps before D-016, and `--skip-full` the four over every frame, `acceptance-full`, `defaults-full`, `pooling-full` and `pooling-defaults-full` (P-027, P-059). Each side of each pair of `acceptance-full` took longer than the whole run of 2026-10-23 below (the wall times in `consistency/acceptance-full/plain/*/delta.md`).
- `--jobs` is `run_delta.sh`'s and the ladder check's: how many scoring processes run at once. The committed runs used 1, except `acceptance-full` and `defaults-full`, which used 4; their first 300 frames have the scores of `scalers-300/bilinear` and `defaults-300/`, made one process at a time (`consistency/acceptance-full/checks.txt`, check 3; `consistency/defaults-full/checks.txt`, check 8).

**What "as committed" means.** For each step, `rerun_check.md` says whether the rerun has:

- for every run and pair, the committed verdict, Mirage's per-frame scores at full precision and every libvmaf metric of every frame of FFmpeg's log;
- for the ladder check, its verdict, the ladder report's master and plan (reference mode, entries, reference, skipped entries, warnings), and every per-frame score of the ladder's pairs and of the pairs scored one by one, at full precision;
- the numbers of `compare_runs.py`'s summary (its `settings` aside, which name a model file by its path) and of `pooling.py`'s series;
- the committed bytes of every playlist and the committed parameters of every probed video stream;
- the committed `clips.txt`: each clip's command, tags and pixel checksum, or, for the alignment cases, each clip's commands, streams, frame timestamps and frame checksums;
- the committed output of each command of the FFmpeg records (heap addresses in FFmpeg's error lines aside).

It also gives each program's SHA-256 next to the one in the committed runs' `build.txt`.

**What it writes.** `run_all.md` and `run_all.json` (every command, its exit status and wall time, and the repository's state at the start and end), `rerun_check.md` and `rerun_check.json`, `builds/<name>/build.txt` with the build logs, `logs/` with the output of every command, and the rerun outputs themselves. Local paths in its own files appear as `<out_dir>`, `<work>`, `<worktree>`, `<repo>`, `<home>` and `<tmp>`.

**Exit status.** 0: every step ran and everything compared is as committed. 1: a step failed or something differs; `rerun_check.md` says what. 2: bad arguments, or the run could not start.

**Not part of it.** The `checks.txt` files of the committed run directories, which compare committed outputs with each other, and the build checks of `build_check/`, which need their own empty directory, build everything, and regenerate glad's loader with Python 3.13.

## Tests

```
python3 -I -B -m unittest discover -s eval/tests -v
python3 -I -B -m unittest discover -s eval/consistency/tests -v
python3 -I -B -m unittest discover -s eval/pooling/tests -v
python3 -I -B -m unittest discover -s eval/streams/tests -v
```

None of them needs the network. `tests/test_run_all.py` checks every step against the committed `run.cmd.txt` and `checks.txt` files, fails when a committed run has no step, and runs the whole script with the stand-ins of `consistency/tests/fake_tools.py`.

## The run of 2026-10-23

`run_all/2026-10-23/` keeps the record of one run, made with no program given, from a clean tree at `e2169f5` (P-025), before the full-length run was committed, so without `acceptance-full` and `pooling-full`: `run_all.md` and `run_all.json`, `rerun_check.md` and `rerun_check.json`, the builds' records and logs, and the output of every command. The rerun outputs themselves aren't kept; where they match the committed ones, which `rerun_check.md` says they all do, they would duplicate them.

- Every step ran, and everything compared is as committed (`rerun_check.md`).
- Both programs it built have the SHA-256 of the program the committed runs name in their `build.txt`: the build of `83c8ed6` that of `diag-before-300` and `color-cases-before`, and the build of HEAD that of `scalers-300` and `models-300`. `diag-after-300` and `color-cases-after` were made with the build of `5263061`, before `--scaler` and `--model` existed; HEAD's build reproduces their scores exactly.
- It took 544.3 s of wall time, both builds included (`run_all.md`).

## The rerun of the full-length steps, 2026-10-23

`run_all/2026-10-23-full/` keeps the record of `eval/run_all.sh <out_dir> --only pooling-full --jobs 4`, made from a clean worktree of `e0beb49` with no program given, the same files as above (P-027). It ran the two steps over every frame, `acceptance-full` and `pooling-full`.

- Everything compared is as committed (`rerun_check.md`): every pair's verdict, all 53946 per-frame scores of every pair on Mirage's side at full precision, every libvmaf metric of FFmpeg's logs, and every pooling series.
- The program it built from `e0beb49` has the SHA-256 that `consistency/acceptance-full/build.txt` records, `3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c`.
- It took 2707.1 s of wall time, the build included, of which 2614.8 s for `acceptance-full` (`run_all.md`).

## The rerun after the review fixes, 2026-10-23

`run_all/2026-10-23-fix/` keeps the record of `eval/run_all.sh <out_dir> --jobs 4`, made from a new clean worktree of `9855319` with no program given, the same files as above (P-032): every step, the reference format cases and the full-length ones included, with the `mirage_vmaf` that has P-030's warning.

- Everything compared is as committed (`rerun_check.md`), in every step.
- The program it built from `9855319` has the SHA-256 of `consistency/format-cases-after/build.txt`, `b225a60efd146d27fbb59335176324fa9529b1a86921f8814a1b64e34d162803`. It reproduces exactly the scores of the other runs made after D-016, which programs of other SHA-256 made: `420b8776…`, built at `5263061` (`diag-after-300`, `color-cases-after`), and `3633c43e…` (`scalers-300`, `models-300`, `acceptance-full`). The program it built from `83c8ed6` has the SHA-256 of `diag-before-300`, `color-cases-before` and `format-cases-before`.
- It took 2968.8 s of wall time, both builds included, of which 2557.4 s for `acceptance-full` (`run_all.md`).

## The rerun after D-021, D-022 and D-032, 2026-10-23

`run_all/2026-10-23-d022/` keeps the record of `eval/run_all.sh <out_dir> --skip-full --jobs 4`, made from a new clean worktree of `5135d58` with no program given, the same files as above (P-037): every step but the two over every frame, with the three programs built from their commits.

- Everything compared is as committed (`rerun_check.md`), in every step.
- The program it built from `5135d58`, which converts a reference that isn't yuv420p as FFmpeg does (D-022), has the SHA-256 of `consistency/format-cases-d022/build.txt`, `2220ce3a0997d53f750b34a770110f5c65f5f870cfb9a0e6347256733bcf09ea`. It reproduces exactly the scores of `format-cases-d022` and of the other runs it reran that were made after D-016, but `format-cases-after`, whose programs had other SHA-256s: `420b8776…` (`diag-after-300`, `color-cases-after`) and `3633c43e…` (`scalers-300`, `models-300`). The program built from `d993686` has the SHA-256 of `consistency/format-cases-after/build.txt`, and the one built from `83c8ed6` that of the runs before D-016.
- It took 521.0 s of wall time, the three builds included (`run_all.md`).

## The rerun after D-024, D-027 and D-030, 2026-10-23

`run_all/2026-10-23-d027/` keeps the record of `eval/run_all.sh <out_dir> --skip-full --jobs 4`, made from a new clean worktree of `0496b21` with no program given, the same files as above (P-042): every step but the two over every frame, `defaults-300` and `pooling-defaults-300` included, with the three programs built from their commits.

- Everything compared is as committed (`rerun_check.md`), in every step. Its `pooling` step shows that HEAD's `mirage_vmaf`, which pools the per-frame scores itself since D-027, reports the pooled mean and min that the program of `diag-after-300` took from libvmaf's `vmaf_score_pooled()`: `pooling.py` records both, and they compare as committed.
- The program it built from `0496b21` has the SHA-256 of `consistency/defaults-300/build.txt`, `85535c5c0ea56865816a71ea9aef837753e0a995d4892be51b863943f6854c5f`. The one built from `d993686` has the SHA-256 of `consistency/format-cases-after/build.txt`, and the one built from `83c8ed6` that of the runs before D-016.
- It took 529.5 s of wall time, the three builds included (`run_all.md`).

## The rerun after D-028's pairing by timestamp, 2026-10-23

`run_all/2026-10-23-d028/` keeps the record of `eval/run_all.sh <out_dir> --skip-full --jobs 4`, made from a new clean worktree of `713431b` with no program given, the same files as above (P-046): every step but the two over every frame, with the three programs built from their commits.

- Everything compared is as committed (`rerun_check.md`), in every step: HEAD's `mirage_vmaf`, which pairs frames by timestamp since D-028, gives every run it reran the committed verdicts and per-frame scores.
- The program it built from `713431b` has the SHA-256 `7fb455e9e1349c85e25afe54d5fd47d714c463ecebea1b03b84ca1b594d27741`, which no committed run's `build.txt` names. It reproduces exactly the scores of the runs made by the builds `420b8776…` (`diag-after-300`, `color-cases-after`), `2220ce3a…` (`format-cases-d022`), `3633c43e…` (`scalers-300`, `models-300`) and `85535c5c…` (`defaults-300`). The one built from `d993686` has the SHA-256 of `consistency/format-cases-after/build.txt`, and the one built from `83c8ed6` that of the runs before D-016.
- It took 520.5 s of wall time, the three builds included (`run_all.md`).

## The rerun after the delta tool's comparison by timestamp, 2026-10-23

`run_all/2026-10-23-align/` keeps the record of `eval/run_all.sh <out_dir> --skip-full --jobs 4`, made from a new clean worktree of `4eeb539` with no program given, the same files as above (P-049): every step but the two over every frame, the alignment cases' three included, with the four programs built from their commits and every `run_delta.sh` run compared by the delta tool's comparison by timestamp (D-028, P-047).

- Everything compared is as committed (`rerun_check.md`), in every step: the runs made before the comparison by timestamp keep their verdicts and scores with it.
- The program it built from `4eeb539` has the SHA-256 of `consistency/align-cases-d028/build.txt`, `7fb455e9e1349c85e25afe54d5fd47d714c463ecebea1b03b84ca1b594d27741`, which reproduces the scores of the other runs made after D-016 but `format-cases-after`, as in the rerun of `2026-10-23-d028/`. The one built from `e8b867f` has the SHA-256 of `consistency/align-cases-before/build.txt` and `consistency/defaults-300/build.txt`, `85535c5c…`; the one built from `d993686` that of `consistency/format-cases-after/build.txt`, and the one built from `83c8ed6` that of the runs before D-016.
- It took 620.4 s of wall time, the four builds included (`run_all.md`).

## The rerun after D-029's ladder scoring, 2026-10-23

`run_all/2026-10-23-ladder/` keeps the record of `eval/run_all.sh <out_dir> --skip-full --jobs 4`, made from a new clean worktree of `d340b32` with no program given, the same files as above (P-052): every step but the two over every frame, `ladder-300` included, with the four programs built from their commits.

- Everything compared is as committed (`rerun_check.md`), in every step: HEAD's `mirage_vmaf`, which has `--ladder` and `--mezzanine` since D-029, gives every run it reran the committed verdicts and per-frame scores, and the ladder check its verdict, the ladder report's master and plan, and every per-frame score of the ladder's pairs and of the pairs scored one by one.
- The program it built from `d340b32` has the SHA-256 of `consistency/ladder-300/build.txt`, `eab1651cc09237fce5451fd1de0c236c2d589b2d3b2d2848065b675e8e35a4c7`. It reproduces exactly the scores of the other runs it reran that were made after D-016, by the builds `420b8776…` (`diag-after-300`, `color-cases-after`), `2220ce3a…` (`format-cases-d022`), `3633c43e…` (`scalers-300`, `models-300`), `85535c5c…` (`defaults-300`) and `7fb455e9…` (`align-cases-d028`). The ones built from `e8b867f`, `d993686` and `83c8ed6` have the SHA-256s of the runs they reran, as in the rerun of `2026-10-23-align/`.
- It took 670.0 s of wall time, the four builds included (`run_all.md`).

## The rerun of every step, 2026-10-24, which the machine's sleep cut short

`run_all/2026-10-24-full/` keeps the record of `eval/run_all.sh <out_dir> --jobs 4`, made from a new clean worktree of `61241e2` with no program given (P-060, P-062): every step, the four over every frame included, with the four programs built from their commits. Besides the same files as above it keeps the reports of the failed `defaults-full` run, the stderr logs of its two failed pairs and the machine's sleep and wake events, with a `README.md` that says what happened.

- 23 of the 25 steps found everything as committed (`rerun_check.md`), `acceptance-full` and `pooling-full` among them: the head's program and the delta tool's comparison by timestamp give every pair of the full-length report with bilinear its committed verdict and every score of both sides.
- `defaults-full` did not, nor `pooling-defaults-full`, which reads it. Its pairs `gear4_vs_gear5` and `gear3_vs_gear5` are as committed. While the other two were being scored, the machine went to sleep with its lid closed, their inputs lost Apple's CDN, and the `mirage_vmaf` of `gear2_vs_gear5`, which logged no error, was stopped before the delta tool's 6 h limit, exit status -15 (`README.md`; `logs/defaults-full_plain.log`; P-062; "A network read without a time limit" in `docs/PHASE1_DECISIONS_PENDING.md`). The two steps were rerun on their own (below).
- The program it built from `61241e2` has the SHA-256 of `consistency/defaults-full/build.txt`, `9f04085f73440a9c9969f9bf6a86767eaf9ade89ae8ca393f8b6c99bb78b9b06`. It reproduces exactly the scores of the other runs it reran that were made after D-016, by the builds `420b8776…` (`diag-after-300`, `color-cases-after`), `2220ce3a…` (`format-cases-d022`), `3633c43e…` (`scalers-300`, `models-300`, `acceptance-full`), `85535c5c…` (`defaults-300`), `7fb455e9…` (`align-cases-d028`) and `eab1651c…` (`ladder-300`). The ones built from `e8b867f`, `d993686` and `83c8ed6` have the SHA-256s of the runs they reran.
- `run_all.md` gives 10127.3 s of wall time, the four builds included; it counts only the time the machine was awake (`README.md`).

## The rerun of defaults-full and its pooling, 2026-10-24

`run_all/2026-10-24-defaults-full/` keeps the record of `eval/run_all.sh <out_dir> --only defaults-full,pooling-defaults-full --jobs 4`, made from a new clean worktree of `5908e3b` with no program given, the same files as `2026-10-23-full/` (P-062): the two steps that the machine's sleep cut short in the rerun above.

- Everything compared is as committed (`rerun_check.md`): every pair's verdict, all 53946 per-frame scores of every pair on Mirage's side at full precision, every libvmaf metric of FFmpeg's logs, and every pooling series.
- The program it built from `5908e3b` has the SHA-256 that `consistency/defaults-full/build.txt` records, `9f04085f73440a9c9969f9bf6a86767eaf9ade89ae8ca393f8b6c99bb78b9b06`.
- It took 3146.5 s of wall time, the build included, of which 3055.2 s for `defaults-full` (`run_all.md`).

## The rerun of every step after the review fixes, 2026-10-26, which the machine's sleep cut short

`run_all/2026-10-26-fix/` keeps the record of `eval/run_all.sh <out_dir> --jobs 4`, made from a new clean worktree of `a4692a2`, the last commit of the review fixes that changes code or scripts, with no program given (P-065): every step, the four over every frame included, with the four programs built from their commits. Like `2026-10-24-full/` it also keeps the reports and logs of the failed `defaults-full` run and the machine's sleep and wake events, and besides them the CPU times of the stopped processes, with a `README.md` that says what happened.

- 23 of the 25 steps found everything as committed (`rerun_check.md`), `acceptance-full` and `pooling-full` among them: in them the analyzer's error for a reference whose frames change size (P-063) and the delta tool's line of given options (P-064) changed no committed verdict or score.
- `defaults-full` did not, nor `pooling-defaults-full`, which reads it. Its pairs `gear4_vs_gear5` and `gear3_vs_gear5` are as committed. While the other two were being scored, the machine went to sleep with its lid closed; after it woke, their four scoring processes, `mirage_vmaf` and `ffmpeg` alike, made no progress and logged no error until they were stopped with SIGTERM (`README.md`, `stall_check.txt`; "A network read without a time limit" in `docs/PHASE1_DECISIONS_PENDING.md`). The two steps were rerun on their own (below).
- The program it built from `a4692a2` has the SHA-256 `217cc2948bf361aa11a37f4aea467ffe1caa30ab251943ba18b72c55005c6f10`, which no earlier run's `build.txt` names. It reproduces exactly the scores of the other runs it reran that were made after D-016, by the builds `420b8776…` (`diag-after-300`, `color-cases-after`), `2220ce3a…` (`format-cases-d022`), `3633c43e…` (`scalers-300`, `models-300`, `acceptance-full`), `85535c5c…` (`defaults-300`), `7fb455e9…` (`align-cases-d028`) and `eab1651c…` (`ladder-300`). The ones built from `e8b867f`, `d993686` and `83c8ed6` have the SHA-256s of the runs they reran.
- `run_all.md` gives 6912.5 s of wall time, the four builds included; it counts only the time the machine was awake (`README.md`).

## The rerun of defaults-full and its pooling, 2026-10-26

`run_all/2026-10-26-defaults-full/` keeps the record of `eval/run_all.sh <out_dir> --only defaults-full,pooling-defaults-full --jobs 4`, made from a new clean worktree of `1f7d42d`, the commit of the record above, with no program given, the same files as `2026-10-23-full/` (P-065): the two steps that the machine's sleep cut short in the rerun above.

- Everything compared is as committed (`rerun_check.md`): every pair's verdict, all 53946 per-frame scores of every pair on Mirage's side at full precision, every libvmaf metric of FFmpeg's logs, and every pooling series.
- The program it built from `1f7d42d` has the SHA-256 of the one the rerun above built from `a4692a2`, `217cc2948bf361aa11a37f4aea467ffe1caa30ab251943ba18b72c55005c6f10`; `consistency/defaults-full/build.txt` names another build, `9f04085f…`, whose scores it reproduces exactly.
- It took 2719.8 s of wall time, the build included, of which 2666.9 s for `defaults-full` (`run_all.md`).

## The rerun of every step after D-035 to D-038, 2026-10-26, which a stalled process and the machine's sleep cut short

`run_all/2026-10-26-d035/` keeps the record of `eval/run_all.sh <out_dir> --jobs 4`, made from a new clean worktree of `bbc669d`, the last commit after D-035 to D-038 that changes code or scripts, with no program given (P-066): every step, the four over every frame and the two of the negotiation cases included, with the four programs built from their commits. Like `2026-10-26-fix/` it also keeps the reports and logs of the failed `defaults-full` run, the machine's sleep and wake events and the CPU times of the stopped process, with a `README.md` that says what happened.

- 25 of the 27 steps found everything as committed (`rerun_check.md`), `acceptance-full`, `pooling-full`, `negotiation-clips` and `negotiation-cases` among them: in them D-036's limits on network reads, D-037's new pooled value and D-038's package version changed no committed verdict or score.
- `defaults-full` did not, nor `pooling-defaults-full`, which reads it. Its pair `gear4_vs_gear5` is as committed. The `ffmpeg` of `gear2_vs_gear5` last wrote its log 3 min 54 s before the machine went to sleep with its lid closed, and made no progress after the machine woke until it was stopped with SIGTERM (`README.md`, `power_log.txt`, `stall_check.txt`). The stderr logs of `mirage_vmaf` in the three other pairs hold network reads that timed out, at D-036's 5 s limit, and its reports scored 52442, 4425 and 15639 of the 53946 frame pairs, with D-028's warning that more frames than the threshold failed to pair (`README.md`). The two steps were rerun on their own (below).
- The program it built from `bbc669d` has the SHA-256 `aaac3630522dc4261f6fa27bb8e81dc66a64ba9860292ecdeae5e9164fb372d3`, that of the negotiation cases' run (`consistency/negotiation-cases/build.txt`). It reproduces exactly the scores of the other runs it reran that were made after D-016, by the builds `420b8776…` (`diag-after-300`, `color-cases-after`), `2220ce3a…` (`format-cases-d022`), `3633c43e…` (`scalers-300`, `models-300`, `acceptance-full`), `85535c5c…` (`defaults-300`), `7fb455e9…` (`align-cases-d028`) and `eab1651c…` (`ladder-300`). The ones built from `e8b867f`, `d993686` and `83c8ed6` have the SHA-256s of the runs they reran.
- `run_all.md` gives 6615.3 s of wall time, the four builds included; it counts only the time the machine was awake (`README.md`).

## The rerun of defaults-full and its pooling, 2026-10-27

`run_all/2026-10-27-defaults-full/` keeps the record of `eval/run_all.sh <out_dir> --only defaults-full,pooling-defaults-full --jobs 4`, made from a new clean worktree of `953d42e`, the commit of the record above, with no program given, the same files as `2026-10-23-full/` (P-066): the two steps that the rerun above didn't finish.

- Everything compared is as committed (`rerun_check.md`): every pair's verdict, all 53946 per-frame scores of every pair on Mirage's side at full precision, every libvmaf metric of FFmpeg's logs, and every pooling series.
- The program it built from `953d42e` has the SHA-256 of the one the rerun above built from `bbc669d`, `aaac3630…`; `consistency/defaults-full/build.txt` names another build, `9f04085f…`, whose scores it reproduces exactly.
- It took 3172.9 s of wall time, the build included, of which 3089.7 s for `defaults-full` (`run_all.md`).
