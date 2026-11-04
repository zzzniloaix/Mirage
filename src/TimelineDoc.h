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
//   its media (design §3.5) and none of them settled by the design: the
//   axis's seconds and media keys, one of each per reference frame, written
//   as stretches of frames over which they step evenly (axis.tick_den,
//   axis.ticks, axis.keys); every run of the check of design §4.2
//   (axis.runs), the facts among them, which Axis::warnings leaves out; and
//   each sequence's first PTS in ticks (first_pts_ticks) beside its
//   seconds. Evenly spaced frames take one stretch of each, as at 25 fps on
//   MPEG-TS's 90 kHz clock, where a frame is 3600 ticks. Where a frame isn't
//   a whole number of ticks the PTS a muxer rounds step unevenly, and so do
//   the playlist time's ticks that follow them: a frame at 24000/1001 fps is
//   3753.75 ticks of that clock and one at 60000/1001 fps 1501.5, so a new
//   stretch starts every 4 frames, or every 2 (tests/test_timeline_doc.cpp).
//   The stretches then grow with the frames, not with the segments as
//   design §8's "Size" has it for the events; a measurement of the
//   documents' size at those rates is unrecorded. The encoding is pending
//   the owner (below).
// - Numbers. Integers as integer literals; every double through
//   json::number (src/JsonText.h), which writes a whole double as an integer
//   literal, so the schema types each such field "number"; null for NaN and
//   for an absent value. Rationals exactly, as "num/den": time bases, frame
//   rates, frame durations, margins. Seconds are doubles for output only,
//   computed from exact ticks (the design's P-A).
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
//   a session part other than a ladder's; an axis whose stretches don't
//   cover its frames, or whose clock and check aren't paired as
//   build_axis() pairs them, the playlist time with the segment map and
//   the frame grid with the report only (src/TimelineAxis.h); an event
//   whose sequence is negative or whose media key isn't the axis's key at
//   its ref_index (Event::key, src/TimelineModel.h); a network or player
//   event without a session; a wall clock on an event of another source
//   (D-073); and a detected attribution where detection is null (D-077).
//   It takes one thing that mirage_vmaf timeline never writes: an
//   attribution whose detected is false, an interval a user gave rather
//   than a drop (design §6.1), whether detection ran or not, so that the
//   answers of mirage_vmaf attribute (WP09a) can take this layout.
//   Timeline::curves stays empty: the caller fills it from the ladder report
//   the document names, once that report's hashes match.
//
// Choices pending the owner. Each changes the output, the design and D-072
// leave it open, and the code does what is said here until the owner
// answers; the place that makes each says so.
// - The axis's per-frame values as stretches (above), against plain
//   per-frame values or another encoding; and axis.runs, an addition to
//   §8's axis, where design §4.2 writes each run to axis.warnings and gives
//   the facts no place.
// - Forms and names §8 leaves open: a curve as {"kind", "index"}, where
//   design §7's --curve writes "pair:<k>"; time bases, frame rates, the
//   frame duration and the margins as "num/den" strings, since a margin of
//   1/3 s has no exact decimal; overlap.scope "curve-inputs"; the runs'
//   tests "clocks", "playlist-grid" and "report-grid"; the stretches'
//   members (first, frames, first_tick and step; first, frames, sequence,
//   first_pts_ticks and step); detection.curves, the curves detect() ran
//   on, with session_rules null; and overlapping_events with all six
//   groups, empty ones included.
// - A variant's pair and a curve's index: the pair's position among the
//   ladder report's complete pairs, which Timeline::curves follows, the
//   query's scope reaches through Curve::variant and the reader's curve
//   check relies on; not the report's own pairs[].index, which skips the
//   number of a pair that failed, so that the two differ once one does.
// - Sentinels: null for the BANDWIDTH and both ranks of a variant without
//   entries, and for the playlist of a variant not read from one; a
//   segment_map that is "ok" or a refusal, so that a reference that isn't a
//   playlist, a mezzanine (design §5.2), says "ok", Variant's default, for
//   a map it doesn't have.
// - The reader's strictness within version 1: every key required, none
//   unknown and every value in seconds recomputed, so that any later
//   addition to the layout needs another schema_version, where
//   LadderReportRead passes over keys it doesn't know in later report
//   layouts.
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
//   and no two naming one pair;
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
// into `info` the rest. A number of a payload or of the options comes back
// in the form of its text: an integer literal as std::int64_t, any other
// number as a double, so that a whole double written by json::number comes
// back as an integer; number_of() takes either. On a refusal `err` says why
// and the three outputs are left as default values; otherwise `err` is
// empty.
bool from_json(std::string_view text, Timeline& timeline, std::vector<Attribution>& attributions,
               DocInfo& info, std::string& err);

// Writes `text` to `path` through `path` + ".partial", renamed over `path`
// once all of it is written and closed (the design's P-W). On a failure
// returns false with `err` saying why, and leaves neither file behind.
bool write_document(const std::string& path, std::string_view text, std::string& err);

} // namespace timeline
