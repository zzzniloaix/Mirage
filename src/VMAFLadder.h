#pragma once

// Ladder scoring in mirage_vmaf (work order sections 1.2 and 2; DECISIONS.md
// D-029, D-043 to D-047): the entries of an HLS master playlist, the pairs a
// reference mode scores, and their scoring by a pool of workers, each pair as
// mirage_vmaf scores a pair given with --ref and --dis.
//
// The rules. D-029 decides the design: top-rung by default (D-002), a
// mezzanine optionally; each video playlist scored once; entries without
// video skipped and listed as skipped; one failed pair fails the run; one
// report per reference mode, with a pairs list (src/VMAFReport.h). Where it
// leaves a value open, the value here is the one named below, recorded for
// the owner's confirmation in DECISIONS.md P-050.
//
// - Entries. The master's EXT-X-STREAM-INF tags, each with the URI line that
//   follows it, blank lines, comments and other tags between them allowed,
//   as FFmpeg's hls demuxer pairs them; each line without the white space
//   around it, as eval/streams/fetch_streams.py reads it. The first line is
//   #EXTM3U, as FFmpeg's hls demuxer requires. I-frame playlists
//   (EXT-X-I-FRAME-STREAM-INF) and renditions (EXT-X-MEDIA) are no entries.
//   Every entry needs a BANDWIDTH, RFC 8216's decimal-integer, since the top
//   rung is chosen by it.
// - URIs are resolved against where the master was read from, after HTTP
//   redirects, as FFmpeg's hls demuxer resolves them; a URI in a form
//   resolve() doesn't resolve makes the master one this doesn't read.
// - Video. An entry has video when it has a RESOLUTION, or a CODECS entry
//   whose sample entry name is a video codec's (kVideoCodecs): the rule of
//   eval/streams/fetch_streams.py, whose committed records say which entries
//   of the repository's streams have video. The others are skipped, without
//   being opened, and listed with the reason kNoVideo.
// - Playlists. Entries whose URLs are the same string name one playlist,
//   which is scored once; each pair lists the video entries that name it.
// - The top rung is the playlist of the video entry with the highest
//   BANDWIDTH; of two or more such entries naming different playlists, the
//   first in the master's order, with a warning.
// - The pairs, in the master's order of their first entry: in top-rung mode
//   every video playlist but the top rung's, against it; in mezzanine mode
//   every video playlist, the top rung's included, against the mezzanine.
// - Scoring. A pool of workers (D-043): each takes the next pair in the
//   plan's order and scores it with the options of the run, opening its two
//   inputs anew, as a pair given on the command line does; nothing within a
//   pair depends on the pool. By default as many workers as the machine's
//   physical cores minus one (D-044). With a memory limit, a worker starts a
//   pair only when no running pair is warming up (kWarmUp), and only while
//   the memory before the run, plus what a pair uses as measured then and
//   kMarginPercent more for each running pair and the new one, stays under
//   it (admits). Running pairs aren't stopped, and with none running a pair
//   always starts (D-045, D-051, D-053).
// - The end (D-047). A cancel, or the first pair whose analysis is
//   incomplete (vmafreport::incomplete) or whose settings aren't those of the
//   first pair to complete, which only a model file that changes during the
//   run can cause, stops the pairs still running and starts no other. The
//   result keeps every pair's state in the plan's order; the report lists
//   the complete pairs and the others with their state.

#include "VMAFAnalyzer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <stop_token>
#include <string_view>
#include <utility>
#include <vector>

