#pragma once

// Scripted sessions: their plans, spans, switches and drops (DECISIONS.md D-068): written by WP07.
//
// The plan as it is read, and each file of the session's fetch as the fetch
// returns it (src/SessionFetch.h), under D-104 and D-105; the session
// itself, its spans on the reference's frames, is the model's (Session and
// Span, src/TimelineModel.h). Core code (eval/phase3-survey/design.md §3.1):
// plain structs, nothing opened or read here.
//
// A session's steps, in the order the timeline build takes them (design
// §3.4, step 6), once build() has placed the ladder's events
// (src/TimelineBuild.h):
//
// - The plan (parse_session_plan()): the file of D-105 (a), version 1 of
//   the layout below, read strictly, every member required and no other, so
//   that a misspelt member is an error rather than a default.
// - The session (make_session()): one Span per span of the plan, on the
//   reference's frames through the segments the build placed (design §5.1):
//   a span runs from where its first segment's `segment` event starts to
//   where its last segment's ends, so that a span of a pair's distorted
//   variant covers the reference frames its segments' frames are paired
//   with, and a span of the reference in top-rung mode its segments' own
//   frames. Its entry must be one of the master's and name a variant of the
//   timeline, the reference or a complete pair's distorted playlist, whose
//   segment map wasn't refused (D-081 (c)), and every segment from its first
//   to its last must be on the timeline; each span must start where the one
//   before it ended, a gap or an overlap being refused with the frames named
//   (D-105 (a)). A span of the reference, the top rung in top-rung mode,
//   isn't scored (D-105 (b)).
// - Its events (session_events()), of the player source (design §5.6): a
//   `variant_span` over each span, and a `variant_switch` at the first frame
//   of each span after the first that plays another entry than the span
//   before it (D-108 (b)), from the rung played before to the one played
//   from there, origin "plan". A rung is named by its master entry and URI
//   with both BANDWIDTH ranks (D-075).
// - Its fetch's files (fetch_files()), in the fetch's order (D-104 (a)):
//   the master; the media playlist of each span's variant, the first time a
//   span plays it; and each span's segments, from its first to its last.
//   URLs are the ladder report's, the master's and the variants' as it
//   gives them, a segment's as its playlist gives it, made absolute against
//   where that playlist was read from; or, given a fetch base, the same
//   paths taken from the master's directory onto it (design §5.7), so that
//   a ladder scored from local files is fetched from a server of those files.
//   src/SessionFetch.h fetches them.
// - Its requests (request_events()), of the network source (design §5.7):
//   one event per fetched file, a redirected one included, its times from
//   its first row's start to its last row's end, its status its last row's,
//   and the chain of rows in its payload (D-104 (a)); placed on the media
//   axis (D-105 (c)): a `segment_request` over its segment's span, a
//   `playlist_request` at the first frame of the first span that plays its
//   variant, the master's at the session's first frame. Its wall clock is its
//   first row's start, in microseconds from the session's start, when its
//   UTC anchor was sampled (D-073).
// - Its curve and its scope (src/TimelineModel.cpp, src/TimelineQuery.cpp):
//   curve_points() of a session's CurveRef gives, over each scored span, the
//   points of the pair whose distorted variant it plays, in the span's
//   frames, so that a coarser pair leaves its gaps, and none over a span that
//   isn't scored (D-068, D-105 (b)); attribute() lists the events of the
//   variant each span plays that meet the part of the interval that span
//   covers, the reference's, and the session's own (D-078).
// - Its drops (session_drops()): the pairs' drops clipped to the spans that
//   played them, under design §6.3's three rules (D-077).
//
// What neither the design nor the decisions before it settled, the owner
// settled in D-108. There is no switch between two spans of one entry
// (D-108 (b); session_events()). And the rest stays as built, each as the
// declaration below says (D-108 (c)): the plan's layout beyond D-105 (a)'s
// spans (parse_session_plan()); a plan refused for a segment the timeline
// doesn't have (make_session()); the payloads' members and units
// (session_events(), request_events()), which
// docs/schema/mirage-timeline-1.schema.json types and docs/TIMELINE_FORMAT.md
// describes; and the rebasing onto a fetch base, a URL outside the master's
// directory refusing the whole fetch (fetch_url()).
//
// The events made here have no id yet: the caller adds them to the
// timeline's events, gives every event its id (assign_ids(), which leaves
// the ladder's events' ids as they were, since it numbers each source, type
// and variant on its own) and builds the index again
// (Timeline::build_index()).

#include "LadderReportRead.h"
#include "TimelineDetect.h"
#include "TimelineModel.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace timeline {

