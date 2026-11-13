#pragma once

// mirage_vmaf's timeline and attribute subcommands (DECISIONS.md D-079): written by WP09a, sessions by WP09b,
// the decoded SCTE-35 messages' options and lines by WP09c.
//
// tools/mirage_vmaf.cpp hands every argument after a first word of
// "timeline" or "attribute" to run_timeline() or run_attribute(), before
// vmafcli::parse() sees them, so that mirage_vmaf's own command lines keep
// their meaning (eval/phase3-survey/design.md §3.6, §7).
//
// - timeline (design §3.4) reads a ladder report that mirage_vmaf --ladder
//   wrote (D-081 (b)), each scored media playlist again (P-D) and each
//   scored input's demux pass (D-074), builds the timeline
//   (src/TimelineBuild.h), detects drops on the pairs' curves when W and N
//   are both given (D-077), and writes the timeline document
//   (src/TimelineDoc.h; D-072). Without both, the document has detection
//   null and no attributions, and a line on stderr says which was missing
//   (D-077; no_detection_note()): a lone W or N is taken as D-077's "not
//   both given", as the owner's D-099 (c) keeps it. The SCTE-35 messages
//   the playlists' tags carry are decoded and placed in the build (design
//   §5.5; src/Scte35Events.h): --cue-types sets the segmentation types that
//   make a time_signal a cue-out or a cue-in, D-106 (b)'s by default
//   (timeline::kDefaultCueTypes), each a Start or an End type
//   (timeline::check_message_options()); --bad-crc keep|drop what becomes
//   of a message whose CRC_32 fails, kept and marked by default, with a
//   warning either way (D-106 (c)). Where a playlist carries a message that
//   the build decodes, the document records both, their defaults included
//   (D-113 (a), D-120 (b)).
// - timeline --session <plan.json> (design §3.4, step 6; D-068) also reads a
//   scripted session's plan (src/SessionPlan.h; D-105 (a)), which the
//   document records by its path and SHA-256, places the session's spans
//   and player events on the timeline (add_session()), and with --fetch on,
//   the default with a session (design §7), fetches the session's files in
//   play order (fetch_files(), src/SessionFetch.h; D-104 (a)) at the
//   report's URLs, or at the same paths from the master's directory on
//   --fetch-base's server, a URL outside the master's directory refusing the
//   whole fetch (D-108 (c)), and adds their requests as network events with
//   their wall clock (add_requests(); D-073, D-105 (c)). With --fetch off
//   the session has no network event and no UTC anchor (D-108 (c)). A fetch
//   whose files aren't http or https URLs, with no --fetch-base, is refused
//   before anything is fetched, exit status 2, the message naming
//   --fetch-base and --fetch off (D-108 (a); check_fetch()): NetworkLogger
//   times a local file's open, not its read. With W and N, detection runs on
//   every pair's curve and then on the session's, its drops its pairs'
//   drops clipped to its spans (D-077; attribute_drops()). SIGINT and
//   SIGTERM stop a build, its playlist reads, demux passes and fetch, as they
//   stop mirage_vmaf --ladder's run; a stopped build exits with status 1, as
//   --ladder's does (D-047), and writes no document, where D-047 has
//   --ladder write a report of its complete pairs. Each choice the design
//   and the decisions left open here is listed below: the owner kept them as
//   built (D-111, D-113 (h)). The path rebased for a URL with dot segments,
//   and the resolved URL that a refusal's text gives, which the owner kept
//   as built too (D-113 (d)), are src/SessionPlan.h's (fetch_url()).
// - attribute (design §3.5) reads a timeline document and the ladder report
//   it names, checks the report's SHA-256, or for a redacted copy its
//   scores digest (D-072), takes the pairs' curves from it, and answers one
//   query without reading media: the events that meet an interval given in
//   seconds on the axis (--interval) or in reference frames (--frames), or a
//   detected drop of the document (--drop), or each drop that detection
//   finds again with the parameters given (--auto); as text by default (the
//   design's P-AD), or as a timeline document (--format json). A session's
//   curve, which the document's session spans and the report's curves give
//   (curve_points()), is --curve session:<id>, its id the document's.
//
// Options (the design's P-V). Each subcommand has an option table of its
// own, one row an option with its config-file setting and its help, as
// vmafcli's is; every option but --config is also a `name = value`
// setting of D-030's config file, one per invocation, recorded by its path
// and SHA-256; the command line replaces the file's setting. vmafcli's
// parser is unchanged: its parse() is written against its own table.
// attribute's queries are one setting, so that one on the command line
// replaces the file's, and --threshold, --window and --min-points go with
// --auto only: given with another query they are refused, and a file's
// are left out when the command line gives another query, as vmafcli
// leaves out a file's settings of the other form of its inputs. --auto,
// which takes no value on the command line, is `auto = on` in a file.
// timeline's --fetch and --fetch-base go with --session only (D-113 (h),
// below), and --fetch-base with --fetch on only (D-111 (d)): given
// otherwise, on the command line or in the file, they are refused, as
// attribute refuses --threshold with another query than --auto. The plan
// itself is a file of its own, since D-030's file holds each setting once
// and a plan is a list of spans (design §7; D-105 (a)). --cue-types takes
// segmentation_type_ids in hex, each alone or a range, comma-separated,
// with blanks after a comma (parse_cue_types()), so that the default as
// design §7 writes it, "0x22-0x23, 0x30-0x37, 0x44-0x47", a blank after
// each comma as in Q15 (a), which D-106 (b) adopts, is taken, on the
// command line and in a config file (D-113 (b), D-120 (c), below). A value
// in a config file is checked on its line as one on the command line is, a
// type that is neither a Start nor an End type included.
//
// Defaults are the decisions' only: T = 6, W and N none (D-077), margins
// none (D-078), text (D-079), --fetch on with a session (design §7),
// --cue-types D-106 (b)'s and --bad-crc keep (D-106 (c)).
//
// The owner's choices (D-111, of 2026-11-11), on what the session part
// chose where the design and the decisions say nothing, as built, each
// cited where it is made (the session's id and the document's forms, D-111
// (a), (b) and (i), are src/TimelineBuild.h's and src/TimelineDoc.h's;
// docs/TIMELINE_FORMAT.md lists them all):
// - (c) a session plan that can't be read, isn't one or doesn't fit the
//   ladder is a bad argument, exit status 2, as the list-valued setting it
//   is (design §7), its message naming it; and with --fetch-base, a fetch
//   of a URL outside the master's directory, which D-108 (c) refuses
//   without naming a status, exits with status 2, an argument the fetch
//   can't honour, like D-108 (a)'s refusal;
// - (d) a --fetch-base that isn't an http or https URL, a local directory
//   or a file:// URL among them, is refused at the parse, exit status 2, so
//   that network events come from network requests alone, D-108 (a)'s
//   reason, and so is --fetch-base with --fetch off;
// - (e) the session options are recorded with the others only when
//   --session is given, so that a ladder's document is what it was before
//   them;
// - (f) with a session, detection runs on every pair's curve, then on the
//   session's, so that the ladder's attributions are the same with or
//   without one;
// - (g) without --curve, attribute answers every curve, each pair's and
//   then the session's, and --drop K counts the drops in that order;
// - (h) a build that SIGINT or SIGTERM stopped exits with status 1 and
//   writes no document;
// - (j) the text of an answer on a session's curve (answer_text()), the
//   design's P-AD extended: its header with the spans, a switch's two
//   rungs, a request's URL, times, bytes and status.
//
// The owner kept one more of WP09b's rules as built (D-113 (h), of
// 2026-11-12, Q82): --fetch or --fetch-base without --session is refused,
// exit status 2, on the command line or in the config file
// (parse_timeline()), an option that can't act being an argument error, as
// D-111 (d) refuses --fetch-base with --fetch off.
//
// The owner's answers (D-113, of 2026-11-12) on what the decoded messages'
// options and lines chose where the design and the decisions said nothing
// (Q75 to Q77), and of 2026-11-13 on the two readings WP09e took of (a) and
// (b) (D-120 (b), (c)), each built as written here (the order of the
// decoded events' ids and of their warnings, which D-113 (c) keeps as
// built, is src/TimelineBuild.h's):
// - (a) --cue-types and --bad-crc are recorded with the other options,
//   after the margins: both, their defaults included, wherever a playlist
//   the build reads carries an SCTE-35 message (carries_messages()), so
//   that every document whose cues, messages and ad breaks they shaped
//   names them, as threshold and the margins are recorded at their
//   defaults (design §6.3, §8); otherwise each where it is given, on the
//   command line or in the config file, so that a document whose playlists
//   carry none, as D-069's carry plain cue tags only, keeps its bytes (Q75,
//   option 2). A playlist carries one only where the build decodes at least
//   one, whatever --bad-crc then does with it (D-120 (b), Q84 option 2):
//   the two options act on decoded messages alone, --cue-types on their
//   segmentation types and --bad-crc on their CRC_32. A tag whose text
//   isn't base64 or hex, a bare or empty #EXT-OATCLS-SCTE35 among them, and
//   one whose bytes aren't a splice_info_section that decodes carry none,
//   and so do the messages of a variant whose segment map was refused,
//   which the build doesn't decode (D-081 (c), D-086); a message whose
//   CRC_32 fails counts, under --bad-crc keep and under drop, which drops
//   it, so that a document of such messages alone names both options too;
// - (b) --cue-types' value: segmentation_type_ids in hex with 0x, one or
//   two digits each, alone or as a range first-last, comma-separated, the
//   set not empty, with blanks after a comma, so that the default as
//   design §7 writes it, "0x22-0x23, 0x30-0x37, 0x44-0x47", is taken, a
//   config file's value too, which no shell quotes (Q76, option 2). The
//   blanks are a space or a tab, as parse_attributes() and a config file's
//   lines take them, any number of them after a comma, and a blank anywhere
//   else, before a comma, at either end of a command-line value (a config
//   file's value loses its end blanks first, as every setting's does) or
//   within an item, is refused with its item: WP09e's reading of D-113 (b),
//   which the owner kept as built (D-120 (c), Q85 option 1);
// - (c) the record of --cue-types, the set as ranges in that form without
//   blanks, ascending, each run of consecutive types one range
//   ("0x22-0x23,0x30-0x37,0x44-0x47"), and of --bad-crc, "keep" or "drop"
//   (Q77 (a)); the text of a decoded message's events (answer_text(); Q77
//   (b)): the line of a splice_point, a message, a cancel or a cue of a
//   message's own starts with the line and form of the tag that carried it,
//   the form alone for another carrier, a cue_in that joins a cue_out
//   naming that cue's line ("line 16, OATCLS, joining the cue_out at line
//   15"; D-109 (e)); then its command, a splice point's time, the frame it
//   lands on and whether that is outside its tag's segment, a message's
//   segmentation types, what a cancel cancels; and a mark where its CRC_32
//   fails, which a cue also gets, a plain one or a signal's, where a
//   message it holds fails its CRC_32 (D-106 (c), (d), D-109 (a)); and the
//   help's row of an option wider than its column (Q77 (e)), --cue-types
//   with design §7's placeholder: its help in the column on the next line,
//   every other row as it was (option_rows()); the record's form, the text
//   and the row as WP09c built them (Q77, option 1).
//
// Choices the design left open, which the owner's D-099 (c) keeps as they
// are here, and stated in the help: attribute's --report defaults to the
// path the document names, taken from the current directory as mirage_vmaf
// takes its inputs (D-081 (b)); without --curve a query answers every pair's
// curve, and with a session the session's after them (D-111 (g), above);
// drops are numbered from 1 in the order they are listed, the curves in
// turn, or with --curve that curve's alone, which is what --drop counts;
// --auto takes W and N from its own options, not from the document, and
// needs both (D-077); arguments that don't fit the document (a curve, a
// drop, frames or an interval it doesn't have) are bad arguments, exit
// status 2. --frames can't go past the axis's last frame, and so an
// --interval can't hold time past the axis: a frame has to start at or after
// its t0, and its t1 can't be after the axis's end, where the last frame
// ends.
//
// A pair's number (D-092 (b)). --curve pair:<k>, the answers' text and every
// error name a pair as the document does, by the ladder report's own
// pairs[].index, the plan's, which skips the number of a pair that failed:
// --curve's k is mapped to the curve whose variant has that number
// (timeline::curve_of_pair(), src/TimelineDoc.h), and a k that no complete
// pair of the document has is an argument the document doesn't fit, exit
// status 2.
//
// attribute --format json writes a timeline document (D-099 (c)) of the
// same schema whose command is "attribute": the timeline of the document
// queried, its axis, variants, events and warnings as they were read; the
// answer as its attributions, each drop of --auto and --drop with detected
// true, each curve's answer to --interval and --frames with detected false;
// attribute's own options, config file, FFmpeg libraries and margins; the
// ladder report it read, by its path as attribute took it and both its
// hashes; and as detection, --auto's parameters with the curves it ran on,
// the document's record for --drop, or null for an interval.
//
// Exit statuses (the design's P-W), as mirage_vmaf's: 0 written; 1 a
// failure: an input that can't be read or is refused (D-081 (a), (b)), a
// report whose hashes the document doesn't name, a document that wouldn't
// read back, a write that fails, a session fetch that can't run, as while
// another NetworkLogger is installed, or a build that SIGINT or SIGTERM
// stopped, with the status mirage_vmaf --ladder gives a cancelled run
// (D-047) but no document (D-111 (h)), and then no new document at --out;
// 2 bad arguments or a config file that can't be read or has an error, for
// timeline a --cue-types or a --bad-crc it can't take, on the command line
// or in the config file, a cue type that is neither a Start nor an End type
// among them (check_message_options()), a session fetch of local files
// without --fetch-base (D-108 (a)), a session plan that can't be read, has
// an error or doesn't fit the ladder, a --fetch-base that isn't an http or
// https URL or is given with --fetch off, and with --fetch-base a fetch of
// a URL outside the master's directory, which D-108 (c) refuses without
// naming a status (D-111 (c), (d)), and --fetch or --fetch-base without
// --session (D-113 (h)); and for attribute the arguments above that don't
// fit the document. A file the fetch fails to read, a 404 or a body cut
// short, is no failure of the build: it is a failed request, recorded as
// such (D-104 (a)).
// Before anything is read, each sets FFmpeg's log level to warnings and
// initializes its network, as mirage_vmaf does after its own parse (the
// design's P-N).
//
// A consumer (design §3.1): option parsing, the calls and the output. The
// parsers, the help and the text of an answer run without media and are
// unit-tested in tests/test_timeline_cli.cpp, which also runs both
// subcommands as mirage_vmaf does (the design's P-T).

