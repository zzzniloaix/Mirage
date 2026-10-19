#pragma once

// A copy of an HLS master playlist for playback to open instead of the
// original, listing every variant playlist once.
//
// Masters often list each video playlist once per audio group (Apple's
// bipbop: 8 playlists × 3 groups = 24 EXT-X-STREAM-INF). FFmpeg's hls demuxer
// does not merge entries that share a URI: each gets its own playlist, which
// it fetches and whose first segment it probes, one after another, before
// avformat_open_input returns — two thirds of a bipbop open is duplicates.
//
// The copy keeps the first entry of every URI and, for each audio / video /
// subtitle group the kept entries do not reference, the cheapest entry that
// does: FFmpeg only reads a rendition that belongs to some variant (AVProgram),
// so every rendition stays playable. Every playlist and rendition is still
// exposed, the variant FFmpeg starts on keeps its own audio group, and kept
// entries stay in master order (programs follow it).
//
// The copy is opened as a data: URL, so every URI in it is made absolute
// against where the master was served from. Masters with anything the
// rewrite is unsure about are left alone (see dedupe_master).

#include <optional>
#include <string>
#include <vector>

namespace hls {

struct MasterCopy {
    std::string      text;   // the playlist to open, every URI absolute
    std::vector<int> kept;   // original EXT-X-STREAM-INF index of each entry in it, ascending
};

// `text` (served from `base_url`, after redirects) as a deduplicated copy,
// or nullopt when it should be opened as it is:
//  * base_url is not http(s), or the text is not a master playlist
//    (no EXT-X-STREAM-INF, or media-playlist tags such as EXTINF),
//  * no URI is listed twice (nothing to save),
//  * a tag outside the ones this knows (EXTM3U, VERSION, INDEPENDENT-SEGMENTS,
//    STREAM-INF, I-FRAME-STREAM-INF, MEDIA, SESSION-DATA): e.g. SESSION-KEY,
//    DEFINE, CONTENT-STEERING, START,
//  * a URI it would not resolve exactly as FFmpeg does (dot segments,
//    "//host" references, other schemes, query- / fragment-only references),
//  * an EXT-X-STREAM-INF not followed directly by its URI line.
std::optional<MasterCopy> dedupe_master(const std::string& text, const std::string& base_url);

// `uri` made absolute against http(s) `base` the way FFmpeg resolves
// playlist URIs, for the reference forms dedupe_master accepts; empty for
// the others.
std::string absolute_uri(const std::string& base, const std::string& uri);

} // namespace hls
