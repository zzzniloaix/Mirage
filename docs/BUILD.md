# Building Mirage

This is the build as it works today (D-031):

- **macOS on Apple Silicon**: the player, `mirage_vmaf` and the unit tests, with the presets `macos-debug` and `macos-release`, and `mirage_vmaf` and the unit tests alone with `macos-headless` (steps 1 to 5).
- **Linux**: `mirage_vmaf` and the unit tests, without the player, with the preset `linux-release`. Written, **not yet built on Linux** (step 6).
- **A Docker image** for rerunning the measurements on Linux: `docker/README.md`. Written, **not yet built** (step 7).

The macOS build was run from a clean worktree of a committed HEAD three times: at `647dfbf` (`eval/build_check/macos-2026-10-23.txt`), at `6a6661f`, after miniaudio and stb were pinned (`eval/build_check/macos-2026-10-23-6a6661f.txt`), and at `5ae44a4`, after glad's loader was committed and the headless configuration added (`eval/build_check/macos-2026-10-24-5ae44a4.txt`). Each record has the versions of everything involved and the outcome of every step; `eval/build_check/README.md` says how they were made (DECISIONS.md P-007, P-022, P-058). The two checks of 2026-10-23 ended as the build was then: the default Python stopped the build at glad's code generation, a venv of Python 3.13 built both presets, and one unit test failed in a clean checkout. The check at `5ae44a4` ran steps 1 to 4 as this file describes them and ended as they say. A clean worktree on a machine that already has the Homebrew packages is not a clean machine: none of the checks installed anything.

## 1. Homebrew packages

```
brew install cmake ninja pkgconf ffmpeg-full libvmaf glfw
```

The build checks used these packages as they were already installed on their machine; their versions are in the records and in "Versions" below. The compiler is Apple clang from the Xcode Command Line Tools. `CMakeLists.txt` asks for CMake 3.28 or later.

- **FFmpeg comes from `ffmpeg-full`.** Both presets put `/opt/homebrew/opt/ffmpeg-full/lib/pkgconfig` first on `PKG_CONFIG_PATH`, so the build links ffmpeg-full's libraries even when Homebrew's `ffmpeg` is installed too, as it is on the machine of the records (D-009). `README.md` and `CLAUDE.md` name `ffmpeg-full` as well.
- **Homebrew's versions can't be pinned.** `brew install` installs the version a formula has on the day it runs. `brew pin` only keeps an installed formula from being upgraded, and `brew extract` copies an older version of a formula into a tap of one's own (`brew help pin` and `brew help extract`, in both records since `6a6661f`); this build hasn't been tried with an extracted formula. So a new machine gets Homebrew's current versions, not those below: in the record at `5ae44a4`, Homebrew's current formulae of ffmpeg-full, libvmaf and GLFW are all newer than the installed versions the build was verified with, ffmpeg-full by a major version ("installed vs current stable"). Nothing in the build checks versions yet. Whether Mirage builds with the newer versions, and whether its scores change with them, is untested. The Docker image builds FFmpeg and libvmaf at the measured versions (step 7).
- No Python is needed to build (step 2). Python 3.13 is only for regenerating glad's loader (`third_party/glad/README.md`); the eval scripts run with `python3` ("Versions").

## 2. Configure and build

```
cmake --preset macos-debug            # or macos-release
cmake --build --preset macos-debug    # or macos-release
```

- The first configure fetches the other dependencies from GitHub with CMake FetchContent (`CMakeLists.txt`), each pinned: pugixml, Catch2, Dear ImGui and nativefiledialog-extended by release tag, miniaudio and stb by commit. The commits are in "Versions" below.
- **glad's OpenGL loader is committed** (`third_party/glad/`, D-031): the files glad 2.0.8 generated in the builds the records show until then, built as a plain static library. So neither the configure nor the build runs Python: in the record at `5ae44a4`, no line of the release, debug or headless configuration's configure log, CMake cache or Ninja build file names Python ("Python in the configure and the build"). `third_party/glad/README.md` says how the files were generated, and `third_party/glad/regenerate.sh` makes them again and compares the bytes: in the check at `5ae44a4` all three were identical ("glad's committed loader").
- The programs land in `build/macos-debug/` or `build/macos-release/`:
  - `mirage`: the player.
  - `mirage_vmaf`: the player's VMAF analyzer without a window (D-017); see `mirage_vmaf --help`.
  - `mirage_tests`: the unit tests.
  - `snapshot_diff`: the PNG comparison behind `tests/pathb_snapshot_regression.sh`.