#include "Scte35Events.h"
#include "SessionPlan.h"
#include "TimelineDetect.h"
#include "TimelineDoc.h"
#include "TimelineModel.h"
#include "TimelineQuery.h"
#include "VMAFCli.h"

extern "C" {
#include <libavutil/rational.h>
}

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace timeline {
struct BuildInputs;   // src/TimelineBuild.h
}

namespace timelinecli {

// What reads a --config file: vmafcli::read_config() unless a test gives
// its own.
using ConfigLoader = std::function<vmafcli::ConfigFile(const std::string&)>;

// mirage_vmaf timeline's options.
struct TimelineArgs {
    std::string report;                  // --report: the ladder report to build from
    std::string out;                     // --out: the timeline document to write
    timeline::DetectOptions detect;      // --threshold, --window, --min-points (D-077)
    timeline::QueryOptions  overlap;     // --margin-before, --margin-after (D-078)
    std::string config;                  // --config, as given; empty: none
    bool        help = false;            // -h / --help
    // --session: a scripted session's plan (D-105 (a)); empty: a ladder alone.
    std::string session;
    // --fetch on|off: whether the session's files are fetched (D-104 (a));
    // nullopt: not given, on with a session (design §7).
    std::optional<bool> fetch;
    // --fetch-base: the server the session's files are fetched from, each at
    // its path from the master's directory (D-108 (c)), an http or https URL
    // (D-111 (d)); empty: the report's URLs.
    std::string fetch_base;
    // --cue-types: the segmentation_type_ids that make a time_signal a cue
    // (D-106 (b)), ascending, each once, each a Start or an End type;
    // nullopt: not given, D-106 (b)'s (timeline::kDefaultCueTypes).
    std::optional<std::vector<std::uint8_t>> cue_types;
    // --bad-crc keep|drop: what becomes of a message whose CRC_32 fails
    // (D-106 (c)); nullopt: not given, kept.
    std::optional<timeline::BadCrc> bad_crc;
};

// attribute's one query.
enum class Query : std::uint8_t {
    None,       // none given: a parse refuses it
    Interval,   // --interval <t0>:<t1>, seconds on the axis
    Frames,     // --frames <first>-<last>, reference frames, both included
    Drop,       // --drop K, a detected drop of the document, from 1
    Auto,       // --auto: detection again, with --threshold, --window and --min-points
};

enum class Format : std::uint8_t { Text, Json };

// A curve as --curve names it (design §7): pair:<k>, by the pair's number,
// the ladder report's pairs[].index (D-092 (b)), not by the curve's place in
// Timeline::curves, which answering it finds (timeline::curve_of_pair()); or
// session:<id>, a session's by its id (Session::id), the document's
// sessions[].id, not by its place, which answering it finds too.
struct CurveName {
    timeline::CurveRef::Kind kind = timeline::CurveRef::Kind::Pair;
    int number = 0;                      // pair:<k>: k, the pair's number
    std::string session;                 // session:<id>: the id, not empty
};

// mirage_vmaf attribute's options.
struct AttributeArgs {
    std::string timeline;                // --timeline: the document to query
    std::string report;                  // --report; empty: the path the document names
    Query       query = Query::None;
    AVRational  from = { 0, 1 }, to = { 0, 1 };   // --interval, exact
    std::int64_t first = 0, last = 0;    // --frames
    std::int64_t drop = 0;               // --drop
    std::optional<CurveName> curve;      // --curve pair:<k> or session:<id>; nullopt: every curve,
                                         // each pair's, then each session's
    timeline::DetectOptions detect;      // with --auto (D-077)
    timeline::QueryOptions  overlap;     // --margin-before, --margin-after (D-078)
    Format      format = Format::Text;   // --format
    std::string out;                     // --out; empty: stdout
    std::string config;                  // --config, as given; empty: none
    bool        help = false;            // -h / --help
};

// Parse the arguments after "timeline" (or "attribute") into `out`. Empty
// on success, otherwise what is wrong: an unknown or repeated option, a
// missing value or required option, a bad value, options that can't go
// together, or a config file that can't be read or has an error, named with
// its line. -h / --help anywhere wins over everything else. A failed parse
// leaves `out` as it was.
std::string parse_timeline(const std::vector<std::string_view>& argv, TimelineArgs& out,
                           const ConfigLoader& load = vmafcli::read_config);
std::string parse_attribute(const std::vector<std::string_view>& argv, AttributeArgs& out,
                            const ConfigLoader& load = vmafcli::read_config);

// The --help texts.
std::string timeline_usage();
std::string attribute_usage();

// A number of seconds as the options take it, digits with an optional
// decimal fraction ("2", "0.5", "3.040"), as the exact rational it names,
// so that an interval or a margin ends exactly where it says (the design's
// P-A); nullopt for any other text, and for one whose reduced numerator or
// denominator an AVRational can't hold.
std::optional<AVRational> parse_seconds(std::string_view text);

// --cue-types' value as the set it names (D-106 (b); design §7): items
// separated by ',', each a segmentation_type_id written 0x or 0X and one or
// two hex digits, or a range of them, first-last with first at or before
// last ("0x22-0x23,0x30-0x37,0x44-0x47"); blanks, spaces and tabs, any
// number of them, may follow a comma, as design §7 writes the default,
// "0x22-0x23, 0x30-0x37, 0x44-0x47", the same set (D-113 (b)), and are
// refused anywhere else, before a comma, at either end of the value or
// within an item, as WP09e built it, which the owner kept (D-120 (c),
// above); ascending, each type once. nullopt, with `err` saying which item is
// wrong and why, for any other text, the empty one included. Whether each
// type is a Start or an End type is timeline::check_message_options()'s,
// not this.
std::optional<std::vector<std::uint8_t>> parse_cue_types(std::string_view text, std::string& err);

// The set `types`, ascending and each once, as parse_cue_types() takes it:
// each run of consecutive types a range, a type alone by itself, with two
// upper-case hex digits after 0x, and no blank; what the options record
// writes, WP09c's form, which the owner kept, the record canonical whatever
// blanks the value had (D-113 (b), (c)). D-106 (b)'s set is
// "0x22-0x23,0x30-0x37,0x44-0x47".
std::string cue_types_text(const std::vector<std::uint8_t>& types);

// The message options of `args` for the build (BuildInputs::messages): its
// --cue-types and --bad-crc, D-106's defaults where they aren't given.
timeline::MessageOptions message_options(const TimelineArgs& args);

// What is wrong with an --interval from `from` to `to` seconds (from
// parse_seconds()) on `axis`, a document's, or empty. As --frames can't go
// past the axis's last frame, an interval can't hold time past the axis: a
// frame has to start at or after `from`, so that its first frame (D-081
// (g)) is one of the axis's, and `to` can't be after the axis's end, the
// seconds of one past its last frame (Axis::ticks_at()), which a span to
// the end has; both compared exactly (the design's P-A). On an axis without
// seconds, where every frame is at 0 s, only 0:0 fits, and the text says to
// give frames instead. An interval that doesn't fit is an argument the
// document doesn't fit, exit status 2, as the owner's D-099 (c) keeps such
// arguments.
std::string check_interval(const timeline::Axis& axis, AVRational from, AVRational to);

// The options a document records (DocInfo::options; the design's P-V),
// each option with the value it took, in the table's order: paths as
// given, T a number, W and N integers or null, margins exact as "num/den"
// as the overlap record writes them. timeline's session options, session,
// fetch ("on" or "off") and fetch-base (null when not given), are recorded
// only with a session, so that a ladder's record is as it was before them
// (D-111 (e)); cue-types (cue_types_text()) and bad-crc ("keep" or "drop")
// after the margins, each where it is given, and both, their defaults
// included, where a playlist carries an SCTE-35 message that the build
// decodes (D-113 (a), D-120 (b), above), which the overload below takes:
// this one gives the record of a build whose playlists carry none.
// attribute's also name its query, its curve, "pair:<k>" or
// "session:<id>", or null, its format and the report it read.
timeline::Payload timeline_options(const TimelineArgs& args);
timeline::Payload attribute_options(const AttributeArgs& args, const std::string& report);

// Whether a media playlist that the build of `in` reads, the reference's
// or a complete pair's distorted one, carries an SCTE-35 message that the
// build decodes, whatever --bad-crc then does with it (D-113 (a), D-120
// (b), above): of the messages its tags hand on as CueInputs
// (PlaylistEvents::messages), an #EXT-OATCLS-SCTE35's, an
// EXT-X-DATERANGE's SCTE35-CMD, SCTE35-OUT or SCTE35-IN, a malformed one's
// where the attribute is a well-formed hexadecimal-sequence (D-106 (e)),
// or the SCTE35= of a plain EXT-X-CUE-OUT, EXT-X-CUE-OUT-CONT or
// EXT-X-CUE-IN, one that scte35_events() decodes as the build reads it
// (src/Scte35Events.h): of a variant whose demux pass gave a segment map,
// its text read as base64 or hex by the decoder's rule, its bytes a
// splice_info_section that scte35::decode() decodes (src/Scte35.h), its
// CRC_32 valid or not. A bare or empty #EXT-OATCLS-SCTE35, text that isn't
// base64, a section that doesn't decode, an encrypted one among them, and
// the messages of a variant whose segment map was refused carry none; a
// message that decodes counts wherever it is then placed or not placed. A
// playlist of the ladder that no complete pair scored isn't read, and so
// isn't asked.
bool carries_messages(const timeline::BuildInputs& in);

// timeline_options() of a build whose playlists carry an SCTE-35 message
// that the build decodes where `messages` is true (carries_messages()):
// cue-types and bad-crc are then both recorded, their defaults included,
// whether they are given or not, as D-113 (a) has it; otherwise each where
// it is given, as timeline_options(args) records them.
timeline::Payload timeline_options(const TimelineArgs& args, bool messages);

// What is wrong with a session fetch of `files`, as fetch_files() lists
// them (src/SessionPlan.h), with `fetch_base` the --fetch-base given, or
// empty: without a fetch base, a file whose URL isn't an http or https URL,
// whose fetch NetworkLogger would time by its open alone, refused before
// anything is fetched (D-108 (a)), the text naming the first such file,
// --fetch-base and --fetch off; exit status 2. Empty with a fetch base,
// whose URLs fetch_url() made, and when every URL is http or https.
std::string check_fetch(const std::vector<timeline::Request>& files, const std::string& fetch_base);

// The line mirage_vmaf timeline prints on stderr when it doesn't detect,
// since W and N aren't both given (D-077): which of --window and
// --min-points is missing, and that the document has detection null and no
// attributions. Empty when both are given.
std::string no_detection_note(const timeline::DetectOptions& detect);

// The text of an answer (the design's P-AD), after its header: for each of
// `curves` in turn a line naming the curve by its pair's number (D-092
// (b)), its variant and the reference by entry, URI and both ranks (D-075),
// or a session's by its id, with its name, its spans by entry, segments and
// reference frames, those not scored marked (D-105 (b)), and the reference,
// then each attribution of `answers` on that curve, in order: a drop's
// block (its number, from `numbers`, parallel to `answers`, its reference
// frames and seconds, its points, baseline, minimum, mean and both
// magnitudes, Q27 (c)) or a user's interval's (the same without the drop's
// own numbers), then one line per event, grouped by source in section
// 3.1's order (D-078), each with its variant, segment, offsets in seconds
// and in frames and what its payload says: a span's rung and segments, a
// switch's two rungs, a request's URL, when it was made, how long it took
// to its last byte and to its response's head, its bytes and its status
// (design §6.4); a decoded message's splice_point its tag's line and form,
// its command, its splice time and the frame of its input it lands on, a
// message its command and segmentation types by Table 23's names, a cancel
// what it cancels, a cue of its own its tag's line and form and the line of
// the cue_out a cue_in joins, and each a mark where the message's CRC_32
// fails (D-106 (c)), as a cue that holds a message whose CRC_32 fails gets
// one. Segments and frames are ranges first-last, both included. The
// wording of a session's lines is the one the owner kept (D-111 (j)), and
// so is that of a decoded message's (D-113 (c), above). A curve with no
// answer, which only detection leaves, says "no drop".
std::string answer_text(const timeline::Timeline& timeline, const std::vector<timeline::CurveRef>& curves,
                        const std::vector<timeline::Attribution>& answers,
                        const std::vector<std::int64_t>& numbers);

// The subcommands, as mirage_vmaf runs them: `argv` is what follows the
// subcommand's word. Each returns the exit status and prints as mirage_vmaf
// does: its progress and result on stdout, its warnings and errors on
// stderr.
int run_timeline(const std::vector<std::string_view>& argv);
int run_attribute(const std::vector<std::string_view>& argv);

} // namespace timelinecli
