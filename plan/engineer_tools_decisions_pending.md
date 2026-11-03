# Engineer tools: what the owner decides before the merge

The engineer-tools branches are ready to be proposed for main: the panels P,
S, X, B and L, the HLS conformance check (`mirage_hlscheck` and the
Conformance tab of `N`) and the DASH conformance check (`mirage_dashcheck`).
What each one does and how it was checked is in
`plan/engineer_tools_status.md`. This file lists what the owner decides
first. Nothing in it is decided yet: each item gives the options and the
agent's recommendation.

1. D-007: the panels' changes to the player
2. The conformance checks: thresholds and error classes
3. The shape of the PR

## 1. D-007: the panels' changes to the player

D-007 asks for a plan the owner approves before code changes the render or
decode path. The panels were written first, as proofs of concept on a branch
of their own, so this plan comes after the code: the owner approves it, asks
for changes or turns it down before anything merges.

### What changes in files that existed

| File | Change | When it runs |
|---|---|---|
| `src/main.cpp`, `run_session` | The 8 calls `renderer.upload(frame)` become `upload_frame(frame)`. It keeps a reference to the frame (`av_frame_ref`: the picture is not copied) until the next one is uploaded, counts the uploads, and lets `frameinfo::remember` keep the encoder settings some streams send in a SEI on a few frames only. Then it calls `renderer.upload` as before. | every frame shown, the panels open or closed |
| `src/VideoRenderer.{h,cpp}` | `init()` keeps the transfer characteristic and the primaries it is given (it ignored the primaries); `interpretation()` returns the colour interpretation the conversion uses. The conversion does not change. | read by P, S and X while they are open |
| `src/AudioDecoder.{h,cpp}` | `set_tap(LoudnessTap*)`: every decoded frame goes to the tap before the resampler, and `flush()` and `reopen()` tell the tap. | every decoded audio frame; the tap returns at once unless L is open |
| `src/MediaSession.{h,cpp}` | A `LoudnessTap` member, declared before `audio_decoder` (which keeps a pointer to it) and set as its tap in `open_media()`. `hls_master()` returns the master playlist copy that playback opened, so that B's scan sees the same stream indices. | — |
| `src/PlayerUI.{h,cpp}` | Menu items, overlay toggles and help rows for the five panels. | — |
| `src/main.cpp`, input | The keys P, S, ⇧S, X, B and L, their letters in `--ui-overlays`, the panels in Esc's list, and two QA options, `--scope-mode` and `--ui-probe`. | — |

Nothing in `src/Decoder.*`, `src/Demuxer.*`, `src/AudioPlayer.*`, the
clocks or the sync code changes.

### What the panels do while they are open

- **P** describes the frame on screen once per new frame, on the render
  thread, from the frame's properties and side data.
- **S** measures on a thread of its own (`scopes::Worker`), one frame at a
  time; the render thread hands it a reference to the frame on screen.
- **X** maps the frame on screen for reading by the CPU (`PixelView`) when a
  new frame is shown. A VideoToolbox buffer stays locked while it is mapped;
  closing X releases it.
- **B** reads the video stream's packets again with a demuxer of its own, on
  its own thread, and decodes nothing. A local file is scanned when B opens,
  a network input only when asked, since that downloads all of it. Closing B
  stops the scan.
- **L** measures on the audio decoding thread, in the tap, the channels as
  decoded, before they are resampled and downmixed.

### What it costs while the panels are closed

For each frame shown, one reference taken and the previous one dropped, and a
loop over the frame's side data (`frameinfo::remember`); the picture on
screen stays allocated until the next upload instead of after its own. For
each decoded audio frame, one relaxed atomic load in the tap. Whether any of
it shows in playback was looked at only in a scratch run whose outputs were
not committed: MEASUREMENT PENDING. A scripted playback with the panels
closed and with all five open, its logs committed, would settle it.

### Options

1. **As built (recommended).** The reference is kept for every frame, so P,
   S and X show the frame on screen as soon as they open, paused or not.
