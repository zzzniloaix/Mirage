# The checks of the player fixes' section 5 (2026-11-05)

`record.txt` is the output of `run.sh`, in the form of `eval/test_runs/`:
each command, then its output and exit status, with `uptime` before each
window. It ran at commit `380b5fd` (`feat/player-fixes`) with the debug
build, on macOS 26.6.2, with the screen unlocked; the ioreg line finds no
lock, hence its exit status 1. Local paths appear as `<scratch>` and
`<repo>`. `snapshots/` has the captures at half size (FFmpeg's lanczos
scaler) and `logs/` each player run's log.

Other work on the machine pushed the load averages past 100 from the fifth
window on, and between the sixth window and the seventh, 46 minutes passed
(9:37 to 10:23). Every window exited with status 0.

- 5.1, the scrub preview and the step back (`ScrubDecoder`), in the
  tag-switch MPEG-TS, whose keyframes are at PTS 1.48 and 4.48:
  - to PTS 2.68 the preview is 2.680, and to PTS 5.68 it is 5.680;
  - to the end it is 7.440, the last frame, for 7.480;
  - the step back from 3.080 lands on 3.040, the frame before.

  `plan/engineer_tools_runs/fixes-gui-2026-11-05/record2.txt` had, before
  the fix, 4.480 and none for the two scrubs and 4.480 for the step back.
  In `h264.mp4`, to 3.2, the preview is 3.200, as before.
- 5.2, bar 2's luma codes over every row of each clip's first frame
  (`bar2_codes.py`), decoded in software and with VideoToolbox:
  - 8-bit H.264 4:2:2 and HEVC 4:4:4: 5376 samples of 127 and 1792 of 128,
    the same both ways;
  - 8-bit HEVC 4:2:0: 126 throughout;
  - 10-bit HEVC 4:2:2: 509 throughout.
- 5.3: `noise_I.png` shows the whole Decode line, and `pq_I.png` shows the
  Dynamic line as "HDR (PQ -> tonemap)", in a panel as wide as its widest
  line.
- 5.4: `evc_badges.png`, after the switch to the EVC track, shows the
  badges "EVC", "640×360", "30", "SDR" and "no decoder".
