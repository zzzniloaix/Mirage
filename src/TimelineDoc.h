#pragma once

// The timeline document, written and read (DECISIONS.md D-072): the ladder part, by WP08.
//
// One document per timeline build, in a file of its own (Q01 (B)): the
// events of a ladder report's timeline on the reference's frames (D-073),
// the attributions of the drops detected in its pairs' curves (D-077,
// D-078), and what they were made from: the ladder report, named by the
// SHA-256 of its bytes and of its scores digest (Q06 (c)), its provenance
// copied (Q04 (a)), and every option of the build. No score is copied: a
// reader takes the curves from the report the document names. The layout is
// eval/phase3-survey/design.md §8's, as schema version 1, with the event
// types of its §5.9 (Q04 (a), Q05 (a)); docs/schema/mirage-timeline-1.schema.json
// is its JSON Schema, and docs/TIMELINE_FORMAT.md describes it key by key.
//
// The rules.
//
// - The ladder part: every key of §8's layout, in its places. The session
//   part's two keys hold what a ladder alone has, session_plan null and
//   sessions empty, until WP09b fills them in round 2 with the network and
//   player events' own fields (design §8, "Who writes what"), so that
//   version 1 has one layout before and after; a reader of this version
//   takes those two values only. An event of a ladder's document therefore
//   has no session, none is of the network or player source, whose events
//   are a session's (D-068), and none has a wall clock, which only observed
//   events have (D-073).
// - Additions to §8's layout, each needed to read a document back without
//   its media (design §3.5): the axis's seconds and media keys, one of each
//   per reference frame (axis.tick_den, axis.ticks, axis.keys); every run of
//   the check of design §4.2 (axis.runs), the facts among them, which
//   Axis::warnings leaves out; and each sequence's first PTS in ticks
//   (first_pts_ticks) beside its seconds. D-092 (a) settles how the
//   per-frame values are written, and (d) keeps axis.runs.
// - The per-frame values as stretches (D-092 (a), D-099 (a)). A stretch
//   holds frames first to first + frames - 1, frame first + k at
//   first_tick + scale * floor((phase + k * num) / den), a key's PTS from
//   first_pts_ticks alike, its step "num/den" in lowest terms with a
//   positive denominator, its phase from 0 to den - 1 and its scale a whole
//   number from 1, so that a step can be a frame that isn't a whole number
//   of ticks, and the scale the number of the values' ticks to one tick of
//   the clock their frames follow, 1 for a plain stretch. A frame that
//   doesn't follow starts a new stretch: every value comes back exactly,
//   whatever it is. The writer tries, for each stretch, the frame duration
//   in the values' own ticks first, of 1/tick_den s for the seconds and of
//   the time base for the PTS, with the scale 1; for the seconds next, where
//   the axis has a whole number above 1 of ticks to a PTS tick, the frame
//   duration in PTS ticks with that number as the scale, since within a
//   segment the playlist time follows the PTS (src/TimelineAxis.h); and
//   last a whole step, the difference between the stretch's first two
//   values, with the scale 1. It takes the one that covers the most frames,
//   the first tried on a tie, with the smallest phase the frames allow; a
//   stretch of one frame, and the frames without a PTS, have the step 0/1,
//   the phase 0 and the scale 1. At 25 fps on MPEG-TS's 90 kHz clock the
//   axis's ticks are the clock's, a frame is 3600 of them, a whole step, and
//   the ad ladder's axis takes one stretch of ticks and three of keys, the
//   ad's restart of its timestamps and the main content's return each
//   starting one. At 24000/1001 fps a frame is 3753.75 ticks of that clock
//   and at 60000/1001 fps 1501.5, and the axis has 8 or 4 ticks to a PTS
//   tick with EXTINF in thousandths, as in the test, and a multiple of that
//   where an EXTINF needs finer ticks, 200 or 100 with the test's EXTINF in
//   millionths: the PTS a muxer rounds to the nearest tick follow the steps
//   15015/4 and 3003/2, one stretch of keys per sequence; the frame grid's
//   seconds, a whole number of the axis's ticks a frame, take one stretch;
//   and the playlist time's take the PTS's steps with the axis's ticks to a
//   PTS tick as the scale, following that multiple, one stretch for the
//   whole reference where each EXTINF is its segment's frames, a
//   discontinuity included, and one per segment where the EXTINF aren't,
//   whole seconds or finer. At 30000/1001 fps a frame is 3003 ticks of the
//   clock, the axis has 2 to a PTS tick, and 6006/1 with the scale 1 holds
//   the same frames as 3003/1 with the scale 2, so the playlist time's
//   ticks take the first, tried first. tests/test_timeline_doc.cpp asserts
//   each of these counts.
// - Numbers. Integers as integer literals; every double through
//   json::number (src/JsonText.h), which writes a whole double as an integer
//   literal, so the schema types each such field "number"; null for NaN and
//   for an absent value. Rationals exactly, as "num/den": time bases, frame
//   rates, frame durations, margins and the stretches' steps, each term in
//   its shortest form, as std::format writes an integer, so that a rational
//   has one text. Seconds are doubles for output only, computed from exact
//   ticks (the design's P-A).
// - Order. Keys in §8's order; events in canonical order (the design's P-I),
//   each written once and named by its id wherever an attribution lists it;
//   variants and attributions in the order given. Identical inputs that
//   meet to_json()'s preconditions give byte-identical documents.
// - Writing (the design's P-W): write_document() writes through a temporary
//   file renamed over the path, as vmafreport::write_file does, so that a
//   failed write leaves no partial document. It is the same rule written
//   again, since the player, which compiles the core from round 3 on
//   (design §3.3), doesn't compile src/VMAFReport.cpp.
// - Reading: from_json() reads what to_json() writes, every key required and
//   no other, and refuses what it can't read, naming the key: another schema
//   or schema_version, a value of another kind, an event type outside
//   §5.9's vocabulary for its source, two events with one id, two variants
//   of one pair or two references, an event, a variant, a session or a
//   curve that isn't there, and seconds or offsets that disagree with the
//   axis. It also refuses what the rules above and the decisions rule out:
//   a rational whose terms aren't in their shortest form; a session part
//   other than a ladder's; an axis whose stretches don't cover its frames,
//   whose steps, phases or scales break the per-frame values' rules (a
//   step not in lowest terms or whose denominator isn't positive, a phase
//   outside 0 to den - 1, a scale below 1, and for frames without a PTS a
//   step other than 0/1 or a scale other than 1), whose values pass 64
//   bits, or whose clock and check aren't paired as build_axis() pairs
//   them, the playlist time with the segment map and the frame grid with
//   the report only (src/TimelineAxis.h); a variant's playlist with neither
//   a location nor a SHA-256, which to_json() writes as null, and a
//   segment_map that isn't null exactly when the playlist is (D-092 (c));
//   an event whose sequence is negative or whose media key isn't the axis's
//   key at its ref_index (Event::key, src/TimelineModel.h); a network or
//   player event without a session; a wall clock on an event of another
//   source (D-073); and a detected attribution where detection is null
//   (D-077). It takes one thing that mirage_vmaf timeline never writes: an
//   attribution whose detected is false, an interval a user gave rather
//   than a drop (design §6.1), whether detection ran or not, so that the
//   answers of mirage_vmaf attribute (WP09a) can take this layout.
//   Timeline::curves stays empty: the caller fills it from the ladder report
//   the document names, once that report's hashes match.
//
// The owner's choices (D-092, of 2026-11-05), on the forms that WP08 left
// open because they change the output and neither the design nor D-072
// settles them; the place that makes each cites its letter.
// - (a) The per-frame values as stretches with a rational step and a phase,
//   as above, the stretches growing with the segments, not the frames, but
//   in two cases pending the owner (below); implemented here, with D-099
//   (a)'s scale.
// - (b) A pair's number, in variants[].pair, in a curve's index and in
//   design §7's --curve pair:<k>, is the ladder report's own pairs[].index,
//   which skips the number of a pair that failed. A later package
//   implements it once WP09a, whose builder fills Variant::pair and whose
//   CLI parses --curve, has merged. Until then the number is the pair's
//   position among the report's complete pairs, which Timeline::curves
//   follows, the query's scope reaches through Curve::variant and the
//   reader's curve check relies on, and which is the same number while no
//   pair has failed.
// - (c) A value a variant doesn't have is null: the BANDWIDTH and both
//   ranks of a variant without entries or that isn't a rung, and the
//   playlist of a variant not read from one, as WP08 wrote them; and then
//   its segment_map too, where WP08 wrote "ok", since a reference that
//   isn't a playlist, a mezzanine (design §5.2), has no map. The reader
//   takes null there exactly when the playlist is null, and gives it back
//   as Variant's default, "ok"; implemented here.
// - (d) The rest stays as WP08 built it: axis.runs; a curve as {"kind",
//   "index"}, where design §7's --curve writes "pair:<k>"; time bases, frame
//   rates, the frame duration and the margins as "num/den" strings;
//   overlap.scope "curve-inputs"; the runs' tests "clocks", "playlist-grid"
//   and "report-grid"; the stretches' members (first, frames, first_tick;
//   first, frames, sequence, first_pts_ticks), with (a)'s step and phase
//   and D-099 (a)'s scale; detection.curves, the curves detect() ran on,
//   with session_rules null; overlapping_events with all six groups, empty
//   ones included; and a strict reader within version 1, every key
//   required, none unknown and every value in seconds recomputed, so that
//   any addition to the layout takes another schema_version, where
//   LadderReportRead passes over keys it doesn't know in later report
//   layouts.
//
// The owner's choices (D-099, of 2026-11-05), on what WP08b left open in
// implementing D-092 (a) and (c):
// - (a) The scale, as above. The playlist time's ticks, kept in a whole
//   number of the axis's ticks to a PTS tick, 8 and 4 at 24000/1001 and
//   60000/1001 fps on MPEG-TS's 90 kHz clock with EXTINF in thousandths,
//   follow the PTS of their segment with that number as the scale and the
//   frame duration in PTS ticks as the step, where no step of D-092 (a)'s
//   formula alone followed them; so they too grow with the segments. Every
//   stretch of both kinds has a scale, and the writer gives a key's PTS the
//   scale 1; the case found since that would need another is pending the
//   owner (below). Implemented here.
// - (b) WP08b's other choices, as built: where several steps and phases
//   hold the same frames, the step that covers the most frames, the frame
//   duration on a tie, the smallest phase, and 0/1 with the scale 1 for a
//   stretch of one frame; whole steps for the keys of an axis without a
//   frame duration (0/1), even where axis.reference.frame_rate is known;
//   and a variant without a playlist whose segment map isn't Variant's
//   default written as given, for the reader to refuse (to_json()).
//
// Pending the owner, each as built here, changing a document's bytes or
// what the reader takes and no value read back:
// - A choice made in implementing D-099 (a). Where the frame duration in
//   the axis's ticks with the scale 1 and the frame duration in PTS ticks
//   with a scale above 1 hold the same frames, the writer takes the first,
//   which it tries first: so at 30000/1001 fps on the 90 kHz clock, a frame
//   being a whole 3003 PTS ticks, the playlist time's ticks have 6006/1 with
//   the scale 1, not 3003/1 with the scale 2, which D-099 (a) read literally
//   gives. D-099 (b)'s tie goes to the frame duration over a whole step and
//   doesn't choose between two frame durations.
// - What the reader takes for the scale: any from 1 on both kinds of
//   stretch, the keys' too, which the writer gives the scale 1, and 1 alone
//   for frames without a PTS. The other choice, the scale 1 alone on every
//   key stretch, would refuse a later writer's keys with a scale (below),
//   which this reader takes within version 1.
// - The case D-099 (a) asks to be reported, where a key's PTS need a scale
//   other than 1: PTS rounded to a clock coarser than their time base, as a
//   millisecond clock written on MPEG-TS's 90 kHz one, follow neither frame
//   duration, so the keys, with the scale 1, and the playlist time's ticks,
//   with the axis's ticks to a PTS tick as the scale, take stretches that
//   grow with the frames.
// - A sequence at another frame rate than the reference's, as an ad after
//   its discontinuity can be (design §4.2), whose frame isn't a whole
//   number of PTS ticks: the writer tries the reference's frame duration
//   and whole steps alone (D-092 (a), D-099 (b)), so that the sequence's
//   stretches grow with its frames, in keys and ticks alike.
// Every value comes back exactly in each case, and a writer that chose
// otherwise, or tried more steps or scales, would stay within version 1 as
// long as the reader takes the scales it takes now.
//
// Core code (design §3.1): plain structs and text in and out, and libavutil's
// rationals only. Nothing here reads a file; write_document() alone writes
// one. Unit-tested in tests/test_timeline_doc.cpp, whose subset JSON Schema
// validator checks the writer's output against the schema file (the
// design's P-AI).

