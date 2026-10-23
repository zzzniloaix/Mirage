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
eval/run_all.sh <out_dir> [--mirage-vmaf PROG] [--mirage-vmaf-before PROG] [--skip-before]
    [--skip-full] [--only STEP[,STEP...]] [--jobs J] [--ffmpeg PROG] [--ffprobe PROG]
    [--model-dir DIR] [--work-dir DIR] [--no-compare]
```

- `<out_dir>`: a new or empty directory outside the repository. The runs inside record the repository's commit and whether its working tree was clean, so run it from a clean tree when its outputs are to be cited.
- It needs what `docs/BUILD.md` installs, without the glad venv, and the network: the streams come from Apple's CDN, and the builds fetch their dependencies from GitHub.
- Without `--mirage-vmaf` and `--mirage-vmaf-before`, it builds both programs itself, each in a temporary worktree of its commit (`cmake --preset macos-release`, then only the `mirage_vmaf` target), and keeps them in `<out_dir>/builds/`.

The steps, in order; each writes under `<out_dir>` the path its committed counterpart has under `eval/` (P-024):

| step | reruns | `mirage_vmaf` |
|---|---|---|
| `streams` | `streams/fetch_streams.py --probe`, against `streams/fetched-2026-10-23/` | |
| `clips` | `consistency/color_cases.py`, against `consistency/color-cases-before/clips.txt` | |
| `diag-before-300` | `consistency/diag-before-300/`: plain and relabel | `83c8ed6`'s |
| `color-cases-before` | `consistency/color-cases-before/`: the scored and the refused cases | `83c8ed6`'s |
| `diag-after-300` | `consistency/diag-after-300/` | HEAD's |
| `color-cases-after` | `consistency/color-cases-after/` | HEAD's |
| `scalers-300` | `consistency/scalers-300/`: three runs and `scaler_effect.*` | HEAD's |
| `models-300` | `consistency/models-300/`: four runs and `model_effect.*` | HEAD's |
| `acceptance-full` | `consistency/acceptance-full/`: every frame of the four pairs of D-018, the §5 report | HEAD's |
| `ffmpeg-scale-default` | the commands of `consistency/ffmpeg-scale-default/checks.txt` | |
| `ffmpeg-libvmaf-filter` | the commands of `consistency/ffmpeg-libvmaf-filter/checks.txt` | |
| `pooling` | `pooling/bipbop16x9/`, on the rerun of `diag-after-300` | |
| `pooling-full` | `pooling/acceptance-full/`, on the rerun of `acceptance-full` | |

- Every `run_delta.sh` run gets the options of its committed `run.cmd.txt`. The runs made before D-016 need the program that didn't convert color yet, stage 1's build of `83c8ed6`; the others use HEAD's.
- `--only` runs some steps, and the steps they need (`pooling` needs `diag-after-300`, `pooling-full` needs `acceptance-full`, the color cases need `clips`); `--skip-before` leaves out the two steps before D-016, and `--skip-full` the two over every frame, `acceptance-full` and `pooling-full` (P-027). Each side of each pair of `acceptance-full` took longer than the whole run of 2026-10-23 below (the wall times in `consistency/acceptance-full/plain/*/delta.md`).
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
