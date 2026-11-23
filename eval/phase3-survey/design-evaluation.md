# Phase 3 design, evaluation first: the unified event timeline (§3)

A design for the work order's §3 (Phase 3), written backwards from what the paper's evaluation and §3.2's two acceptance tests have to show. Nothing of Phase 3 is implemented. The design rests on the five surveys in `<out>` (`sources.md`, `scte35.md` with `scte35_vectors.json`, `testgen.md` with `testgen/`, `schema.md`, `ui.md`), on the code of `vmaf/phase-3` at `75cf6e0` (read only; `design-evaluation/runs/01_git_state.txt`: clean), and on twelve runs made for this design.

**Conventions.**

- `<repo>` is the worktree of `vmaf/phase-3` at `75cf6e0`; a `file:line` without a prefix is in it. `<out>` is the survey directory; `<scratch>` the session's scratch directory. The five surveys are cited by file name, with their own sections or runs.
- **Runs.** This design's runs are in `<out>/design-evaluation/runs/NN_name.txt`, each with its exact command, the directory it ran in, start and end (UTC), exit status, and full stdout and stderr, local paths replaced by the placeholders above (`design-evaluation/tools/run.py`). The tools are in `<out>/design-evaluation/tools/`: two C++ probes on FFmpeg 8.1.1's library API (no `ffmpeg` or `ffprobe` binary was run; the binaries were deleted after the runs) and Python 3 standard-library scripts. They read the testgen prototype's media and reports (`testgen/media/`, `testgen/reports/`) and change nothing outside `<out>/design-evaluation/`. They are prototypes for checking the design, not repository code. Appendix A lists the runs.
- **Numbers.** Every number about this code, these streams or a run comes from a run named next to it or from a cited source. Where there is neither: MEASUREMENT PENDING. Parameters the prototypes ran with (thresholds, windows, the session's plan, the server's delay) are the prototypes' and are named as such; none is a default.
- **Decisions are the owner's.** Every design choice is listed in §12 as an owner question with its options and trade-offs; a preference of the agent's is labelled "Agent's recommendation". Implementation details that don't need the owner are P-entries (§13), the agent's process choices pending the owner's confirmation. Nothing here is decided.
- "The work order" is the owner's document as translated in the task. D-0xx and P-0xx are entries of `docs/DECISIONS.md`.

---

## 0. Summary

- **What the evaluation must show** (§1): attribution that is right on known causes (D-069's two synthetic ladders, then D-011's ladders), the work order's composite answer (a drop that overlapped an SCTE-35 cue-out, a switch between two numbered rungs, and a segment request of 2.3 s) from a scripted session with measured HTTP timing, and reproduction from one command. Every attribution must be checked against ground truth that the test content's generator writes, not against the tool's own output.
- **The smallest system that shows it** (§3): the ladder report as it is (D-068: scored once), each scored media playlist read and parsed for its segments and tags, a demux pass per playlist (library API, no decoding) that places every scored frame in its segment, one axis on the reference's timeline, events from five sources, a detector, an overlap query, a document per session that names the ladder report by hash, and two `mirage_vmaf` subcommands. SCTE-35's binary decoding (D-070), the UI (§3.3) and the player's session log come after the critical path.
- **Checked by prototype runs on the testgen ladders** (§2):
  - The frame → segment join by position from a demux pass agrees with each frame's PTS for 200 of 200 frames on each of the 15 playlists checked, including the ad ladders, where 100 of the 200 frames have a PTS that two segments share (run 04).
  - A trailing-median detector recovers both acceptance drops exactly, with no drop in their controls, for every threshold from 3 to 40 points and every minimum length from 1 to 24 frames, at each window tested (10, 25, 50 and 100 frames) (run 05). The acceptance tests therefore need not depend on the defaults the owner picks.
  - A fetch of a scripted session's segments with FFmpeg's avio measured 2.313 s for a segment the local server answered 2.3 s late (the work order's example value), and took its status line from FFmpeg's own log line (run 06).
  - End to end, the prototype returned segment 4 of `v270` for the degraded drop, the `EXT-X-CUE-OUT` for the ad drop, and for the scripted session the cue-out, the switch from entry 1 to entry 2 and the 2.313 s segment request, each at +0.000 s from the drop's start (run 10).
  - Two pitfalls the design must avoid were measured: on the raw PTS axis, 50 of the ad ladder's PTS values name two frames; on the continuous-PTS axis, the 2 s ad break spans 5.000 s (run 09). Seconds as doubles listed boundary events inconsistently, and ticks didn't (runs 07, 08).
- **Owner questions** (§12): 40, of which these block the most work: Q01 (where the timeline goes), Q02 (the axis), Q17 (where a scripted session's HTTP timing comes from), Q28 (the subcommands), Q08 to Q11 (the detection), Q04 (the document's layout), Q32 and Q33 (the test content).
- **Work packages** (§14): 17 packages. A skeleton package that owns `CMakeLists.txt` comes first. The library packages WP01 to WP08 then run largely in parallel, each on new files, followed by the CLI, the acceptance tests, the evaluation harness, the UI and the documents. Two packages (WP15, WP16) are conditional or later, and wait on owner questions.

---

## 1. What the paper's evaluation must show, and what that requires

The paper claims one contribution: a timeline that joins per-frame VMAF to the events that explain it (the work order's §3). A reviewer will believe the claim if the experiments below hold. The table works backwards from each to what the system must provide.

| | Experiment | What makes it credible | What the system must provide |
|---|---|---|---|
| E1 | **Attribution on known causes.** (1) A ladder with one segment of one rung encoded at a low quantizer: the query answers with that segment. (2) A ladder with a low-quality ad between `EXT-X-DISCONTINUITY` tags, with `EXT-X-CUE-OUT`/`-CUE-IN`: the query returns the cue-out (D-069). | Ground truth from the generator's own layout, not from the tool. A control rung with no defect, whose curve gives no drop. Assertions on what doesn't depend on the axis chosen: the reference frames of the drop, the segment's URI and media sequence number, the event types, and the offsets. Explicit detection parameters, in the measured region. Two encoders. The hard timestamp case: an ad whose PTS repeat the main content's. | Frames → segments for every scored input; playlist tags at their segments; the plain cue tags; one axis; detection; the overlap query; the CLI. |
| E2 | **The work order's composite answer.** A scripted session (D-068) that switches down into the ad and fetches one of its segments slowly: the drop overlaps a cue-out, a switch from one rung to another, and a segment fetch of a measured duration. | The HTTP timing is **measured** by a fetch of the session's own segments, not declared in the plan (a declared value would make the answer circular). The switch comes from the plan, as D-068 says. The output names each rung as the master does. | Sessions: spans, the session's curve (D-068), switch events, a session fetcher with HTTP timing. |
| E3 | **Real content** (later). D-011's ladders, picked by the owner, with a degradation and a stitched ad injected as in E1. A public SCTE-35 VOD playlist (`scte35.md` §8: Unified's Remix AVOD demo) for parsing and placement only. | Many injection lengths and strengths. Detection hits and false alarms counted against what was injected. How many unrelated events an attribution lists. | The same system, at the scale of real ladders. Its costs: MEASUREMENT PENDING. |
| E4 | **Reproduction from one command.** | One script generates the synthetic ladders, scores them, builds each session's document, queries it, and checks the answers against the generator's layout. Wall-clock fields are excluded from the comparison; everything else is compared exactly. | Deterministic content (`testgen.md` §3.3: 29 of 29 files the same in two generations, on one machine), a deterministic query, and an eval script. |
| E5 | **Cost.** The document's size, the time to build and to query, and the UI's load time on real ladders. | Measured on D-011's ladders. | MEASUREMENT PENDING. Only the prototype's documents for 200-frame ladders are measured: 8577 to 14470 bytes (run 11). |

Two consequences shape everything below:

1. **Segments are the join unit.** Every non-VMAF source in E1 and E2 is per segment: playlist tags apply to the segment whose URI follows them, HTTP requests fetch segments, and a scripted switch happens at a segment boundary. So the critical step is placing each scored frame in its segment, for every input, including when PTS repeat across a discontinuity. Run 04 checks that step.
2. **The tests must not depend on choices that are still open.** The acceptance tests pin their own detection parameters inside the region run 05 measured, and they assert facts that hold under every axis option of Q02 (P-entry P-J).

---

## 2. What the prototype runs established

The runs read the testgen prototype's ladders (`testgen.md` §2.3: 3 rungs, 640x360, 480x270 and 320x180; 8 segments of 25 frames at 25 fps; MPEG-TS) and its Phase 2 reports (`testgen/reports/`; top-rung mode, reference `v360`).

### 2.1 Frames → segments by a demux pass (runs 03, 04)

- **The pass.** `tools/segmap.cpp` opens a media playlist as the analyzer opens an HLS input, with `http_persistent` and `http_multiple` off (`src/VMAFSegments.h:35-40`). It records each file the hls demuxer opens through `io_open`, and that file's `bytes_read` at `io_close2`. It reads every packet with `av_read_frame`, without decoding. Each packet is placed in a segment by its byte position (`AVPacket::pos`) against the segment files' cumulative bytes in open order, which is `sources.md`'s choice 2(a).
- **What it found** (run 03; 15 playlists, every rung of five ladders): 200 video packets in each playlist, 25 in each segment, 1 key packet per segment, no corrupt packet, and no packet left unplaced.
- **The join** (run 04): frame *i* of an input (`ref_index` or `dis_index` in the report) is the *i*-th packet in presentation order: each segment's packets sorted by PTS, the segments in the order they were read. The PTS that the report gives the frame then matches its packet's PTS, to the tick, for 200 of 200 frames in every pair, on both inputs, in all five ladders.
- **Why position is needed.** In `ad-x264-a` and `ad-mpeg2-a`, 100 of the 200 frames have a PTS that two segments of the same input share, because the ad restarts at the main content's origin. A PTS alone can't name their segment, and the position does. In `ad-x264-origin1` and the degraded ladders no PTS is shared (run 04).
- **Limits.** The join assumes one packet per presented frame and segments that start with a key frame. It was checked on MPEG-TS from one generator only. fMP4, open GOPs, decoders that drop frames, and D-011's real ladders: MEASUREMENT PENDING (as `sources.md` §12 says for byte positions). A mismatch is detected by the PTS check; what follows from one is P-B.

### 2.2 Detection: the region where both acceptance cases pass (run 05)

`tools/sweep.py` runs three trailing-baseline detectors over 21 pair curves of the testgen reports. A frame is low when its score is more than T points below the baseline; a drop is a run of more than N low frames, the work order's "for more than N frames". Ground truth comes from each ladder's `layout.json`: the frames of the segments whose quantizer for that rung is above the normal one.

- **The four acceptance curves** (degraded `v270` and its control `v180`; ad `v180` and its control `v270`), with the baseline as the median of the last W scores that weren't low (testgen's `tables.py` prototype): every T from 3 to 40 and every N from 1 to 24 gives both drops exactly and no drop in either control, at W = 10, 25, 50 and 100. N = 0 also passes, except at the lowest thresholds (T ≤ 6 at W = 10, 25 and 100; T ≤ 5 at W = 50). N ≥ 30 misses the degraded case's 25-frame drop. At T = 50 both drops are found but not exactly, and N = 24 then misses one.
- **All 21 curves** (adding MPEG-2, mezzanine mode, the ad at origin 1 s, unmarked continuity counters, the busy ad, and two controls at the normal quantizer): at T = 6 and W = 50, every N from 3 to 24 passes everywhere. With N = 1 or 2 the busy ad's control (`ad-x264-busy-control`, `v180`) gives false drops: runs of 1 to 3 low frames, such as frames 88-90. With N = 0, four more controls give single-frame drops (below). At T = 40 and 50 some target is missed (the MPEG-2 degraded drop's magnitude is 37.939, run 10).
- **Single-frame dips at frame 75**, the first frame of a libvmaf context (D-037), appear in four control curves at N = 0 (run 05, last list). Any N ≥ 1 passes over them.
- **A baseline that takes the low frames in** (the median of the last W scores, low or not) follows a long drop down. At W = 10 to 50 the drops it finds are shorter than the truth, and it misses them altogether from N = 8 on at W = 10 and from N = 16 on at W = 25. Only at W = 100 are they exact.
- **What this does and doesn't show.** It shows that the acceptance tests can pin parameters with a wide margin. It doesn't measure good defaults: the content is synthetic, from one source picture, with 200 frames per pair. Q11 says which measurement would.

