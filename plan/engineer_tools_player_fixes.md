# Fixes to the existing player: plans for the owner

What the engineer-tools checks found in the player itself
(`plan/engineer_tools_ideas.md`, "Found on the way";
`plan/engineer_tools_status.md`, "Found while checking"), each with what
happens, why, the fix the agent proposes, the alternatives, the risks and
how a fix would be checked. D-007 asks for a plan the owner approves before
a change to the render or decode path: sections 1, 3 and 4 are such
changes, section 2 is the UI's. "Owner's choice" marks what was the
owner's to pick. The owner decided all four sections on 2026-11-03, each as
the agent recommended (`docs/DECISIONS.md` D-088 to D-091). They are
implemented on the branch `feat/player-fixes`. A code review of that branch
then raised two more questions, which the owner decided too (D-093 for
section 1, D-094 for section 2). Each section ends with what was decided
and the commits that implement it. P-083 lists the choices the
implementation made beyond this plan.

Records: `plan/engineer_tools_runs/seek-2026-11-03/` (section 1);
`plan/engineer_tools_runs/gui-2026-11-03/` (sections 1 and 2);
`plan/engineer_tools_runs/investigate-2026-11-03/` (sections 3 and 4: the
programs agents ran against the code, read-only, with their outputs, which
the sections cite by their tags);
`plan/engineer_tools_runs/review-2026-11-05/` (D-093 and D-094). Line numbers are those of commit
`0b50883`.

## 1. A seek that lands after the keyframe before its target shows no frame

**What happens.** The MPEG-TS `tests/engineer_tools_snapshots.sh` makes by
joining two 3 s HEVC encodes has two keyframes, at 1.48 s and 4.48 s. A seek
into its second half after 4.48 s shows no frame, with VideoToolbox and
without: the startup watchdog reports the video decoder holding packets and
no frame, the demuxer at the end (`gui-2026-11-03/logs/seek_hw.log`,
`seek_sw.log`). FFmpeg's command line does the same: seeking there it
decodes no frame, seeking before 4.48 s it decodes from that keyframe on
(`seek-2026-11-03/`).

**Why.** `Demuxer::read_loop` seeks with `av_seek_frame(fmt_ctx_, -1, t,
AVSEEK_FLAG_BACKWARD)` (`src/Demuxer.cpp:505`). An MPEG-TS has no index:
FFmpeg's mpegts demuxer finds the position by searching the file for
timestamps and lands on a packet at or before `t` that need not be a
keyframe. Decoding starts at the first keyframe after the landing. With one
between the landing and `t`, the frames up to `t` are decoded and dropped
(`show_from`) and `t` is shown; with the next one after `t`, by the code that
later frame is shown (not measured); with none before the end, as here,
nothing is.

**Plan.** After a backward seek, the demuxer makes sure a video keyframe lies
between the landing and the target before the packets reach the decoders:

1. If `KeyframeIndex` knows for certain the last keyframe at or before the
   target (the stretch up to the target was read through), it seeks to that
   keyframe as keyframe steps do (`Keyframe::seek_s`).
2. Otherwise it seeks to the target and reads on, holding the packets back.
   A video keyframe at or before the target: the packets from it on go to
   the decoders. The target passed without one: it seeks again from
   earlier, 1 s before the target, then 2, 4, 8 … s, down to the media's
   start, and reads again.

Unchanged: the forward seek of keyframe steps (`key_after`,
`src/Demuxer.cpp:497-503`); HLS, whose backward seeks land on segment
starts, which are keyframes, so the check passes at once; inputs without
video. A track switch's repositioning (`src/Demuxer.cpp:431-436`) seeks
the same way and is left to a second step.

**Cost.** Packets read again, not decoded: a few extra seeks and reads where
the landing is wrong, nothing where it is right.

