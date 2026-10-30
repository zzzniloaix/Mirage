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

## S / X — Scopes and pixel probe

**What they do.**

- `S` opens the scopes card (also View › Scopes and the overlays popover);
  `Shift+S` or the chips pick the scope:
  - luma waveform, graticule at the legal levels (16/235 at 8 bits, 64/940
    at 10) and 50 %, and for PQ a nits scale (100, 203, 1,000, 4,000 cd/m²);
  - R′G′B′ parade on the same graticule;
  - vectorscope with the 75 % and 100 % targets computed from the matrix in
    use, the extent of the RGB cube and the skin-tone line;
  - luma histogram at the stream's bit depth, illegal codes shaded.
  Below them: Y′ min/max/average, the share of samples below black, above
  white and with chroma out of range, how many Y′ codes are used, and a
  warning when a 10/12-bit frame never uses its low bits (an 8-bit source).
- `X` turns the probe on: at the mouse over the video, the coded pixel, its
  Y′CbCr codes at the stream's bit depth, the normalised values, unclamped
  R′G′B′, display light for PQ and HLG (BT.2100, HLG at 1,000 cd/m²), and
  flags for sub-black, super-white, chroma out of range and R′G′B′ outside
  EBU R103's −1…101 %.
- `--scope-mode` and `--ui-probe <fx>,<fy>` stage both for `--ui-snapshot`.

**How it works.** Both read code values, not the picture on screen:
`PixelView` reads a software frame as decoded and maps a VideoToolbox frame
(`av_hwframe_map`, the pixel buffer locked for reading) — on the SW path the
renderer's RGB has already clipped and quantised. The scopes run on a worker
thread of their own, one measure at a time of the newest frame; frames above
1920 × 1080 are sampled on a grid (every second pixel and row of 4K). The
probe maps the frame on screen once per new frame while it is on. Code:
`src/PixelView.*`, `src/Scopes.*` (pure parts unit-tested),
`src/ScopesPanel.*`.

**Checked.**

- `mirage_tests '[scopes]'`: 11 test cases — code values to R′G′B′ (10-bit
  reference white is exactly 1.0), PQ grey at 100 cd/m² and HLG 75 % at
  about 203 cd/m², P010 written by hand and read back, chroma lookup for
  4:2:0 / NV12 / 4:2:2 / 4:4:4, refused formats, a flat frame's waveform and
  histogram, legality shares, unused low bits, the vectorscope bin and the
  parade levels of BT.709 red, grey frames, the worker answering the newest
  frame.
- Scripted runs: x264 SDR (waveform, parade, vectorscope: the pattern's
  100 % bars fall on the computed 100 % targets), x265 HDR10 (waveform with
  the nits scale, histogram), and a 4K PQ AV1 file decoded by VideoToolbox
  into P010 (waveform; the probe on a face).

**Not checked.** HLG content in the app (HLG only in a unit test); 4:2:2 /
4:4:4 VideoToolbox buffers; rotated video (the probe maps the cursor as if
the picture were not rotated); how often the worker keeps up at 4K60 (not
measured).

**To finish.**

- Probe: pin points (Shift+click), average over 3×3 / 5×5, copy.
- A GPU version of the scopes that reads the frame's textures, so 4K60 is
  measured on every frame.

## B — Frame sizes / GOP

**What it does.** `B` (also View › Frame Sizes / GOP and the overlays
popover) opens a card along the bottom:

- a bar per frame in display order, its height the frame's size, coloured
  by picture type (I red, P blue, B violet, H.264 B-frames nothing refers to
  lighter), around the frame on screen; keyframes (GOP starts) marked; the
  rolling one-second bitrate over the bars; the mouse wheel zooms (20 frames
  to the whole stream);
- an overview of the whole stream's bitrate below, the span in view marked;
- a summary: frames by type, keyframe interval (frames and seconds, min to
  max), mean and peak one-second bitrate;
- hovering a bar names the frame (type, keyframe, size, pts / dts, bitrate);
  clicking it, or the overview, seeks there (the same frame-accurate seek as
  the seek bar).