- While configuring, CMake prints `libvmaf package version: …`. `mirage_vmaf` writes that value into its reports, next to the version libvmaf reports about itself (D-006). It is read from the real path of the libvmaf library that pkg-config resolves (a Homebrew Cellar path), so it is only as current as the last configure: configure again after `brew upgrade libvmaf`. Where the path carries no version, as outside Homebrew, it is `unknown`.

Until D-031 glad generated the loader during the build with the Python CMake had found, and on the machine of the records Homebrew's Python 3.14 couldn't import `pyexpat` (`Symbol not found: _XML_SetAllocTrackerActivationThreshold`), so the build stopped at that step unless CMake was given a venv of Python 3.13 with Jinja2 (P-003; "Release preset, default Python" in both records of 2026-10-23). That is no longer needed.

## 3. Without the player: the headless preset

```
cmake --preset macos-headless
cmake --build --preset macos-headless
```

- `macos-headless` is `macos-release` with `MIRAGE_HEADLESS` on, in `build/macos-headless/`. With that CMake option, any configure leaves out the player and what only the player uses: OpenGL, GLFW, glad, stb, Dear ImGui and nativefiledialog-extended, and the player's snapshot check. It builds `mirage_vmaf` and `mirage_tests`, which links none of them.
- In the record at `5ae44a4` ("Headless preset", "Built programs"), it fetched only miniaudio, pugixml and Catch2, built `mirage_vmaf` and `mirage_tests` but not `mirage` or `snapshot_diff`, and its `mirage_vmaf` is identical to the release preset's (SHA-256 `9f04085f73440a9c9969f9bf6a86767eaf9ade89ae8ca393f8b6c99bb78b9b06` for both).
- On Linux the preset is `linux-release` (step 6).

## 4. Run the unit tests

```
perl -e 'alarm 900; exec @ARGV' build/macos-debug/mirage_tests '~[audio]'
```

- This is the form the build checks and unattended runs use: it leaves out the tests tagged `[audio]` and ends a run that stalls after 900 seconds. Without arguments, `mirage_tests` runs every test. The headless build's tests are `build/macos-headless/mirage_tests`.
- The `[vmaf]` tests run the VMAF analyzer on small clips they generate.
- **In the record at `5ae44a4` every test passed**, in the debug build and in the headless build: 311 test cases each, with 111765 and 111752 assertions ("Unit tests (debug build)", "Unit tests (headless build)").
- **The subtitle fixture.** In a clean checkout one test failed until `97f0098`: `SidecarSubtitles — decodes data/sub_test/test.srt` read that file, but `.gitignore` ignores `data/`, so the file isn't in the repository (both records of 2026-10-23, "Unit tests"). Since then the test, `SidecarSubtitles — decodes an SRT file`, writes its fixture at run time (D-032); in the clean checkout of the check at `5ae44a4` it passed with the others.
- **Release builds.** Until `a0d69fc`, the test `HLS master → variant playlist: FFmpeg structure, open_variant checks, same timeline` passed a temporary stop token to `ffopen::open_variant` and then read from the context it opened, whose interrupt callback keeps a pointer to the token (`src/FfOpen.h`: the token must outlive the context), so the context's reads checked a token that no longer existed (P-057). The test is fixed. The headless preset is a release build; no committed record ran a release build's tests before the check at `5ae44a4`.
- Every test that writes a file writes it in a temporary directory of the run's own, `mirage_tests.XXXXXX` under the system's temporary directory, which the run removes when it ends normally (`tests/TestTemp.h`), so two runs at the same time don't share files.

## 5. Rerun the measurements

```
eval/run_all.sh <out_dir>
```

- It builds the `mirage_vmaf` of each committed run from its commit, reruns every committed Phase 1 measurement, and compares the outputs with the committed ones; `eval/README.md` says how. It needs the packages of step 1 and the network, and no Python but the `python3` it runs with.
- Its runs and their records are listed in `eval/README.md`.

