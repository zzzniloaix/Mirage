#pragma once

// Does an MPEG-DASH MPD declare what its media holds? A player picks a
// Representation by what the MPD says before it downloads anything: a
// @bandwidth too low for the segments stalls on a link the Representation
// seemed to fit, a @codecs level below the stream's lets a device choose
// what it cannot decode, PQ video without its transfer characteristics is
// taken for SDR, a segment that does not start where the timeline puts it
// leaves a gap or an overlap at every switch. Per Representation:
//  * @bandwidth against the segment sizes as ISO/IEC 23009-1 defines it —
//    delivered at @bandwidth bit/s from any segment start, playout begun
//    @minBufferTime after the first bit never runs out (bucket_model);
//  * @codecs against the decoder configuration of the init segment (RFC
//    6381 strings, compared as HlsConform.h compares CODECS);
//  * @width / @height / @sar and @frameRate against the decoded video,
//    @audioSamplingRate and AudioChannelConfiguration against the decoded
//    audio, the cicp TransferCharacteristics descriptor against the
//    stream's transfer characteristics;
//  * @startWithSAP / @subsegmentStartsWithSAP, and the SAP flags of a
//    Segment Index, against the first picture of every segment read;
//  * the timeline: each segment read starts where the MPD puts it (its
//    earliest presentation time against S@t or its @duration slot) and
//    lasts as long; @maxSegmentDuration covers every segment;
//  * @segmentAlignment / @subsegmentAlignment: the same segment starts at
//    the same time in every Representation of the AdaptationSet.
// Pure functions, no FFmpeg — unit-tested in tests/test_dash_conform.cpp;
// DashConformProbe.h gathers the measurements.

#include "DashMpd.h"
#include "HlsConform.h"

#include <array>
#include <optional>

namespace dash {

using hls::Severity;

// ── What the media holds (DashConformProbe fills these) ─────────────────────

struct SegmentSeen {                     // one segment read in full
    hls::SegmentRead read;               // read.index: into Representation::segments
    double start = kNaN;                 // its earliest presentation time on the media timeline,
                                         // seconds (ISO/IEC 14496-12: edit list applied)
    double edit = 0.0;                   // what the init segment's edit list took off, seconds
};

struct RepRead {
    std::string error;                   // its init segment or Segment Index could not be read
    std::string skipped;                 // why its segments were not read (WebM, text, encrypted)
    std::vector<std::int64_t> sizes;     // per segment; -1 = unknown
    std::vector<SegmentSeen>  reads;
    std::optional<hls::SegmentRead> init;   // the init segment alone: what an encrypted
                                            // Representation's sample entries say
};

// ── @bandwidth (ISO/IEC 23009-1: a constant bit rate channel of @bandwidth
//    from any start point; playout @minBufferTime after the first bit) ─────

struct Bucket {
    double need = 0.0;                   // bit/s: below it playout runs dry whatever the order of the
                                         // bytes inside the segments — each segment in by its end
    double safe = 0.0;                   // bit/s: at or above it never does — each segment in by its start
    double buffer = kNaN;                // at the bandwidth given: the @minBufferTime it needs
    int    from = -1, to = -1;           // the run of segments (indices) that falls furthest behind at
                                         // the bandwidth given, else at `need`
    int    sized = 0, total = 0;
    bool complete() const { return total > 0 && sized == total; }
};
// `starts`, `seconds`: the segments' MPD times; `bytes`: their sizes, -1
// unknown (counted as 0: `need` is then a lower bound). Every segment start
// is taken for a start point.
Bucket bucket_model(const std::vector<double>& starts, const std::vector<double>& seconds,
                    const std::vector<std::int64_t>& bytes, double min_buffer, double bandwidth);

// ── Smaller readings ─────────────────────────────────────────────────────────

// The channels an AudioChannelConfiguration says: the ISO/IEC 23003-3
// scheme's count, a CICP ChannelConfiguration (ISO/IEC 23091-3) index, a
// Dolby channel mask; -1 = a scheme or value not read here.
int channel_count(const Descriptor& d);

// The SAP type a segment read starts with (ISO/IEC 14496-12 Annex I): an
// IDR is 1, or 2 when leading pictures are presented before it; a CRA or
// BLA 1, or 3 with RASL pictures (2 with RADL ones only); an H.264 recovery
// point 1, or 3 with leading pictures; audio 1; 0 = not a random access
// point (P / B); -1 = cannot be told (an I picture with neither).
int sap_type(const hls::SegmentRead& s);

// ── The verdict ──────────────────────────────────────────────────────────────

enum class Check : std::uint8_t {
    Bandwidth, Codecs, Resolution, FrameRate, Audio, Colour, Sap, Timeline, Alignment, Count
};
const char* check_name(Check c);         // "@bandwidth", "@codecs", "@width/@height", …

struct RepRow {
    std::array<hls::Cell, static_cast<std::size_t>(Check::Count)> cells;
    Bucket   bucket;
    int      read = 0;                   // segments read
    Severity worst = Severity::Ok;
    const hls::Cell& cell(Check c) const { return cells[static_cast<std::size_t>(c)]; }
    hls::Cell&       cell(Check c)       { return cells[static_cast<std::size_t>(c)]; }
};

struct Report {
    std::vector<hls::Finding> findings;  // most severe first; renditions = Representation indices
    std::vector<RepRow>       rows;      // parallel to Mpd::reps
    int errors = 0, warnings = 0;
};

// Thresholds, the owner's to set. A segment's earliest presentation time
// may sit kTimeSlack off where its timeline puts it (kTimeBad: an error);
// addressed by @duration, a quarter of the duration is noted and half of it
// is a warning (players compute the segment number from the time). Its
// media may run kTimeSlack past its duration. The same segment of two
// aligned Representations may start kAlignSlack apart (kAlignBad: an
// error). A @bandwidth below Bucket::need is an error when short by more
// than hls::kPeakSlack of it, a note within it (as the HLS check takes a
// BANDWIDTH under the peak); one above hls::kOverDeclared × Bucket::safe is
// noted.
inline constexpr double kTimeSlack  = 0.050;
inline constexpr double kTimeBad    = 0.500;
inline constexpr double kAlignSlack = 0.100;
inline constexpr double kAlignBad   = 0.500;

// `reads`: parallel to m.reps.
Report analyze_conformance(const Mpd& m, const std::vector<RepRead>& reads);

// The report as JSON: the MPD's timing, then per Representation what it
// declares, what was measured and the checks, the findings. `source`: the
// MPD's URL as given.
std::string conformance_json(const Mpd& m, const std::vector<RepRead>& reads, const Report& r,
                             const std::string& source);

// "v1080 (video, 1920×1080)": its @id, kind and size.
std::string rep_label(const Mpd& m, int rep);

} // namespace dash