**Alternatives.** Index every keyframe at open, reading the whole input once
on a thread of its own (as B's `FrameScan` does), and seek from the index:
exact, but a full read before the first seek is certain, and not for
network inputs. Or leave it: the player then does what FFmpeg's command line
does.

**Risks.** The seek path serves every input: MP4 and MKV seeks, which land
on keyframes from the container's index, must stay as they are; held-back
packets make a seek on a slow input show its frame later; the startup
watchdog's timing.

**Check.** A unit test on `Demuxer` with a generated MPEG-TS like the one
above (a seek past its last keyframe but one gives a frame at or before the
target), and with H.264 at a long GOP (the frame shown is the target's);
the scripted seek of `gui-2026-11-03` then shows a frame.

**Decided and implemented.** The plan as written (D-088), in commit
`a7db00c`, merged as `6cc8f94`. Beyond the plan (P-083):

- HLS is left out of the check. Near a segment's start, a target can fall
  inside the B-frame delay, before the segment's first keyframe, and the
  check would then fetch the segment before it.
- Step 1's seek is checked too. On an MPEG-TS with B-frames, a seek to a
  keyframe's PTS lands after that keyframe, because its DTS is earlier.
- The last try seeks to a second before the media's start.
- A network read error gives the wait up, and a track switch's
  repositioning drops it.

After the code review, D-093 settled what happens when no keyframe is
flagged before a seek's target. FFmpeg flags only the first frame of an
H.264 MPEG-TS whose I-frames are neither IDR nor marked by a recovery point
SEI, so the tries go back to the media's start, and everything up to the
target is decoded. The owner first chose a cap at 16 s, then withdrew it.
With three references, FFmpeg's H.264 decoder makes no frame from such an
I-frame, only from an IDR, so a capped seek showed nothing
(`plan/engineer_tools_runs/review-2026-11-05/`). The code is unchanged.
`66751ed` adds a test of such a stream and says why in the landing's
comment.

Tests: `tests/test_demuxer_seek.cpp` (`[demux][seek]`). Scripted runs
(`plan/engineer_tools_runs/fixes-gui-2026-11-05/`): the seek of
`gui-2026-11-03` that showed no frame now shows the target's frame, with
and without VideoToolbox. On the stream without flagged keyframes, a seek
50 s in reads from the start and shows the target's frame, 1233 ms after
the seek with VideoToolbox and 135 ms after it with `--no-hw`. Found on the way: the scrub preview and the step back,
which `ScrubDecoder` serves, still land after the keyframe before their
target on an MPEG-TS.

## 2. Positions are absolute PTS against a duration counted from 0

**What happens.** On the MPEG-TS of section 1, whose start time is 1.48 s,
the control bar reads `00:07.040 / 00:06.000` near the end
(`gui-2026-11-03/snapshots/tagswitch_end.png`). By the code:

- the time and the playhead are the frame's absolute PTS against
  `[0, duration]`, the duration a length from the container's start
  (`src/Demuxer.cpp:1018-1022`; `src/UiFormat.cpp:39-45`,
  `src/UiWidgets.cpp:135-138`, `src/PlayerUI.cpp:366-369`);
- a click or a drag on the bar seeks to fraction × duration taken as an
  absolute PTS (`src/main.cpp:2096`, `2947`, `3586-3589`), which the
  demuxer clamps into the media (`src/Demuxer.cpp:487-495`): the start of
  the bar, up to the start time, seeks to the first frame, and the last
  start-time seconds cannot be reached from it;
- the timecode's frame mode and the frame-step toasts number frames
  `llround(pts × fps)` (`src/UiFormat.cpp:40`; `src/main.cpp:2571`, `2597`,
  `2647`), while the frame inspector counts from the video's start
  (`src/FrameInfo.cpp:281`);
- the Manifest tab and the timeline strip compare and seek with tag times
  counted from 0 against absolute PTS (`src/NetInspector.cpp:1197-1208`;
  `src/DebugHUD.cpp:612-643`, `src/main.cpp:3011`, `3199`), and the VMAF
  marker sits at absolute PTS over the duration (`src/main.cpp:3083-3084`,
  `3402-3403`).

An input whose start time is 0 shows none of this, as FFmpeg's MP4 with its
default edit list; MPEG-TS and HLS do (the code's own comment: HLS
timestamps can start later, "bipbop: 10 s", `src/Demuxer.cpp:481-484`).

**Plan.** Show positions from the stream's start, PTS − start; keep PTS
everywhere else: the demuxer, the decoders, the clocks and sync, the logs,
the HUD's and the frame inspector's PTS rows, B, the Alignment tab.

- `start` once per session in `MediaSession`, next to `video_start_s`: the
  video stream's start time, else the container's, else 0; 0 for live,
  whose display is the wall clock.
- Pure helpers in `src/UiFormat.*`, unit-tested: position ↔ PTS, and a frame
  number `llround((pts − start) × fps)`.
- Every place the UI shows a position, or turns a point on a timeline into
  a seek, goes through them: the control bar's time and playhead; the seek
  bar's click, drag, hover card, keyframe ticks and buffered range; the
  filmstrip; the seek, keyframe-step and frame-step toasts; the timecode's
  three modes; the VMAF marker; the Manifest tab and the timeline strip.
  About seven files: `src/UiFormat.*`, `src/UiWidgets.cpp`,
  `src/PlayerUI.*`, `src/main.cpp`, `src/ThumbnailStrip.cpp`,
  `src/NetInspector.*`.

**Owner's choice.**

- The origin: the video stream's start, so that the first frame is at 0 and
  numbered 0, as the frame inspector numbers it now (recommended); or the
  container's start, which `ffmpeg -ss` counts from, as do the duration and
  VMAF's per-frame times, and then the first frame can read a little after
  0.
- A PTS view: a fourth timecode mode showing the absolute PTS, one click
  away (recommended), or none.
- The QA flags (`--ui-sub-at`, `--speed-seq seek:` and `scrub:`): positions,
  as the bar shows them (recommended), or PTS, as now.

**Alternatives.** Absolute PTS everywhere: the bar spans
`[start, start + duration]` and its end reads start + duration; true to
ffprobe and the logs, unlike `ffmpeg -ss`, EXTINF and tag times, SRT files
and VMAF. Or only the frame numbers: the bar's scale stays as it is.

**Risks.** A seek target below 0 means "none" (`src/main.cpp:79`, `2076`;
`src/Demuxer.cpp:464`), so a conversion must never make one; thumbnails of
inputs without an index are sampled at other times; QA runs that give these
flags PTS (the status file's `--ui-sub-at 5.2`) land elsewhere if the flags
take positions. Seen on the way, not part of the fix: the seek bar's own
`seek_to` on release is swallowed (`src/UiWidgets.cpp:103`,
`src/main.cpp:3567`), and `src/main.cpp:2084-2092` is never reached.

**Check.** Unit tests on the helpers (the first and last frame of the
MPEG-TS above at 0 and 149, its end reading `00:05.960 / 00:06.000`);
scripted runs: a scrub on an MPEG-TS that reaches its last frame, the
frame-step toasts against the frame inspector's numbers, a snapshot of the
end of the clip.

**Decided and implemented.** Positions from the video stream's start, the
PTS mode, and positions for the QA flags (D-089), in commit `0fb839f`,
merged as `b6d1992`. Beyond the plan (P-083):

- The PTS mode shows seconds to the millisecond ("PTS 1.480"), with no
  duration after it.
- The thumbnails' decode thread works out its input's start itself,
  because it starts before the session has worked out its own.
- `--ui-sub-at` asks for an absolute seek, instead of a delta from the PTS
  on screen.

After the code review, D-094 changed the span of the seek bar and the
duration the UI shows. They run from the video's first frame to the
media's end, the container's start plus its duration (`63cde27`). The
container's duration counts from its earliest stream, often the audio. So
on FFmpeg's MPEG-TS of AAC and H.264, whose audio starts 0.021333 s before
its video (`plan/engineer_tools_runs/review-2026-11-05/`), the bar ran past
the end and the remaining time did not reach 0.

Tests: `tests/test_ui_format.cpp` (`[ui]`). Scripted runs
(`fixes-gui-2026-11-05/`): the end of the MPEG-TS reads 00:05.960 /
00:06.000, and so does that of a clip whose audio starts 0.541333 s first,
with the bar full. A step forward lands on the next frame. A step back on
the MPEG-TS lands on the next keyframe (section 1's finding). Since the QA flags now take positions, the status file's
`--ui-sub-at 5.2` (a PTS when it was run) is `--ui-sub-at 3.72` on that
clip.

## 3. Colour

The tags `[C1]` to `[C10]` are the sections of
`investigate-2026-11-03/render/results.txt`: a replica of the HW draw (an
offscreen GL context binding VideoToolbox's buffers as `upload_hw` does,
with the shaders read from `src/VideoRenderer.cpp` and the uniforms from
`src/YuvMatrix.h`), and probes of VideoToolbox and of sws, on generated
bars whose codes are known.

### 3.1 The HW path's limited-range constants are 8-bit ones, for 8-bit and 10-bit frames alike

**What happens.** `make_yuv_matrix` builds the limited-range scale and
offsets from 8-bit code values (16/255, 255/219, 255/224) and puts the
chroma zero at 0.5 (`src/YuvMatrix.h:52-62`, `78-80`); the matrix is
uploaded once (`src/VideoRenderer.cpp:409-411`). VideoToolbox's 10-bit
4:2:0 buffers ('x420', P010) hold code × 64 in 16-bit words, read as
normalised R16 / RG16 textures (`src/VideoRenderer.cpp:469-471`,
`497-499`), so 10-bit reference white, code 940, comes out at E′ 0.995838
instead of 1 ([C2], [C3]): SDR white shows as 254, not 255; on PQ, code 940
means 9610 cd/m² instead of 10000, up to 1.53 codes on screen after the
tone map. The comment at `src/YuvMatrix.h:18-19` says "<0.1%", which is the
full-range figure. On 8-bit buffers the chroma zero at 0.5 instead of
128/255 tints every grey by about a code (a grey that should read 128.082
on every channel reads R 128.979, G 127.709, B 129.138, [C2], [C9]); `tests/test_yuv_matrix.cpp:54-55` asserts the 0.5, and
`141-146` allows the tint. The software path (`--no-hw`, scrub frames,
codecs VideoToolbox doesn't decode) goes through sws, which with the
renderer's flags is off for 10-bit too: SDR white 253, an HDR white gain of
0.99615 ([C4]; FFmpeg's scale filter gives the same, [C5]).

**Plan.** `make_yuv_matrix(cs, range, container_bits, sample_bits)` with the
constants from the container's code values (limited range: black 16·u,
white 235·u, chroma zero 128·u, over 2^N − 1, with u = 2^(N−8)), and the HW
path uploading the matrix again when the buffer kind changes. The agent's
prototype makes 10-bit exact and the 8-bit greys neutral in the replica
([C9]); `tests/test_yuv_matrix.cpp:54` changes to 128/255.

**Owner's choice.** (a) The HW constants alone. (b) With them,
`SWS_FULL_CHR_H_INT` on the software path's SDR output, which makes 10-bit
exact there, at a cost per 1080p frame (3.08–3.81 ms without it,
6.34–7.33 ms with it, [C4]),
so that scrub frames and playback agree in SDR (recommended). No sws flag
corrects the software path's HDR output ([C10]). (c) Instead, convert
software frames on the GPU with the same matrix: a bigger change, which
also removes sws's CPU cost for 10-bit (4.82–6.29 ms per 1080p frame,
18.42–24.22 ms per 2160p frame to RGB48, [C4]).

**Risks.** Every HW pixel moves, by up to about a code in SDR and 1.53 in
HDR. With the HW path fixed alone, the gap between it and the software path
grows on 10-bit content (SDR white 255 against 253), which shows between
scrub frames and playback, and which `snapshot_diff`'s threshold would not
catch.

**Check.** Unit tests on `make_yuv_matrix` (P010 codes 64, 940 and 512 to
black, white and neutral; 8-bit Cb = Cr = 128 exactly neutral), as the
prototype's; snapshots of generated bars with and without `--no-hw`.

### 3.2 Colour tags are read once

**What happens.** The renderer is set up once, after open, with the tags the
stream declares (`src/MediaSession.cpp:526-531`): the HDR mode, the matrix,
the range, sws's destination and coefficients, the software and HW programs
and their uniforms (`src/VideoRenderer.cpp:301-357`, `379-445`). `upload()`
checks a frame's size and pixel format only
(`src/VideoRenderer.cpp:566-628`). In the tag-switch MPEG-TS, the frames
from 4.48 s are tagged PQ / BT.2020 while the stream's tags stay BT.709,
and the decoder context's tags change two frames before the frames' own
([C7]): the frames' tags, not the context's, are what to follow. The PQ
frames are drawn with the SDR program, as the frame inspector reports
(`gui-2026-11-03/snapshots/frame_inspector_tag_switch.png`). The same
codes show a grey of 129 drawn by the SDR program and of 54 by the PQ
program ([C2]: `bars_sdr10.mp4` and `bars_pq.mp4`, which decode to the same
frame, [CLIPS]). Calling `init()` again is not safe as written: it leaks the
converted frame, the programs, the VAO and the texture, and the HW path
keeps its first program and matrix (`src/VideoRenderer.cpp:381`).

**Plan.** At the top of `upload()`, the frame's interpretation (a specified
tag wins, an unspecified one keeps the current value) against the current
one; on a change, a new `reconfigure()`: the uniforms always, and when the
HDR mode flips, the software program, converted frame and texture, the sws
context and the HW program rebuilt; one log line, so the change is no
longer silent. Every upload is on the render thread between two draws.
Cost at a change ([C6]): each program's first build in a process
1.74–7.44 ms, a rebuild after it 1.01–2.51 ms, the uniforms alone with a
draw 0.313 ms.

**Owner's choice.** (a) `reconfigure()` on a change (recommended). (b) Both
program variants built at open, so that a change is a pick and its
uniforms: no compile at the switch, more at open. (c) Detect and report
only (log, HUD), the rendering unchanged. Separately, (d): a primaries
uniform, so that the SDR path converts BT.2020 SDR content's gamut too,
which changes how such content looks.

**Risks.** The frame inspector's "frame is X, rendered as Y" stops firing,
by design, and its wording, `src/VideoRenderer.h`'s comment and the status
file describe the behaviour at open; the I panel's lines from open go
stale; a scrub across a change flips the interpretation per scrub frame;
thumbnails stay unaware of tags.

**Check.** Pure-function tests of the resolver and of a change's kind (none,
uniforms, program: SDR to PQ a program, PQ to HLG uniforms, an untagged
frame nothing); snapshots of a generated switch clip against its second
part alone, with `snapshot_diff`, with and without `--no-hw`.

### 3.3 VideoToolbox's 4:2:2 and 4:4:4 10-bit buffers are bound as 8-bit textures

**What happens.** Only 'x420' and 'xf20' are bound as 16-bit
(`src/VideoRenderer.cpp:466-471`). VideoToolbox gives 'x422' for ProRes
422 HQ and HEVC 4:2:2 10-bit, 'x444' for HEVC 4:4:4 10-bit, 'sv44' for
ProRes 4444 and 'y416' for ProRes 4444 with alpha ([C1]). Their 2- and
4-byte planes bound as R8 / RG8 give a garbled picture, not a coarser one
([C2]). 'y416' has no second plane, so its bind fails, and the software
fallback is handed a VideoToolbox frame, which sws refuses: nothing is
drawn, and three lines are logged per frame (`src/VideoRenderer.cpp:548`,
`577`, `609`; [C2], [C4]), after a log line that said "8-bit … ready"
(`src/VideoRenderer.cpp:434-439`).

**Plan.** The binding from the planes' bytes per element (1 and 2: 8-bit,
2 and 4: 16-bit), with 3.1's container constants; any other buffer refused
before it is referenced, and for a refused buffer a real fallback:
`av_hwframe_transfer_data` to the CPU, then sws, which accepts every
VideoToolbox software format tried ([C1], [C8]). Bound as 16-bit, 'x422',
'x444' and 'sv44' render as P010 does ([C2]).

**Alternatives.** A table of fourccs, explicit but to maintain; for 'y416',
one RGBA16 texture and a packed shader variant ([C8]), or asking
VideoToolbox for P416, a decode-path change that drops the alpha.

**Risks.** Only the HW path changes; the fallback costs CPU per frame for
refused buffers (not measured).

**Check.** A table-driven test of the binding rule over the measured
(format, planes, bytes) tuples; snapshots of generated ProRes and HEVC
4:2:2 / 4:4:4 bars, HW against `--no-hw`.

Seen on the way: `--snapshot` writes a PNG (`src/main.cpp:2835`) where
`src/main.cpp:541` says PPM.

**Decided and implemented.** 3.1 (b), 3.2 (a) and 3.3 (D-090), in commits
`c28bcfe`, `0068b00` and `2eb443e`, merged as `46449d8`, and `39ad661`.
Beyond the plan (P-083):

- The pure parts live in GL-free headers, so they can be unit-tested:
  `src/YuvMatrix.h` holds the constants and the binding rule,
  `src/ColourInterpretation.h` the tags followed and the kind of a change.
- sws is rebuilt on every change of interpretation, not only on an SDR/HDR
  flip, because `--no-hw` and scrub frames need its new coefficients.
- The samples' depth comes from the frames' `AVHWFramesContext::sw_format`.
- A buffer without an IOSurface takes the same CPU copy as a refused one.
- A matrix that no tag gives is kept across frame sizes, and derived again
  only when the HDR state flips (`2eb443e`, after review). The first version
  derived it from each frame's size, so an untagged ladder switched between
  BT.601 and BT.709 from one variant to the next.
- The frame inspector no longer says that the renderer reads the tags at
  open (`0068b00`).
- The log prints a pixel format's unprintable bytes as '.' (`39ad661`,
  after a code review of the merged branch).

