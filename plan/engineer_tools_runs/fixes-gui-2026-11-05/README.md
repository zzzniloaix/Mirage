# The player fixes' scripted checks (2026-11-05)

These are the checks of `plan/engineer_tools_player_fixes.md` (each
section's Check) that open the player's window. They ran at commit
`3d2b8cc` (`feat/player-fixes`; its code is `66751ed`'s) with its debug
build, on macOS 26.6.2. The record is in the form of `eval/test_runs/`:
each command, then its output and exit status, with `uptime` before each
window. Another session's builds ran on the machine meanwhile; the load
averages are in the records. Local paths appear as `<scratch>` and
`<repo>`.

The screen was locked throughout; `record2.txt` shows it. CoreAudio then
plays nothing, so video, which follows the audio clock, hardly moves in a
clip with audio. The runs whose check needs that playback (4.2 and 4.4)
show nothing yet. `run3.sh` makes them again, and it refuses to run while
the screen is locked: MEASUREMENT PENDING.

| Files | What |
|---|---|
| `record.txt` | `run.sh`: the clips it makes, then each section's runs |
| `record2.txt` | `run2.sh`: the ten `--no-hw` runs of the bars that `run.sh` skipped, and their comparisons with its HW captures; the frame steps, one capture per run; the scrub preview |
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
  51.480 (testsrc's count 50 in `seek_nosei_*.png`). The first frame after
  the seek came 3592 ms after it with VideoToolbox, and 311 ms after it
  with `--no-hw`. Its audio did not play, and `run3.sh` times it again.

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
- 4.2 and 4.4 ran with the screen locked. Every report with audio has
  `audio_media_s` 0. The switch to EVC says "No decoder found for evc",
  ends at once, and leaves the track blank (`logs/evc_tracks.log`). The
  switch to AC-4 takes over at 1.003 s, which playback never reached
  (`reports/ac4.json`: `video_media_s` 0.467), so its log says nothing
  about the decoder. MEASUREMENT PENDING (`run3.sh`).

## Found on the way

The scrub preview and the step back go through `ScrubDecoder`, which seeks
for itself. On an MPEG-TS they land after the keyframe before their
target, as the demuxer did before D-088: the preview shows the next
keyframe's frame, or none in the last GOP, and the step back lands on the
next keyframe. In an MP4 the preview is the target's frame. D-088 changed
`Demuxer::read_loop` only.

The I panel's Decode line is cut at the panel's edge: `noise_I.png` shows
"VideoToolbox: 67 decode errors (last: Invalid data found when processin".
