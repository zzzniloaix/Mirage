# Phase 3 design, library first: the unified event timeline (§3)

A design for the work order's §3 (the unified event timeline, §3.1; attribution queries, §3.2; the UI, §3.3) under the owner's decisions D-068, D-069 and D-070. It is written from the five Phase 3 surveys in `<out>` (`sources.md`, `scte35.md`, `testgen.md`, `schema.md`, `ui.md`), from reading `<repo>` (branch `vmaf/phase-3` at `75cf6e0`, read only), and from a few runs of its own. **Nothing of Phase 3 is implemented.** This document proposes how to build it and lists what the owner has to decide first.

The angle: the timeline is a C++ library with a small, testable API (the data model, the merge, interval queries, detection), free of FFmpeg and ImGui. `mirage_vmaf`'s subcommands are thin wrappers over it, the FFmpeg-facing parts are adapters at its edge, and the player's panel is one more consumer.

**Conventions**

- `<repo>` is the worktree, `<out>` the survey directory, `<scratch>` the session's scratch directory. `file:line` is in `<repo>` at `75cf6e0`. FFmpeg is cited at the tag `n8.1.1`.
- **Numbers.** Every number about the code, the streams or a run comes from a cited `file:line` or URL, from a survey file in `<out>` (named next to it), or from a run of this design. Each run is a record under `<out>/design-library/runs/<name>.txt` with its working directory, exact command, start and end time, full output and exit status; `<out>/design-library/tools/pipeline.sh <repo> <out> <scratch> <homebrew>` reruns them all (Appendix A). Where there is no source the text says MEASUREMENT PENDING.
- **Decisions.** Design choices are owner questions Q01 to Q33 (§12), each with its options, their trade-offs and a recommendation labelled "agent's recommendation"; nothing here is decided. Process choices the agent would make while building are P-entries (§13), named P-A to P-X here and numbered from P-081 when recorded in `docs/DECISIONS.md`; they are pending the owner's confirmation.
- **The work order** is the owner's document as translated in the task, quoted as "the work order". **D-0xx** are entries of `<repo>/docs/DECISIONS.md`.

---

## Where Phase 3 stands (2026-10-31)

- **Done.** Five surveys: the §3.1 sources in today's code (`sources.md`), SCTE-35 as HLS carries it (`scte35.md`, with `scte35_vectors.json`), a prototype of D-069's synthetic ladders (`testgen.md`), the report against §7 (`schema.md`) and the player's UI (`ui.md`). This design, with the runs in `<out>/design-library/`.
- **Not done.** Any Phase 3 code. Nothing in `<repo>` was changed: branch `vmaf/phase-3` is at `75cf6e0`, its status clean (checked before the runs, with read-only git commands).
- **Next.** The owner's answers to the questions of §12. WP0 (the core types and the build skeleton) and the pending-decisions document of WP14 need no answer and can start at once; every other package names the questions that block it (§14).

## 0. Summary

1. **Architecture (§2).** A core library in the namespace `timeline` (pure C++20: no FFmpeg, libvmaf, GL or ImGui), compiled once as a CMake object library and linked into `mirage_vmaf`, `mirage_tests` and the player. Around it, FFmpeg adapters that only `mirage_vmaf` and the tests link: a demux pass that maps frames to segments, a reader of media playlists, and a replay fetch for scripted sessions. On top, two thin subcommands, `mirage_vmaf timeline` and `mirage_vmaf attribute` (names: Q26), and the player's timeline panel.
2. **Inputs (§2.4).** The timeline is built after scoring from the ladder report §2 already writes (D-068: scored once), the media playlists' text, the demux pass, an optional scripted session plan and its replayed HTTP timing. Nothing in the scorer or the ladder report changes unless the owner picks the analyzer-side options of Q02 or Q03.
3. **Time (§3, §4).** Every event carries the reference frame it is placed at (`ref_index`), that frame's media key (discontinuity sequence and PTS in ticks of the reference's time base) and its position on one axis. The axis is the owner's (Q01). This design's runs on the D-069 prototype ladders show why: raw PTS repeat (150 distinct `ref_pts` for 200 frames) and make the query return the wrong segments; sequences laid end to end keep a 3.04 s hole inside a sequence; the reference playlist's time and the reference frame position both put the cue-in exactly at the drop's end (runs `axis-ad`, `axis-ad-origin1`, `axis-degraded`). The agent's recommendation is the reference frame position, with exact integer arithmetic: in doubles, the playlist axis listed the ad's next segment and its CUE-IN as overlapping a drop they only touch, and two axes did the same with the degraded ladder's segment 5 (`axis-ad-float`, `axis-degraded-float`).
4. **Frames to segments (§5.2).** By byte position, as the sources survey proposed: in this design's runs it named the right segment for 200 of 200 packets in each of four rungs of the D-069 prototype ladders, B-frames and restarted timestamps included; the PTS-against-EXTINF rule got 145 of 200 on the ad ladders and 193 of 200 on the degraded one (runs `map-by-pos-*`).
5. **Sources (§5).** VMAF curves per pair (and per session, D-068); playlist segments, discontinuities, PDT, DATERANGE and vendor tags; SCTE-35 from D-070's tags with a splice_info_section decoder; switches from a scripted plan; HTTP timing from a replay recorded by the player's own `NetworkLogger` (Q04), placed on the media span of the segment it fetched; keyframes and corrupt packets from the demux pass, decode errors from the scoring decode (Q03).
6. **Queries and detection (§6).** `timeline::attribute()` returns the events meeting a half-open interval, grouped by §3.1's six sources in a fixed order, each with its offsets from the interval's start; `timeline::detect()` finds drops below a sliding baseline. Every parameter that changes an attribution is an owner question (Q09 to Q14); no default is supplied where nothing measured one.
7. **Output (§8).** A separate, versioned timeline document per run, naming its ladder report by two hashes (Q22 to Q25); per-frame scores are not copied into it.
8. **Tests (§9).** A unit-test file per module; the two acceptance tests of §3.2 on synthetic ladders made in the test from testgen's prototype (D-069), and a session test that answers the work order's example question end to end.
9. **UI (§10).** ui.md's panel, reading the timeline document and calling the same query API; by the agent's reading no recommended change touches the render or decode path, and the measured limit (no seek reaches an ad whose timestamps restart) is Q29.
10. **For the owner.** 33 questions (§12), 13 differences from the work order to report (§11), 24 process choices (§13), 15 work packages (§14).

---

## 1. What the design rests on

### 1.1 Facts from the surveys

| Fact | Where |
|---|---|
| A ladder report keys each frame of a pair by `index` and carries `ref_index`, `dis_index`, `ref_pts`, `dis_pts` (raw best-effort PTS in seconds) and `vmaf`; each input's discontinuity sequences are in `alignment.*.sequences` (D-057) | sources.md §2; `src/VMAFReport.h:57-59` |
| `ref_index` and `ref_pts` agree across the pairs of a run: each of 36000 reference frames has one `ref_pts` in every pair that scored it; 30 fps variants are scored at every other reference frame | schema-survey run 11 |
| `mirage_vmaf` reads only the master; FFmpeg's hls demuxer skips every timeline tag (DISCONTINUITY, PDT, CUE-*, DATERANGE, OATCLS) with a VERBOSE `Skip` line; nothing parses media playlists in the headless build | sources.md §1, §4.1; scte35.md §7 |
| `hls::parse_media_timeline` is pure, gives each segment's `seq`, `start`, `duration`, `uri`, `range`, `disc_seq`, `pdt` and the line of its URI, and ignores cue tags | `src/HlsAlign.h:33-75`, `src/HlsAlign.cpp:478-639` |
| `ManifestParser` has defects for D-070 (CUE-OUT-CONT taken as CueOut, OATCLS dropped, DATERANGE unparsed, `disc_seq` not counted, playlist time called stream time) and needs pugixml | sources.md §4.2; scte35.md §6 |
| No code decodes a splice_info_section; the layout, the CRC (CRC-32/MPEG-2) and `pts_adjustment` mod 2^33 are checked against 79 vectors | scte35.md §3, §5 |
| HTTP status is not an AVOption in 8.1.1: only `av_log` lines or a failed open's `AVERROR_HTTP_*` give it; the analyzer's reads are paced by scoring | sources.md §3.3 |
| `NetworkLogger` records rows with status, request sent, first byte, end and bytes, and has a manual tracking API for plain `avio_open` users | sources.md §3.1; `src/NetworkLogger.h:118-125` |
| The player has no ABR; switches are manual, logged as free text without the rung | sources.md §6 |
| Concealed decode errors leave no trace in a complete report (6 frames with `decode_error_flags` 4, VMAF minimum 0) | sources.md §7 |
| `av_log`'s callback is process-global; one thread replacing it redirected another's lines | sources.md §9 (run `logprobe-concurrent`) |
| The D-069 prototype ladders work: degraded `v270` segment 4 mean 41.239 against 97.134 to 98.231 elsewhere, low frames exactly 100 to 124; the ad's low frames exactly 75 to 124 in `v180`; controls without a drop | testgen.md §3.1, §3.2 |
| Generation of a ladder 2.022 to 2.313 s, scoring 0.457 to 0.515 s; generation at -O0 16.941 s | testgen.md §3.4 |
| With the ad's timestamps from the main content's origin, frames 0 and 75 both have `ref_pts` 10; the report's sequences are 2 where the playlist has 3 | testgen.md §3.5, §3.6 |
| No seek reaches the frames of an ad whose timestamps restart, with FFmpeg 8.1.1's hls demuxer and the player's clamp | ui.md §2.4 (`experiments/hls-seek-probe/run-output.txt`) |
| Per-frame lines are 99.91% of a ladder report over every frame; repeating every score as a timeline entry would grow it from 27969051 to 54462827 bytes | schema.md §3.3 (run 05) |
| `src/` has no JSON reader; the report has no `schema_version`, `tool_version` or `git_commit` | schema.md §1.1, §1.7 |

### 1.2 This design's runs

