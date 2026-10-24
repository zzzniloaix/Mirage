#pragma once

// Ladder scoring in mirage_vmaf (work order section 1.2; DECISIONS.md D-029):
// the entries of an HLS master playlist, the pairs a reference mode scores,
// and their scoring one pair after another, each as mirage_vmaf scores a pair
// given with --ref and --dis.
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
// - Scoring. One pair after another, with the options of the run; each pair
//   opens its two inputs anew, as a pair given on the command line does. The
//   first pair whose analysis is incomplete (vmafreport::incomplete) ends the
//   run without a report, as P-006 has it for a pair, and so does a pair
//   scored with other settings than the first, which only a model file that
//   changes during the run can cause.

#include "VMAFAnalyzer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
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
// FFmpeg's default options, as the analyzer opens its inputs. Stops, with an
// error, as soon as the text doesn't start with "#EXTM3U".
Fetched read_master(const std::string& url);

// A ladder run: the master, the plan and the pairs' results.
struct Result {
    std::string             ladder;    // the master playlist as given
    Fetched                 fetched;
    Master                  master;
    Plan                    plan;
    std::vector<VMAFResult> results;   // of plan.pairs, in order, as far as the run got
    std::string             error;     // why the run gets no report; empty = complete
};

// Called after each pair whose analysis is complete: its index in the plan's
// pairs, their number, and its result.
using OnPair = std::function<void(std::size_t index, std::size_t count, const VMAFResult&)>;

// Reads the master playlist `ladder`, plans the run (plan()) and scores its
// pairs one after another with VMAFAnalyzer::analyze() and `opts`. Stops at
// the first pair whose analysis is incomplete, or whose settings aren't the
// first pair's; its result is then the last of `results`.
Result score(const std::string& ladder, const std::string& mezzanine, const VMAFOptions& opts,
             const OnPair& on_pair = {});

} // namespace vmafladder
