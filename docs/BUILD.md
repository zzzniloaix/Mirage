# Building Mirage on macOS

This is the build as it works today: macOS on Apple Silicon, with the two presets in `CMakePresets.json`. Linux, and a Docker or Nix environment for the evaluation, wait for the owner's decisions (the rest of item 11 of `docs/VMAF_GAP_REPORT.md`: whether glad's generated output is committed, and the scope of Linux support; see `docs/PHASE1_DECISIONS_PENDING.md`).

Steps 2 to 4 were run on 2026-10-23 from a clean worktree of a committed HEAD, twice: at `647dfbf` (`eval/build_check/macos-2026-10-23.txt`) and, after miniaudio and stb were pinned, at `6a6661f` (`eval/build_check/macos-2026-10-23-6a6661f.txt`). Each record has the versions of everything involved and the outcome of every step; `eval/build_check/README.md` says how they were made (DECISIONS.md P-007, P-022). Both ended as this file describes: the default Python stops the build at glad, the venv of step 3 builds both presets, and one unit test fails in a clean checkout. A clean worktree on a machine that already has the Homebrew packages is not a clean machine: neither check installed anything.

## 1. Homebrew packages

```
brew install cmake ninja pkgconf ffmpeg-full libvmaf glfw python@3.13
```

The build check used these packages as they were already installed on its machine; their versions are in the record and in "Versions" below. The compiler is Apple clang from the Xcode Command Line Tools. `CMakeLists.txt` asks for CMake 3.28 or later.

- **FFmpeg comes from `ffmpeg-full`.** Both presets put `/opt/homebrew/opt/ffmpeg-full/lib/pkgconfig` first on `PKG_CONFIG_PATH`, so the build links ffmpeg-full's libraries even when Homebrew's `ffmpeg` is installed too, as it is on the machine of the record (D-009). `README.md` and `CLAUDE.md` name `ffmpeg-full` as well.
- **Homebrew's versions can't be pinned.** `brew install` installs the version a formula has on the day it runs. `brew pin` only keeps an installed formula from being upgraded, and `brew extract` copies an older version of a formula into a tap of one's own (`brew help pin` and `brew help extract`, both in `eval/build_check/macos-2026-10-23-6a6661f.txt`); this build hasn't been tried with an extracted formula. So a new machine gets Homebrew's current versions, not those below: in the record, Homebrew's current formulae of ffmpeg-full, libvmaf and GLFW are all newer than the installed versions the build was verified with, ffmpeg-full by a major version ("installed vs current stable"). Nothing in the build checks versions yet. Whether Mirage builds with the newer versions, and whether its scores change with them, is untested.
- `python@3.13` is only for the glad workaround in step 3.

## 2. Configure and build

```
cmake --preset macos-debug            # or macos-release
cmake --build --preset macos-debug    # or macos-release
```

- The first configure fetches the other dependencies from GitHub with CMake FetchContent (`CMakeLists.txt`), each pinned: glad, pugixml, Catch2, Dear ImGui and nativefiledialog-extended by release tag, miniaudio and stb by commit. The commits are in "Versions" below.
- The programs land in `build/macos-debug/` or `build/macos-release/`:
  - `mirage`: the player.
  - `mirage_vmaf`: the player's VMAF analyzer without a window (D-017); see `mirage_vmaf --help`.
  - `mirage_tests`: the unit tests.
  - `snapshot_diff`: the PNG comparison behind `tests/pathb_snapshot_regression.sh`.
- While configuring, CMake prints `libvmaf package version: …`. `mirage_vmaf` writes that value into its reports, next to the version libvmaf reports about itself (D-006). It is read from the real path of the libvmaf library that pkg-config resolves (a Homebrew Cellar path), so it is only as current as the last configure: configure again after `brew upgrade libvmaf`.

## 3. If the build stops at glad: use Python 3.13 (P-003)

glad generates the OpenGL loader during the build, with the Python interpreter CMake found when it configured. On the machine of the record, Homebrew's Python 3.14 can't import `pyexpat` (`Symbol not found: _XML_SetAllocTrackerActivationThreshold`), so the build stops at the glad step (record: "Release preset, default Python"). A virtual environment with Python 3.13 and jinja2 avoids it:

```
/opt/homebrew/opt/python@3.13/bin/python3.13 -m venv <venv>
<venv>/bin/pip install jinja2==3.1.6 markupsafe==3.0.4
cmake --preset macos-release -DPython_EXECUTABLE=<venv>/bin/python
cmake --build --preset macos-release
```

- `<venv>` is a new directory. Outside the repository it doesn't show up in `git status`.
- `markupsafe==3.0.4` is the version pip installed with `jinja2==3.1.6` in the record; naming it keeps a later install from taking a newer one.
- Configure each preset once this way. The interpreter stays in that build directory's cache, and later configures keep using it.
- `mirage_vmaf` and `mirage_tests` don't use OpenGL, so `cmake --build --preset macos-release --target mirage_vmaf` (or `--target mirage_tests`) builds without the workaround.

## 4. Run the unit tests

```
perl -e 'alarm 900; exec @ARGV' build/macos-debug/mirage_tests '~[audio]'
```

