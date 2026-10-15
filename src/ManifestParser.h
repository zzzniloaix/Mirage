#pragma once

#include <string>
#include <vector>

// Tag kinds surfaced from HLS and DASH manifests.
enum class ManifestTagKind {
    Discontinuity,           // #EXT-X-DISCONTINUITY
    DiscontinuitySequence,   // #EXT-X-DISCONTINUITY-SEQUENCE:N
    Map,                     // #EXT-X-MAP  (init segment change)
    ProgramDateTime,         // #EXT-X-PROGRAM-DATE-TIME
    CueOut,                  // #EXT-X-CUE-OUT  (ad break start)
    CueIn,                   // #EXT-X-CUE-IN   (ad break end)
    Period,                  // DASH Period boundary
    Event,                   // DASH EventStream/Event
    Unknown,                 // any unrecognised #EXT-X-* tag
};

struct ManifestTag {
    double          pts      = 0.0;               // stream time in seconds
    int             disc_seq = 0;                 // HLS discontinuity sequence number
    ManifestTagKind kind     = ManifestTagKind::Unknown;
    std::string     raw;                          // full original tag line / XML element name
    std::string     value;                        // parsed value / attributes string
};

// One rendition from an HLS EXT-X-STREAM-INF or DASH Representation element.
struct VariantStream {
    std::string url;            // fully-resolved playback URL
    int64_t     bandwidth = 0;  // bits/sec
    int         width     = 0;
    int         height    = 0;
    std::string codecs;
    int64_t     avg_bandwidth = 0;   // HLS AVERAGE-BANDWIDTH, 0 = absent
    double      frame_rate    = 0.0; // HLS FRAME-RATE / DASH frameRate, 0 = absent
    std::string audio_group;         // HLS AUDIO="..." group id
    std::string subtitle_group;      // HLS SUBTITLES="..." group id
};

// One HLS EXT-X-MEDIA alternative rendition (audio / subtitles / CC).
struct MediaRendition {
    std::string type;        // "AUDIO" | "SUBTITLES" | "CLOSED-CAPTIONS" | "VIDEO"
    std::string group_id;
    std::string name;
    std::string language;
    std::string channels;    // AUDIO only, e.g. "2", "6"
    std::string url;         // fully-resolved playlist URL; empty for in-band (CC)
    bool        is_default = false;
};

// Fetches and parses HLS (.m3u8) or DASH (.mpd) manifests.
//
// Call is_manifest() first to check if the URL looks like a manifest.
// Then call parse(); on success, tags() returns all extracted events.
class ManifestParser {
public:
    ManifestParser() = default;

    // Returns true if the URL looks like an HLS or DASH manifest.
    static bool is_manifest(const std::string& url);

    // Fetch and parse the manifest at the given URL or local path.
    // Returns true on success.
    [[nodiscard]] bool parse(const std::string& url);

    const std::vector<ManifestTag>&    tags()     const { return tags_; }
    const std::vector<VariantStream>&  variants() const { return variants_; }
    const std::vector<MediaRendition>& renditions() const { return renditions_; }

    // Returns the discontinuity sequence number active at the given pts.
    int disc_seq_at(double pts) const;

    // Parse from already-fetched text — public so unit tests can drive them directly
    // without needing a network or filesystem.
    [[nodiscard]] bool parse_hls_text(const std::string& text,
                                      const std::string& base_url = {});
    [[nodiscard]] bool parse_dash_text(const std::string& text,
                                       const std::string& base_url = {});

private:
    [[nodiscard]] bool parse_hls(const std::string& url);
    [[nodiscard]] bool parse_dash(const std::string& url);

    // Fetch raw text content via FFmpeg avio. Returns true on success.
    static bool fetch_text(const std::string& url, std::string& out);

    // Resolve a possibly-relative variant URL against a base manifest URL.
    static std::string resolve_url(const std::string& base_url,
                                   const std::string& variant_url);

    std::vector<ManifestTag>   tags_;
    std::vector<VariantStream> variants_;
    std::vector<MediaRendition> renditions_;
};