// One span of a session's plan (D-105 (a)): a master entry, by its index
// among the master's entries from 0 as the ladder report numbers them
// (MasterEntry::index, src/LadderReportRead.h), and the first and the last
// media sequence numbers it plays, both included, of the media playlist
// that entry names.
struct PlanSpan {
    std::size_t  entry     = 0;
    std::int64_t first_seq = 0;
    std::int64_t last_seq  = 0;
};

// A scripted session's plan (D-068): a file of its own, versioned JSON,
// recorded by its path and SHA-256, that lists its spans in play order
// (D-105 (a)). The session's spans on the reference's frames come from them
// one for one, each starting where the one before it ended: a plan whose
// spans leave a gap or overlap there is refused, naming the frames.
struct SessionPlan {
    std::string           name;          // as the plan gives it
    std::vector<PlanSpan> spans;         // in play order
};

// One row that NetworkLogger made while the session fetch read a file: one
// HTTP request, or the open of a local file, with the fields of its
// NetRequest (src/NetworkLogger.h) that the session's network events give
// (D-104 (a); design §5.7). Times are microseconds on one steady clock from
// the session's start, the wall clock of its events (D-073); -1: not
// reached.
struct RequestRow {
    std::string  url;                    // the URL requested: a redirect's target after the first row
    std::int64_t t_start      = 0;       // the open began, its connection included; for a later row
                                         // of the same open, a redirect's, its request was sent
    std::int64_t t_sent       = -1;      // the request written
    std::int64_t t_first_byte = -1;      // the response's status line received
    std::int64_t t_end        = -1;      // the body finished, or the request ended
    std::int64_t bytes        = -1;      // the body's bytes read; -1: unknown
    int          status = 0;             // the HTTP status; 0: none, as for a local file
    std::string  status_text;            // "OK", "Not Found", ...
    // NetRequest::failed, which NetworkLogger sets at a failed open, at an
    // error line FFmpeg logs for the request, and at a status of 400 or more.
    // The error text alone doesn't mark a failure: NetworkLogger keeps a
    // reconnect's note there too, without marking the request failed.
    bool         failed = false;
    std::string  error;                  // NetworkLogger's text of a failure or a reconnect;
                                         // may be empty
    // NetRequest::content_length: the response's Content-Length, or a local
    // file's size; -1: unknown. A body cut short reads fewer bytes than it.
    std::int64_t content_length = -1;
    // NetRequest::initiator as net_initiator_name() names it: "playback" for
    // every row of the session fetch (the design's P-N), which keeps those
    // rows alone (src/SessionFetch.h). The event's origin, "fetch", says whose
    // rows they are, so the payload leaves it out.
    std::string  initiator;
};

// What a fetched file is to its session, which places its request on the
// media axis (D-105 (c)): the master, at the session's first frame; a
// variant's media playlist, fetched once, the first time a span plays it
// (D-104 (a)), at that span's first frame; a segment, over its own media
// span.
enum class FetchedFile : std::uint8_t { Master, MediaPlaylist, Segment };

// One file of a session's fetch, in the fetch's order: the master, then for
// each span of the plan its variant's media playlist, the first time a span
// plays it, and the span's segments, one file at a time and unpaced
// (D-104 (a)). Every row NetworkLogger made while the file was fetched is
// kept, in order, a redirect's included: one fetched file makes one event,
// its times from the first row's start to the last row's end and its status
// the last row's, with the chain in its payload (D-104 (a)). Each file is
// tried once, a failure kept as a failed request (D-104 (a)).
struct Request {
    FetchedFile  kind    = FetchedFile::Master;
    int          variant = -1;           // index into Timeline::variants of a media playlist's or a
                                         // segment's variant; -1: the master
    std::optional<std::int64_t> segment; // a segment's media sequence number
    std::string  url;                    // as fetched: from the report's URLs, or the same path
                                         // rebased onto --fetch-base (design §5.7)
    std::vector<RequestRow> chain;       // NetworkLogger's rows of its fetch, in order
    // A segment's EXT-X-BYTERANGE, which is all the fetch reads of its file:
    // its first byte and its length; -1 and -1: the whole file.
    std::int64_t range_offset = -1;
    std::int64_t range_length = -1;
    // The fetch's own failure, in FFmpeg's words (av_strerror) or the
    // fetch's: an open or a read that failed, a body cut short among them,
    // which NetworkLogger doesn't mark failed, or a byte range not read whole;
    // empty: the file was read to its end. A stop requested ends the fetch
    // with the file it interrupts.
    std::string  error;
};

// ── The plan (D-105 (a)) ─────────────────────────────────────────────────────

// What a plan says it is: its "schema" and its "schema_version".
inline constexpr std::string_view kPlanSchema  = "mirage-session-plan";
inline constexpr int              kPlanVersion = 1;

