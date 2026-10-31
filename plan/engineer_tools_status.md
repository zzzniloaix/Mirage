# Engineer tools POCs: status

The POCs listed in `plan/engineer_tools_ideas.md`, what each one does now, how
it was checked, and what is left. Checks marked "scripted run" used the
player's QA hooks (`--qa-no-focus`, `--ui-overlays`, `--ui-snapshot`) on
clips generated with FFmpeg's `testsrc2` source;
`tests/engineer_tools_snapshots.sh [build_dir] [out_dir]` makes those clips
again, writes the same snapshots and prints FFmpeg's `ebur128` reading of the
loudness clips (it needs a display, like `pathb_snapshot_regression.sh`).

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
every segment of the variant. Closing the card stops a scan still running
(reopening restarts a local one; a network one offers "Scan again").
`FramesModel` copies new records as they come and rebuilds the display order,
bitrate and summary on a task of its own, at most twice a second while the
scan runs. The frame on screen is looked up by a time that never decreases:
the pts, else the last one carried forward a frame at a time (raw elementary
streams have no timestamps at all; their bitrates use those times too, and
the summary says so). Code: `src/FrameScan.*` (with
`tests/test_frame_scan.cpp`), `src/FramesPanel.*`.

**Checked.**

- `mirage_tests '[framescan]'`: 14 test cases (generated H.264 in MKV, MP4
  and MPEG-TS, MPEG-2, raw video, an MP4 cut inside a GOP, stop mid-scan,
  the helpers). Its author also ran them under ThreadSanitizer and
  AddressSanitizer + UBSan in scratch builds; this branch's checks did not
  repeat that.
- Scripted runs: the x264 clip (GOP of 50, B-frames: the summary and the bar
  pattern match the encode), a five-minute 1080p H.264 file (the view
  follows the frame on screen; the frame numbers agree with the frame
  inspector's) and a raw H.264 elementary stream (no timestamps: the frame on
  screen is found by frame-rate times, the bitrates and GOP durations come
  from them).

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

## N › Conformance — declared vs measured (and `mirage_hlscheck`)

Added after the POCs above, when the direction turned to streaming
development. On the branch `feat/rendition-check`, stacked on
`feat/engineer-tools-poc-on-main`.

**What it does.** The network inspector (`N`) has a fourth tab,
Conformance. "Check" reads the HLS master playback opened and compares what
each EXT-X-STREAM-INF declares with what its media holds:

- BANDWIDTH against the peak segment bit rate (RFC 8216 §4.1: the largest
  bit rate of any run of segments lasting 0.5 to 1.5 target durations) of
  the variant's playlist plus its largest audio rendition — §4.3.4.2 makes
  BANDWIDTH the largest sum of the renditions' peaks — and
  AVERAGE-BANDWIDTH against the average likewise;
- CODECS against the codec strings built from each stream's decoder
  configuration (avcC / hvcC / av1C, the SPS of an MPEG-TS stream, the
  Dolby Vision configuration record): a format the media has that CODECS
  does not list (§6.2.4), another profile, a lower level or tier, the AAC
  object type, the fMP4 sample entry (avc1 / avc3, hvc1 / hev1);
- RESOLUTION against the decoded size, FRAME-RATE against the rate the
  timestamps give, VIDEO-RANGE against the transfer characteristics
  (draft-pantos-hls-rfc8216bis-22; Dolby Vision without a compatible base
  layer counts as PQ);
- every segment read: its first picture (IDR, CRA, BLA — or a recovery
  point, an I that is not an IDR, a P/B) against EXT-X-INDEPENDENT-SEGMENTS,
  and the span of its pictures against EXTINF (a note only when the
  segments have leading pictures: an open GOP, or a cut inside a GOP);
- the audio renditions' CHANNELS against the channels decoded, and one
  target duration for every playlist (§6.2.4).

A row per variant with a cell per check (hovering one shows the declared
and measured values and the numbers behind them); the findings below, a
mistake in a playlist several variants share listed once with all of them;
clicking a row keeps its own findings only. "Copy the report as JSON". The
same check runs without the player: `mirage_hlscheck [--segments N|all]
[--no-sizes] [--json FILE] <master URL or path>`, exit status 1 when it
finds errors; it builds in the headless configuration too.