Still open: a `reconfigure()` that fails (a shader that does not link, no
memory for the staging frame) is not tried again, and frames can go
undrawn until the interpretation changes again. Nothing falls back at
`init()` either.

Tests: `[yuv]`, `[hw_binding]` and `[colour_interp]`. Snapshots
(`fixes-gui-2026-11-05/`): 10-bit white is 255 and the 8-bit 4:2:0 greys
are neutral. Every buffer above is drawn, and 'y416' is converted on the
CPU. After their switch, the tag-switch clips equal their second part
alone. The bars' centres with and without VideoToolbox are equal, but for
HDR, which the software path does not correct (3.1), and for 8-bit 4:2:2
and 4:4:4: one code higher with VideoToolbox, not explained yet. As the
risks said, the frame inspector's "rendered as"
warnings no longer fire for the frame on screen, and the I panel's colour
lines are still those of open.

## 4. Decode errors, the stereo downmix, the W strip

### 4.1 Decode errors are counted nowhere, and VideoToolbox loses a GOP to one damaged packet

**What happens.** `Decoder::push` drops `avcodec_send_packet`'s result
(`src/Decoder.cpp:151-154`) and `pull` turns every error into "no frame"
(`src/Decoder.cpp:163`). FFmpeg's own messages reach stderr
(`src/NetworkLogger.cpp:208-212`); nothing counts them, nor shows them in
the HUD, the I panel, `--av-report` or the startup watchdog, which then
says the decoder "waits for more input" (`src/PipelineState.cpp:94-101`).
The frame inspector shows a frame's `decode_error_flags`
(`src/FrameInfo.cpp:110-121`) for the frame on screen only. Measured with
the player's decoder set-ups on a generated H.264 clip with damaged packets
(`investigate-2026-11-03`, 1a): in software a damaged packet is rejected
or concealed and the next frames decode; under VideoToolbox one damaged
P-frame made every packet fail up to the next keyframe and past it (about a
GOP of frames lost), with no error shown. FFmpeg's command line on the same
file reports the errors and, with VideoToolbox, stops on its error rate
(1b). With frame threads, an error in the drain at the end of the stream
ends it early and its last frames are lost (`src/MediaSession.cpp:128-135`,
`149-155`; 1a).

