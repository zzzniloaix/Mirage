# Checks that need a display (2026-11-03)

The engineer-tools checks that open the player's window, run at commit
`0e448cb` (`feat/dash-check`) with its debug build (`build.txt`: `cmake
--build` had nothing to rebuild), on macOS 26.6.2, in the form of
`eval/test_runs/`: each command, then its output and exit status, with
`uptime` before each player window. Another session's builds ran on the
machine meanwhile; the load averages are in the records. Local paths appear
as `<scratch>` and `<repo>`.

| Files | What |
|---|---|
| `record.txt` | the run: `tests/engineer_tools_snapshots.sh`; the panels' cost; the Conformance tab against `mirage_hlscheck`; the MPEG-TS whose colour tags switch |
| `record2.txt` | the cost clip once more with all five panels open, for one snapshot (`snapshots/cost_open_panels.png`); FFmpeg decoding the tag-switch MPEG-TS |
| `build.txt` | the build was current |
| `snapshots/` | the windows' captures, downscaled to half size with FFmpeg's lanczos scaler (the full-size captures are not committed), and each snapshot run's log |
| `reports/` | each player run's `--av-report`, and the two conformance reports compared |
| `logs/` | each player run's log |

**The panels' cost.** `cost_1080p60.mp4` is 45 s of 1080p60 H.264 with AAC
audio, made by the `ffmpeg` command in `record.txt`. Each run plays it for
30 s (`--av-report … 30`), with the panels closed or with all five open
(`--ui-overlays PSXBL`), closed and open in turn, two of each; VideoToolbox
decoded every run. From `reports/cost_*.json` and the `uptime` before each
run:

| Run | Frames presented | Late frames dropped | Audio underruns | A/V difference p95 / max | Load averages before |
|---|---|---|---|---|---|
| `cost_closed_1` | 1808 | 0 | 0 | 3.354 / 6.667 ms | 6.14 5.31 5.61 |
| `cost_open_1` | 1810 | 1 | 0 | 3.354 / 6.646 ms | 5.30 5.20 5.55 |
| `cost_closed_2` | 1809 | 1 | 0 | 3.354 / 6.667 ms | 5.63 5.33 5.59 |
| `cost_open_2` | 1810 | 1 | 0 | 3.354 / 6.667 ms | 5.01 5.24 5.55 |

`snapshots/cost_open_panels.png` is the same command's window with a
snapshot instead of a report: P, S, B and L are on it. X draws its readout
only at the mouse or at `--ui-probe`'s point, neither of which these runs
had; while X is on, the frame on screen is mapped for reading at each new
frame all the same (the pixel probe's block in `src/main.cpp`).

A first attempt at this run was stopped part-way; nothing of it is
committed.
