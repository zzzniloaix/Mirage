#pragma once

// Does an HLS master playlist declare what its media holds? Each
// EXT-X-STREAM-INF tells a player what it gets before anything is
// downloaded, and players pick variants by it: a BANDWIDTH below the real
// peak stalls on a link the variant seemed to fit; a CODECS level below the
// stream's lets a device choose a variant it cannot decode; an HDR variant
// without VIDEO-RANGE is taken for SDR. The checks a packaging engineer
// otherwise runs a validator for after every encode:
//  * bit rates — the peak segment bit rate (RFC 8216 §4.1: the largest bit
//    rate of any run of segments lasting 0.5 to 1.5 target durations) of
//    the variant's playlist plus its largest audio rendition, against
//    BANDWIDTH (§4.3.4.2: "the largest sum of peak segment bit rates that is
//    produced by any playable combination of Renditions"); the average
//    segment bit rates likewise against AVERAGE-BANDWIDTH;
//  * codecs — every CODECS format (RFC 6381) against the codec, profile,
//    tier and level in the stream's decoder configuration; every format in
//    the media must be listed (§6.2.4); the audio renditions' CHANNELS
//    against the channels decoded;
//  * picture — RESOLUTION against the decoded size, FRAME-RATE against the
//    frame rate measured in the segments, VIDEO-RANGE against the transfer
//    characteristics (draft-pantos-hls-rfc8216bis-22: SDR for CICP 1, 6,
//    13, 14, 15, HLG for 18, PQ for 16, PQ / HLG also when mixed with SDR;
//    absent means SDR);
//  * segments — the first picture of every segment read (an IDR or other
//    random access point: EXT-X-INDEPENDENT-SEGMENTS says every segment
//    decodes on its own, and RFC 8216 §3 wants an IDR in every H.264
//    segment), its media duration against EXTINF, and one target duration
//    for every variant's playlist (§6.2.4).
// Where segments line up across renditions is the Alignment check's
// (HlsAlign.h). Pure functions, no FFmpeg — unit-tested in
// tests/test_hls_conform.cpp; HlsConformProbe.h gathers the measurements.

#include "HlsAlign.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hls {

// ── What the master declares ─────────────────────────────────────────────────

// An attribute list (RFC 8216 §4.2) as (name, value) pairs in order, a
// quoted-string's quotes removed (its value may hold commas).
std::vector<std::pair<std::string, std::string>> parse_attribute_list(std::string_view list);

struct VariantDecl {                     // one EXT-X-STREAM-INF
    int          line = 0;               // 1-based line of the tag
    std::string  uri;                    // absolute
    std::int64_t bandwidth     = -1;     // BANDWIDTH; -1 = absent
    std::int64_t avg_bandwidth = -1;     // AVERAGE-BANDWIDTH; -1 = absent
    bool         has_codecs = false;
    std::vector<std::string> codecs;     // CODECS split at its commas
    int          width = 0, height = 0;  // RESOLUTION; 0 = absent
    double       frame_rate = 0.0;       // FRAME-RATE; 0 = absent
    std::string  video_range;            // VIDEO-RANGE as written; empty = absent (SDR)
    std::string  audio, video, subtitles;   // GROUP-IDs it names
};

struct MediaDecl {                       // one EXT-X-MEDIA
    int         line = 0;
    std::string type;                    // AUDIO, VIDEO, SUBTITLES, CLOSED-CAPTIONS
    std::string group_id, name, language;
    std::string uri;                     // absolute; empty = carried in the variant's own segments
    std::string channels;                // CHANNELS as written ("2", "16/JOC"); empty = absent
};

struct MasterDecl {
    bool ok = false;                     // a master playlist with an EXT-X-STREAM-INF
    bool independent = false;            // EXT-X-INDEPENDENT-SEGMENTS (for every media playlist)
    std::vector<VariantDecl> variants;   // in master order — ManifestParser::variants()' order
    std::vector<MediaDecl>   media;
    std::vector<std::string> problems;   // what is wrong with the master itself

    // The media playlists to read: every variant's URI, then every AUDIO
    // rendition's, each once, in that order.
    std::vector<std::string> playlists() const;
};

// `text` served from `base_url` (URIs made absolute against it, as
// resolve_uri does). ok = false for a media playlist or other text.
MasterDecl parse_master_decl(const std::string& text, const std::string& base_url);

// ── Codec strings (RFC 6381) ─────────────────────────────────────────────────

