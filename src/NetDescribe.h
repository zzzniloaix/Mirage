#pragma once

// Pure helpers that turn a recorded NetRequest into what the network
// inspector shows: display name, request kind (manifest / playlist / init /
// segment / ...), media kind (video / audio / subtitles) and the HLS
// rendition it belongs to (e.g. "v9 1080p60"), using the parsed manifest.
// No ImGui / FFmpeg dependency — unit-tested in tests/test_network_logger.cpp.

#include "ManifestParser.h"
#include "NetworkLogger.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

enum class NetKind : std::uint8_t {
    Manifest,   // master playlist / MPD
    Playlist,   // HLS media (variant / rendition) playlist
    Init,       // init segment (EXT-X-MAP / fMP4 header)
    Segment,    // media segment
    Subtitle,   // WebVTT / TTML / SRT payload
    Key,        // decryption key
    Media,      // progressive media file over HTTP
    File,       // local file
    Other,
};
const char* net_kind_name(NetKind k);

enum class NetMedia : std::uint8_t { None, Video, Audio, Subs };

// One playlist the manifest references, with the label shown in the Type column.
struct NetRendition {
    std::string url;      // playlist URL
    std::string dir;      // url up to and including the last '/'
    NetMedia    media = NetMedia::None;
    std::string label;    // "v9 1080p60", "a1 English 2ch", "s1 English"
    int         variant = -1;   // first index into ManifestParser::variants(), video only
};

struct NetCatalog {
    std::string               manifest_url;
    std::vector<NetRendition> renditions;
    // The media and init segments the renditions' playlists list, by URL
    // without query: learned from the playlist responses (learn()). Where
    // directories say nothing — every playlist next to the master, the
    // segments of all renditions in one directory (Unified Streaming's
    // "hls/") — this is how a segment request finds its rendition.
    std::unordered_map<std::string, int> listed;    // URL → index into renditions
    std::unordered_set<std::string>      inits;     // EXT-X-MAPs: "URL@offset" (a byte range of
                                                    // the media file can be one: bipbop's main.mp4)
    std::uint64_t learned = 0;   // bumped whenever `listed` gains an entry

    static NetCatalog build(const std::string& manifest_url,
                            const std::vector<VariantStream>& variants,
                            const std::vector<MediaRendition>& media);
    // `url` against the playlist URLs, then the segments they list, then
    // (longest prefix) the playlist directories.
    const NetRendition* match(const std::string& url) const;
    // Whether a playlist lists `url` (any byte range of it) / lists the byte
    // range from `range_start` (-1: the whole file) as an init segment.
    bool is_listed(const std::string& url) const;
    bool is_init(const std::string& url, std::int64_t range_start) const;
    // `text`, the response to playlist `playlist_url`: the segments it
    // lists belong to that playlist's rendition (the first playlist to list
    // one keeps it). False if nothing new was learned.
    bool learn(const std::string& playlist_url, const std::string& text);
};

struct NetDesc {
    NetKind     kind  = NetKind::Other;
    NetMedia    media = NetMedia::None;
    std::string rendition;   // label from NetCatalog, may be empty
};

NetDesc net_describe(const NetRequest& r, const NetCatalog& cat);

// "https://h/a/b/seg-1.m4s?tok=1" → "seg-1.m4s"; "/Users/x/movie.mkv" → "movie.mkv";
// "https://h/" → "h".
std::string net_request_name(const std::string& url);

// Human-readable byte count: "721 B", "12.3 kB", "5.61 MB".
std::string net_format_bytes(std::int64_t bytes);
// Duration in µs: "84 ms", "6.02 s", "1:04 min".
std::string net_format_duration_us(std::int64_t us);
