#include "HlsMarkers.h"

#include "JsonText.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <set>
#include <string_view>

namespace hls {

namespace {

constexpr std::string_view kDateRange = "EXT-X-DATERANGE";
constexpr std::string_view kInterstitial = "com.apple.hls.interstitial";

// A playlist time as seconds and clock: "754.360 s (12:34.360)".
std::string at_text(double t)
{
    if (std::isnan(t)) return "—";
    return std::format("{:.3f} s ({})", t, format_clock(t));
}

// "+3 ms", "−500 ms", "+1.250 s".
std::string signed_text(double d)
{
    if (std::isnan(d)) return "—";
    return (d < 0 ? "−" : "+") + format_seconds(std::fabs(d));
}

// The date of playlist time t: the segment holding it (or the last one
// before it) and its PROGRAM-DATE-TIME. NaN without one.
double date_of_time(const MediaTimeline& tl, double t)
{
    const Segment* best = nullptr;
    for (const auto& s : tl.segments) {
        if (s.start > t) break;
        if (!std::isnan(s.pdt)) best = &s;
    }
    return best ? best->pdt + (t - best->start) : kNaN;
}

// Segment boundaries: every segment's start, and the end of the last.
struct Nearest { double t = kNaN; int index = -1; };
Nearest nearest_boundary(const MediaTimeline& tl, double t)
{
    Nearest n;
    double best = std::numeric_limits<double>::infinity();
    const auto consider = [&](double b, int i) {
        const double d = std::fabs(b - t);
        if (d < best) { best = d; n.t = b; n.index = i; }
    };
    for (std::size_t i = 0; i < tl.segments.size(); ++i) consider(tl.segments[i].start, static_cast<int>(i));
    if (!tl.segments.empty()) consider(tl.total, static_cast<int>(tl.segments.size()));
    return n;
}

double start_of(const MediaTimeline& tl, int index)
{
    if (index < 0) return kNaN;
    if (static_cast<std::size_t>(index) >= tl.segments.size()) return tl.total;
    return tl.segments[static_cast<std::size_t>(index)].start;
}

// "#519" — a segment by its media sequence number; "the end" past the last.
std::string seg_text(const MediaTimeline& tl, int index)
{
    if (index < 0) return "—";
    if (static_cast<std::size_t>(index) >= tl.segments.size()) return "the end of the playlist";
    return std::format("segment #{}", tl.segments[static_cast<std::size_t>(index)].seq);
}

// A DATERANGE a player splices at: an interstitial, or an SCTE-35 cue.
// (CUE= is an interstitial's attribute; other dates — chapters, programme
// boundaries — need not fall on a boundary.)
bool splices(const Marker& m)
{
    if (m.tag != kDateRange) return true;
    return m.cls == kInterstitial || m.value.find("SCTE35-OUT=") != std::string::npos
        || m.value.find("SCTE35-IN=") != std::string::npos;
}

} // namespace

std::vector<std::string> marker_tag_candidates(std::string_view text)
{
    // RFC 8216 and draft-pantos-hls-rfc8216bis: the tags a player knows.
    static constexpr std::string_view kKnown[] = {
        "EXTM3U", "EXTINF", "EXT-X-VERSION", "EXT-X-BYTERANGE", "EXT-X-DISCONTINUITY", "EXT-X-KEY", "EXT-X-MAP",
        "EXT-X-PROGRAM-DATE-TIME", "EXT-X-DATERANGE", "EXT-X-TARGETDURATION", "EXT-X-MEDIA-SEQUENCE",
        "EXT-X-DISCONTINUITY-SEQUENCE", "EXT-X-ENDLIST", "EXT-X-PLAYLIST-TYPE", "EXT-X-I-FRAMES-ONLY", "EXT-X-MEDIA",
        "EXT-X-STREAM-INF", "EXT-X-I-FRAME-STREAM-INF", "EXT-X-SESSION-DATA", "EXT-X-SESSION-KEY",
        "EXT-X-INDEPENDENT-SEGMENTS", "EXT-X-START", "EXT-X-GAP", "EXT-X-BITRATE", "EXT-X-PART", "EXT-X-PART-INF",
        "EXT-X-SERVER-CONTROL", "EXT-X-PRELOAD-HINT", "EXT-X-RENDITION-REPORT", "EXT-X-SKIP", "EXT-X-DEFINE",
        "EXT-X-CONTENT-STEERING", "EXT-X-ALLOW-CACHE",
    };
    std::vector<std::string> out;
    std::size_t pos = 0;
    while (pos < text.size()) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string_view::npos) eol = text.size();
        std::string_view line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!line.starts_with("#EXT")) continue;
        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos) continue;
        const std::string_view name = line.substr(1, colon - 1);
        const std::string_view value = line.substr(colon + 1);
        if (std::find(std::begin(kKnown), std::end(kKnown), name) != std::end(kKnown)) continue;
        // An ID attribute: "ID=" first, or after a comma (not GROUP-ID=, …).
        bool id = value.starts_with("ID=");
        for (std::size_t at = value.find(",ID="); !id && at != std::string_view::npos; at = value.find(",ID=", at + 1))
            id = true;
        if (!id) continue;
        if (std::find(out.begin(), out.end(), name) == out.end()) out.emplace_back(name);
    }
    return out;
}