**Plan.** Inside `Decoder`, callers unchanged: `push` returns and counts
errors (not EAGAIN, nor EOF after a flush); `pull` counts an error and
receives again, bounded, as FFmpeg's command line does, which also keeps
the drain's last frames; counters (errors, the last one, frames flagged
corrupt, a VideoToolbox error streak) as atomics. Shown in the I panel's
Decode line (`src/main.cpp:2879-2884`), `--av-report`
(`src/main.cpp:3713-3727`) and the pipeline snapshot the watchdog reads
(`src/PipelineState.h:36-43`), with one rate-limited log line.

**Owner's choice: VideoToolbox after errors.** (1) No automatic fallback:
show the errors, suggest `--no-hw`. (2) After N failures in a row, reopen
in software for the rest of the session; frames come back at the next
keyframe (the renderer already takes VideoToolbox and software frames
alike, `src/VideoRenderer.cpp:566-613`). (3) As (2), and decode the current
GOP again in software. (4) Software until the next keyframe, then
VideoToolbox again. One damaged packet made a run of failures longer than
the clip's GOP (1a), so N decides between "any damage" and "lasting
failure". The agent's recommendation: (1) for now, the errors shown with
`--no-hw` named next to them, since for an engineer's player seeing the
failure matters more than hiding it; (2) with N above a GOP later, if
lasting failures turn up.

