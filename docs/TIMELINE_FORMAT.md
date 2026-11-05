# The timeline document, schema version 1

`mirage_vmaf timeline` writes one timeline document per build: the events of one ladder report's timeline, placed on the reference's frames, and the attributions of the drops found in its pairs' curves, with everything needed to make them again. It is a file of its own, beside the ladder report, which it names by two hashes and whose scores it doesn't copy (`docs/DECISIONS.md` D-072). This file describes its layout key by key, its event vocabulary, and where it stands against §7 of the VMAF work order. `docs/schema/mirage-timeline-1.schema.json` is its JSON Schema; `src/TimelineDoc.h` writes and reads it; `tests/test_timeline_doc.cpp` checks what the writer writes against the schema file with a validator of the schema keywords that file uses, and reads it back.

What is described here is the ladder part, which every document has. The session part (a scripted session's plan, its spans, its switches and the requests of its fetch, D-068) comes with sessions, in round 2 of Phase 3 (`eval/phase3-survey/design.md` §8 and §14, WP09b), which fills `session_plan` and `sessions` and gives the network and player events their fields. Both keys are in their places in every document already, so that schema version 1 has one layout before and after: a document of a ladder alone has `session_plan` null and `sessions` empty, no event with a session, and no event of the network or player source, whose events are a session's. A reader of this version takes no other value of the two keys until then.

Some of the forms below are choices the design left open, which the owner settled on 2026-11-05 (D-092), and choices made in implementing them, which the owner settled the same day (D-099); each is marked where it is described, and [Settled by the owner](#settled-by-the-owner) lists them, with what still waits for the owner: two choices made since, and two cases where the axis's stretches still grow with the frames.

## Conventions

- **Version.** `"schema": "mirage-timeline"` names the format and `"schema_version": 1`, an integer, its layout (D-072, Q05 (a)). A reader refuses another schema or version. `tool_version` and `git_commit` aren't written before Phase 5; until then the eval tooling records the commit when it runs, as P-006 has it.
- **Positions.** Every event sits at a reference frame, `ref_index`, the position of a frame in the reference input, or spans reference frames, `[ref_index, ref_end)`, half-open (D-073). Intervals are compared on these integers.
- **Seconds.** `t` and `t_end`, an attribution's `interval.t` and `interval.t_end`, and the offsets are seconds of the axis (below) at those frames, computed from exact ticks; they are for reading, and none is compared. A frame past the last, as a span's end can be, is at the last frame's seconds plus one frame duration for each frame beyond it.
- **Media keys.** `seq` and `pts_ticks` are the media key of an event's frame: the reference's discontinuity sequence in the ladder report (D-057) and its PTS in ticks of `axis.reference.time_base`. `pts` is the same PTS in seconds, as the analyzer computes a timestamp's seconds, so that a scored frame's `pts` equals the report's `ref_pts`.
- **Numbers.** Integers are integer literals. Every other number is a double written by `json::number` (`src/JsonText.h`), the shortest text that reads back as the same double; a whole double is written as an integer literal (`3`, not `3.0`), so the schema types each such field `number`. A value that doesn't exist is null. Read back, a number has the form of its text: an integer literal is an integer, so a whole double comes back as one. Where the layout types a field, that changes nothing; a member of a `payload` or of `options` can be either form for one value, a segment's `extinf` or a cue_out's `duration` of 2 s among them, which `timeline::number_of` (`src/TimelineDoc.h`) reads as a double whichever it is.
- **Rationals** are strings `"num/den"`, exactly: time bases, frame rates, the frame duration, the margins and the steps of the axis's stretches, since a margin of 1/3 s has no exact decimal (D-092 (a), (d)). Each term is an integer in its shortest form, without a leading zero and without `-0`, as the writer writes it, so that a rational has one text and a document read back is written again to the same bytes; a reader refuses any other text.
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
- `tick_den`, `ticks`: each reference frame's seconds exactly, in ticks of 1/`tick_den` s, a time base of which the reference's time base, its frame duration, half of it and every EXTINF read are multiples. They are written as stretches of frames (D-092 (a), D-099 (a)): `{first, frames, first_tick, step, phase, scale}` holds frames `first` to `first + frames - 1`, frame `first + k` at the tick `first_tick + scale × ⌊(phase + k × num) / den⌋`, where `step` is the rational `"num/den"` in lowest terms with a positive denominator, so that a step has one pair of terms, `phase` an integer from 0 to den − 1, 0 when den is 1, and `scale` an integer from 1: the number of the values' ticks to one tick of the clock their frames follow, 1 for a plain stretch, and more for the playlist time's ticks where a tick of `reference.time_base` is several of the axis's ticks, since within a segment the playlist time follows the PTS. Frame `first` is at `first_tick`. The stretches cover every frame, one after the other, and an axis of no frames has none.
- `keys`: each reference frame's media key, as stretches of frames of one `sequence`, `{first, frames, sequence, first_pts_ticks, step, phase, scale}`, frame `first + k` with the PTS `first_pts_ticks + scale × ⌊(phase + k × num) / den⌋` by the same rule, or with every PTS unknown (`first_pts_ticks` null, `step` `"0/1"`, `phase` 0, `scale` 1), covering every frame as the ticks' do. The writer gives the PTS the scale 1 (D-099 (a)); PTS rounded to a clock coarser than their time base then take stretches that grow with the frames, a case that waits for the owner (below).
- `runs`: every run of consecutive scored frames that the check found more than half a frame off, each `{test, sequence, first, last, frames, largest_at, largest, warning}`. `test` is `"clocks"` (the frame grid's seconds against the playlist time), `"playlist-grid"` (each PTS against the frame grid of its playlist discontinuity sequence, `sequence` its number) or `"report-grid"` (the same against the report's sequences, without a map); `largest` is the largest difference in seconds, signed, at frame `largest_at`. A run with `warning` false is a fact: for a playlist whose map was refused, the report's sequences may hide a jump the playlist signals, so the run says nothing about the seconds. Design §4.2 writes each run to `warnings` and gives the facts no place; `runs`, which holds both, is an addition to design §8's axis, which the owner kept with its tests' names (D-092 (d)).
- `warnings`: what is wrong with the axis's inputs, then the text of each run that is a warning.

The stretches let a reader restore the axis exactly without the media, whatever its values: a frame that doesn't follow its stretch's rule starts a new one. The writer chooses them so that the same axis gives the same stretches (D-099 (b)). For each stretch it tries the frame duration in its values' own ticks first, with the scale 1: `frame_duration` × `tick_den` for the ticks, `frame_duration` ÷ `reference.time_base` for the PTS. For the ticks it tries next, where `tick_den` × `reference.time_base` is a whole number above 1, the frame duration in ticks of the time base with that number as the scale (D-099 (a)). Last, for both, it tries a whole step, the difference between the stretch's first two values, with the scale 1. It takes the one that covers the most frames, the first tried on a tie, so that the frame duration wins a tie with a whole step, with the smallest phase the frames allow, and the stretch ends before the first frame that doesn't follow. A stretch of one frame has the step `"0/1"`, the phase 0 and the scale 1, and an axis without a frame duration (`"0/1"`) has whole steps only, even where `reference.frame_rate` is known. No other step is tried: a sequence at another frame rate than the reference's, as an ad after its discontinuity can be (design §4.2), whose frame isn't a whole number of PTS ticks, takes stretches that grow with its frames, in keys and ticks alike, which waits for the owner (below).

What that gives (`tests/test_timeline_doc.cpp` asserts each count). At 25 fps on MPEG-TS's 90 kHz clock the axis's ticks are the clock's and a frame is 3600 of them, a whole step: the test's ad ladder, in the shape of testgen's, takes one stretch of ticks and three of keys, the ad's restart of its timestamps and the main content's return each starting one. At 24000/1001 fps a frame is 3753.75 ticks of that clock and at 60000/1001 fps 1501.5, so the PTS a muxer rounds to the nearest tick step unevenly; they follow the steps `"15015/4"` and `"3003/2"` with a phase, one stretch of keys per sequence. The axis has 8 (24000/1001) or 4 (60000/1001) ticks to a PTS tick there with EXTINF in thousandths, as in the test, and a multiple of that where an EXTINF needs finer ticks, 200 or 100 with the test's EXTINF in millionths; a frame is a whole number of the axis's ticks, so the frame grid's ticks take one stretch; the playlist time's, which within a segment follow the PTS, take the PTS's step with the axis's ticks to a PTS tick as the scale, following that multiple: one stretch for the whole reference where each EXTINF is its segment's frames, a discontinuity included, and one per segment where the EXTINF aren't, whole seconds or finer. At 30000/1001 fps a frame is 3003 ticks of the clock and the axis has 2 to a PTS tick: `"6006/1"` with the scale 1 holds the same frames as `"3003/1"` with the scale 2, and the playlist time's ticks take the first, which is tried first. None of these grows with the frames.

A reader refuses an axis of more than 2^24 frames, whose stretches it expands into a tick and a key per frame: at that limit they take 2^24 × (8 + 24) bytes, 512 MiB, on a 64-bit build, whatever the length of the document's text, since one stretch of each can claim every frame. It refuses as well, naming the stretch, a step that isn't in lowest terms or whose denominator isn't positive, a phase outside 0 to den − 1, a scale that isn't an integer from 1, a step other than `"0/1"` or a scale other than 1 for frames without a PTS, and a stretch whose values pass 64 bits. It computes each value with 128-bit integers, since k × num can pass 2^63 within one stretch, and takes a quotient ⌊(phase + k × num) / den⌋ of 2^64 or more either way as past 64 bits before it multiplies it by the scale. What it takes for the scale, any integer from 1 on either kind of stretch and 1 alone for frames without a PTS, waits for the owner (below).

## variants

One entry per scored playlist, the reference's and each pair's distorted one, in the order of their ids (design §4.5):

- `id`: its index, which every event's `variant` names; `role`: `"reference"` or `"distorted"`; `pair`: for a distorted playlist, the index of its pair's curve; null for the reference. One variant at most is the reference, and no two name one pair. The owner made a pair's number the ladder report's own `pairs[].index` (D-092 (b)), in `pair`, in a curve's `index` and in `--curve pair:<k>`; a later package implements it, with the builder that fills `pair` and the option. Until then the number is the pair's position among the report's complete `pairs`, which is the same number while no pair has failed: `pairs[].index` skips the number of a pair that failed.
- `entries`: the master's entries naming it; `url`: as the report gives it. A rung is named by its entry and URI (D-075).
- `bandwidth`: the highest BANDWIDTH of its entries (D-081 (e)); `resolution`, `codecs`: as the master gives them, null where absent; `rank_from_top` and `rank_from_bottom`: its ranks by BANDWIDTH, 1 the highest and 1 the lowest, both printed as labels (D-075). A value a variant doesn't have is null (D-092 (c)): a variant without entries, a mezzanine, has null for each, and one that isn't a rung null ranks.
- `playlist`: the media playlist as the build read it again (the design's P-D), `{url, location, sha256}`, or null for an input that isn't a playlist (D-092 (c)). A variant was read from a playlist when it has a location or a SHA-256: the writer gives one with neither as null, and a reader refuses an object whose `location` and `sha256` are both empty.
- `segment_map`: for a variant read from a playlist, `"ok"`, or `"refused: "` followed by the reason the demux pass gave; a refused variant has no segment-bound event (D-081 (c), D-086). Null for a variant without a playlist, a mezzanine (design §5.2), which has no map (D-092 (c)). A reader takes null exactly when `playlist` is null, and no string but those two forms.

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

Null when no detection ran: `mirage_vmaf timeline` detects only when the window W and the count N are both given, and otherwise writes no attribution (D-077). When it ran: `threshold` (T, in VMAF points), `window` (W) and `min_points` (N), both counting points of the curve; `baseline`, `"median-non-low"`, the median of the last W points that weren't low; `warm_up`, W/2 rounded down and at least 1, the points at a curve's start that can't be low; `context_first`, `"no-special-case"`, how a libvmaf context's first pair is taken; `curves`, the curves detection ran on, each `{kind, index}` with `kind` `"pair"`; and `session_rules`, null for a ladder alone (the rules that clip a pair's drops to a session's spans, design §6.3, come with the session part). `curves` as that list, the `{kind, index}` form where design §7's `--curve` option writes `pair:<k>`, and `session_rules` null are WP08's forms, which the owner kept (D-092 (d)).

## overlap

`rule`, `"half-open"`: a point at frame f meets an interval `[a, b)` when a ≤ f < b, a span `[s, e)` when s < b and e > a (D-078). `margin_before` and `margin_after`: seconds by which every interval was widened on the axis before events were looked for, `"0/1"` for none, the default. `scope`, `"curve-inputs"`: a pair's curve lists the events of its distorted variant and of the reference, and no other variant's (D-078); the name is WP08's, which the owner kept (D-092 (d)).

## attributions

One per drop that detection found on a curve it ran on, and none when `detection` is null (D-077): all that `mirage_vmaf timeline` writes has `detected` true. An attribution with `detected` false answers an interval a user gave rather than a drop (design §6.1); the timeline subcommand never writes one, and a reader takes one whether detection ran or not, so that `mirage_vmaf attribute`'s answers can take this layout (WP09a).

- `curve`: `{kind, index}`, the pair's curve, as `variants[].pair` names it; `detected`: true for a drop detection found, false for a user's interval.
- `interval`: the drop's reference frames `ref_index` to `ref_end`, half-open: from its first low point's frame to the frame of the curve's next point, or to one past its last point where the curve ends (D-081 (g)); `t` and `t_end`, their seconds; `points`, the curve's points with a score in it.
- `baseline`: the trailing baseline at the drop's first point; `min` and `mean`: of its points' scores; `drop_magnitude`, the baseline minus the minimum, and `mean_drop`, the baseline minus the mean (Q27 (c)).
- `overlapping_events`: six groups, one per source in the order of the vocabulary above, each `{source, events}`, empty groups included (D-092 (d)). Each event is `{event, offset, offset_end, offset_frames, offset_end_frames}`: its `id`, and its start's and a span's end's distance from the interval's start in seconds on the axis and in reference frames. Within a group events come by offset, with no ranking (D-078).

## Reading a document back

`timeline::from_json` reads what the writer writes and refuses anything else, naming the key: another schema or version, a missing or unknown key, a value of another kind, an event type outside its source's vocabulary, two events with one id, two variants of one pair or two references, an event, a variant, a session or a curve that isn't there, and seconds or offsets that disagree with the axis. It refuses as well what the sections above rule out: a rational whose terms aren't in their shortest form; a session part other than a ladder's; ticks or keys whose stretches don't cover the axis's frames, or whose steps, phases, scales or values break the rules of the axis; an axis whose `seconds` and `checked` aren't paired as above; a `playlist` whose `location` and `sha256` are both empty, and a `segment_map` that isn't null exactly when the variant's `playlist` is; an event whose `seq` is negative, or whose media key isn't its frame's in `axis.keys`; a network or player event without a session; a wall clock on an event of another source; and an attribution with `detected` true when `detection` is null. It restores the axis exactly from its stretches and widens each attribution's interval by the overlap's margins again. It gives a null `segment_map` back as `"ok"`, the default of `timeline::Variant`, and leaves the curves to the caller, who reads them from the ladder report the document names once its hashes match: curve k is the report's k-th complete pair, on the variant whose `pair` is k, until D-092 (b) makes k the report's `pairs[].index`.

Every key is required and no other is taken, and every value in seconds is computed again and compared, so any later addition to the layout takes another `schema_version`; the ladder report's reader, by contrast, passes over keys it doesn't know in later layouts. The owner kept that strictness (D-092 (d)).

The writer checks nothing: a document whose events lack ids, or whose payloads hold one name twice, or a variant without a playlist whose segment map is a refusal (D-099 (b)), is written as it is given, and the reader then refuses it. `src/TimelineDoc.h` lists what the writer's inputs must be.

## Settled by the owner

Forms of this version that change what a document says and that the design and D-072 left open, as the owner settled them on 2026-11-05 (D-092):

- (a) the axis's per-frame values as stretches with a rational step, the frame duration in the values' ticks where they follow it, and a phase, so that the values come back exactly whatever they are and the document grows with the segments, not the frames (above), but in two cases that wait for the owner (below); this version implements it, with D-099 (a)'s scale;
- (b) a pair's number, in `variants[].pair`, in a curve's `index` and in `--curve pair:<k>`, as the report's own `pairs[].index`; a later package implements it, and until then the number is the pair's position among the report's complete pairs, the same number while no pair has failed;
- (c) null for a value a variant doesn't have: the BANDWIDTH, the ranks and the playlist, as before, and the `segment_map` of a variant without a playlist, where the first build of this version wrote `"ok"`; this version implements it;
- (d) the other forms as they were built: `axis.runs`; a curve as `{kind, index}`; rationals as `"num/den"` strings; the names `"curve-inputs"`, `"clocks"`, `"playlist-grid"` and `"report-grid"`, and the stretches' members; `detection.curves` as the curves detection ran on, with `session_rules` null; `overlapping_events` with all six groups, empty ones included; and a reader that requires every key, takes no other and recomputes every value in seconds, so that any addition takes a new `schema_version`.

The choices made in implementing D-092, as the owner settled them the same day (D-099):

- (a) a `scale` in every stretch of both kinds, a whole multiplier of the formula's quotient: where a tick of `reference.time_base` is a whole number of the axis's ticks, 8 and 4 at 24000/1001 and 60000/1001 fps on MPEG-TS's 90 kHz clock with EXTINF in thousandths, the playlist time's ticks follow the PTS of their segment with that number as the scale and the frame duration in PTS ticks as the step, where no step of D-092 (a)'s formula alone followed them, so that they too grow with the segments; the keys' PTS take the scale 1, and the case found since that would need another waits for the owner (below); this version implements it;
- (b) the writer's other choices as they were built: where several steps and phases hold the same frames, the one that covers the most frames, the frame duration on a tie, the smallest phase, and `"0/1"` with the scale 1 for a stretch of one frame; whole steps for the keys of an axis without a frame duration; and a variant without a playlist whose segment map isn't the default written as given, for the reader to refuse.

What still waits for the owner, each as this version builds it, changing a document's bytes or what a reader takes, and no value read back:

- A choice made in implementing D-099 (a): where the frame duration in the axis's ticks with the scale 1 and the frame duration in PTS ticks with a scale above 1 hold the same frames, as they do wherever a frame is a whole number of PTS ticks, the writer takes the first, which it tries first. At 30000/1001 fps on the 90 kHz clock the playlist time's ticks so have `"6006/1"` with the scale 1, not `"3003/1"` with the scale 2, which D-099 (a) read literally gives; D-099 (b)'s tie goes to the frame duration over a whole step and doesn't choose between two frame durations.
- What a reader takes for `scale`: any integer from 1 on both kinds of stretch, the keys' included, which the writer gives the scale 1, and 1 alone for frames without a PTS. The other choice, the scale 1 alone on every stretch of keys, would refuse the keys of a later writer that gave them a scale, which this reader takes within this version.
- The case D-099 (a) asks to be reported, where the keys' PTS need a scale other than 1: PTS rounded to a clock coarser than their time base, as a millisecond clock written on MPEG-TS's 90 kHz one, follow neither frame duration, so the keys, with the scale 1, and the playlist time's ticks, with the axis's ticks to a PTS tick as the scale, take stretches that grow with the frames.
- A sequence at another frame rate than the reference's, as an ad after its discontinuity can be (design §4.2), whose frame isn't a whole number of PTS ticks: the writer tries the reference's frame duration and whole steps alone (D-092 (a), D-099 (b)), so the sequence's stretches grow with its frames, in keys and ticks alike.

Every value comes back exactly in each case, and a writer that chose otherwise, or tried more steps or scales, would stay within schema version 1 as long as a reader takes the scales this one takes.

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