2. **Keep the reference only while P, S or X is open.** Nothing changes per
   frame while they are closed. Opened while paused, they would show nothing
   until the next upload, unless the renderer hands its frame back.
3. **Leave the panels out of the player**, as tools of their own that read
   files, like `mirage_vmaf`. Nothing in the player changes; the panels lose
   the frame on screen and the audio being heard.

Where L taps is a choice of its own: before the resampler (as built), where
the stream's channels get the weights ITU-R BS.1770 gives them, or after it,
where the meter would measure the stereo downmix the player plays, in which
the LFE's mix level is 0 (`plan/engineer_tools_ideas.md`). The
recommendation is before, as built.

### Not in this plan

Each of these would need a plan of its own:

- Re-initialising the renderer when the colour tags change mid-stream (P
  reports the change; the renderer keeps the tags `init()` was given).
- Numbering frames from the stream's start in the timecode and the
  frame-step toasts.
- Opening MPDs in the player: FFmpeg 8.1.1 as installed has no DASH demuxer.
- What the checks found in the existing player ("Found while checking" in
  `plan/engineer_tools_status.md`), such as a seek into the second half of a
  concatenated HEVC MPEG-TS that shows no frame.

## 2. The conformance checks: thresholds and error classes

The values are the code's now. The owner keeps or changes each one; a change
is one line. The recommendation is to keep them for the merge.

**HLS** (`src/HlsConform.h`):

| Constant | Value | What it decides |
|---|---|---|
| `kPeakSlack` | 0.01 | A BANDWIDTH below the measured peak is an error when short by more than this fraction of the peak, a note within it. |
| `kOverDeclared` | 1.50 | A BANDWIDTH above this many times the peak is noted. |
| `kAverageSlack` | 0.10 | AVERAGE-BANDWIDTH is reported when off the measured average by more than this fraction of it. |
| `kRateSlack` | 0.002 | The measured frame rate's own error allowed when FRAME-RATE is checked against it. |
| `kDurationSlack` | 0.050 s | How far a segment's media may run past its EXTINF before it is reported. |

**DASH** (`src/DashConform.h`), which also uses `kPeakSlack` and
`kOverDeclared` for @bandwidth:

| Constant | Value | What it decides |
|---|---|---|
| `kTimeSlack` | 0.050 s | How far a segment's earliest presentation time may sit from where its timeline puts it, and how far its media may run past its duration. |
| `kTimeBad` | 0.500 s | Beyond this, the first is an error. |
| a quarter / half of @duration | — | Addressed by @duration, an offset of a quarter is noted, of half a warning. |
| `kAlignSlack` | 0.100 s | How far apart the same segment of two aligned Representations may start. |
| `kAlignBad` | 0.500 s | Beyond this, an error. |

Also the owner's:

- Which findings count as errors, which make the exit status 1. Today, for
  example, a @maxSegmentDuration exceeded by up to `kTimeSlack` is a warning
  and by more an error, and a wrong @frameRate is a warning.
- Whether a @bandwidth between the two rates the DASH check computes
  deserves a note. It conforms when the bytes of each segment arrive no
  sooner than they play, which the sizes alone cannot tell.
- Whether the Conformance tab sizes every segment by default: one request
  per segment.
- How the player should reach the DASH check for an MPD it cannot play, a
  URL field in the Conformance tab, say. Opening MPDs at all is the open
  path's, so a D-007 plan of its own.

## 3. The shape of the PR

1. **One PR (recommended)** from the top branch, with the three parts in
   their order: the panels, the HLS check, the DASH check. One review and
   one merge; the decision in section 1 covers the only part that changes
   the player's render, decode and audio path.
2. **Three PRs, one per part**, in that order, each based on the one before
   until that one merges.

If the owner turns section 1 down, the two checks can go without the
panels. The HLS check includes none of the panels' headers, but its branch
sits on theirs, so it would need a rebase onto main first.
