#include "ManifestParser.h"
#include "Logger.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <cstring>
#include <cstdlib>

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/error.h>
}

// pugixml (added via FetchContent)
#include <pugixml.hpp>

// ── Helpers ───────────────────────────────────────────────────────────────────

static std::string str_lower(std::string s)
{
    for (auto& c : s) c = static_cast<char>(std::tolower(c));
    return s;
}

static bool str_ends_with(const std::string& s, const char* suffix)
{
    auto n = strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// Extract the value after the first ':' in a tag line.
static std::string extract_value(const std::string& line)
{
    auto colon = line.find(':');
    return (colon != std::string::npos) ? line.substr(colon + 1) : "";
}

// Parse ISO 8601 duration PT[nH][nM][nS] into seconds.
static double parse_iso_duration(const char* s)
{
    if (!s || s[0] != 'P' || s[1] != 'T') return 0.0;
    double t = 0.0;
    const char* p = s + 2;
    while (*p) {
        char* end;
        double v = std::strtod(p, &end);
        if (end == p) { ++p; continue; }
        char unit = *end;
        if      (unit == 'H') t += v * 3600.0;
        else if (unit == 'M') t += v * 60.0;
        else if (unit == 'S') t += v;
        p = end + 1;
    }
    return t;
}

// ── Public API ────────────────────────────────────────────────────────────────

bool ManifestParser::is_manifest(const std::string& url)
{
    std::string low = str_lower(url);
    return str_ends_with(low, ".m3u8") || str_ends_with(low, ".mpd")
        || low.find(".m3u8?") != std::string::npos
        || low.find(".mpd?")  != std::string::npos;
}

bool ManifestParser::parse(const std::string& url)
{
    tags_.clear();
    std::string low = str_lower(url);
    if (str_ends_with(low, ".mpd") || low.find(".mpd?") != std::string::npos)
        return parse_dash(url);
    return parse_hls(url);
}

int ManifestParser::disc_seq_at(double pts) const
{
    int seq = 0;
    for (const auto& t : tags_) {
        if (t.kind == ManifestTagKind::DiscontinuitySequence && t.pts <= pts)
            seq = t.disc_seq;
    }
    return seq;
}

// ── FFmpeg avio fetch ─────────────────────────────────────────────────────────

bool ManifestParser::fetch_text(const std::string& url, std::string& out)
{
    AVIOContext* ctx = nullptr;
    if (avio_open(&ctx, url.c_str(), AVIO_FLAG_READ) < 0) {
        logger::warn("ManifestParser: cannot open {}", url);
        return false;
    }

    std::string result;
    result.reserve(65536);
    uint8_t buf[4096];
    int n;
    while ((n = avio_read(ctx, buf, sizeof(buf))) > 0)
        result.append(reinterpret_cast<char*>(buf), static_cast<size_t>(n));

    avio_close(ctx);
    out = std::move(result);
    return true;
}

// ── HLS fetch wrapper ─────────────────────────────────────────────────────────

bool ManifestParser::parse_hls(const std::string& url)
{
    std::string text;
    if (!fetch_text(url, text)) return false;
    bool ok = parse_hls_text(text);
    if (ok)
        logger::info("ManifestParser: parsed HLS — {} tags from {}", tags_.size(), url);
    return ok;
}

// ── HLS line-by-line parser ───────────────────────────────────────────────────

bool ManifestParser::parse_hls_text(const std::string& text)
{
    tags_.clear();

    double running_pts = 0.0;
    int    disc_seq    = 0;

    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        // Strip Windows-style CR
        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line == "#EXT-X-DISCONTINUITY") {
            tags_.push_back({ running_pts, disc_seq,
                              ManifestTagKind::Discontinuity, line, "" });

        } else if (line.starts_with("#EXT-X-DISCONTINUITY-SEQUENCE:")) {
            disc_seq = std::stoi(line.substr(30));
            tags_.push_back({ running_pts, disc_seq,
                              ManifestTagKind::DiscontinuitySequence, line,
                              line.substr(30) });

        } else if (line.starts_with("#EXT-X-MAP:")) {
            tags_.push_back({ running_pts, disc_seq,
                              ManifestTagKind::Map, line, extract_value(line) });

        } else if (line.starts_with("#EXT-X-PROGRAM-DATE-TIME:")) {
            tags_.push_back({ running_pts, disc_seq,
                              ManifestTagKind::ProgramDateTime, line, extract_value(line) });

        } else if (line.starts_with("#EXT-X-CUE-OUT")) {
            tags_.push_back({ running_pts, disc_seq,
                              ManifestTagKind::CueOut, line, extract_value(line) });

        } else if (line == "#EXT-X-CUE-IN") {
            tags_.push_back({ running_pts, disc_seq,
                              ManifestTagKind::CueIn, line, "" });

        } else if (line.starts_with("#EXT-X-") &&
                   !line.starts_with("#EXT-X-VERSION:") &&
                   !line.starts_with("#EXT-X-TARGETDURATION:") &&
                   !line.starts_with("#EXT-X-MEDIA-SEQUENCE:") &&
                   !line.starts_with("#EXT-X-PLAYLIST-TYPE:") &&
                   !line.starts_with("#EXT-X-ENDLIST") &&
                   !line.starts_with("#EXT-X-KEY:") &&
                   !line.starts_with("#EXT-X-ALLOW-CACHE:")) {
            // Unknown / proprietary tag
            tags_.push_back({ running_pts, disc_seq,
                              ManifestTagKind::Unknown, line, extract_value(line) });

        } else if (line.starts_with("#EXTINF:")) {
            const char* p = line.c_str() + 8;   // after "#EXTINF:"
            char* end = nullptr;
            running_pts += std::strtod(p, &end);
        }
    }

    return true;
}