- This is the form the build check and unattended runs use: it leaves out the tests tagged `[audio]` and ends a run that stalls after 900 seconds. Without arguments, `mirage_tests` runs every test.
- The `[vmaf]` tests run the VMAF analyzer on small clips they generate.
- **In a clean checkout one test fails**: `SidecarSubtitles — decodes data/sub_test/test.srt` reads that file, but `.gitignore` ignores `data/`, so the file isn't in the repository (record: "Unit tests").

## 5. Rerun the measurements

```
eval/run_all.sh <out_dir>
```

- It builds the `mirage_vmaf` of each committed run from its commit, reruns every committed Phase 1 measurement, and compares the outputs with the committed ones; `eval/README.md` says how. It needs the packages of step 1 and the network, not the venv of step 3.
- Its run of 2026-10-23, from a clean tree at `e2169f5`, found everything as committed and took 544.3 s of wall time, both builds included (`eval/run_all/2026-10-23/rerun_check.md`, `run_all.md`). That was before the full-length §5 run was committed. `run_all.sh` reruns it since, as the step `acceptance-full`; its one rerun so far, with `pooling-full` and `--jobs 4`, found everything as committed and took 2707.1 s, the build included (`eval/run_all/2026-10-23-full/`). `--skip-full` leaves those two steps out.

## Versions

The versions the committed measurements were made with, from the environment captures committed next to them:

- `eval/baseline/environment.txt`: the Phase 0 baseline, 2026-10-21;
- `eval/build_check/macos-2026-10-23.txt`: the build check of 2026-10-23;
- `environment.txt` in every run directory of `eval/consistency/`: the §5 runs of Phase 1.

| What | Version | Where it is recorded |
|---|---|---|
| Machine | Apple M3 Pro (arm64), macOS 26.2 (25C56) | all three |
| Compiler | Apple clang 21.0.0 (clang-2100.1.1.101), Xcode Command Line Tools | `environment.txt`, build check |
| CMake, Ninja, pkgconf | 4.3.0, 1.13.2, 2.5.1 | `environment.txt`, build check |
| FFmpeg | Homebrew's ffmpeg-full 8.1.1: libavformat 62.12.101, libavcodec 62.28.101, libavutil 60.26.101, libswscale 9.5.101, libswresample 6.3.101, libavfilter 11.14.101 | all three (the §5 runs: `ffmpeg -version`) |
| libvmaf | Homebrew's libvmaf 3.1.0, whose library reports 3.0.0 (`vmaf_version()`, `pkg-config --modversion`; D-006) | all three |
| GLFW | Homebrew's glfw 3.4 (pkg-config: 3.4.0) | `environment.txt`, build check |
| OpenGL | 4.1 core profile, through the loader glad generates (`glad_add_library(... API gl:core=4.1)` in `CMakeLists.txt`) | `CMakeLists.txt` |
| Python for glad | Homebrew's python@3.13 3.13.1, in a venv with Jinja2 3.1.6, MarkupSafe 3.0.4 and pip 24.3.1 | build check |
| Python for the eval scripts | `python3` 3.14.7 (Homebrew's python@3.14); the scripts use the standard library only | the §5 runs' `environment.txt` |

The FetchContent dependencies: how `CMakeLists.txt` pins each one, and the commit the Phase 0 build fetched:

| Dependency | Pin in `CMakeLists.txt` | Commit fetched |
|---|---|---|
| glad | tag `v2.0.8` | `73db193f853e2ee079bf3ca8a64aa2eaf6459043` |
| miniaudio | commit | `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` |
| stb | commit | `a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d` |
| pugixml | tag `v1.14` | `db78afc2b7d8f043b4bc6b185635d949ea2ed2a8` |
| Catch2 | tag `v3.7.1` | `fa43b77429ba76c462b1898d6cd2f2d7a9416b14` |
| Dear ImGui | tag `v1.91.9` | `97428e8ac99e339ce05eee531cf55b77b29ea709` |
| nativefiledialog-extended | tag `v1.2.1` | `86d5f2005fe1c00747348a12070fec493ea2407e` |

- The Phase 0 build fetched all seven commits above (`environment.txt`). miniaudio and stb then followed their `master` branch; they are pinned to the commits Phase 0 fetched.
- The first build check of 2026-10-23 fetched the same commits except stb, which was then at `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5` (`macos-2026-10-23.txt`, "Dependencies fetched at configure time"). The two upstream commits from the pinned one to `003501f6` change only stb's `CONTRIBUTING.md` (`macos-2026-10-23-6a6661f.txt`).
- The check at `6a6661f` fetched exactly the commits of the table. The miniaudio commit is the one miniaudio's tag `0.11.25` names; no tag names the stb commit (`macos-2026-10-23-6a6661f.txt`, "Dependencies fetched at configure time").
- `mirage_vmaf`, the program behind every §5 run, compiles neither miniaudio nor stb (`CMakeLists.txt`), so their versions take no part in any committed score. The release `mirage_vmaf` the check at `6a6661f` built has the SHA-256 of the one that made the scaler and model runs, `3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c` (`macos-2026-10-23-6a6661f.txt`, "Built programs"; `eval/consistency/scalers-300/build.txt`).

## Not covered yet

- Linux, and a Docker or Nix environment (§8; the decisions are the owner's, `docs/PHASE1_DECISIONS_PENDING.md`).
- Whether glad's generated output is committed instead of generated at build time (the same).
- Pinned versions of the Homebrew packages: Homebrew can't pin them (step 1).
