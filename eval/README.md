# eval/

The evaluation's scripts and their committed outputs. The numbers in this repository's documents come from committed outputs, most of them here, and each document names the file (hard rule 2 of the work order).

| Path | What it is |
|---|---|
| `baseline/` | Phase 0: the player's existing scorer on bipbop 16x9, the environment, and the first diagnostic comparison with FFmpeg (`baseline/README.md`) |
| `build_check/` | the build of `docs/BUILD.md`, checked in clean worktrees, and its records (`build_check/README.md`) |
| `consistency/` | the §5 delta tool, Mirage against FFmpeg's libvmaf filter, and its committed runs (`consistency/README.md`) |
| `pooling/` | the pooling definitions measured on committed per-frame scores (`pooling/README.md`) |
| `streams.md`, `streams/` | the public test streams, and the script that fetches their playlists |
| `run_all.sh`, `run_all.py`, `tests/` | one command that reruns every committed Phase 1 measurement and compares its outputs with the committed ones |
| `run_all/` | the records of `run_all.sh` runs |
| `test_runs/` | records of the unit test suites run at a commit, which the PR descriptions quote (`test_runs/README.md`) |

## Rerunning every measurement: run_all.sh

```
eval/run_all.sh <out_dir> [--mirage-vmaf PROG] [--mirage-vmaf-before PROG]
    [--mirage-vmaf-before-d022 PROG] [--skip-before] [--skip-full] [--only STEP[,STEP...]]
    [--jobs J] [--ffmpeg PROG] [--ffprobe PROG] [--model-dir DIR] [--work-dir DIR] [--no-compare]
```

- `<out_dir>`: a new or empty directory outside the repository. The runs inside record the repository's commit and whether its working tree was clean, so run it from a clean tree when its outputs are to be cited.
- It needs what `docs/BUILD.md` installs, without the glad venv, and the network: the streams come from Apple's CDN, and the builds fetch their dependencies from GitHub.
- Without `--mirage-vmaf`, `--mirage-vmaf-before` and `--mirage-vmaf-before-d022`, it builds the programs the steps need itself, each in a temporary worktree of its commit (`cmake --preset macos-release`, then only the `mirage_vmaf` target), and keeps them in `<out_dir>/builds/`.

The steps, in order; each writes under `<out_dir>` the path its committed counterpart has under `eval/` (P-024):

| step | reruns | `mirage_vmaf` |
|---|---|---|
| `streams` | `streams/fetch_streams.py --probe`, against `streams/fetched-2026-10-23/` | |
| `clips` | `consistency/color_cases.py`, against `consistency/color-cases-before/clips.txt` | |
| `format-clips` | `consistency/color_cases.py --set formats`, against `consistency/format-cases-before/clips.txt` | |
| `diag-before-300` | `consistency/diag-before-300/`: plain and relabel | `83c8ed6`'s |
| `color-cases-before` | `consistency/color-cases-before/`: the scored and the refused cases | `83c8ed6`'s |
| `format-cases-before` | `consistency/format-cases-before/`: the scored and the refused cases | `83c8ed6`'s |
| `diag-after-300` | `consistency/diag-after-300/` | HEAD's |
| `color-cases-after` | `consistency/color-cases-after/` | HEAD's |
| `format-cases-after` | `consistency/format-cases-after/` | `d993686`'s |
| `format-cases-d022` | `consistency/format-cases-d022/` | HEAD's |
| `scalers-300` | `consistency/scalers-300/`: three runs and `scaler_effect.*` | HEAD's |
| `models-300` | `consistency/models-300/`: four runs and `model_effect.*` | HEAD's |
| `defaults-300` | `consistency/defaults-300/`: the first 300 frames of the four pairs with the defaults of D-024 and D-027 | HEAD's |
| `acceptance-full` | `consistency/acceptance-full/`: every frame of the four pairs of D-018, the §5 report | HEAD's |
| `ffmpeg-scale-default` | the commands of `consistency/ffmpeg-scale-default/checks.txt` | |
| `ffmpeg-libvmaf-filter` | the commands of `consistency/ffmpeg-libvmaf-filter/checks.txt` | |
| `pooling` | `pooling/bipbop16x9/`, on the rerun of `diag-after-300` | |
| `pooling-defaults-300` | `pooling/defaults-300/`, `--check-pooled` on the rerun of `defaults-300` | |
| `pooling-full` | `pooling/acceptance-full/`, on the rerun of `acceptance-full` | |

- Every `run_delta.sh` run gets the options of its committed `run.cmd.txt`, the scaler, the model and the threads always named, which a test checks: since D-024 `run_delta.sh`'s default scaler is bicubic, and the runs made before it with bilinear, the default then, are rerun with bilinear (P-038). The runs made before D-016 need the program that didn't convert color yet, the build of `83c8ed6`; `format-cases-after`, made after D-016 and before D-022, the build of `d993686`, which doesn't convert a reference that isn't yuv420p (P-034); the others use HEAD's.
- `--only` runs some steps, and the steps they need (`pooling` needs `diag-after-300`, `pooling-defaults-300` needs `defaults-300`, `pooling-full` needs `acceptance-full`, the color tag cases need `clips` and the three runs of the reference format cases `format-clips`); `--skip-before` leaves out the three steps before D-016, and `--skip-full` the two over every frame, `acceptance-full` and `pooling-full` (P-027). Each side of each pair of `acceptance-full` took longer than the whole run of 2026-10-23 below (the wall times in `consistency/acceptance-full/plain/*/delta.md`).
- `--jobs` is `run_delta.sh`'s: how many scoring processes run at once. The committed runs used 1, except `acceptance-full`, which used 4; its first 300 frames have the scores of `scalers-300/bilinear`, made by the same program one process at a time (`consistency/acceptance-full/checks.txt`, check 3).

**What "as committed" means.** For each step, `rerun_check.md` says whether the rerun has:

- for every run and pair, the committed verdict, Mirage's per-frame scores at full precision and every libvmaf metric of every frame of FFmpeg's log;
- the numbers of `compare_runs.py`'s summary (its `settings` aside, which name a model file by its path) and of `pooling.py`'s series;
- the committed bytes of every playlist and the committed parameters of every probed video stream;
- the committed `clips.txt`: each clip's command, tags and pixel checksum;
- the committed output of each command of the FFmpeg records (heap addresses in FFmpeg's error lines aside).

It also gives each program's SHA-256 next to the one in the committed runs' `build.txt`.

**What it writes.** `run_all.md` and `run_all.json` (every command, its exit status and wall time, and the repository's state at the start and end), `rerun_check.md` and `rerun_check.json`, `builds/<name>/build.txt` with the build logs, `logs/` with the output of every command, and the rerun outputs themselves. Local paths in its own files appear as `<out_dir>`, `<work>`, `<worktree>`, `<repo>`, `<home>` and `<tmp>`.

**Exit status.** 0: every step ran and everything compared is as committed. 1: a step failed or something differs; `rerun_check.md` says what. 2: bad arguments, or the run could not start.

**Not part of it.** The `checks.txt` files of the committed run directories, which compare committed outputs with each other, and the build checks of `build_check/`, which need their own empty directory and Python 3.13 and build everything.

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
