# Engineer tools POCs: status

The POCs listed in `plan/engineer_tools_ideas.md`, what each one does now, how
it was checked, and what is left. Checks marked "scripted run" used the
player's QA hooks (`--qa-no-focus`, `--ui-overlays`, `--ui-snapshot`) on
clips generated with FFmpeg's `testsrc2` source.

## P — Frame inspector

**What it does.** `P` (also View › Frame Inspector and the overlays popover)
opens a card with the metadata of the frame on screen — the frame playback
shows, a scrub preview, a frame step:

- frame: picture type and key flag, number counted from the stream's start
  time, PTS (with the raw value and time base in the tooltip), duration,
  size and SAR, crop, scan (interlaced / field order / repeat), decode errors;
- format: pixel format (VideoToolbox frames name their pixel buffer's format),
  range, matrix, transfer, primaries, chroma siting — each against what the
  renderer converts with;
- HDR: SMPTE ST 2086 mastering display (primaries recognised by name),
  content light level, HDR10+ (scene maxRGB, average, percentiles), Dolby
  Vision configuration and RPU level 1, ambient viewing environment — from the
  frame, or from the stream's coded side data;
- side data as the decoder attached it, decoded where FFmpeg has a structure
  for it (captions, timecodes, AFD, display matrix, film grain, encode
  parameters, motion vectors, stereo 3D, spherical);
- encoder settings from the user-data SEI x264 and x265 write. They write it
  once, on the first IDR frame, so the card remembers the last one seen in the
  session and says when it was seen;
- findings first: colour tags that differ from the renderer's interpretation,
  PQ without ST 2086 / without MaxCLL, MaxCLL 0/0, BT.2020 on the SDR path,
  Dolby Vision without a compatible base layer, decode errors, interlacing;
- Copy puts the whole report on the clipboard as text.

**How it works.** Every upload to the renderer now goes through one lambda in
`run_session` that keeps a reference to the uploaded frame (`av_frame_ref`, no
copy) until the next upload. The report is rebuilt only when the card is open
and the frame changed. `VideoRenderer::interpretation()` exposes the colour
interpretation the renderer already used; nothing in the render or decode
path changed. Code: `src/FrameInfo.*` (pure, unit-tested),
`src/FrameInspector.*` (the card).

**Checked.**

- `mirage_tests '[frameinfo]'`: 10 test cases (encoder SEI parsing, the PQ
  EOTF against its inverse, frame rows and numbering, tag mismatches,
  unspecified tags, HDR10 static metadata and HDR10+, side data, decode errors,
  remembered settings). Whole suite `'~[audio]'`: every case passes except
  the subtitle-fixture case that already fails on `main` in a clean checkout.
- Scripted runs:
  - H.264 SDR MP4 (x264, B-frames), VideoToolbox and `--no-hw`: types, numbers
    and timestamps match the pattern's burnt-in frame counter;
  - HEVC 10-bit tagged HDR10 (x265 with mastering display and light level
    SEI), VideoToolbox P010: the mastering primaries read as P3-D65, the
    luminance and MaxCLL/MaxFALL as encoded, x265's settings listed;
  - an MPEG-TS whose HEVC stream switches from BT.709 SDR to PQ at 4.48 s:
    after the switch the card flags transfer, matrix and primaries as
    differing from the interpretation the renderer opened with, which is what
    the renderer keeps using.

**Not checked.** Dolby Vision, HDR10+, captions and timecodes from real
streams (HDR10+ only in a unit test; the others only as code paths);
interlaced content.

**To finish.**

- Act on a tag change, not just report it (re-init the renderer on the new
  interpretation). That changes the render path: D-007 asks for an approved
  plan first.
- Number frames from the stream start everywhere: the timecode's frame mode
  and the frame-step toasts still use absolute PTS × fps.

## Found while checking

- **A seek into the second half of a concatenated HEVC MPEG-TS never shows a
  frame.** The SDR→PQ clip above (two 3 s x265 encodes joined with the concat
  demuxer, `-c copy`) plays from the start, but opened with `--ui-sub-at 5.2`
  (a seek to 5.2 s) the video decoder takes the packets up to the end and
  returns no frame, with VideoToolbox and with `--no-hw`; the startup
  watchdog reports it.
  FFmpeg's command line decodes the file without errors. Not investigated
  further.
- On the same MPEG-TS (start time 1.48 s) the control bar shows the absolute
  PTS against a duration counted from 0 (`00:07.440 / 00:06.000` at the end).
