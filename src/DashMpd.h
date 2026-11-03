#pragma once

// An MPEG-DASH MPD (ISO/IEC 23009-1) as DashConform.h checks it: every
// Representation with what it declares — its own attributes, else its
// AdaptationSet's — and every segment it lists, with its URL, byte range,
// media time and MPD time:
//  * SegmentTemplate with a SegmentTimeline ($Time$ or $Number$), or with
//    @duration: the Period's length in a static MPD, the segments available
//    at `now` in a dynamic one;
//  * SegmentList: its SegmentURLs (@media, @mediaRange);
//  * SegmentBase: one file whose Segment Index (the sidx box at
//    @indexRange) lists the subsegments — the caller fetches those bytes,
//    parse_sidx reads them, apply_sidx turns them into segments.
// SegmentTemplate, SegmentList, SegmentBase and BaseURL are inherited from
// the Period and the AdaptationSet, attribute by attribute; the lowest level
// that has one of the three decides which addressing a Representation uses.
// Pure functions, no FFmpeg — unit-tested in tests/test_dash_conform.cpp.

#include "HlsAlign.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace dash {

using hls::ByteRange;
using hls::kNaN;
using hls::Track;

struct Descriptor {                      // EssentialProperty, SupplementalProperty, AudioChannelConfiguration, …
    std::string scheme;                  // @schemeIdUri
    std::string value;                   // @value; ContentProtection: its cenc:default_KID
};

struct SegmentRef {
    std::string  uri;                    // absolute
    ByteRange    range;                  // @mediaRange, or the subsegment's bytes in a SegmentBase file
    std::int64_t number = -1;            // $Number$; -1 = none (SegmentBase)
    std::int64_t time = 0;               // media time of its start, @timescale units (S@t, or nominal)
    std::int64_t duration = 0;           // @timescale units (S@d, @duration, subsegment_duration)
    double       start = 0.0;            // MPD time: Period@start + (time − @presentationTimeOffset) / @timescale
    double       seconds = 0.0;          // duration / @timescale
    int          sap_type = -1;          // the Segment Index's SAP_type of the subsegment's start,
                                         // 0 = it does not start with one; -1 = no index says
};

enum class Addressing : std::uint8_t { None, Timeline, Template, List, Base };
// "SegmentTimeline", "SegmentTemplate@duration", "SegmentList", "SegmentBase", "none".
const char* addressing_name(Addressing a);

struct Representation {
    int          period = 0;             // index into Mpd::periods
    int          set = 0;                // index into Mpd::sets
    std::string  id;                     // @id
    std::int64_t bandwidth = -1;         // @bandwidth; -1 = absent
    std::string  mime_type;              // @mimeType
    std::string  codecs;                 // @codecs as written
    Track        kind = Track::Unknown;  // from @contentType, @mimeType, @codecs
    int          width = 0, height = 0;  // @width, @height; 0 = absent
    std::string  frame_rate_text;        // @frameRate as written ("30000/1001")
    double       frame_rate = 0.0;       // 0 = absent
    int          sar_num = 0, sar_den = 0;   // @sar; 0 = absent
    int          sampling_rate = 0;      // @audioSamplingRate (its first value); 0 = absent
    int          start_with_sap = -1;    // @startWithSAP; -1 = absent
    int          subsegment_sap = -1;    // the AdaptationSet's @subsegmentStartsWithSAP; -1 = absent
    std::vector<Descriptor> essential, supplemental;   // the AdaptationSet's, then its own
    std::vector<Descriptor> channels;    // AudioChannelConfiguration: its own, else the AdaptationSet's
    std::vector<Descriptor> protection;  // ContentProtection: the AdaptationSet's, then its own
    std::string  lang;

