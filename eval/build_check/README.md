# Build check

The record behind `docs/BUILD.md`: a committed HEAD built in a clean worktree the way `BUILD.md` describes, with the outcome of every step.

## How it was run

```
eval/build_check/run_build_check.sh <repo> <scratch> /opt/homebrew/opt/python@3.13/bin/python3.13 > <scratch>/record.txt
```

The output was copied verbatim to `macos-2026-10-23.txt`. The commit it built is in the record's "Commit" section.

What `run_build_check.sh` does, in order:
1. Records the machine, the tool versions, the installed and current Homebrew versions of ffmpeg-full, libvmaf and GLFW, and what pkg-config resolves through the presets' `PKG_CONFIG_PATH`.
2. Checks that the repository's working tree is clean, and creates a worktree of HEAD with `git worktree add --detach`.
3. Configures and builds the `macos-release` preset with the default Python, then builds only the `mirage_vmaf` target and only the `mirage_tests` target with it.
4. Creates the glad venv (Python 3.13 and `jinja2==3.1.6` from PyPI). Configures and builds the release preset with `-DPython_EXECUTABLE=<venv>/bin/python`, configures it once more without that option and shows the interpreter left in its cache, then configures and builds the debug preset with the venv as well.
5. Runs the unit tests of the debug build, excluding `[audio]`, with a 900 s alarm.
6. Lists the built programs, the libraries `mirage_vmaf` links, the first line of `mirage_vmaf --help`, the revision of every dependency CMake fetched, and whether the generated glad files of the two builds are identical.
7. Removes the worktree.

Local paths in the record are replaced by placeholders: `<worktree>`, `<venv>`, `<scratch>`, `<repo>`, `<home>`, `<tmp>` (the per-user temporary directory) and `<hostname>`. The full logs of each step stayed in `<scratch>/logs` and aren't committed.

## Files

| File | Contents |
|---|---|
| `run_build_check.sh` | the script |
| `macos-2026-10-23.txt` | its output for the run of 2026-10-23 |
