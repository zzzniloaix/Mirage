# eval/timeline/ui-qa-2026-11-16/

The timeline panel's scripted QA run (`eval/phase3-survey/ui.md` §5.3) and the player's GUI regressions outside ctest, for work package WP13, the panel of `eval/phase3-survey/design.md` §10 that the owner approved under D-007 (`docs/DECISIONS.md` D-114). Made on 2026-11-16 on macOS 26.6.2, after the package's review, from the working tree of the commit that brings this record up to date: HEAD `b8c7e51`, the package's first commit, with the review's fixes that `environment.txt` lists, each changed source file's `git hash-object` there being its blob in the fixes' commit, which is folded into the package's, so that the programs ran the committed code. It replaces the package's first record, made on 2026-11-13 before the fixes. In the form of `eval/timeline/run-f7d5ed8/` (P-AF): every command with its exit status in `cmd.txt`, the environment in `environment.txt`, HEAD and `git status` at the start and the end of the run among it, the local directories replaced by placeholders and then searched for (`redact.txt`, `search.txt`), as P-011 has it. Other packages' builds ran on the machine meanwhile; `environment.txt` gives the load averages at the start.

## How it was made

```
sh eval/timeline/ui-qa-2026-11-16/regressions.sh build/macos-debug <before> <out2> engineer
sh eval/timeline/ui-qa-2026-11-16/regressions.sh build/macos-debug <before> <out2> loudness
sh eval/timeline/ui-qa-2026-11-16/regressions.sh build/macos-debug <before> <out2> pathb
sh eval/timeline/ui-qa-2026-11-16/run.sh build/macos-debug <before> <out>
```

then `eval/timeline/check.py redact` over `<out2>`, as `regressions/cmd.txt` ends.

- `build/macos-debug`: the debug build of this checkout, current: `build.txt`, taken after the run with the sources unchanged, gives `ninja -n` with no work to do and the player's SHA-256, `environment.txt`'s. The regressions ran before the QA run, with a debug build of the same code but for two comments reworded since, in `src/TimelinePanel.cpp` on a switch inside an ad and in `src/main.cpp` on the document's setting; a debug build here isn't the same byte for byte from one build to the next, so its player's SHA-256 isn't `environment.txt`'s. A run of them with the final build, not recorded, opened its windows on displays of two scales, the machine's own and an external one, as macOS placed them, which a comparison of pixels can't take.
- `<before>`: the player and `snapshot_diff` of a debug build of `83beea8`, the commit before the package, unchanged, whose GL tag ruler the change retires (D-114 (e)).
- `<out>`, `<out2>`: new directories outside the repository; this directory holds what `run.sh` wrote in `<out>` but the media segments (`.ts`, which `.gitignore` leaves out) and the snapshots' empty FFmpeg logs (empty outputs aren't kept, P-011), with the snapshots cut down as below; `regressions/` is `<out2>`'s.
- Every player window: `--qa-no-focus`, `--window 1280x720` (1600x1000 in `tests/engineer_tools_snapshots.sh`, which passes its own, and in the loudness part, which runs its case), a HOME of its own outside the repository (the player keeps its recent list there; a snapshot run neither reads nor writes `prefs.txt`), under `caffeinate -i` and a perl alarm of 60 s, as `eval/baseline/run_baseline.sh` starts the player; each in the foreground, ended by its last snapshot. D-069's ladders carry no audio, and neither does `tests/pathb_snapshot_regression.sh`'s clip, so the video follows its own clock and the screen's lock, which `environment.txt` records by `ioreg`'s line (none: not locked), doesn't hold it back. The engineer-tools windows play audio: `regressions.sh` starts their two parts only on an unlocked screen, and the loudness cards of the engineer part's snapshots (`regressions/engineer-differ/`) show the clips' levels measured. The VideoToolbox service count is the same before and after the windows (`environment.txt`).

`run.sh`'s head says what each step does; `check_ui.py`'s what it checks; `regressions.sh`'s how the regressions were run with each player.

## Files

