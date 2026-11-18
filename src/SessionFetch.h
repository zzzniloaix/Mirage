#pragma once

// The session fetch: a scripted session's files timed by NetworkLogger (DECISIONS.md D-104): written by WP07.
//
// The adapter of eval/phase3-survey/design.md §3.1 that reads a session's
// files after scoring (design §3.4, step 6; §5.7): fetch_files()
// (src/SessionPlan.h) lists them in the fetch's order, fetch_session()
// fetches them and returns NetworkLogger's rows of each, and
// request_events() (src/SessionPlan.h) makes their events, so that the core
// is tested on Requests a test builds by hand.
//
// The rules (D-104 (a); the design's P-N):
// - The logger. A NetworkLogger of the fetch's own (src/NetworkLogger.h),
//   compiled into mirage_vmaf unchanged, is installed for the fetch alone
//   and uninstalled after it, which gives FFmpeg's log its default callback
//   back. FFmpeg's log has one callback for the whole process
//   (eval/phase3-survey/sources.md §9), so a fetch while another
//   NetworkLogger is installed is refused rather than taking that one's
//   lines from it.
// - Alone (design §15, R4): fetch_session() must not run while other code
//   of the process does FFmpeg I/O, nor beside another fetch. A file keeps
//   only the rows tagged NetInitiator::Playback, as every row of the fetch's
//   own opens is, so a request that another thread makes meanwhile on a
//   context nothing hooks or tracks, which NetworkLogger tags Other, is left
//   out of every file's rows; but one made while the fetch's logger is
//   installed on a context hooked or tracked as Playback, as the player's
//   demuxer hooks its own (src/Demuxer.cpp), would be taken for the file's,
//   and two fetches at once would take each other's rows, since the check
//   for another installed NetworkLogger and install() aren't one step. The
//   timeline build fetches after its demux passes, with nothing else
//   reading (design §5.7).
// - The order and the pacing: one file at a time, in the order given, each
//   opened once the file before it was read and closed, with no wait
//   between them (Q44 (f) 1): what one sequential client got, as quickly as
//   its server allowed, not a player's fetch pattern (design §5.7).
// - One file: a plain avio_open2() of its own, so a connection of its own
//   (design §5.7), with the analyzer's network limits
//   (vmafnet::set_io_limits(), D-036) and an interrupt callback on the stop
//   token, read to its end and closed, through NetworkLogger's manual
//   tracking (track_open_begin(), track_open_end(), track_close()) as
//   NetInitiator::Playback. A segment's byte range is all that is read of
//   its file: over HTTP through the http protocol's offset and end_offset
//   options, its Range header; from a local file by a seek to its first
//   byte, as src/NetProbe.cpp reads one; at most its length either way.
// - One try (Q44 (c) 1): a file that can't be opened or read is recorded as
//   a failed request (Request::error) and the fetch goes on with the next.
//   NetworkLogger marks a status of 400 or more failed itself, and FFmpeg
//   fails the open there; a body cut short fails the read, which
//   NetworkLogger doesn't mark, so Request::error says it.
// - The rows. Once a file is closed, the rows the logger holds that are
//   tagged NetInitiator::Playback (above) are copied into its Request, in
//   order, and the logger cleared, so that each Request holds its own rows
//   and the logger's limit of NetworkLogger::kMaxRequests rows can't evict
//   a long session's first ones. The logger's poll() runs once a file's
//   read ends, before its close, so that a row's end is where its body's
//   read ended, also for a body cut short, whose row NetworkLogger would
//   otherwise end at its status line (by reading src/NetworkLogger.cpp: a
//   body that ends short of its Content-Length ends at the last time poll()
//   saw its bytes grow). Times are made microseconds from the session's
//   start, the fetch's, taken on NetworkLogger::now_us()'s clock, and the
//   UTC time is sampled at that moment as the session's anchor (D-073).
// - What the rows hold, as NetworkLogger makes them (tests/
//   test_session_fetch.cpp asserts them over HTTP): an HTTP request a row
//   from its open's start, connection included, to its body's end; a
//   redirect a row per request, the first with its 3xx status, the next its
//   target's, from where the first ended. A local file has one row, for its
//   open, which by reading src/NetworkLogger.cpp ends once the file is open:
//   NetworkLogger times no read of a local file, so a fetch of local files
//   measures their opens, and a local file's event would last its open,
//   whatever the file's size, its bytes the bytes read. fetch_session()
//   keeps NetworkLogger's rows as it makes them, a local file's included;
//   the timeline subcommand refuses such a fetch before anything is fetched,
//   a session whose files aren't http or https URLs without --fetch-base
//   exiting with status 2, so that a session's network events come from
//   network requests alone (D-108 (a); src/TimelineCli.h). A ladder scored
//   from local files is timed over HTTP when its files are fetched from a
//   fetch base that serves them (fetch_url(), src/SessionPlan.h).
// - A stop requested on `cancel` interrupts the file being fetched, while
//   its open waits or while its body is read, which is recorded with its
//   error, and ends the fetch.

#include "SessionPlan.h"

#include <stop_token>
#include <string>
#include <vector>

namespace timeline {

// What a session fetch returns.
struct FetchResult {
    // The files given, in their order, each with its rows and its error: all
    // of them, or up to the one a stop interrupted.
    std::vector<Request> files;
    // The UTC time at the fetch's start, the session's anchor (D-073;
    // Session::wallclock_anchor_utc): RFC 3339 to the microsecond with the
    // zone Z, YYYY-MM-DDTHH:MM:SS.ffffffZ, the form the owner kept
    // (D-108 (c)). Empty when the fetch didn't start.
    std::string anchor_utc;
    // Why the fetch didn't try every file: a stop requested, or another
    // NetworkLogger installed; empty when it tried each, failed ones
    // included.
    std::string error;
};

// Fetches `files`, as fetch_files() lists them, by the rules above, with no
// other FFmpeg I/O in the process and no other fetch at the same time.
FetchResult fetch_session(std::vector<Request> files, std::stop_token cancel = {});

} // namespace timeline
