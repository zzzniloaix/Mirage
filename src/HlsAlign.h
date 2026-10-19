#pragma once

// Are the HLS renditions a player combines aligned? The checks a video
// engineer otherwise does by hand when a stream plays black, stalls at a
// boundary or drifts out of lip sync:
//  * playlist level — each media playlist's segment timeline (EXTINF sums,
//    media / discontinuity sequence, discontinuities, PROGRAM-DATE-TIME,
//    ENDLIST), rendition against rendition;
//  * media level — the start timestamps inside the segments (fMP4 tfdt +
//    trun, MPEG-TS PES PTS, packed audio's ID3 timestamp, WebVTT
//    X-TIMESTAMP-MAP) against where the playlist puts those segments.
// Pure functions, no FFmpeg / ImGui — unit-tested in tests/test_hls_align.cpp.

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <string>
#include <vector>

namespace hls {

inline constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// ── Media playlist, in full ──────────────────────────────────────────────────

struct ByteRange {
    std::int64_t offset = -1;   // -1: the whole resource
    std::int64_t length = -1;
    bool whole() const { return offset < 0; }
};

struct Segment {
    std::int64_t seq      = 0;      // media sequence number
    double       start    = 0.0;    // playlist time: Σ EXTINF of the segments listed before it
    double       duration = 0.0;    // EXTINF
    std::string  uri;               // absolute
    ByteRange    range;             // EXT-X-BYTERANGE
    bool         discontinuity = false;   // EXT-X-DISCONTINUITY before it
    std::int64_t disc_seq = 0;      // discontinuity sequence number of its period
    double       pdt = kNaN;        // EXT-X-PROGRAM-DATE-TIME, Unix seconds (explicit or carried on)
    bool         pdt_explicit = false;
    bool         gap = false;       // EXT-X-GAP
    int          map = -1;          // EXT-X-MAP in effect: index into MediaTimeline::maps
    std::string  key;               // EXT-X-KEY METHOD in effect ("" = none listed)
    int          line = 0;          // 1-based line of its URI in the playlist
};

struct InitMap {
    std::string uri;                // absolute
    ByteRange   range;
};

struct MediaTimeline {
    bool          ok = false;       // a media playlist with at least one segment
    int           version = 0;
    double        target = 0.0;     // EXT-X-TARGETDURATION
    std::int64_t  media_seq = 0;    // EXT-X-MEDIA-SEQUENCE
    std::int64_t  disc_seq  = 0;    // EXT-X-DISCONTINUITY-SEQUENCE
    std::string   type;             // EXT-X-PLAYLIST-TYPE: "VOD", "EVENT" or ""
    bool          endlist = false;
    bool          independent = false;
    double        total = 0.0;      // Σ EXTINF
    int           discontinuities = 0;   // EXT-X-DISCONTINUITY tags
    std::vector<Segment> segments;
    std::vector<InitMap> maps;
    std::vector<std::string> problems;   // spec violations found while parsing

    bool live() const { return !endlist && type != "VOD"; }
    bool has_pdt() const;
    // Segment whose [start, start + duration) holds playlist time t; -1 = none.
    int  segment_at(double t) const;
    // Segment with media sequence number `seq`; -1 = none.
    int  index_of(std::int64_t seq) const;
};

// `text` as a media playlist served from `base_url` (URIs made absolute
// against it). ok = false for a master playlist or text that is not a playlist.
MediaTimeline parse_media_timeline(const std::string& text, const std::string& base_url);

// RFC 3986 §5.2 reference resolution (dot segments removed), the way FFmpeg
// makes playlist URIs absolute. Local paths resolve against their directory.
std::string resolve_uri(const std::string& base, const std::string& ref);

// ISO 8601 date-time ("2024-03-01T12:00:00.000Z", "+08:00" offsets) as Unix
// seconds; NaN if it does not parse.
double parse_iso8601(const std::string& s);

// ── Segment start timestamps ─────────────────────────────────────────────────

enum class Track : std::uint8_t { Unknown, Video, Audio, Text, Data };
const char* track_name(Track t);

// An fMP4 init segment (moov): what a media segment's traf boxes refer to.
struct InitInfo {
    struct T {
        std::uint32_t id = 0;
        Track         kind = Track::Unknown;
        std::uint32_t timescale = 0;         // mdhd
        std::string   codec;                 // sample entry ("avc1", "mp4a", …; "encv" resolved)
        std::int64_t  edit_shift = 0;        // what FFmpeg subtracts from tfdt: elst media_time
                                             // minus a leading empty edit, track timescale
        std::uint32_t default_duration = 0;  // trex
    };
    bool              ok = false;
    std::uint32_t     movie_timescale = 0;   // mvhd
    std::vector<T>    tracks;
    const T* track(std::uint32_t id) const;
};
InitInfo parse_init_segment(const std::uint8_t* data, std::size_t n);

struct SegmentStart {
    enum class Format : std::uint8_t { Unknown, Fmp4, Ts, PackedAudio, WebVtt };
    struct T {
        Track         kind  = Track::Unknown;
        std::uint32_t id    = 0;         // fMP4 track_ID / TS PID
        double        start = kNaN;      // earliest presentation time, seconds — as FFmpeg reports it
        double        decode_start = kNaN;
        double        local_zero = kNaN; // WebVTT: media time of cue time 00:00:00.000
        std::string   codec;
        std::string   note;              // "edit list −44 ms", "no init segment", …
    };
    Format          format = Format::Unknown;
    std::vector<T>  tracks;
    std::uint32_t   fragment_seq = 0;    // fMP4 mfhd sequence_number
    std::string     error;               // why no time could be read