### 2.3 Which axis (run 09)

The same reference frames of `ad-x264-a`, on the three axes the prototype computes (run 09):

| frame | raw PTS (s) | continuous PTS (s) | reference playlist time (s) |
|---|---|---|---|
| 0 | 10.000 | 10.000 | 0.000 |
| 74 | 12.960 | 12.960 | 2.960 |
| 75 (the ad's first) | 10.000 | 13.000 | 3.000 |
| 124 (the ad's last) | 11.960 | 14.960 | 4.960 |
| 125 | 15.000 | 18.000 | 5.000 |
| 199 | 17.960 | 20.960 | 7.960 |

- **Raw PTS.** 50 PTS values name two frames each, and the time goes back by 2.960 s at frame 75.
- **Continuous PTS** (the report's discontinuity sequences laid end to end, `ui.md` §2.4 (c)): unique, but it keeps the forward jump of 3.040 s between frames 124 and 125. That jump is under the aligner's 10 s threshold (`src/VMAFAlign.h:114`), so it isn't a discontinuity. The ad break from frame 75 to frame 125 therefore spans 5.000 s there, for an ad that `EXT-X-CUE-OUT:2.000` declares as 2 s.
- **Reference playlist time** (each frame's segment start, the sum of the EXTINF before it, plus the frame's PTS minus the PTS of its segment's first frame): the ad break spans 2.000 s, and the drop is 3.000 to 5.000 s.

### 2.4 Seconds as doubles at boundaries (runs 07, 08)

- **With doubles** (run 07): the prototype first compared the report's PTS as the doubles the report writes. The cue-in at the drop's exact end (+2.000 s) was then listed for `ad-x264` and `ad-mpeg2` but not for `ad-x264-origin1`, whose frames are the same. In the session, spans that only touch the drop's start (+0.000 s) were listed too.
- **With ticks** (run 08): rounded to ticks of the reference's time base, the same query lists none of these touching events, consistently.
- This is `sources.md` choice 1's point measured on attribution, and it is why P-A has every comparison done in ticks.

### 2.5 A scripted session's measured HTTP timing (run 06)

- **The setup.** `tools/session_fetch.sh` serves `ad-x264-a` from a local Python `http.server` (`tools/slow_server.py`). The server answers `v180/ad_000.ts` 2.3 s late; 2.3 s is the work order's example value, used here as the injected delay. `tools/fetch.cpp` fetches the session's 11 files in plan order with `avio_open2` and `avio_read`, under the analyzer's 5 s limits (`src/VMAFNet.h`). The plan: the master, then `v270`'s playlist and segments 0-2, `v180`'s playlist and segments 3-4, and `v270`'s segments 5-7.
- **The delayed segment.** 2313133 µs from the request to its last byte, of which 2.312 s to the response head, 12032 bytes. The other segments took 578 to 3569 µs.
- **Status.** `HTTP/1.0 200 OK` for every file. It comes from the `header='HTTP/…'` TRACE line of FFmpeg's `http.c` (`sources.md` §3.1), which a custom `av_log` callback sees whatever the log level.
- **What this shows.** A fetch by the tool gives the composite answer a measured duration. In the repository, the fetcher would use `NetworkLogger`'s manual tracking for plain avio users (`src/NetworkLogger.h:118-125`), which parses the same lines into `NetRequest` rows. That is the work order's "NetworkDebugger (av_log interception)" in a headless process (Q17, Q40).

### 2.6 End to end (run 10)

`tools/attribute.py` puts §2.1 to §2.5 together, with the prototype's detection parameters (T = 6, the work order's suggestion; W = 50 and N = 12, testgen's prototype values, inside run 05's region). The axis is the reference's playlist time; PTS are in ticks; each segment is a span event; and a pair's curve lists only its two inputs' events. Results:

| Case | Drop found | What the attribution returns (offsets from the drop's start) |
|---|---|---|
| E1(1) degraded, x264, `v270` | reference frames 100-124, 4.000-5.000 s; baseline 97.963, min 38.443, magnitude 59.520 | `playlist/segment v270 seg_004.ts` (media sequence 4), +0.000 to +1.000 s; the reference's `seg_004.ts` likewise |
| control `v180` | none | — |
| E1(1) degraded, MPEG-2, `v270` | 100-124, magnitude 37.939 | `v270 seg_004.ts` |
| E1(2) ad, x264, `v180` | 75-124, 3.000-5.000 s; baseline 91.692, min 22.237, magnitude 69.455 | `scte35/cue_out` (`EXT-X-CUE-OUT:2.000`, segment 3) at +0.000 s; `scte35/ad_break` +0.000 to +2.000 s; `scte35/cue_out_cont` at +1.000 s; `playlist/discontinuity` at +0.000 s; segments `ad_000.ts`, `ad_001.ts` |
| control `v270` | none | — |
| E1(2) ad, MPEG-2; ad at origin 1 s | 75-124 each, magnitudes 47.413 and 69.455 | the same events |
| E2, session `ad-switch` (entry 1 for segments 0-2, entry 2 for 3-4, entry 1 for 5-7) | 75-124; baseline 97.681, min 22.237, magnitude 75.445 | `scte35/cue_out` at +0.000 s and `ad_break` +0.000 to +2.000 s; `player/variant_switch` at +0.000 s, entry 1 (480x270, BANDWIDTH 565504; rank 2 from the top, 2 from the bottom) to entry 2 (320x180, BANDWIDTH 345920; rank 3 from the top, 1 from the bottom); `network/segment_request /v180/ad_000.ts`, 2.313 s, 12032 bytes, `HTTP/1.0 200 OK`, over +0.000 to +1.000 s; `playlist/discontinuity` at +0.000 s |

The last row is the work order's composite answer, measured: a drop that overlapped an SCTE-35 cue-out, a switch that went from rung 2 to rung 3 (counted from the top; Q16), and a segment request of 2.313 s.

The prototype's documents are 8577 to 14470 bytes, with 32 to 44 events each; the session's has 35 events (run 11).

---

## 3. The smallest system, and what comes after it

```
mirage_vmaf --ladder ...      (Phase 2, unchanged: D-068 scores once)
        │  ladder report (JSON)
        ▼
mirage_vmaf <build subcommand>  --report R [--plan P] --out T
  1. read R (JSON reader)                                  → pairs, frames, entries, reference
  2. per scored playlist: read it (vmafladder::read_master), SHA-256,
     parse segments (hls::parse_media_timeline) and tags (HlsEvents)
  3. per scored playlist: demux pass (TimelineMedia)       → segment of every frame, key / corrupt packets
  4. axis on the reference (Q02), in ticks                 → every reference frame's position
  5. events: playlist, SCTE-35 (plain tags; binary per D-070), decoder (demux pass),
     vmaf (unpaired frames from R); with a plan: spans, switches, session curve (D-068),
     and the session fetch (SessionFetch: NetworkLogger rows)
  6. detect drops on the curves (Q08-Q12), attribute each (overlap query, Q13-Q15)
  7. write T (the timeline document, Q01, Q04)
        │
        ▼
mirage_vmaf <query subcommand> --timeline T (--interval a:b | --auto [params]) [--format text|json]
        │  attributions grouped by source, with offsets (text answers the work order's question)
        ▼
player: timeline panel (§3.3), reads T and R
```

**The critical path to E1**: the JSON reader, playlist events, the demux pass, the axis and query, detection, the document, the CLI, and the acceptance tests. **E2 adds** sessions and the session fetcher. **D-070's binary SCTE-35** (`#EXT-OATCLS-SCTE35`, DATERANGE's `SCTE35-*`) runs in parallel and joins at the event mapping; E1 and E2 need only the plain cue tags (`testgen.md` §2.3). **After the headless parts**: the UI (§3.3) and the documents. **Later, by owner question**: the player's session log (Q20), decode errors (Q19), DASH (Q27), and SCTE-35 inside segments (D-070's interface).

Nothing on the critical path touches the player's render or decode path, or the analyzer's decoding (D-007, D-068: "The render and decode paths don't change for it"). The analyzer, the ladder report, `ManifestParser`, `HlsAlign` and `NetworkLogger` stay as they are, unless Q07 or Q19 is answered otherwise.

---

## 4. Data model and the single time base

### 4.1 Identities

- **Reference frame**: its position in the reference input, `ref_index`, the join key the ladder already has (`sources.md` §2(2)). Each reference frame has one `ref_pts` in every pair that scored it (`schema.md` §3.1, run 11).
- **Distorted frame**: `(pair, dis_index)`. It reaches the reference's frame through the pair's frames.
- **Segment**: `(entry, media sequence number)`, where `entry` is the master's `EXT-X-STREAM-INF` index (`src/VMAFLadder.h:88`) of a scored playlist. The segment table keeps its URI, byte range, EXTINF, discontinuity sequence and PDT (`src/HlsAlign.h:33-47`), plus its first frame and number of frames from the demux pass.
- **Event**: an id within its document.

### 4.2 The axis (owner question Q02)

One axis for intervals and offsets, on the reference variant's timeline (§3.1 asks for a single time base, on which variants are compared along the reference variant's timeline). Its options, measured in §2.3:

- (a) **raw reference PTS**, the work order's wording. Ambiguous where PTS restart: 50 shared values in the ad ladder.
- (b) **continuous PTS**. Unique, but forward jumps under 10 s become empty time: a 2 s ad break spans 5.000 s.
- (c) **reference playlist time**. Clean on the ad ladder, and robust to restarts and changes of frame rate. It is derived from EXTINF, which can be inexact (`sources.md` §2: the hls muxer's last EXTINF is 0.36 s for 10 frames at 25 fps).
- (d) **reference frame position × frame duration**. Exact for constant frame rates, and wrong for variable ones or a rate change at an ad.

**Whichever is chosen**, every entry also keeps its exact identity: the reference frame position, its raw PTS in ticks with the time base, and its discontinuity sequence. A pair report with no playlist, and the mezzanine reference, have one segment starting at the input's start.

**Agent's recommendation**: (c). Of the axes run 09 measured, it is the only one on which both the drop and the ad break of the evaluation's hardest case come out right. (d) would give the same there by construction (50 frames at 25 fps), but fails on rate changes. It is also the clock FFmpeg's hls demuxer seeks by (`ui.md` §1.6). The difference from "keyed by media PTS" is reported (§11, item 2).

### 4.3 Ticks, points and spans

- **Ticks** (P-A). Every time is an integer in ticks of the reference stream's time base; seconds are derived for output.
  - The report gives PTS as doubles (`src/VMAFReport.cpp:193-201`). Each is recovered exactly by rounding seconds × den / num, then checked against the demux pass's packet PTS (run 04: all agree).
  - Run 07 against run 08 shows why the ticks are needed.
- **Spans.** An event is a point (`t`) or a half-open span (`t`, `t_end`).
  - The spans are: segments, ad breaks (cue-out to cue-in), a session's variant spans, a segment request (its media span, the reference frames its frames pair with), and a detected drop.
  - A drop's span is [first frame's `t`, last frame's `t` + one reference frame duration).
  - An event whose `t_end` is absent is a point.

### 4.4 Wallclock (owner question Q03)

- **Observed events** have a wall clock: the session fetch's requests, and later the player's log. They are recorded as microseconds of one steady clock from the session's start, plus one UTC anchor sampled at the start (P-G). Run 06's fetcher recorded both.
- **Offline events** (VMAF, playlist, decoder) have none. A segment's `EXT-X-PROGRAM-DATE-TIME` goes in its payload.
- **Agent's recommendation**: that, which is `sources.md` choice 7(c).

### 4.5 Rungs (owner question Q16)

- **Identity.** Each variant is named by its master entry index and URI. Its BANDWIDTH ranks from the top (1 = highest) and from the bottom (1 = lowest) are labels.
- **The work order's example.** Its switch, from the rung numbered 5 to the one numbered 3, reads as a switch down only when counting from the bottom.
- Run 10 prints both ranks.

### 4.6 C++ types (proposal; headers of the skeleton package, P-R)

```cpp
// src/Timeline.h — no FFmpeg, no ImGui; built into mirage_vmaf, mirage_tests and the player.
namespace timeline {

using Tick = std::int64_t;                        // ticks of the reference time base

enum class Source : std::uint8_t { Vmaf, Network, Playlist, Scte35, Player, Decoder };
std::string_view source_name(Source);             // "vmaf", "network", ...

struct Value;                                     // a payload value: null, bool, int64, double, string, list, object

struct Event {
    std::uint32_t id = 0;
    Source        source{};
    std::string   type;                           // "segment", "cue_out", "segment_request", ...
    std::optional<std::size_t>  entry;            // master entry index of the variant it belongs to
    std::optional<std::int64_t> segment;          // its media sequence number
    std::int64_t  ref_index = -1;                 // first reference frame it covers; -1: none
    Tick          t = 0;                          // axis position
    std::optional<Tick> t_end;                    // spans: exclusive end
    std::optional<std::int64_t> pts;              // raw ticks on its own input's clock, when it has one
    std::optional<std::int64_t> wallclock_us;     // observed events: µs from the session's UTC anchor
    std::vector<std::pair<std::string, Value>> payload;
};

struct Axis {                                     // the owner's axis (Q02)
    std::string kind;                             // "playlist-time" | "continuous-pts" | ...
    int tb_num = 0, tb_den = 0;                   // reference time base
    std::vector<Tick> frame_t;                    // by ref_index
    Tick frame_duration = 0;
};

struct Curve {                                    // a pair's, or a session's
    std::string name;                             // "pair 1", "session"
    std::vector<std::int64_t> ref_index;          // the reference frames it has a score at
    std::vector<double> score;                    // NaN: no score (a reference span in top-rung mode)
    std::vector<std::size_t> entry;               // the variant scored at each point
};

struct Hit { std::uint32_t event; Tick offset; std::optional<Tick> offset_end; };

struct Attribution {
    std::string curve;
    std::int64_t first_ref_index = 0, last_ref_index = 0;
    Tick t = 0, t_end = 0;
    std::optional<double> baseline, min, mean, magnitude;   // absent for a user interval
    std::vector<std::pair<Source, std::vector<Hit>>> groups; // fixed source order (Q15)
};

class Timeline {
public:
    std::uint32_t add(Event e);
    const std::vector<Event>& events() const;
    // Events meeting [t0, t1) by the rule of Q14, restricted by `filter` (Q13).
    std::vector<std::uint32_t> overlapping(Tick t0, Tick t1, const Filter& filter) const;
};

Attribution attribute(const Timeline&, const Axis&, Tick t0, Tick t1, const QueryOptions&);

} // namespace timeline
```

---

## 5. How each §3.1 source becomes events

### 5.1 The frame → segment map (the join every other source uses)

- **Placing frames** (P-B). For each scored playlist, the reference's included: a demux pass as in §2.1, in `TimelineMedia` (FFmpeg library API, no decoding). It gives each frame of that input its segment, each segment its first frame and frame count, and the key and corrupt flags of the packets.
- **The check.** Each frame's PTS from the report is checked against its packet's. A variant with any mismatch gets no segment map: its segment-bound events are left out, with a warning in the document. Run 04 found no mismatch on 15 playlists.
- **The playlists** are read with `vmafladder::read_master` (`src/VMAFLadder.cpp:293-334`), under the analyzer's network limits. That function reads any playlist starting with `#EXTM3U`. Each playlist's URL, location and SHA-256 go in the document (P-C).
- **Live playlists** are out of scope and are refused (P-T).
- **The cost** is one more read of every segment. For a local ladder it is a local read; for a remote ladder, a second download (MEASUREMENT PENDING). The alternative, recording the segment while scoring, changes the analyzer and the ladder report (Q07, Q19).

### 5.2 VMAF

- **Curves.** A pair's curve is its frames' `vmaf` at their `ref_index`, read from the ladder report. The scores are not copied into the document (Q01).
- **Unpaired frames.** The report's `alignment.failures` (input, index, PTS, reason) become `vmaf/unpaired_frame` events at the reference frame nearest to them. This is how a frame the offline decode lacks shows (`sources.md` §7).
- **Drops.** Detected drops are `vmaf/drop` spans; each carries its attribution (§6).
- **Top-rung mode.** The top rung has no pair (`src/VMAFLadder.h:36-38`). A session span on it has no score (Q18).

### 5.3 Playlist

- **Where tags go.** Tags are read from each scored playlist's text by a new pure module, `HlsEvents`, on top of `hls::parse_media_timeline`'s segments (P-D). A tag belongs to the segment whose URI follows it, including a tag written between `#EXTINF` and the URI (`scte35.md` §6).
- **The events, each at its segment's start:**
  - `segment`, a span over the segment's frames;
  - `discontinuity`, with the discontinuity sequence numbered per segment (`src/HlsAlign.cpp:603-608`);
  - `program_date_time`;
  - `gap`, `map` and `byterange`, as payload of the segment;
  - `daterange`, its non-SCTE attributes;
  - `vendor_tag`, any other `#EXT-X-` or `#EXT-` line, kept raw.
- **Through the pairs.** A distorted variant's events reach the axis through its frames' paired reference frames.
- **DASH period boundaries** stay an interface (Q27).

### 5.4 SCTE-35 (D-070)

- **Plain tags.** `EXT-X-CUE-OUT` takes its duration in the forms `scte35.md` §2.1 lists. `EXT-X-CUE-OUT-CONT` gives `elapsed/duration` or `ElapsedTime=…,Duration=…,SCTE35=…`, and `EXT-X-CUE-IN` is bare or has an `ID`.
- **Messages.** `#EXT-OATCLS-SCTE35` (base64) and DATERANGE's `SCTE35-OUT`, `-IN` and `-CMD` (hex) are decoded by a new module, `Scte35`. It decodes `splice_insert`, `time_signal`, `break_duration`, `splice_time`, segmentation descriptors and the CRC, from SCTE 35 2023r1's tables as `scte35.md` §3 gives them. The CRC is checked with libavutil's `av_crc(AV_CRC_32_IEEE)`, as `mpegts.c` checks sections (`scte35.md` §3.6; P-E).
- **Event types.**
  - `cue_out`, `cue_in` and `cue_out_cont`;
  - `ad_break`, a span from a cue-out to the next cue-in in play order, or to the cue-out's start plus its declared duration (Q24);
  - `splice_point`, the decoded splice time when a message has one: `(pts_time + pts_adjustment) mod 2^33`, placed at the first frame whose PTS is at or after it (SCTE 35's own definition, `scte35.md` §3.2);
  - `scte35_message`, for any other decoded command or segmentation type (Q22).
- **Placement** of cue events is by tag position (Q21). The other anchors are kept in the payload, with a warning event when they disagree.
- **Invalid and cancelled messages** follow Q23.
- **Signalings outside D-070's list** are vendor tags unless Q25 adds them.
- **Measured.** Run 10 placed the plain tags of the testgen ad ladder at +0.000 s (cue-out) and +1.000 s (cue-out-cont), and paired the ad break over +0.000 to +2.000 s.

### 5.5 Sessions and switches (D-068)

- **The plan** (Q29): one scripted session, a list of spans in play order, each naming a master entry and a range of its segments.
- **The session's curve.** At each reference frame it is the score of the variant played then. The frame's span names the variant; the pair's frames give the score, through the variant's segment map (D-068). Spans on the reference in top-rung mode have no score (Q18).
- **Events.**
  - `player/variant_span`, one per span;
  - `player/variant_switch`, at each span boundary, with the entries before and after it (entry index, URI, BANDWIDTH, RESOLUTION, both ranks) and `source: "scripted plan"`.
- **Frames a player drops at a switch** don't exist in a scripted session. They come from the player's log later (D-068's limits; Q20).

### 5.6 Network: the session fetcher (owner question Q17)

- **The fetch.** With a plan and a fetch base, `SessionFetch` fetches the session's files in plan order: each media playlist before its first segment, then the segments. The fetches run one at a time, unpaced, one connection per file.
- **The rows.** Each fetch is a plain avio read. `NetworkLogger`'s `track_open_begin`/`track_open_end`/`track_close` (`src/NetworkLogger.h:118-125`) and its `av_log` parsing give a row with the status, the time sent, the first byte, the end and the bytes.
- **The logger's process.** The logger is installed only in the subcommand's process, after the demux passes, so that its rows are the session's own (P-G).
- **The events.** Each segment's row becomes a `network/segment_request` span over that segment's media span. It carries the URL path, the times, the duration, the bytes, the status and the wall clock. A playlist's row is a `network/playlist_request` with no media span.
- **What the timing means.** It measures the server and network as one sequential client sees them, not a player's buffer-paced fetches; the paper has to say so (risk R15).
- **What isn't used.** The scoring reads' timing is not used: decoding and scoring pace those reads (`sources.md` §3.3).

### 5.7 Decoder

- **From the demux pass:** `decoder/keyframe` (key packets) and `decoder/corrupt_packet` (`AV_PKT_FLAG_CORRUPT`). The latter shows, for example, continuity counters that restart unmarked at a splice (`testgen.md` §3.6, item 2).
- **Decode errors** need a decode, and are not in the minimal system (Q19). On the damaged fixture, `decode_error_flags` = 4 on 6 frames left no trace in the report (`sources.md` §7).
- **Frames dropped at a switch** come from the player's log only (Q20).

---

## 6. Query API, detection, and the answer to the example question

### 6.1 The overlap query (owner questions Q13 to Q15)

- **What it takes and returns.** `timeline::attribute(timeline, axis, t0, t1, options)` takes a span on the axis: a detected drop, or a user's interval given in seconds of the axis or as reference frames. It returns every event that meets the span by the rule of Q14, grouped by source in a fixed order. Each event comes with its offset from the span's start (`t − t0`) and, for a span event, the offset of its end.
- **Filters.** Options restrict the variants (Q13) and the sources, and can widen the span by margins (Q14, (b)).
- **Ranking.** None (Q15).
- **Index** (P-M). The events are kept sorted by `t`, with a running maximum of `t_end`. That makes a query O(log n + k) with nothing more elaborate; documents hold one event per segment and per tag (run 11: 32 to 44 events for 200-frame ladders).

### 6.2 Detection (owner questions Q08 to Q12)

- **What it computes.** `dropdetect::detect(scores, params)` runs over one curve's scored points in order. A point is low when its score is more than T below the baseline; a drop is a run of more than N low points.
- **The baseline** is a statistic (Q09) of the last W points before it that weren't low, tested from W/2 such points on.
- **Gaps.** Points without a score (NaN) are skipped (Q10).
- **The record.** Every attribution records the parameters, the baseline at the drop's first point, and the drop's minimum, mean and magnitude (the baseline minus the minimum).
- **Which curves** it runs on is Q08. For a session the recommended option compares each point with the played variant's own baseline, so a switch alone isn't a drop. In the ad ladder the two rungs' medians, 97.934 and 91.140 (`testgen.md` §3.2), are 6.794 apart, more than the work order's 6 points.
- **The parameters run 05 measured** are in §2.2. The defaults are Q11.

### 6.3 The answer to the example question (text output)

The query subcommand's text form, as the prototype printed it for the scripted session (run 10, abridged to the lines that answer the question):

```
Drop 1: reference frames 75-124 (50 frames); playlist time 3.000-5.000 s; ... baseline 97.681, min 22.237, ... magnitude 75.445
  scte35   cue_out          v180, segment 3 at +0.000 s: line 14 '2.000'
  scte35   ad_break         v180 at +0.000 s to +2.000 s: cue-out to cue-in, declared duration 2.0 s, placed by tag position
  player   variant_switch   v180 at +0.000 s: entry 1 (480x270, BANDWIDTH 565504; rank 2 from the top, 2 from the bottom) -> entry 2 (320x180, BANDWIDTH 345920; rank 3 from the top, 1 from the bottom)
  network  segment_request  v180, segment 3 at +0.000 s to +1.000 s: /v180/ad_000.ts: 2.313 s from request to last byte (2.312 s to the response head), 12032 bytes, HTTP/1.0 200 OK
  playlist discontinuity    v180, segment 3 at +0.000 s: line 13
```

Put in the form of the work order's example: the drop overlapped an SCTE-35 cue-out (+0.000 s), a switch from rung 2 to rung 3 counted from the top (+0.000 s), and a segment request of 2.313 s (over +0.000 to +1.000 s). The JSON form carries the same groups, with ids that refer to the document's events (§8.2).

---

## 7. The CLI (owner questions Q28, Q29)

**Proposal.** Two first-word subcommands of `mirage_vmaf`; their names are the owner's (Q28).

```
mirage_vmaf <build> --report <ladder report.json> --out <timeline.json>
                    [--plan <session.json>] [--fetch-base <url>]
                    [--threshold T] [--window W] [--min-frames N] [--margin S]
                    [--config <file>]
mirage_vmaf <query> --timeline <timeline.json>
                    (--interval <t0>:<t1> | --ref-frames <first>-<last> | --auto)
                    [--curve <name>] [--threshold T] [--window W] [--min-frames N]
                    [--format text|json] [--out <file>] [--config <file>]
```

- **Dispatch.** `tools/mirage_vmaf.cpp` dispatches on a first word before `vmafcli::parse`. The existing forms (`--ref`/`--dis`, `--ladder`) and their tests stay as they are: today any argument that isn't an option is an error (`src/VMAFCli.cpp:288`).
- **Option tables.** Each subcommand has an option table in `src/TimelineCli.cpp`, built like `vmafcli`'s, where one row gives the option, its config setting and its help (`src/VMAFCli.cpp:24-35`). Every configurable item is therefore both a CLI option and a config setting, in D-030's `name = value` format (`src/VMAFCli.h:54-63`; P-P).
- **The plan** is a list, so it is a file of its own, named by `--plan` and recorded by path and SHA-256, as `config_file` is (`src/VMAFReport.cpp:146-150`; `schema.md` §3.3).
- **Detection.** The build subcommand detects drops and writes their attributions, with the parameters used. The query subcommand reads the document and its ladder report (whose hash it checks, Q06), and answers a user interval, or detects again with the parameters given.
- **Exit status**, as `mirage_vmaf`'s (P-U): 0 written; 1 a failure (an unreadable input, a hash mismatch, a refused live playlist); 2 bad arguments.

---

## 8. The document and its schema (owner questions Q01, Q04 to Q07, Q30, Q31)

### 8.1 Where (Q01)

| | A: in the ladder report | B: a document per session, naming the ladder report by hash (scores not copied) | B′: as B, with the session's scores copied | C: per-variant events in the ladder report, the session in a document |
|---|---|---|---|---|
| D-068 (one scoring, many sessions) | each session means scoring again (1882.86 s for the full advanced ladder, `schema.md` §3.3), or rewriting the scoring run's output | the ladder report stays the scoring run's output | as B | as B for sessions; per-variant events need the analyzer or a pass at scoring time |
| Size | per-frame data is 99.91% of a full ladder report (`schema.md` §3.3), and every session adds to that file | 8577 to 14470 bytes for the prototype's 200-frame documents (run 11); on real ladders, MEASUREMENT PENDING | adds the curve: 36000 entries for a 60 fps pair of the advanced sample (`schema.md` §3.3) | the ladder report grows |
| Committed records, scripts, tests | top-level keys break nothing (`schema.md` §4.1) | untouched | untouched | keys under `pairs[]` break an exact key count (`tests/test_vmaf_report.cpp:909`) |
| §7 | matches §7's single output | differs: reported | differs | differs |
| Readers | none needed in the scoring run | a JSON reader in `src/` (Q30) | the same | the same |

**Agent's recommendation**: B, as `schema.md` §3.3 recommends. A ladder with no session is a document with no session, which is what the acceptance tests write.

### 8.2 Layout (proposal; Q04)

```
{
  "schema_version": <the owner's value and form, Q05>,
  "tool": "mirage_vmaf", "command": "<build subcommand's name>",
  "ffmpeg_libraries": { ... },                   // as the ladder report's (P-035)
  "options": { ... }, "config_file": { "path", "sha256" } | null,
  "ladder_report": { "path", "sha256", "scores_sha256", "reference_mode",
                     "reference": { "url", "entries" }, "master_sha256" },   // Q06
  "plan": { "path", "sha256" } | null,
  "axis": { "kind", "time_base", "frame_rate", "frames", "fallback" },        // Q02
  "parameters": { "detection": {...}, "overlap": {...}, "scte35": {...} },
  "variants": [ { "entries", "url", "bandwidth", "resolution", "rank_from_top", "rank_from_bottom",
                  "role": "reference" | "distorted", "pair": <index> | null,
                  "playlist": { "url", "location", "sha256" },
                  "segment_map": "ok" | "refused: <why>",
                  "segments": [ { "seq", "uri", "range", "extinf", "disc_seq", "pdt",
                                  "first_index", "frames", "t", "t_end" } ] } ],
  "session": { "name", "wallclock_anchor", "spans": [ ... ] } | null,
  "timeline": [ { "id", "source", "type", "entry", "segment", "ref_index",
                  "t", "t_s", "t_end", "t_end_s", "pts", "wallclock_us", "payload" } ],
  "attributions": [ { "curve", "detected": true | false, "first_ref_index", "last_ref_index",
                      "t", "t_s", "t_end", "t_end_s", "baseline", "min", "mean", "drop_magnitude",
                      "groups": { "<source>": [ { "event", "offset_s", "offset_end_s" } ] } } ],
  "warnings": [ ... ]
}
```

- **Numbers.** Ticks are integers. Seconds are written with `json::number` (`src/JsonText.h:17-19`), so a schema types scores as `number`, not `integer` (`schema.md` §1.6).
- **Writing.** The file is written through a temporary file and renamed, as `vmafreport::write_file` does (P-N).
- **Against §7.** §7's `timeline[]` entries (`pts`, `wallclock`, `source`, `type` and `payload`) map to these entries as follows: `pts` is the raw PTS, `t` the axis, and `wallclock_us` with the session's anchor is §7's `wallclock`. §7's `attributions[] { interval, drop_magnitude, overlapping_events[] }` becomes the span, `drop_magnitude` and `groups`. The differences are listed in §11.

### 8.3 Schema file and checks (Q31)

- **The file.** A JSON Schema file for the document goes in the repository (the work order's §7; path proposed: `docs/schema/`, name the owner's).
- **The check.** No JSON reader exists in `src/`, and the eval tooling is standard-library Python (`schema.md`, open issues 3). The recommended check is a subset validator in `mirage_tests` for the keywords the schema uses: `type`, `required`, `properties`, `items`, `enum`.

### 8.4 JSON reader (Q30)

- **What reads JSON.** The build subcommand reads ladder reports and plans; the query subcommand and the UI read documents and ladder reports.
- **What exists.** `src/` writes JSON by hand and reads none (`src/JsonText.h:1-21`). The tests' minimal reader rejects duplicate keys (`schema.md` §1.1).
- **The cost.** A full ladder report is 27969051 bytes of text (`schema.md` §3.3). Its load time and memory with a document reader: MEASUREMENT PENDING.

---

## 9. Tests

### 9.1 The two acceptance tests (D-069) and the composite example

**Content** (`tests/TestLadders.{h,cpp}`, ported from `testgen/src/testgen.cpp`; P-I):

- **What the generator makes.** The noise source; 3 rungs; 8 segments of 25 frames at 25 fps; segments at QP 22 (one at QP 46) with libx264, or qscale 4 (31) with MPEG-2 (Q32); the calm ad; continuity counters marked at the splice; BANDWIDTH measured (`testgen.md` §2).
- **Ground truth.** The generator writes its layout (each segment's frames, period, tags and quantizers), and the tests check against it, never against the tool's output.
- **When it runs.** Once per test run, as the existing fixtures are made (`tests/test_vmaf_ladder.cpp:192-216`).
- **Optimization.** The generator is compiled with optimization in every build type, as `src/VMAFPool.cpp` gets its own compile options (`CMakeLists.txt:227`). The reason: generation took 16.941 s without optimization, against 2.312 s with it (`testgen.md` §3.4). The effect in a Debug build: MEASUREMENT PENDING.

**Steps of each test** (`tests/test_attribution_acceptance.cpp`; programs run as `tests/test_vmaf_ladder.cpp:286-310` runs `mirage_vmaf`; P-O):

1. Generate the ladder.
2. `mirage_vmaf --ladder` → the ladder report (top-rung mode, D-002).
3. The build subcommand → the document, with explicit detection parameters inside run 05's region (P-J).
4. The query subcommand `--auto --format json`.
5. Assertions:
   - **Test 1 (degraded segment):** exactly one drop on the degraded rung's curve, over the reference frames of the degraded segment (100-124 in testgen's layout). Its attribution's `playlist` group contains that rung's segment, by URI and media sequence number. The control rung has no drop.
   - **Test 2 (ad boundary):** exactly one drop on the low-quality ad rung, over the ad's reference frames (75-124). Its `scte35` group contains the `cue_out` of the segment after the first `EXT-X-DISCONTINUITY`, at offset 0, and an `ad_break` whose end offset is the ad's length. The control rung has no drop.
   - **Test 3 (the work order's example; not an acceptance test of §3.2, but the evaluation's E2):**
     - the setup: the ad ladder served by `tests/HttpFileServer.h` with a new delay rule (P-H), and a plan that switches down into the ad;
     - the drop on the session's curve must list the `cue_out`, a `variant_switch` between the planned entries, and a `segment_request` for the delayed segment whose measured duration is at least the delay and whose status is 200;
     - the test sets its own delay (P-K); the work order's 2.3 s is used only in the eval harness.
   - **Every assertion is key-independent:** frame ranges, URIs, sequence numbers, types and offsets. A separate unit test checks the axis values of the option the owner picks.
6. **Variants of test 2** (Q33): the ad at the main content's origin (the PTS collision: the hard case); the ad from 1 s (a jump of more than 10 s); and continuous timestamps (which the player can seek into, `ui.md` §2.4).

### 9.2 Unit tests, by package

Each work package in §14 lists its own. Every new module is pure or has a local fixture: synthetic playlists as string literals, HLS streams the test writes with FFmpeg's libraries (as `tests/test_vmaf_segments.cpp:46-117` does), and `tests/HttpFileServer.h`. Nothing is downloaded, and no binary but `mirage_vmaf` is run.

### 9.3 Reproduction (E4)

- **The command.** `eval/timeline/run_all.sh <build dir> <out>` (proposal) runs the acceptance cases, keeps their ladders, reports and documents (P-V), runs the composite example against a local delaying server (as this design's `tools/slow_server.py`, with the work order's 2.3 s), and checks every answer against the generator's layout.
- **The checks** compare everything except wall-clock fields and measured durations. For a duration the check is that it is at least the injected delay.
- **The record.** `cmd.txt` and `environment.txt`, with `git rev-parse HEAD`, as P-006 has the eval tooling record the commit (P-Q).

---

## 10. §3.3 UI: a plan for the owner (D-007)

This keeps `ui.md`'s plan (§2 there) and changes its input to this design's document.

- **What it is.** A `TimelinePanel` docked above the control stack (placement: Q35). It loads a timeline document and the ladder report it names, and matches the master to the open stream by SHA-256, else by URL (`ui.md` §2.3).
- **What it draws.** Each pair's curve and the session's curve as per-column min/max envelopes, so that a one-frame dip survives (`ui.md` §4.2), plus one event lane per source.
- **Interaction.** Wheel zoom, drag to pan and double-click to fit, as the Alignment tab does (`src/NetInspectorAlign.cpp:637-662`).
- **A click** picks the lowest frame near the cursor and shows its attribution. The document's attribution is used when the frame lies in a detected drop; otherwise the panel calls the same query API, compiled into the player, so that the panel and the CLI agree.
- **Seeking.** A click seeks through `s_seek_to` only when the frame is reachable (`ui.md` §2.4's rules).
- **Frames FFmpeg 8.1.1 can't seek to.** No seek reaches the frames of an ad whose timestamps restart (`ui.md`: `experiments/hls-seek-probe/run-output.txt`). That is a difference from the work order (Q36).
- **ImGui-free parts.** The view math, the envelope pyramid, hit testing, matching, the time mapping and the seek decision go in ImGui-free files tested in `mirage_tests` (D-031: `mirage_tests` links no ImGui). The player-only panel follows `NetInspector`'s pattern, with `NoNav` and the focus drop (`ui.md` §4.1).
- **D-007.** By `ui.md` §3's reading, none of the recommended changes touches the render or decode path. The changes that would are listed there: a seek with its own landing point, reaching timestamp-reset frames, scrubbing by drag, and the player's own session log. The owner approves the plan or not (Q35).
- **Tests:** `ui.md` §5.1's unit tests; its seek probe made into a ctest case (`ui.md` §5.2); a scripted QA run outside ctest (`ui.md` §5.3); and the manual checks of `ui.md` §5.4.

---

## 11. Differences from the work order, to report (not edited away)

1. **§3.1's "NetworkDebugger (av_log interception)"** is the code's `NetworkLogger` (`sources.md` §10). In this design a headless session fetcher uses it (§5.6). In the player it is unchanged (Q40).
2. **"Keyed by media PTS".** Under the recommended axis the key is the reference's playlist time, with the raw PTS kept on every entry. PTS can't key the evaluation's hardest case (§2.3; Q02).
3. **§7's single output.** The timeline and attributions go in a document per session, not in the ladder report (Q01).
4. **§7's attribution shape.** Events are grouped by source with offsets, and the detection's parameters and the curve are recorded, which §7's flat `overlapping_events[]` doesn't have (Q04).
5. **"ABR / player: variant switch events".** Mirage has no ABR; switches come from a scripted plan (D-068), and later from the player's log (Q20).
6. **Decoder events (frames dropped, errors in decoding, where keyframes are).** Keyframes and corrupt packets come from a demux pass, and frames the offline decode lacks show as unpaired frames. Decode errors are not recorded (Q19).
7. **"HLS / DASH parsing … DASH period boundaries".** DASH stays an interface: `mirage_vmaf` scores HLS ladders only (`src/VMAFLadder.h:15-22`; Q27).
8. **SCTE-35 "splice point".** Many published messages carry no splice time (`scte35.md` §9, item 3). Cue events are placed by tag position, and a `splice_point` event exists only where the message has a time (Q21).
9. **SCTE-35 signalings beyond D-070's list** (`#EXT-X-SCTE35`, `#EXT-X-SPLICEPOINT-SCTE35`, Adobe's `#EXT-X-CUE`) are kept only as vendor tags (Q25).
10. **The example's switch, from the rung numbered 5 to the one numbered 3,** assumes a numbering the report doesn't have (Q16).
11. **§3.3's seek to a low point's frame on a click** can't hold for an ad whose timestamps restart, with FFmpeg 8.1.1's hls demuxer (Q36).
12. **The top rung in top-rung mode** has no score, so a session's span on it isn't scored (Q18). An ad that is low in every rung, the top included, can't show as a drop in that mode (D-002's limit; `testgen.md` §5).
13. **The HTTP status and first-byte time** come from FFmpeg's TRACE/DEBUG log text, whose wording FFmpeg may change (`sources.md` §3.3).
14. **§7's `schema_version`, `tool_version` and `git_commit`.** The build has no version (`CMakeLists.txt:2`), and P-006 has the eval tooling record the commit (Q05).
15. **A repository document.** `docs/VMAF_GAP_REPORT.md:230-231` says the Decoder has a dropped-frame counter; the counter is the render loop's `total_dropped` (`src/main.cpp:1751`; `sources.md` §12). Noted, not edited.

---

## 12. Owner questions

Each stands alone; its options carry their trade-offs; "Agent's recommendation" is the agent's; "Blocks" names the work packages of §14.

### Core model and outputs

**Q01. Where the timeline and its attributions are written.**
- **Options.**
  - (A) In the ladder report, as §7 lists them.
  - (B) A separate document per session (a ladder alone is a session-less document) that names the ladder report by hash and doesn't copy its scores.
  - (B′) As B, with the session's per-frame scores copied, so that the document reads alone.
  - (C) Per-variant events in the ladder report, the session in a document.
- **Trade-offs** in §8.1.
- **Agent's recommendation**: B. It keeps D-068's one scoring with many sessions, it leaves the committed ladder records and their checks valid, and per-frame data dominates the size. It is a difference from §7, to report.
- **Blocks**: WP08, WP09, WP12, WP15.

**Q02. The single axis for intervals and offsets.**
- **Options.**
  - (a) Raw reference PTS: the work order's wording, but 50 values name two frames in the ad ladder (run 09).
  - (b) Continuous PTS, the report's sequences end to end: unique, but forward jumps under 10 s become empty time, and the 2 s ad break spans 5.000 s (run 09).
  - (c) Reference playlist time: segment start plus the PTS offset within the segment. Clean on the ad ladder (2.000 s; drop 3.000-5.000 s, runs 09, 10) and the clock hls.c seeks by, but derived from EXTINF, which can be inexact.
  - (d) Reference frame position × frame duration: exact for constant frame rates, wrong for variable ones or a rate change at an ad.
- **In every option** each entry keeps its reference frame, raw PTS in ticks, and sequence.
- **Agent's recommendation**: (c), with a fall back to (b) when the reference's segment map is refused (P-S). The difference from "keyed by media PTS" is reported.
- **Blocks**: WP01, WP08, WP12.

**Q03. "Wallclock recorded too".**
- **Options.**
  - (a) The observation time of observed events only; null otherwise.
  - (b) (a), as µs from one UTC anchor per session, with `EXT-X-PROGRAM-DATE-TIME` in the payload of offline events.
  - (c) A PDT-derived date used as the wall clock of offline events too.
- **Agent's recommendation**: (b), which is `sources.md` choice 7(c). A PDT is a program date, not when anything was observed.
- **Blocks**: WP07, WP08.

**Q04. The document's layout, as its first schema version.**
- **Options.**
  - (a) The layout of §8.2: entries with ticks and seconds, source, type, entry, segment, raw PTS, wall clock and payload; attributions with their span, curve, detection parameters, baseline, magnitude, and groups by source with offsets.
  - (b) §7's literal shapes only, with everything else in `payload`.
  - (c) (a) plus §7-named aliases.
- **Agent's recommendation**: (a). §3.2 asks for groups and offsets, and an attribution can't be reproduced without its parameters. The difference from §7 is reported.
- **Blocks**: WP08, WP09, WP11.

**Q05. Provenance fields in the document.**
- **Options.**
  - (a) `schema_version` only (its value and form the owner's), with the tool name, the libraries' versions and the ladder report's settings copied; `tool_version` and `git_commit` left to Phase 5. P-006 has the eval tooling record the commit.
  - (b) All three now, `git_commit` from a configure-time `git rev-parse` and "unknown" outside a checkout. The project has no VERSION (`CMakeLists.txt:2`), and this changes P-006.
  - (c) None until Phase 5.
- **Agent's recommendation**: (a). It respects P-006, and the difference from §7 is reported.
- **Blocks**: WP08.

**Q06. How the document names its ladder report.**
- **Options.**
  - (a) The SHA-256 of the file's bytes. It doesn't match a committed copy, whose paths are redacted (`schema.md` §1.8).
  - (b) The SHA-256 of a canonical digest of what attribution uses: each pair's index, entries, frames' indices, PTS and scores, and the settings. Redaction leaves it unchanged.
  - (c) Both.
- **Agent's recommendation**: (c). (a) identifies the file, and (b) survives the redaction.
- **Blocks**: WP08, WP09.

**Q07. Does Phase 3 change the ladder report?**
- **Options.**
  - (a) No. The build subcommand reads and hashes the media playlists itself, at the risk that a playlist read later isn't the one scored (VOD only, so low).
  - (b) Add each pair's media-playlist SHA-256s (`pairs[]` keys). This breaks one exact key count (`tests/test_vmaf_report.cpp:909`), not the committed records' checks (`schema.md` §4.1).
  - (c) (b) plus per-pair segment tables recorded while scoring. That changes the analyzer's reading of its inputs and needs a D-007-style plan.
- **Agent's recommendation**: (a).
- **Blocks**: WP15.

### Detection and query

**Q08. Which curves detection runs on.**
- **Options.**
  - (a) Each pair's curve only; a session adds events but no detection.
  - (b) The pairs' curves and the session's curve, with its own trailing baseline. A switch between rungs whose levels differ by more than T is then itself a drop: the ad ladder's `v270` and `v180` medians, 97.934 and 91.140 (`testgen.md` §3.2), differ by 6.794.
  - (c) The pairs' curves and the session's curve, each point compared with the played variant's own baseline: a switch alone isn't a drop, a dip within a variant is.
  - (d) The session's curve only.
- **Agent's recommendation**: (c). It reads "the variant's sliding baseline" literally in both places, and the composite drop is still found. Run 10 found it with (b)'s baseline.
- **Blocks**: WP02, WP07, WP10.

**Q09. The baseline's statistic.**
- **Options.**
  - (a) The median of the last W points that weren't low.
  - (b) Their mean.
  - (c) The median of the last W points, low or not.
  - (d) The curve's global median, which isn't sliding.
- **Measured (run 05).** (a) and (b) are exact on the acceptance curves for T from 3 to 40. (c) shortens long drops at W = 10 to 50 and is exact only at W = 100.
- **Agent's recommendation**: (a). It resists outliers, and a drop doesn't pull it down.
- **Blocks**: WP02.

**Q10. The units of W and N, and points without a score.**
- **Options.**
  - (a) Points of the curve, with points without a score (a lower-frame-rate variant's off-grid reference frames, `schema.md` §3.1 run 11; reference spans in top-rung mode) skipped.
  - (b) Reference frames, with gaps holding the last score.
  - (c) Seconds of the axis, with gaps skipped.
- **Agent's recommendation**: (a). The work order says "N frames", and nothing is invented at the gaps.
- **Blocks**: WP02, WP09.

**Q11. Defaults for T, W and N.**
- **Options.**
  - (a) T defaults to 6, the work order's suggestion; W and N have no default and must be given until a measurement sets them.
  - (b) All three set now from run 05: synthetic content with one source, 200 frames per pair.
  - (c) All three required, with no default.
- **Agent's recommendation**: (a). The acceptance tests pin their own parameters, and run 05's region is wide, so they don't constrain the defaults.
- **What would inform the defaults.** On D-011's ladders with degradations injected at several lengths and strengths: detection hits and false alarms for each (T, W, N), and the longest run of low points in unaltered rungs (3 points in the busy control, run 05).
- **Blocks**: WP02 (defaults only), WP09.

**Q12. Frames that start a libvmaf context (D-037).**
- **Options.**
  - (a) No special case: N passes over their single-frame dips (frame 75 in four controls, run 05).
  - (b) Never low: they never start or extend a drop. The ad's drop would then start at frame 76, since frame 75 starts a libvmaf context (`testgen.md` §3.2, §3.5: two contexts, of 75 and 125 pairs).
  - (c) Left out of the baseline only.
- **Agent's recommendation**: (a). (b) moves an ad's drop off its cue by one frame.
- **Blocks**: WP02, WP10.

**Q13. Which variants' events an attribution lists.**
- **Options.**
  - (a) For a pair's curve, the events of its two inputs, distorted and reference; for a session's, the events of the played segments.
  - (b) Every scored variant's events.
  - (c) For a pair's curve, the distorted input's only.
- **Agent's recommendation**: (a), as run 10 did. It keeps another rung's cues out of a pair's answer.
- **Blocks**: WP01, WP09.

**Q14. What "overlapping it in time" means.**
- **Options.**
  - (a) Half-open spans on the axis in ticks. A point in [t0, t1) counts; a span meeting [t0, t1) counts; touching ends don't.
  - (b) (a) with margins before and after, given by the user, for causes that precede their effect (in a real player a slow fetch comes before the frames it delivers).
  - (c) Closed spans, where touching ends count: the cue-in at a drop's end would be listed.
- **Measured.** With (a), the ad break carries the cue-in at its end offset of +2.000 s (run 10). Boundary events were listed inconsistently with doubles and consistently with ticks (runs 07, 08).
- **Agent's recommendation**: (a), with (b)'s margins as options that default to none (the literal overlap).
- **Blocks**: WP01.

**Q15. Ordering and ranking of the events listed.**
- **Options.**
  - (a) No ranking: groups in a fixed source order, events by offset.
  - (b) Ranked by a heuristic, such as distance from the drop's start or overlap fraction.
  - (c) A "likely cause" field.
- **Agent's recommendation**: (a). The claim is overlap; a ranking is a causal heuristic that would need its own evaluation.
- **Blocks**: WP01, WP08.

**Q16. Rung numbering in outputs.**
- **Options.**
  - (a) The master's entry index and URI only.
  - (b) A BANDWIDTH rank from the top (1 = highest).
  - (c) A rank from the bottom (1 = lowest), under which "rung 5 to rung 3" is a switch down.
  - (d) The entry index and URI as identity, with both ranks as labels.
- **Agent's recommendation**: (d), as run 10 prints them. A rank changes when an entry is added; the index and URI don't.
- **Blocks**: WP08, WP09.

### Sources

**Q17. Where a scripted session's per-segment HTTP timing comes from** (D-068 leaves it open).
- **Options.**
  - (a) Measured by the tool. The build subcommand fetches the session's files in plan order (sequential, unpaced) and times them with `NetworkLogger`, compiled into `mirage_vmaf`, installed only in that process: status, sent, first byte, end, bytes.
  - (b) Declared in the plan: circular for the evaluation.
  - (c) The scoring reads' timing: paced by decoding and scoring, not the network (`sources.md` §3.3).
  - (d) None in scripted sessions, only from the player's log later.
- **Agent's recommendation**: (a), with (b) allowed only in unit tests of the query, marked "declared". Run 06 measured 2.313 s for a segment answered 2.3 s late. Pacing like a player's buffer would be a later option.
- **Blocks**: WP07, WP10.

**Q18. A session's spans on the reference in top-rung mode.**
- **Options.**
  - (a) Marked "reference, not scored", with no detection there.
  - (b) Taken as 100.
  - (c) Sessions require mezzanine mode, which can't score a stitched ladder against a one-file mezzanine: 76.92% of frames failed (`testgen.md` §3.5).
  - (d) The top rung scored against itself once.
- **Agent's recommendation**: (a), stated in the document. (b) is assumed, not measured; (c) fails stitched ladders; (d) costs a pair for a curve that says nothing.
- **Blocks**: WP07.

**Q19. Decoder events in Phase 3.**
- **Options.**
  - (a) Keyframes and corrupt packets from the demux pass, and frames the offline decode lacks from the report's unpaired frames; decode errors are not recorded, as a difference.
  - (b) The analyzer records each frame's key flag, picture type, `decode_error_flags` and `avcodec_send_packet` errors in the ladder report. This changes the analyzer's decode loop and the report, needs rescoring, and raises the D-007/D-068 question.
  - (c) The build subcommand decodes each played variant again for decode errors, at the cost of a decode per variant (MEASUREMENT PENDING).
- **Agent's recommendation**: (a) for Phase 3. Neither acceptance test nor the composite example needs decode errors. (b) goes through a written plan if the paper wants them.
- **Blocks**: WP06 (scope), WP15.

**Q20. The player's session log.**
- **Options.**
  - (a) Deferred to Phase 4, where §4 needs D-007's plan anyway; Phase 3 defines the log's format so the player can write it.
  - (b) Logging-only hooks in `switch_video`, `read_loop`, `video_decode_loop` and `on_present` now, after a D-007 plan.
  - (c) Parse the existing `switch-trace` text: no code change, but fragile, with no rung and the uptime clock (`sources.md` §6).
- **Agent's recommendation**: (a).
- **Blocks**: WP16.

**Q21. Placing SCTE-35 cue events.**
- **Options.**
  - (a) By tag position: cue-out, cue-in and ad break at the segments the tags precede. A decoded splice time becomes a separate `splice_point` event, and START-DATE goes in the payload, with a warning event when anchors disagree.
  - (b) The splice PTS places the cue when the message has one, else the position.
  - (c) DATERANGE by START-DATE through PDT, else the position.
  - (d) A configurable precedence, `scte35.md` §4.4's recommendation (B, then C, then A).
- **Agent's recommendation**: (a). D-069's tests and the plain tags have only a position, the one public VOD stream's message is immediate, with no PTS (`scte35.md` §8), and vendors disagree. The splice time still appears, as its own event. This differs from `scte35.md`'s recommendation.
- **Blocks**: WP04, WP05.

**Q22. Which decoded messages count as a cue-out and a cue-in.**
- **Options.**
  - (a) `splice_insert` by `out_of_network_indicator`; `time_signal` by the segmentation types 0x22-0x23, 0x30-0x37 and 0x44-0x47; every other message a `scte35_message` event (`scte35.md` §4.4).
  - (b) MediaPackage v1's set.
  - (c) Every Start/End pair of Table 23.
- **In every option** the set is configurable.
- **Agent's recommendation**: (a).
- **Blocks**: WP05.

**Q23. Messages whose CRC fails, and cancelled events.**
- **Options.**
  - (a) Keep a message whose CRC fails, marked `crc_valid: false`, with a warning; keep a cancellation as its own event.
  - (b) Drop both.
  - (c) Keep both silently.
- **Agent's recommendation**: (a), with an option to drop. 9 of the 79 vectors have a bad CRC, 3 of them scte35-go's intended negative tests (`scte35.md` §5), and an event left out can't explain a drop.
- **Blocks**: WP05.

**Q24. The ad break's duration and end.**
- **Options.**
  - (a) Keep every declared duration in the payload (CUE-OUT's value, PLANNED-DURATION/DURATION, `break_duration`, `segmentation_duration`). The break ends at the cue-in when there is one, else at its start plus the first declared duration in a fixed order.
  - (b) CUE-OUT's value wins (Video Stitcher's rule).
  - (c) The message's `break_duration` wins.
- **Agent's recommendation**: (a). In run 10 the cue-in's position gave 2.000 s, equal to `EXT-X-CUE-OUT:2.000`.
- **Blocks**: WP04, WP05.

**Q25. Signalings outside D-070's list** (`#EXT-X-SCTE35`, `#EXT-X-SPLICEPOINT-SCTE35`, Adobe's `#EXT-X-CUE`).
- **Options.**
  - (a) D-070's list only. The others stay `vendor_tag` events and appear in attributions raw, reported as a difference.
  - (b) Add `#EXT-X-SCTE35`, the standard's own tag.
  - (c) Add all three; they reuse the same decoder.
- **Agent's recommendation**: (a) for Phase 3.
- **Blocks**: WP04.

**Q26. A bare `EXT-X-DISCONTINUITY` as an ad boundary** (MediaTailor's stitched playlists keep only it, `testgen.md` §5).
- **Options.**
  - (a) Never inferred: a discontinuity is its own event and is listed as such.
  - (b) An `ad_break (inferred)` from discontinuity pairs.
  - (c) Inference as an option, off by default.
- **Agent's recommendation**: (a). The evaluation mustn't claim SCTE-35 where none was signalled, and the discontinuity still shows (run 10: at +0.000 s).
- **Blocks**: WP04, WP10.

**Q27. DASH period boundaries.**
- **Options.**
  - (a) An interface only: `mirage_vmaf` scores HLS ladders, and D-070 leaves DASH's EventStream as an interface. Reported as a difference.
  - (b) Parse an MPD's periods and EventStreams into events for a DASH presentation of the same content as an HLS ladder.
  - (c) DASH ladder scoring, outside §2's scope.
- **Agent's recommendation**: (a).
- **Blocks**: WP04.

### CLI, formats and readers

**Q28. The subcommands' shape and names.**
- **Options.**
  - (a) Two first-word subcommands of `mirage_vmaf`, one building a document and one querying it (names the owner's; proposed `timeline` and `attribute`), the bare options still scoring.
  - (b) One subcommand that builds and queries in one run.
  - (c) A separate program, as D-017 split `mirage_vmaf` from the player.
  - (d) A third form of `mirage_vmaf`'s options.
- **Agent's recommendation**: (a). The build does the I/O once (playlists, demux passes, session fetch), queries run offline and deterministically, and there is one binary to package.
- **Blocks**: WP09, WP11.

**Q29. The session plan's format.**
- **Options.**
  - (a) JSON with its own version: spans in play order of `{entry, first segment, last segment}`, a session name, an optional fetch base.
  - (b) D-030's `name = value` lines with repeated keys, against its rule that a setting is given once (`src/VMAFCli.cpp:223-225`).
  - (c) A line format, `"<entry> <first>-<last>"`, as the prototype's.
  - (d) Spans in time on the axis instead of segments.
- **Agent's recommendation**: (a), with segment spans. HLS switches at segment boundaries, the JSON reader exists anyway, and time spans can come with the player's log.
- **Blocks**: WP07, WP09.

**Q30. The JSON reader.**
- **Options.**
  - (a) Written in the repository, as its writers are, building a document tree.
  - (b) Written in the repository, streaming large arrays.
  - (c) A header-only library added to D-031's dependency list.
- **Agent's recommendation**: (a), moving to (b) if full-length reports need it: 27969051 bytes of text (`schema.md` §3.3), load cost MEASUREMENT PENDING.
- **Blocks**: WP03, WP12.

**Q31. Checking documents against the JSON Schema file.**
- **Options.**
  - (a) A subset validator in `mirage_tests` for the keywords the schema uses.
  - (b) A Python validator in the eval tooling, a new dependency for standard-library-only tooling.
  - (c) Key tests only, the schema being documentation.
- **Agent's recommendation**: (a).
- **Blocks**: WP08, WP11.

### Test content

**Q32. The encoder of the acceptance tests.**
- **Options.**
  - (a) libx264, skipped where absent.
  - (b) FFmpeg's MPEG-2 encoder, which every build has.
  - (c) libx264 when present, else MPEG-2, the encoder recorded and the same assertions.
- **Agent's recommendation**: (c). Both give clear drops (41.239 against 97.134 to 98.231, and 62.022 against 96.923 to 97.671, `testgen.md` §3.1), and run 10's attributions match for both. An acceptance test that skips shows nothing.
- **Blocks**: WP10.

**Q33. The ad's timestamps in acceptance test 2.**
- **Options.**
  - (a) The ad restarts at the main content's origin: the PTS collision, testgen's default.
  - (b) The ad starts at 1 s, a jump of more than 10 s, where the report's sequences match the playlist's (`testgen.md` §3.5).
  - (c) Continuous timestamps, which the player can seek into.
  - (d) All three, with (a) as the acceptance test proper.
- **Agent's recommendation**: (d). (a) is the hard case for the key; (b) and (c) cover the aligner's and the UI's other cases.
- **Blocks**: WP10, WP12.

**Q34. Committing third-party SCTE-35 vectors.**
- **Options.**
  - (a) Self-made sections only: a writer in the test builds each from field values and computes its CRC.
  - (b) (a) plus scte35-go's Apache-2.0 cases, which include copies of SCTE 35's §14 samples.
  - (c) Also the copies from vendor and SCTE pages ("All Rights Reserved" or no licence stated).
- **Agent's recommendation**: (a) until the owner settles the licences, as with D-008's question.
- **Blocks**: WP05 (third-party vectors only).

### UI (§3.3)

**Q35. The UI plan under D-007, and the panel's place.**
- **Options.**
  - (a) Approve §10 as the written plan, with the panel docked above the controls, lowest in the bottom stack (`ui.md` D1 (A)).
  - (b) The plan, as a fourth tab of the network sidebar.
  - (c) The plan, as a floating window like "VMAF Analysis".
- **Agent's recommendation**: (a), after the headless packages.
- **Blocks**: WP13.

**Q36. Frames no seek can reach** (an ad whose timestamps restart).
- **Options.**
  - (a) The panel says "not reachable by a seek" and offers the segment before the discontinuity, with no change to the decode path.
  - (b) A "play to frame, then pause" request in the render loop, after a D-007 plan.
  - (c) Another way for `Demuxer` to position the hls demuxer, after a D-007 plan.
- **Agent's recommendation**: (a), with the difference reported.
- **Blocks**: WP12, WP13.

**Q37. The panel's key.**
- **Options.**
  - (a) A free letter of the owner's choice among B E J L P R S U X Y Z (`ui.md` §1.5).
  - (b) No key: the View menu and the overlays popover only.
  - (c) A shifted letter beside V's VMAF panel.
- **Agent's recommendation**: none; a matter of the owner's preference.
- **Blocks**: WP13.

**Q38. The panel's settings and the rule that every setting is also in a config file.**
- **Options.**
  - (a) `prefs.txt` counts as the player's config file, with `--timeline`, `--ui-timeline` and `--timeline-report` as CLI options. Snapshot runs ignore `prefs.txt` (`ui.md` D9).
  - (b) A D-030-style `--config` for the player.
  - (c) The panel's settings as CLI options only.
- **Agent's recommendation**: (a), the least new mechanism.
- **Blocks**: WP13.

**Q39. The player's tag ruler and ManifestParser's cue defects** (CUE-OUT-CONT taken as a cue-out, OATCLS dropped, `disc_seq` not counted; `sources.md` §4.2).
- **Options.**
  - (a) Left as they are in Phase 3's headless work, reported.
  - (b) Fix ManifestParser, which changes a tested behaviour (`tests/test_manifest_parser.cpp:95`).
  - (c) Retire the GL ruler for the panel's playlist lane once the panel exists.
- **Agent's recommendation**: (a) now, (c) with the UI package.
- **Blocks**: WP13.

### Difference to report

**Q40. §3.1's "NetworkDebugger" against the code's `NetworkLogger`.**
- **Options.**
  - (a) Keep the code's name, report the difference, and reuse `NetworkLogger` unchanged in the session fetcher.
  - (b) Rename it throughout the player.
  - (c) A headless wrapper named after the work order.
- **Agent's recommendation**: (a).
- **Blocks**: WP07, WP14.

---

## 13. P-entries: the agent's process choices, pending the owner's confirmation

| | Choice | Alternatives | Evidence |
|---|---|---|---|
| P-A | Every time in integer ticks of its stream's time base. The report's seconds are recovered by rounding seconds × den / num and checked against the demux pass's PTS; seconds are derived only for output. | doubles with tolerances | runs 07, 08; run 04 (every PTS agreed) |
| P-B | Frames are placed in segments by a demux pass: byte position against the segment files' `bytes_read` in open order, and frames by position in presentation order, each checked against its packet's PTS. A variant with a mismatch gets no segment map, and a warning. (Applies if Q07 and Q19 leave the analyzer unchanged.) | PTS against EXTINF sums (`sources.md`: 37 of 40, or 39 with a tolerance); segment heads | runs 03, 04 |
| P-C | Media playlists are read with `vmafladder::read_master` (the analyzer's limits); their URL, location and SHA-256 are recorded. | a new reader | `src/VMAFLadder.cpp:293-334` |
| P-D | Playlist tags in a new pure module, `HlsEvents`, on `hls::parse_media_timeline`'s segments by URI line. `HlsAlign` and `ManifestParser` are unchanged. | extending `HlsAlign`; fixing `ManifestParser` (Q39) | `sources.md` §11 choice 4 |
| P-E | The SCTE-35 decoder follows SCTE 35 2023r1's tables as `scte35.md` §3 gives them. CRC with libavutil's `av_crc(av_crc_get_table(AV_CRC_32_IEEE), UINT32_MAX, …)`, base64 with `av_base64_decode`; the module uses libavutil only, as `VMAFAlign` uses its rationals. | an own CRC table | `scte35.md` §3.6; `src/VMAFAlign.h:9-11` |
| P-F | Unit-test sections come from a writer in the test (fields → bytes and CRC). `scte35.md`'s reference decoder is a cross-check while writing, not committed. | third-party vectors (Q34) | `scte35.md` §5 |
| P-G | The session fetcher is sequential and unpaced, one connection per file. `NetworkLogger` is installed in the subcommand's process after the demux passes. Times are steady µs from the session's start, plus one UTC anchor. | pacing; NetworkLogger installed throughout | run 06 |
| P-H | `tests/HttpFileServer.h` gains a delay rule (delay one path's response head by a set time), keeping its other rules. | a second server class | `tests/HttpFileServer.h:31-36` |
| P-I | The acceptance ladders come from testgen's generator ported to `tests/TestLadders.{h,cpp}` (noise source, calm ad, 3 rungs, 8 × 25 frames at 25 fps, the quantizers of `testgen.md` §2.3, measured BANDWIDTH, marked continuity counters). Ground truth is the generator's layout. Made once per run, compiled with optimization in every build type. | the ramp source; busy ad; unmarked counters | `testgen.md` §2, §3.2, §3.4, §4 |
| P-J | Acceptance assertions are key-independent (frame ranges, URIs, sequence numbers, types, offsets), with detection parameters pinned inside run 05's region (T = 6 with the prototype's W = 50 and N = 12, for example), not the tool's defaults. | asserting defaults | run 05 |
| P-K | The composite example is a ctest case with a delay of its own, asserting a measured duration of at least the delay and status 200. The work order's 2.3 s is used only in the eval harness. | 2.3 s in ctest | run 06 |
| P-L | Detection is one deterministic pass over the report's doubles (comparisons of scores, not times), with NaN points skipped; parameters are recorded in every attribution. | — | run 05's prototype |
| P-M | The interval index is a vector sorted by `t` with a running maximum of `t_end`. | an interval tree | run 11: 32 to 44 events per 200-frame document |
| P-N | Documents are written through a temporary file and renamed; numbers through `json::number`; ticks as integers. | — | `src/VMAFReport.h:146-149`, `src/JsonText.h:17-19` |
| P-O | Tests run the programs with `posix_spawn`, as `tests/test_vmaf_ladder.cpp` runs `mirage_vmaf` (`MIRAGE_VMAF_PATH`). | in-process calls only | `tests/test_vmaf_ladder.cpp:286-310` |
| P-P | Each subcommand's config file is in D-030's `name = value` form, one per invocation, recorded by path and SHA-256. | a JSON config | `src/VMAFCli.h:54-63` |
| P-Q | The eval harness lives under `eval/timeline/` in standard-library Python, with records holding `cmd.txt` and `environment.txt` (commit by `git rev-parse`), as P-006's practice. | — | `schema.md` §1.7 |
| P-R | One skeleton commit (WP00) adds every new file, its declarations and its CMake lines, so that the parallel packages never edit `CMakeLists.txt`. | each package edits it | — |
| P-S | When the reference's segment map is refused, the axis falls back to continuous PTS, recorded in `axis.fallback`. (Applies if Q02 picks (c).) | refusing the document | run 09 |
| P-T | A media playlist that is live (no `EXT-X-ENDLIST` and not `PLAYLIST-TYPE:VOD`) is refused, since live is out of scope. | warning only | `src/HlsAlign.h:69` |
| P-U | The subcommands' exit status follows `mirage_vmaf`'s: 0, 1, 2. | — | `tools/mirage_vmaf.cpp:7-17` |
| P-V | The acceptance tests keep their ladders, reports and documents in a directory named by an environment variable (proposed `MIRAGE_KEEP_MEDIA`), for the eval harness and the UI QA run, instead of a new generator program. | a `tools/` generator program | `ui.md` §5.3 asks for such a switch |
| P-W | Work packages are built in worktrees branched from `vmaf/phase-3` (D-066) and merged by the lead in the order of §14; nothing is pushed until the owner says. | — | D-066, hard rule 5 |

---

## 14. Work packages

File ownership is disjoint. A shared file is touched only by the package that owns it (column "Owns").

| WP | Title | New files | Owns (changed) | Unit tests | Depends on | Blocked by |
|---|---|---|---|---|---|---|
| WP00 | Skeleton and build wiring | stubs of every new file below, with §4.6's declarations | `CMakeLists.txt` (new sources into `mirage_vmaf`, `mirage_tests` and later `mirage`; `HlsAlign.cpp` and `NetworkLogger.cpp` into `mirage_vmaf`; optimization for `tests/TestLadders.cpp`) | the presets `macos-debug` and `macos-headless` build, and ctest passes as before | — | — (names follow the recommendations; renamed if Q28 says otherwise) |
| WP01 | Timeline core: events, axis, overlap query, grouping | `src/Timeline.{h,cpp}`, `tests/test_timeline.cpp` | — | axes (a) to (d) on synthetic frame lists with a PTS restart and a forward jump; ticks at exact boundaries; half-open overlap; offsets; groups; variant filters; empty cases | WP00 | Q02, Q13, Q14, Q15 |
| WP02 | Drop detection | `src/DropDetect.{h,cpp}`, `tests/test_drop_detect.cpp` | — | run 05's semantics on synthetic series: a known drop, a control, single-frame context dips, alternating dips (the busy control), a drop before warm-up, NaN gaps, parameter checks | WP00 | Q08, Q09, Q10, Q11, Q12 |
| WP03 | JSON reader | `src/JsonRead.{h,cpp}`, `tests/test_json_read.cpp` | — | numbers (integer 100, exponents such as 1.1792e-05, `schema.md` §1.6), escapes and UTF-8, nesting, duplicate keys rejected, errors with positions, round trip with `JsonText`, a ladder report written in the test | WP00 | Q30 |
| WP04 | Playlist events and plain cue tags | `src/HlsEvents.{h,cpp}`, `tests/test_hls_events.cpp` | — | tags before a URI and between EXTINF and URI; discontinuity numbering; PDT; the cue forms of `scte35.md` §2.1-2.3 written by the test; DATERANGE attributes, malformed kept raw; vendor tags; a live playlist refused | WP00 (WP05 for messages) | Q21, Q24, Q25, Q26, Q27 |
| WP05 | SCTE-35 decoder | `src/Scte35.{h,cpp}`, `tests/test_scte35.cpp` | — | self-made sections: `splice_insert` out/in, immediate, `break_duration` with auto return, `time_signal` with segmentation descriptors (sub-segments), a non-CUEI descriptor, command length 0xFFF, alignment stuffing, `pts_adjustment` wrap mod 2^33, CRC good/bad, truncation, encrypted flag, cancel indicators | WP00 | Q22, Q23, Q34 (third-party vectors only) |
| WP06 | Media ingest: playlists, demux pass, segment maps | `src/TimelineMedia.{h,cpp}`, `tests/test_timeline_media.cpp` | — | HLS written in the test: byte positions, join with PTS check, the PTS-collision case, BYTERANGE, a missing frame → map refused, over HTTP from `HttpFileServer` | WP00 (segments from the existing `hls::parse_media_timeline`) | Q19 (scope), Q07 |
| WP07 | Sessions and the session fetcher | `src/Session.{h,cpp}`, `src/SessionFetch.{h,cpp}`, `tests/test_session.cpp`, `tests/test_session_fetch.cpp` | `tests/HttpFileServer.h` (delay rule) | plan checks (unknown entry, overlap, gaps); the session curve (D-068) with reference spans and lower-rate gaps; switch events; fetch rows with status 200, duration ≥ delay, a 404 and a cut body recorded; the clock anchor | WP00, WP03, WP06 | Q17, Q18, Q29, Q08, Q03, Q40 |
| WP08 | Document writer, reader and schema | `src/TimelineReport.{h,cpp}`, `docs/schema/<name>.schema.json`, `tests/test_timeline_report.cpp` | — | the layout against the schema (Q31's checker); provenance; both hashes, the digest unchanged under a redaction-like rewrite; numbers; write → read round trip; temporary file and rename | WP01, WP03 | Q01, Q04, Q05, Q06, Q16, Q31, Q03 |
| WP09 | CLI subcommands | `src/TimelineCli.{h,cpp}`, `tests/test_timeline_cli.cpp` | `tools/mirage_vmaf.cpp` (dispatch), `src/VMAFCli.cpp` (usage text only) | option and config parsing, precedence, unknown and repeated options; the subcommands as programs on a small fixture (exit status, outputs); the existing `tests/test_vmaf_cli.cpp` passes unchanged | the parser after WP00; the integration after WP01 to WP08 | Q28, Q29, Q10, Q11, Q13, Q16 |
| WP10 | Acceptance tests and the composite example | `tests/TestLadders.{h,cpp}`, `tests/test_attribution_acceptance.cpp` | — | test 1 (degraded segment), test 2 (ad boundary, three timestamp variants), test 3 (scripted session with a delayed segment); the generator's layout checked; content generated twice within a run gives the same bytes | WP09, WP07 | Q32, Q33, Q26, Q12, Q08 |
| WP11 | Evaluation harness, one command | `eval/timeline/README.md`, `eval/timeline/run_all.sh`, `eval/timeline/check.py`, `eval/timeline/slow_server.py`, `eval/timeline/tests/test_check.py` | — | `check.py` against fake documents (standard-library `unittest`, as `eval/ladder/tests/`) | WP09, WP10 | Q28, Q04, Q31 |
| WP12 | UI math, ImGui-free | `src/TimelineView.{h,cpp}`, `src/TimelineMatch.{h,cpp}`, `tests/test_timeline_view.cpp`, `tests/test_timeline_match.cpp`, `tests/test_hls_seek.cpp` | — | `ui.md` §5.1 (view, envelope, hit test, loading, matching, mapping, seek decision); `ui.md` §5.2's seek behaviour as a ctest case | WP01, WP03, WP08 | Q02, Q36, Q01, Q33 |
| WP13 | The timeline panel in the player | `src/TimelinePanel.{h,cpp}` | `src/main.cpp`, `src/PlayerUI.{h,cpp}`; optionally `src/NetInspectorAlign.cpp` (moving `nice_step`/`axis_label`), `src/DebugHUD.{h,cpp}` (ruler, if Q39 (c)) | through WP12; the scripted QA run of `ui.md` §5.3 (outside ctest); `ui.md` §5.4's manual checks | WP12 | Q35, Q36, Q37, Q38, Q39 |
| WP14 | Documents | `docs/PHASE3_STATUS.md`, `docs/PHASE3_DECISIONS_PENDING.md` (§12's questions, with options), `docs/plans/timeline-ui.md` (§10 as the D-007 plan) | `docs/DECISIONS.md` (new P-entries appended only, D-060), `docs/AI_CONTRIBUTIONS.md` (Phase 3 rows), `docs/PAPER_METHOD_NOTES.md` (§3's statements, once measured), `docs/BUILD.md` (if `mirage_vmaf`'s sources change what it links) | cross-reference audit; no document is edited to match the implementation, and differences are listed (§11) | runs alongside; the status document comes last | — |
| WP15 | *Conditional*: analyzer and ladder report changes | — | `src/VMAFAnalyzer.{h,cpp}`, `src/VMAFReport.{h,cpp}`, `tests/test_vmaf_report.cpp`, `tests/test_vmaf_analyzer.cpp` | the new fields; scores unchanged on the committed records' inputs | a written plan approved first | Q07 (b/c), Q19 (b) |
| WP16 | *Later*: the player's session log | `src/SessionLog.{h,cpp}` | `src/main.cpp` (after WP13 has merged: both own it, so the two never run in parallel), `src/Demuxer.cpp`, `src/MediaSession.cpp` | log format round trip; a scripted `--qa-switch` run | WP07 (format), WP13 | Q20, D-007 plan |

**Order.**

1. WP00.
2. WP01 to WP06 in parallel; the parser half of WP09 too.
3. WP07 (needs WP03 and WP06) and WP08 (needs WP01 and WP03).
4. WP09's integration.
5. WP10, then WP11.
6. WP12, then WP13 (after the owner's answer to Q35); WP16 only after WP13.
7. WP14 throughout.

The critical path to the two acceptance tests: WP00 → (WP01, WP02, WP03, WP04, WP06 in parallel) → WP08 → WP09 → WP10. The composite example (test 3) adds WP07.

---

## 15. Risks

- **R1.** The join (§2.1) assumes one packet per presented frame and key-frame-led segments. fMP4, open GOPs, decoders that drop frames and VFR are unmeasured; the PTS check detects a mismatch, but that variant then loses its segment events (MEASUREMENT PENDING on D-011's ladders).
- **R2.** The demux pass re-reads every segment: a second download for remote ladders (MEASUREMENT PENDING), and a playlist could differ from the one scored (VOD only; hash and PTS check).
- **R3.** HTTP status and first byte come from FFmpeg's TRACE/DEBUG wording, which can change; the tests pin FFmpeg 8.1.1's.
- **R4.** The `av_log` callback is process-global (`sources.md` §9). The fetcher must run alone in its process, with no scoring in parallel.
- **R5.** Defaults chosen on synthetic content may mislead on real video. The detection parameters wait for the D-011 measurement (Q11).
- **R6.** Top-rung mode hides a defect present in the reference too (D-002's limit). Mezzanine mode can't score stitched ladders against a one-file mezzanine (`testgen.md` §3.5).
- **R7.** Generating in Debug builds takes 16.941 s per ladder without optimization (`testgen.md` §3.4). The mitigation's effect is MEASUREMENT PENDING.
- **R8.** Generated bytes may differ on another machine or x264 build. The tests compare behaviour, not hashes (`testgen.md` §3.3).
- **R9.** Playlist time depends on EXTINF accuracy. An EXTINF shorter than its frames can make the axis non-monotonic; the tool must check monotonicity and fall back (P-S). Real ladders: MEASUREMENT PENDING.
- **R10.** The player can't seek into a timestamp-reset ad (Q36).
- **R11.** A hand-written JSON reader and schema checker add code to test, and full-length ladder reports are large (Q30, Q31).
- **R12.** The session curve has gaps at lower-frame-rate variants (`schema.md` §3.1). Windows in points cover more time there (Q10).
- **R13.** Published SCTE-35 examples are often invalid (`scte35.md` §5), so the policy questions (Q23, Q24) change what attributions show.
- **R14.** Parallel packages could still conflict in shared files, which P-R and the "Owns" column prevent.
- **R15.** The scripted session's timing is one sequential client's, on localhost in the tests. The paper must say the 2.3 s is injected, and that a player's timing differs (§5.6).
- **R16.** The player's PTS for a variant opened through the master may differ from `mirage_vmaf`'s for the media playlist opened alone, 33-bit wrap included (`sources.md` §12; MEASUREMENT PENDING). The UI's playhead and seeks depend on it.

---

## 16. Open issues (MEASUREMENT PENDING unless stated)

- The join and the axis on fMP4 with `EXT-X-MAP`, on open-GOP H.264 and HEVC, and on D-011's ladders.
- The build subcommand's cost (demux passes, fetch) and the document's size on real ladders; the UI's load time and memory.
- Whether the player's PTS equal `mirage_vmaf`'s for the same frame (R16).
- Detection defaults (Q11), and whether a session's curve should also detect quality changes caused by switches (Q08 (b)).
- The Debug-build generation time after the mitigation (R7).
- Decode errors from H.264 and HEVC decoders (`sources.md` §12); only MPEG-2 was observed.
- SCTE-35 inside MPEG-TS segments and as timed ID3 (D-070's interface; `scte35.md` §10, issue 6).
- SCTE 35-1 2023r2 and 35-2 2026, which weren't read (`scte35.md` §10, issue 1).

---

## Appendix A. This design's runs (`<out>/design-evaluation/runs/`)

| Run | What | Tool |
|---|---|---|
| `01_git_state.txt` | the worktree's commit, branch and status (read-only git) | `tools/git_state.sh` |
| `02_build.txt` | builds the two C++ probes against Homebrew's ffmpeg-full (libavformat 62.12.101, libavcodec 62.28.101, libavutil 60.26.101) | `tools/build.sh` |
| `03_segmap.txt` | the demux pass on 15 playlists of five testgen ladders: files, bytes, packets per segment, key and corrupt flags | `tools/segmap.cpp`, `tools/segmap_all.sh` |
| `04_join_check.txt` | each report frame placed by position and checked against its packet's PTS; PTS shared by two segments | `tools/join_check.py`, `tools/join_all.sh` |
| `05_detection_sweep.txt` | three detectors × W × T × N over 21 curves, against the generator's ground truth | `tools/sweep.py` |
| `06_session_fetch.txt` | the scripted session's 11 files fetched in plan order from a local server that answers one segment 2.3 s late | `tools/session_fetch.sh`, `tools/fetch.cpp`, `tools/slow_server.py` |
| `07_attribute_seconds_as_doubles.txt` | the prototype's attribution with the report's PTS as doubles | `tools/attribute.py`, `tools/attribute_all.sh` |
| `08_attribute_ticks.txt` | the same with PTS in ticks | the same, `TICKS=--ticks` |
| `09_axes.txt` | the ad ladder's boundary frames on three axes | `tools/attribute.py --axes-at` |
| `10_attribute_final.txt` | the final prototype: segments as events, a pair's own inputs; acceptance cases, controls, MPEG-2, origin 1 s, the scripted session | `tools/attribute_all.sh` with `--segments --own-variants` |
| `11_doc_sizes.txt` | the prototype documents' bytes and events | `tools/doc_sizes.py` |
| `12_repro_check.txt` | runs 07, 08 and 10 rerun with the tools as they now are, each stdout identical to its record | `tools/repro_check.sh` |

The prototype's outputs are in `<out>/design-evaluation/out/`: the demux passes' JSON, the session's plan and fetch JSON, and the timeline documents. They hold relative paths only, and are regenerable from the commands above; the testgen media they read is under `<out>/testgen/media/`.