| Run | What it shows |
|---|---|
| `build-logprobe` | builds the sources survey's log probe (`<out>/probe/logprobe.cpp`, unchanged) against ffmpeg-full; the binary is deleted at the end of the pipeline |
| `logprobe-ad-v360`, `-ad-v180`, `-ad-origin1-v360`, `-degraded-v270` | the analyzer's reads of four rung playlists of testgen's ladders (`<out>/testgen/media/`), as `DecodeCtx::open()` reads an input: every open and close with `bytes_read`, every packet with `pos`, PTS, DTS, key and corrupt flags |
| `map-by-pos-*` | packets to segments by byte position and by PTS against EXTINF, against the generator's layout: by position **200 of 200** in all four; by PTS **145 of 200** in the three ad runs and **193 of 200** in the degraded run; key packets at read positions 0, 25, …, 175, each segment's first |
| `axis-ad` (exact ticks) | the ad ladder (ad from the main content's origin): **150 distinct `ref_pts` for 200 frames**, 200 distinct (sequence, `ref_pts`); on raw PTS the drop's interval [10, 12) s overlaps `main_000.ts` and `main_001.ts` besides the ad's segments; on sequences laid end to end the axis has a **3.04 s step** (frames 124 to 125) and the CUE-IN falls **5.0 s** after the drop's start, after it; on the playlist and frame axes every step is 0.04 s and the CUE-IN is **at the drop's end, +2.0 s** |
| `axis-ad-origin1` | the ad from 1 s: 200 distinct `ref_pts`, but the PTS axis steps by −11.96 s and +12.04 s; the three other axes agree, CUE-IN at the end, +2.0 s |
| `axis-degraded` | all four axes return segment 4 alone for frames 100 to 124 |
| `axis-degraded-float`, `axis-ad-float` | the same in doubles: the interval's end comes out above 5 s (5.000000000000001 on the degraded ladder's sequence and playlist axes, 5.000000000000001 and 5.000000000000002 on the ad ladder's playlist and sequence axes), so the degraded ladder's segment 5 is listed on two axes, and the ad's `main_005.ts`, its DISCONTINUITY and its CUE-IN on the playlist axis, as overlapping a drop they only touch |

The drop intervals given to the axis probe are the ones testgen's tables found (testgen.md §3.1, §3.2; `testgen/logs/15`, `46`, `73`); the probe takes them from its command line and detects nothing itself. Its frame-to-segment assignment is the generator's layout, which the `map-by-pos-*` runs show the byte-position rule reproduces.

---

## 2. Architecture

### 2.1 Layers

```
  consumers (thin)
    mirage_vmaf timeline / attribute (src/TimelineCli.*)      player TimelinePanel (src/TimelinePanel.*)
            │                                                          │
  adapters (FFmpeg; mirage_vmaf, mirage_tests)        UI math (pure; mirage, mirage_tests)
    TimelineMedia: demux pass, playlist read            TimelineView: view, envelopes,
    TimelineFetch: replay through NetworkLogger         hit test, playhead, seek decision
            │                                                          │
  core (pure C++20; mirage_vmaf, mirage_tests, mirage) ◄───────────────┘
    TimelineTime · TimelineModel · TimelineAxis · TimelineBuild · TimelineQuery · TimelineDetect
    TimelineDoc · JsonRead · LadderReportRead · PlaylistEvents · Scte35 · Scte35Cues · SessionPlan
    (calls HlsAlign's parse_media_timeline and JsonText, both pure, already in the tree)
```

- **Core**: no FFmpeg, libvmaf, GL or ImGui header. It takes plain structs and text, and returns plain structs and text. Everything that changes an attribution lives here, so the CLI and the panel cannot disagree (ui.md §2.2 recommends the same for the panel).
- **Adapters**: the only code that opens inputs. Each returns a core struct (`VariantMedia`, `PlaylistText`, `Request` rows), so a test can build those structs by hand and test the core without media.
- **Consumers**: parse options, call adapters and the core, write or draw. No logic of their own beyond formatting.

### 2.2 Files

New files, by layer (names are the agent's; the owner may rename):

| File | Layer | Contents |
|---|---|---|
| `src/TimelineTime.{h,cpp}` | core | exact rationals and ticks; seconds ↔ ticks of a time base; checked 128-bit rescaling (P-B) |
| `src/TimelineModel.{h,cpp}` | core | `Source`, `Event`, `MediaKey`, `Curve`, `Variant`, `Session`, `Timeline`; canonical order; an interval index for `overlapping()` |
| `src/TimelineAxis.{h,cpp}` | core | the reference axis (Q01): positions of reference frames, the media key of each, placement of a variant's frame through its pair |
| `src/TimelineBuild.{h,cpp}` | core | the merge: ladder scores + playlists + media facts + session → `Timeline` |
| `src/TimelineQuery.{h,cpp}` | core | `attribute()`: events meeting an interval, grouped by source, with offsets |
| `src/TimelineDetect.{h,cpp}` | core | `detect()`: drops below a sliding baseline |
| `src/TimelineDoc.{h,cpp}` | core | the timeline document: writer (with `JsonText`) and reader (with `JsonRead`) |
| `src/JsonRead.{h,cpp}` | core | a JSON reader (Q27) |
| `src/LadderReportRead.{h,cpp}` | core | a ladder report → `LadderScores`; the canonical scores text for the digest (Q23) |
| `src/PlaylistEvents.{h,cpp}` | core | a media playlist's segments (through `hls::parse_media_timeline`) and its tags, each attached to the segment whose URI follows it; DATERANGE attributes |
| `src/Scte35.{h,cpp}` | core | splice_info_section decoder: base64 and hex input, CRC (P-H), splice_insert, time_signal, descriptors, segmentation_descriptor, `pts_adjustment` |
| `src/Scte35Cues.{h,cpp}` | core | D-070's tags and decoded messages → cue events: out, cont, in, break spans, pairing, anchors (Q15 to Q18) |
| `src/SessionPlan.{h,cpp}` | core | the session plan (Q05) → spans; switch events; the session curve (D-068) |
| `src/TimelineMedia.{h,cpp}` | adapter | the demux pass (Q02): per scored playlist, files with `bytes_read` in open order, packets with `pos`, PTS, flags; frames → segments; keyframes; corrupt packets |
| `src/TimelineFetch.{h,cpp}` | adapter | the replay fetch (Q04) through `NetworkLogger`'s manual tracking |
| `src/TimelineCli.{h,cpp}` | consumer | the subcommands' option tables, config files (D-030), `--help`, and their `run_*()` functions |
| `src/TimelineView.{h,cpp}` | UI math | ui.md §2.5's view, envelopes, hit testing, playhead mapping and seek decision |
| `src/TimelinePanel.{h,cpp}` | UI | the ImGui panel (player only) |
| `tests/SyntheticLadder.{h,cpp}` | test | D-069's generator, from testgen's prototype (`<out>/testgen/src/testgen.cpp`) |
| `tests/test_timeline_*.cpp`, `tests/test_json_read.cpp`, `tests/test_scte35.cpp`, `tests/test_playlist_events.cpp`, `tests/test_session_plan.cpp`, `tests/test_ladder_report_read.cpp`, `tests/test_attribution_acceptance.cpp`, `tests/test_hls_seek_behaviour.cpp` | test | §9 |
| `docs/PHASE3_STATUS.md`, `docs/PHASE3_DECISIONS_PENDING.md`, `docs/TIMELINE_FORMAT.md`, `docs/schema/mirage-timeline-1.schema.json` | docs | §14, WP8, WP14 |

Shared files and the one package that owns each (§14): `CMakeLists.txt` (WP0's structure; each package one block of its own), `tools/mirage_vmaf.cpp` and `src/VMAFCli.{h,cpp}` (WP9), `src/VMAFAnalyzer.{h,cpp}`, `src/VMAFReport.{h,cpp}`, `tests/test_vmaf_report.cpp`, `tests/test_vmaf_analyzer.cpp` (WP6, only if Q03 is answered (a)), `tests/HttpFileServer.h` (WP7), `src/main.cpp`, `src/PlayerUI.{h,cpp}`, `src/NetInspectorAlign.cpp` (WP13), `docs/DECISIONS.md` and the other documents (WP14). Not changed by any package: `src/HlsAlign.*`, `src/VMAFLadder.*`, `src/NetworkLogger.*`, `src/Demuxer.*`, `src/Decoder.*`, `src/MediaSession.*`, `src/VideoRenderer.*`.

### 2.3 Build

- The core is a CMake `OBJECT` library, say `mirage_timeline_core`, linked into `mirage_vmaf`, `mirage_tests` and `mirage`, with no FFmpeg, libvmaf, GL or ImGui target on its link line (P-A). `HlsAlign.cpp` and `JsonText.cpp`, which the core calls and which have no FFmpeg dependency (`src/HlsAlign.h:12`, `src/JsonText.h:1-21`), stay in each executable's own source list, as today (`CMakeLists.txt:190, 245, 313, 319`); `mirage_vmaf` gains `src/HlsAlign.cpp`, and, for the replay (Q04 (a)), `src/NetworkLogger.cpp`, which needs only FFmpeg (`src/NetworkLogger.cpp:15-20`); the player, which lists `HlsAlign.cpp` and `FileHash.cpp` but not `JsonText.cpp` (`CMakeLists.txt:150-194`), gains `src/JsonText.cpp`.
- The adapters and `TimelineCli` go into `mirage_vmaf` and `mirage_tests`; `TimelineView` into `mirage` and `mirage_tests`, which links no ImGui (D-031); `TimelinePanel` into `mirage` only.
- The headless presets (`MIRAGE_HEADLESS`) build everything but the panel.

### 2.4 What `mirage_vmaf timeline` does

1. **Read the ladder report** (`LadderReportRead`, core): its settings, the master's entries, the reference, each complete pair's frames, failures, sequences and libvmaf contexts. Compute the report's SHA-256 and its scores digest (Q23); a layout older than `75cf6e0`'s is refused (P-K).
2. **Read each scored playlist's text** (adapter): the reference's and every pair's distorted playlist, through `vmafladder::read_master`, which reads any `#EXTM3U` text with the analyzer's I/O limits and records its location and SHA-256 (`src/VMAFLadder.cpp:293-334`; P-C). Parse it (`PlaylistEvents`, core).
3. **Run the demux pass** (`TimelineMedia`, adapter; Q02 (a)): each playlist opened as the analyzer opens an input, its video packets read without decoding, mapped to segments by byte position, then to the report's frames (P-D, P-E).
4. **Build the axis** (`TimelineAxis`, core; Q01) from the reference's frames, and **merge** (`TimelineBuild`, core): curves per pair, events per source (§5).
5. **With a session plan** (`SessionPlan`, core; Q05): spans, switch events, the session's curve; then **replay its fetches** (`TimelineFetch`, adapter; Q04) and add the requests as network events.
6. **Detect and attribute** (`TimelineDetect`, `TimelineQuery`, core; Q09 to Q14).
7. **Write the document** (`TimelineDoc`, core; Q22 to Q25) through a temporary file renamed over `--out`, as `vmafreport::write_file` does (`src/VMAFReport.h:146-149`).

`mirage_vmaf attribute` reads a document and the ladder report it names (checking both hashes), and runs step 6 for the intervals it is given.

---

## 3. The data model

### 3.1 Time

- **Exact.** Times are whole ticks, never seconds in doubles (P-B). The report gives PTS in seconds; the reader turns each into ticks of its input's time base (`ref.time_base`, `dis.time_base`, `src/VMAFReport.cpp:20-29`) by rounding `pts × den / num`, as the axis probe does. The aligner already compares exact ticks (`src/VMAFAlign.h:17-22`). Runs `axis-degraded-float` and `axis-ad-float` show what doubles do at a boundary: an interval end of 5.000000000000001 and a false overlap.
- **The media key** (`MediaKey`): for the reference frame an event is placed at, its `ref_index`, its discontinuity sequence (the report's reference sequences, D-057) and its PTS in ticks. This is "keyed by media PTS" made unique: (sequence, PTS) was unique for all 200 frames where PTS alone had 150 values (run `axis-ad`).
- **The axis** (`Axis`): one position per reference frame, in integer ticks of a declared tick length, with seconds = ticks × tick (an exact rational). What a tick is depends on Q01; under the agent's recommendation (d) a tick is one reference frame, so a position is a `ref_index` and its seconds are `ref_index × frame_duration`, with `frame_duration = 1 / alignment.reference.frame_rate` (a rational, `"25/1"` in testgen's reports). Every event is placed at a reference frame (§5's placement rules), so every position is an integer under every option.
- **Wall clock** is kept only for observed events (the replay's requests), in seconds from the session's start on one steady clock, with one UTC anchor per session (schema.md §3.4's recommendation). Offline events have none; a segment's EXT-X-PROGRAM-DATE-TIME stays in its payload.

### 3.2 Events

```cpp
// One thing that happened, or holds, on the reference's timeline.
struct Event {
    Source      source = Source::Vmaf;   // §3.1's six sources (§5)
    std::string type;                    // the vocabulary of §5, "cue_out", "segment", ...
    std::string id;                      // unique in the timeline, deterministic (P-F)
    int64_t     start = 0;               // axis position, ticks
    int64_t     end   = 0;               // exclusive end of a span; == start for a point
    MediaKey    key;                     // the reference frame at start: ref_index, sequence, PTS
    int         variant = -1;            // index into Timeline::variants; -1 = ladder-wide
    std::optional<double> wallclock;     // seconds from the session's start; observed events only
    Payload     payload;                 // ordered name -> value: what the source says
};
```

- Points and spans are one type: a segment, an ad break, a session's span and a request are spans; a tag, a switch and a keyframe are points (schema.md §3.4 recommends an end field for every span).
- `payload` is an ordered list of name and value, written as a JSON object in that order; values are null, bool, integer, number, string, list or object.
- `variant` says whose event it is: a segment of `v270`'s playlist belongs to `v270`; an event of the session belongs to the session (`variant` -1, the played variant in its payload).

### 3.3 Curves and sessions

- **A pair's curve** (`Curve`): the pair's scored frames as (`ref_index`, position, `vmaf`), ascending, and the positions where a libvmaf context starts (`alignment.vmaf_contexts`, D-037), which detection may leave out (Q12). A 30 fps variant against a 60 fps reference has a point at every other reference frame (schema-survey run 11); the curve keeps those points only.
- **A session** (D-068): spans of the axis, each with the variant played; its curve is, at each reference frame of a span, the score of that span's variant there, taken from that variant's curve. It is derived, not stored (schema.md §3.3). Spans that play the top rung in top-rung mode have no score (Q07).

### 3.4 Variants and rungs

`Timeline::variants` lists each scored playlist once (a pair's distorted playlist, and the reference): its master entries (the report's `pairs[].entries`, `reference.entries`), URL, the BANDWIDTH, RESOLUTION and CODECS of its entries, the pair index (-1 for the reference) and the playlist's SHA-256. How a rung is numbered in the output is Q06.

### 3.5 Order and determinism

- Events are kept in one canonical order: start, end, source, type, variant, id (P-F). Identical inputs give byte-identical documents, apart from the replay's measured times (§5.7).
- `overlapping(a, b)` answers from an index built once (start-sorted events with a running maximum of ends), checked against brute force on random events in the tests.
- Nothing in the core reads a clock, the environment or a file.

---

## 4. The single time base (Q01)

§3.1 asks for a timeline keyed by media PTS, with the wall clock recorded as well, and for a single time base on which variants are compared along the reference variant's timeline. The four candidates, as computed on testgen's ladders in exact ticks:

| Axis | `axis-ad` (ad PTS from the main origin) | `axis-ad-origin1` (ad PTS from 1 s) | `axis-degraded` | Needs |
|---|---|---|---|---|
| (a) raw reference PTS | 150 distinct of 200; not rising (−2.96 s, +3.04 s); the drop [10, 12) s overlaps `main_000.ts`, `main_001.ts`, `ad_000.ts`, `ad_001.ts`; CUE-IN after, +5.0 s | 200 distinct; not rising (−11.96 s, +12.04 s); CUE-IN after, +14.0 s | segment 4 only | the report |
| (b) (sequence, PTS), sequences end to end | rising; one step of 3.04 s inside sequence 1; CUE-IN after, +5.0 s; `ad_001.ts` spans [4, 8) s | rising, 0.04 s steps; CUE-IN at the end, +2.0 s | segment 4 only | the report |
| (c) reference playlist time | rising, 0.04 s steps; CUE-IN at the end, +2.0 s | the same | segment 4 only | the frame→segment map, and EXTINF that matches the media |
| (d) reference frame position | rising, 0.04 s steps; CUE-IN at the end, +2.0 s | the same | segment 4 only | the report (and the frame rate) |

Reading the table:

- (a) fails the D-069 ad test as testgen recommends making it (testgen.md §4, choice 4 (a)): the query would name main-content segments for the ad's drop.
- (b) is exact and needs nothing but the report, but the report's sequences start only at a backward step or a forward jump of more than 10 s (`kForwardJump`, `src/VMAFAlign.h:114`; testgen.md §3.5), so a forward jump inside a sequence stays a hole in the axis, and offsets count it.
- (c) is the clock tags, segments, HTTP rows and the hls demuxer's seeks share (ui.md §1.6), but it inherits EXTINF's error: in the sources survey's fixture the muxer's last EXTINF was 0.36 s for 10 frames at 25 fps (sources.md §2, run `fixture`), which would put frames past the next segment's start and break the axis's order. RFC 8216 lets EXTINF be rounded (integers before version 3, [§4.3.2.1](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.2.1)).
- (d) needs nothing beyond the report, gives integers, and agrees with (c) on all three runs. It is wrong where the reference's frame rate varies; the axis then warns where a frame's PTS differs from its position by more than half a frame within a sequence (P-B).

Agent's recommendation: (d), each event also carrying the (sequence, PTS) of its reference frame as its media key (§3.1) and, where its segment is known, its playlist time in the payload. Offsets are then seconds of reference media in frames, not PTS differences: a difference from §3.1's wording to report (§11, item 4).

---

## 5. From each §3.1 source to events

### 5.1 Placement rules

Every event is placed at a reference frame:

| Anchor | Position |
|---|---|
| a reference frame | itself |
| a distorted frame of a pair | the reference frame it is paired with (`frames[]`); a distorted frame that failed to pair, at the next paired frame of its input (P-V) |
| a segment of the reference | its first frame (the demux pass, §5.2); a span to the next segment's first frame |
| a segment of a distorted variant | its first frame that paired, through the pair; a span to the next segment's |
| a playlist tag | the segment whose URI follows it, by line (`Segment::line`, `src/HlsAlign.h:46`): a tag between `#EXTINF` and the URI belongs to that URI's segment (scte35.md §2, §6) |
| an SCTE-35 splice PTS | the first reference frame of the tag's sequence whose PTS is at or after `(pts_time + pts_adjustment) mod 2^33`, the standard's Splice Point (scte35.md §3.2); used as the anchor only as Q15 says |
| a session's span | the reference frame at or after the plan's time (P-P) |
| an HTTP request for a segment | the media span of the segment it fetched (P-U) |

An event whose anchor cannot be placed (a segment of which no frame paired, a splice PTS outside its sequence) is kept with no position in a `warnings` list of the document, never dropped silently.

### 5.2 Media facts: the demux pass (Q02)

- **What it reads.** Each scored playlist, opened as `DecodeCtx::open()` opens an input: `avformat_alloc_context()`, the interrupt callback, `vmafnet::set_io_limits`, `vmafsegments::set_hls_options` (one connection per file, `seg_max_retry` 10), hooks on `io_open` and `io_close2` (`src/VMAFAnalyzer.cpp:166-194`, `src/VMAFSegments.h:26-40`), `avformat_find_stream_info`, the best video stream. Then `av_read_frame` to the end, no decoder (P-E).
- **What it records.** Each file opened, in order, with `bytes_read` at close; each video packet's `pos`, PTS, DTS and flags (`AV_PKT_FLAG_KEY`, `AV_PKT_FLAG_CORRUPT`).
- **Packets to segments.** By byte position: segment k holds the positions from the sum of the sizes of the segments opened before it to that plus its own (sources.md §11, choice 2 (a)). 40 of 40 on the sources survey's fixture (sources.md §2) and 200 of 200 on each of four rungs here, B-frames and restarted timestamps included (runs `map-by-pos-*`). The PTS-against-EXTINF rule, FFmpeg's own placement rule, got 145 of 200 on the ad ladders and 193 of 200 on the degraded one (same runs), so it is not used.
- **Packets to frames.** Within a segment the frames are its packets in PTS order. The pass checks itself against the report: the PTS of the reference's k-th frame must equal the report's `ref_pts` at `ref_index` k for every scored frame, and likewise for each pair's distorted frames; a mismatch leaves that playlist's segment events unplaced, with a warning (P-D).
- **Also from the pass**: keyframes (key packets: at each segment's first packet in the runs above), corrupt packets (testgen measured 2 packets flagged with continuity counters left unmarked at the splice, testgen.md §3.6), and the reference's PTS for reference frames no pair scored.
- **Unmeasured**: fMP4 with EXT-X-MAP, ID3-timestamped audio, byte ranges, and the pass's time on real ladders, local or over HTTP: MEASUREMENT PENDING.

### 5.3 VMAF

- **Curves** per pair (§3.3), from `pairs[].frames`.
- **Events** (`source: vmaf`): `context_start` at the first pair of each libvmaf context but the first (`alignment.vmaf_contexts`), since that pair scores low (D-037; testgen.md §3.6 item 7: frame 75 of `v270` is its only frame more than 6 points below its median, the first of a context). The per-frame scores are not copied into events or into the document (Q25).
- **Drops** are attributions (§6), not events.

### 5.4 Playlist (HLS)

`PlaylistEvents` runs `hls::parse_media_timeline` for the segments, then scans the same text once more for tags, attaching each to the segment whose URI line follows it. No existing parser changes: `ManifestParser` stays the player's, with its tested behaviour (`tests/test_manifest_parser.cpp:95`; sources.md §11, choice 4).

| Type | From | Placed | Payload |
|---|---|---|---|
| `segment` (span) | each segment | its frames (§5.1) | URI, media sequence, EXTINF, byte range, discontinuity sequence, PDT (ISO 8601 if present), bytes (the demux pass), playlist time |
| `discontinuity` | `EXT-X-DISCONTINUITY` | the next segment's first frame | the playlist's discontinuity sequence after it |
| `program_date_time` | `EXT-X-PROGRAM-DATE-TIME` | the next segment | the date as written and as Unix seconds (`hls::parse_iso8601`) |
| `gap` (span) | `EXT-X-GAP` | its segment | |
| `daterange` | `EXT-X-DATERANGE` without `SCTE35-*` | position; START-DATE in the payload | every attribute, quoted values unquoted, as RFC 8216 §4.3.2.7 lists them |
| `tag` | any other `#EXT-X-*` or `#EXT-*` tag not consumed above | the next segment | name, raw value |

A bare `EXT-X-DISCONTINUITY` stays a `discontinuity` (Q20). DASH periods: `mirage_vmaf` scores HLS ladders only (`src/VMAFLadder.h:15-22`), and D-070 leaves DASH as an interface; the playlist source keeps a `period` type in its vocabulary for that interface, unused in Phase 3.

### 5.5 SCTE-35 (D-070)

- **Tags read**: `EXT-X-CUE-OUT` (bare, `:30.000`, `:DURATION=…`, with `K=V` and `ID`/`X-TYPE` attributes), `EXT-X-CUE-OUT-CONT` (`8.308/30` and `ElapsedTime=…,Duration=…,SCTE35=…`), `EXT-X-CUE-IN` (bare or with `ID`), `#EXT-OATCLS-SCTE35` (base64), and `EXT-X-DATERANGE`'s `SCTE35-OUT`, `SCTE35-IN`, `SCTE35-CMD` (hex), in every form scte35.md §2 quotes. Others (Q19) stay playlist `tag` events.
- **Decoded** (`Scte35`): the splice_info_section layout of scte35.md §3 (SCTE 35 2023r1 Tables 5, 9 to 15, 17 to 20, 27, 28): splice_insert, time_signal, break_duration, splice_time with `pts_adjustment` added mod 2^33, splice_descriptor loop with unknown descriptors skipped by length, segmentation_descriptor with UPID bytes kept. The CRC_32 is CRC-32/MPEG-2 (scte35.md §3.6), computed in the core (P-H). Encrypted messages (`encrypted_packet` 1) are reported undecoded, encrypted streams being out of scope.
- **Events** (`source: scte35`, by `Scte35Cues`):

| Type | When | Payload |
|---|---|---|
| `cue_out` | a CUE-OUT, an OATCLS or DATERANGE whose message is an out (Q16) | the form, the raw line, the tag's duration, the decoded message, `crc_valid`, every anchor with its offset from the position (Q15) |
| `cue_out_cont` | a CUE-OUT-CONT | elapsed, duration, the message if any |
| `cue_in` | a CUE-IN, or an in message | the same fields |
| `break` (span) | a cue_out paired with the next cue_in of the same event, or with its duration when none (Q18) | the durations each source gives |
| `splice` | a decoded message that is neither out nor in (Q16) | its command and segmentation types |
| `cancel` | a message whose cancel indicator is set (Q16) | its event id |

- **Placement**: by Q15. The D-069 ad ladder's cue tags carry no message (testgen.md §2.3), so its events are placed by position whatever Q15 says.
- **Interfaces only** (D-070): DASH `EventStream`; SCTE-35 inside MPEG-TS segments. The one public VOD stream found carries it as an ID3 PRIV frame, not a 0x86 stream (scte35.md §8), and whether either reaches `mirage_vmaf`'s demuxing is MEASUREMENT PENDING (scte35.md §10, item 6).

### 5.6 Player (ABR and switches)

Mirage has no ABR (D-068; sources.md §6). Phase 3's switches come from a scripted plan (D-068), in the format of Q05, read by `SessionPlan`:

| Type | Placed | Payload |
|---|---|---|
| `span` (span) | from the plan's time to the next span's, on reference frames (P-P) | the variant played: entries, URI, BANDWIDTH, rung (Q06); `scored: false` where it is the reference in top-rung mode (Q07) |
| `switch` | at each span boundary after the first | `from` and `to` as above, `origin: "plan"` |

The player's own log, with the demuxer's take-over point and the frames dropped at a switch (sources.md §6), is Q08; the format of Q05 is chosen to hold it later.

### 5.7 Network

- **Recorded by** the replay (Q04 (a)): after the plan's spans are known, the segments each span plays are fetched in the session's order: for each span, the segments of its variant whose media spans meet it, consecutive repeats dropped, each variant's playlist first. Each fetch is one plain `avio_open2`, a read to the end and a close, tracked by `NetworkLogger::track_open_begin`, `track_open_end` and `track_close` (`src/NetworkLogger.h:118-125`) with the logger installed for the replay only (P-M). Its rows give status, request sent, first byte, end and bytes (sources.md §3.1).
- **Events** (`source: network`, type `request`): placed on the media span of the segment fetched (a playlist fetch at the first span that plays its variant, P-U), with `wallclock` = the request's start from the session's start, and in the payload the URL, variant, media sequence, the four times, duration, bytes, status, error and `origin: "replay"`.
- **What it is not**: a player's fetch pattern. The replay is sequential, with no buffer and no ABR, so its times are what this network gave a sequential fetch at the time of the run, recorded as such. The scoring reads are not used: they are paced by scoring (sources.md §3.3).

### 5.8 Decoder

| Type | From | Placed |
|---|---|---|
| `keyframe` | key packets of the demux pass (Q02) | the frame |
| `corrupt_packet` | `AV_PKT_FLAG_CORRUPT` in the demux pass | the frame |
| `decode_error` | the scoring decode's `decode_error_flags`, `AV_FRAME_FLAG_CORRUPT`, and `avcodec_send_packet` errors (Q03 (a)) | the frame, the flags in the payload |
| `pairing_failure` | `alignment.failures` of the report: `unmatched_reference`, `extra_distorted`, `duplicate_distorted`, `no_timestamp`, `after_discontinuity` | §5.1 |

The offline decode drops no frame of its own (sources.md §7); a frame a decoder never output shows as a pairing failure. Frames the player drops at a switch would come from its log (Q08), as D-068's limits say.

---

## 6. Queries and detection

### 6.1 The API

Appendix B has the headers. In short:

```cpp
namespace timeline {
// Half-open, on the axis.
struct Interval { int64_t start = 0, end = 0; };

// One event an attribution returns, with its offsets from the interval's start, exact.
struct Found {
    const Event*            event = nullptr;
    Rational                offset;          // event start - interval start, seconds
    std::optional<Rational> end_offset;      // for a span
    bool                    at_end = false;  // a point exactly at the interval's end (Q14)
};

struct Attribution {
    std::string curve;                      // "variant:<k>" or "session:<id>"
    Interval    interval;
    int64_t     first_ref_index = 0, last_ref_index = 0, frames = 0;
    double      baseline = 0, min = 0, mean = 0, drop_magnitude = 0;   // Q13
    std::array<std::vector<Found>, kSourceCount> by_source;        // §3.1's order
};

Attribution attribute(const Timeline& tl, const Curve& curve, Interval iv, const QueryOptions& q);
std::vector<Drop> detect(const Curve& curve, const DetectOptions& d);
}
```

### 6.2 Interval semantics (Q14)

- An interval is half-open on the axis. A drop of reference frames i to j is [pos(i), pos(j + 1)) where frame j + 1 exists, else [pos(i), pos(j) + one frame).
- A span meets it when it starts before the interval's end and ends after its start; a point meets it when it lies in it. A point exactly at the end is returned flagged `at_end` under Q14 (a): the ad's CUE-IN and its DISCONTINUITY in runs `axis-ad` and `axis-ad-origin1` on axes (c) and (d).
- Which events: those of the curve's variant, of the reference, ladder-wide ones, and for a session's curve, those of the variant played in each span (Q14 (a)).
- Grouping: §3.1's six sources in its order (vmaf, network, playlist, scte35, player, decoder); within a group by position, then the canonical order. No ranking (Q14).
- Offsets are exact rationals of seconds, written as numbers in the document; under Q01 (d) they are multiples of the reference's frame duration.

### 6.3 Detection (Q09 to Q13)

`detect()` walks a curve once:

1. A point is low when its score is more than the threshold (Q10) below the baseline at that point (Q11).
2. A drop is a run of low points longer than N (Q12), the first pair of each libvmaf context left out under Q12 (a).
3. Each drop gets its interval (§6.2), its baseline at the first low point, its minimum and mean, and its magnitude (Q13).
4. Under Q07 (a), a session's curve is split at spans without scores, and each part is walked on its own.

Every parameter goes into the document (§8), with the config file it came from, since each changes the attributions as the scoring options change the scores (§7's rule, schema.md §2.3).

What would inform the values nobody measured (W and N, and a test of the threshold): detection over the two D-069 ladders and D-011's ladders across a grid of window and N values, counted against the segments known to be degraded. The synthetic ladders bound N only on themselves: single-frame dips at context starts (testgen `logs/46`), runs of 1 to 3 frames in the busy control (`logs/59c`), drops of 25 and 50 frames (`logs/15`, `logs/46`). D-011's ladders: MEASUREMENT PENDING.

### 6.4 The answer to the work order's example

The work order's example answer: a drop that overlapped an SCTE-35 cue-out, a switch between two numbered rungs, and a segment request of 2.3 s. Each part is an event of a group:

- the cue-out: `scte35` group, type `cue_out`, offset from the drop's start;
- the switch: `player` group, type `switch`, `from.rung` and `to.rung` (Q06);
- the segment: `network` group, type `request`, its segment's media span meeting the drop, `duration` in its payload, measured by the replay (Q04).

On the D-069 ad ladder, the frame axis gives (run `axis-ad`, axis `index`): the drop of frames 75 to 124 is [3.000, 5.000) s; `EXT-X-CUE-OUT:2.000` and the first `EXT-X-DISCONTINUITY` at +0.000 s, `ad_000.ts` at [3.000, 4.000), `EXT-X-CUE-OUT-CONT:1.000/2.000` and `ad_001.ts` at +1.000 s, and `EXT-X-CUE-IN` with the second `EXT-X-DISCONTINUITY` at the end, +2.000 s. The text form of `mirage_vmaf attribute` (P-T) would print, for that drop:

```
drop on v180/index.m3u8 (entry 2, rung <Q06>): reference frames 75-124, 3.000-5.000 s
  baseline <Q11>, minimum <from the curve>, drop <Q13>
  scte35:   cue_out (EXT-X-CUE-OUT:2.000) +0.000 s · cue_out_cont +1.000 s · cue_in at the end +2.000 s
            break 3.000-5.000 s
  playlist: discontinuity +0.000 s · segment ad_000.ts 3.000-4.000 s · segment ad_001.ts 4.000-5.000 s
            discontinuity at the end +2.000 s
```

The values in angle brackets come from the owner's choices; the positions are the run's.

---

## 7. CLI

Subcommand names and their split are Q26; under its option (a):

```
mirage_vmaf timeline --report <ladder report.json> --out <timeline.json>
                     [--session <plan>] [--replay on|off]
                     [--curves variants|session|both] [--threshold X]
                     [--baseline <statistic>] [--window W] [--min-frames N]
                     [--context-first skip|keep] [--config FILE]
mirage_vmaf attribute --timeline <timeline.json> [--report <ladder report.json>]
                      (--interval <a>:<b> | --frames <i>:<j> | --drop <k>)
                      [--curve variant:<k>|session:<id>] [--format json|text]
                      [--out <file>] [--config FILE]
```

- `tools/mirage_vmaf.cpp` dispatches on its first argument: a subcommand word goes to `timelinecli::run_timeline()` or `run_attribute()`; anything else goes to today's parser unchanged. Today an argument that isn't an option is "unknown argument" (`src/VMAFCli.cpp:288`), so no existing command line changes meaning.
- Every option is also a config-file setting, `name = value` through the same option table, as D-030 has it for `mirage_vmaf` (`src/VMAFCli.h:54-65`); the command line replaces the file; the file's path and SHA-256 go into the document. The table-driven parser of `src/VMAFCli.cpp:92-344` is made reusable for the subcommands, its behaviour and `--help` for today's forms unchanged and still pinned by `tests/test_vmaf_cli.cpp` (P-G).
- Exit status as `mirage_vmaf`'s: 0 written, 1 a failure (no document left at `--out`), 2 bad arguments or config file (`tools/mirage_vmaf.cpp:6-17`).
- Defaults for the detection options exist only where the owner gives them (Q10 to Q12); until then the options are required.
- Each subcommand is a thin wrapper: parse, call the adapters and the core, write. Its tests are the option tables and one end-to-end run each through `MIRAGE_VMAF_PATH`, as `tests/test_vmaf_ladder.cpp` runs `mirage_vmaf` (`CMakeLists.txt:345-349`).

---

## 8. The timeline document

Where it goes is Q22 (agent: B, a separate document), how it names the ladder report Q23, its versioning Q24, its layout Q25. The proposed layout, §7's names where §7 has the field:

```
{
  "schema": "mirage-timeline", "schema_version": 1,
  "tool": "mirage_vmaf timeline",
  "ladder_report": { "path", "sha256", "scores_sha256", "status", "reference_mode",
                     "model", "model_source", "model_sha256", "scaler",
                     "libvmaf_version", "ffmpeg_libraries" },          // copied (P-X)
  "config_file": { "path", "sha256" } | null,
  "session_plan": { "path", "sha256" } | null,
  "playlists": [ { "variant", "url", "location", "sha256" } ],
  "axis": { "kind": "reference-frames", "tick": "1/25", "frames",
            "reference": { "url", "time_base", "frame_rate", "sequences": [...] },
            "warnings": [ ... ] },
  "variants": [ { "variant", "entries", "url", "bandwidth", "resolution", "codecs",
                  "rung", "pair" } ],
  "timeline": [ { "t", "end", "ref_index", "seq", "pts", "wallclock",
                  "source", "type", "variant", "id", "payload" } ],
  "sessions": [ { "id", "wallclock_anchor", "spans": [ { "t", "end", "variant" } ] } ],
  "detection": { "curves", "threshold", "baseline", "window", "min_frames",
                 "context_first" },
  "attributions": [ { "curve", "interval": { "t", "end", "first_ref_index",
                                             "last_ref_index", "frames" },
                      "baseline", "min", "mean", "drop_magnitude", "mean_drop",
                      "overlapping_events": { "vmaf": [ { "id", "offset", "end_offset",
                                                          "at_end" } ],
                                              "network": [...], "playlist": [...],
                                              "scte35": [...], "player": [...],
                                              "decoder": [...] } } ],
  "warnings": [ ... ]
}
```

- `t` and `end` are seconds on the axis (the exact rational written with `json::number`, `src/JsonText.h:17-19`), `ref_index` the frame, `seq` and `pts` its media key (`pts` in seconds, as §7 names it; ticks over `axis.reference.time_base` for an exact reading).
- `overlapping_events` refers to `timeline[]` entries by `id`, so each event is written once.
- Per-frame scores are not copied: the document is read together with the ladder report it names (schema.md §3.3: 99.91% of a full report is per-frame lines). A reader checks the report's SHA-256, or its scores digest for a committed, redacted copy (Q23).
- `schema_version` and a JSON Schema file, `docs/schema/mirage-timeline-1.schema.json`, come with it under Q24 (a). ctest checks the writer's keys and types, as `tests/test_vmaf_report.cpp` checks the report's; no validator runs in ctest unless the owner adds a dependency (schema.md open issue 3).
- The commit is recorded by the eval tooling, as P-006 has it for the report (`docs/DECISIONS.md:791`), until Phase 5 decides §7's `git_commit` (P-J).
- Size: MEASUREMENT PENDING (nothing is built). Its per-segment events grow with the segments, 100 per variant in Apple's advanced sample (schema-survey run 06), not with the frames.

---

## 9. Tests

### 9.1 Unit tests (ctest, nothing downloaded)

| File | Package | What it checks |
|---|---|---|
| `test_timeline_time.cpp` | WP0 | rationals and ticks; seconds → ticks for 1/90000 and 1001/30000; overflow refused |
| `test_timeline_model.cpp` | WP0 | canonical order; deterministic ids; `overlapping()` against brute force on random events; points, spans, empty intervals |
| `test_json_read.cpp` | WP1 | RFC 8259's grammar, escapes and UTF-8, numbers as `JsonText` writes them (integer literals for whole doubles, exponents; schema.md §1.6), duplicate keys refused, nesting limit, round trip with the writer |
| `test_ladder_report_read.cpp` | WP1 | a report written by `vmafreport::ladder_to_json` from results built in the test, read back field by field; an older layout refused with the missing key named; the scores digest unchanged when URLs are redacted |
| `test_playlist_events.cpp` | WP2 | tags before `#EXTINF` and between `#EXTINF` and the URI attached to the same segment; every CUE form of scte35.md §2 as literals; DATERANGE attributes (quoted, hex, an illegal form refused); OATCLS kept; DISCONTINUITY counted; vendor tags; testgen's ad playlist (testgen.md §2.3) |
| `test_scte35.cpp` | WP3 | the vectors Q21 allows; CRC against `av_crc(AV_CRC_32_IEEE)` (P-H); `pts_adjustment` wrap; `splice_command_length` 0xFFF; alignment stuffing; a non-CUEI descriptor; sub-segment fields; base64 and hex; malformed input never read past its end; cue classes (Q16), CRC policy (Q17), durations (Q18) |
| `test_timeline_axis.cpp` | WP4 | the axis on hand-made reports: restarted PTS (the D-069 ad shape), a forward jump under 10 s, coarser-grid pairs, the VFR warning; placement of distorted frames through pairs |
| `test_timeline_query.cpp` | WP4 | half-open overlaps, `at_end`, spans, grouping order, exact offsets; the work order's example on a hand-built timeline: a cue-out, a switch between two numbered rungs and a request of 2.3 s, returned in their groups (the numbers are the test's inputs) |
| `test_timeline_detect.cpp` | WP4 | a step drop; single-frame dips at context starts; runs shorter than N; a curve split at unscored spans; a coarser-grid curve; baseline statistics; the same drops twice |
| `test_timeline_media.cpp` | WP5 | the demux pass on HLS streams the test writes: the byte map against the writer's layout, B-frames, restarted timestamps, keyframes, corrupt packets from unmarked continuity counters (testgen.md §3.6, item 2), the check against a report's PTS |
| `test_vmaf_analyzer.cpp`, `test_vmaf_report.cpp` (additions) | WP6 | decode facts recorded on a damaged segment as the sources survey made one (run `corrupt-segment`), none on a clean one; the scores unchanged; the report's new pair key |
| `test_session_plan.cpp` | WP7 | the plan's format and errors; snapping; spans; switch events; the session curve, coarser grid and top-rung spans (Q07) |
| `test_timeline_fetch.cpp` | WP7 | the replay over `tests/HttpFileServer.h`: status 200 and the file's bytes; a segment the server delays (P-N) with a duration at least the delay; a missing segment's error status; the logger uninstalled afterwards |
| `test_timeline_doc.cpp` | WP8 | the writer's keys and types; reader round trip; an unknown `schema_version` refused; byte-identical output for the same timeline twice |
| `test_timeline_build.cpp` | WP9 | the merge on hand-built inputs: each source's events placed by §5.1's rules, unplaceable anchors in `warnings`, a session curve from spans, the same timeline from the same inputs twice |
| `test_timeline_cli.cpp` | WP9 | option tables, config files, errors, `--help`; one end-to-end run of each subcommand |
| `test_synthetic_ladder.cpp` | WP10 | the generator's ladders: segments and frames per the layout, BANDWIDTH in rung order (the ramp's failed it, testgen.md §2.2), the tags written; two generations byte-identical, as testgen found its files (testgen.md §3.3) |
| `test_timeline_view.cpp` | WP12 | ui.md §5.1: view math, min/max envelopes (a one-frame dip survives), hit testing, matching, playhead mapping, the seek decision |
| `test_hls_seek_behaviour.cpp` | WP12 | ui.md §5.2: FFmpeg's seek behaviour the seek decision relies on, on playlists the test writes |

### 9.2 The two acceptance tests (§3.2; D-069)

In `tests/test_attribution_acceptance.cpp`, tagged `[attribution][acceptance]` (P-O), with the content of Q28 made by `tests/SyntheticLadder` once per run (P-I):

1. **A segment encoded at a deliberately low quality.** The degraded ladder (testgen.md §2.3: three rungs, eight 1 s segments, `v270`'s segment 4 at the low quantizer). Score it with `vmafladder::score` in top-rung mode, write the report with `vmafreport::ladder_to_json`, read it back (the real path), build the timeline from the playlists on disk and the demux pass, detect with the test's explicit parameters (the owner's defaults once Q10 to Q12 give them). Pass when: `v270`'s curve has one drop; its attribution's playlist group holds `v270`'s segment with media sequence 4 (`seg_004.ts`), whose span holds the drop's interval; the control rung `v180` has no drop. On testgen's content the low frames were exactly that segment's 25 (testgen `logs/15`).
2. **A score drop at an ad boundary.** The ad ladder (`EXT-X-DISCONTINUITY`, `EXT-X-CUE-OUT:2.000`, `EXT-X-CUE-OUT-CONT`, `EXT-X-CUE-IN`; `v180`'s ad at the low quantizer). Pass when: `v180`'s curve has one drop; its scte35 group holds the `cue_out` of the `EXT-X-CUE-OUT` line at offset 0 and a `break` whose span holds the drop; its playlist group holds the `discontinuity` at offset 0; under Q14 (a), the `cue_in` at the end; the control `v270` has no drop. Run once with the ad's timestamps from the main content's origin and once from 1 s (Q28 (a)), which the frame axis treats alike (runs `axis-ad`, `axis-ad-origin1`).

Each test also runs end to end once through the CLI: `mirage_vmaf --ladder`, then `mirage_vmaf timeline`, the document read back and checked as above.

### 9.3 The example question, end to end

`[attribution][session]`: the ad ladder served by `HttpFileServer` with a delay rule on `v180/ad_000.ts` (P-N); a plan playing `v270` until the ad and `v180` from its first frame. Pass when the session curve's drop is attributed to: the `cue_out` (scte35), the `switch` from `v270` to `v180` with their rungs (player), and the request for `ad_000.ts` with a duration at least the delay (network). It is the work order's example with this ladder's events. The delay is the test's own parameter; it has to exceed the undelayed requests' durations in the same run.

### 9.4 Time

testgen measured 2.022 to 2.313 s to generate a ladder with the generator optimized, 16.941 s without optimization as the debug preset compiles tests, and 0.457 to 0.515 s to score one (testgen.md §3.4). With the generator compiled optimized in every build type (P-I), the acceptance tests' time in a Debug build is MEASUREMENT PENDING.

---

## 10. §3.3: the UI plan, for the owner

ui.md §2 to §5 is the plan; this design changes where the panel gets its data.

- **Input.** The panel loads a timeline document and the ladder report it names, both checked by hash (§8). It links the core (pure) and draws the core's `Timeline`: curves from the report, events and attributions from the document. A click inside a detected drop shows its attribution; a click elsewhere calls `timeline::attribute()` around the clicked frame. The panel computes nothing the CLI does not (ui.md §2.2).
- **The panel** (ui.md §2.1): one curve per variant as per-column min/max envelopes (so one-frame dips stay visible, ui.md F4), the session's curve, the detected drops shaded, event lanes per source, the playhead; wheel zoom, drag pan, double-click fit, a click picks the lowest frame near the cursor. Placement Q30, key Q31.
- **Its math** (`TimelineView`, pure, tested in `mirage_tests`): view and zoom, the envelope pyramid, hit testing, the matching of the document to the open master (by SHA-256, else by URL, ui.md §2.3), the playhead (the frame on screen → its pair's frame → its reference frame → the axis), and the seek decision.
- **Seeking** (ui.md §2.4, measured): a frame whose timestamps continue from the stream's start, and content after an ad whose timestamps restarted, are reached through the existing `s_seek_to`; the frames of such an ad are reached by no seek in FFmpeg 8.1.1. What the panel does there is Q29. The seek decision needs each variant's segments and discontinuities, which the document carries (the playlists' segments are `segment` events), so the panel fetches nothing (ui.md D11).
- **QA** (ui.md §5.3): `--timeline <document>`, `--ui-timeline <spec>`, `--timeline-report <txt> <secs>`, run as the repository's scripted runs are, outside ctest.
- **D-007.** By the agent's reading, nothing recommended touches the render or decode path: the panel is an ImGui window in the overlay frame, its seeks go through the entry point the seek bar already uses, and its height joins the overlay's layout, not the video rectangle (ui.md §3). The changes that would touch it: a seek with its own landing point or "play to frame, then pause" (Q29 (b)); another way to position the hls demuxer (Q29 (c)); scrubbing by dragging the panel (ui.md D10, not proposed); the player's own session log (Q08 (b)). Each needs a written plan the owner approves first.

---

## 11. Differences from the work order, to report

None of these is fixed by editing a document (the work order's constraint). Each is listed again in `docs/PHASE3_DECISIONS_PENDING.md` by WP14.

1. **"NetworkDebugger (av_log interception)"** is `NetworkLogger` in the code (sources.md §10, item 1). Under Q04 (a) it records Phase 3's HTTP timing, but of a replay, not of a playback.
2. **"ABR / player: variant switch events"**: there is no ABR (D-068); switches come from a scripted plan; the player's own log is Q08.
3. **"Decoder: dropped frames"**: the offline decode drops none; pairing failures stand for frames a decoder never output; frames the player drops at a switch are not in Phase 3's scores (D-068's limits) and come only with the player's log.
4. **"Keyed by media PTS"**: PTS alone repeats (run `axis-ad`); the key is (sequence, PTS) and the axis is Q01's; under its option (d), offsets are reference frames times the frame duration, not PTS differences.
5. **"HLS / DASH parsing: … DASH period boundaries"**: `mirage_vmaf` scores HLS ladders only; DASH stays an interface, as D-070 has it for `EventStream`.
6. **"SCTE-35: … splice point"**: several published messages carry no splice time, and the HLS-only tags none, so the splice point is often the tag's position (scte35.md §9, item 3); placement is Q15.
7. **D-070's list** omits `EXT-X-SCTE35`, `EXT-X-SPLICEPOINT-SCTE35` and Adobe's `EXT-X-CUE` (Q19); the one public VOD stream found carries SCTE-35 in segments as ID3, not as stream type 0x86 (scte35.md §9).
8. **The example's switch between two numbered rungs** assumes a rung numbering the report lacks (Q06).
9. **The top rung in top-rung mode** has no score, so a session's spans on it have none (Q07).
10. **"HTTP status"** is not exported by FFmpeg 8.1.1's API (`http.c#L180-L206`); it comes from `av_log` lines through `NetworkLogger`, or as a class from a failed open (Q04).
11. **§7's single output** with `timeline` and `attributions`: under Q22 (B) they are in a separate document; attributions are grouped by source with offsets and carry the detection parameters, beyond §7's flat list (Q25).
12. **A click on a low point taking the player to its frame**: not for frames of an ad whose timestamps restart (Q29).
13. **`docs/VMAF_GAP_REPORT.md:230-231`** says the Decoder has a dropped-frame counter; it is the render loop's `total_dropped` (`src/main.cpp:1751`; sources.md §12). Noted, not edited.

---

## 12. Owner questions

Each question stands alone; its options with their trade-offs, the agent's recommendation and the reasons for it are in full in Appendix C (also as data in `<out>/design-library/owner_questions.json`), and summarized here. "Blocks" names the work packages of §14 that wait for the answer.

| # | Question | Options | Agent's recommendation | Blocks |
|---|---|---|---|---|
| Q01 | The time key and axis | (a) raw PTS; (b) (sequence, PTS), sequences end to end; (c) (sequence, PTS), reference playlist time; (d) (sequence, PTS), reference frame position | (d) (§4) | WP4, WP8, WP11, WP12 |
| Q02 | Frame→segment map and keyframes | (a) demux pass in the timeline command; (b) recorded by the analyzer into the report | (a) | WP5, WP6 |
| Q03 | Decode errors | (a) recorded by the analyzer while scoring; (b) a decode pass; (c) out of Phase 3 | (a), if the owner confirms recording-only is outside D-007's kind | WP6 |
| Q04 | A scripted session's HTTP timing | (a) replay recorded by NetworkLogger; (b) replay with own timing, status class only; (c) in the plan; (d) none until the player's log | (a) | WP7 |
| Q05 | The session plan's format | (a) text lines; (b) versioned JSON shared with the future player log; (c) `--qa-switch` syntax | (b) | WP7 |
| Q06 | Rung naming | (a) entry and URI; (b) plus ascending BANDWIDTH rank; (c) plus descending rank | (b) | WP7, WP8 |
| Q07 | Top-rung spans in top-rung mode | (a) unscored, left out of detection; (b) 100; (c) mezzanine mode required; (d) score the top rung against itself | (a) | WP4, WP7 |
| Q08 | The player's own session log | (a) not in Phase 3; (b) hooks under a D-007 plan; (c) parse `switch-trace` text | (a) | none (a new package under (b)) |
| Q09 | Curves detection runs on | (a) each variant's; (b) the session's; (c) both, labelled | (c) | WP4, WP8, WP9 |
| Q10 | Threshold default | (a) 6 VMAF points (the work order's suggestion); (b) required option; (c) relative | (a) | WP4 |
| Q11 | Baseline statistic and window | (a) trailing median of non-low points; (b) trailing mean; (c) centred median; (d) one value per curve; W unmeasured | (a), W after the measurement of §6.3 | WP4 |
| Q12 | Minimum duration N, short dips | (a) N in the curve's points, context-first pairs left out; (b) N as a duration; (c) (a) with context-first pairs counted | (a), N after the measurement of §6.3 | WP4 |
| Q13 | drop_magnitude | (a) baseline − min; (b) baseline − mean; (c) both | (c) | WP4, WP8 |
| Q14 | Which events, in what order | (a) half-open overlap, end points flagged, variant scope, by source and time, no ranking; (b) with a margin; (c) ranked | (a) | WP4, WP11 |
| Q15 | SCTE-35 placement anchor | (a) position; (b) splice PTS, else position; (c) START-DATE via PDT, else position; (d) record all, declared precedence, warn on disagreement | (d) with position first | WP3 |
| Q16 | Cue-out and cue-in message classes, cancellations | (a) splice_insert by indicator, Break/Advertisement/PO/Ad Block pairs, others as `splice`, cancels kept; (b) splice_insert only; (c) MediaPackage v1's set | (a) | WP3 |
| Q17 | A message whose CRC_32 fails | (a) kept and marked; (b) dropped; (c) kept silently | (a) | WP3 |
| Q18 | Ad duration from several sources | (a) all in the payload, the break ends at its cue-in, else start + a declared order; (b) tag first; (c) message first | (a) | WP3 |
| Q19 | Signalings beyond D-070's list | (a) D-070's only; (b) plus EXT-X-SCTE35 and SPLICEPOINT; (c) all three | (a) | WP2, WP3 |
| Q20 | A bare EXT-X-DISCONTINUITY | (a) a discontinuity only; (b) also an inferred ad boundary | (a) | WP2 |
| Q21 | SCTE-35 vectors that may be committed | (a) Apache-2.0 and self-built only; (b) plus the standard's and the RFCs' examples; (c) plus vendors' | (a) | WP3 |
| Q22 | Where the timeline is written | (A) the ladder report; (B) a separate document; (C) both | (B) | WP8, WP9 |
| Q23 | The hash naming the ladder report | (a) bytes; (b) a canonical scores digest; (c) both | (c) | WP1, WP8 |
| Q24 | Versioning the new document now | (a) `schema_version` and a schema file now; (b) the version only; (c) neither until Phase 5 | (a) | WP8 |
| Q25 | The document's layout and vocabulary | (a) as §8 and §5 propose; (b) §7's flat lists; (c) (a) as provisional until Phase 5 | (a) | WP2, WP3, WP7, WP8 |
| Q26 | Subcommand names and split | (a) `timeline` and `attribute`; (b) one `attribute`; (c) a separate program; (d) a third input form, no subcommand | (a) | WP9 |
| Q27 | The JSON reader | (a) written in the repository; (b) a header-only library | (a) | WP1 |
| Q28 | D-069's test content | (a) testgen's recommendations as a package; (b) the same with MPEG-2; (c) item by item | (a) | WP10, WP11 |
| Q29 | Clicking inside an ad whose timestamps restart | (a) "not reachable", offer the segment before; (b) play to frame (D-007); (c) reposition the demuxer (D-007) | (a) | WP12, WP13 |
| Q30 | The panel's placement | (A) bottom dock; (B) a sidebar tab; (C) a floating window | (A) | WP13 |
| Q31 | The panel's key | (a) L; (b) E; (c) menu and `--ui-overlays` only | the owner's; L if a key is wanted | WP13 |
| Q32 | The panel's settings and the config-file rule | (a) shows the document's drops only, its one setting is the document; (b) re-detects with its own settings | (a) | WP13 |
| Q33 | The differences without an alternative (§11, items 1, 2, 3, 5, 10, 13) | (a) acknowledged and reported; (b) some reopened | (a) | WP14 |

---

## 13. Process choices (P-entries, pending the owner's confirmation)

| | Choice | Alternatives |
|---|---|---|
| P-A | The core is a CMake object library linked into the three targets, with no FFmpeg, libvmaf, GL or ImGui target on its link line; adapters and the CLI in `mirage_vmaf` and `mirage_tests`; `HlsAlign.cpp` (and under Q04 (a) `NetworkLogger.cpp`) added to `mirage_vmaf`, `JsonText.cpp` to the player | every file listed in each target, as today |
| P-B | Exact arithmetic: PTS as int64 ticks of their stream's time base (seconds from the report rounded to ticks), frame durations and offsets as rationals, 128-bit intermediates, conversion to double only when written; the axis warns where a reference frame's PTS is more than half a frame from its position within a sequence, half a frame being the aligner's tolerance (`src/VMAFAlign.h:23-25`) | seconds in doubles with tolerances |
| P-C | Media playlists read again by the timeline command through `vmafladder::read_master`, each one's location and SHA-256 written to the document | tapping the bytes the hls demuxer read (sources.md §11, choice 3 (b)) |
| P-D | Packets to segments by byte position, frames to packets by PTS within a segment, the result checked against the report's PTS for every scored frame; a mismatch leaves that playlist's segment events unplaced, with a warning | PTS against EXTINF; segment heads |
| P-E | The demux pass opens each playlist with the analyzer's options (I/O limits, hls options, hooks) and reads video packets without decoding | a decode pass |
| P-F | Events in a canonical order (start, end, source, type, variant, id); ids built from source, type, variant and a per-type index; identical inputs give byte-identical documents apart from measured times | insertion order |
| P-G | The option-table parser of `src/VMAFCli.cpp` made reusable for the subcommands, today's forms and `--help` unchanged; each subcommand with its own table and D-030's config file | a second parser in `TimelineCli` |
| P-H | CRC-32/MPEG-2 computed by a constexpr table in the core, checked in a test against `av_crc(av_crc_get_table(AV_CRC_32_IEEE), UINT32_MAX, …)`, the call FFmpeg's own PSI parser makes (scte35.md §3.6) | `av_crc` in the decoder, which would make it an adapter |
| P-I | `tests/SyntheticLadder` from testgen's prototype, its source compiled with optimization in every build type, each ladder made once per test run (testgen.md §4, choice 7 (a) and (d)) | the prototype as is; per-test generation |
| P-J | The document records the tool, the ladder report's settings and hashes, the library versions; the commit is the eval tooling's to record (P-006) until Phase 5 | the commit in the document now |
| P-K | The report reader accepts the layout of `75cf6e0` on (with `alignment.*.sequences`, D-057); older committed layouts are refused with the missing key named | readers for the 12 older layouts (schema.md §1.8) |
| P-L | Relative paths in the report are taken from the current directory, as `mirage_vmaf` takes its own inputs | relative to the report's directory |
| P-M | The replay: sequential in the session's order, one connection per request, the analyzer's I/O limits (D-036), no retries, `NetworkLogger` installed only for the replay, rows tagged `NetInitiator::Playback` | a new initiator value, which changes `src/NetworkLogger.h` |
| P-N | `tests/HttpFileServer.h` gains a rule that delays the first byte of a path's responses by a set time | a second test server |
| P-O | Test tags `[timeline]`, `[scte35]`, `[session]`, `[attribution]`, `[acceptance]`; tests that need libx264 skip without it, as tests skip without a model file (`tests/test_vmaf_ladder.cpp:822`) | failing without libx264 |
| P-P | A plan's times snap to the first reference frame at or after them; at a switch inside a segment both variants' segments holding the switch frame are fetched | snapping to segment boundaries |
| P-Q | The JSON reader builds a document tree with objects in their order and duplicate keys refused, as the tests' reader does (`tests/test_vmaf_report.cpp:26`, `:104`); its memory and time on the full advanced report are measured before the panel loads such reports (MEASUREMENT PENDING) | a streaming reader from the start |
| P-R | Hashes are computed at the edges (`filehash`, libavutil's AVSHA); the core returns the canonical text to hash | hashing inside the core |
| P-S | An event in several variants' playlists is kept once per variant; an attribution lists those Q14 scopes | deduplication across variants |
| P-T | `attribute --format text` prints one block per drop, one line per source group, as §6.4 shows | JSON only |
| P-U | A segment request is placed on its segment's media span; a playlist request at the first span that plays its variant | at the request's place in the session's order |
| P-V | A distorted frame that failed to pair is placed at its input's next paired frame; a reference frame that failed, at itself | dropping them from the timeline |
| P-W | Under Q06 (b), a playlist's rank uses the highest BANDWIDTH among the entries that name it, as the top rung is chosen (`src/VMAFLadder.h:33-35`) | its first entry's |
| P-X | The document copies the report's reference mode, model, scaler and library versions, so it can be read for provenance without the report | the report's hashes only |

---

## 14. Work packages

Each package is built in a worktree of its own from `vmaf/phase-3`, with its unit tests; new files are its own, and a shared file belongs to one package only. `CMakeLists.txt` is the exception: WP0 creates the timeline section, and each later package adds one block of its own to it, merged by the lead.

| Package | Builds | Files it owns | Depends on | Blocked by |
|---|---|---|---|---|
| WP0 Core types and build skeleton | `TimelineTime`, `TimelineModel`, the object library, `HlsAlign.cpp` in `mirage_vmaf`, `JsonText.cpp` in the player | new: `src/TimelineTime.*`, `src/TimelineModel.*`, `tests/test_timeline_time.cpp`, `tests/test_timeline_model.cpp`; changed: `CMakeLists.txt` (structure) | none | none |
| WP1 JSON and report readers | `JsonRead`, `LadderReportRead`, the scores digest's canonical text | new: `src/JsonRead.*`, `src/LadderReportRead.*`, `tests/test_json_read.cpp`, `tests/test_ladder_report_read.cpp` | WP0 | Q27, Q23 |
| WP2 Playlist events | `PlaylistEvents` | new: `src/PlaylistEvents.*`, `tests/test_playlist_events.cpp` | WP0 | Q19, Q20, Q25 |
| WP3 SCTE-35 | `Scte35`, `Scte35Cues` | new: `src/Scte35.*`, `src/Scte35Cues.*`, `tests/test_scte35.cpp`, test vectors per Q21 | WP0, WP2 | Q15, Q16, Q17, Q18, Q19, Q21, Q25 |
| WP4 Axis, query, detection | `TimelineAxis`, `TimelineQuery`, `TimelineDetect` | new: `src/TimelineAxis.*`, `src/TimelineQuery.*`, `src/TimelineDetect.*`, `tests/test_timeline_axis.cpp`, `tests/test_timeline_query.cpp`, `tests/test_timeline_detect.cpp` | WP0 | Q01, Q07, Q09, Q10, Q11, Q12, Q13, Q14 |
| WP5 Media facts (demux pass) | `TimelineMedia` | new: `src/TimelineMedia.*`, `tests/test_timeline_media.cpp` | WP0 | Q02 |
| WP6 Decode facts in the analyzer (only under Q03 (a), or Q02 (b)) | recording in `DecodeCtx`, a sparse per-pair list in the report | changed: `src/VMAFAnalyzer.*`, `src/VMAFReport.*`, `tests/test_vmaf_analyzer.cpp`, `tests/test_vmaf_report.cpp` | none | Q03 (and Q02 for (b)) |
| WP7 Sessions | `SessionPlan`, the session curve, switch events, `TimelineFetch` with `NetworkLogger` | new: `src/SessionPlan.*`, `src/TimelineFetch.*`, `tests/test_session_plan.cpp`, `tests/test_timeline_fetch.cpp`; changed: `tests/HttpFileServer.h` | WP0, WP2, WP5 | Q04, Q05, Q06, Q07, Q25 |
| WP8 The document | `TimelineDoc`, the schema file, the format document | new: `src/TimelineDoc.*`, `tests/test_timeline_doc.cpp`, `docs/schema/mirage-timeline-1.schema.json`, `docs/TIMELINE_FORMAT.md` | WP0 (WP1 for the reader) | Q22, Q23, Q24, Q25, Q06, Q09, Q13 |
| WP9 Builder and CLI | `TimelineBuild`, `TimelineCli`, the dispatch, `--help`, config files | new: `src/TimelineBuild.*`, `src/TimelineCli.*`, `tests/test_timeline_build.cpp`, `tests/test_timeline_cli.cpp`; changed: `tools/mirage_vmaf.cpp`, `src/VMAFCli.*` | WP1 to WP5, WP7, WP8 (WP6 if built) | Q26, Q22, Q09 |
| WP10 Synthetic ladders | `tests/SyntheticLadder` from testgen's prototype | new: `tests/SyntheticLadder.*`, `tests/test_synthetic_ladder.cpp` | none | Q28 |
| WP11 Acceptance and example tests | §9.2 and §9.3 | new: `tests/test_attribution_acceptance.cpp` | WP9, WP10 | Q28, Q14, Q01 |
| WP12 UI math | `TimelineView`, the seek decision, the seek-behaviour test | new: `src/TimelineView.*`, `tests/test_timeline_view.cpp`, `tests/test_hls_seek_behaviour.cpp` | WP0, WP8 | Q01, Q29 |
| WP13 The panel | `TimelinePanel`, toggles, QA flags | new: `src/TimelinePanel.*`; changed: `src/main.cpp`, `src/PlayerUI.*`, `src/NetInspectorAlign.cpp` (optional move of `nice_step`, `axis_label`) | WP4, WP8, WP12 | Q29, Q30, Q31, Q32 |
| WP14 Documents | `docs/PHASE3_STATUS.md`, `docs/PHASE3_DECISIONS_PENDING.md` (started at once with §12 and §11), the P-entries in `docs/DECISIONS.md`, the Phase 3 rows of `docs/AI_CONTRIBUTIONS.md` | new and changed: those files | all, for the final state | Q33 (for the differences' wording only) |

Order: WP0 first (small); then WP1, WP2, WP4, WP5, WP8, WP10 and WP12 in parallel; WP3 after WP2; WP7 after WP5; WP9 once its inputs merge; WP11 and WP13 last. WP14's pending document starts with WP0.

---

## 15. Risks

1. **Byte-position mapping beyond MPEG-TS.** Measured on MPEG-TS only (sources.md §2; runs `map-by-pos-*`); fMP4 with EXT-X-MAP, ID3-timestamped audio and byte ranges: MEASUREMENT PENDING. The check against the report's PTS (P-D) makes a failure visible, not silent.
2. **A variable frame rate reference** misplaces events on axis (d); the axis warns (P-B).
3. **The demux pass reads every segment again**; over HTTP that is a second download (time: MEASUREMENT PENDING), and a server could serve other bytes than it served the scorer (the PTS check catches frame-level differences).
4. **`NetworkLogger`'s parse of `av_log` text** (TRACE and DEBUG lines of http.c) depends on FFmpeg 8.1.1's wording; the replay test pins it, and a new FFmpeg version fails a test rather than a field.
5. **The process-global `av_log` callback** (sources.md §9): the replay must not run while anything else in the process logs through FFmpeg; the `timeline` command scores nothing, and the replay installs the logger for its own duration only (P-M).
6. **The replay is not a player.** Its times say what a sequential fetch got, not what playback would have; the document says `origin: "replay"`.
7. **Memory and time of the JSON reader** on full reports (27969051 bytes of text for the advanced sample, schema.md §3.3): MEASUREMENT PENDING (P-Q).
8. **Debug-build test time**: the generator without optimization took 16.941 s per ladder (testgen.md §3.4); P-I's effect is MEASUREMENT PENDING.
9. **libx264 missing** from an FFmpeg build skips the acceptance tests under Q28 (a).
10. **Top-rung mode hides a drop present in the top rung too** (D-002's limit; testgen.md §5): such an ad would score well against itself.
11. **The player's PTS against `mirage_vmaf`'s** for the same frame through the master and alone, 33-bit wrap included: MEASUREMENT PENDING (sources.md §12; ui.md §4.4). The panel's playhead depends on it.
12. **SCTE-35 in segments** (0x86 or ID3) is only an interface; streams that signal only there produce no SCTE-35 events in Phase 3.

## 16. Open issues

- MEASUREMENT PENDING: the demux pass's time on real ladders (local and HTTP); the timeline document's size; the JSON reader's memory and time on the largest committed report; the byte-position mapping on fMP4 and ID3; the detection parameters' effect on D-011's ladders (§6.3); the acceptance tests' time in a Debug build; the spread of the replay's times between runs.
- The fixture's hls-muxer EXTINF problem (sources.md §12) does not affect testgen's generator, which writes its playlists itself (testgen.md §2.3), but any fixture made with FFmpeg's hls muxer for WP5's tests should check its EXTINF against its frames.
- Q01 (d) and Q14 (a) together decide the acceptance tests' assertions on the cue-in; the tests are written once those answers are in.
- The scte35 survey's open issues stand (SCTE 35-1 2023r2 and 35-2 2026 unread; the PDF text from the agent's own extractor; one public VOD stream with SCTE-35, generated on request).

---

## Appendix A. Runs of this design

`<out>/design-library/` holds:

- `tools/pipeline.sh <repo> <out> <scratch> <homebrew>`: every run below, in order. It writes nothing outside `design-library/`, and deletes the probe binary it builds, since a binary carries local paths.
- `tools/run.sh`: runs one command and writes its record `runs/<name>.txt` (working directory, command, start and end in UTC, stdout and stderr, exit status), local paths replaced by `<repo>`, `<out>`, `<scratch>`, `<homebrew>`, `<home>`.
- `tools/map_by_pos.py`: packets to segments by byte position and by PTS, against testgen's `layout.json`.
- `tools/axis_probe.py`: the four axes and the interval query on a ladder report, exact or (`--float`) in doubles.
- `owner_questions.json`: the questions of §12 and Appendix C as data, the source of both.

| Record | Command (from its record) |
|---|---|
| `build-logprobe.txt` | `clang++ -std=c++20 -O1 … -o bin/logprobe <out>/probe/logprobe.cpp $(pkg-config … ffmpeg-full)` |
| `logprobe-ad-v360.txt`, `logprobe-ad-v180.txt` | `bin/logprobe file v360/index.m3u8` (and `v180`) in `<out>/testgen/media/ad-x264-a` |
| `logprobe-ad-origin1-v360.txt` | the same in `<out>/testgen/media/ad-x264-origin1` |
| `logprobe-degraded-v270.txt` | the same for `v270` in `<out>/testgen/media/degraded-x264-a` |
| `map-by-pos-ad-v360.txt`, `-ad-v180`, `-ad-origin1-v360`, `-degraded-v270` | `python3 -I tools/map_by_pos.py logprobe-<name>.txt <playlist> <layout.json>` |
| `axis-ad.txt`, `axis-ad-float.txt` | `python3 -I tools/axis_probe.py [--float] reports/ad-x264.json reports/ad-x264-a.layout.json media/ad-x264-a/v360/index.m3u8 v180/index.m3u8 75:124` in `<out>/testgen` |
| `axis-degraded.txt`, `axis-degraded-float.txt` | `… reports/degraded-x264.json reports/degraded-x264-a.layout.json media/degraded-x264-a/v360/index.m3u8 v270/index.m3u8 100:124` |
| `axis-ad-origin1.txt` | `… reports/ad-x264-origin1.json reports/ad-x264-origin1.layout.json media/ad-x264-origin1/v360/index.m3u8 v180/index.m3u8 75:124` |

The inputs are testgen's (`<out>/testgen/media/` and `reports/`, made by its `run_all.sh`), read and not changed. By its records' times the whole pipeline ran from 04:30:06 to 04:30:09 UTC on 2026-11-01, on the machine shared with other work; nothing in it is a benchmark.

## Appendix B. API sketch

In the repository's style (namespaces of free functions and structs, errors as strings, no exceptions). Names are proposals; the owner may rename.

```cpp
// src/TimelineTime.h — exact time; no FFmpeg.
namespace timeline {
struct Rational { int64_t num = 0; int64_t den = 1; };   // normalized, den > 0
Rational operator+(Rational, Rational);  Rational operator-(Rational, Rational);
Rational operator*(Rational, int64_t);   bool operator<(Rational, Rational);
double   to_double(Rational);             // correctly rounded, for output only
// `seconds` as the report writes it, in whole ticks of `time_base`; nullopt on overflow.
std::optional<int64_t> to_ticks(double seconds, Rational time_base);
}

// src/TimelineModel.h — the timeline's data.
namespace timeline {
enum class Source : std::uint8_t { Vmaf, Network, Playlist, Scte35, Player, Decoder };
inline constexpr std::size_t kSourceCount = 6;
std::string_view source_name(Source);            // "vmaf", "network", ...

struct MediaKey {                                 // the reference frame an event sits at
    std::optional<int64_t> ref_index;
    std::optional<int32_t> sequence;              // the report's reference sequence (D-057)
    std::optional<int64_t> pts;                   // ticks of the reference's time base
};
struct Value;                                     // null, bool, int64, double, string, list, object
using Payload = std::vector<std::pair<std::string, Value>>;
struct Event { /* §3.2 */ };

struct Variant {
    int index = -1, pair = -1;                    // pair -1: the reference
    std::vector<std::size_t> entries;             // master entries naming it
    std::string url, playlist_sha256;
    int64_t bandwidth = 0;  std::optional<std::string> resolution, codecs;
    int rung = 0;                                 // Q06
};
struct Curve {
    std::string id;  int variant = -1;
    std::vector<int64_t> pos, ref_index;  std::vector<double> vmaf;
    std::vector<int64_t> context_first;           // positions of libvmaf context starts
};
struct Span { int64_t start = 0, end = 0; int variant = -1; bool scored = true; };
struct Session { std::string id; std::vector<Span> spans; std::string wallclock_anchor; };

struct Axis {
    std::string kind;                              // Q01
    Rational    tick;                              // seconds per tick
    int64_t     frames = 0;                        // reference frames
    Rational    time_base, frame_duration;
    std::vector<MediaKey> frame_keys;              // per reference frame, where known
    Rational seconds(int64_t pos) const;
};

struct Timeline {
    Axis axis;
    std::vector<Variant> variants;
    std::vector<Curve>   curves;
    std::vector<Event>   events;                   // canonical order (P-F)
    std::vector<Session> sessions;
    std::vector<std::string> warnings;
    // Events meeting [a, b); points at b too, for Found::at_end.
    std::vector<const Event*> overlapping(int64_t a, int64_t b) const;
};
}

// src/LadderReportRead.h — a ladder report, read.
namespace timeline {
struct PairScores {
    std::size_t index = 0;  std::vector<std::size_t> entries;
    std::string ref_url, dis_url;  Rational ref_time_base, dis_time_base, ref_rate, dis_rate;
    std::vector<int64_t> ref_index, dis_index;  std::vector<double> ref_pts, dis_pts, vmaf;
    std::vector<int64_t> context_first;            // pair indices
    std::vector<Failure> failures;                 // alignment.failures
    std::vector<Sequence> ref_sequences, dis_sequences;
};
struct LadderScores { /* settings, ladder entries, reference, pairs */ };
// The report in `text`, or why not (a layout before 75cf6e0, P-K).
LadderScores read_ladder_report(std::string_view text, std::string& err);
// The canonical text of every pair's frames, which redaction doesn't change (Q23).
std::string scores_text(const LadderScores&);
}

// src/PlaylistEvents.h, src/Scte35.h, src/Scte35Cues.h, src/SessionPlan.h
namespace timeline {
struct PlaylistTag { int segment = -1; int line = 0; std::string name, value; };
struct PlaylistEvents { hls::MediaTimeline timeline; std::vector<PlaylistTag> tags; };
PlaylistEvents parse_playlist_events(const std::string& text, const std::string& base_url);
}
namespace scte35 {
struct SpliceInfo { /* scte35.md §3's fields, raw and with pts_adjustment applied */ };
std::optional<std::vector<std::uint8_t>> from_base64(std::string_view);
std::optional<std::vector<std::uint8_t>> from_hex(std::string_view);     // "0xFC..."
SpliceInfo decode(std::span<const std::uint8_t> section);                 // never reads past it
}
namespace timeline {
struct CueOptions { /* Q15 to Q18 */ };
std::vector<Event> cue_events(const PlaylistEvents&, int variant, const CueOptions&);
struct PlanSpan { Rational from_seconds; std::size_t entry = 0; };
struct SessionPlan { std::vector<PlanSpan> spans; };
SessionPlan parse_session_plan(std::string_view text, std::string& err);   // Q05
}

// src/TimelineBuild.h — the merge.
namespace timeline {
struct SegmentFrames { int segment = -1; int64_t first_index = 0, frames = 0, bytes = 0; };
struct VariantMedia {                              // from the demux pass (or the report, Q02 (b))
    std::vector<SegmentFrames> segments;  std::vector<int64_t> keyframes, corrupt;
    std::vector<int64_t> pts;                      // per frame, ticks
};
struct Request { /* NetworkLogger's row fields for one fetch */ };
struct BuildInputs {
    LadderScores scores;
    std::vector<PlaylistEvents> playlists;         // parallel to the variants
    std::vector<VariantMedia>   media;
    std::optional<SessionPlan>  plan;  std::vector<Request> requests;
    std::string axis_kind;  CueOptions cues;
};
Timeline build(const BuildInputs& in);             // warnings in Timeline::warnings
}

// src/TimelineQuery.h, src/TimelineDetect.h — §6.1.
namespace timeline {
struct QueryOptions { bool end_points = true; /* scope, Q14 */ };
struct DetectOptions {
    double  threshold = 0;                         // Q10; no default here
    std::string baseline;                          // Q11
    int64_t window = 0, min_points = 0;            // Q11, Q12
    bool    skip_context_first = true;             // Q12
};
struct Drop { Interval interval; int64_t first = 0, last = 0; double baseline = 0, min = 0, mean = 0; };
std::vector<Drop> detect(const Curve&, const DetectOptions&);
Attribution attribute(const Timeline&, const Curve&, Interval, const QueryOptions&);
}

// src/TimelineDoc.h — §8.
namespace timeline {
struct DocInfo { /* report path and hashes, config file, plan, playlists, detection */ };
std::string to_json(const Timeline&, const std::vector<Attribution>&, const DocInfo&);
bool from_json(std::string_view text, Timeline&, std::vector<Attribution>&, DocInfo&,
               std::string& err);
}

// Adapters (FFmpeg): src/TimelineMedia.h, src/TimelineFetch.h
namespace timeline {
VariantMedia read_media(const std::string& playlist_url, std::stop_token, std::string& err);
std::vector<Request> replay(const std::vector<std::string>& urls, std::stop_token);
}
```

## Appendix C. Owner questions in full

The same text as `<out>/design-library/owner_questions.json`. Each question stands alone; the recommendations are the agent's, and nothing is decided.

### Q01. The timeline's time key and axis

On what single time base does Phase 3 place events and measure intervals and offsets? §3.1 asks for 'keyed by media PTS', 'one time base' and comparisons on the reference variant's timeline, but raw PTS repeat where an ad's timestamps restart.

- **(a) Raw reference PTS.** The report's ref_pts, as §3.1 words it; nothing derived. On testgen's ad ladder 200 reference frames have 150 distinct ref_pts, and the query on raw PTS lists main_000.ts and main_001.ts as overlapping the ad's drop; even without repeats the axis jumps by -11.96 s and +12.04 s (design-library/runs/axis-ad.txt, axis-ad-origin1.txt).
- **(b) (sequence, PTS), sequences laid end to end.** Exact and unique (200 of 200), from the report alone (D-057's sequences). A sequence starts only at a backward step or a forward jump over 10 s (src/VMAFAlign.h:114), so a smaller jump stays a hole: a 3.04 s step inside sequence 1, and the CUE-IN 5.0 s after the drop's start instead of at its end (runs/axis-ad).
- **(c) (sequence, PTS), axis = the reference playlist's time.** Each frame at its segment's start (sum of EXTINF) plus its PTS offset in the segment. Continuous (0.04 s steps), CUE-IN at the drop's end, +2.0 s; the clock of tags, segments, HTTP rows and FFmpeg's hls seeks. Needs the frame-to-segment map and EXTINF that matches the media: an EXTINF shorter than its frames (0.36 s for 10 frames at 25 fps in the sources survey's fixture) breaks the axis's order, and RFC 8216 §4.3.2.1 allows integer EXTINF before version 3.
- **(d) (sequence, PTS), axis = reference frame position.** ref_index x 1/frame_rate. Continuous and exact on all three runs, with the same results as (c); integer positions; ref_index agrees across a run's pairs (schema-survey run 11). Wrong for a variable-frame-rate reference (warned); offsets are frames of reference media, not PTS differences, a difference to report.

**Agent's recommendation:** (d), every event also carrying its reference frame's (sequence, PTS) as its media key and, where its segment is known, its playlist time; exact integer arithmetic (P-B).

**Why:** It is the only axis that is monotonic, gap-free and exact on both D-069 shapes without depending on EXTINF; the PTS stays in every event as the identity §3.1 asks for.

**Blocks:** WP4, WP8, WP11, WP12.

### Q02. Where the frame-to-segment map and keyframes come from

Placing playlist, SCTE-35 and network events needs each scored frame's segment, and §3.1 wants keyframe positions. Who records them?

- **(a) A demux-only pass in the timeline command.** Each scored playlist opened as the analyzer opens it, packets read without decoding, mapped to segments by byte position; keyframes and corrupt flags from packet flags. Scorer and ladder report unchanged; works on existing reports; reads every segment again (time on real ladders: MEASUREMENT PENDING); must enumerate frames as the scorer did, checked against the report's PTS.
- **(b) The analyzer records them while scoring.** A per-pair segment table and keyframe list in the ladder report. No second read, exact by construction; changes the analyzer (recording only) and the report: a new pair key, which tests/test_vmaf_report.cpp:909 counts; older reports lack it.

**Agent's recommendation:** (a).

**Why:** Byte position named the right segment for 200 of 200 packets in each of four rungs probed, where PTS against EXTINF got 145 and 193 of 200 (design-library/runs/map-by-pos-*); the pass leaves §2's scorer, report and committed comparisons as they are. If Q03 is answered (a), (b) costs little more in the same code.

**Blocks:** WP5, WP6.

### Q03. Decode errors in the timeline

§3.1 lists decoder decode errors, and today a damaged segment scores to a complete report with no trace of them (6 frames with decode_error_flags 4, a VMAF minimum of 0; sources.md §7). Where does Phase 3 get them?

- **(a) Recorded by the analyzer while scoring.** decode_error_flags, AV_FRAME_FLAG_CORRUPT and avcodec_send_packet's errors as a sparse per-pair list in the ladder report. No frame, pairing or score changes; but it touches DecodeCtx (src/VMAFAnalyzer.cpp:197-241) and the report, which D-007 and D-068 ('the render and decode paths don't change for it') may cover.
- **(b) A decode pass in the timeline command.** The scorer untouched; every variant decoded again (time: MEASUREMENT PENDING); for a network input it may not see what the scoring decode saw.
- **(c) Not in Phase 3.** Keyframes, corrupt packets and pairing failures only; decode errors reported as a difference from the work order.

**Agent's recommendation:** (a), if the owner confirms that recording-only changes in the analyzer are outside D-007's kind.

**Why:** Only the scoring decode is the one whose scores are attributed; it costs no extra decode and changes no score.

**Blocks:** WP6.

### Q04. The HTTP timing of a scripted session

D-068 gives a session 'the HTTP timing of its segments' but does not say where a scripted session's timing comes from (sources.md §12); the work order's 'segment that took 2.3 seconds' needs one. Which source?

- **(a) A replay fetch recorded by NetworkLogger.** The timeline command fetches the segments the session plays, in its order, one at a time, through the player's NetworkLogger compiled into mirage_vmaf with its manual tracking for plain avio opens (src/NetworkLogger.h:118-125): status, request sent, first byte, end, bytes. It is §3.1's 'NetworkDebugger' itself, and measured; but sequential, without a player's buffer; it relies on FFmpeg 8.1.1's av_log wording; the process-global callback means the replay runs alone.
- **(b) A replay with its own timing, status class only.** No NetworkLogger or av_log; the status only as the class of a failed open's AVERROR_HTTP_* (http.c#L592-L608), no exact code (200 against 206).
- **(c) Stated in the plan.** Deterministic; measures nothing; the example's 2.3 s becomes an input.
- **(d) None for scripted sessions.** HTTP timing only from the player's log, later (Q08).

**Agent's recommendation:** (a).

**Why:** It uses the source §3.1 names and gives every field §3.1 lists, and a test can make one segment slow with a delayed local server and check that the attribution reports it.

**Blocks:** WP7.

### Q05. The session plan's format

A scripted session (D-068) is a list of spans, each with the variant played. In what file format are plans written?

- **(a) Text lines.** '<seconds on the axis> <entry index>' per line with # comments, recorded by path and SHA-256 as config_file is. Simple to write by hand; the player's log would need a second format.
- **(b) Versioned JSON shared with the future player log.** {schema, schema_version, spans: [{from, variant}]}, the player's log later adding observed requests and events to the same document. One reader for plans and logs; needs the JSON reader (Q27), which Phase 3 adds anyway.
- **(c) The player's --qa-switch syntax.** 'v720@4,...' (src/main.cpp:553-556), already known; but its times are wall seconds after the first frame, not media time, and v<height> is ambiguous when two playlists share a height.

**Agent's recommendation:** (b).

**Why:** Plans and the later player log become one format with one reader, and times are media time on the reference's axis.

**Blocks:** WP7.

### Q06. How rungs are named in the output

§3.2's example of a switch between two numbered rungs assumes a rung numbering the ladder report doesn't have (schema.md §3.4). How is a rung named?

- **(a) Entry index and URI only.** Exact as the master gives them; no number to read at a glance.
- **(b) Plus an ascending BANDWIDTH rank.** rung = the 1-based rank of the playlist's highest BANDWIDTH among the ladder's video playlists, 1 the lowest, so that '5 to 3' reads as a switch down (the agent's reading of the example); the ranks change if a rung is added.
- **(c) Plus a descending rank.** 1 = the top rung; '5 to 3' would then read as a switch up.

**Agent's recommendation:** (b).

**Why:** It keeps the exact identity and gives the example's numbers their natural reading; the rank's rule is stated in the document.

**Blocks:** WP7, WP8.

### Q07. Session spans that play the reference in top-rung mode

In top-rung mode the top rung is not scored (src/VMAFLadder.h:36-38). What does a session's curve hold where the plan plays it?

- **(a) Unscored, left out of detection.** No points there; detection runs on each part of the curve between such spans; the document says so.
- **(b) Taken as 100.** Assumed, not measured; a switch down from it would look like a drop of 100 minus the score.
- **(c) Mezzanine mode required.** Sessions that play the top rung refused in top-rung mode.
- **(d) The top rung scored against itself once.** Measured, at the cost of one more pair in §2's run.

**Agent's recommendation:** (a).

**Why:** It claims nothing that wasn't measured and needs no change to §2.

**Blocks:** WP4, WP7.

### Q08. The player's own session log in Phase 3

D-068: sessions come from a scripted plan, and later from a log the player records; recording needs hooks in switch_video, read_loop, video_decode_loop and on_present (sources.md §6). Is the player's log part of Phase 3?

- **(a) Not in Phase 3.** Scripted plans only; the session format leaves room for the log; §3.1's ABR/player and NetworkDebugger sources reach the timeline only through scripted sessions, a difference to report.
- **(b) In Phase 3, under a D-007 plan.** Logging-only hooks with the rung, the take-over point and the frames dropped; a written plan approved before code.
- **(c) From the existing switch-trace text.** No code change; fragile text, no rung, the uptime clock (sources.md §6).

**Agent's recommendation:** (a).

**Why:** Phase 3's acceptance needs no player log, and (b) is a render and decode path question the owner should take with its own plan.

**Blocks:** no package of Phase 3 as planned.

### Q09. Which curves detection runs on

§3.2 detects a drop where the score goes below a sliding baseline of the variant; D-068 defines a session's curve. Which curves does automatic detection run on?

- **(a) Each scored variant's curve.** §3.2's words; no session needed; both D-069 tests are of this kind.
- **(b) A session's curve only.** Where a switch down can itself show as a drop, attributed to the switch.
- **(c) Both, each drop labelled with its curve.** Answers both; more drops to read.

**Agent's recommendation:** (c).

**Why:** (a) is what §3.2 says and what the acceptance tests need; (b) is what the example question, with its switch, needs.

**Blocks:** WP4, WP8, WP9.

### Q10. The detection threshold's default

§3.2: the threshold is configurable, 6 VMAF points suggested. What is the default?

- **(a) 6 VMAF points below the baseline.** The work order's suggestion; configurable. Untested on real ladders (D-011): MEASUREMENT PENDING.
- **(b) No default.** A required option until a measurement picks one.
- **(c) A relative threshold.** A percentage of the baseline; not the work order's form.

**Agent's recommendation:** (a).

**Why:** It is the work order's own suggestion; detection over D-011's ladders would test it.

**Blocks:** WP4.

### Q11. The sliding baseline: statistic and window

The work order doesn't define the baseline. Which statistic, over what window W?

- **(a) Trailing median of the last W points that were not low.** testgen's prototype form (testgen.md §3.1): robust to the drop itself and to short dips; a switch down held to the end stays one drop.
- **(b) Trailing mean of the last W points.** Simple; dragged down by the drop it should measure.
- **(c) Centred median over W points.** Offline only, symmetric; uses points after the frame.
- **(d) One value per curve, its median.** No window; blind to slow changes of level.

**Agent's recommendation:** (a); W the owner's after detection is measured over the D-069 ladders and D-011's ladders across W values (no value is measured or cited; testgen's 50 was its prototype's own).

**Why:** A median of the points that weren't low keeps a drop from lowering its own baseline; no number for W exists yet.

**Blocks:** WP4.

### Q12. The minimum duration N and short dips

§3.2: a drop lasts more than N frames. How is N counted, and are the first pairs of libvmaf contexts, which score low (D-037), counted?

- **(a) N in the curve's own points, context-first pairs left out.** A 30 fps variant's curve has a point at every other reference frame (schema-survey run 11); the first pair of each context is skipped, as D-037's pooled minimum skips it.
- **(b) N as a duration on the axis.** The same N means the same time on every curve.
- **(c) (a) with context-first pairs counted.** Every splice's first frame can then join a drop.

**Agent's recommendation:** (a); N the owner's after the measurement of design §6.3.

**Why:** testgen measured single-frame dips at context starts (logs/46) and runs of 1 to 3 frames in a control (logs/59c) against drops of 25 and 50 frames (logs/15, logs/46), on synthetic content only; D-011's ladders: MEASUREMENT PENDING.

**Blocks:** WP4.

### Q13. What drop_magnitude measures

§7 names an attribution's drop_magnitude without defining it. What does it measure?

- **(a) Baseline minus the interval's minimum.** The depth of the worst frame, the baseline taken at the drop's start.
- **(b) Baseline minus the interval's mean.** The drop's average depth.
- **(c) Both.** (a) as drop_magnitude, (b) as mean_drop.

**Agent's recommendation:** (c).

**Why:** Both are cheap and answer different questions; §7's name keeps the first.

**Blocks:** WP4, WP8.

### Q14. Which events an attribution returns, and in what order

§3.2 asks for all the events that overlap the drop in time, gathered per source, each with its time offset. What counts as overlapping, which variants' events count, and how are they ordered?

- **(a) Half-open overlap, end points flagged, scoped, no ranking.** Spans meeting [start, end), points in it, points exactly at the end flagged at_end (the ad's CUE-IN in runs axis-ad and axis-ad-origin1); events of the curve's variant, the reference, ladder-wide ones and the session's played variants; grouped in §3.1's order, by time within a group.
- **(b) (a) plus a configurable margin.** Events just before or after the drop too; one more parameter to defend.
- **(c) (a) ranked by a relevance score.** An inference the paper would have to defend.

**Agent's recommendation:** (a).

**Why:** It is the work order's 'overlapping' made exact, and still shows the cue-in that ends a drop.

**Blocks:** WP4, WP11.

### Q15. The anchor that places an SCTE-35 event

A cue can be placed by its tag's position, by a DATERANGE's START-DATE through PDT, or by its message's splice PTS, and vendors disagree (scte35.md §4). Which anchor places it?

- **(a) Position only.** Every form has it; segment-accurate; what D-069's tests write.
- **(b) Splice PTS, else position.** The standard's Splice Point, frame-accurate where present; many published messages carry none, and an upstream restamp without a pts_adjustment update misplaces it.
- **(c) START-DATE through PDT, else position.** The HLS rule for DATERANGE; vendor examples put tag and date 13.8 s apart (Wowza, scte35.md §4.2).
- **(d) Every anchor recorded, one declared precedence, a warning on disagreement.** The disagreement becomes visible, a finding in itself; more fields and tests.

**Agent's recommendation:** (d) with position first, then splice PTS, then START-DATE, configurable (scte35.md recommended (d) with splice PTS first).

**Why:** Position is where stitched media changes segment, what MediaTailor, Video Stitcher and broadpeak.io use (scte35.md §4.1) and what the tests write; the other anchors stay in the payload with their offsets.

**Blocks:** WP3.

### Q16. Which SCTE-35 messages are cue-outs and cue-ins, and cancellations

splice_insert says out or in by its out_of_network_indicator; for time_signal the segmentation_type_id set is a choice (scte35.md §4.4). Which set, and what happens to cancelled events?

- **(a) Break, Advertisement, Placement Opportunity and Ad Block pairs.** 0x22-0x23, 0x30-0x37, 0x44-0x47 (SCTE 35 2023r1 Table 23); other decoded messages kept as non-break 'splice' events; cancellations kept as 'cancel' events; configurable.
- **(b) splice_insert only.** Every time_signal a non-break event.
- **(c) MediaPackage v1's default set.** splice_insert plus Provider and Distributor Advertisement and Placement Opportunity (scte35.md §4.4).

**Agent's recommendation:** (a).

**Why:** It covers the break hierarchy the standard defines and keeps every other message on the timeline, so that a Program or Chapter boundary can still be attributed.

**Blocks:** WP3.

### Q17. An SCTE-35 message whose CRC_32 fails

Of 79 published vectors, 9 have a bad CRC_32, 3 of them intended negative tests (scte35.md §5). What does Phase 3 do with such a message?

- **(a) Kept, marked crc_valid false, with a warning.** The event stays attributable; the doubt is visible.
- **(b) Dropped.** A corrupted message isn't trusted; its tag still shows by position.
- **(c) Kept silently.** No signal of the doubt.

**Agent's recommendation:** (a), with an option to drop.

**Why:** Vendors and the RFCs publish such messages, and an event the attribution omits cannot explain a drop.

**Blocks:** WP3.

### Q18. The ad duration when several sources give one

A break's duration can come from CUE-OUT's value, DATERANGE's DURATION, PLANNED-DURATION or END-DATE, break_duration, segmentation_duration, or the cue-in's position. Which counts?

- **(a) Each kept; the break ends at its cue-in.** Else at start plus the first of a declared order of the others; every value in the payload (scte35.md §4.4).
- **(b) The tag's duration first.** Google Video Stitcher's rule.
- **(c) The message's break_duration first.** The standard's value; absent from tag-only cues.

**Agent's recommendation:** (a).

**Why:** The cue-in is where the media changes back; keeping every value shows any disagreement.

**Blocks:** WP3.

### Q19. SCTE-35 signalings beyond D-070's list

Sources also use #EXT-X-SCTE35 (the standard's own HLS tag), #EXT-X-SPLICEPOINT-SCTE35 and Adobe's #EXT-X-CUE (scte35.md §2.6, §9). Does Phase 3 read them?

- **(a) D-070's list only.** The others kept as raw playlist 'tag' events; a difference to report.
- **(b) Add EXT-X-SCTE35 and SPLICEPOINT.** Base64 messages through the same decoder; a little more parsing and tests.
- **(c) Add all three.** EXT-X-CUE carries no message, only attributes.

**Agent's recommendation:** (a) for Phase 3.

**Why:** D-070 decided the list; the others are visible as tags and can be added later at little cost.

**Blocks:** WP2, WP3.

### Q20. A bare EXT-X-DISCONTINUITY as an ad boundary

MediaTailor's stitched playlists drop the cue tags and keep only EXT-X-DISCONTINUITY (testgen.md §5). Is a bare discontinuity an ad boundary?

- **(a) A discontinuity event only.** A fact of the playlist.
- **(b) Also an inferred ad-boundary event.** Helps with stitched streams; an inference the paper would have to defend.

**Agent's recommendation:** (a).

**Why:** The attribution still shows the discontinuity at the drop; calling it an ad is the reader's inference.

**Blocks:** WP2.

### Q21. Which SCTE-35 test vectors may be committed

The vectors' sources carry different licences (scte35.md §5): Apache-2.0, 'All Rights Reserved', none stated, GPL-2.0. Which may Phase 3's tests commit to GPL-3.0-or-later Mirage?

- **(a) Apache-2.0 and self-built only.** Comcast/scte35-go's cases, its copies of the standard's §14 samples included, and messages the tests build themselves.
- **(b) Plus the standard's samples and the RFCs' examples.** SCTE's PDF is 'All Rights Reserved'; the RFC examples are under the IETF Trust's terms.
- **(c) Plus vendor documentation examples.** Pages without a licence, 'All Rights Reserved', or threefive's GPL-2.0.

**Agent's recommendation:** (a); the licence question is the owner's, as D-008's is.

**Why:** It needs no licence judgement beyond Apache-2.0's and covers the layout: scte35-go's cases agree with the standard's samples (scte35.md §3.7).

**Blocks:** WP3.

### Q22. Where the timeline and attributions are written

§7 puts timeline and attributions in its one output; D-068 scores a ladder once for many sessions. Where does Phase 3 write them?

- **(A) In the ladder report.** §7's single output; a new session means scoring again (the full advanced ladder took 1882.86 s, schema.md §3.3) or rewriting the scorer's file; committed comparisons see the change.
- **(B) A separate timeline document per run.** One ladder report and at most one session, the report named by hash; the scorer's output and the committed records untouched; a difference from §7.
- **(C) Both.** Per-variant events in the report, session parts in the document; changes the report and its tests.

**Agent's recommendation:** (B).

**Why:** Scored once, attributed many times (D-068); per-frame lines are 99.91% of a full report (schema.md §3.3), so the document stays small without copying them.

**Blocks:** WP8, WP9.

### Q23. The hash that ties a timeline document to its ladder report

Committed reports are redacted (local paths replaced), so a hash of the bytes mirage_vmaf wrote won't match a committed copy (schema.md §1.8). How does a document name its report?

- **(a) SHA-256 of the report's bytes as read.** Exact; fails against redacted copies.
- **(b) SHA-256 of a canonical text of every pair's frames.** Redaction doesn't touch it; a canonical form to define and test.
- **(c) Both.** Exact identity and a redaction-proof check.

**Agent's recommendation:** (c).

**Why:** The document can then be checked against the file it was built from and against the committed record.

**Blocks:** WP1, WP8.

### Q24. Versioning the timeline document now

§7 (Phase 5) wants a versioned output with a JSON Schema file. Does Phase 3's new document get them now?

- **(a) schema_version and a JSON Schema file now.** §7's rule applied early to the new document; ctest checks keys and types; no validator in ctest without a new dependency (schema.md open issue 3).
- **(b) schema_version only.** The schema file with Phase 5.
- **(c) Neither until Phase 5.** Readers tell layouts apart by keys, as they do for the 12 committed report layouts.

**Agent's recommendation:** (a).

**Why:** A new format costs nothing to version from its first day, and the paper can cite its schema.

**Blocks:** WP8.

### Q25. The timeline document's layout and event vocabulary

Design §8 proposes the document's layout and §5 each source's event types. Does the owner approve them?

- **(a) As proposed.** §7's names where §7 has the field (pts, wallclock, source, type, payload; interval, drop_magnitude, overlapping_events), extended with t, end, seq, ref_index, variant and id; attributions grouped by source with offsets and the detection parameters; per-frame scores not copied; §3.1's six sources, pairing failures under decoder.
- **(b) §7's flat lists exactly.** No spans, groups or offsets: cannot answer §3.2's example.
- **(c) (a) as provisional.** Its names revisited in Phase 5.

**Agent's recommendation:** (a).

**Why:** It keeps §7's names, adds what §3.2's example needs, and writes each event once.

**Blocks:** WP2, WP3, WP7, WP8.

### Q26. The CLI subcommands

§3.2 asks for a CLI subcommand; mirage_vmaf has none today (an argument that isn't an option is an error, src/VMAFCli.cpp:288). What are they called, and how is the work split?

- **(a) mirage_vmaf timeline and mirage_vmaf attribute.** Build, detect and write a document; query a document for given intervals.
- **(b) One mirage_vmaf attribute.** Build and query in one call; the timeline rebuilt, every segment read again, for each query.
- **(c) A separate program, mirage_timeline.** As D-017 separated mirage_vmaf from the player; one more binary to build and test.
- **(d) A third input form in today's parser.** --timeline and the like as options, no subcommand; the parser's forms grow.

**Agent's recommendation:** (a).

**Why:** Building, which reads every segment, is done once; queries are cheap and repeatable; both stay thin over the library.

**Blocks:** WP9.

### Q27. The JSON reader

src/ has no JSON reader, and Phase 3 needs one for ladder reports, documents and plans. Written in the repository, or a dependency?

- **(a) Written in the repository.** Like its writers (src/JsonText.h); no new dependency, as D-030 chose for the config file; code to write and test.
- **(b) A header-only library through FetchContent.** Less code; a new dependency in D-031's list.

**Agent's recommendation:** (a).

**Why:** It follows D-030's 'no new dependency' and the repository's own writers, and its tests can pin exactly the forms the writers produce.

**Blocks:** WP1.

### Q28. The synthetic content of the D-069 tests

testgen.md §4 lists the content's choices: encoder, source, ad content, the ad's timestamps, continuity counters, SCTE-35 binary forms, the generator's speed in a Debug build, the reference mode. How are they settled?

- **(a) testgen's recommendations as one package.** libx264, the tests skipped where it is absent; the fractal noise source; the calm ad; the ad timestamped from the main content's origin for the acceptance test and from 1 s as a second case; continuity counters marked at the splice; no SCTE-35 binary forms in the ladders (decoder vectors per Q21 instead); top-rung mode; the generator compiled optimized, each ladder made once per run.
- **(b) The same with FFmpeg's MPEG-2 encoder.** In every FFmpeg build, no skip; no HLS CODECS value; a smaller drop (62.022 against 96.923 to 97.671, testgen.md §3.1).
- **(c) Item by item.** Each of testgen.md §4's choices answered on its own.

**Agent's recommendation:** (a).

**Why:** Each item of it was measured to give a clear, deterministic drop confined to the intended frames (testgen.md §3).

**Blocks:** WP10, WP11.

### Q29. Clicking a low point inside an ad whose timestamps restart

No seek reaches the frames of such an ad with FFmpeg 8.1.1's hls demuxer (ui.md §2.4, measured). What does the panel do on such a click?

- **(a) It says 'not reachable by a seek'.** And offers the segment before the discontinuity, from which playing reaches the frame; the difference from §3.3 reported.
- **(b) Play to the frame, then pause.** A new kind of request in the render loop: a D-007 plan first.
- **(c) Another way to position the hls demuxer.** A Demuxer change: a D-007 plan first.

**Agent's recommendation:** (a) for Phase 3.

**Why:** It touches no render or decode path; (b) or (c) can follow with their own plan.

**Blocks:** WP12, WP13.

### Q30. The panel's placement

Where does the timeline panel go (ui.md D1)?

- **(A) A bottom dock above the control stack.** Full width beside the seek bar, lowest in the bottom stack so that it doesn't move when the filmstrip shows; it covers the lower video.
- **(B) A tab of the network sidebar.** Docking, resizing and focus handling for free; narrow; tied to N.
- **(C) A floating window.** Simplest; can cover the seek bar; its position isn't kept.

**Agent's recommendation:** (A).

**Why:** A timeline belongs beside the seek bar it shares a clock with (ui.md §2.6).

**Blocks:** WP13.

### Q31. The panel's toggle key

The letters bound to nothing are B, E, J, L, P, R, S, U, X, Y and Z (ui.md §1.5). Which, if any, toggles the panel?

- **(a) L.** Reads as 'timeline'.
- **(b) E.** Reads as 'events'.
- **(c) No key.** The View menu and --ui-overlays only.

**Agent's recommendation:** The owner's choice; L if a key is wanted.

**Why:** A key binding is the owner's taste; L is the mnemonic one.

**Blocks:** WP13.

### Q32. The panel's settings and the config-file rule

Every configurable item must be both a CLI option and a config-file setting. What does the panel let the user configure?

- **(a) Nothing beyond the document to load.** It shows the document's detected drops and calls the query API only for clicks outside them; its one setting is --timeline and a line in the player's prefs.txt.
- **(b) Its own detection settings.** Re-detection with another threshold, window and N in the panel; these need player CLI flags and a config file, and its results can differ from the document's.

**Agent's recommendation:** (a).

**Why:** The panel then never disagrees with the document, and the config-file rule is met by its one setting.

**Blocks:** WP13.

### Q33. Differences from the work order without an alternative in Phase 3

Design §11 lists differences no option of Phase 3 removes: 'NetworkDebugger' is NetworkLogger; there is no ABR; the offline decode drops no frame; DASH is not scored; the HTTP status comes only through av_log; the gap report's claim about the Decoder's dropped-frame counter. How are they handled?

- **(a) Acknowledged as stated.** Reported in docs/PHASE3_DECISIONS_PENDING.md and Phase 3's PR; no document edited to match.
- **(b) Some reopened.** The owner names which, and the agent writes options for each.

**Agent's recommendation:** (a).

**Why:** Each follows from facts of the code or from decided items (D-068, D-070); reporting them is the work order's rule.

**Blocks:** WP14.
