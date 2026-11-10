# The checks still pending after the first GUI record (2026-11-03)

What `plan/engineer_tools_runs/gui-2026-11-03/` left MEASUREMENT PENDING,
on clips generated in the run instead of the files of the first checks,
which the repository does not hold. Run at commit `d36045d` with its debug
build (`record.txt` begins with `cmake --build`, which had nothing to
rebuild), on macOS 26.6.2, in the form of `eval/test_runs/`: each command,
then its output and exit status, with `uptime` before each player window.
The clips' commands are in `record.txt`; the clips are not committed. The
AV1 encode is piped through `grep -v` to drop SVT-AV1's info lines, so its
exit status 1 is grep's, finding nothing else to print; the clip was made
(`ffprobe` after it, and the windows that open it). Local paths appear as
`<scratch>` and `<repo>`.

| Window | Clip | What it shows |
|---|---|---|
| `p_nohw` | `x264_sdr.mp4` (the snapshot script's), `--no-hw`, 2 s in | P with the software decoder: yuv420p, frame 50 at 2.000000 s, I, key, as the burnt-in counter |
| `sx_av1` | `av1_4k_pq.mp4`: 2 s of 3840×2160 testsrc2, 10-bit, PQ / BT.2020 in the bitstream and the container, SVT-AV1 | S's waveform with the nits scale, and X on the red bar (code values, R′G′B′, PQ luminance), decoded by VideoToolbox |
| `p_av1` | the same | P: VideoToolbox p010le, bt2020nc / smpte2084 / bt2020, and the two HDR10 findings for PQ without ST 2086 or MaxCLL |
| `bp_long` | `long_1080p.mp4`: 300 s of 1080p25 H.264, GOP 50, 150 s in | B (7500 frames, its view at the frame on screen) and P (frame 3750, 150.000000 s, as the burnt-in counter) |
| `b_raw` | `raw.264`: a 10 s H.264 elementary stream | B without timestamps: "times from the frame rate", the GOP durations and bit rates from them |
| `tab_apple` | Apple's fMP4 bipbop example over HTTP, playing | the Conformance tab's report against `mirage_hlscheck`'s (`compare_conform.py` in `record.txt`; both reports in `reports/`): the same, key for key |

The snapshots are downscaled to half size with FFmpeg's lanczos scaler
(the full-size captures are not committed); each window's log is in
`logs/`. The JSON reports are compressed with `xz -9`.