**How it works.** `FrameScan` reads the playing video stream again on a
thread of its own — every packet, nothing decoded — and types each frame with
libavcodec's parser for the codec (I/P/B for H.264, HEVC, MPEG-2, MPEG-4
Part 2; I/P for VP8, VP9, AV1). A local file is scanned when the card opens;
a network input only on request ("Scan this stream"), since a scan downloads
every segment of the variant. `FramesModel` copies new records as they come
and rebuilds the display order at most twice a second while the scan runs.
Code: `src/FrameScan.*` (with `tests/test_frame_scan.cpp`), `src/FramesPanel.*`.

**Checked.**

- `mirage_tests '[framescan]'`: 14 test cases (generated H.264 in MKV, MP4
  and MPEG-TS, MPEG-2, raw video, an MP4 cut inside a GOP, stop mid-scan,
  the helpers). Its author also ran them under ThreadSanitizer and
  AddressSanitizer + UBSan in scratch builds; this branch's checks did not
  repeat that.
- Scripted runs: the x264 clip (GOP of 50, B-frames: the summary and the bar
  pattern match the encode) and a five-minute 1080p H.264 file (the view
  follows the frame on screen; the frame numbers agree with the frame
  inspector's).

**Not checked.** Clicking (seek on click) in a scripted run — it sets the
same absolute seek the seek bar uses; HLS scans; a video track switch during
a scan (a local file is rescanned for the new stream).

**To finish.** Live and HLS: feed the view from the playback demuxer's own
packets instead of a second read; a frame table (StreamEye-style list); a
VBV / bandwidth-cap line.

## L — Loudness

**What it does.** `L` (also View › Loudness Meters and the overlays popover)
opens a card with what is heard: momentary, short-term and integrated
loudness (LUFS), loudness range (LU), the maximum true peak (dBTP), a verdict
against a delivery target (EBU R128 −23 LUFS ±0.5 LU / −1 dBTP, ATSC A/85
−24 LKFS ±2 LU / −2 dBTP, streaming −16 or −14 LUFS ±1 LU / −1 dBTP), a minute
of short-term and momentary history, per-channel peak (one-second hold), RMS
and true peak labelled from the channel layout (LFE marked as not counted),
and the phase correlation of the first two channels. Reset restarts
integrated loudness, LRA and the true-peak maximum; seeks restart them too,
and the card says since when they count.

**How it works.** `LoudnessTap` takes every frame the audio decoder decodes,
as decoded — its own layout and rate, before the stereo downmix, the speed
change and the volume — but only while the card is open: `AudioDecoder` got a
tap pointer and calls it after each decode and on flush / reopen, nothing
else changed in it. Non-planar-float input is converted to planar float;
BS.1770 weights come from the layout (LFE 0, side / back / surround 1.41, the
rest 1.0); a layout without channel order gets FFmpeg's default for its
channel count, as the ffmpeg command line guesses. `LoudnessMeter` measures
on the audio thread and publishes 100 ms blocks; the card shows the block at
the audio clock, i.e. what has just been heard. Code: `src/LoudnessMeter.*`
(with `tests/test_loudness_meter.cpp`), `src/LoudnessTap.*`,
`src/LoudnessPanel.*`.

**Checked.**

- `mirage_tests '[loudness]'`: 18 test cases — the K-weighting coefficients
  against BS.1770-4's 48 kHz table, EBU Tech 3341 cases 1–5, channel weights,
  44.1 and 96 kHz, Tech 3342 LRA cases, true peak of an fs/4 sine, block
  timing, resets, the Feed, and a cross-check against FFmpeg's `ebur128` on
  generated stereo and 5.1 signals.
- Scripted runs against `ffmpeg -af ebur128=peak=true` on the same file:
  - a 5.1 PCM clip (1 kHz on the fronts and surrounds at set levels, a loud
    50 Hz LFE, no channel order in the file): integrated loudness, LRA and
    maximum true peak read as FFmpeg's to the 0.1 shown; the LFE is excluded;
  - the stereo AAC clip: integrated loudness and LRA as FFmpeg's. Its maximum
    true peak read lower than FFmpeg's whole-file figure: the card's
    measurement began half a second in, FFmpeg's at the start.

**Not checked.** 7.1 and other layouts in the app; speed changes while
metering (the tap is before the speed change, so the readings are in media
time); a track switch while the card is open.

**To finish.** Channel solo / mute (the LFE is inaudible through the
stereo downmix today), a loudness strip on the seek bar from a background
scan, and a headless `--loudness-report`.

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
