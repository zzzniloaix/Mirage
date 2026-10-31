#include "DashMpd.h"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <format>

namespace dash {

namespace {

// ── Text ─────────────────────────────────────────────────────────────────────

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

// A whole decimal integer (a leading '-' allowed).
bool to_i64(const char* s, std::int64_t& out)
{
    if (!s) return false;
    const std::string t(trim(s));
    if (t.empty()) return false;
    char* end = nullptr;
    errno = 0;
    const long long v = std::strtoll(t.c_str(), &end, 10);
    if (end == t.c_str() || *end || errno == ERANGE) return false;
    out = v;
    return true;
}

double to_double(const char* s)
{
    if (!s) return kNaN;
    const std::string t(trim(s));
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    return end == t.c_str() || *end ? kNaN : v;
}

// "a-b" (bytes a to b inclusive).
bool to_range(const char* s, ByteRange& out)
{
    if (!s) return false;
    const std::string_view t = trim(s);
    const auto dash = t.find('-');
    if (dash == std::string_view::npos) return false;
    std::int64_t a = 0, b = 0;
    if (!to_i64(std::string(t.substr(0, dash)).c_str(), a) || !to_i64(std::string(t.substr(dash + 1)).c_str(), b)
        || a < 0 || b < a)
        return false;
    out.offset = a;
    out.length = b - a + 1;
    return true;
}

std::uint32_t be16(const std::uint8_t* p) { return (std::uint32_t(p[0]) << 8) | p[1]; }
std::uint32_t be32(const std::uint8_t* p)
{
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
std::uint64_t be64(const std::uint8_t* p) { return (std::uint64_t(be32(p)) << 32) | be32(p + 4); }

// ── XML: names whatever their namespace prefix ───────────────────────────────

std::string_view local(const char* name)
{
    const std::string_view s(name);
    const auto colon = s.find(':');
    return colon == std::string_view::npos ? s : s.substr(colon + 1);
}

pugi::xml_node child(pugi::xml_node n, std::string_view name)
{
    for (pugi::xml_node c = n.first_child(); c; c = c.next_sibling())
        if (c.type() == pugi::node_element && local(c.name()) == name) return c;
    return {};
}

std::vector<pugi::xml_node> children(pugi::xml_node n, std::string_view name)
{
    std::vector<pugi::xml_node> out;
    for (pugi::xml_node c = n.first_child(); c; c = c.next_sibling())
        if (c.type() == pugi::node_element && local(c.name()) == name) out.push_back(c);
    return out;
}

const char* attr(pugi::xml_node n, std::string_view name)
{
    if (!n) return nullptr;
    for (pugi::xml_attribute a = n.first_attribute(); a; a = a.next_attribute())
        if (local(a.name()) == name) return a.value();
    return nullptr;
}

std::string attr_or(pugi::xml_node n, std::string_view name, std::string_view fallback = {})
{
    const char* v = attr(n, name);
    return v ? std::string(trim(v)) : std::string(fallback);
}

// A common attribute (RepresentationBaseType): the Representation's, else its AdaptationSet's.
const char* common(pugi::xml_node rep, pugi::xml_node set, std::string_view name)
{
    if (const char* v = attr(rep, name)) return v;
    return attr(set, name);
}

std::string base_of(pugi::xml_node n, const std::string& parent)
{
    if (const pugi::xml_node b = child(n, "BaseURL")) {
        const std::string t(trim(b.child_value()));
        if (!t.empty()) return hls::resolve_uri(parent, t);
    }
    return parent;
}

void descriptors(pugi::xml_node n, std::string_view name, std::vector<Descriptor>& out)
{
    for (const pugi::xml_node d : children(n, name)) {
        Descriptor x;
        x.scheme = attr_or(d, "schemeIdUri");
        x.value  = attr_or(d, "value");
        if (name == "ContentProtection")
            if (const char* kid = attr(d, "default_KID")) x.value = std::string(trim(kid));
        out.push_back(std::move(x));
    }
}

Track kind_of(std::string_view content_type, std::string_view mime, std::string_view codecs)
{
    if (content_type == "video") return Track::Video;
    if (content_type == "audio") return Track::Audio;
    if (content_type == "text")  return Track::Text;
    if (mime.starts_with("video/")) return Track::Video;
    if (mime.starts_with("audio/")) return Track::Audio;
    if (mime.starts_with("text/") || mime == "application/ttml+xml") return Track::Text;
    const std::string_view c = codecs.substr(0, codecs.find(','));
    for (const char* v : { "avc", "hvc1", "hev1", "av01", "vp09", "vp08", "dvh", "dva", "dav1" })
        if (c.starts_with(v)) return Track::Video;
    for (const char* a : { "mp4a", "ac-3", "ec-3", "ac-4", "Opus", "opus", "fLaC", "flac", "mha", "mhm" })
        if (c.starts_with(a)) return Track::Audio;
    for (const char* t : { "wvtt", "stpp", "tx3g" })
        if (c.starts_with(t)) return Track::Text;
    return Track::Unknown;
}

// ── Segment information, inherited ───────────────────────────────────────────

// The SegmentTemplate, SegmentList or SegmentBase elements over a
// Representation, its own first: the lowest level with any of the three
// decides the kind; every level with one of that kind gives what the levels
// below leave out.
struct SegInfo {
    Addressing kind = Addressing::None;
    std::vector<pugi::xml_node> chain;

    const char* get(std::string_view a) const
    {
        for (const auto n : chain)
            if (const char* v = attr(n, a)) return v;
        return nullptr;
    }
    pugi::xml_node sub(std::string_view name) const
    {
        for (const auto n : chain)
            if (const pugi::xml_node c = child(n, name)) return c;
        return {};
    }
};

SegInfo seg_info(pugi::xml_node period, pugi::xml_node set, pugi::xml_node rep)
{
    const pugi::xml_node levels[3] = { rep, set, period };
    std::string_view kind;
    for (const auto lv : levels) {
        for (const char* k : { "SegmentTemplate", "SegmentList", "SegmentBase" })
            if (child(lv, k)) {
                kind = k;
                break;
            }
        if (!kind.empty()) break;
    }
    SegInfo si;
    if (kind.empty()) return si;
    for (const auto lv : levels)
        if (const pugi::xml_node c = child(lv, kind)) si.chain.push_back(c);
    if (kind == "SegmentTemplate") si.kind = si.sub("SegmentTimeline") ? Addressing::Timeline : Addressing::Template;
    else if (kind == "SegmentList") si.kind = Addressing::List;
    else si.kind = Addressing::Base;
    return si;
}

// (media time, duration, number) of every S of a SegmentTimeline. `limit`:
// the media time an S@r of -1 repeats up to when no S@t follows (the
// Period's end, or now in a dynamic MPD); NaN = unknown.
struct Slot { std::int64_t time, duration, number; };

std::vector<Slot> timeline_slots(pugi::xml_node tl, std::int64_t start_number, double limit,
                                 std::vector<std::string>& problems)
{
    constexpr std::int64_t kMaxSegments = 500'000;
    std::vector<Slot> out;
    const std::vector<pugi::xml_node> ss = children(tl, "S");
    std::int64_t t = 0, number = start_number;
    for (std::size_t k = 0; k < ss.size(); ++k) {
        std::int64_t st = 0, d = 0, r = 0, n = 0;
        if (to_i64(attr(ss[k], "t"), st)) {
            if (k > 0 && st < t)
                problems.push_back(std::format("SegmentTimeline S #{} starts at t={}, before the previous one ends ({})",
                                               k + 1, st, t));
            t = st;
        }
        if (!to_i64(attr(ss[k], "d"), d) || d <= 0) {
            problems.push_back(std::format("SegmentTimeline S #{} has no valid @d", k + 1));
            break;
        }
        to_i64(attr(ss[k], "r"), r);
        if (to_i64(attr(ss[k], "n"), n)) number = n;
        std::int64_t count = r + 1;
        if (r < 0) {
            std::int64_t next = 0;
            double end = kNaN;
            if (k + 1 < ss.size() && to_i64(attr(ss[k + 1], "t"), next)) end = static_cast<double>(next);
            else end = limit;
            if (std::isnan(end)) {
                problems.push_back(std::format("SegmentTimeline S #{} repeats (@r=-1) up to an end that is not known", k + 1));
                count = 1;
            } else {
                count = std::max<std::int64_t>(1, static_cast<std::int64_t>(
                                                      std::ceil((end - static_cast<double>(t)) / static_cast<double>(d) - 1e-9)));
            }
        }
        for (std::int64_t i = 0; i < count && static_cast<std::int64_t>(out.size()) < kMaxSegments; ++i) {
            out.push_back({ t, d, number });
            t += d;
            ++number;
        }
    }
    return out;
}

} // namespace

// ── Public ───────────────────────────────────────────────────────────────────

const char* addressing_name(Addressing a)
{
    switch (a) {
        case Addressing::Timeline: return "SegmentTimeline";
        case Addressing::Template: return "SegmentTemplate@duration";
        case Addressing::List:     return "SegmentList";
        case Addressing::Base:     return "SegmentBase";
        case Addressing::None:     break;
    }
    return "none";
}

double Representation::total() const
{
    double t = 0.0;
    for (const auto& s : segments) t += s.seconds;
    return t;
}

bool AdaptationSet::aligned() const
{
    return (!segment_alignment.empty() && segment_alignment != "false")
        || (!subsegment_alignment.empty() && subsegment_alignment != "false");
}

double parse_duration(std::string_view in)
{
    std::string_view s = trim(in);
    bool neg = false;
    if (!s.empty() && s.front() == '-') {
        neg = true;
        s.remove_prefix(1);
    }
    if (s.empty() || s.front() != 'P') return kNaN;
    s.remove_prefix(1);
    double total = 0.0;
    bool time = false, any = false;
    while (!s.empty()) {
        if (s.front() == 'T') {
            if (time) return kNaN;
            time = true;
            s.remove_prefix(1);
            continue;
        }
        std::size_t i = 0;
        while (i < s.size() && (std::isdigit(static_cast<unsigned char>(s[i])) || s[i] == '.')) ++i;
        if (i == 0 || i == s.size()) return kNaN;
        const double v = std::strtod(std::string(s.substr(0, i)).c_str(), nullptr);
        const char unit = s[i];
        s.remove_prefix(i + 1);
        any = true;
        if (!time) {
            if (unit == 'D') total += v * 86400.0;
            else if ((unit == 'Y' || unit == 'M') && v == 0.0) continue;   // P0Y0M…: no length
            else return kNaN;
        } else {
            if (unit == 'H') total += v * 3600.0;
            else if (unit == 'M') total += v * 60.0;
            else if (unit == 'S') total += v;
            else return kNaN;
        }
    }
    if (!any) return kNaN;
    return neg ? -total : total;
}

std::string expand_template(std::string_view t, const std::string& rep_id, std::int64_t number,
                            std::int64_t bandwidth, std::int64_t time)
{
    std::string out;
    std::size_t i = 0;
    while (i < t.size()) {
        const auto d = t.find('$', i);
        if (d == std::string_view::npos) {
            out += t.substr(i);
            break;
        }
        out += t.substr(i, d - i);
        const auto e = t.find('$', d + 1);
        if (e == std::string_view::npos) {   // unterminated: kept as written
            out += t.substr(d);
            break;
        }
        const std::string_view id = t.substr(d + 1, e - d - 1);
        i = e + 1;
        if (id.empty()) {   // $$
            out += '$';
            continue;
        }
        std::string_view name = id, fmt;
        if (const auto pct = id.find('%'); pct != std::string_view::npos) {
            name = id.substr(0, pct);
            fmt  = id.substr(pct + 1);
        }
        auto num = [&](std::int64_t v) {
            std::string s = std::to_string(v);
            // %0<width>d
            if (fmt.size() >= 2 && fmt.back() == 'd') {
                const int width = std::atoi(std::string(fmt.substr(0, fmt.size() - 1)).c_str());
                if (width > static_cast<int>(s.size()) && width < 64) s.insert(0, static_cast<std::size_t>(width) - s.size(), '0');
            }
            return s;
        };
        if (name == "RepresentationID") out += rep_id;
        else if (name == "Number")      out += num(number);
        else if (name == "Bandwidth")   out += num(bandwidth);
        else if (name == "Time")        out += num(time);
        else                            out += t.substr(d, e - d + 1);   // not an identifier: kept
    }
    return out;
}

Mpd parse_mpd(const std::string& text, const std::string& base_url, double now)
{
    Mpd m;
    pugi::xml_document doc;
    if (const pugi::xml_parse_result res = doc.load_buffer(text.data(), text.size()); !res) {
        m.problems.push_back(std::format("not XML: {} at byte {}", res.description(), res.offset));
        return m;
    }
    pugi::xml_node mpd;
    for (pugi::xml_node c = doc.first_child(); c; c = c.next_sibling())
        if (c.type() == pugi::node_element && local(c.name()) == "MPD") mpd = c;
    if (!mpd) {
        m.problems.push_back("no MPD element");
        return m;
    }
    m.dynamic  = attr_or(mpd, "type", "static") == "dynamic";
    m.profiles = attr_or(mpd, "profiles");
    if (const char* v = attr(mpd, "minBufferTime")) m.min_buffer_time = parse_duration(v);
    if (const char* v = attr(mpd, "mediaPresentationDuration")) m.duration = parse_duration(v);
    if (const char* v = attr(mpd, "maxSegmentDuration")) m.max_segment_duration = parse_duration(v);
    if (const char* v = attr(mpd, "availabilityStartTime")) m.availability_start = hls::parse_iso8601(v);
    if (const char* v = attr(mpd, "timeShiftBufferDepth")) m.time_shift_buffer = parse_duration(v);
    m.now = now;
    if (m.profiles.empty()) m.problems.push_back("MPD@profiles is absent (it is mandatory)");
    if (std::isnan(m.min_buffer_time)) m.problems.push_back("MPD@minBufferTime is absent or not a duration (it is mandatory)");
    if (m.dynamic && std::isnan(m.availability_start))
        m.problems.push_back("a dynamic MPD without a valid @availabilityStartTime");
    const std::string mpd_base = base_of(mpd, base_url);

    // Period times: @start, else the end of the one before; the length to
    // the next Period's start, or to the end of the presentation.
    const std::vector<pugi::xml_node> pnodes = children(mpd, "Period");
    for (std::size_t i = 0; i < pnodes.size(); ++i) {
        Period p;
        p.id = attr_or(pnodes[i], "id");
        const double start = parse_duration(attr_or(pnodes[i], "start"));
        if (!std::isnan(start)) p.start = start;
        else if (i > 0) p.start = m.periods[i - 1].start + m.periods[i - 1].duration;   // NaN if that is unknown
        p.duration = parse_duration(attr_or(pnodes[i], "duration"));
        m.periods.push_back(p);
    }
    for (std::size_t i = 0; i < m.periods.size(); ++i) {
        Period& p = m.periods[i];
        if (!std::isnan(p.duration)) continue;
        if (i + 1 < m.periods.size() && !std::isnan(m.periods[i + 1].start)) p.duration = m.periods[i + 1].start - p.start;
        else if (i + 1 == m.periods.size() && !std::isnan(m.duration)) p.duration = m.duration - p.start;
    }
    if (m.periods.empty()) {
        m.problems.push_back("no Period");
        return m;
    }

    for (std::size_t pi = 0; pi < pnodes.size(); ++pi) {
        const pugi::xml_node period = pnodes[pi];
        const Period&        pd = m.periods[pi];
        const std::string    period_base = base_of(period, mpd_base);
        for (const pugi::xml_node set : children(period, "AdaptationSet")) {
            AdaptationSet as;
            as.period = static_cast<int>(pi);
            as.id = attr_or(set, "id");
            as.content_type = attr_or(set, "contentType");
            as.mime_type = attr_or(set, "mimeType");
            as.lang = attr_or(set, "lang");
            as.segment_alignment = attr_or(set, "segmentAlignment");
            as.subsegment_alignment = attr_or(set, "subsegmentAlignment");
            const std::string set_base = base_of(set, period_base);
            const int set_index = static_cast<int>(m.sets.size());

            for (const pugi::xml_node rep : children(set, "Representation")) {
                Representation r;
                r.period = static_cast<int>(pi);
                r.set = set_index;
                r.id = attr_or(rep, "id");
                if (r.id.empty()) {
                    r.id = std::format("#{}", m.reps.size());
                    r.problems.push_back("Representation@id is absent (it is mandatory)");
                }
                if (!to_i64(attr(rep, "bandwidth"), r.bandwidth))
                    r.problems.push_back("Representation@bandwidth is absent (it is mandatory)");
                r.mime_type = std::string(trim(common(rep, set, "mimeType") ? common(rep, set, "mimeType") : ""));
                r.codecs    = std::string(trim(common(rep, set, "codecs") ? common(rep, set, "codecs") : ""));
                r.kind = kind_of(attr_or(set, "contentType"), r.mime_type, r.codecs);
                std::int64_t v = 0;
                if (to_i64(common(rep, set, "width"), v)) r.width = static_cast<int>(v);
                if (to_i64(common(rep, set, "height"), v)) r.height = static_cast<int>(v);
                if (const char* fr = common(rep, set, "frameRate")) {
                    r.frame_rate_text = std::string(trim(fr));
                    const auto slash = r.frame_rate_text.find('/');
                    if (slash == std::string::npos) {
                        r.frame_rate = to_double(r.frame_rate_text.c_str());
                    } else {
                        const double num = to_double(r.frame_rate_text.substr(0, slash).c_str());
                        const double den = to_double(r.frame_rate_text.substr(slash + 1).c_str());
                        r.frame_rate = den > 0.0 ? num / den : kNaN;
                    }
                    if (!(r.frame_rate > 0.0)) r.frame_rate = 0.0;
                }
                if (const char* sar = common(rep, set, "sar")) {
                    const std::string s(trim(sar));
                    const auto colon = s.find(':');
                    std::int64_t a = 0, b = 0;
                    if (colon != std::string::npos && to_i64(s.substr(0, colon).c_str(), a) && to_i64(s.substr(colon + 1).c_str(), b)
                        && a > 0 && b > 0) {
                        r.sar_num = static_cast<int>(a);
                        r.sar_den = static_cast<int>(b);
                    }
                }
                if (const char* sr = common(rep, set, "audioSamplingRate")) {
                    const std::string s(trim(sr));
                    if (to_i64(s.substr(0, s.find_first_of(" \t")).c_str(), v)) r.sampling_rate = static_cast<int>(v);
                }
                if (to_i64(common(rep, set, "startWithSAP"), v)) r.start_with_sap = static_cast<int>(v);
                if (to_i64(attr(set, "subsegmentStartsWithSAP"), v)) r.subsegment_sap = static_cast<int>(v);
                descriptors(set, "EssentialProperty", r.essential);
                descriptors(rep, "EssentialProperty", r.essential);
                descriptors(set, "SupplementalProperty", r.supplemental);
                descriptors(rep, "SupplementalProperty", r.supplemental);
                descriptors(rep, "AudioChannelConfiguration", r.channels);
                if (r.channels.empty()) descriptors(set, "AudioChannelConfiguration", r.channels);
                descriptors(set, "ContentProtection", r.protection);
                descriptors(rep, "ContentProtection", r.protection);
                r.lang = as.lang;

                // ── Segments ─────────────────────────────────────────────────
                const std::string base = base_of(rep, set_base);
                const SegInfo si = seg_info(period, set, rep);
                r.addressing = si.kind;
                std::int64_t ts = 1;
                if (to_i64(si.get("timescale"), ts) && ts > 0) r.timescale = static_cast<std::uint64_t>(ts);
                to_i64(si.get("presentationTimeOffset"), r.pto);
                std::int64_t start_number = 1, end_number = -1;
                to_i64(si.get("startNumber"), start_number);
                to_i64(si.get("endNumber"), end_number);
                to_i64(si.get("duration"), r.duration);
                const double tsd = static_cast<double>(r.timescale);
                // Media time of the Period's end, or of now in a dynamic MPD.
                double limit = kNaN;
                if (m.dynamic && !std::isnan(now) && !std::isnan(m.availability_start) && !std::isnan(pd.start))
                    limit = static_cast<double>(r.pto) + (now - m.availability_start - pd.start) * tsd;
                else if (!std::isnan(pd.duration))
                    limit = static_cast<double>(r.pto) + pd.duration * tsd;
                auto add = [&](std::string uri, ByteRange range, std::int64_t number, std::int64_t time, std::int64_t dur) {
                    SegmentRef s;
                    s.uri = std::move(uri);
                    s.range = range;
                    s.number = number;
                    s.time = time;
                    s.duration = dur;
                    s.start = pd.start + static_cast<double>(time - r.pto) / tsd;
                    s.seconds = static_cast<double>(dur) / tsd;
                    r.segments.push_back(std::move(s));
                };
                if (const pugi::xml_node init = si.sub("Initialization")) {
                    r.init_uri = hls::resolve_uri(base, attr_or(init, "sourceURL"));
                    to_range(attr(init, "range"), r.init_range);
                }

                if (si.kind == Addressing::Timeline || si.kind == Addressing::Template) {
                    const std::string media = si.get("media") ? std::string(trim(si.get("media"))) : std::string();
                    if (const char* in = si.get("initialization"))
                        r.init_uri = hls::resolve_uri(base, expand_template(trim(in), r.id, 0, r.bandwidth, 0));
                    if (media.empty()) r.problems.push_back("SegmentTemplate@media is absent");
                    const auto uri = [&](std::int64_t number, std::int64_t time) {
                        return hls::resolve_uri(base, expand_template(media, r.id, number, r.bandwidth, time));
                    };
                    if (si.kind == Addressing::Timeline) {
                        for (const Slot& s : timeline_slots(si.sub("SegmentTimeline"), start_number, limit, r.problems))
                            add(uri(s.number, s.time), {}, s.number, s.time, s.duration);
                    } else if (r.duration <= 0) {
                        r.problems.push_back("SegmentTemplate has neither a SegmentTimeline nor a @duration");
                    } else {
                        const double seg = static_cast<double>(r.duration) / tsd;
                        std::int64_t first = 0, count = 0;
                        if (!m.dynamic) {
                            if (end_number >= start_number) count = end_number - start_number + 1;
                            else if (!std::isnan(pd.duration)) count = static_cast<std::int64_t>(std::ceil(pd.duration / seg - 1e-9));
                            else r.problems.push_back("the Period's length is not known: how many segments @duration gives cannot be told");
                        } else if (std::isnan(limit)) {
                            r.problems.push_back("a dynamic MPD addressed by @duration, and no clock to tell which segments exist");
                        } else {
                            // Available: ended by now (@availabilityTimeOffset earlier),
                            // within @timeShiftBufferDepth, kLiveSegments at most.
                            const double ato = to_double(si.get("availabilityTimeOffset"));
                            const double elapsed = (limit - static_cast<double>(r.pto)) / tsd + (std::isnan(ato) ? 0.0 : ato);
                            const auto last = static_cast<std::int64_t>(std::floor(elapsed / seg)) - 1;
                            first = 0;
                            if (!std::isnan(m.time_shift_buffer))
                                first = std::max<std::int64_t>(0, static_cast<std::int64_t>(std::ceil((elapsed - m.time_shift_buffer) / seg)) - 1);
                            first = std::max(first, last - kLiveSegments + 1);
                            count = last >= first ? last - first + 1 : 0;
                        }
                        constexpr std::int64_t kMaxSegments = 500'000;
                        for (std::int64_t i = first; i < first + count && i - first < kMaxSegments; ++i) {
                            const std::int64_t number = start_number + i;
                            const std::int64_t time = r.pto + i * r.duration;
                            std::int64_t dur = r.duration;
                            if (!m.dynamic && !std::isnan(pd.duration) && i + 1 == first + count) {   // the last: to the Period's end
                                const auto rest = static_cast<std::int64_t>(std::llround(pd.duration * tsd)) - i * r.duration;
                                if (rest > 0 && rest < dur) dur = rest;
                            }
                            add(uri(number, time), {}, number, time, dur);
                        }
                    }
                } else if (si.kind == Addressing::List) {
                    const pugi::xml_node list = si.chain.front();
                    const std::vector<pugi::xml_node> urls = children(list, "SegmentURL");
                    std::vector<Slot> slots;
                    if (const pugi::xml_node tl = si.sub("SegmentTimeline")) {
                        slots = timeline_slots(tl, start_number, limit, r.problems);
                        if (slots.size() != urls.size())
                            r.problems.push_back(std::format("SegmentList lists {} SegmentURLs, its SegmentTimeline {} segments",
                                                             urls.size(), slots.size()));
                    } else if (r.duration > 0) {
                        for (std::size_t i = 0; i < urls.size(); ++i)
                            slots.push_back({ r.pto + static_cast<std::int64_t>(i) * r.duration, r.duration,
                                              start_number + static_cast<std::int64_t>(i) });
                    } else if (urls.size() > 1) {
                        r.problems.push_back("SegmentList has neither a SegmentTimeline nor a @duration");
                    }
                    for (std::size_t i = 0; i < urls.size(); ++i) {
                        ByteRange range;
                        to_range(attr(urls[i], "mediaRange"), range);
                        const Slot s = i < slots.size() ? slots[i]
                                     : Slot{ r.pto, std::isnan(pd.duration) ? 0 : static_cast<std::int64_t>(std::llround(pd.duration * tsd)),
                                             start_number + static_cast<std::int64_t>(i) };
                        add(hls::resolve_uri(base, attr_or(urls[i], "media")), range, s.number, s.time, s.duration);
                    }
                } else if (si.kind == Addressing::Base) {
                    r.index_uri = base;
                    if (const pugi::xml_node ri = si.sub("RepresentationIndex")) {
                        r.index_uri = hls::resolve_uri(base, attr_or(ri, "sourceURL"));
                        to_range(attr(ri, "range"), r.index_range);
                    } else if (!to_range(si.get("indexRange"), r.index_range)) {
                        // No index: the file is one segment as long as the Period.
                        add(base, {}, -1, r.pto,
                            std::isnan(pd.duration) ? 0 : static_cast<std::int64_t>(std::llround(pd.duration * tsd)));
                    }
                    if (r.init_uri.empty() && !r.init_range.whole()) r.init_uri = base;
                } else {
                    // A BaseURL alone: one segment, the whole file.
                    if (child(rep, "BaseURL"))
                        add(base, {}, -1, 0, std::isnan(pd.duration) ? 0 : static_cast<std::int64_t>(std::llround(pd.duration * tsd)));
                    else
                        r.problems.push_back("no SegmentTemplate, SegmentList, SegmentBase or BaseURL of its own");
                }
                as.reps.push_back(static_cast<int>(m.reps.size()));
                m.reps.push_back(std::move(r));
            }
            m.sets.push_back(std::move(as));
        }
    }
    m.ok = true;
    return m;
}

Sidx parse_sidx(const std::uint8_t* d, std::size_t n, std::int64_t offset)
{
    Sidx s;
    std::size_t p = 0;
    while (p + 8 <= n) {
        std::uint64_t size = be32(d + p);
        const std::string_view type(reinterpret_cast<const char*>(d + p + 4), 4);
        std::size_t hdr = 8;
        if (size == 1) {
            if (p + 16 > n) break;
            size = be64(d + p + 8);
            hdr = 16;
        } else if (size == 0) {
            size = n - p;
        }
        if (size < hdr || size > n - p) {
            if (type == "sidx" || !s.ok) s.error = std::format("the {} box at byte {} runs past the bytes read", type, offset + static_cast<std::int64_t>(p));
            break;
        }
        if (type == "sidx") {
            const std::uint8_t* b = d + p + hdr;
            const std::size_t bn = static_cast<std::size_t>(size) - hdr;
            const int version = bn > 0 ? b[0] : 0;
            const std::size_t fixed = 4 + 8 + (version == 0 ? 8 : 16) + 4;
            if (bn < fixed) {
                s.error = "a sidx box too short for its fields";
                break;
            }
            std::size_t q = 8;   // version + flags, reference_ID
            const std::uint32_t ts = be32(b + q);
            q += 4;
            std::uint64_t ept = 0, first = 0;
            if (version == 0) {
                ept = be32(b + q);
                first = be32(b + q + 4);
                q += 8;
            } else {
                ept = be64(b + q);
                first = be64(b + q + 8);
                q += 16;
            }
            q += 2;   // reserved
            const std::uint32_t count = be16(b + q);
            q += 2;
            if (bn < q + 12ull * count) {
                s.error = std::format("a sidx box lists {} references but holds fewer", count);
                break;
            }
            if (!s.ok) {
                s.timescale = ts;
                s.earliest = static_cast<std::int64_t>(ept);
            } else if (ts != s.timescale) {
                s.error = std::format("sidx boxes with timescales {} and {}", s.timescale, ts);
            }
            // Offsets count from the first byte after this box.
            std::int64_t pos = offset + static_cast<std::int64_t>(p + size + first);
            auto t = static_cast<std::int64_t>(ept);
            for (std::uint32_t i = 0; i < count; ++i, q += 12) {
                const std::uint32_t a = be32(b + q), dur = be32(b + q + 4), c = be32(b + q + 8);
                Sidx::Ref r;
                r.index = (a >> 31) != 0;
                r.size = a & 0x7fffffff;
                r.duration = dur;
                r.sap = (c >> 31) != 0;
                r.sap_type = static_cast<int>((c >> 28) & 7);
                r.offset = pos;
                r.time = t;
                pos += r.size;
                t += dur;
                s.refs.push_back(r);
            }
            s.ok = true;
        }
        p += static_cast<std::size_t>(size);
    }
    if (!s.ok && s.error.empty()) s.error = "no sidx box in the bytes of @indexRange";
    return s;
}

void apply_sidx(Representation& r, const Sidx& s, double period_start)
{
    if (!s.ok) return;
    r.segments.clear();
    const double pto = static_cast<double>(r.pto) / static_cast<double>(r.timescale);
    // Its times are the index's: the Representation's follow, @presentationTimeOffset converted.
    if (s.timescale > 0 && s.timescale != r.timescale) {
        if (r.timescale > 1)
            r.problems.push_back(std::format("SegmentBase@timescale {} but the Segment Index's is {}", r.timescale,
                                             s.timescale));
        r.timescale = s.timescale;
        r.pto = static_cast<std::int64_t>(std::llround(pto * s.timescale));
    }
    const double ts = static_cast<double>(r.timescale);
    bool nested = false;
    for (const Sidx::Ref& ref : s.refs) {
        if (ref.index) {
            nested = true;
            continue;
        }
        SegmentRef seg;
        seg.uri = r.index_uri;
        seg.range = { ref.offset, ref.size };
        seg.time = ref.time;
        seg.duration = ref.duration;
        seg.start = period_start + static_cast<double>(ref.time) / ts - pto;
        seg.seconds = static_cast<double>(ref.duration) / ts;
        seg.sap_type = ref.sap ? ref.sap_type : 0;
        r.segments.push_back(std::move(seg));
    }
    if (nested) r.problems.push_back("the Segment Index points to further sidx boxes, whose subsegments are not listed");
}

} // namespace dash
