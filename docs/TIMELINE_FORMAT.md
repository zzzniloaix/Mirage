# The timeline document, schema version 1

`mirage_vmaf timeline` writes one timeline document per build: the events of one ladder report's timeline, placed on the reference's frames, and the attributions of the drops found in its pairs' curves, with everything needed to make them again. It is a file of its own, beside the ladder report, which it names by two hashes and whose scores it doesn't copy (`docs/DECISIONS.md` D-072). This file describes its layout key by key, its event vocabulary, and where it stands against §7 of the VMAF work order. `docs/schema/mirage-timeline-1.schema.json` is its JSON Schema; `src/TimelineDoc.h` writes and reads it; `tests/test_timeline_doc.cpp` checks what the writer writes against the schema file with a validator of the schema keywords that file uses, and reads it back.

What is described here is the ladder part, which every document has. The session part (a scripted session's plan, its spans, its switches and the requests of its fetch, D-068) comes with sessions, in round 2 of Phase 3 (`eval/phase3-survey/design.md` §8 and §14, WP09b), which fills `session_plan` and `sessions` and gives the network and player events their fields. Both keys are in their places in every document already, so that schema version 1 has one layout before and after: a document of a ladder alone has `session_plan` null and `sessions` empty, no event with a session, and no event of the network or player source, whose events are a session's. A reader of this version takes no other value of the two keys until then.

Some of the forms below are choices the design leaves open, which wait for the owner's answer; each is marked where it is described, and [Pending the owner](#pending-the-owner) lists them.

## Conventions

- **Version.** `"schema": "mirage-timeline"` names the format and `"schema_version": 1`, an integer, its layout (D-072, Q05 (a)). A reader refuses another schema or version. `tool_version` and `git_commit` aren't written before Phase 5; until then the eval tooling records the commit when it runs, as P-006 has it.
- **Positions.** Every event sits at a reference frame, `ref_index`, the position of a frame in the reference input, or spans reference frames, `[ref_index, ref_end)`, half-open (D-073). Intervals are compared on these integers.
- **Seconds.** `t` and `t_end`, an attribution's `interval.t` and `interval.t_end`, and the offsets are seconds of the axis (below) at those frames, computed from exact ticks; they are for reading, and none is compared. A frame past the last, as a span's end can be, is at the last frame's seconds plus one frame duration for each frame beyond it.
- **Media keys.** `seq` and `pts_ticks` are the media key of an event's frame: the reference's discontinuity sequence in the ladder report (D-057) and its PTS in ticks of `axis.reference.time_base`. `pts` is the same PTS in seconds, as the analyzer computes a timestamp's seconds, so that a scored frame's `pts` equals the report's `ref_pts`.
- **Numbers.** Integers are integer literals. Every other number is a double written by `json::number` (`src/JsonText.h`), the shortest text that reads back as the same double; a whole double is written as an integer literal (`3`, not `3.0`), so the schema types each such field `number`. A value that doesn't exist is null. Read back, a number has the form of its text: an integer literal is an integer, so a whole double comes back as one. Where the layout types a field, that changes nothing; a member of a `payload` or of `options` can be either form for one value, a segment's `extinf` or a cue_out's `duration` of 2 s among them, which `timeline::number_of` (`src/TimelineDoc.h`) reads as a double whichever it is.
- **Rationals** are strings `"num/den"`, exactly: time bases, frame rates, the frame duration, the margins, since a margin of 1/3 s has no exact decimal (pending the owner).
- **Order.** Keys come in the order below. The events come in canonical order (by first frame, end, source in the order of the sources below, type, variant and id; the design's P-I), and each is written once: an attribution names it by its `id`. Identical inputs give byte-identical documents, whatever the order of the events given, as long as no two events share an id (`src/TimelineDoc.h` lists what the writer's inputs must be).
- **Writing.** The document is written to a temporary file next to its path and renamed over it once complete (the design's P-W), so a failed build leaves no partial document.

## The top level

| Key | What it holds |
|---|---|
| `schema`, `schema_version` | `"mirage-timeline"` and `1` |
| `tool`, `command` | the program and its subcommand: `"mirage_vmaf"`, `"timeline"` |
| `ffmpeg_libraries` | each FFmpeg library's version at run time, by name, as the ladder report lists its own (P-035) |
| `options` | every option of the build, by name, with the value it ran with |
| `config_file` | `{path, sha256}` of the config file the options were read from (D-030), or null |
| `ladder_report` | the report the timeline was built from (below) |
| `session_plan` | null: a ladder alone has no session plan (the session part, round 2) |
| `axis` | the reference's frames and seconds (below) |
| `variants` | each scored playlist (below) |
| `sessions` | `[]`: a ladder alone has no session (the session part, round 2) |
| `timeline` | the events (below) |
| `detection` | what detection ran with (below), or null when it didn't run |
| `overlap` | which events an attribution lists (below) |
| `attributions` | the detected drops and the events that meet them (below) |
| `warnings` | what the build couldn't place, each with its reason: an event is never left out silently (design §5.1) |

## ladder_report

`path` is the report's path as given. `sha256` is the SHA-256 of its bytes; `scores_sha256` the SHA-256 of the canonical text of what the timeline reads from it (`timeline::scores_text`, `src/LadderReportRead.h`), which leaves out every field that holds a path, so that a committed copy, whose paths the eval scripts replace with placeholders, still matches (Q06 (c)). Then the report's provenance, as the report gives it (Q04 (a)): `status`, `reference_mode`, `model`, `model_source`, `model_sha256` (null for a built-in model), `scaler`, `libvmaf_version`, `libvmaf_package_version`, and `master`, the master playlist's `url`, `location` and `sha256`. A reader checks the report it is given against `sha256`, or against `scores_sha256` for a redacted copy, before it takes the curves from it.

## axis

The reference's frames, on which events are placed, and the seconds in which offsets are printed (D-073; `src/TimelineAxis.h`).

- `kind`: `"reference-frames"`.
- `seconds`: the clock of every `t` and offset. `"playlist-time"` where the reference has a segment map that fits its playlist: a frame is at the EXTINF of the segments its playlist lists before its segment, plus its PTS's distance from that segment's first frame. `"frame-grid"` where it has none (a reference that isn't a playlist, or a map refused under D-081 (c)): a frame is at its index times the frame duration.
- `frame_duration`: the reference's, as a rational, `"0/1"` when its frame rate is unknown; `frames`: the number of reference frames.
- `reference`: its `url`, its `time_base` and `frame_rate` as the report gives them, and its discontinuity `sequences` (D-057), each `{first_index, first_pts, first_pts_ticks, frames}`, from the pair that read the most of the reference.
- `checked`: `"segment-map"` when both clocks were computed and compared at every scored frame, `"report-only"` when only the report's sequences were (design §4.2). The playlist time is computed only with a segment map, so `"playlist-time"` goes with `"segment-map"` and `"frame-grid"` with `"report-only"`, and a reader refuses the other two pairs.
- `tick_den`, `ticks`: each reference frame's seconds exactly, in ticks of 1/`tick_den` s, a time base of which the reference's time base, its frame duration, half of it and every EXTINF read are multiples. They are written as stretches of frames over which the ticks step evenly: `{first, frames, first_tick, step}` holds frames `first` to `first + frames - 1`, the k-th at `first_tick + k × step`. The stretches cover every frame, one after the other, and an axis of no frames has none. A reference whose playlist time follows an even frame grid takes one stretch.
- `keys`: each reference frame's media key, as stretches of frames of one `sequence` whose PTS step evenly from `first_pts_ticks` by `step`, or are all unknown (`first_pts_ticks` null, `step` 0), covering every frame as the ticks' do.
- `runs`: every run of consecutive scored frames that the check found more than half a frame off, each `{test, sequence, first, last, frames, largest_at, largest, warning}`. `test` is `"clocks"` (the frame grid's seconds against the playlist time), `"playlist-grid"` (each PTS against the frame grid of its playlist discontinuity sequence, `sequence` its number) or `"report-grid"` (the same against the report's sequences, without a map); `largest` is the largest difference in seconds, signed, at frame `largest_at`. A run with `warning` false is a fact: for a playlist whose map was refused, the report's sequences may hide a jump the playlist signals, so the run says nothing about the seconds. Design §4.2 writes each run to `warnings` and gives the facts no place; `runs`, which holds both, is an addition to design §8's axis, pending the owner, and so are its tests' names.
- `warnings`: what is wrong with the axis's inputs, then the text of each run that is a warning.

The stretches let a reader restore the axis exactly without the media. They are this version's encoding of the per-frame values, pending the owner, and they don't keep the document growing with the reference's segments wherever a frame isn't a whole number of ticks of the reference's time base. Evenly spaced frames take one stretch, as at 25 fps on MPEG-TS's 90 kHz clock, where a frame is 3600 ticks; at 24000/1001 fps a frame is 3753.75 ticks of that clock and at 60000/1001 fps 1501.5, so the PTS a muxer rounds to whole ticks step unevenly, and a new stretch of ticks and of keys starts every 4 frames, or every 2 (`tests/test_timeline_doc.cpp`): the stretches grow with the frames there. No measurement of a document's size at those rates is recorded. A reader refuses an axis of more than 2^24 frames, whose stretches it expands into a tick and a key per frame: at that limit they take 2^24 × (8 + 24) bytes, 512 MiB, on a 64-bit build, whatever the length of the document's text, since one stretch of each can claim every frame.

## variants

One entry per scored playlist, the reference's and each pair's distorted one, in the order of their ids (design §4.5):

- `id`: its index, which every event's `variant` names; `role`: `"reference"` or `"distorted"`; `pair`: for a distorted playlist, the index of its pair's curve, the pair's position among the ladder report's complete `pairs`; null for the reference. One variant at most is the reference, and no two name one pair. The position isn't the report's own `pairs[].index`, which skips the number of a pair that failed, so the two differ once a pair fails; which of them a document gives is pending the owner.
- `entries`: the master's entries naming it; `url`: as the report gives it. A rung is named by its entry and URI (D-075).
- `bandwidth`: the highest BANDWIDTH of its entries (D-081 (e)); `resolution`, `codecs`: as the master gives them, null where absent; `rank_from_top` and `rank_from_bottom`: its ranks by BANDWIDTH, 1 the highest and 1 the lowest, both printed as labels (D-075). A variant without entries, a mezzanine, has null for each (pending the owner).
- `playlist`: the media playlist as the build read it again (the design's P-D), `{url, location, sha256}`, or null for an input that isn't a playlist (pending the owner).
- `segment_map`: `"ok"`, or `"refused: "` followed by the reason the demux pass gave; a refused variant has no segment-bound event (D-081 (c), D-086). No other value is taken, so a reference that isn't a playlist, a mezzanine (design §5.2), says `"ok"` for a map it doesn't have (pending the owner).

## timeline: the events

Each event has:

| Field | What it holds |
|---|---|
| `id` | its name in the document, `<source>/<type>/<variant>/<n>`, `-` for no variant, `n` counting that source's, type's and variant's events in canonical order |
| `source`, `type` | the source and the type, from the vocabulary below |
| `variant` | the variant whose input or playlist it comes from; null for none |
| `session` | the session it belongs to, an index into `sessions`; null for every event of a ladder alone. A network or player event has one: their events are a session's (D-068) |
| `segment` | the media sequence number of its segment, for an event bound to one; null otherwise |
| `ref_index`, `ref_end` | its reference frame; a span's exclusive end, null for a point |
| `t`, `t_end` | the seconds of `ref_index` and `ref_end` on the axis |
| `seq`, `pts`, `pts_ticks` | the media key of its frame: at one of the axis's frames, that frame's key in `axis.keys` |
| `wallclock` | seconds from its session's start on one steady clock, for an observed event, of the network or player source; null for every offline event, so for every event of a ladder alone (D-073) |
| `payload` | what its source says of it, an object whose members keep their order |

The vocabulary (design §5.9), the sources in the order attributions group them:

| Source | Type | Point or span | What it is |
|---|---|---|---|
| `vmaf` | `context_start` | point | the first pair of a libvmaf context other than a pair's first context; it scores low (D-037) |
| | `unpaired_frame` | point | a frame the aligner left unpaired (the report's `alignment.failures`): a distorted frame at the next paired frame of its input (D-081 (d)), a reference frame at itself |
| `network` | `segment_request` | span | a session fetch's request of a segment, over its segment's frames (session part) |
| | `playlist_request` | point | a session fetch's request of a playlist (session part) |
| `playlist` | `segment` | span | a media segment, from its first frame to the next segment's |
| | `discontinuity` | point | an `EXT-X-DISCONTINUITY`, never taken for an ad boundary (D-076) |
| | `program_date_time` | point | an `EXT-X-PROGRAM-DATE-TIME` |
| | `daterange` | point | an `EXT-X-DATERANGE` without an SCTE-35 attribute |
| | `tag` | point | any other tag, vendor tags and the signalings outside D-070's list included, raw (D-076) |
| | `period` | | reserved for a DASH period boundary; no reader makes one in Phase 3 (D-076) |
| `scte35` | `cue_out`, `cue_out_cont`, `cue_in` | point | the plain cue tags `EXT-X-CUE-OUT`, `EXT-X-CUE-OUT-CONT` and `EXT-X-CUE-IN` (D-070) and, from round 2, decoded messages that Q15 classes as outs or ins, which make `cue_out` and `cue_in` events (design §5.5) |
| | `ad_break` | span | a cue_out to the next cue_in of its variant, or to the end of the duration it declares; breaks don't nest (D-076, D-087) |
| | `splice_point`, `message`, `cancel` | point | from decoded SCTE-35 messages (round 2) |
| `player` | `variant_span` | span | the variant a session played over a span (session part) |
| | `variant_switch` | point | a session's switch from one variant to another (session part) |
| `decoder` | `keyframe` | point | the frame of a key packet in the demux pass (D-074) |
| | `corrupt_packet` | point | the frame of a packet flagged corrupt, with the next segment boundary in its payload (D-081 (h)) |
| | `decode_error` | point | only as Q09 (round 3) answers: under (a), the scoring decode's errors, which the analyzer would record in the ladder report; under (b), the errors of a decode pass of the timeline build; none under (c) (design §5.8) |

A tag that several rungs' playlists carry is one event per variant (D-081 (f)). The payloads of the playlist and plain cue events are those `src/PlaylistEvents.h` lists for each type (every one starts with the tag's `line`), and an ad break's are its cue lines, how it ended and every duration declared; the other types' payloads are their makers', described with them. A reader reads a payload whatever its members.

## detection

Null when no detection ran: `mirage_vmaf timeline` detects only when the window W and the count N are both given, and otherwise writes no attribution (D-077). When it ran: `threshold` (T, in VMAF points), `window` (W) and `min_points` (N), both counting points of the curve; `baseline`, `"median-non-low"`, the median of the last W points that weren't low; `warm_up`, W/2 rounded down and at least 1, the points at a curve's start that can't be low; `context_first`, `"no-special-case"`, how a libvmaf context's first pair is taken; `curves`, the curves detection ran on, each `{kind, index}` with `kind` `"pair"`; and `session_rules`, null for a ladder alone (the rules that clip a pair's drops to a session's spans, design §6.3, come with the session part). `curves` as that list, the `{kind, index}` form where design §7's `--curve` option writes `pair:<k>`, and `session_rules` null are pending the owner.

## overlap

`rule`, `"half-open"`: a point at frame f meets an interval `[a, b)` when a ≤ f < b, a span `[s, e)` when s < b and e > a (D-078). `margin_before` and `margin_after`: seconds by which every interval was widened on the axis before events were looked for, `"0/1"` for none, the default. `scope`, `"curve-inputs"`: a pair's curve lists the events of its distorted variant and of the reference, and no other variant's (D-078); the name is pending the owner.

## attributions

One per drop that detection found on a curve it ran on, and none when `detection` is null (D-077): all that `mirage_vmaf timeline` writes has `detected` true. An attribution with `detected` false answers an interval a user gave rather than a drop (design §6.1); the timeline subcommand never writes one, and a reader takes one whether detection ran or not, so that `mirage_vmaf attribute`'s answers can take this layout (WP09a).

- `curve`: `{kind, index}`, the pair's curve, as `variants[].pair` names it; `detected`: true for a drop detection found, false for a user's interval.
- `interval`: the drop's reference frames `ref_index` to `ref_end`, half-open: from its first low point's frame to the frame of the curve's next point, or to one past its last point where the curve ends (D-081 (g)); `t` and `t_end`, their seconds; `points`, the curve's points with a score in it.
- `baseline`: the trailing baseline at the drop's first point; `min` and `mean`: of its points' scores; `drop_magnitude`, the baseline minus the minimum, and `mean_drop`, the baseline minus the mean (Q27 (c)).
- `overlapping_events`: six groups, one per source in the order of the vocabulary above, each `{source, events}`, empty groups included (pending the owner). Each event is `{event, offset, offset_end, offset_frames, offset_end_frames}`: its `id`, and its start's and a span's end's distance from the interval's start in seconds on the axis and in reference frames. Within a group events come by offset, with no ranking (D-078).

## Reading a document back

`timeline::from_json` reads what the writer writes and refuses anything else, naming the key: another schema or version, a missing or unknown key, a value of another kind, an event type outside its source's vocabulary, two events with one id, two variants of one pair or two references, an event, a variant, a session or a curve that isn't there, and seconds or offsets that disagree with the axis. It refuses as well what the sections above rule out: a session part other than a ladder's; ticks or keys whose stretches don't cover the axis's frames; an axis whose `seconds` and `checked` aren't paired as above; an event whose `seq` is negative, or whose media key isn't its frame's in `axis.keys`; a network or player event without a session; a wall clock on an event of another source; and an attribution with `detected` true when `detection` is null. It restores the axis exactly from its stretches and widens each attribution's interval by the overlap's margins again. It leaves the curves to the caller, who reads them from the ladder report the document names once its hashes match: curve k is the report's k-th complete pair, on the variant whose `pair` is k.

Every key is required and no other is taken, and every value in seconds is computed again and compared, so any later addition to the layout needs another `schema_version`; the ladder report's reader, by contrast, passes over keys it doesn't know in later layouts. That strictness is pending the owner.

The writer checks nothing: a document whose events lack ids, or whose payloads hold one name twice, is written as it is given, and the reader then refuses it. `src/TimelineDoc.h` lists what the writer's inputs must be.

## Pending the owner

Forms of this version that change what a document says, that the design and D-072 leave open, and that wait for the owner's answer; until then a document is as described above:

- the axis's per-frame values as stretches, which grow with the frames where a frame isn't a whole number of ticks, against plain per-frame values or another encoding; and `axis.runs`, an addition to design §8's axis;
- a curve as `{kind, index}` rather than `pair:<k>`; rationals as `"num/den"` strings; the names `"curve-inputs"`, `"clocks"`, `"playlist-grid"` and `"report-grid"`, and the stretches' members; `detection.curves` as the curves detection ran on, with `session_rules` null; and `overlapping_events` with all six groups, empty ones included;
- a variant's `pair` and a curve's `index` as the pair's position among the report's complete pairs, not the report's `pairs[].index`;
- null for the BANDWIDTH, the ranks and the playlist a variant doesn't have, and `segment_map` `"ok"` for a reference without a map;
- a reader that requires every key, takes no other and recomputes every value in seconds within version 1.

## Against the work order's §7

§7 lists the fields of `mirage_vmaf`'s one JSON output, the timeline and the attributions among them. Where each is here:

| §7's field | In the timeline document |
|---|---|
| `schema_version` | `schema_version`, the integer 1, with `schema` naming the format and the schema file under `docs/schema/` |
| `tool_version`, `git_commit` | not in version 1; they come with Phase 5, and the eval tooling records the commit meanwhile (P-006) |
| `libvmaf_version`, `reference_mode`, `scaler` | copied into `ladder_report` |
| `model_id` | `ladder_report.model`, with `model_source` |
| `model_path_hash` | `ladder_report.model_sha256`: the SHA-256 of the model file's contents, null for a built-in model |
| `reference_identity` | `axis.reference.url`, the reference variant's `url` and `playlist.sha256`, and `ladder_report.master` |
| `environment.ffmpeg_version` | `ffmpeg_libraries`, the run-time versions of the libraries (P-035) |
| `ladder[]` | in the ladder report, which the document names by its hashes; `variants` lists each scored playlist with its entries, `bandwidth`, `resolution` and `codecs`; the per-frame scores stay in the report (D-072) |
| `timeline[]`, with its five fields (`source`, `type`, `payload`, `pts` and `wallclock`) | `timeline[]`, the same five fields extended: an `id`, the `variant`, `session` and `segment`, the position on the reference's frames with its seconds, and the media key `seq` and `pts_ticks` beside `pts` |
| `attributions[]`: `interval`, `drop_magnitude`, `overlapping_events` | `attributions[]`, with the same three: `interval` an object of reference frames and seconds; `drop_magnitude` beside `baseline`, `min`, `mean` and `mean_drop`; `overlapping_events` an array of groups by source, each event named by its id with its offsets; the detection parameters in `detection` |

These are differences from the work order, reported and not settled by this file (`docs/PHASE3_DECISIONS_PENDING.md`, "Differences from the work order, to report"): the document is separate from the ladder report, and its attributions group events, give offsets and carry their parameters (item 13); events sit on reference frames keyed by sequence and PTS, and offsets are seconds of the axis rather than PTS differences (item 4); only observed events have a wall clock, so a ladder's have none (item 5); a ladder alone has no network events (item 6); the aligner's unpaired frames are `vmaf` events, not the decoder's (item 3); rungs are named by entry and URI with two ranks (item 10).
