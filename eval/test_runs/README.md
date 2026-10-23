# Test runs

Records of the unit test suites run at a commit, for the PR descriptions that quote their output (hard rule 5 of the work order; `docs/DECISIONS.md` P-028). Each record gives each command, then its output and exit status, run from the root of the main working tree with a clean working tree:

- the commit, the working tree's state, and the changes to the C++ sources and CMake files since `6a6661f`, the commit of the last build check (`eval/build_check/macos-2026-10-23-6a6661f.txt`);
- `cmake --build --preset macos-debug`, which rebuilds whatever the tests need;
- whether the gitignored fixture of the subtitle test is present: it is in the main working tree, not in a clean checkout (`docs/BUILD.md`, step 4);
- `mirage_tests` without the `[audio]` tests, in the form of `docs/BUILD.md` step 4, and a second run only if the first fails;
- the four Python suites of `eval/` (`eval/README.md`, "Tests");
- the working tree's state afterwards.

Local paths appear as `<repo>`, `<home>`, `<tmp>` and `<scratch>`.

| Record | Commit | Result |
|---|---|---|
| `macos-2026-10-23-8aca01a.txt` | `8aca01a9d6aaa38437e30bfdb1cf889de55d4998` | `mirage_tests '~[audio]'`: all tests passed, 90726 assertions in 240 test cases, at the first run; the Python suites: 19, 77, 7 and 16 tests, all OK |

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

So the test fails intermittently at the commit Phase 1 started from as well. The runs don't show whether the test's timing or the track switch is at fault (`docs/PHASE1_DECISIONS_PENDING.md`, "Smaller items found in Phase 1").