// The plan `text`, version 1: one JSON object (src/JsonRead.h) with these
// four members, each required and no other, in any order:
//   "schema": "mirage-session-plan"
//   "schema_version": 1
//   "name": a string, the session's name (Session::name), which may be empty
//   "spans": its spans in play order, one or more, each an object with three
//            members, each required and no other: "entry", the master entry
//            it plays, by its index among the master's entries from 0, as the
//            ladder report's ladder.entries[].index numbers them; "first_seq"
//            and "last_seq", the media sequence numbers of the first and the
//            last segment it plays, both included: integers from 0, the first
//            no greater than the last.
// D-105 (a) fixes the spans' {entry, first_seq, last_seq} and that the file
// is versioned; the rest of this layout, its top-level members and their
// names, and the strict reading, every member required and no other taken,
// are WP07's, which the owner kept (D-108 (c)).
// For example, entry 1 for segments 0 to 2, entry 2 for 3 and 4, entry 1
// for 5 to 7 (design §9.3):
//   {"schema": "mirage-session-plan", "schema_version": 1, "name": "down at the ad",
//    "spans": [{"entry": 1, "first_seq": 0, "last_seq": 2},
//              {"entry": 2, "first_seq": 3, "last_seq": 4},
//              {"entry": 1, "first_seq": 5, "last_seq": 7}]}
// Whether its entries and segments are the ladder's, and its spans follow
// one another, make_session() checks. On a refusal `err` says why, naming
// the member, and the result is empty; otherwise `err` is empty.
SessionPlan parse_session_plan(std::string_view text, std::string& err);

// ── The session on the reference's frames (D-105 (a), (b)) ───────────────────

// The session of `plan` on `timeline`, a ladder's timeline as build() gives
// it, whose master's entries are `entries` (LadderScores::entries): one Span
// per span of the plan, in its order, as this file's head says, its name
// the plan's. Its id, by which a document and --curve session:<id> name it
// (design §7, §8), is left empty for the caller to give, and its wall clock
// anchor for the fetch's (src/SessionFetch.h). On a refusal `err` says why,
// naming the span by its place in the plan's spans from 0, and the frames,
// entries and segments concerned, and the result is empty; otherwise `err`
// is empty. A span through a segment the timeline doesn't have, one its
// playlist doesn't list or one the build couldn't place, as a segment none
// of whose frames is paired, refuses the whole plan, naming the span and the
// segment, so a session can't play across one such segment: the owner kept
// this check of WP07's (D-108 (c)), which design §14's list of the plan's
// checks, unknown entries and gaps or overlaps, doesn't name.
Session make_session(const Timeline& timeline, const std::vector<MasterEntry>& entries,
                     const SessionPlan& plan, std::string& err);

// ── Its events ───────────────────────────────────────────────────────────────

// The event types made here, of design §5.9's vocabulary.
inline constexpr std::string_view kTypeVariantSpan     = "variant_span";       // player, a span
inline constexpr std::string_view kTypeVariantSwitch   = "variant_switch";     // player
inline constexpr std::string_view kTypeSegmentRequest  = "segment_request";    // network, a span
inline constexpr std::string_view kTypePlaylistRequest = "playlist_request";   // network

// The player events of session `session` of `timeline` (design §5.6), the
// master's entries being `entries`, in play order: a variant_span over each
// span, and before each span after the first that plays another entry than
// the span before it a variant_switch at its first frame (D-108 (b)). Each
// carries the session's index, no segment, and the media key of
// the frame it starts at; a variant_span the variant it plays, a
// variant_switch the variant played from it. Payloads, in order, a rung
// being an object of its entry, its URI as the master gives it, its
// BANDWIDTH and RESOLUTION (null when absent), and the ranks of its
// variant from the top and from the bottom (D-075; null for a variant
// without one):
//   variant_span    entry; uri; bandwidth; resolution; rank_from_top;
//                   rank_from_bottom; first_seq; last_seq; scored: false for
//                   a span of the reference in top-rung mode (D-105 (b))
//   variant_switch  from: the rung of the span before; to: the rung of the
//                   span it starts; origin: "plan"
// No switch is made at a boundary between two spans of one entry, where
// nothing changes, so that an attribution of the session lists no player
// event there; two entries that name one playlist still switch, the
// master's entry being what changes, `from` and `to` then naming the same
// variant (D-108 (b)). These forms, a rung's members flat in a variant_span,
// whose first_seq, last_seq and scored are written always, and as objects
// in a variant_switch, a switch's Event::variant the variant played from it
// (the one design §6.4's prototype lists a switch under), and the members'
// names, are WP07's, which the owner kept (D-108 (c)): the schema file
// types them. None for a session that isn't there.
std::vector<Event> session_events(const Timeline& timeline, const std::vector<MasterEntry>& entries,
                                  int session);