double playlist_time_of_date(const MediaTimeline& tl, double d)
{
    if (std::isnan(d)) return kNaN;
    const Segment* best = nullptr;
    for (const auto& s : tl.segments)
        if (!std::isnan(s.pdt) && s.pdt <= d + 1e-9) best = &s;
    return best ? best->start + (d - best->pdt) : kNaN;
}

MarkerReport analyze_markers(const std::vector<MarkerIn>& in)
{
    MarkerReport rep;
    const int n = static_cast<int>(in.size());
    for (int i = 0; i < n && rep.reference < 0; ++i)
        if (in[static_cast<std::size_t>(i)].kind == Track::Video && in[static_cast<std::size_t>(i)].tl.ok) rep.reference = i;
    for (int i = 0; i < n && rep.reference < 0; ++i)
        if (in[static_cast<std::size_t>(i)].tl.ok) rep.reference = i;
    if (rep.reference < 0) return rep;
    const MediaTimeline& ref = in[static_cast<std::size_t>(rep.reference)].tl;

    // A rendition's playlist time from the reference's: the same time for
    // VOD (both timelines start at the first segment), the same date for
    // live windows fetched at different moments.
    const auto from_ref = [&](int r, double t) {
        const MediaTimeline& tl = in[static_cast<std::size_t>(r)].tl;
        if (r == rep.reference || !(ref.live() && tl.live() && ref.has_pdt() && tl.has_pdt())) return t;
        return playlist_time_of_date(tl, date_of_time(ref, t));
    };
    const auto to_ref = [&](int r, double t) {
        const MediaTimeline& tl = in[static_cast<std::size_t>(r)].tl;
        if (r == rep.reference || !(ref.live() && tl.live() && ref.has_pdt() && tl.has_pdt())) return t;
        return playlist_time_of_date(ref, date_of_time(tl, t));
    };

    // Events by ID, first seen first; one place per rendition and tag.
    std::map<std::string, std::size_t> by_id;
    std::vector<std::set<std::string>> tags_of(static_cast<std::size_t>(n));   // tags each rendition carries
    struct Raw { int r; const Marker* m; };
    std::vector<std::vector<Raw>> raws;
    for (int r = 0; r < n; ++r) {
        const MediaTimeline& tl = in[static_cast<std::size_t>(r)].tl;
        if (!tl.ok) continue;
        std::set<std::pair<std::string, std::string>> seen;   // (id, tag)
        for (const Marker& m : tl.markers) {
            tags_of[static_cast<std::size_t>(r)].insert(m.tag);
            if (!seen.insert({ m.id, m.tag }).second) continue;   // a DATERANGE may be repeated
            auto [it, fresh] = by_id.try_emplace(m.id, rep.events.size());
            if (fresh) {
                rep.events.emplace_back();
                rep.events.back().id = m.id;
                raws.emplace_back();
            }
            raws[it->second].push_back({ r, &m });
            if (rep.events[it->second].cls.empty() && !m.cls.empty()) rep.events[it->second].cls = m.cls;
        }
    }

    auto add = [&](Severity sev, std::vector<int> rs, double t, std::string title, std::string detail) {
        Finding f;
        f.sev = sev;
        f.renditions = std::move(rs);
        f.time = t;
        f.title = std::move(title);
        f.detail = std::move(detail);
        rep.findings.push_back(std::move(f));
    };

    for (std::size_t e = 0; e < rep.events.size(); ++e) {
        MarkerEvent& ev = rep.events[e];
        const std::vector<Raw>& rs = raws[e];

        // The event's time: a DATERANGE's START-DATE, the reference's first.
        const Raw* src = nullptr;
        double src_at = kNaN;
        for (int pass = 0; pass < 2 && !src; ++pass)
            for (const Raw& x : rs) {
                if (x.m->tag != kDateRange || (pass == 0 && x.r != rep.reference)) continue;
                const double t = playlist_time_of_date(in[static_cast<std::size_t>(x.r)].tl, x.m->start_date);
                if (std::isnan(t)) continue;
                src = &x;
                src_at = t;
                break;
            }
        if (src) {
            ev.time = to_ref(src->r, src_at);
            ev.time_from = std::format("EXT-X-DATERANGE START-DATE in {}", in[static_cast<std::size_t>(src->r)].label);
        } else {
            for (int pass = 0; pass < 2 && !src; ++pass)
                for (const Raw& x : rs) {
                    if (x.m->tag == kDateRange || (pass == 0 && x.r != rep.reference)) continue;
                    src = &x;
                    break;
                }
            if (src) {
                ev.time = to_ref(src->r, start_of(in[static_cast<std::size_t>(src->r)].tl, src->m->before));
                ev.time_from = std::format("#{} in {}", src->m->tag, in[static_cast<std::size_t>(src->r)].label);
            }
        }

        std::set<std::pair<int, std::string>> present;
        for (const Raw& x : rs) {
            const MarkerIn& mi = in[static_cast<std::size_t>(x.r)];
            const MediaTimeline& tl = mi.tl;
            MarkerPlace p;
            p.rendition = x.r;
            p.tag = x.m->tag;
            p.line = x.m->line;
            present.insert({ x.r, x.m->tag });
            const bool is_dr = x.m->tag == kDateRange;
            if (is_dr) {
                p.at = playlist_time_of_date(tl, x.m->start_date);
            } else {
                p.before = x.m->before;
                p.at = start_of(tl, x.m->before);
            }
            p.expected = std::isnan(ev.time) ? kNaN : from_ref(x.r, ev.time);
            const Nearest nb = nearest_boundary(tl, std::isnan(p.expected) ? p.at : p.expected);
            p.nearest = nb.t;
            p.nearest_index = nb.index;
            p.off = p.at - p.nearest;

            if (is_dr && std::isnan(p.at)) {
                p.sev = Severity::Info;
                add(Severity::Info, { x.r }, ev.time,
                    std::format("{}: its START-DATE cannot be placed in {}", ev.id, mi.label),
                    std::format("Line {}: {} has no EXT-X-PROGRAM-DATE-TIME at or before {}, so the date has no "
                                "playlist time.", p.line, mi.label, std::isnan(x.m->start_date) ? "it"
                                                                                                : "the START-DATE"));
            } else if (is_dr) {
                // A DATERANGE the player splices at: on a boundary of its own rendition.
                const Nearest own = nearest_boundary(tl, p.at);
                p.nearest = own.t;
                p.nearest_index = own.index;
                p.off = p.at - own.t;
                if (splices(*x.m) && std::fabs(p.off) > kMarkerSlack) {
                    p.sev = Severity::Warning;
                    add(Severity::Warning, { x.r }, ev.time,
                        std::format("{}: START-DATE is {} off a segment boundary in {}", ev.id, signed_text(p.off),
                                    mi.label),
                        std::format("Line {}: START-DATE falls at {}; the nearest boundary is {} ({}). A player "
                                    "splicing {} there cuts inside a segment, or moves the cut to a boundary "
                                    "{} away.", p.line, at_text(p.at), at_text(own.t),
                                    seg_text(tl, own.index), ev.cls.empty() ? "the cue" : "the interstitial",
                                    format_seconds(std::fabs(p.off))));
                }
            } else if (!std::isnan(p.expected) && !(src && src->r == x.r && src->m == x.m)) {
                // A tag: before the segment nearest the event, a tie either way.
                const double got = std::fabs(p.at - p.expected), best = std::fabs(p.nearest - p.expected);
                if (got - best > kMarkerSlack) {
                    p.sev = Severity::Error;
                    const int by = p.before - p.nearest_index;
                    const std::string segs = std::abs(by) == 1 ? "one segment" : std::format("{} segments", std::abs(by));
                    add(Severity::Error, { x.r }, ev.time,
                        std::format("{}: #{} in {} is {} {}", ev.id, p.tag, mi.label, segs, by < 0 ? "early" : "late"),
                        std::format("Line {}: it comes before {} at {}, {} from the event at {} ({}). The boundary "
                                    "nearest the event is {} ({}), {} from it. A player splices {} {} away from the "
                                    "renditions that mark it there.",
                                    p.line, seg_text(tl, p.before), at_text(p.at), signed_text(p.at - p.expected),
                                    at_text(p.expected), ev.time_from, at_text(p.nearest),
                                    seg_text(tl, p.nearest_index), signed_text(p.nearest - p.expected), mi.label,
                                    format_seconds(std::fabs(p.at - p.nearest))));
                }
            }
            ev.places.push_back(std::move(p));
        }

        // Renditions that carry a tag but not for this event, where their timeline reaches it.
        std::set<std::string> ev_tags;
        bool ev_splices = false;
        for (const Raw& x : rs) {
            ev_tags.insert(x.m->tag);
            ev_splices = ev_splices || (x.m->tag == kDateRange && splices(*x.m));
            ev.splice = ev.splice || splices(*x.m);
        }
        for (int r = 0; r < n; ++r) {
            const MarkerIn& mi = in[static_cast<std::size_t>(r)];
            if (!mi.tl.ok) continue;
            bool has_any = false;
            for (const Raw& x : rs) has_any = has_any || x.r == r;
            if (has_any) continue;
            std::string tag;
            for (const auto& t : ev_tags)
                if (tags_of[static_cast<std::size_t>(r)].contains(t)) { tag = t; break; }
            if (tag.empty() && ev_splices) {   // a break it marks with another tag (DATERANGE in video, a tag in audio)
                for (const auto& t : tags_of[static_cast<std::size_t>(r)]) { tag = t; break; }
            }
            if (tag.empty()) continue;   // it marks nothing at all
            const double t = std::isnan(ev.time) ? kNaN : from_ref(r, ev.time);
            if (std::isnan(t) || t < -kMarkerSlack || t > mi.tl.total + kMarkerSlack) continue;
            // As bad as a misplaced marker of its kind (D-102): a player that splits the rendition at its
            // own tag has no break there at all.
            const Severity sev = tag == kDateRange ? Severity::Warning : Severity::Error;
            ev.missing.push_back(r);
            ev.missing_sev.push_back(sev);
            add(sev, { r }, ev.time, std::format("{}: no #{} in {}", ev.id, tag, mi.label),
                std::format("{} marks other events with #{}, but not this one at {} ({}): a player has no break "
                            "to splice there in it.", mi.label, tag, at_text(t), ev.time_from));
        }
    }

    // Renditions that mark nothing while others do: one note.
    std::vector<int> unmarked;
    if (!rep.events.empty())
        for (int r = 0; r < n; ++r)
            if (in[static_cast<std::size_t>(r)].tl.ok && tags_of[static_cast<std::size_t>(r)].empty()) unmarked.push_back(r);
    if (!unmarked.empty()) {
        std::string names;
        for (const int r : unmarked) names += (names.empty() ? "" : ", ") + in[static_cast<std::size_t>(r)].label;
        add(Severity::Info, unmarked, kNaN,
            std::format("{} rendition{} mark{} no event", unmarked.size(), unmarked.size() == 1 ? "" : "s",
                        unmarked.size() == 1 ? "s" : ""),
            std::format("{}: no EXT-X-DATERANGE and none of the tags asked for. A rendition a player splices by "
                        "the others' dates needs none; one spliced by its own tags does.", names));
    }

    std::stable_sort(rep.events.begin(), rep.events.end(), [](const MarkerEvent& a, const MarkerEvent& b) {
        if (std::isnan(a.time) != std::isnan(b.time)) return std::isnan(b.time);
        return a.time < b.time;
    });
    std::stable_sort(rep.findings.begin(), rep.findings.end(), [](const Finding& a, const Finding& b) {
        if (a.sev != b.sev) return a.sev < b.sev;
        if (std::isnan(a.time) != std::isnan(b.time)) return std::isnan(b.time);
        return a.time < b.time;
    });
    for (const auto& f : rep.findings) {
        rep.errors += f.sev == Severity::Error ? 1 : 0;
        rep.warnings += f.sev == Severity::Warning ? 1 : 0;
    }
    return rep;
}

