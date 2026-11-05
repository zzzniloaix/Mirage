# The HLS break-marker check, the HTML report and the player's marker views (2026-11-05)

`record.txt` runs at commit `77601c9` (branch `feat/hls-markers`), on
macOS 26.6.2, in the form of `eval/test_runs/`: each command, then its
output and exit status, from the root of a worktree of the branch with a
clean working tree. Both builds were already made, so `cmake --build` had
nothing to do. Local paths appear as `<repo>` (that worktree) and
`<scratch>` (a temporary directory). The package every run reads is the one
`tests/hlsmarkers_demo.sh` makes: generated media, made-up dates and IDs.

| Run | What | Result |
|---|---|---|
| `mirage_tests '~[audio]'`, debug and headless | the unit tests, `[audio]` left out as in `docs/BUILD.md` step 4 | all tests passed, 443 test cases in each build |
| `mirage_tests '[markers],[html]'` | the marker-tag candidates and `report_html` cases | all tests passed |
| `tests/hlsmarkers_demo.sh` | the marker check of the command line on the package | 1 error: the audio playlist's cue tag one segment early (−2.005 s from the boundary nearest the break); 1 warning: v3's interstitial `START-DATE` 300 ms off a boundary; the chapter is listed, not checked |
| `mirage_hlscheck --marker-tag … --json --html` | the full check with markers, as JSON and as the HTML page | `reports/cli_playing.json.xz`, `reports/cli_playing.html` |
| player, Conformance tab, the master playing | `--ui-net tab=conformance,conform=1,marker_tags=…,conform_out=…,conform_html=…` | the tab's JSON report is the command line's byte for byte (`cmp`; `reports/tab_playing.json.xz`); `snapshots/tab_playing.png` |
| player, Conformance tab, URL mode | a local clip playing, `hls=<the package's master>`, `markers_only=1` | the tab's JSON report is `mirage_hlscheck --markers-only`'s byte for byte (`reports/tab_url.json.xz`); `snapshots/tab_url.png` |
| player, Alignment tab | `tab=alignment,scope=all,probe=1,marker_tags=…` | `snapshots/align.png`: the splice point's line at 6.000 s, the audio's flag at 4.011 s joined to it in red, v3's at 6.300 s in amber |

- The command lines compared with the tab use its defaults: 3 segments read
  per variant playlist, 1 per audio one, every segment sized.
- Not exercised by a run: the tab's seek button ("Play from 3 s before the
  break"), its Export HTML button (which writes its page with the same
  `NetInspector::export_html` that `conform_html=` ran, then opens it), and
  the "+ tag" buttons that offer candidate marker tags.
- The snapshots are downscaled to half size with FFmpeg's lanczos scaler;
  each window's log is in `logs/`. The JSON reports are compressed with
  `xz -9`.