// ── Its fetch (D-104 (a); design §5.7) ───────────────────────────────────────

// Where the session fetch reads `url`, one of the ladder's: `url` itself
// with an empty `fetch_base`; otherwise its part after the directory of
// `master_url` (up to its last '/' before any '?' or '#') added to
// `fetch_base`, with a '/' between them: with the master at /data/ladder/
// master.m3u8 and the base http://127.0.0.1:8080, /data/ladder/v180/
// ad_000.ts is fetched from http://127.0.0.1:8080/v180/ad_000.ts. A URL that
// isn't under that directory can't be rebased: `err` says so, and the result
// is empty; otherwise `err` is empty. This rule is WP07's reading of design
// §5.7's paths rebased onto --fetch-base, which the owner kept (D-108 (c)):
// a ladder whose segments are on another host, or beside the master's
// directory, can't be fetched from a base, fetch_files() refusing its whole
// fetch.
std::string fetch_url(const std::string& url, const std::string& master_url,
                      const std::string& fetch_base, std::string& err);

// The files of session `session`'s fetch, in its order, as this file's head
// says, the rows of none of them yet: the master at `master_url`
// (LadderScores::master_url), each variant's media playlist at its
// Variant::url, and each segment at its `segment` event's url, with its
// byte range where it has one, each through fetch_url() with `fetch_base`.
// On a refusal (a session that isn't there, a segment that isn't on the
// timeline, a URL that can't be rebased) `err` says why and the result is
// empty; otherwise `err` is empty.
std::vector<Request> fetch_files(const Timeline& timeline, int session, const std::string& master_url,
                                 const std::string& fetch_base, std::string& err);

// The network events of `files`, session `session`'s fetch as
// src/SessionFetch.h returns it, and what couldn't be placed.
struct RequestEvents {
    std::vector<Event>       events;     // in the order of the files
    std::vector<std::string> warnings;   // each file whose event couldn't be made, and why
};

// One event per file of `files` (D-104 (a)), placed as this file's head says
// (D-105 (c)), with the session's index, a segment_request its segment's
// variant and media sequence number, a playlist_request its variant, -1 for
// the master's, and its wall clock its first row's start. A file without a
// row, or whose segment, variant or session the timeline doesn't have, makes
// no event and a warning. Times in the payloads are seconds from the
// session's start, null where a row didn't reach them. Payloads, in order:
//   url: as fetched; range: {offset, length} of the bytes fetched, or null
//   for the whole file; status and status_text: the last row's, both null
//   without an HTTP status; bytes: the last row's body bytes, null when
//   unknown; start: the first row's start; first_byte: the last row's
//   status line; end: the last row's end; duration: end minus start;
//   failed: the fetch's own failure (Request::error) or the last row's;
//   error: the fetch's own failure, or null; origin: "fetch"; chain: each
//   row, in order, as {url, start, sent, first_byte, end, status,
//   status_text, bytes, content_length, failed, error}, NetworkLogger's
//   error text of a row null when empty.
// These payloads' members and units are WP07's, which the owner kept
// (D-108 (c)): the schema file types them.
RequestEvents request_events(const Timeline& timeline, int session, const std::vector<Request>& files);

// ── Its drops (D-077; design §6.3) ───────────────────────────────────────────

// The drops of session `session`'s curve: each pair's drops, which detect()
// finds with `options` on the pair's curve (src/TimelineDetect.h), clipped
// to the frames over which the session plays that pair's distorted variant,
// under design §6.3's three rules (D-077):
// - (i) a piece is a drop only if it keeps more than N of the drop's low
//   points, as any drop must;
// - (ii) pieces of two pairs that meet at a switch stay two drops, each
//   with its own pair's numbers;
// - (iii) a piece reports its pair's baseline at its own first point, which
//   is the drop's (D-077: a low point never enters the baseline).
// A piece's interval is its drop's clipped to those frames; its points,
// minimum, mean and magnitudes are those of the drop's low points in it;
// first_point and last_point are positions in the session's curve
// (curve_points()). Consecutive spans of one variant are one stretch of
// frames for the clipping, so that a drop isn't cut where the plan's spans
// meet without a change of curve. Spans that aren't scored have no drops
// (D-105 (b)). In the order of the session's curve. None, and no error,
// when W and N aren't both given (D-077): nothing else is checked then, the
// session and the options included, as detect() checks nothing then.
// Otherwise none, and `err` says why, for a session that isn't there, then
// for an option check_options() refuses, then, span by span in play order,
// for a scored span whose variant has no pair's curve or a curve detect()
// refuses; otherwise `err` is empty.
std::vector<Drop> session_drops(const Timeline& timeline, int session, const DetectOptions& options,
                                std::string& err);

} // namespace timeline