**Risks.** Errors arrive late with frame threads, so counts and the last
error, not "this packet"; fewer frames than packets is not an error count
(skipped and dropped frames); a fallback costs software decoding and the
software render path's `sws_scale` on the render thread, at 4K or at 2–4×
speed (not measured), and loses the zero-copy path.

**Check.** A unit test on `Decoder` with packets made unusable on purpose
(software): the error count equals the packets damaged, and the drain at
the end gives every frame; a VideoToolbox test tagged so the headless build
skips it; a scripted run with `--av-report` on a damaged file, with and
without `--no-hw`, against FFmpeg's counts.

### 4.2 A decoder that fails to reopen leaves a null context

**What happens.** `Decoder::open` leaves the codec context null when no
decoder is found (`src/Decoder.cpp:59`, `86-89`); the result of a reopen
after a track switch is not checked (`src/MediaSession.cpp:175` for video,
`275` for audio); `push`, `pull` and `flush` then pass the null context to
FFmpeg, which crashes on each (1c, FFmpeg's calls on a null context, not a
player run). FFmpeg 8.1.1 as installed has no decoder for AC-4 or MPEG-H 3D
Audio (`supplement-2026-11-03.txt`), so a track switch to one would be such
a case.

**Plan.** `push`, `pull` and `flush` do nothing on a null context; the
switch reports the failure (the track stays silent or blank, and the
Tracks panel says why). Part of 4.1's change.