## 6. Linux: mirage_vmaf and the unit tests, not yet built on Linux

**Nothing of this step has been built or run on Linux** (MEASUREMENT PENDING): the machine of the records has no Linux host and no container runtime (D-031). It follows from `CMakeLists.txt` and `CMakePresets.json`, and the Docker image of step 7 configures and builds the same way.

What the headless build needs, from `CMakeLists.txt` and the sources:

- CMake 3.28 or later, Ninja, pkg-config, git (for FetchContent) and the network;
- a C++20 compiler whose standard library has `<format>`;
- FFmpeg 8 with libavformat, libavcodec, libavutil, libswresample, libswscale and libavfilter, found by pkg-config;
- libvmaf 3 with its built-in models, found by pkg-config, and its model files in `<prefix>/share/libvmaf/model`, where `mirage_tests` reads one (`CMakeLists.txt`, `MIRAGE_LIBVMAF_MODEL_DIR`).

The measurements were made with FFmpeg 8.1.1 and libvmaf 3.1.0 ("Versions" below); a distribution's packages may be other versions. The Dockerfile builds both from their release sources, checked by SHA-256 (step 7).

```
cmake --preset linux-release
cmake --build --preset linux-release
perl -e 'alarm 900; exec @ARGV' build/linux-release/mirage_tests '~[audio]'
```

