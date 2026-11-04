# Unit tests and demo packages of the engineer-tools branches

Records of `mirage_tests` and of the two demo packages at a commit of these
branches, in the form of
`eval/test_runs/`: each command, then its output and exit status, run from
the root of a worktree of the branch named in the row, with a clean working
tree.
Local paths appear as `<repo>` (that worktree), `<tmp>` (the tests'
temporary files), `<scratch>` and `<main tree>`.

| Record | Commit | Result |
|---|---|---|
| `macos-2026-11-03-eb0f9a6.txt` | `eb0f9a689717dd3088309e4020821ebb03d3a6a2` (`feat/dash-check`, main `d9f57fb` merged) | `mirage_tests '~[audio]'`, debug build: all tests passed, 127923 assertions in 431 test cases; headless build: all tests passed, 127927 assertions in 431 test cases; macOS 26.6.2 (25G83); load averages 6.47, 10.68 and 10.01 at the start |
| `demos-2026-11-03-44f66cb.txt` | `44f66cb04b7f3eb738f7b95c65012d60ba19ab1b` (the same code; the commits since `eb0f9a6` add documents only) | `tests/hlscheck_demo.sh` and `tests/dashcheck_demo.sh` with the headless build's checks: each report, text and JSON, is byte for byte the one committed as `plan/engineer_tools_runs/hlscheck/demo.*` (built from `61af543`) and `dashcheck/demo.*` (built from `8314657`) |
| `macos-2026-11-03-0b50883.txt` | `0b50883470bb585ecd96736e9241be61af07e7a2` (`feat/engineer-tools-followups`; its code is `a60e4bc`'s, the commits after it change documents only) | `mirage_tests '~[audio]'`, debug build: all tests passed, 127927 assertions in 431 test cases; headless build: all tests passed, 127929 assertions in 431 test cases; macOS 26.6.2 (25G83); load averages 5.71, 4.82 and 4.83 at the start |
| `macos-2026-11-04-cfd6ae0.txt` | `cfd6ae0beabf9a2470840f5ecd942b2ec008042f` (`feat/player-fixes`, the four player fixes merged; the commits after it change documents only) | `mirage_tests '~[audio]'`, debug build: all tests passed, 135745 assertions in 464 test cases; headless build: all tests passed, 135575 assertions in 463 test cases (that build leaves out the VideoToolbox case of `tests/test_decoder.cpp`); `mirage_tests '[audio]'`, both builds: all tests passed, 1039 assertions in 21 test cases; macOS 26.6.2 (25G83); load averages 4.72, 5.36 and 5.91 at the start |

- Both builds were already made in the worktree, so `cmake --build` had no
  work to do. The record gives the compiler, CMake and the pkg-config
  versions of FFmpeg's libraries, libvmaf and GLFW.
- The tests tagged `[audio]` are left out, as in `docs/BUILD.md` step 4.
  The exception is `macos-2026-11-04-cfd6ae0.txt`, which runs them as well,
  because the player fixes' downmix and W strip tests carry that tag. Those
  tests use the audio player's ring without opening the audio device.
- The builds were configured with copies of the fetched dependencies
  (`FETCHCONTENT_SOURCE_DIR_*`) instead of fetching them.
  `deps-2026-11-03.txt` shows the settings, that each copy is identical,
  file for file, to the main working tree's FetchContent checkout, and the
  commit of each checkout. The copies were made again on 2026-11-04 from
  the same checkouts. `deps-2026-11-04.txt` runs the same commands on them,
  for the builds of `macos-2026-11-04-cfd6ae0.txt`.