#include "LadderReportRead.h"
#include "TimelineDetect.h"
#include "TimelineModel.h"
#include "TimelineQuery.h"

extern "C" {
#include <libavutil/rational.h>
}

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace timeline {

// What a document says it is: its "schema" and its "schema_version", an
// integer from 1 (Q05 (a)).
inline constexpr std::string_view kDocSchema  = "mirage-timeline";
inline constexpr int              kDocVersion = 1;

// The event types of `source`, in the order of eval/phase3-survey/design.md
// §5.9, whose vocabulary is part of schema version 1 (Q04 (a)); none for a
// value outside Source's.
std::span<const std::string_view> event_types(Source source);

// A payload's or an option's number as a double, whichever form it has:
// from_json() gives a number back in the form of its text, and json::number
// writes a whole double as an integer literal, so that a double the build
// held, a segment's extinf or a cue_out's declared duration of 2 s
// (src/PlaylistEvents.cpp), can come back as an integer. nullopt for a
// value that is no number. One accessor for every reader of a document's
// payloads, the attribute subcommand's and the player's among them.
std::optional<double> number_of(const Value& value);

// A file named by its path as given and the SHA-256 of its bytes.
struct FileRef {
    std::string path;
    std::string sha256;   // lowercase hex (filehash::sha256_hex)
};

// The ladder report a document is built from: its path and its two hashes
// (Q06 (c)), and its provenance as the report gives it (Q04 (a)).
struct ReportInfo {
    std::string path;              // as given
    std::string sha256;            // of its bytes
    std::string scores_sha256;     // of scores_text() (src/LadderReportRead.h)
    std::string status, reference_mode;
    std::string model, model_source;
    std::string model_sha256;      // empty for a built-in model, which has none: written null
    std::string scaler;
    std::string libvmaf_version, libvmaf_package_version;
    std::string master_url, master_location, master_sha256;   // the report's ladder.*
};

// `scores`' provenance as ReportInfo holds it. path, sha256 and
// scores_sha256 stay empty: the caller, who reads the file, computes the
// hashes (the design's P-Z).
ReportInfo report_info(const LadderScores& scores);

// The reference the axis is built on, as axis.reference gives it: its URL,
// and the frame rate and the discontinuity sequences (D-057) of the pair
// whose reference build_axis() takes, the one whose reference sequences
// hold the most frames, the first of equal ones (src/TimelineAxis.cpp). Its
// time base is the axis's (Axis::time_base).
struct ReferenceInfo {
    std::string url;                       // the report's reference.url
    AVRational  frame_rate = { 0, 1 };     // as the report gives it; 0/1: unknown
    std::vector<Sequence> sequences;       // first_pts in ticks of the axis's time base
};

// `scores`' reference, as above, for `axis`, the axis build_axis() made of
// `scores`. Axis doesn't record the pair it was built on, so this picks it
// by build_axis()'s rule, written again here, and refuses a pair that reads
// the reference with another time base than the axis's: should the two
// rules part, the sequences' first_pts_ticks would be on another time base
// than axis.reference.time_base, which a reader can't detect. On a refusal
// returns false with `err` saying why and leaves `out` a default value;
// otherwise `err` is empty.
bool reference_info(const LadderScores& scores, const Axis& axis, ReferenceInfo& out,
                    std::string& err);

// What detection ran with, when it ran (D-077): its options, which give W
// and N, and the curves it ran on, every pair's for a ladder.
struct Detection {
    DetectOptions         options;
    std::vector<CurveRef> curves;
};

// What a document holds beside its timeline and its attributions.
struct DocInfo {
    std::string tool    = "mirage_vmaf";   // design §8's
    std::string command = "timeline";
    // The FFmpeg libraries and their versions at run time, in order, as the
    // ladder report lists its own (P-035).
    std::vector<std::pair<std::string, std::string>> ffmpeg_libraries;
    Payload options;                       // every option of the build, with its value
    std::optional<FileRef> config_file;    // the config file it was read from (D-030); nullopt: none
    ReportInfo    report;
    ReferenceInfo reference;
    // nullopt, or options without both W and N: no detection ran, and the
    // document says "detection": null (D-077).
    std::optional<Detection> detection;
    QueryOptions overlap;                  // the margins the attributions were made with (D-078)
};

// The document of `timeline` and `attributions`, as above, with a line feed
// at its end. The events need not be in canonical order; each Found::event
// is an index into them. to_json() checks nothing and writes what it is
// given, so a document that from_json() reads back, and that comes out the
// same bytes whatever the events' order, needs of its inputs:
// - every event an id, and no two the same one, as assign_ids() gives them:
//   without them the canonical order isn't total;
// - each event's type one of its source's (event_types()), its source
//   neither network nor player and its session -1, as a ladder alone has
//   them (above), no wall clock, its variant -1 or an index into the
//   variants, and its media key the axis's key at its ref_index;
// - each Variant::index its position, one variant at most the reference,
//   and no two naming one pair; a variant without a playlist (no location
//   and no SHA-256) with Variant's default segment_map, "ok", which the
//   document gives as null (D-092 (c)), any other value being written as
//   it is, for the reader to refuse (D-099 (b));
// - names unique within each payload object, in the options and among the
//   FFmpeg libraries, as JSON's objects need (src/JsonRead.h);
// - the axis as build_axis() makes it, a tick and a media key per frame, at
//   most 2^24 frames, its clock and check paired, with DocInfo::reference
//   from reference_info() for that axis;
// - check_options() passing on the detection's options, and a detected
//   attribution only with the detection that found it;
// - each Found::event an index into the events: one past them is written
//   as null, which the schema and the reader refuse.
// A document that breaks one of them is written all the same. from_json()
// of the text refuses it, naming the first key that is wrong: the check to
// run before write_document().
std::string to_json(const Timeline& timeline, const std::vector<Attribution>& attributions,
                    const DocInfo& info);

// The document `text`, read back: into `timeline` its axis, variants,
// events (in canonical order, with Timeline::build_index() run) and
// warnings, its curves left empty; into `attributions` each attribution,
// every Found naming its event by its index in those events, and each
// searched interval widened again by the overlap record's margins (widen());
// into `info` the rest. The axis's stretches come back as a tick and a
// media key per frame (D-092 (a), D-099 (a)), and the null segment_map of a
// variant without a playlist as Variant's default, "ok" (D-092 (c)). A
// number of a payload or of the options comes back in the form of its
// text: an integer literal as std::int64_t, any other number as a double,
// so that a whole double written by json::number comes back as an integer;
// number_of() takes either. On a refusal `err` says why and the three
// outputs are left as default values; otherwise `err` is empty.
bool from_json(std::string_view text, Timeline& timeline, std::vector<Attribution>& attributions,
               DocInfo& info, std::string& err);

// Writes `text` to `path` through `path` + ".partial", renamed over `path`
// once all of it is written and closed (the design's P-W). On a failure
// returns false with `err` saying why, and leaves neither file behind.
bool write_document(const std::string& path, std::string_view text, std::string& err);

} // namespace timeline