int splice_event(const MarkerReport& r, int n)
{
    for (int e = 0; e < static_cast<int>(r.events.size()); ++e)
        if (r.events[static_cast<std::size_t>(e)].splice && n-- == 0) return e;
    return -1;
}

std::string markers_json(const std::vector<MarkerIn>& in, const MarkerReport& r)
{
    using json::number;
    using json::quote;
    std::string o = std::format("{{\"reference\": {}, \"slack\": {}, \"renditions\": [", r.reference, number(kMarkerSlack));
    for (std::size_t i = 0; i < in.size(); ++i)
        o += std::format("{}{}", i ? ", " : "", quote(in[i].label));
    o += "],\n    \"events\": [";
    for (std::size_t e = 0; e < r.events.size(); ++e) {
        const MarkerEvent& ev = r.events[e];
        o += std::format("{}\n      {{\"id\": {}, \"class\": {}, \"time\": {}, \"time_from\": {}, \"splice\": {}, "
                         "\"places\": [",
                         e ? "," : "", quote(ev.id), ev.cls.empty() ? "null" : quote(ev.cls), number(ev.time),
                         ev.time_from.empty() ? "null" : quote(ev.time_from), ev.splice ? "true" : "false");
        for (std::size_t k = 0; k < ev.places.size(); ++k) {
            const MarkerPlace& p = ev.places[k];
            const MediaTimeline& tl = in[static_cast<std::size_t>(p.rendition)].tl;
            const auto seq = [&](int idx) {
                return idx >= 0 && static_cast<std::size_t>(idx) < tl.segments.size()
                     ? std::to_string(tl.segments[static_cast<std::size_t>(idx)].seq) : std::string("null");
            };
            o += std::format("{}\n        {{\"rendition\": {}, \"tag\": {}, \"line\": {}, \"at\": {}, \"expected\": {}, "
                             "\"nearest_boundary\": {}, \"nearest_sequence\": {}, \"before_sequence\": {}, \"off\": {}, "
                             "\"severity\": {}}}",
                             k ? "," : "", p.rendition, quote(p.tag), p.line, number(p.at), number(p.expected),
                             number(p.nearest), seq(p.nearest_index), p.before >= 0 ? seq(p.before) : "null",
                             number(p.off), quote(severity_name(p.sev)));
        }
        std::string miss, miss_sev;
        for (const int m : ev.missing) miss += (miss.empty() ? "" : ", ") + std::to_string(m);
        for (const Severity s : ev.missing_sev) miss_sev += (miss_sev.empty() ? "" : ", ") + quote(severity_name(s));
        o += std::format("], \"missing\": [{}], \"missing_severity\": [{}]}}", miss, miss_sev);
    }
    o += "],\n    \"findings\": [";
    for (std::size_t i = 0; i < r.findings.size(); ++i) {
        const Finding& f = r.findings[i];
        std::string vs;
        for (const int k : f.renditions) vs += (vs.empty() ? "" : ", ") + std::to_string(k);
        o += std::format("{}\n      {{\"severity\": {}, \"title\": {}, \"detail\": {}, \"renditions\": [{}]}}",
                         i ? "," : "", quote(severity_name(f.sev)), quote(f.title), quote(f.detail), vs);
    }
    o += std::format("], \"errors\": {}, \"warnings\": {}}}", r.errors, r.warnings);
    return o;
}

} // namespace hls