**Check.** A unit test: `Decoder::open` on a codec it has no decoder for
returns false, and `push`, `pull` and `flush` then return without a crash.

### 4.3 The stereo downmix drops the LFE

**What happens.** `AudioDecoder` builds its resampler with
`swr_alloc_set_opts2` and no options (`src/AudioDecoder.cpp:34-43`, `49`),
so FFmpeg's defaults apply: centre and surrounds at 0.707107 (−3 dB), the
LFE at 0 (2a). Through the player's own `AudioDecoder` and `AudioPlayer`,
an LFE-only tone comes out silent, from PCM and from AAC (2c). The output
is not normalised: channels in phase can sum above full scale (2a). The
downmix levels an AC-3 or E-AC-3 stream carries (its downmix side data) are
not read: FFmpeg's `aresample` applies them, the player does not (2b).
A trap for whoever sets the levels: the installed `ffmpeg-resampler(1)`
gives them in dB (`supplement-2026-11-03.txt`), while the library takes
linear factors, so an `lfe_mix_level` of −10 meant as dB gives the LFE a
coefficient of −7.07 (2a).

**Owner's choice.** (A) Keep the LFE out, as FFmpeg's defaults and its AC-3
decoder's own downmix do (2b), and say so where the output is described.
(B) Mix it in at a fixed level, e.g. −10 dB per side (a linear 0.316228,
set as `lfe_mix_level` 0.447214 against the matrix's 0.707107; 2a).
(C) Honour the stream: build the resampler from the first decoded frame,
with the downmix side data where there is one, else the defaults, and
rebuild when they change. (D) A control for it (an LFE switch, or channel
solo, `plan/engineer_tools_ideas.md`). The agent's recommendation: (C) for
the stream's levels, with (A) for the LFE unless a stream asks for it.
Normalisation, separately: as now (overs possible), `rematrix_maxval` 1
(the fronts lower, 2a), or a limiter.

**Risks.** The loudness meters measure before the resampler, so none of this
changes the L panel; a rebuild in mid-stream drops the resampler's delay
line unless it is drained first (size not measured).

**Check.** A unit test feeding 5.1 PCM through `AudioDecoder` and
`AudioPlayer`: LFE-only and centre-only levels at the output, as the
agents' program did (2c); for (C), an AC-3 file against `aresample`'s
levels (2b).

### 4.4 The W strip shows the left channel, and its comment is wrong

**What happens.** The strip keeps the peaks of the left channel of what the
audio player is given, after the downmix and the speed change
(`src/AudioPlayer.cpp:178`): a right-only signal leaves it empty, a
centre-only one shows the downmix's level, and at 2× it covers about 2 s of
media (2c). Its buffer comment says "≈3.6 s"; 1,200 entries of 40 samples
at 48 kHz are 1.0 s (`src/AudioPlayer.h:103`). Peaks are taken when audio
is pushed, ahead of what is heard (by up to the player's ring buffer and the
device latency, from the code), before the volume, and clamped to full
scale when drawn (`src/DebugHUD.cpp:445`).

**Plan.** The comment corrected (1.0 s of output, speed × that of media) and
the peak taken over both channels at `src/AudioPlayer.cpp:178`.
Alternatives: left up and right down from two buffers; peaks taken in the
audio callback, which matches what is heard and the volume, at a cost on
the real-time thread; a longer buffer.

**Check.** A unit test on `AudioPlayer`: a ramp's oldest entry 1.0 s before
its newest, and a right-only signal no longer empty.

**Decided and implemented.** Every recommendation of section 4 (D-091):

- 4.1, option (1);
- 4.2;
- 4.3, (C), with (A) for the LFE unless a stream gives its level, and the
  output not normalised;
- 4.4.

The commits:

| Commit | What |
|---|---|
| `7211671` | 4.1 and 4.2 |
| `defba3f` | 4.3 |
| `07dce07` | 4.4 |
| `651193e`, `e9ac2c9` | 4.2 |
| `05165ea` | 4.1's VideoToolbox test |
| `12105b8` | the merge of the six above |
| `cfd6ae0` | 4.2 |
| `3b4e646` | 4.2, after a code review of the merged branch |

Beyond the plan (P-083):

- A stream that prefers an Lt/Rt or Pro Logic II downmix gets those levels
  and swr's matrix encoding, as `aresample` does.
- After an error, a pull asks the codec again up to 64 times
  (`Decoder::kMaxErrorsPerPull`).
- The counts carry over a track switch's reopen. Those of `--av-report`
  count from when the media opened.
- An audio track with no decoder plays its packets' time as silence
  (`e9ac2c9`), so the audio clock, which video follows, keeps going.
- On a video track with no decoder, a seek does not wait for a frame, and
  the stall check does not say "Buffering" (`cfd6ae0`).
- A frame no resampler can be made for plays its time as silence too
  (`3b4e646`). Since 4.3 the resampler is made from the first decoded
  frame, and such a frame was dropped, which stopped the audio clock.
- The VideoToolbox test is compiled out of the headless build
  (`MIRAGE_HEADLESS`), because both builds run the same `'~[audio]'`.

Still open: when a stream gives an LFE level, it is passed to swr as
`aresample` passes it. swr then mixes the LFE into each side at that level
× 0.707107 (2a). No FFmpeg encoder writes such a level, so this is
untested.

Tests:

- `[decoder]` (`tests/test_decoder.cpp`; its VideoToolbox case is tagged
  `[decoder][videotoolbox]`);
- `tests/test_pipeline_state.cpp`;
- `[audio]` in `tests/test_audio_decoder.cpp` and
  `tests/test_audio_player.cpp`.

Scripted runs (`fixes-gui-2026-11-05/`): 4.1 counts and shows decode
errors as FFmpeg's command line does (82 with VideoToolbox). A switch to
an audio or a video track with no decoder ends at once, the Tracks panel
says so, and the audio clock goes on. The W strip shows a right-only
signal.

## 5. Found by the scripted checks

The scripted checks of sections 1 to 4
(`plan/engineer_tools_runs/fixes-gui-2026-11-05/`) found four things:
three bugs (5.1, 5.3, 5.4) and a difference that is no bug (5.2). Under
D-095 the agent plans such fixes, audits the plan and makes them itself.
5.1 changes the decode path, so it is planned here, as D-007 asks.

### 5.1 On an MPEG-TS, the scrub preview and the step back land after the keyframe before their target

**What happens.** `ScrubDecoder` serves the scrub preview and the step
back (`src/main.cpp:2636-2668`) with a seek of its own:
`avformat_seek_file` with the target as its largest timestamp
(`src/ScrubDecoder.cpp:492-507`). On an MPEG-TS this lands on a packet at or
before the target, keyframe or not, as playback's seek did before D-088.
Decoding then starts at the next keyframe, and the first frame at or past
the target is posted (`:575-598`). Its other way in is a keyframe that
KeyframeIndex knows (`:515-521`), but only when that keyframe lies within
2.5 s of the target or the index is trusted, and a miss there falls back
to the same seek (`:579-590`).

In the tag-switch MPEG-TS, whose keyframes are at PTS 1.48 and 4.48:

- a scrub to PTS 2.68 shows 4.480;
- a scrub to PTS 5.68 shows nothing, and so does one to the end;
- a step back from 3.080 lands on 4.480.

In an MP4, whose index gives the keyframe, the preview is the target's
frame (`record2.txt`).

**Plan.** D-088's landing check, in `ScrubDecoder`'s request.

- After the seek, packets are read but not decoded until the stream gives
  a keyframe at or before the seek's target, with half a frame of
  tolerance. The target is the request's, kept inside the media (`seek_pts`,
  `src/ScrubDecoder.cpp:487-488`). That keyframe and what follows go to the
  decoder.
