# Build check

The record behind `docs/BUILD.md`: a committed HEAD built in a clean worktree the way `BUILD.md` describes, with the outcome of every step.

## How it was run

```
eval/build_check/run_build_check.sh <repo> <scratch> /opt/homebrew/opt/python@3.13/bin/python3.13 > <scratch>/record.txt
```

The output was copied verbatim to the record. The commit it built is in the record's "Commit" section.

What `run_build_check.sh` does, in order:
1. Records the machine, the tool versions, the installed and current Homebrew versions of ffmpeg-full, libvmaf and GLFW, what pkg-config resolves through the presets' `PKG_CONFIG_PATH`, Homebrew's version, and the help of `brew pin` and `brew extract`.
2. Checks that the repository's working tree is clean, and creates a worktree of HEAD with `git worktree add --detach`.
3. Configures and builds the `macos-release` preset with the default Python, then builds only the `mirage_vmaf` target and only the `mirage_tests` target with it.
4. Creates the glad venv (Python 3.13, with `jinja2==3.1.6` and `markupsafe==3.0.4` from PyPI). Configures and builds the release preset with `-DPython_EXECUTABLE=<venv>/bin/python`, configures it once more without that option and shows the interpreter left in its cache, then configures and builds the debug preset with the venv as well.
5. Runs the unit tests of the debug build, excluding `[audio]`, with a 900 s alarm.
6. Lists the built programs, the libraries `mirage_vmaf` links, the first line of `mirage_vmaf --help`, and the SHA-256 of both builds' `mirage_vmaf`.
7. For every `FetchContent_Declare` in `CMakeLists.txt`: the `GIT_TAG` it asks for, the commit fetched, the release tag at that commit, and how many commits the upstream default branch had after it when the check fetched it; for a dependency pinned by commit, those later commits with the files they change.
8. For each file glad generated: its size, its SHA-256, and whether the debug build's is identical; and the generator and license lines of `gl.h`.
9. The SHA-256 of `LICENSE`, and every GPLv3 text that Homebrew's installed packages ship (`COPYING*` and `LICENSE*` files under `/opt/homebrew/Cellar`), grouped by SHA-256, with a `diff` against each text that differs from `LICENSE`.
10. Removes the worktree.

The record of 2026-10-23 (`macos-2026-10-23.txt`) was made by the script as of commit `647dfbf`, which had steps 1 to 6 and 10 without the additions above, and installed `jinja2==3.1.6` alone.

Local paths in the record are replaced by placeholders: `<worktree>`, `<venv>`, `<scratch>`, `<repo>`, `<home>`, `<tmp>` (the per-user temporary directory) and `<hostname>`. The full logs of each step stayed in `<scratch>/logs` and aren't committed.

## Files

| File | Contents |
|---|---|
| `run_build_check.sh` | the script |
| `macos-2026-10-23.txt` | its output for the run of 2026-10-23 |