**How it works.** `ConformProbe` (`src/HlsConformProbe.*`) runs on a thread
of its own with four workers. It fetches every variant and audio playlist
(parsed by `hls::parse_media_timeline`, the Alignment tab's parser), sizes
every segment — the EXT-X-BYTERANGE length, else a request for its first
byte, whose Content-Range carries the size — and reads three segments of
each variant playlist in full (the first, the last, the middle; one of each
audio rendition; the toolbar picks 1, 3, 10 or all), each behind its init
segment, demuxed from memory: the decoder configuration, the NAL unit types
of the first packet, every packet's timestamps. The first video frame and a
few audio frames are decoded (decoded size, transfer; AAC's implicit SBR /
PS, channels). Its requests go through NetworkLogger as initiator
"inspector", so the Network tab lists them. `hls::analyze_conformance`
(`src/HlsConform.*`, no FFmpeg) compares; `src/NetInspectorConform.cpp`
draws; `tools/mirage_hlscheck.cpp` prints.

**Checked.**

- `mirage_tests '[hls_conform]'`: 15 test cases. Attribute lists and
  masters (URI resolution; a missing BANDWIDTH, a missing URI line, an
  undefined AUDIO group); codec strings parsed, and built from hand-made
  avcC / hvcC / av1C records and Annex B SPSs with emulation prevention;
  first pictures from hand-made H.264 and HEVC access units, Annex B and
  length-prefixed; peak and average bit rates on windows worked out by hand;
  the verdict on hand-built measurements (each check, findings merged
  across variants sharing a playlist, BANDWIDTHs a hair under the peak); and
  media encoded with libx264 / libx265: MPEG-TS and fragmented MP4 segments
  read from memory, and a local package (two variant playlists cut by
  FFmpeg's hls muxer, one with `split_by_time`) checked end to end. They
  pass in the `macos-debug` and `macos-headless` builds.
- Apple's four public example streams, `mirage_hlscheck --workers 6`
  (`plan/engineer_tools_runs/hlscheck/`): no error and no warning in any of
  them. Each has one note: the BANDWIDTH of some or all variants is slightly
  below the measured peak — at most 0.018% (fMP4), 0.015% (TS), 0.027%
  (HEVC), 0.93% (Dolby Vision / Atmos). §4.3.4.2 sums each rendition's own
  peak; a peak taken per segment, or over the video and audio of the same
  time together, comes out that little lower. Shortfalls up to `kPeakSlack`
  (1%) are therefore a note, beyond it an error.
- `tests/hlscheck_demo.sh` (`demo.txt`): a package of generated media whose
  master has one mis-declaration of each kind. All of them are reported: a
  BANDWIDTH 41.9% below the peak, an AVERAGE-BANDWIDTH 49% off, CODECS Main
  for a High stream, HEVC level 3 declared for a level 3.1 stream, no
  VIDEO-RANGE on PQ video, RESOLUTION and FRAME-RATE wrong, CHANNELS="6" on
  stereo, and 6 of 8 segments starting with a P/B picture under
  EXT-X-INDEPENDENT-SEGMENTS.
- Scripted runs of the player (`--ui-net
  tab=conformance,conform=1,conform_out=…` with `--ui-snapshot`) on Apple's
  fMP4 example over HTTP and on the demo package as local files: the tab
  draws, and reports the same counts as the command line.

**Not checked.** Live playlists (the check reads the window listed when it
starts; nothing follows reloads); encrypted segments (sized, not read);
SUPPLEMENTAL-CODECS and Dolby Vision profile 8 declarations; I-frame
playlists, subtitle renditions and EXT-X-MEDIA TYPE=VIDEO; DASH; a server
that ignores Range on byte-range segments (what is read would be the start
of the resource); the player against a local HTTP server (the sandbox these
checks ran in blocks local sockets, so HTTP was Apple's CDN only).

**To finish.** Decisions for the owner: the thresholds — `kPeakSlack` (1%),
`kOverDeclared` (a BANDWIDTH above 1.5 × the peak is noted), `kAverageSlack`
(10%), `kRateSlack`, `kDurationSlack` (50 ms) — and which findings count as
errors; whether the player should size every segment by default (one
request each: 1,076 for the TS example, see `apple_ts.txt`). NetworkLogger
keeps the newest 4,000 requests, so checking a large master pushes the
playback's own requests out of the Network tab.

## Review

A review of the branch (`git diff main...HEAD`, read by someone who had not
written it) found nothing that changes playback while the panels are closed,
and six defects, all fixed after it:

| Panel | Defect | Fix |
|---|---|---|
| B | Closing the card left a scan running: an HLS scan kept downloading the variant, a live one never ended | the scan stops when the card closes |
| B | The display order, bitrate and summary were rebuilt on the render thread twice a second while scanning — a long stream's sort stalls a debug build's frames | built on a task of its own and swapped in |
| B | The frame on screen was looked up by pts with a binary search; frames without a pts (NaN) broke it (raw streams: always the last frame) | a never-decreasing time per frame (above) |
| B | Fully zoomed out, `span × columns` overflowed `int` on very long streams (millions of frames) | 64-bit arithmetic, as the overview had |
| S | Above 12 bits, "codes used" counted the histogram's 4096 bins against 2^depth codes | an exact count of the codes |
| L | Reopened while paused, the card showed the last block from before it was closed until playback resumed | the history clears when the card opens |

Still as the review left it: a file on a network share counts as local, so
opening `B` scans it at once over the share.

## Found while checking

- **A seek into the second half of a concatenated HEVC MPEG-TS never shows a
  frame.** The SDR→PQ clip above (two 3 s x265 encodes joined with the concat
  demuxer, `-c copy`) plays from the start, but opened with `--ui-sub-at 5.2`
  (a seek to 5.2 s) the video decoder takes the packets up to the end and
  returns no frame, with VideoToolbox and with `--no-hw`; the startup
  watchdog reports it.
  FFmpeg's command line decodes the file without errors. Not investigated
  further.
- **FFmpeg's hls muxer writes EXTINF 2.000 for fMP4 segments of 46 and 53
  frames at 25 fps** (x265, open GOP, the demo package's `v3`; `demo.json.xz`
  lists each segment's frame count). The conformance check reports it; the
  playlist's total stays right.
- **FFmpeg's hls muxer writes `EXT-X-TARGETDURATION:0` for segments under
  half a second**, which the Alignment parser reads as no target duration;
  the conformance check tells the two apart.
- On the same MPEG-TS (start time 1.48 s) the control bar shows the absolute
  PTS against a duration counted from 0 (`00:07.440 / 00:06.000` at the end).