struct CodecId {
    std::string   text;                  // as written
    std::string   fourcc;                // "avc1", "hvc1", "av01", "mp4a", "ac-3", "Opus", …
    Track         kind = Track::Unknown;
    bool          ok = false;            // a format known here, its fields parsed
    // Video. AVC: profile_idc, the constraint_set flags byte, level_idc.
    // HEVC: general_profile_space / _profile_idc, the compatibility flags
    // (bit j = general_profile_compatibility_flag[j]), general_tier_flag,
    // general_level_idc (30 × the level). AV1: seq_profile, seq_level_idx_0,
    // seq_tier_0, the bit depth. Dolby Vision: its profile and level.
    int           profile = -1;
    int           level   = -1;
    int           tier    = -1;
    int           constraints = -1;
    int           profile_space = 0;
    std::uint32_t compat = 0;
    int           bit_depth = -1;
    bool          dolby_vision = false;  // dvh1 / dvhe / dvav / dva1 / dav1
    // Audio, mp4a.40.N: the MPEG-4 audio object type (2 AAC-LC, 5 HE-AAC, 29 HE-AAC v2).
    int           object_type = -1;
};
CodecId parse_codec(std::string_view s);

// What a CodecId names: "H.264 High, level 4.0", "HEVC Main 10, Main tier,
// level 4.1", "AV1 Main, level 5.0, 10-bit", "AAC-LC", "AC-3".
std::string describe_codec(const CodecId& c);

// The RFC 6381 string of a video stream built from its decoder
// configuration: the avcC / hvcC / av1C record an MP4 sample entry carries,
// or Annex B parameter sets (MPEG-TS extradata, or an access unit holding
// them). `fourcc`: the sample entry's name; empty = "avc1" / "hvc1". Empty
// when no configuration parses.
std::string avc_codec_string(const std::uint8_t* config, std::size_t n, std::string_view fourcc = {});
std::string hevc_codec_string(const std::uint8_t* config, std::size_t n, std::string_view fourcc = {});
std::string av1_codec_string(const std::uint8_t* config, std::size_t n);

// NAL unit length size of an avcC / hvcC record, 0 = not one (Annex B).
int nal_length_size(const std::uint8_t* config, std::size_t n, bool hevc);

// ── Segment starts ───────────────────────────────────────────────────────────

// The first picture of a segment, from the NAL units of its first access
// unit in decode order.
enum class FirstPicture : std::uint8_t {
    Unknown,     // not H.264 / HEVC, or no slice found
    Idr,         // H.264 IDR; HEVC IDR_W_RADL / IDR_N_LP
    Cra,         // HEVC clean random access: its RASL pictures need the segment before
    Bla,         // HEVC broken link access
    Recovery,    // H.264 non-IDR picture after a recovery point SEI (an open GOP)
    Intra,       // H.264 I slices without either: later pictures may reference across it
    Other,       // P / B (HEVC: any non-IRAP picture): decoding cannot start here
};
const char* first_picture_name(FirstPicture p);
// Random access points a player can start decoding at.
bool is_random_access(FirstPicture p);

// `nal_length`: 1..4 for length-prefixed NAL units (MP4), 0 for Annex B.
FirstPicture first_picture_h264(const std::uint8_t* au, std::size_t n, int nal_length);
FirstPicture first_picture_hevc(const std::uint8_t* au, std::size_t n, int nal_length);

// ── Bit rates (RFC 8216 §4.1) ────────────────────────────────────────────────

struct SegmentBits {
    double       duration = 0.0;         // EXTINF
    std::int64_t bytes    = -1;          // size; -1 = unknown
};

struct Bitrates {
    double peak    = 0.0;                // bit/s; 0 = no segment has a size
    int    peak_at = -1;                 // the window it is over: first segment, count
    int    peak_n  = 0;
    bool   peak_single = false;          // no run fits 0.5–1.5 target durations: one segment's rate
    double average = 0.0;                // Σ bytes × 8 / Σ EXTINF of the segments with a size
    int    sized   = 0;                  // segments with a size
    int    total   = 0;
    bool complete() const { return total > 0 && sized == total; }
};

// A segment without a size breaks a run: the peak is over what is known.
Bitrates segment_bitrates(const std::vector<SegmentBits>& segs, double target);

// ── What the media holds (HlsConformProbe fills these) ───────────────────────

struct VideoSeen {
    std::string codec;                   // FFmpeg's name: "h264", "hevc", "av1", …
    std::string codec_string;            // built from the decoder configuration; empty = unknown
    std::string sample_entry;            // fMP4 sample entry ("avc1", "hev1", …); empty = MPEG-TS
    int width = 0, height = 0;           // decoded size
    int sar_num = 0, sar_den = 0;        // sample aspect ratio; 0 = unknown
    int transfer = 2;                    // CICP TransferCharacteristics; 2 = unspecified
    int bit_depth = 0;
    int dv_profile = -1, dv_level = -1;  // Dolby Vision configuration record; -1 = none
    int dv_compat = -1;                  // its dv_bl_signal_compatibility_id: 0 = no compatible base
                                         // layer (IPT-PQ-c2, profile 5: PQ whatever the VUI says)
};

