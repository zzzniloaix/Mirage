# Unit tests of the engineer-tools branches

Records of `mirage_tests` at a commit of these branches, in the form of
`eval/test_runs/`: each command, then its output and exit status, run from
the root of a worktree of `feat/dash-check` with a clean working tree.
Local paths appear as `<repo>` (that worktree), `<tmp>` (the tests'
temporary files), `<scratch>` and `<main tree>`.

| Record | Commit | Result |
|---|---|---|
| `macos-2026-11-03-eb0f9a6.txt` | `eb0f9a689717dd3088309e4020821ebb03d3a6a2` (`feat/dash-check`, main `d9f57fb` merged) | `mirage_tests '~[audio]'`, debug build: all tests passed, 127923 assertions in 431 test cases; headless build: all tests passed, 127927 assertions in 431 test cases; macOS 26.6.2 (25G83); load averages 6.47, 10.68 and 10.01 at the start |

- Both builds were already made in the worktree, so `cmake --build` had no
  work to do. The record gives the compiler, CMake and the pkg-config
  versions of FFmpeg's libraries, libvmaf and GLFW.
- The tests tagged `[audio]` are left out, as in `docs/BUILD.md` step 4.
- The builds were configured with copies of the fetched dependencies
  (`FETCHCONTENT_SOURCE_DIR_*`) instead of fetching them.
  `deps-2026-11-03.txt` shows the settings, that each copy is identical,
  file for file, to the main working tree's FetchContent checkout, and the
  commit of each checkout.