- Two things show the keyframe was missed: a packet whose dts (its pts when
  it has none) lies past that target, or the end of the media. Either way
  the request seeks again from further back: 1, 2, 4 … s before where the
  first try sought.
- From the media's start, a second before it (or start_time itself, when
  the demuxer refuses that), it reads on without waiting, with no cap
  (D-093).
- A try whose seek fails gives up the wait and reads on, as D-088 does.
- The miss of the KeyframeIndex way in goes through the same check.
- Every packet read still goes to KeyframeIndex (`report()`), and a newer
  request still stops the work at the next packet.
- HLS is left out, as in D-088, because its seeks land on segment
  starts. An input is HLS when its input format's name is "hls", as
  `Demuxer::is_hls_` tells (`src/Demuxer.cpp:1109`).

**Risks.** On an MPEG-TS each try reads the packets up to the target. A
preview in a stream with no flagged keyframe before its target decodes from
the start, as playback does since D-093. A drag's newer requests stop that
work, so the preview comes once the drag stops.

**Check.** Unit tests in `tests/test_demuxer_seek.cpp`: `ScrubDecoder` on
the joined HEVC MPEG-TS (a request in its last GOP), on H.264 in MPEG-TS
with a long GOP (a request 2 s after a keyframe), and on MP4 (one seek, as
before), each giving the target's frame, and a request at the end of the
MPEG-TS giving its last frame. The scripted runs `scrub_gop1`,
`scrub_gop2`, `scrub_end` and `steps_back` again.

### 5.2 The 8-bit 4:2:2 and 4:4:4 bars, one code apart: no bug

**What happens.** `fixes-gui-2026-11-05/` reads bar 2 of the 8-bit 4:2:2
and 4:4:4 clips as 130 with VideoToolbox and as 129 without, through
`bars_at.py`, which reads one pixel per bar. Its README calls this not
explained yet.

**Why.** The clips' bar 2 is not one code. They were made in 8 bits from
the 10-bit bars (`investigate-2026-11-03/render/results.txt`, [CLIPS]). Y
509, which is 127.25 in 8-bit codes, became an ordered dither: of bar 2's
luma samples over every row, 5376 are 127 and 1792 are 128, as decoded with
and without VideoToolbox alike. The 8-bit 4:2:0 clip, made from 8-bit raw
frames, holds 126 throughout, and the 10-bit 4:2:2 one 509 (the check below).

