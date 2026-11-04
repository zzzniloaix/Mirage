#pragma once

// Do the renditions of an HLS stream mark their breaks at the same place?
// An ad break, an interstitial or a chapter is marked in every rendition a
// player combines: by EXT-X-DATERANGE (RFC 8216 §4.3.2.7) at a date, or by
// a packager's own tag in front of the segment the break comes before. A
// player splices each rendition where its own playlist marks it, so one
// marker a segment away from the others puts audio or subtitles a segment
// out of step with the video across the break. The checks:
//  * markers are matched across renditions by their ID;
//  * the break's time is the reference rendition's EXT-X-DATERANGE
//    START-DATE, put on its playlist timeline by EXT-X-PROGRAM-DATE-TIME
//    (else any rendition's DATERANGE, else the reference's marker);
//  * a marker tag should come before the segment whose start is nearest
//    the break, in each rendition;
//  * an interstitial (CLASS="com.apple.hls.interstitial") or a cue
//    (a CUE or SCTE35-OUT / -IN attribute) should start on a segment
//    boundary of each rendition that carries it;
//  * a rendition that carries a marker tag should carry every ID the
//    others mark within its timeline.
// Pure functions, no FFmpeg — unit-tested in tests/test_hls_markers.cpp.

#include "HlsAlign.h"

#include <string>
#include <vector>

namespace hls {

struct MarkerIn {
    std::string   label;            // "v0 1080p", "audio en", "subtitles fr"
    Track         kind = Track::Unknown;
    MediaTimeline tl;
};

// Where one rendition marks an event.
struct MarkerPlace {
    int         rendition = -1;     // index into the input
    std::string tag;                // "EXT-X-DATERANGE", "EXT-X-CUE-OUT", …
    int         line = 0;
    double      at = kNaN;          // its time on the rendition's playlist timeline: a DATERANGE's
                                    // START-DATE, a tag's next segment start; NaN = cannot be placed
    double      expected = kNaN;    // the event's time on that timeline
    double      nearest = kNaN;     // the segment boundary nearest `expected`
    int         nearest_index = -1; // the segment starting there (segments.size(): the end)
    int         before = -1;        // a tag: the segment it comes before
    double      off = kNaN;         // at − nearest
    Severity    sev = Severity::Ok;
};

struct MarkerEvent {
    std::string              id;
    std::string              cls;   // the DATERANGE's CLASS, if one has it
    double                   time = kNaN;   // on the reference's playlist timeline
    std::string              time_from;     // "EXT-X-DATERANGE START-DATE in v0", …
    std::vector<MarkerPlace> places;
    std::vector<int>         missing;       // renditions carrying the tag elsewhere but not this ID
    bool                     splice = false;   // a point players splice at: an interstitial or
                                               // SCTE-35 DATERANGE, or a tag asked for (other
                                               // dates — chapters, programmes — are listed only)
};

struct MarkerReport {
    int                      reference = -1;   // first video rendition, else the first one
    std::vector<MarkerEvent> events;           // by time
    std::vector<Finding>     findings;         // most severe first; renditions = input indices
    int errors = 0, warnings = 0;
    bool any() const { return !events.empty(); }
};

// A marker within kMarkerSlack of a boundary is on it (an audio boundary
// falls on an audio frame, ~21 ms at 48 kHz AAC; ~32 ms for E-AC-3).
inline constexpr double kMarkerSlack = 0.050;

MarkerReport analyze_markers(const std::vector<MarkerIn>& in);

// The report as a JSON object: {"reference": …, "events": [...], "findings": [...]}.
std::string markers_json(const std::vector<MarkerIn>& in, const MarkerReport& r);

// The playlist time of date `d` (Unix seconds) by the segments'
// PROGRAM-DATE-TIME (explicit or carried on): the last segment dated at or
// before it. NaN when no segment before it has a date.
double playlist_time_of_date(const MediaTimeline& tl, double d);

} // namespace hls
