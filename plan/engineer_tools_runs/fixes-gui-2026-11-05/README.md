# The player fixes' scripted checks (2026-11-05)

These are the checks of `plan/engineer_tools_player_fixes.md` (each
section's Check) that open the player's window. They ran with the debug
build of `feat/player-fixes`, whose code is `66751ed`'s, on macOS 26.6.2:
`record.txt` and `record2.txt` at commit `3d2b8cc`, `record3.txt` at
`6f5ec80`. The commits in between add records and documents only. The
records are in the form of `eval/test_runs/`: each command, then its output
and exit status, with `uptime` before each window. Another session's builds
ran on the machine meanwhile; the load averages are in the records. Local
paths appear as `<scratch>` and `<repo>`.

The screen was locked during the first two runs; `record2.txt` shows it.
CoreAudio then plays nothing, so in a clip with audio the video, which
follows the audio clock, hardly moves. `record3.txt` makes again, with the
screen unlocked, the runs that need audio output: a control clip, 4.2 and
4.4, and the timed seek of section 1. `run3.sh` refuses to run while the
screen is locked. The locked runs' files are kept, and `record3/` holds the
third run's.

| Files | What |
|---|---|
| `record.txt` | `run.sh`: the clips it makes, then each section's runs |
| `record2.txt` | `run2.sh`: the ten `--no-hw` runs of the bars that `run.sh` skipped, and their comparisons with its HW captures; the frame steps, one capture per run; the scrub preview |
| `record3.txt` | `run3.sh`: the runs that need audio output, the screen unlocked |
| `run.sh`, `run2.sh`, `run3.sh` | the scripts (`SP`: the scratch folder). `record.txt` was made before `player()` stopped setting `n`: it overwrote the bars loop's own `n`, so each second call named a log in a folder that does not exist, and the shell ran nothing (exit status 1) |
| `tools/` | `make_ac4_mov.c`, `make_evc_mov.c`: copy a clip and add an AC-4 audio or an EVC video stream of made-up packets, for which this FFmpeg has no decoder |
| `snapshots/` | the captures at half size (FFmpeg's lanczos scaler). The full-size captures, which `bars_at.py` and `snapshot_diff` read, stay in the scratch folder |
| `reports/`, `logs/` | each `--av-report`, and each player run's log |

## Section 1: the seek (D-088, D-093)

- `tagswitch.ts` at position 3.72, PTS 5.2, which showed no frame on
  2026-11-03. One try goes back, to 4.2. With VideoToolbox and with
  `--no-hw` there is no watchdog message and no decode error
  (`reports/seek_*.json`). `seek_shot.png` shows the frame, its burnt-in
  time 00:00:03.720.
- `nosei.ts`: 60 s of H.264 with open GOPs and every SEI taken out, with
  three references. FFmpeg flags one keyframe in it. A seek to position 50
  goes back to the start (seven tries) and shows the target's frame, PTS
  51.480 (testsrc's count 50 in `seek_nosei_*.png`). With the screen
  unlocked (`record3.txt`), the first frame after the seek came 1233 ms
  after it with VideoToolbox and 135 ms after it with `--no-hw`. With the
  screen locked it took 3592 ms and 311 ms.

## Section 2: positions (D-089, D-094)

- `end_bar.png`: the end of `tagswitch.ts` reads 00:05.960 / 00:06.000. It
  read 00:07.040 / 00:06.000 on 2026-11-03.
- `end_bar_av.png`: the last frame of `av_lead.ts` reads 00:05.960 /
  00:06.000, with the bar full. Its audio starts 0.541333 s before its
  video: ffprobe gives the video's start as 1.941333 s, the audio's and the
  container's as 1.4 s, and the container's duration as 6.541333 s, which
  was the duration shown before D-094.
- Frame steps (`record2.txt`): the step forward lands at PTS 2.520, the
  next frame (`steps_fwd.png`). The step back, from 3.080 and asking for
  3.020, lands at 4.480, the next keyframe (`steps_back.png`: the frame
  inspector's #75). In `record.txt` the first capture of `steps`, at
  3200×2000, held the render loop until 6.48 s. The step back and the
  second capture then came in one frame, before the step landed.
- The scrub preview in `tagswitch.ts`, whose keyframes are at PTS 1.48 and
  4.48 (`record2.txt`):
  - to PTS 2.68, the preview is 4.480;
  - to PTS 5.68, there is none;
  - to the end (`record.txt`), there is none, and the frame shown is
    playback's last.

  In `h264.mp4`, to 3.2, the preview is 3.200.

## Section 3: colour (D-090)

- With VideoToolbox (`record.txt`):
  - 10-bit SDR white is 255; it was 254 in the replica,
    `investigate-2026-11-03/render/results.txt` [C2].
  - The 8-bit 4:2:0 greys are neutral, 128 and 191.
  - 'x420', 'x422', 'x444' and 'sv44' are bound as R16 / RG16, and '420v',
    '422v' and '444v' as R8 / RG8. 'y416' is refused and converted on the
    CPU.
  - After their switch, the tag-switch clips equal their second part alone
    (`snapshot_diff` max 0), with and without VideoToolbox.
- With `--no-hw` against VideoToolbox (`record2.txt`):
  - The bars' centres are equal for 10-bit 4:2:0, ProRes 422 HQ, 4444 and
    4444 with alpha, HEVC 4:2:2 and 4:4:4 at 10 bits, and 8-bit HEVC 4:2:0.
  - Two pairs go over `snapshot_diff`'s budget. In `bars_pq` the software
    path's HDR output is 1 to 3 codes lower, which section 3.1 says no sws
    flag corrects ([C10]). In `bars_prores4444a` 5117 pixels, 7 × 731 (the
    capture's height), differ by more than 20 (max 48); the centres are
    equal.
  - Not explained yet: 8-bit 4:2:2 (H.264) and 4:4:4 (HEVC). Bar 2 (Y 127,
    exactly 129.25) shows as 130 with VideoToolbox and 129 without, and
    bar 6 is up to one code higher. VideoToolbox returns the
    same codes as FFmpeg ([C1]).

## Section 4: decode errors, a track with no decoder, the W strip (D-091)

- 4.1, `h264_noise.mp4`:
  - With VideoToolbox, the log ends at 82 decode errors, up to 25 in a
    row, and the I panel's Decode line shows them (`noise_I.png`). FFmpeg's
    command line with VideoToolbox also gives 82 decode errors, and 38
    frames decoded.
  - With `--no-hw` the 3 s report counts 2 decode errors and 7 corrupt
    frames. Over the whole clip, FFmpeg's command line in software counts
    3 decode errors.
- 4.2 and 4.4 (`record3.txt`, the screen unlocked):
  - The control, `av_src.mp4` played plainly, has 5.067 s of video and
    4.95 s of audio in 5.026 s.
  - The switch to AC-4 says "No decoder found for ac4" and ends at once,
    and the track stays silent. The Tracks panel shows "No decoder for ac4"
    under it (`record3/ac4_tracks.png`). The audio clock goes on: the 8 s
    report has 8.133 s of video and 8.0 s of audio, with no underrun
    (`reports/record3/ac4_report.json`).
  - The switch to EVC says "No decoder found for evc", ends at once, and
    leaves the track blank, and the Tracks panel shows "No decoder for evc"
    (`record3/evc_tracks.png`). A seek on that blank track does not wait,
    and no "Buffering" shows (`logs/record3/evc_seek.log`).
  - The W strip of the clip whose left channel is silent is no longer
    empty (`record3/r_only_W.png`).
- In `record.txt`, with the screen locked, every report with audio has
  `audio_media_s` 0. The AC-4 switch's take-over point, 1.003 s, was never
  reached there (`video_media_s` 0.467).

## Found on the way

The scrub preview and the step back go through `ScrubDecoder`, which seeks
for itself. On an MPEG-TS they land after the keyframe before their
target, as the demuxer did before D-088: the preview shows the next
keyframe's frame, or none in the last GOP, and the step back lands on the
next keyframe. In an MP4 the preview is the target's frame. D-088 changed
`Demuxer::read_loop` only.

The I panel's Decode line is cut at the panel's edge: `noise_I.png` shows
"VideoToolbox: 67 decode errors (last: Invalid data found when processin".

After the switch to the EVC track, the badges read "EVC 1280×720": the new
track's codec beside the size of the old track's last frame. The EVC track
is 640×360, and none of its frames is decoded (`record3/evc_tracks.png`).