| Path | What it holds |
|---|---|
| `run.sh`, `check_ui.py`, `regressions.sh` | the scripts that made and checked this record |
| `cmd.txt`, `environment.txt` | the QA run's commands, from `<out>`, with their exit statuses; its environment, at the start and the end |
| `build.txt` | the build the run used, current |
| `tests/mirage_tests.txt`, `tests/mirage_tests_example.txt` | the acceptance test case of §3.2's two tests (`tests/test_attribution_acceptance.cpp`) and the composite example's case (test 3, `tests/test_attribution_session.cpp`), run with `MIRAGE_KEEP_MEDIA=<out>/media` (P-U): all passed |
| `media/` | what those cases kept: D-069's four ladders (degraded; the ad from the main content's origin, from 1 s, continuing), their playlists and `layout.json`, their ladder reports and the test's own documents and answers; and the composite example's ladder with its session's report, plan, document and answer |
| `<ladder>/timeline.json`, `timeline.log` | the document `mirage_vmaf timeline` built from the ladder's report with T = 6, W = 50, N = 12, as `eval/timeline/run_all.sh` pins them (D-077): the panel's input |
| `<ladder>/run1.txt`, `run1.log`, `run2.txt`, `run2.log`; `ad-main-origin/run3.txt`, `run3.log`; `session/run1.txt`, `run1.log` | each run's panel report (`--timeline-report`) and the player's log |
| `<run>.attribute.txt` | `mirage_vmaf attribute`'s answer to the query of the run's card: the document's drop it names (`--drop`, counted along its curve), or the frames of its interval (`--frames`) |
| `extras/` | the logs of six runs more; `drag.txt`, the panel's report on a pick made while the seek bar was dragged; `ruler-after.txt`, its report on a variant's playlist opened alone |
| `snapshots/` | each run's snapshot (`--speed-seq snap`, or `--ui-snapshot`'s frame count), downscaled to half size with FFmpeg's lanczos scaler, its lower half alone, the panel, the seek bar and the controls, but for `extras-overlays.png` and `extras-help.png`, whole; the full-size captures aren't committed |
| `checks.txt` | `check_ui.py`'s checks: 113 of 113 passed |
| `redact.txt`, `search.txt` | the replacement of the local directories, then the last search: nothing local left |
| `regressions/` | the GUI regressions outside ctest, with the player before and after: `cmd.txt`; each run's output and the logs of its windows; `compare.txt`, their snapshots compared; `engineer-differ/`, half-size copies of the snapshots that differ; `loudness.txt` and `loudness-counters/`, the loudness part; and `redact.txt` and `search.txt`, as above |

In every file, `<out>`, `<build>`, `<repo>`, `<home>` and `<tmp>` stand for the local directories, and `<before>` for the directory of the player before the change.

## The runs

Two runs a ladder, from the ladder's `master.m3u8`, with `--timeline <ladder>/timeline.json`:

- **Run 1**: `--ui-timeline "fit=1,variants=all,pick=attribution:0@3" --speed-seq "pause@2,snap@8"`: the whole axis, every curve, and at 3 s after the first frame, paused since 2 s, a pick of the lowest point of the document's attribution 0, its detected drop, on the variant the player starts on, the top rung, which is the reference in top-rung mode.
- **Run 2**: `--qa-switch v<height>@0.5 --ui-timeline "zoom=2:6,variants=<entry>,pick=<position>@3.5" --speed-seq "pause@2.5,snap@8"`: switched at 0.5 s to the rung whose curve holds the drop, so that the playhead crosses a variant switch, the view zoomed to 2-6 s, that rung's curve alone, and at 3.5 s, paused, a pick at a position, the middle of a frame's span.
- Each writes the panel's report at 7 s after launch (`--timeline-report`), before its last snapshot, at 8 s after the first frame.

And on the ad from the main content's origin, a third:

- **Run 3**: `--qa-switch v180@3.3 --ui-timeline "fit=1,variants=all,pick=attribution:0@2.5" --speed-seq "pause@5,snap@11"`, the report at 10 s after launch: the drop picked at 2.5 s on the top rung, the decision an offer; at 3.3 s, inside the ad, whose timestamps restart at the main content's origin, the switch to the drop's rung, which the Demuxer repositions by PTS into the main content; paused at 5 s, after the switch's first frame and before the ad plays again.