    bool   ok() const;                   // some track has a time
    double start(Track kind) const;      // earliest start among tracks of that kind; NaN
    const T* first(Track kind) const;    // first track of that kind with a time
};
const char* format_name(SegmentStart::Format f);

// The top-level ISO BMFF boxes of `data` with their sizes, and the children
// of moov / moof / traf: "styp 24 B · moof 6.1 kB (mfhd traf) · mdat 5.87 MB
// (26.0 kB here)". Empty if it is not ISO BMFF.
std::string describe_boxes(const std::uint8_t* data, std::size_t n);

// Start times from the first bytes of a media segment (the head the network
// inspector keeps is enough). fMP4 needs the rendition's init segment for
// timescales and track kinds; MPEG-TS whose PAT/PMT sit in an EXT-X-MAP needs
// those bytes prepended.
SegmentStart parse_segment_start(const std::uint8_t* data, std::size_t n, const InitInfo* init);

// ── Alignment ────────────────────────────────────────────────────────────────

struct RenditionIn {
    std::string   label;            // "v9 1080p60"
    Track         kind = Track::Unknown;
    bool          in_use = false;   // what is played now (video variant, audio, subtitles)
    MediaTimeline tl;
    std::map<std::int64_t, SegmentStart> starts;   // read from segments, by media sequence number
};

enum class Severity : std::uint8_t { Error, Warning, Info, Ok };
const char* severity_name(Severity s);

struct Finding {
    Severity         sev = Severity::Info;
    std::string      title;         // one line
    std::string      detail;        // the numbers, and what a player shows because of it
    std::vector<int> renditions;    // indices into the input
    double           time = kNaN;   // reference playlist time it is about
};

struct RenditionRow {
    double shift = 0.0;             // add to its playlist times to get the reference's (live: by PDT)
    double offset = kNaN;           // media time − playlist time, first period with a start read
    double offset_vs_ref = kNaN;    // offset − the reference's, same period
    double max_boundary_delta = kNaN;   // largest segment-boundary distance to the reference's
    int    probed = 0;              // segments whose start time was read
    Severity worst = Severity::Ok;  // most severe finding naming it
};

struct AlignReport {
    int                       reference = -1;   // the rendition the others are compared with
    std::vector<Finding>      findings;         // most severe first
    std::vector<RenditionRow> rows;             // parallel to the input
    int errors = 0, warnings = 0;
};

// Thresholds: timestamps within kAlignOk of each other are aligned (audio
// priming, a frame of B-frame delay); beyond kAlignBad a viewer sees it.
inline constexpr double kAlignOk  = 0.100;
inline constexpr double kAlignBad = 0.500;

AlignReport analyze_alignment(const std::vector<RenditionIn>& in);

// Media times from here on are wall-clock: seconds since 1970 (2001-09-09
// on) — live CMAF timestamps such as Unified Streaming's.
inline constexpr double kWallClockMedia = 1e9;

// "1.234 s", "−45 ms" — for findings and the inspector.
std::string format_seconds(double s);
// "1:23.456" playlist / media time; a wall-clock media time (kWallClockMedia)
// as its UTC time of day, "20:52:03.840 UTC".
std::string format_clock(double s);

} // namespace hls