    Addressing    addressing = Addressing::None;
    std::uint64_t timescale = 1;
    std::int64_t  pto = 0;               // @presentationTimeOffset, @timescale units
    std::int64_t  duration = 0;          // SegmentTemplate / SegmentList @duration; 0 = none
    std::string   init_uri;              // the Initialization Segment; empty = none
    ByteRange     init_range;
    std::string   index_uri;             // SegmentBase: the file whose @indexRange holds the sidx
    ByteRange     index_range;
    std::vector<SegmentRef>  segments;
    std::vector<std::string> problems;   // what of its addressing cannot be resolved

    double total() const;                // Σ segment seconds
};

struct AdaptationSet {
    int         period = 0;
    std::string id, content_type, mime_type, lang;
    std::string segment_alignment;       // @segmentAlignment as written ("true", "false", a group
    std::string subsegment_alignment;    // number); empty = absent, which means false
    std::vector<int> reps;               // indices into Mpd::reps
    bool aligned() const;                // either one not absent and not "false"
};

struct Period {
    std::string id;
    double start = 0.0;                  // MPD time, seconds
    double duration = kNaN;              // @duration, else to the next Period's start or the MPD's end
};

struct Mpd {
    bool   ok = false;                   // an MPD element with a Period
    bool   dynamic = false;              // @type="dynamic"
    std::string profiles;
    double min_buffer_time = kNaN;       // @minBufferTime, seconds
    double duration = kNaN;              // @mediaPresentationDuration
    double max_segment_duration = kNaN;  // @maxSegmentDuration
    double availability_start = kNaN;    // @availabilityStartTime, Unix seconds
    double time_shift_buffer = kNaN;     // @timeShiftBufferDepth
    double now = kNaN;                   // the wall clock a dynamic MPD's segments were listed for
    std::vector<Period>         periods;
    std::vector<AdaptationSet>  sets;
    std::vector<Representation> reps;    // Period by Period, AdaptationSet by AdaptationSet
    std::vector<std::string>    problems;   // what is wrong with the MPD itself
};

// `text` served from `base_url` (BaseURLs and templates made absolute
// against it). `now`: Unix seconds, for a dynamic MPD whose segments are
// addressed by @duration (they are the ones available then, at most
// kLiveSegments of them).
Mpd parse_mpd(const std::string& text, const std::string& base_url, double now = kNaN);
inline constexpr int kLiveSegments = 60;

// An xs:duration ("PT634.566S", "P1DT2H", "PT1H2M3.5S") in seconds; NaN
// when it is not one, or names years or months (not a fixed length).
double parse_duration(std::string_view s);

// A template (@media, @initialization) with its identifiers substituted:
// $RepresentationID$, $Number$, $Bandwidth$ and $Time$, each with an
// optional %0<width>d, and $$.
std::string expand_template(std::string_view tmpl, const std::string& rep_id, std::int64_t number,
                            std::int64_t bandwidth, std::int64_t time);

// ── Segment Index (ISO/IEC 14496-12 sidx) ────────────────────────────────────

struct Sidx {
    bool          ok = false;
    std::uint32_t timescale = 0;
    std::int64_t  earliest = 0;          // earliest_presentation_time of the first box
    struct Ref {
        std::int64_t  offset = 0;        // absolute byte offset in the file
        std::int64_t  size = 0;          // referenced_size
        std::int64_t  time = 0;          // its earliest presentation time, timescale units
        std::uint32_t duration = 0;      // subsegment_duration
        bool          index = false;     // reference_type 1: another sidx, not media
        bool          sap = false;       // starts_with_SAP
        int           sap_type = 0;
    };
    std::vector<Ref> refs;
    std::string   error;
};
// The sidx boxes in `data`: the bytes of @indexRange, which start at byte
// `offset` of the file.
Sidx parse_sidx(const std::uint8_t* data, std::size_t n, std::int64_t offset);
// The Representation's segments from its Segment Index, one per
// subsegment (MPD times from `period_start`).
void apply_sidx(Representation& r, const Sidx& s, double period_start);

} // namespace dash
