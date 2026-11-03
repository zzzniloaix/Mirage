# The Conformance tab's DASH mode against the command line (2026-11-03)

The N inspector's Conformance tab with the DASH MPD mode D-084 asked for,
run at commit `a60e4bc` with its debug build (`record.txt` begins with
`cmake --build`, which had nothing to rebuild), on macOS 26.6.2, in the
form of `eval/test_runs/`: each command, then its output and exit status.
Each player window opens a local clip (a `tests/engineer_tools_snapshots.sh`
clip; nothing is played from the MPD) with `--ui-net
"tab=conformance,…,conform_out=<json>,dash=<MPD>"`, which checks the MPD
with the tab's defaults (3 segments read per video Representation, 1 per
audio one, every segment sized) and writes the tab's report as JSON. The
command line runs with the same defaults. `compare_conform.py` (its text is
in `record.txt`) compares the two reports key by key. Local paths appear as
`<scratch>` and `<repo>`.

| Run | Input | Result |
|---|---|---|
| `record.txt`, 1 | the package `tests/dashcheck_demo.sh` makes, local files | the tab's report is the command line's, key for key (`reports/cli_demo.json.xz`, `reports/tab_demo.json.xz`, `snapshots/tab_demo.png`) |
| `record.txt`, 2 | Akamai's Big Buck Bunny MPD over HTTP | not compared: the window closed (`--av-report … 70`) before the tab's check was done, and the command line's took 110.1 s this time; `snapshots/tab_akamai.png` shows the tab part-way |
| `record.txt`, 3 | the package `tests/hlscheck_demo.sh` makes, in the tab's HLS mode | the tab's report is the command line's, key for key (`reports/cli_hls.json.xz`, `reports/tab_hls.json.xz`, `snapshots/tab_hls.png`) |
| `record2.txt` | Shaka Player's "Angel One" MPD over HTTP | the tab's report is the command line's, key for key (`reports/cli_shaka.json.xz`, `reports/tab_shaka.json.xz`, `snapshots/tab_shaka.png`) |

The text reports in `record.txt` show the table's new "safe" column. The
snapshots are downscaled to half size with FFmpeg's lanczos scaler (the
full-size captures are not committed); each window's log is in `logs/`.
The JSON reports are compressed with `xz -9`.