The two paths then filter in a different order, and both are right. The
HW path mixes neighbouring samples before converting them, so between a
127 row and a 128 one it gives 129.25 to 129.83, which round to 129 or 130
from one place to the next. The software path converts each sample first
(129 or 130), then filters the RGB. The agent's first reading, that the
GPU's filtering was inexact, rested on rows it had not looked at, and its
scratch fix, filtering in the shader, changed nothing.

**Plan.** No change to the code. The README's "not explained yet" and
P-083's open item are corrected.

**Check.** The luma codes of bar 2 over every row of each clip's first
frame, decoded with FFmpeg's command line, in the fix's record.

### 5.3 The I panel's long lines are cut short

**What happens.** DebugHUD's text drawing gives stb_easy_font a vertex
buffer of 16384 bytes (`src/DebugHUD.cpp:862`). stb_easy_font truncates
when its buffer is too small, and "expect it to use an average of ~270
bytes per character" (`stb_easy_font.h:51-52`). A longer line stops where
the buffer is full, while the panel is sized for the whole line
(`stb_easy_font_width`). The Decode line with VideoToolbox's errors stops
at "when processin" (`noise_I.png`), with the panel wider than the text.
The record's README first called this a cut at the panel's edge.

Measuring has a fault of its own. The panels measure the raw text with
`stb_easy_font_width` (`src/DebugHUD.cpp:158`, `212`, `755`, `777`), which
indexes its glyph table with c − 32 for any byte, as drawing does. Drawing
maps non-ASCII to '?' first (`:865-869`), but measuring does not. The I
panel's Dynamic line can read "HDR (PQ → tonemap)"
(`src/MediaSession.cpp:729-730`), whose '→' reads past the table.

**Plan.**

- A buffer that grows to the line's length times 1024 bytes, kept from one
  call to the next. No glyph takes more than 11 quads of 64 bytes
  (`stb_easy_font_charinfo`, 'X').
- Measuring and drawing take the same text, from one helper. Each UTF-8
  character becomes one ASCII stand-in: '->' for '→', '-' for '—' and '–',
  '.' for '·', 'x' for '×', and '?' for anything else. Drawing used to make
  a '?' of each byte, so the Dynamic line read "HDR (PQ ??? tonemap)".

**Check.** `noise_I.png` again: the whole Decode line. An HDR clip's I
panel: the panel as wide as its widest line, and its Dynamic line readable.

### 5.4 After a switch to a video track with no decoder, the badges mix two tracks

**What happens.** The codec chip comes from the stream now selected
(EVC). The size, frame-rate and range chips come from what was last on
screen: the old track's 1280×720, the frame rate shown before, and the
renderer's mode (`shown_w`, `stream_fps`, `renderer.hdr_mode()`,
`src/main.cpp:3150-3154`). The decode chip says "SW" (`:3165`).

**Plan.** On a blank video track, the one `MediaSession::video_open_failed`
names:

- the size, frame-rate and range chips show what the track declares (its
  codec parameters' size, its stream's frame rate, its transfer);
- the decode chip shows "no decoder".

**Check.** `evc_tracks.png` again.

**Implemented** under D-095 (P-085): 5.1 in `9b8f04a`, 5.3 in `7d3ee19` and
`380b5fd`, 5.4 in `ec2e8e6`; 5.2 needs no code. The record of the checks
is `plan/engineer_tools_runs/fixes2-2026-11-05/`:

- 5.1: the scrub to PTS 2.68 now shows 2.680, the one to 5.68 shows 5.680,
  and the one to the end shows the last frame. The step back from 3.080
  lands on 3.040.
- 5.2: the dither of bar 2, the same decoded both ways.
- 5.3: the whole Decode line, and "HDR (PQ -> tonemap)".
- 5.4: the badges "EVC", "640×360", "30", "SDR", "no decoder".

The unit tests: `plan/engineer_tools_runs/tests/macos-2026-11-05-57c3a10.txt`.

## The owner's choices, together

1. Section 1: the plan as written (recommended), the full index at open, or
   leave it. Decided: the plan as written (D-088).
2. Section 2: the plan, its origin, the PTS mode and the QA flags. Decided:
   the plan, with the video stream's start as the origin, the PTS mode and
   positions for the QA flags (D-089).
3. Section 3: 3.1 (a) to (c), 3.2 (a) to (d). Decided: 3.1 (b), 3.2 (a) and
   3.3 (D-090).
4. Section 4.1: VideoToolbox after errors, (1) to (4), and N. Decided: (1)
   (D-091).
5. Section 4.3: the LFE, (A) to (D), and the normalisation. Decided: (C),
   with (A) for the LFE unless a stream asks for it; the normalisation as
   now (D-091).
6. Sections 4.2 and 4.4 have no choice in them beyond yes or no. Decided:
   yes (D-091).
7. After the code review, section 1: a cap on the tries. First: at 16 s,
   at 16 s or the longest keyframe interval seen, or none. Decided: at
   16 s. Then, with the agent's description of it corrected: none, the cap
   with the decoder's `output_corrupt`, or the cap as it was. Decided: none
   (D-093).
8. After the code review, section 2: the duration shown and the bar's span,
   to the media's end or the container's duration as before. Decided: to
   the media's end (D-094).