- `linux-release` is Ninja, Release, `MIRAGE_HEADLESS` on, in `build/linux-release/`. CMake offers it only on a Linux host (its `condition`), and it sets no `PKG_CONFIG_PATH`: pkg-config finds FFmpeg and libvmaf where they are installed, or where `PKG_CONFIG_PATH` says.
- `eval/run_all.sh` with the Linux build's program, and FFmpeg and libvmaf's model directory named, since its defaults for them are Homebrew's paths:

  ```
  eval/run_all.sh <out_dir> --mirage-vmaf build/linux-release/mirage_vmaf --ffmpeg <ffmpeg> --model-dir <prefix>/share/libvmaf/model
  ```

  Its FFmpeg needs the libvmaf filter, libx264 (the reference format clips, `eval/consistency/color_cases.py`) and HTTPS (Apple's CDN). It builds the `mirage_vmaf` of the runs made before D-016, D-022 and D-028 from their commits with their `macos-release` preset, and, without `--mirage-vmaf`, HEAD's too. Those commits' `CMakeLists.txt` configure the player as well, so they need the development files of OpenGL, GLFW and GTK 3 (nativefiledialog-extended's Linux backend) and a Python 3 interpreter (glad's configure step) to configure, though none of it goes into `mirage_vmaf`: read from those `CMakeLists.txt` and their dependencies' CMake files, not run on Linux.
- `mirage_vmaf`'s reports give the libvmaf package version as `unknown` outside Homebrew (step 2).

## 7. The Docker image: not yet built

`docker/Dockerfile` builds a Debian image with FFmpeg 8.1.1 and libvmaf 3.1.0 from their release sources, checked by SHA-256, and `mirage_vmaf` from the repository with the `linux-release` preset, and runs `eval/run_all.sh` as its command. **It has not been built** (MEASUREMENT PENDING): the machine of the records has no container runtime (D-031). `docker/README.md` has the commands, the pins and where each comes from, and what differs from the macOS measurements.

## Versions

The versions the committed measurements were made with, from the environment captures committed next to them:

- `eval/baseline/environment.txt`: the Phase 0 baseline, 2026-10-21;
- `eval/build_check/macos-2026-10-23.txt`, `macos-2026-10-23-6a6661f.txt` and `macos-2026-10-24-5ae44a4.txt`: the build checks;
- `environment.txt` in every run directory of `eval/consistency/`: the §5 runs of Phase 1.

| What | Version | Where it is recorded |
|---|---|---|
| Machine | Apple M3 Pro (arm64), macOS 26.2 (25C56) | all three |
| Compiler | Apple clang 21.0.0 (clang-2100.1.1.101), Xcode Command Line Tools | `environment.txt`, build checks |
| CMake, Ninja, pkgconf | 4.3.0, 1.13.2, 2.5.1 | `environment.txt`, build checks |
| FFmpeg | Homebrew's ffmpeg-full 8.1.1: libavformat 62.12.101, libavcodec 62.28.101, libavutil 60.26.101, libswscale 9.5.101, libswresample 6.3.101, libavfilter 11.14.101 | all three (the §5 runs: `ffmpeg -version`) |
| libvmaf | Homebrew's libvmaf 3.1.0, whose library reports 3.0.0 (`vmaf_version()`, `pkg-config --modversion`; D-006) | all three |
| x264 | Homebrew's x264 r3222, which ffmpeg-full's libraries link | the build check at `5ae44a4` |
| GLFW | Homebrew's glfw 3.4 (pkg-config: 3.4.0) | `environment.txt`, build checks |
| OpenGL | 4.1 core profile, through glad's committed loader (`third_party/glad/`) | `third_party/glad/include/glad/gl.h` |
| Python for glad's loader | glad 2.0.8 generated it; `regenerate.sh` uses Homebrew's python@3.13 3.13.1, in a venv with Jinja2 3.1.6, MarkupSafe 3.0.4 and pip 24.3.1 | build checks |
| Python for the eval scripts | `python3` 3.14.7 (Homebrew's python@3.14); the scripts use the standard library only | the §5 runs' `environment.txt` |

The FetchContent dependencies: how `CMakeLists.txt` pins each one, and the commit the Phase 0 build fetched:

| Dependency | Pin in `CMakeLists.txt` | Commit fetched |
|---|---|---|
| miniaudio | commit | `9634bedb5b5a2ca38c1ee7108a9358a4e233f14d` |
| stb | commit (not in the headless configuration) | `a7d4c2175630e1ea5b6f8dd00ab96fed0b97b96d` |
| pugixml | tag `v1.14` | `db78afc2b7d8f043b4bc6b185635d949ea2ed2a8` |
| Catch2 | tag `v3.7.1` | `fa43b77429ba76c462b1898d6cd2f2d7a9416b14` |
| Dear ImGui | tag `v1.91.9` (not in the headless configuration) | `97428e8ac99e339ce05eee531cf55b77b29ea709` |
| nativefiledialog-extended | tag `v1.2.1` (not in the headless configuration) | `86d5f2005fe1c00747348a12070fec493ea2407e` |

- The Phase 0 build fetched all six commits above (`environment.txt`), and glad's `v2.0.8`, `73db193f853e2ee079bf3ca8a64aa2eaf6459043`, which generated the committed loader. miniaudio and stb then followed their `master` branch; they are pinned to the commits Phase 0 fetched.
- The first build check of 2026-10-23 fetched the same commits except stb, which was then at `003501f6d7a8668ce8b6d875e34dd97ad0d5d5b5` (`macos-2026-10-23.txt`, "Dependencies fetched at configure time"). The two upstream commits from the pinned one to `003501f6` change only stb's `CONTRIBUTING.md` (`macos-2026-10-23-6a6661f.txt`).
- The checks at `6a6661f` and `5ae44a4` fetched exactly the commits of the table. The miniaudio commit is the one miniaudio's tag `0.11.25` names; no tag names the stb commit ("Dependencies fetched at configure time" in both).
- `mirage_vmaf`, the program behind every §5 run, compiles neither miniaudio nor stb (`CMakeLists.txt`), so their versions take no part in any committed score. The release `mirage_vmaf` the check at `6a6661f` built has the SHA-256 of the one that made the scaler and model runs, `3633c43eaf4261e567ef2fb3a24d015e3e00b9a8ee354ddef88b6330aca4938c` (`macos-2026-10-23-6a6661f.txt`, "Built programs"; `eval/consistency/scalers-300/build.txt`).

## Not covered yet

- Linux and the Docker image are written, not built or run (steps 6 and 7; MEASUREMENT PENDING).
- Pinned versions of the Homebrew packages: Homebrew can't pin them (step 1). The Docker image pins FFmpeg, libvmaf and x264, not Debian's packages (`docker/README.md`).
- The libvmaf package version of D-006 outside Homebrew (step 2).
