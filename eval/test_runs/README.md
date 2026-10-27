# Test runs

Records of the unit test suites run at a commit, for the PR descriptions that quote their output (hard rule 5 of the work order; `docs/DECISIONS.md` P-028, P-032, P-060, P-065, P-066, P-073, P-074). Each record gives each command, then its output and exit status, run from the root of the main working tree with a clean working tree, and on `vmaf/phase-2` from the root of a worktree of that branch:

- the commit, the working tree's state, and the changes to the C++ sources and CMake files since the commit of the last build check: `6a6661f` (`eval/build_check/macos-2026-10-23-6a6661f.txt`) in the records of 2026-10-23, `5ae44a4` (`eval/build_check/macos-2026-10-24-5ae44a4.txt`) in the records of 2026-10-24 and later, which also list the changes to the Python and shell scripts since then, and on `vmaf/phase-2` the changes since `vmaf/phase-1` as merged (`dc3af0d`);
- `cmake --build --preset macos-debug`, which rebuilds whatever the tests need;
- in the records of 2026-10-23, whether the gitignored fixture of the subtitle test is present: it is in the main working tree, not in a clean checkout (`docs/BUILD.md`, step 4); since D-032 the test writes its fixture itself (P-036), and the records of 2026-10-24 and 2026-10-26 show that no source or test names the fixture's directory;
- `mirage_tests` without the `[audio]` tests, in the form of `docs/BUILD.md` step 4, and a second run only if the first fails;
- the four Python suites of `eval/` (`eval/README.md`, "Tests"), and on `vmaf/phase-2` a fifth, `eval/ladder/tests`, in the records of 2026-10-24 and later with `TMPDIR` in a scratch directory outside the repository;
- the working tree's state afterwards.

Local paths appear as `<repo>`, `<home>`, `<tmp>` and `<scratch>`.

| Record | Commit | Result |
|---|---|---|
| `macos-2026-10-23-8aca01a.txt` | `8aca01a9d6aaa38437e30bfdb1cf889de55d4998` | `mirage_tests '~[audio]'`: all tests passed, 90726 assertions in 240 test cases, at the first run; the Python suites: 19, 77, 7 and 16 tests, all OK |
| `macos-2026-10-23-9855319.txt` | `985531978d4f300e3638a0937aa5acb84e6fbb05` | `mirage_tests '~[audio]'`: all tests passed, 90866 assertions in 244 test cases, at the first run; the Python suites: 20, 85, 7 and 16 tests, all OK (P-032) |
| `macos-2026-10-24-12715a6.txt` | `12715a65f6c05143459d2f306ed21ca029624009` | `mirage_tests '~[audio]'`: all tests passed, 111752 assertions in 311 test cases, at the first run; the Python suites: 27, 132, 11 and 16 tests, all OK (P-060) |
| `macos-2026-10-26-a4692a2.txt` | `a4692a204313fae5c6e6c472029eb5db75b80740` | `mirage_tests '~[audio]'`: all tests passed, 112039 assertions in 312 test cases, at the first run; the Python suites: 27, 133, 11 and 16 tests, all OK (P-065) |
| `macos-2026-10-26-bbc669d.txt` | `bbc669dfeae9febd6f609f5f1e7b4f1489a57182` | `mirage_tests '~[audio]'`: all tests passed, 112082 assertions in 317 test cases, at the first run; the Python suites: 28, 136, 11 and 16 tests, all OK (P-066) |
| `macos-2026-10-27-1db7b89.txt` | `1db7b89d3cd34fb8f1db97ee60bb56083d3d6a16` (`vmaf/phase-2`) | `mirage_tests '~[audio]'`: all tests passed, 112439 assertions in 325 test cases, at the first run; the Python suites: 28, 136, 11, 16 and 8 tests, all OK (P-073) |
| `macos-2026-10-27-57b5ea5.txt` | `57b5ea555a6cacc29533fcb5a8e8c791cb0adf6c` (`vmaf/phase-2`) | `mirage_tests '~[audio]'`: all tests passed, 112445 assertions in 325 test cases, at the first run; the Python suites: 28, 136, 11, 16 and 8 tests, all OK (P-074) |

## Repeated runs of one test (P-031)

`Demuxer: seamless video and audio switches on a local file` (`tests/test_demuxer_switch.cpp`) fails now and then. Two records run it alone, again and again, in `mirage_tests` builds of `37472fd`, the first commit of Phase 1, whose code is `main`'s, and of `855a71b`, after which no commit of the branch changes C++ code. Each build was made in a new worktree of its commit, debug preset, and the two builds alternate, one run at a time:

| Record | Runs of each build | Failed at `37472fd` | Failed at `855a71b` |
|---|---|---|---|
| `demuxer-switch-150-2026-10-23.txt` | 150 | 0 | 1 |
| `demuxer-switch-500-2026-10-23.txt` | 500 | 5 | 3 |

- Every failed run failed one assertion, at line 441 (`CHECK( v[i].pts < 3.0 )`) or line 491 (`CHECK( a[i].pts < apos + 0.5 )`) of the test, and both lines failed at both commits.
- The test and `src/Demuxer.*` are the same at both commits (each record's `git diff --stat`), and every build fetched the same miniaudio commit.
- Each of the 1300 runs took from 0.96 to 1.11 s of wall time (the records' last sections).
- The second series built both commits anew, in other worktrees; the SHA-256 of each build differs between the two series.

So the test fails intermittently at the commit Phase 1 started from as well. The runs don't show whether the test's timing or the track switch is at fault (`docs/PHASE1_DECISIONS_PENDING.md`, "Smaller items found in Phase 1"); `docs/plans/demuxer-switch-test.md` is the plan D-032 asked for to find out, for the owner's approval.
