# Build check

The record behind `docs/BUILD.md`: a committed HEAD built in a clean worktree the way `BUILD.md` describes, with the outcome of every step.

## How it was run

```
eval/build_check/run_build_check.sh <repo> <scratch> /opt/homebrew/opt/python@3.13/bin/python3.13 > <scratch>/record.txt
```

The output was copied verbatim to the record. The commit it built is in the record's "Commit" section.

What `run_build_check.sh` does, in order, since D-031:
1. Records the machine, the tool versions, the installed and current Homebrew versions of ffmpeg-full, libvmaf, GLFW and x264, what pkg-config resolves through the presets' `PKG_CONFIG_PATH`, Homebrew's version, and the help of `brew pin` and `brew extract`.
2. Records, from the copy of each formula Homebrew keeps in the installed package, the source ffmpeg-full, libvmaf and x264 were built from (`url`, `sha256`, or the git `revision`), the pins of `docker/Dockerfile`; and which x264 and libvmaf ffmpeg-full's libraries link.
3. Checks that the repository's working tree is clean, creates a worktree of HEAD with `git worktree add --detach`, and lists the presets CMake offers there.
4. Configures and builds the `macos-release` preset, the `macos-debug` preset and the `macos-headless` preset (`MIRAGE_HEADLESS`), each with nothing more than `docs/BUILD.md` gives, and lists the dependencies the release and headless configurations fetched.
5. Shows every line naming Python in each configuration's configure log, CMake cache and Ninja build file.
6. Runs the unit tests of the debug build and of the headless build, excluding `[audio]`, each with a 900 s alarm.
7. Lists the built programs, the libraries `mirage_vmaf` links, the first line of `mirage_vmaf --help`, the SHA-256 of the three builds' `mirage_vmaf`, and whether the release and headless ones are identical.
8. For every `FetchContent_Declare` in `CMakeLists.txt`: the `GIT_TAG` it asks for, the commit fetched, the release tag at that commit, and how many commits the upstream default branch had after it when the check fetched it; for a dependency pinned by commit, those later commits with the files they change.
9. For each file of glad's committed loader (`third_party/glad/`): its size and SHA-256, and the generator and license lines of `gl.h`; then `third_party/glad/regenerate.sh` with the given Python 3.13, which regenerates the files with glad 2.0.8 in its own venv and compares the bytes.
10. The SHA-256 of `LICENSE`, and every GPLv3 text that Homebrew's installed packages ship (`COPYING*` and `LICENSE*` files under `/opt/homebrew/Cellar`), grouped by SHA-256, with a `diff` against each text that differs from `LICENSE`.
11. Removes the worktree.

Until D-031 the script built the release preset with the default Python, which stopped at glad's code generation on this machine, then `mirage_vmaf` and `mirage_tests` alone, then both presets with a glad venv it created (Python 3.13, with `jinja2==3.1.6` and `markupsafe==3.0.4` from PyPI), ran the debug build's unit tests, and recorded the files glad generated in both builds. The record of 2026-10-23 (`macos-2026-10-23.txt`) was made by the script as of commit `647dfbf`, which had steps 1 to 6 and 10 of that script without its later additions, and installed `jinja2==3.1.6` alone; the record at `6a6661f` by the script as of that commit.

Local paths in the record are replaced by placeholders: `<worktree>`, `<scratch>`, `<repo>`, `<home>`, `<tmp>` (the per-user temporary directory) and `<hostname>`. The full logs of each step stayed in `<scratch>/logs` and aren't committed.

## Files

| File | Contents |
|---|---|
| `run_build_check.sh` | the script |
| `macos-2026-10-23.txt` | its output for the run of 2026-10-23 at `647dfbf`, made by the script as it was at that commit |
| `macos-2026-10-23-6a6661f.txt` | its output for the run of 2026-10-23 at `6a6661f`, after miniaudio and stb were pinned: the check of the section 8 build (`docs/DECISIONS.md` P-022) |
