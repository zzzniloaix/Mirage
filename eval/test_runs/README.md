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
