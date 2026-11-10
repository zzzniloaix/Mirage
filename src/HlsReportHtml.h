#pragma once

// The HLS check's report as one HTML page to read in a browser: a verdict,
// the variants against what they declare, the findings by severity, the
// break markers of every rendition and the segments read. The page is self-
// contained, styles and script inline, no request anywhere: it opens from
// disk, offline, and can be attached to a ticket as it is. The report is
// embedded as the JSON mirage_hlscheck --json writes; the page's script
// lays it out. No FFmpeg here — unit-tested in tests/test_hls_report_html.cpp.

#include "HlsMarkers.h"

#include <string>
#include <string_view>
#include <vector>

namespace hls {

// A conformance_json() report with the marker check as one more member,
// "markers", when there are markers: the reports of streams without stay
// as they were. What mirage_hlscheck --json writes.
std::string with_markers(std::string conformance_json, const std::vector<MarkerIn>& in, const MarkerReport& r);

// The run, for report_html: how long it took, whether it was the marker
// check only, the tags asked for, and when (UTC, now).
std::string run_meta_json(double seconds, bool markers_only, const std::vector<std::string>& marker_tags);

// `report_json`: a conformance_json() object, with its "markers" member when
// there are markers. `meta_json`: a JSON object about the run, e.g.
// {"seconds": 1.2, "markers_only": false, "marker_tags": ["EXT-X-CUE-OUT"]}.
// Both are embedded as they are, '<' escaped so neither can end the script.
std::string report_html(std::string_view report_json, std::string_view meta_json);

} // namespace hls
