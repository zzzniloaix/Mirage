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
| `macos-2026-11-06-efed1c7.txt` | `efed1c740e09687f2d4bd6c79e5ebe7277dd738d` (`feat/hls-markers-usability`, from `feat/hls-markers`; its code is `2eab999`'s, and it adds the scripts of the checks of `plan/hls_markers_usability.md`) | `mirage_tests '~[audio]'`, debug build: all tests passed, 128201 assertions in 446 test cases; headless build: all tests passed, 128205 assertions in 446 test cases; `mirage_tests '[audio]'`, both builds: all tests passed, 69 assertions in 13 test cases; macOS 26.6.2 (25G83); load averages 5.64, 8.80 and 7.35 at the start |
| `macos-2026-11-06-7b647a1.txt` | `7b647a180d630bacadada24191080a37f349ec26` (`feat/hls-markers-usability`; its code is `2d08592`'s, section 3's fix 2, and it adds the scripts of fix 2's checks) | `mirage_tests '~[audio]'`, debug build: all tests passed, 128214 assertions in 447 test cases; headless build: all tests passed, 128213 assertions in 447 test cases; `mirage_tests '[audio]'`, both builds: all tests passed, 69 assertions in 13 test cases; macOS 26.6.2 (25G83); load averages 3.53, 5.15 and 5.98 at the start |
| `macos-2026-11-06-a218aad.txt` | `a218aadd0787e679b8787ae2568b93d2197df073` (`feat/hls-markers-usability`; its code is `9ca5b66`'s, section 3's fix 3, and it adds the scripts of fix 3's checks) | `mirage_tests '~[audio]'`, debug build: all tests passed, 128226 assertions in 448 test cases; headless build: all tests passed, 128231 assertions in 448 test cases; `mirage_tests '[audio]'`, both builds: all tests passed, 69 assertions in 13 test cases; macOS 26.6.2 (25G83); load averages 4.08, 5.55 and 6.76 at the start |
| `macos-2026-11-06-47b6a46.txt` | `47b6a4666ed3365c7281a90adf16d82cff858f02` (`feat/hls-markers-usability`; its code is `99b9f2f`'s, section 3's fix 4, and it adds the scripts of fix 4's checks) | `mirage_tests '~[audio]'`, debug build: all tests passed, 128226 assertions in 448 test cases; headless build: all tests passed, 128223 assertions in 448 test cases; `mirage_tests '[audio]'`, both builds: all tests passed, 69 assertions in 13 test cases; macOS 26.6.2 (25G83); load averages 7.61, 8.10 and 10.59 at the start |

- Both builds were already made in the worktree, so `cmake --build` had no
  work to do. The record gives the compiler, CMake and the pkg-config
  versions of FFmpeg's libraries, libvmaf and GLFW.
- The tests tagged `[audio]` are left out, as in `docs/BUILD.md` step 4.
  The exceptions are the four records of 2026-11-06, which run them as well.
- In `macos-2026-11-06-efed1c7.txt` and `macos-2026-11-06-a218aad.txt`,
  FFmpeg's request for a sample, logged
  by a test, names its mailing list's address. Each shows it as
  `<the ffmpeg-devel list's address>`, as it shows local paths by name. The
  test order is random, and in `macos-2026-11-06-7b647a1.txt` the request
  was not logged.
- The builds were configured with copies of the fetched dependencies
  (`FETCHCONTENT_SOURCE_DIR_*`) instead of fetching them.
  `deps-2026-11-03.txt` shows the settings, that each copy is identical,
  file for file, to the main working tree's FetchContent checkout, and the
  commit of each checkout. On 2026-11-04 the copies moved out of `/tmp`.
  `deps-2026-11-06.txt` runs the same commands on them, for the builds of
  the four records of 2026-11-06.
