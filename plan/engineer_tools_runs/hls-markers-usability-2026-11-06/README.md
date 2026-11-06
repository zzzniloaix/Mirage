# The checks of the HLS marker check's usability fixes (2026-11-06)

`record.txt` is the output of `run.sh`, in the form of `eval/test_runs/`:
each command, then its output and exit status, with `uptime` before each
window.
- It ran at commit `efed1c7` (`feat/hls-markers-usability`), whose code is
  `2eab999`'s, with the debug build, on macOS 26.6.2.
- The screen was unlocked: the ioreg line finds no lock, hence its exit
  status 1.
- Both builds were already made.
- Each window ran from the scratch folder with relative paths, so no
  snapshot shows a local path. In the record, local paths appear as
  `<scratch>` and `<repo>`.

The inputs are the package `tests/hlsmarkers_demo.sh` makes, and those
`make_inputs.py` makes. Every name, date and ID in them is made up.
`mirage_hlscheck` exits with status 1 on each package, since each has
errors to find. The folders:
- `snapshots/`: the captures at half size (FFmpeg's lanczos scaler);
- `logs/`: each window's log;
- `reports/`: the JSON and HTML reports, and the command line's text.

| Section | Run | Result |
|---|---|---|
| 1 | no media argument; `--ui-net "markers_only=1,…,hls=demo/master.m3u8"` and `--ui-snapshot` | The player exits with status 0 after its capture. The tab's JSON report is `mirage_hlscheck --markers-only`'s byte for byte (`cmp`). `snapshots/launcher_demo.png` shows the launcher's card left of the inspector, its "Check a stream without playing it (N)" link, and the Conformance tab with `break-1` open. |
| 1 | `tests/hlsmarkers_gui.sh` | PASS (`snapshots/guitest_launcher.png`). |
| 1 | the Network, Manifest and Alignment tabs on the launcher | Their empty states: no request, "Not an adaptive stream", "Not an HLS stream" (`snapshots/launcher_network.png`, `launcher_manifest.png`, `launcher_alignment.png`). |
| 1 | `enc/master.m3u8` as the media: SAMPLE-AES with a made-up `skd://` key | "avformat_open_input failed: Invalid data found when processing input". The launcher shows the error with "Check it" (`snapshots/enc_fail.png`). |
| 1 | `mpd/manifest.mpd` as the media | The open fails, and the error has "Check it" (`snapshots/mpd_fail.png`). |
| 1 | `hls=enc/master.m3u8` on the launcher, the function "Check it" calls | The tab's JSON report is the command line's byte for byte: the demo package's 1 error and 1 warning (`snapshots/launcher_enc.png`). |
| 2 | `marker_open=0` and `marker_open=1` on the names package | `break-1`, then `break-2` open (`snapshots/names_b1.png`, `names_b2.png`). |
| 3 | `break-2` at the inspector's automatic width, and at `width=900` | Automatic width: the four audio labels are cut in the middle, each keeping its own tail ("…stereo-english", "…stereo-french", "…stereo-german", "…round-english"), and every column stays in view. At 900 they are whole (`snapshots/names_b2.png`, `names_b2_wide.png`). |
| 4 | the names package, on the command line and in the tab | 2 errors (D-102): `break-1`'s tag one segment early in "…-stereo-german", and `break-2`'s tag missing in "…-surround-english". The tab's JSON report is the command line's byte for byte, and the missing row is red. |
| 4 | the page, `reports/cli_names.html` | Its script parses: `page_script.py` wraps it in a function nothing calls, and JavaScriptCore's `jsc` loads that and prints "parsed". Its data has `"missing": [4], "missing_severity": ["error"]`. |
| — | the demo master playing, with `conform=1` (the 2026-11-05 record's run) | The tab's JSON report is the command line's byte for byte (`reports/tab_playing.json`). `snapshots/tab_playing.png` shows the "Play from 3 s before the break" button. |

- Not exercised by a run:
  - a click on the launcher's link or on "Check it", and `N` on the
    launcher. Scripted runs do not click or type. "Check it" calls
    `NetInspector::check_url`, which `hls=` calls too;
  - opening the page in a browser;
  - the Alignment timeline's circle for a missing marker. The names package
    has no media to play.
- On the launcher, the Manifest and Alignment tabs say what they would say
  for a file that is not a stream. Their texts were left as they were.
- The load averages are in the record.
