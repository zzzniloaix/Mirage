# Building Mirage on macOS

This is the build as it works today: macOS on Apple Silicon, with the two presets in `CMakePresets.json`. It is the minimal §8 documentation that Phase 1 starts with (`docs/DECISIONS.md` D-001). The rest of §8 comes later in Phase 1: pinned dependency versions, Linux, a Docker or Nix environment and a LICENSE file. Parts of it wait for the owner (item 11 of `docs/VMAF_GAP_REPORT.md`: whether glad's generated output is committed, and the scope of Linux support).

Steps 2 to 4 were run on 2026-10-23 from a clean worktree of a committed HEAD. The record, with the versions of everything involved, is `eval/build_check/macos-2026-10-23.txt`; `eval/build_check/README.md` says how it was made (DECISIONS.md P-007).

## 1. Homebrew packages

```
brew install cmake ninja pkgconf ffmpeg-full libvmaf glfw python@3.13
```

The build check used these packages as they were already installed on its machine; their versions are in the record. The compiler is Apple clang from the Xcode Command Line Tools. `CMakeLists.txt` asks for CMake 3.28 or later.

- **FFmpeg comes from `ffmpeg-full`.** Both presets put `/opt/homebrew/opt/ffmpeg-full/lib/pkgconfig` first on `PKG_CONFIG_PATH`, so the build links ffmpeg-full's libraries even when Homebrew's `ffmpeg` is installed too, as it is on the machine of the record (D-009). `README.md` and `CLAUDE.md` name `ffmpeg-full` as well.
- **Homebrew doesn't pin versions.** `brew install` installs the current formulae. In the record, Homebrew's current formulae of ffmpeg-full, libvmaf and GLFW are all newer than the installed versions the build was verified with, ffmpeg-full by a major version ("installed vs current stable"). Nothing in the build checks versions yet.
- `python@3.13` is only for the glad workaround in step 3.

## 2. Configure and build

```
cmake --preset macos-debug            # or macos-release
cmake --build --preset macos-debug    # or macos-release
```

- The first configure fetches the other dependencies from GitHub with CMake FetchContent (`CMakeLists.txt`): glad v2.0.8, miniaudio, stb, pugixml v1.14, Catch2 v3.7.1, Dear ImGui v1.91.9 and nativefiledialog-extended v1.2.1. miniaudio and stb follow their `master` branch, so configures on different days can get different code. The record lists the revisions that were fetched.
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
<venv>/bin/pip install jinja2==3.1.6
cmake --preset macos-release -DPython_EXECUTABLE=<venv>/bin/python
cmake --build --preset macos-release
```

- `<venv>` is a new directory. Outside the repository it doesn't show up in `git status`.
- Configure each preset once this way. The interpreter stays in that build directory's cache, and later configures keep using it.
- `mirage_vmaf` and `mirage_tests` don't use OpenGL, so `cmake --build --preset macos-release --target mirage_vmaf` (or `--target mirage_tests`) builds without the workaround.

## 4. Run the unit tests

```
perl -e 'alarm 900; exec @ARGV' build/macos-debug/mirage_tests '~[audio]'
```

- This is the form the build check and unattended runs use: it leaves out the tests tagged `[audio]` and ends a run that stalls after 900 seconds. Without arguments, `mirage_tests` runs every test.
- The `[vmaf]` tests run the VMAF analyzer on small clips they generate.
- **In a clean checkout one test fails**: `SidecarSubtitles — decodes data/sub_test/test.srt` reads that file, but `.gitignore` ignores `data/`, so the file isn't in the repository (record: "Unit tests").

## Not covered yet

- Linux, and a Docker or Nix environment (§8; the Linux scope waits for the owner).
- Pinned versions of the Homebrew packages, and of miniaudio and stb.
- Whether glad's generated output is committed instead of generated at build time.
- The LICENSE file (D-008).
