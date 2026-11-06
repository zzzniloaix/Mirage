#pragma once

// Scripted sessions: their plans, spans, switches and drops (DECISIONS.md D-068): written by WP07.
//
// The plan as it is read, and each file of the session's fetch as the fetch
// returns it (src/SessionFetch.h), under D-104 and D-105; the session
// itself, its spans on the reference's frames, is the model's (Session and
// Span, src/TimelineModel.h). Core code (eval/phase3-survey/design.md §3.1):
// plain structs, nothing opened or read here.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
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
};

} // namespace timeline
