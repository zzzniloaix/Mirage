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

    const std::vector<ManifestTag>& tags() const { return tags_; }

    // Returns the discontinuity sequence number active at the given pts.
    int disc_seq_at(double pts) const;

    // Parse from already-fetched text — public so unit tests can drive them directly
    // without needing a network or filesystem.
    [[nodiscard]] bool parse_hls_text(const std::string& text);
    [[nodiscard]] bool parse_dash_text(const std::string& text);

private:
    [[nodiscard]] bool parse_hls(const std::string& url);
    [[nodiscard]] bool parse_dash(const std::string& url);

    // Fetch raw text content via FFmpeg avio. Returns true on success.
    static bool fetch_text(const std::string& url, std::string& out);

    std::vector<ManifestTag> tags_;
};
