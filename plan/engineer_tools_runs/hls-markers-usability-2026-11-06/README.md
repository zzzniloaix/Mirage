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

## Record 2: section 3, fix 2

`record2.txt` is the output of `run2.sh`, in the same form. It ran at
commit `7b647a1`, whose code is `2d08592`'s, with the debug build and the
screen unlocked (the ioreg line's exit status 1). Its captures are in
`snapshots/record2/`, its logs in `logs/record2/` and its reports in
`reports/record2/`. `make_inputs.py` also makes the tails package. The
packages of `run.sh` come out as they did.

| Run | Result |
|---|---|
| the tails package (audio NAMEs that differ in their first token only), `marker_open=0` and `1` | The rows read "audio aud/aa…", "audio aud/bb…", "audio aud/cc…", "audio aud/dd…" (`snapshots/record2/tails_b1.png`, `tails_b2.png`). The tab's JSON report is the command line's byte for byte. |
| the names package (NAMEs that share their head), `break-2` | The rows read "audio aud/…stereo-english", "…stereo-french", "…stereo-german", "…surround-english" (`snapshots/record2/names_b2.png`). The tab's JSON report is the command line's byte for byte. |
| `tests/hlsmarkers_gui.sh` | PASS |

Fix 2 did not tell apart the renditions of the owner's packages: one group
there mixes three codec families with their own tails, so its labels share
no end. Fix 3 replaced it.

## Record 3: section 3, fix 3

`record3.txt` is the output of `run3.sh`, in the same form. It ran at
commit `a218aad`, whose code is `9ca5b66`'s, with the debug build and the
screen unlocked. Its captures are in `snapshots/record3/`, its logs in
`logs/record3/` and its reports in `reports/record3/`. `make_inputs.py`
also makes the mixed package, and the earlier packages come out as they
did.

| Run | Result |
|---|---|
| the mixed package, `marker_open=0` and `1` | Each card logs its 31 rows as it shows them at the inspector's automatic width, and no two are alike (`sort | uniq -d` counts 0). One language's rows read "aa CODA L51 1 256K", "aa CODB STEREO 128K" and "aa CODC L71 1 1000K". The widest is cut in the middle ("ee-419 COD…STEREO 128K"), and a bar in the kind's colour starts each row (`snapshots/record3/mixed_b1.png`, `mixed_b2.png`). The two errors are `break-1` one segment early in "dd … CODB" and `break-2` missing in "gg-HH … CODC". The tab's JSON report is the command line's byte for byte. |
| the tails and names packages, `break-2` | The rows read "aa", "bb", "cc", "dd", then "stereo-english", "stereo-french", "stereo-german" and "surround-english" (`logs/record3/tails_b2.log`, `names_b2.log`). |
| `tests/hlsmarkers_gui.sh` | PASS |