struct AudioSeen {
    std::string codec;                   // FFmpeg's name
    std::string codec_string;            // "mp4a.40.2", "ac-3", …; empty = unknown
    int channels    = 0;
    int sample_rate = 0;
};

struct SegmentRead {                     // one media segment read in full
    int          index = -1;             // in its playlist
    std::int64_t bytes = -1;
    std::string  error;                  // why nothing was read
    std::optional<VideoSeen> video;
    std::optional<AudioSeen> audio;
    FirstPicture first = FirstPicture::Unknown;
    bool         first_key = false;      // the keyframe flag on its first video packet
    int          frames    = 0;          // video packets
    int          keyframes = 0;
    int          leading   = 0;          // video packets with a pts below the first one's: the
                                         // leading pictures of an open GOP, or of a cut inside a GOP
    double       media_duration = kNaN;  // video: max(pts + duration) − min(pts), seconds
    double       frame_rate = 0.0;       // (frames − 1) / (max pts − min pts); 0 = unknown
};

struct PlaylistRead {                    // one media playlist: a variant's or an audio rendition's
    std::string   uri;
    std::string   error;                 // it could not be fetched or parsed
    MediaTimeline tl;
    bool          target_tag = false;    // it has an EXT-X-TARGETDURATION (tl.target 0 is also none)
    std::vector<std::int64_t> sizes;     // per segment of tl; -1 = unknown
    std::vector<SegmentRead>  reads;     // the segments read
    std::string   key;                   // EXT-X-KEY METHOD of the segments read ("" = none):
                                         // encrypted contents are not read, sizes still count
};

// ── The verdict ──────────────────────────────────────────────────────────────

enum class Check : std::uint8_t {
    Bandwidth, AverageBandwidth, Codecs, Resolution, FrameRate, VideoRange, SegmentStart, Duration,
    Count
};
const char* check_name(Check c);         // "BANDWIDTH", "AVERAGE-BANDWIDTH", "CODECS", …

struct Cell {
    Severity    sev = Severity::Info;
    bool        checked = false;         // false: nothing to compare (not declared, not measured)
    std::string declared, measured;      // short text for a table cell
    std::string note;                    // longer: the numbers behind it
};

struct VariantRow {
    std::array<Cell, static_cast<std::size_t>(Check::Count)> cells;
    double   peak = 0.0, average = 0.0;  // bit/s, the variant's playlist + its largest audio rendition
    bool     rates_complete = false;     // every segment of them has a size
    int      read = 0;                   // segments of its playlist read
    Severity worst = Severity::Ok;
    const Cell& cell(Check c) const { return cells[static_cast<std::size_t>(c)]; }
    Cell&       cell(Check c)       { return cells[static_cast<std::size_t>(c)]; }
};

struct ConformReport {
    std::vector<Finding>    findings;    // most severe first; renditions = variant indices
    std::vector<VariantRow> rows;        // parallel to MasterDecl::variants
    int errors = 0, warnings = 0;
};

// Thresholds. A FRAME-RATE is right when it rounds the measured rate to
// three decimals (RFC 8216 §4.3.4.2); kRateSlack allows the measured
// rate's own error. A segment's media may run kDurationSlack past EXTINF
// before it is reported.
inline constexpr double kRateSlack     = 0.002;
inline constexpr double kDurationSlack = 0.050;
// A BANDWIDTH below the measured peak is an error when it is short by more
// than kPeakSlack of the peak, a note within it: a peak computed over other
// windows than RFC 8216's (per segment, say) lands that close. A BANDWIDTH
// above kOverDeclared × the peak is noted as keeping players off the variant
// longer than needed. AVERAGE-BANDWIDTH is reported when it is off the
// measured average by more than kAverageSlack of it.
inline constexpr double kPeakSlack    = 0.01;
inline constexpr double kOverDeclared = 1.50;
inline constexpr double kAverageSlack = 0.10;

// `reads`: the playlists MasterDecl::playlists() lists, any order (matched by URI).
ConformReport analyze_conformance(const MasterDecl& m, const std::vector<PlaylistRead>& reads);

// The report as JSON: the declarations, the measurements and the findings
// per variant, then every playlist read. `source` is the master's URL as given.
std::string conformance_json(const MasterDecl& m, const std::vector<PlaylistRead>& reads,
                             const ConformReport& r, const std::string& source);

// "2.18 Mb/s", "845 kb/s".
std::string format_bitrate(double bps);

// Ascending indices with runs shortened: "0–7, 9, 12–14".
std::string format_list(const std::vector<int>& v);

} // namespace hls