And on the composite example's ladder, with the session's document:

- **session/run1**: `--ui-timeline "fit=1,variants=all,pick=attribution:1@3" --speed-seq "pause@2,snap@8"`: the session's drop, the document's attribution 1, picked as run 1 picks.

What the reports and the logs give:

| Run | On screen | Picked | Card | Seek | Log |
|---|---|---|---|---|---|
| `degraded/run1` | entry 0, the reference | `pair:0` (entry 1) at reference frame 107, VMAF 38.443 | attribution 0: the drop over frames 100-124, segment 4, `seg_004.ts` | to PTS 14.280 s, sent and made | landed at 14.280 |
| `degraded/run2` | entry 1, switched | frame 124, the degraded segment's last, VMAF 38.769 | attribution 0 | not reachable: with the first DTS a frame before the start the demuxer finds segment 5; segment 4's start, 14.000 s, offered | no seek |
| `ad-main-origin/run1` | entry 0 | `pair:1` (entry 2) at frame 84, in the ad, sequence 1, PTS 10.360 s, VMAF 22.237 | attribution 0: the drop over frames 75-124, the ad's cue-out, its discontinuity, its segments and keyframes | not reachable: its PTS shows the main content's frame of that PTS in segment 0; segment 2's start, 12.000 s, offered (D-114 (b)) | no seek |
| `ad-main-origin/run2` | entry 2, switched | frame 130, after the ad, PTS 15.200 s | the query of its segment, 5: its discontinuity, the cue-in, the segment, the keyframe (D-114 (d)) | to PTS 15.200 s, sent and made | landed at 15.200 |
| `ad-main-origin/run3` | entry 2, switched inside the ad | frame 84, as in run 1 | attribution 0 | the offer made again for entry 2 once it was on screen: segment 2's start, 12.000 s, not taken | no seek |
| `ad-from-1s/run1` | entry 0 | `pair:1` at frame 84, PTS 1.360 s | attribution 0 | not reachable: its PTS is before the stream's start, 10 s; segment 2's start, 12.000 s, offered | no seek |
| `ad-from-1s/run2` | entry 2, switched | frame 130, sequence 2, PTS 15.200 s | the query of segment 5 | to PTS 15.200 s, sent and made | landed at 15.200 |
| `ad-continuous/run1` | entry 0 | `pair:1` at frame 84, PTS 13.360 s | attribution 0 | to PTS 13.360 s, sent and made | landed at 13.360 |
| `ad-continuous/run2` | entry 2, switched | frame 74, segment 2's last, PTS 12.960 s | the query of segment 2 | not reachable: the demuxer finds segment 3; segment 2's start, 12.000 s, offered | no seek |
| `session/run1` | entry 0 | `session:0` at frame 84, PTS 10.360 s, VMAF 22.237 | attribution 1: the ad's segment requests, the first 1.007 s from request to last byte, with their times, bytes and statuses, the session's switch from entry 1 to entry 2 and its span, by their rungs, and the ad's cue-out, discontinuity, segments and keyframes | not reachable, as in `ad-main-origin/run1`; segment 2's start, 12.000 s, offered | no seek |

The offers of a segment's own start for its last frame (`degraded/run2`, `ad-continuous/run2`) are WP12's extension of D-114 (b) beyond ads, which Q88 asks the owner about. In each run the document matched the open master by its SHA-256, its entries in order, and the playhead's frame is the frame on screen's to the microsecond: the player's PTS through the master equal `mirage_vmaf`'s through the media playlists for the frames these runs show (the design's R16, on D-069's MPEG-TS ladders). In run 2 the playhead followed the switch to the rung's own clock. In run 3 the switch was asked for at PTS 10.280, inside the ad, and repositioned to 10.530, which the demuxer finds in segment 0: the playhead is at reference frame 40, the main content's frame of PTS 11.600, the frame on screen, in sequence 0, not at the ad's frame of that PTS; the snapshot shows it so. Every card's lines, from its interval's head on, are those of `mirage_vmaf attribute`'s answer to the same query (`<run>.attribute.txt`), 5 to 23 lines a card. Every seek the panel sent landed on the picked frame, and every frame it offered instead got no seek: `checks.txt`.