// ── DASH fetch wrapper ────────────────────────────────────────────────────────

bool ManifestParser::parse_dash(const std::string& url)
{
    std::string text;
    if (!fetch_text(url, text)) return false;
    bool ok = parse_dash_text(text);
    if (ok)
        logger::info("ManifestParser: parsed DASH MPD — {} tags from {}", tags_.size(), url);
    return ok;
}

// ── DASH MPD XML parser (pugixml) ─────────────────────────────────────────────

bool ManifestParser::parse_dash_text(const std::string& text)
{
    tags_.clear();

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_buffer(text.data(), text.size());
    if (!result) {
        logger::warn("ManifestParser: MPD XML parse error: {}", result.description());
        return false;
    }

    // Walk <MPD> → <Period> elements
    pugi::xml_node mpd = doc.child("MPD");
    if (!mpd) mpd = doc.first_child();

    double period_start = 0.0;
    int    period_idx   = 0;

    for (pugi::xml_node period : mpd.children("Period")) {
        // Explicit start attribute overrides accumulated duration
        if (auto attr_start = period.attribute("start"))
            period_start = parse_iso_duration(attr_start.value());

        if (period_idx > 0) {
            std::string lbl = std::string("Period ") + std::to_string(period_idx);
            if (auto id = period.attribute("id"))
                lbl = std::string("Period ") + id.value();
            tags_.push_back({ period_start, period_idx,
                              ManifestTagKind::Period, lbl, lbl });
        }
        ++period_idx;

        // Walk EventStream → Event
        for (pugi::xml_node evstream : period.children("EventStream")) {
            double timescale = evstream.attribute("timescale").as_double(1.0);
            if (timescale == 0.0) timescale = 1.0;

            for (pugi::xml_node ev : evstream.children("Event")) {
                double ev_pts = period_start;
                if (auto pt = ev.attribute("presentationTime"))
                    ev_pts = period_start + pt.as_double() / timescale;

                std::string scheme = evstream.attribute("schemeIdUri").as_string();
                std::string id_val = ev.attribute("id").as_string();

                std::string label = scheme.empty() ? "Event" : scheme;
                if (!id_val.empty()) label += " id=" + id_val;

                tags_.push_back({ ev_pts, 0, ManifestTagKind::Event, label, label });
            }
        }

        // Accumulate period duration for next period_start
        if (auto dur_attr = period.attribute("duration"))
            period_start += parse_iso_duration(dur_attr.value());
    }

    std::sort(tags_.begin(), tags_.end(),
              [](const ManifestTag& a, const ManifestTag& b) { return a.pts < b.pts; });

    return true;
}
