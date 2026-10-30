# Engineer tools: ideas and proofs of concept

Branch `feat/engineer-tools-poc` (from `main` at `a102e80`). This file collects
feature ideas for the people Mirage is built for — codec, picture-quality,
streaming and audio/QA engineers — ranks them, and says which ones this
branch builds as proofs of concept (POCs) and what was found on the way.

VMAF is out of scope here: it is being extended on the `vmaf/*` branches.

## How the list was made

Four reviews of the code at `a102e80`, one per role, each checking its ideas
against the sources (so nothing duplicates an existing panel) and against the
FFmpeg 8.1.1 (`ffmpeg-full`) headers and binaries:

- codec / bitstream analysis
- picture quality, colour and HDR
- streaming / delivery (HLS, DASH, CDN, ABR)
- audio QC and everyday QA workflow

**The gap they agree on.** Mirage's network and HLS tooling is already deep
(request log, playlist bodies, segment heads, A/V/subtitle alignment). Nothing
in the player reads pixel values, bitstream structure, per-frame metadata or
audio levels, and those are the questions engineers bring to a player most
days. The POCs therefore start there.

Effort: **S** under a day, **M** one to three days, **L** more.

## Ideas by role

### Codec / bitstream

| Idea | Problem it answers | Effort |
|---|---|---|
| **Frame-size / GOP analyzer** — bars per frame coloured I/P/B, GOP boundaries, rolling bitrate, click to seek; frame table | I-frame spikes, keyint and scene-cut placement, B-pyramid structure, peaks against a VBV/bandwidth cap | M |
| **Frame inspector** — `ffprobe -show_frames` for the frame on screen: type, flags, timestamps, crop, colour tags, side data decoded (mastering display, light level, HDR10+, Dolby Vision, captions, timecode, encoder-settings SEI) | "Did the HDR metadata / captions / RPU survive packaging?", "which x264 settings made this rung?" | S–M |
| Decode-error tracker — errors and concealed frames as seek-bar ticks and a list | "Green smear at 12:34: packet loss, bad splice or encoder bug?" (`Decoder::push` ignores `avcodec_send_packet`'s status today) | S |
| Header / SEI syntax trace through the public `trace_headers` bitstream filter | slice QP deltas, POC, CRA/RASL, VUI/HRD, AV1 `base_q_idx` without a separate analyzer | S–M |
| Timestamp and GOP lint, with a JSON report for CI | DTS going backwards at splices, PTS < DTS, gaps, reorder deeper than declared, segments not starting on an IDR | S–M |
| QP heatmap / motion-vector overlay (software decode only) | adaptive-quantisation and motion-search tuning | M–L |
| HW-vs-SW decode cross-check and per-frame hashes (framemd5-compatible) | "Is this corruption VideoToolbox-only?", decoder conformance | M |

Side data the FFmpeg 8.1.1 software decoders export, checked on 1-second
`testsrc2` encodes with
`ffprobe -export_side_data venc_params -flags2 +export_mvs -show_entries frame_side_data`:

| Codec | Per-block QP (`VIDEO_ENC_PARAMS`) | Motion vectors | Encoder-settings SEI |
|---|---|---|---|
| H.264 (libx264) | yes | yes | yes |
| MPEG-2 | yes | yes | — |
| HEVC (libx265) | no | no | yes |
| VP9 (libvpx) | yes | no | — |
| AV1 (SVT-AV1 encode) | no | no | — |

So a QP/MV overlay is an H.264/MPEG-2 (QP also VP9) tool and needs `--no-hw`.

### Picture quality, colour, HDR

| Idea | Problem it answers | Effort |
|---|---|---|
| **Pixel probe** — true code values under the cursor (Y′CbCr at the stream's bit depth, unclamped R′G′B′, nits for PQ/HLG) | "Is black at 16 or 4?", "is that logo at 941 illegal?", "1,000 or 4,000 nits?" | S |
| **Scopes** — luma waveform, RGB parade, vectorscope, histogram, from code values (not from the converted RGB on screen) | legal range, clipping, colour casts, saturation, "fake 10-bit" (only every 4th code used) | M |
| Colour-interpretation override (range / matrix / transfer) and a watch for tag changes mid-stream | "washed out / crushed" tickets; an SDR ad inside a PQ stream renders wrongly and nothing says so | S |
| Zoom, 1:1 and nearest-neighbour pixel peeping with pan and block grids | ringing, banding, deblocking, chroma bleed hidden by the fitted bilinear scale | S–M |
| HDR signalling and light-level QC (MDCV, MaxCLL/FALL, HDR10+, DV profile; measured CLL/FALL) | "is MaxCLL honest?", "DV profile 5 or 8.1?" | M |
| Analysis view modes: false colour, zebra, out-of-legal, out-of-gamut, single channel, LSB plane | where exactly the illegal / over-1,000-nit / out-of-P3 pixels are | S (HW path) – M |
| A/B compare: wipe, split, flicker, difference between two files or renditions | preset/bitrate comparisons, source vs encode | M (paused) – L (synced) |

### Streaming / delivery

| Idea | Problem it answers | Effort |
|---|---|---|
| Network condition emulator: bandwidth cap, added RTT, scripted HTTP errors/timeouts, per session | reproducible bad networks and CDN failures; exercising the retry/stall code on purpose | M |
| Delivery panel: throughput, forward buffer and variant bitrate over time, plus a QoE JSON (startup, rebuffers, switches) | "was it the network, the buffer or the decoder?"; CI numbers per build or CDN | S–M |
| CDN cache inspector: HIT/MISS, Age, TTL, POP per request, hit ratio | cold rungs, stale live playlists at the edge | S |
| Live playlist health: latency, distance to the live edge, diff of each reload against the previous one | creeping latency, a stuck edge, media sequence going backwards | S–M |
| ABR "what-if" simulator over a measured or synthetic throughput trace | ladder spacing and ABR rules without a device lab (Mirage has no ABR today: variants change only by hand) | M |
| Ad-break / SCTE-35 inspector (DATERANGE, CUE-OUT/IN, splice decoding, IDR/segment alignment checks) | SSAI splice problems | M |
| DASH MPD inspector with SegmentTemplate expansion | `$Number$`/`$Time$` and timeline gaps | L |

Note for anything that measures throughput: the demux thread blocks while its
queues are full, so in steady state a segment download is paced by playback,
not by the link. Throughput read from requests is a lower bound unless reading
is allowed to burst.

### Audio QC and QA workflow

| Idea | Problem it answers | Effort |
|---|---|---|
| **Loudness and level meters** — BS.1770-4 / EBU R128 momentary, short-term, integrated, LRA, true peak; per-channel peak/RMS labelled from the layout; phase correlation; in sync with what is heard | delivery specs (R128 −23 LUFS, ATSC A/85 −24 LKFS, streaming −14/−16), dead or swapped channels, inverted polarity | M |
| Channel solo / mute through the stereo output | "is the centre really dialogue?" — today the LFE is dropped and C/surrounds are folded into L/R by the downmix | S |
| A–B repeat and go-to frame / timecode | repeating a two-second glitch; "frame 41237", "01:02:03:12" | S |
| Frame export (bit-exact raw planes + PNG + JSON metadata) and a one-key bug bundle | attaching evidence to a ticket | M |
| Bookmarks and QC notes with CSV/JSON export | review sessions | S |
| Content A/V sync measurement on flash/beep test clips | priming not trimmed, timestamp offsets — content sync, not the player-clock drift the G graph shows | M |
| QC scan (silence, black, freeze, loudness) with seek-bar markers and a headless `--qc-report` | signing off a long programme, CI after encode/package | L |

## Built on this branch

Chosen for value to engineers per effort, for covering three of the four roles
whose gap is widest, and because each one is additive: new modules and panels,
no refactor of the existing render or decode paths. (Decision D-007 asks for an
approved plan before such a refactor; whatever a POC would need there to
become a feature is listed under its "To finish" notes instead of done here.)

| Key | POC | Role |
|---|---|---|
| `P` | Frame inspector | codec, HDR |
| `B` | Frame-size / GOP analyzer | codec |
| `S` / `X` | Scopes / pixel probe | picture quality |
| `L` | Loudness and level meters | audio QC |

The status of each POC (what works, how it was checked, what is left) is kept
in `plan/engineer_tools_status.md`.

## Found on the way

Checked in the code unless marked otherwise:

- **Frame numbers are absolute.** The timecode's frame mode and the frame-step
  toasts compute `round(pts × fps)` from the presentation timestamp
  (`UiFormat.cpp`, `main.cpp`), so a stream that starts at a non-zero time
  (MPEG-TS, HLS) is numbered from its start time, not from 0.
- **10-bit limited range uses 8-bit constants on the HW path.** `YuvMatrix.h`
  scales limited range by 255/219 with a 16/255 offset; a P010 sample is
  normalised as code·64/65535, so code 940 (10-bit reference white) comes out
  as about 0.9958 instead of 1.0. Small for SDR; amplified by the PQ curve.
- **Colour tags are read once.** The renderer takes matrix, range and transfer
  at open; `upload()` never looks at a frame's own tags, and primaries are not
  used. A mid-stream change (variant switch, an SDR ad in an HDR stream) is
  rendered with the old interpretation, silently.
- **Decode errors are invisible.** `Decoder::push` discards
  `avcodec_send_packet`'s return value and `pull` turns every error into
  "no frame".
- **The LFE never reaches the output** (the stereo downmix's default LFE mix
  level is 0), and the W strip shows the left channel only, after the downmix
  and the speed change. Its buffer comment says "≈3.6 s"; 1,200 entries of 40
  samples at 48 kHz are 1.0 s.
- **Possibly: 4:2:2 / 4:4:4 10-bit VideoToolbox buffers** (ProRes) are bound as
  8-bit textures, because the 10-bit check lists the 4:2:0 formats only. Not
  tried with a ProRes file.