The six runs more, from the ad ladders:

- `extras/keys`: `--speed-seq "panel:E@1,panel:E@2,esc@3,snap@4"`: E closes and opens the panel, then Esc closes it, the panel opened last (`key: Esc — closing the timeline` in `keys.log`); the snapshot has no panel.
- `extras/overlays`, `extras/help`: `--ui-menu overlays`, `--ui-menu help`: the overlays popover with its Timeline row (E), on, and the help card with its row "E Timeline panel".
- `extras/drag`: on the ad with continuous timestamps, `--ui-timeline "variants=all,pick=attribution:0@3" --speed-seq "pause@2,scrub:6@2.5,release@4,snap@8"`: the drop's frame 84, which a seek to 13.360 s reaches, picked while a seek-bar drag, from 2.5 s to its release at 4 s, holds the seeks: the panel asks for none, and its card says so (`drag.txt`); the release's seek lands at 16.000, and none at the picked frame.
- `extras/ruler-before`, `extras/ruler-after`: the low rung's media playlist opened alone, by the URL the report gives it, with the network sidebar open on its Manifest tab (`--ui-net tab=manifest`): ManifestParser then reads the playlist's own tags, where a master has none of its segments'. The player before the change draws its GL tag ruler, a dark strip with a tick at each tag, over the picture; this one draws none, and its panel matches the playlist as its variant, entry 2, the variant on screen (`ruler-after.txt`).

## The regressions

`regressions/compare.txt` compares the snapshots of the two players by their decoded pixels (FFmpeg's md5 muxer), and where two differ, gives the box that holds every pixel that differs, in window pixels.

- `tests/pathb_snapshot_regression.sh`, the HW-vs-SW check: with either player the HW and SW snapshots agree within its budget (`pixels>20=0 (budget 100)` in `pathb-before.txt` and `pathb-after.txt`), and each player's HW snapshot and SW snapshot have the same pixels as the other's.
- `tests/engineer_tools_snapshots.sh`, twice with the player before and once with this one: 8 of its 10 snapshots have the same pixels in all three runs: the frame inspector's three, the scopes' four and the frame sizes'. The two loudness meters' cases differ, as their runs differ from each other:
  - `loudness_aac.png` differs between the two runs of the player before as well as from this one's, each time inside the loudness card alone (x 2420-3131, y 1261-1553 at the most): its trace, its meters' levels and the time from which it integrates, 0.3 s in the two runs before and 0.4 s in this one's (`engineer-differ/`).
  - `loudness_51.png`: the first run of the player before and this one's show the source's frame 419 (testsrc2's counter, 00:00:16.760) and differ inside the loudness card alone (x 2633-2968, y 1261-1373); the second run of the player before shows frame 418 (00:00:16.720), so the picture differs throughout (x 0-3199, y 100-1944). The snapshot is taken at the render loop's 420th iteration, a count of iterations, which the loop makes as frames fall due and as events come, so on a clip played on its audio's clock the frame then on screen follows the run's timing. The loudness part shows it (`loudness.txt`): the case again at the 419th, 420th and 421st iteration, twice each with each player, gave frame 418 at the 419th in all four runs, frame 419 at the 420th in both runs of the player before and in one of this one's, frame 418 in the other, and frame 420 at the 421st in all four; and for each frame, the picture outside the loudness card and the top bar's drop counter has the same pixels with either player, in every snapshot of it, the engineer part's three included. The counters' pixels are `loudness-counters/<MD5>.png`.
  - None of the ten opens the network sidebar, the timeline panel, the help card or the menus whose rows the change adds, so none shows the tag ruler or anything else the change draws.

## Not checked here

`eval/phase3-survey/ui.md` §5.4's manual checks need a person, since the QA flags inject no mouse input over the panel: the panel hovered while the chrome fades; a pan whose release is over the seek bar; Space and Esc after a click on the panel; the wheel over the panel and over the volume control; a 1x display beside a Retina one. So do a click followed, within the double-click time, by E, Esc, the × or a wheel zoom, which picks nothing; the card's button that takes an offer; the plot's tooltips; resizing the panel; Open…; and an axis without seconds, which D-069's ladders, timed on their playlist time, don't have.