namespace vmafladder {

// The CODECS sample entry names (the part before the first '.') of video
// codecs, RFC 8216bis's, as eval/streams/fetch_streams.py lists them.
constexpr std::array<std::string_view, 10> kVideoCodecs = {
    "avc1", "avc3", "hvc1", "hev1", "dvh1", "dvhe", "av01", "vp09", "vp08", "mp4v",
};

// Why an entry is skipped.
constexpr std::string_view kNoVideo = "no video";

// The reference modes of a ladder run (work order section 1.2).
constexpr std::string_view kTopRung   = "top-rung";
constexpr std::string_view kMezzanine = "mezzanine";

// One EXT-X-STREAM-INF entry of a master playlist.
struct Entry {
    std::size_t index = 0;   // its position among the master's entries, from 0
    std::size_t line  = 0;   // the line of its EXT-X-STREAM-INF, from 1
    std::string uri;         // its URI line, as the master gives it
    std::string url;         // `uri` resolved against the master's location
    int64_t     bandwidth = 0;   // BANDWIDTH
    // Attributes the report names, as the master gives them; nullopt when absent.
    std::optional<std::string> average_bandwidth, resolution, codecs, frame_rate;
    bool        video = false;   // has_video()
};

// A master playlist's entries in its order, or why it can't be read as one.
struct Master {
    std::vector<Entry> entries;
    std::string        error;   // empty = read
};

// The attributes of an HLS attribute list, as eval/streams/fetch_streams.py
// reads them: each match of ([A-Z0-9-]+)=("[^"]*"|[^",]*) from left to
// right, a quoted value without its quotes. A name given twice is listed
// twice; lookups take the last.
std::vector<std::pair<std::string, std::string>> attributes(std::string_view list);

// Whether an entry with these attributes has video: it has a RESOLUTION, or
// one of its CODECS, without the white space around it and up to its first
// '.', is in kVideoCodecs.
bool has_video(const std::optional<std::string>& resolution,
               const std::optional<std::string>& codecs);

// `uri` resolved against `location`, where its playlist was read from. For an
// http(s) location, as FFmpeg resolves a playlist's URIs, in the forms
// hls::absolute_uri() accepts (src/HlsMaster.h); empty for the others: dot
// segments, "//host" references, query- or fragment-only references, schemes
// other than http(s). For any other location, a local path or a URL of
// another protocol: `uri` as it is when it has a scheme or starts with '/',
// else `uri` after the part of `location` up to its last '/'.
std::string resolve(std::string_view location, std::string_view uri);

// The entries of the master playlist `text`, read from `location`.
Master parse_master(std::string_view text, std::string_view location);

// The video playlist a pair scores, and the video entries that name it.
struct Pair {
    std::string              url;
    std::vector<std::size_t> entries;   // Entry::index, in the master's order
};

// An entry that isn't scored, and why (kNoVideo).
struct Skipped {
    std::size_t entry = 0;   // Entry::index
    std::string reason;
};

// What a ladder run scores, in one reference mode.
struct Plan {
    std::string              reference_mode;      // kTopRung | kMezzanine
    std::string              reference_url;       // the top rung's playlist, or the mezzanine
    std::vector<std::size_t> reference_entries;   // the video entries naming the top rung
    std::vector<Pair>        pairs;
    std::vector<Skipped>     skipped;             // in the master's order
    std::vector<std::string> warnings;            // another playlist with the top rung's BANDWIDTH
    std::string              error;               // why nothing can be scored; empty = a plan
};

// The plan for `master`: top-rung mode when `mezzanine` is empty, mezzanine
// mode with it as the reference otherwise. An error when the master has
// one, has no entry with video, or, in top-rung mode, no video playlist but
// the top rung's.
Plan plan(const Master& master, const std::string& mezzanine);

// A master playlist as read.
struct Fetched {
    std::string text;       // its bytes
    std::string sha256;     // of `text`, lowercase hex (filehash::sha256_hex)
    std::string location;   // where it was read from: the URL after HTTP redirects, else as given
    std::string error;      // empty = read
};

// Reads the playlist at `url`, a path or any URL FFmpeg's avio opens, with
// FFmpeg's default options but the analyzer's limits on a network read
// (src/VMAFNet.h, D-036). Stops, with an error, as soon as the text doesn't
// start with "#EXTM3U", and once a stop is requested on `cancel` and the
// read has gone on for 100 ms: a cancel ends a read that waits, as it ends
// the analyzer's, and leaves one that doesn't, as a local file's, to finish,
// so that a run cancelled before it starts still has its plan (D-047).
Fetched read_master(const std::string& url, std::stop_token cancel = {});

// With a memory limit, the frame pairs a pair scores before what it uses is
// taken to show in the process's resident memory (D-045): until a pair has
// scored them, or all of its frame pairs if it has fewer, it is warming up.
inline constexpr std::uint64_t kWarmUp = 30;

// The frame pairs a pair scores before it has warmed up, in a run whose
// pairs score at most `max_frames` (0 = no limit): kWarmUp, or all of them
// when the limit is lower.
constexpr std::uint64_t warm_up(unsigned max_frames)
{
    return max_frames > 0 && max_frames < kWarmUp ? max_frames : kWarmUp;
}

// What the memory limit adds to what a pair uses, in percent (D-051, D-053).
inline constexpr std::uint64_t kMarginPercent = 25;

// The memory limit's rule (D-045, D-051, D-053): whether one more pair may start,
// with `running` pairs running, `warming` of them still warming up, the
// process's resident memory `now` (0 = unknown) and `baseline` when the run
// began. While a pair is warming up no other starts, so that what every
// running pair uses shows in `now`. What a pair uses, `per_pair` (0 = not
// measured yet), is the memory gained since the run began shared out among
// the pairs running, kept at its largest; a pair starts while `baseline`
// plus that and kMarginPercent more for each running pair and the new one
// stays within `limit`. With no limit or none running it always starts, and
// with the memory unknown once no pair is warming up.
bool admits(std::uint64_t limit, std::uint64_t now, std::uint64_t baseline, unsigned running,
            unsigned warming, std::uint64_t& per_pair);

// The pool of a ladder run (D-043 to D-045).
struct Pool {
    unsigned      jobs         = 0;   // pairs scored at a time; 0 = vmafproc::default_jobs()
    std::uint64_t memory_limit = 0;   // bytes of the process's resident memory; 0 = none
    // The process's resident memory in bytes, 0 when unknown; empty =
    // vmafproc::resident_bytes(). Tests give a stand-in.
    std::function<std::uint64_t()> resident;
};

// What became of a pair of the plan.
enum class PairStatus { NotStarted, Complete, Failed, Stopped };

// "not started", "complete", "failed", "stopped".
std::string_view status_name(PairStatus s);

// One pair of the plan in a run.
struct PairRun {
    PairStatus  status = PairStatus::NotStarted;
    VMAFResult  result;            // its analysis, once it ran
    std::string why;               // Failed: why its result can't be reported
    double      started = -1.0;    // seconds from the run's start; -1 = not started
    double      ended   = -1.0;
};

// How a run that scored ended.
constexpr std::string_view kComplete  = "complete";
constexpr std::string_view kCancelled = "cancelled";
constexpr std::string_view kFailed    = "failed";

// A ladder run: the master, the plan, the pairs and how the pool ran them.
struct Result {
    std::string          ladder;    // the master playlist as given
    Fetched              fetched;
    Master               master;
    Plan                 plan;
    std::vector<PairRun> pairs;     // one per plan.pairs, in the plan's order
    std::string          status;    // kComplete | kCancelled | kFailed; empty = nothing scored
    std::string          error;     // why the run isn't complete; empty = complete
    unsigned             jobs = 0;           // the pool's size
    std::uint64_t        memory_limit  = 0;  // 0 = none
    unsigned             max_running   = 0;  // the most pairs scored at once
    std::uint64_t        peak_resident = 0;  // vmafproc::peak_resident_bytes() at the end
    std::uint64_t        pair_memory   = 0;  // with a limit, what a pair was measured to
                                             // use, at its largest (admits); 0 = never (D-053)
    std::vector<std::string> pool_warnings;  // a pair over the memory limit on its own
};

// Called after each pair whose analysis is complete, one call at a time, on
// the worker that scored it: its index in the plan's pairs, their number,
// and its result.
using OnPair = std::function<void(std::size_t index, std::size_t count, const VMAFResult&)>;

// Reads the master playlist `ladder`, plans the run (plan()) and scores its
// pairs with VMAFAnalyzer::analyze() and `opts`, `pool` of them at a time.
// A stop requested on `cancel` ends the run as cancelled; while the master
// is read, it ends a read that waits (read_master). When the master can't
// be read or planned, `status` is empty and `error` says why.
Result score(const std::string& ladder, const std::string& mezzanine, const VMAFOptions& opts,
             const Pool& pool = {}, std::stop_token cancel = {}, const OnPair& on_pair = {});

} // namespace vmafladder
