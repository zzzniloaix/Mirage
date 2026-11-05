#pragma once

// mirage_vmaf's timeline and attribute subcommands (DECISIONS.md D-079): written by WP09a.
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
//   both given", as the owner's D-099 (c) keeps it.
// - attribute (design §3.5) reads a timeline document and the ladder report
//   it names, checks the report's SHA-256, or for a redacted copy its
//   scores digest (D-072), takes the pairs' curves from it, and answers one
//   query without reading media: the events that meet an interval given in
//   seconds on the axis (--interval) or in reference frames (--frames), or a
//   detected drop of the document (--drop), or each drop that detection
//   finds again with the parameters given (--auto); as text by default (the
//   design's P-AD), or as a timeline document (--format json).
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
//
// Defaults are the decisions' only: T = 6, W and N none (D-077), margins
// none (D-078), text (D-079). Choices the design left open, which the
// owner's D-099 (c) keeps as they are here, and stated in the help:
// attribute's --report defaults to the path the document names, taken from
// the current directory as mirage_vmaf takes its inputs (D-081 (b));
// without --curve a query answers every pair's curve; drops are numbered
// from 1 in the order they are listed, the curves in turn, or with --curve
// that curve's alone, which is what --drop counts; --auto takes W and N
// from its own options, not from the document, and needs both (D-077);
// arguments that don't fit the document (a curve, a drop, frames or an
// interval it doesn't have) are bad arguments, exit status 2. --frames
// can't go past the axis's last frame, and so an --interval can't hold
// time past the axis: a frame has to start at or after its t0, and its t1
// can't be after the axis's end, where the last frame ends.
//
// Exit statuses (the design's P-W), as mirage_vmaf's: 0 written; 1 a
// failure: an input that can't be read or is refused (D-081 (a), (b)), a
// report whose hashes the document doesn't name, a document that wouldn't
// read back, or a write that fails, and then no new document at --out;
// 2 bad arguments or a config file that can't be read or has an error, and
// for attribute the arguments above that don't fit the document.
// Before anything is read, each sets FFmpeg's log level to warnings and
// initializes its network, as mirage_vmaf does after its own parse (the
// design's P-N).
//
// A consumer (design §3.1): option parsing, the calls and the output. The
// parsers, the help and the text of an answer run without media and are
// unit-tested in tests/test_timeline_cli.cpp, which also runs both
// subcommands as mirage_vmaf does (the design's P-T).

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

// mirage_vmaf attribute's options.
struct AttributeArgs {
    std::string timeline;                // --timeline: the document to query
    std::string report;                  // --report; empty: the path the document names
    Query       query = Query::None;
    AVRational  from = { 0, 1 }, to = { 0, 1 };   // --interval, exact
    std::int64_t first = 0, last = 0;    // --frames
    std::int64_t drop = 0;               // --drop
    std::optional<timeline::CurveRef> curve;   // --curve pair:<k>; nullopt: every pair's
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
// as the overlap record writes them. attribute's also name its query, its
// curve or null, its format and the report it read.
timeline::Payload timeline_options(const TimelineArgs& args);
timeline::Payload attribute_options(const AttributeArgs& args, const std::string& report);

// The line mirage_vmaf timeline prints on stderr when it doesn't detect,
// since W and N aren't both given (D-077): which of --window and
// --min-points is missing, and that the document has detection null and no
// attributions. Empty when both are given.
std::string no_detection_note(const timeline::DetectOptions& detect);

// The text of an answer (the design's P-AD), after its header: for each of
// `curves` in turn a line naming the curve, its variant and the reference
// by entry, URI and both ranks (D-075), then each attribution of
// `answers` on that curve, in order: a drop's block (its number, from
// `numbers`, parallel to `answers`, its reference frames and seconds, its
// points, baseline, minimum, mean and both magnitudes, Q27 (c)) or a
// user's interval's (the same without the drop's own numbers), then one
// line per event, grouped by source in section 3.1's order (D-078), each
// with its variant, segment, offsets in seconds and in frames and what its
// payload says. A curve with no answer, which only detection leaves, says
// "no drop".
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
