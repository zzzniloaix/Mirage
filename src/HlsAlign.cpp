#include "HlsAlign.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <format>
#include <optional>
#include <string_view>

namespace hls {

namespace {

// ── Text helpers ─────────────────────────────────────────────────────────────

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

// Value of attribute `name` in an HLS attribute list (quotes stripped), or
// nullopt. Quoted values may contain commas.
std::optional<std::string> attr(std::string_view list, std::string_view name)
{
    std::size_t i = 0;
    while (i < list.size()) {
        while (i < list.size() && (list[i] == ' ' || list[i] == ',')) ++i;
        const std::size_t eq = list.find('=', i);
        if (eq == std::string_view::npos) return std::nullopt;
        const std::string_view key = trim(list.substr(i, eq - i));
        std::size_t v0 = eq + 1, v1;
        std::string value;
        if (v0 < list.size() && list[v0] == '"') {
            v1 = list.find('"', v0 + 1);
            if (v1 == std::string_view::npos) v1 = list.size();
            value.assign(list.substr(v0 + 1, v1 - v0 - 1));
            i = v1 + 1;
        } else {
            v1 = list.find(',', v0);
            if (v1 == std::string_view::npos) v1 = list.size();
            value.assign(trim(list.substr(v0, v1 - v0)));
            i = v1;
        }
        if (key == name) return value;
    }
    return std::nullopt;
}

double to_double(std::string_view s, double fallback = kNaN)
{
    const std::string t(trim(s));
    if (t.empty()) return fallback;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    return end == t.c_str() ? fallback : v;
}

std::int64_t to_int(std::string_view s, std::int64_t fallback = 0)
{
    const std::string t(trim(s));
    if (t.empty()) return fallback;
    char* end = nullptr;
    const long long v = std::strtoll(t.c_str(), &end, 10);
    return end == t.c_str() ? fallback : static_cast<std::int64_t>(v);
}

// "n[@o]" (EXT-X-BYTERANGE / EXT-X-MAP BYTERANGE). offset -1 if absent.
bool parse_range(std::string_view v, std::int64_t& len, std::int64_t& off)
{
    v = trim(v);
    const auto at = v.find('@');
    len = to_int(v.substr(0, at), -1);
    off = at == std::string_view::npos ? -1 : to_int(v.substr(at + 1), -1);
    return len >= 0;
}

// ── URI resolution (RFC 3986 §5.2) ───────────────────────────────────────────

struct UriParts {
    std::string scheme, authority, path, query, fragment;
    bool has_scheme = false, has_authority = false, has_query = false, has_fragment = false;
};

UriParts split_uri(std::string_view s)
{
    UriParts u;
    if (const auto h = s.find('#'); h != std::string_view::npos) {
        u.fragment.assign(s.substr(h + 1));
        u.has_fragment = true;
        s = s.substr(0, h);
    }
    if (const auto q = s.find('?'); q != std::string_view::npos) {
        u.query.assign(s.substr(q + 1));
        u.has_query = true;
        s = s.substr(0, q);
    }
    // scheme = ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) ":", before any '/'
    if (const auto c = s.find(':'); c != std::string_view::npos && c > 0 && c < s.find('/')
        && std::isalpha(static_cast<unsigned char>(s[0]))) {
        bool ok = true;
        for (std::size_t i = 0; i < c; ++i) {
            const char ch = s[i];
            if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '+' && ch != '-' && ch != '.') ok = false;
        }
        if (ok) {
            u.scheme.assign(s.substr(0, c));
            u.has_scheme = true;
            s = s.substr(c + 1);
        }
    }
    if (s.starts_with("//")) {
        s.remove_prefix(2);
        const auto slash = s.find('/');
        u.authority.assign(s.substr(0, slash));
        u.has_authority = true;
        s = slash == std::string_view::npos ? std::string_view() : s.substr(slash);
    }
    u.path.assign(s);
    return u;
}

std::string remove_dot_segments(std::string_view in)
{
    std::string out;
    while (!in.empty()) {
        if (in.starts_with("../"))      in.remove_prefix(3);
        else if (in.starts_with("./"))  in.remove_prefix(2);
        else if (in.starts_with("/./")) in.remove_prefix(2);
        else if (in == "/.")            in = "/";
        else if (in.starts_with("/../") || in == "/..") {
            in = in.size() > 3 ? in.substr(3) : std::string_view("/");
            const auto slash = out.rfind('/');
            out.erase(slash == std::string::npos ? 0 : slash);
        } else if (in == "." || in == "..") {
            in = {};
        } else {
            const std::size_t start = in[0] == '/' ? 1 : 0;
            const auto next = in.find('/', start);
            const std::size_t n = next == std::string_view::npos ? in.size() : next;
            out.append(in.substr(0, n));
            in.remove_prefix(n);
        }
    }
    return out;
}

std::string compose(const UriParts& u)
{
    std::string s;
    if (u.has_scheme) s += u.scheme + ":";
    if (u.has_authority) s += "//" + u.authority;
    s += u.path;
    if (u.has_query) s += "?" + u.query;
    if (u.has_fragment) s += "#" + u.fragment;
    return s;
}

// ── Binary readers ───────────────────────────────────────────────────────────

std::uint32_t be16(const std::uint8_t* p) { return (std::uint32_t(p[0]) << 8) | p[1]; }
std::uint32_t be32(const std::uint8_t* p)
{
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}
std::uint64_t be64(const std::uint8_t* p) { return (std::uint64_t(be32(p)) << 32) | be32(p + 4); }

constexpr std::uint32_t fourcc(const char (&s)[5])
{
    return (std::uint32_t(std::uint8_t(s[0])) << 24) | (std::uint32_t(std::uint8_t(s[1])) << 16)
         | (std::uint32_t(std::uint8_t(s[2])) << 8) | std::uint32_t(std::uint8_t(s[3]));
}

std::string fourcc_text(std::uint32_t v)
{
    std::string s(4, ' ');
    for (int i = 0; i < 4; ++i) {
        const char c = static_cast<char>((v >> (24 - 8 * i)) & 0xff);
        s[static_cast<std::size_t>(i)] = std::isprint(static_cast<unsigned char>(c)) ? c : '.';
    }
    return s;
}

// One ISO BMFF box. A box running past the data is returned clipped
// (truncated = true): a kept segment head ends mid-box.
struct Box {
    std::uint32_t        type = 0;
    const std::uint8_t*  body = nullptr;
    std::size_t          size = 0;     // body bytes available
    std::uint64_t        declared = 0; // the box's size field, header included
    bool                 truncated = false;
};

bool next_box(const std::uint8_t*& p, const std::uint8_t* end, Box& b)
{
    if (end - p < 8) return false;
    std::uint64_t size = be32(p);
    const std::uint32_t type = be32(p + 4);
    std::size_t hdr = 8;
    if (size == 1) {
        if (end - p < 16) return false;
        size = be64(p + 8);
        hdr = 16;
    } else if (size == 0) {
        size = static_cast<std::uint64_t>(end - p);
    }
    if (type == fourcc("uuid")) hdr += 16;
    if (size < hdr) return false;
    const auto avail = static_cast<std::uint64_t>(end - p);
    if (hdr > avail) return false;
    b.type      = type;
    b.declared  = size;
    b.truncated = size > avail;
    b.body      = p + hdr;
    b.size      = static_cast<std::size_t>((b.truncated ? avail : size) - hdr);
    p += b.truncated ? avail : size;
    return true;
}

// Child boxes of a container box body.
template <class F>
void for_children(const std::uint8_t* body, std::size_t size, F&& f)
{
    const std::uint8_t* p = body;
    const std::uint8_t* end = body + size;
    Box b;
    while (next_box(p, end, b))
        if (!f(b)) break;
}

bool find_child(const std::uint8_t* body, std::size_t size, std::uint32_t type, Box& out)
{
    bool found = false;
    for_children(body, size, [&](const Box& b) {
        if (b.type == type) { out = b; found = true; return false; }
        return true;
    });
    return found;
}

Track kind_of_handler(std::uint32_t h)
{
    if (h == fourcc("vide")) return Track::Video;
    if (h == fourcc("soun")) return Track::Audio;
    if (h == fourcc("text") || h == fourcc("sbtl") || h == fourcc("subt") || h == fourcc("clcp")) return Track::Text;
    if (h == fourcc("meta")) return Track::Data;
    return Track::Unknown;
}

// ── Time ─────────────────────────────────────────────────────────────────────

constexpr std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d)
{
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

// "hh:mm:ss.mmm" or "mm:ss.mmm" (WebVTT); NaN if malformed.
double parse_vtt_time(std::string_view s)
{
    s = trim(s);
    double parts[3] = { 0, 0, 0 };
    int n = 0;
    std::size_t i = 0;
    while (n < 3) {
        const auto colon = s.find(':', i);
        const std::string_view f = s.substr(i, colon == std::string_view::npos ? std::string_view::npos : colon - i);
        if (f.empty()) return kNaN;
        parts[n++] = to_double(f);
        if (std::isnan(parts[n - 1])) return kNaN;
        if (colon == std::string_view::npos) break;
        i = colon + 1;
    }
    if (n == 3) return parts[0] * 3600 + parts[1] * 60 + parts[2];
    if (n == 2) return parts[0] * 60 + parts[1];
    return kNaN;
}

constexpr double kTsWrap = 8589934592.0 / 90000.0;   // 2^33 ticks of 90 kHz, 26.5 h

} // namespace

// ── Public helpers ───────────────────────────────────────────────────────────

std::string resolve_uri(const std::string& base, const std::string& ref)
{
    if (ref.empty()) return base.substr(0, base.find('#'));
    const UriParts r = split_uri(ref);
    if (r.has_scheme) {
        UriParts t = r;
        t.path = remove_dot_segments(r.path);
        return compose(t);
    }
    const UriParts b = split_uri(base);
    if (!b.has_scheme) {
        // A local path: relative to its directory.
        if (ref[0] == '/') return remove_dot_segments(ref);
        const std::string dir = base.substr(0, base.rfind('/') + 1);
        return dir.starts_with('/') ? remove_dot_segments(dir + ref) : dir + ref;
    }
    UriParts t;
    t.scheme = b.scheme;
    t.has_scheme = true;
    if (r.has_authority) {
        t.authority = r.authority;
        t.has_authority = true;
        t.path = remove_dot_segments(r.path);
        t.query = r.query;
        t.has_query = r.has_query;
    } else {
        t.authority = b.authority;
        t.has_authority = b.has_authority;
        if (r.path.empty()) {
            t.path = b.path;
            t.query = r.has_query ? r.query : b.query;
            t.has_query = r.has_query || b.has_query;
        } else {
            if (r.path[0] == '/') {
                t.path = remove_dot_segments(r.path);
            } else {
                std::string merged = (b.has_authority && b.path.empty())
                                   ? "/" + r.path
                                   : b.path.substr(0, b.path.rfind('/') + 1) + r.path;
                t.path = remove_dot_segments(merged);
            }
            t.query = r.query;
            t.has_query = r.has_query;
        }
    }
    t.fragment = r.fragment;
    t.has_fragment = r.has_fragment;
    return compose(t);
}

double parse_iso8601(const std::string& in)
{
    const std::string_view s = trim(in);
    // YYYY-MM-DDThh:mm:ss[.f][Z|±hh[:mm]]
    if (s.size() < 19 || s[4] != '-' || s[7] != '-' || (s[10] != 'T' && s[10] != 't' && s[10] != ' ')
        || s[13] != ':' || s[16] != ':')
        return kNaN;
    auto num = [&](std::size_t at, std::size_t len) -> int {
        int v = 0;
        for (std::size_t i = at; i < at + len; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) return -1;
            v = v * 10 + (s[i] - '0');
        }
        return v;
    };
    const int y = num(0, 4), mo = num(5, 2), d = num(8, 2), h = num(11, 2), mi = num(14, 2), se = num(17, 2);
    if (y < 0 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 24 || mi < 0 || mi > 59 || se < 0 || se > 60)
        return kNaN;
    std::size_t i = 19;
    double frac = 0.0;
    if (i < s.size() && (s[i] == '.' || s[i] == ',')) {
        double scale = 0.1;
        ++i;
        while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
            frac += (s[i] - '0') * scale;
            scale *= 0.1;
            ++i;
        }
    }
    double tz = 0.0;
    if (i < s.size()) {
        if (s[i] == 'Z' || s[i] == 'z') {
            ++i;
        } else if (s[i] == '+' || s[i] == '-') {
            const int sign = s[i] == '-' ? -1 : 1;
            ++i;
            if (i + 2 > s.size()) return kNaN;
            const int th = num(i, 2);
            i += 2;
            int tm = 0;
            if (i < s.size() && s[i] == ':') ++i;
            if (i + 2 <= s.size()) { tm = num(i, 2); i += 2; }
            if (th < 0 || tm < 0) return kNaN;
            tz = sign * (th * 3600.0 + tm * 60.0);
        }
    }
    if (i != s.size()) return kNaN;
    const std::int64_t days = days_from_civil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d));
    return static_cast<double>(days) * 86400.0 + h * 3600.0 + mi * 60.0 + se + frac - tz;
}

std::string format_seconds(double s)
{
    if (std::isnan(s)) return "—";
    if (std::fabs(s) < 1.0) return std::format("{:.0f} ms", s * 1000.0);
    return std::format("{:.3f} s", s);
}

std::string format_clock(double s)
{
    if (std::isnan(s)) return "—";
    if (s >= kWallClockMedia) {
        // Seconds since 1970: the UTC time of day.
        const auto total_ms = static_cast<std::int64_t>(std::llround(s * 1000.0));
        const std::int64_t day_ms = total_ms % 86'400'000;
        return std::format("{:02}:{:02}:{:02}.{:03} UTC", day_ms / 3'600'000, day_ms / 60'000 % 60,
                           day_ms / 1000 % 60, day_ms % 1000);
    }
    const bool neg = s < 0;
    double a = std::fabs(s);
    const auto total_ms = static_cast<std::int64_t>(std::llround(a * 1000.0));
    const std::int64_t ms = total_ms % 1000, sec = (total_ms / 1000) % 60, min = (total_ms / 60000) % 60,
                       hr = total_ms / 3600000;
    if (hr > 0) return std::format("{}{}:{:02}:{:02}.{:03}", neg ? "-" : "", hr, min, sec, ms);
    return std::format("{}{}:{:02}.{:03}", neg ? "-" : "", min, sec, ms);
}

const char* track_name(Track t)
{
    switch (t) {
        case Track::Video: return "video";
        case Track::Audio: return "audio";
        case Track::Text:  return "subtitles";
        case Track::Data:  return "data";
        case Track::Unknown: break;
    }
    return "unknown";
}

const char* format_name(SegmentStart::Format f)
{
    switch (f) {
        case SegmentStart::Format::Fmp4:        return "fMP4";
        case SegmentStart::Format::Ts:          return "MPEG-TS";
        case SegmentStart::Format::PackedAudio: return "packed audio";
        case SegmentStart::Format::WebVtt:      return "WebVTT";
        case SegmentStart::Format::Unknown:     break;
    }
    return "unknown";
}

const char* severity_name(Severity s)
{
    switch (s) {
        case Severity::Error:   return "error";
        case Severity::Warning: return "warning";
        case Severity::Info:    return "info";
        case Severity::Ok:      return "ok";
    }
    return "?";
}

// ── MediaTimeline ────────────────────────────────────────────────────────────

bool MediaTimeline::has_pdt() const
{
    return std::any_of(segments.begin(), segments.end(), [](const Segment& s) { return s.pdt_explicit; });
}

int MediaTimeline::segment_at(double t) const
{
    if (segments.empty()) return -1;
    auto it = std::upper_bound(segments.begin(), segments.end(), t,
                               [](double v, const Segment& s) { return v < s.start; });
    if (it == segments.begin()) return -1;
    --it;
    return t < it->start + it->duration + 1e-9 ? static_cast<int>(it - segments.begin()) : -1;
}

int MediaTimeline::index_of(std::int64_t seq) const
{
    if (segments.empty()) return -1;
    const std::int64_t i = seq - segments.front().seq;
    if (i < 0 || i >= static_cast<std::int64_t>(segments.size())) return -1;
    return segments[static_cast<std::size_t>(i)].seq == seq ? static_cast<int>(i) : -1;
}

MediaTimeline parse_media_timeline(const std::string& text, const std::string& base_url)
{
    MediaTimeline tl;
    bool header = false;
    double pend_dur = -1.0;
    bool pend_disc = false, pend_gap = false, pend_range = false;
    double pend_pdt = kNaN;
    std::int64_t pend_len = -1, pend_off = -1;
    int cur_map = -1;
    std::string cur_key;
    std::int64_t seq = 0, disc = 0;
    bool seq_locked = false;           // a segment was listed: MEDIA-SEQUENCE is too late now
    double t = 0.0;
    std::string last_uri;
    std::int64_t last_end = -1;
    int over = 0;
    double over_max = 0.0;
    std::int64_t over_first = -1;
    bool uri_without_extinf = false;

    std::size_t pos = 0;
    int line_no = 0;
    while (pos <= text.size()) {
        const auto nl = text.find('\n', pos);
        std::string_view line(text.data() + pos, (nl == std::string::npos ? text.size() : nl) - pos);
        pos = nl == std::string::npos ? text.size() + 1 : nl + 1;
        ++line_no;
        if (line_no == 1 && line.starts_with("\xEF\xBB\xBF")) line.remove_prefix(3);
        line = trim(line);
        if (line.empty()) continue;
        if (!header) {
            if (!line.starts_with("#EXTM3U")) {
                tl.problems.push_back("not a playlist: no #EXTM3U header");
                return tl;
            }
            header = true;
            continue;
        }
        if (line[0] == '#') {
            if (!line.starts_with("#EXT")) continue;   // comment
            const auto colon = line.find(':');
            const std::string_view tag = line.substr(0, colon);
            const std::string_view val = colon == std::string_view::npos ? std::string_view() : line.substr(colon + 1);
            if (tag == "#EXT-X-STREAM-INF" || tag == "#EXT-X-I-FRAME-STREAM-INF" || tag == "#EXT-X-MEDIA") {
                tl.problems.push_back("a master playlist, not a media playlist");
                tl.segments.clear();
                return tl;
            }
            if (tag == "#EXTINF") {
                const auto comma = val.find(',');
                pend_dur = to_double(val.substr(0, comma), -1.0);
                if (pend_dur < 0) tl.problems.push_back(std::format("line {}: EXTINF without a duration", line_no));
            } else if (tag == "#EXT-X-TARGETDURATION") {
                tl.target = to_double(val, 0.0);
            } else if (tag == "#EXT-X-MEDIA-SEQUENCE") {
                if (!seq_locked) seq = tl.media_seq = to_int(val);
            } else if (tag == "#EXT-X-DISCONTINUITY-SEQUENCE") {
                if (!seq_locked) disc = tl.disc_seq = to_int(val);
            } else if (tag == "#EXT-X-VERSION") {
                tl.version = static_cast<int>(to_int(val));
            } else if (tag == "#EXT-X-PLAYLIST-TYPE") {
                tl.type.assign(trim(val));
            } else if (tag == "#EXT-X-ENDLIST") {
                tl.endlist = true;
            } else if (tag == "#EXT-X-INDEPENDENT-SEGMENTS") {
                tl.independent = true;
            } else if (tag == "#EXT-X-DISCONTINUITY") {
                pend_disc = true;
            } else if (tag == "#EXT-X-GAP") {
                pend_gap = true;
            } else if (tag == "#EXT-X-PROGRAM-DATE-TIME") {
                pend_pdt = parse_iso8601(std::string(val));
                if (std::isnan(pend_pdt))
                    tl.problems.push_back(std::format("line {}: PROGRAM-DATE-TIME \"{}\" does not parse", line_no, val));
            } else if (tag == "#EXT-X-BYTERANGE") {
                pend_range = parse_range(val, pend_len, pend_off);
            } else if (tag == "#EXT-X-MAP") {
                InitMap m;
                if (auto u = attr(val, "URI")) m.uri = resolve_uri(base_url, *u);
                if (auto br = attr(val, "BYTERANGE")) {
                    std::int64_t l = -1, o = -1;
                    if (parse_range(*br, l, o)) { m.range.length = l; m.range.offset = o < 0 ? 0 : o; }
                }
                tl.maps.push_back(std::move(m));
                cur_map = static_cast<int>(tl.maps.size()) - 1;
            } else if (tag == "#EXT-X-KEY") {
                cur_key = attr(val, "METHOD").value_or("");
                if (cur_key == "NONE") cur_key.clear();
            } else if (tag == "#EXT-X-SKIP") {
                const std::int64_t skipped = to_int(attr(val, "SKIPPED-SEGMENTS").value_or("0"));
                seq += skipped;
                tl.problems.push_back(std::format("a delta update (EXT-X-SKIP): {} segments not listed, times start after them",
                                                  skipped));
            }
            continue;
        }
        // URI line
        if (pend_dur < 0) {
            if (!uri_without_extinf)
                tl.problems.push_back(std::format("line {}: a URI without EXTINF", line_no));
            uri_without_extinf = true;
            continue;
        }
        Segment s;
        s.seq      = seq++;
        s.start    = t;
        s.duration = pend_dur;
        s.uri      = resolve_uri(base_url, std::string(line));
        s.line     = line_no;
        if (pend_range) {
            s.range.length = pend_len;
            if (pend_off >= 0) {
                s.range.offset = pend_off;
            } else if (s.uri == last_uri && last_end >= 0) {
                s.range.offset = last_end;
            } else {
                s.range.offset = 0;
                tl.problems.push_back(std::format("line {}: BYTERANGE without an offset does not follow a range of the same URI",
                                                  line_no));
            }
            last_end = s.range.offset + s.range.length;
        } else {
            last_end = -1;
        }
        last_uri = s.uri;
        if (pend_disc) {
            s.discontinuity = true;
            ++disc;
            ++tl.discontinuities;
        }
        s.disc_seq = disc;
        if (!std::isnan(pend_pdt)) {
            s.pdt = pend_pdt;
            s.pdt_explicit = true;
        } else if (!tl.segments.empty() && !pend_disc && !std::isnan(tl.segments.back().pdt)) {
            s.pdt = tl.segments.back().pdt + tl.segments.back().duration;
        }
        s.gap = pend_gap;
        s.map = cur_map;
        s.key = cur_key;
        if (tl.target > 0 && std::round(s.duration) > tl.target) {
            ++over;
            if (s.duration > over_max) over_max = s.duration;
            if (over_first < 0) over_first = s.seq;
        }
        t += s.duration;
        tl.segments.push_back(std::move(s));
        seq_locked = true;
        pend_dur = -1.0;
        pend_disc = pend_gap = pend_range = false;
        pend_pdt = kNaN;
        pend_len = pend_off = -1;
    }
    tl.total = t;
    tl.ok = !tl.segments.empty();
    if (!tl.ok && tl.problems.empty()) tl.problems.push_back("no media segments");
    if (tl.ok && tl.target <= 0) tl.problems.push_back("no EXT-X-TARGETDURATION");
    if (over > 0)
        tl.problems.push_back(std::format("{} segment{} longer than EXT-X-TARGETDURATION {:g} s (up to {:.3f} s, first #{})",
                                          over, over == 1 ? " is" : "s are", tl.target, over_max, over_first));
    return tl;
}

// ── fMP4 init segment ────────────────────────────────────────────────────────

const InitInfo::T* InitInfo::track(std::uint32_t id) const
{
    for (const auto& t : tracks)
        if (t.id == id) return &t;
    return nullptr;
}

InitInfo parse_init_segment(const std::uint8_t* data, std::size_t n)
{
    InitInfo info;
    if (!data || n < 8) return info;
    struct Elst { bool have = false; std::int64_t empty = 0; std::int64_t start = 0; };
    std::vector<Elst> elsts;
    Box moov{};
    bool have_moov = false;
    for_children(data, n, [&](const Box& b) {
        if (b.type == fourcc("moov")) { moov = b; have_moov = true; return false; }
        return true;
    });
    if (!have_moov) return info;

    for_children(moov.body, moov.size, [&](const Box& b) {
        if (b.type == fourcc("mvhd") && b.size >= 24) {
            info.movie_timescale = be32(b.body + (b.body[0] == 1 ? 20 : 12));
        } else if (b.type == fourcc("trak")) {
            InitInfo::T t;
            Elst e;
            for_children(b.body, b.size, [&](const Box& c) {
                if (c.type == fourcc("tkhd") && c.size >= 24) {
                    t.id = be32(c.body + (c.body[0] == 1 ? 20 : 12));
                } else if (c.type == fourcc("edts")) {
                    Box elst;
                    if (find_child(c.body, c.size, fourcc("elst"), elst) && elst.size >= 8) {
                        const bool v1 = elst.body[0] == 1;
                        const std::uint32_t count = be32(elst.body + 4);
                        const std::size_t esz = v1 ? 20 : 12;
                        // FFmpeg (mov_build_index): a leading empty edit is the
                        // stream's delay, the next edit's media_time its start.
                        std::size_t idx = 0;
                        for (std::uint32_t k = 0; k < count && 8 + (k + 1) * esz <= elst.size; ++k) {
                            const std::uint8_t* q = elst.body + 8 + k * esz;
                            const std::int64_t dur  = v1 ? static_cast<std::int64_t>(be64(q)) : be32(q);
                            const std::int64_t time = v1 ? static_cast<std::int64_t>(be64(q + 8))
                                                         : static_cast<std::int32_t>(be32(q + 4));
                            if (k == 0 && time == -1) { e.empty = dur; idx = 1; e.have = true; }
                            else if (k == idx && time >= 0) { e.start = time; e.have = true; }
                        }
                    }
                } else if (c.type == fourcc("mdia")) {
                    for_children(c.body, c.size, [&](const Box& m) {
                        if (m.type == fourcc("mdhd") && m.size >= 24) {
                            t.timescale = be32(m.body + (m.body[0] == 1 ? 20 : 12));
                        } else if (m.type == fourcc("hdlr") && m.size >= 12) {
                            t.kind = kind_of_handler(be32(m.body + 8));
                        } else if (m.type == fourcc("minf")) {
                            Box stbl, stsd;
                            if (find_child(m.body, m.size, fourcc("stbl"), stbl)
                                && find_child(stbl.body, stbl.size, fourcc("stsd"), stsd) && stsd.size >= 16) {
                                const std::uint8_t* entry = stsd.body + 8;
                                const std::size_t entry_avail = stsd.size - 8;
                                const std::uint32_t esize = be32(entry);
                                const std::uint32_t fmt = be32(entry + 4);
                                t.codec = fourcc_text(fmt);
                                // Encrypted sample entries name the original format in sinf/frma.
                                if (fmt == fourcc("encv") || fmt == fourcc("enca") || fmt == fourcc("enct")
                                    || fmt == fourcc("encs")) {
                                    const std::size_t len = std::min<std::size_t>(esize, entry_avail);
                                    for (const std::size_t skip : { std::size_t(8 + 78), std::size_t(8 + 28) }) {
                                        if (skip >= len) continue;
                                        Box sinf, frma;
                                        if (find_child(entry + skip, len - skip, fourcc("sinf"), sinf)
                                            && find_child(sinf.body, sinf.size, fourcc("frma"), frma) && frma.size >= 4) {
                                            t.codec = fourcc_text(be32(frma.body)) + " (encrypted)";
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        return true;
                    });
                }
                return true;
            });
            if (t.id != 0) {
                info.tracks.push_back(t);
                elsts.push_back(e);
            }
        }
        return true;
    });
    // trex (in mvex, before or after the traks): default sample durations.
    for_children(moov.body, moov.size, [&](const Box& b) {
        if (b.type != fourcc("mvex")) return true;
        for_children(b.body, b.size, [&](const Box& c) {
            if (c.type == fourcc("trex") && c.size >= 20)
                    for (auto& t : info.tracks)
                    if (t.id == be32(c.body + 4)) t.default_duration = be32(c.body + 12);
            return true;
        });
        return true;
    });
    for (std::size_t i = 0; i < info.tracks.size(); ++i) {
        const Elst& e = elsts[i];
        auto& t = info.tracks[i];
        if (!e.have) continue;
        std::int64_t empty = e.empty;
        if (empty && info.movie_timescale > 0 && t.timescale > 0)
            empty = empty * static_cast<std::int64_t>(t.timescale) / static_cast<std::int64_t>(info.movie_timescale);
        t.edit_shift = e.start - empty;
    }
    info.ok = !info.tracks.empty();
    return info;
}

// ── Segment start: fMP4 ──────────────────────────────────────────────────────

namespace {

std::string byte_text(std::uint64_t b)
{
    if (b < 1000) return std::format("{} B", b);
    if (b < 1000 * 1000) return std::format("{:.1f} kB", static_cast<double>(b) / 1e3);
    return std::format("{:.2f} MB", static_cast<double>(b) / 1e6);
}

bool looks_like_bmff(const std::uint8_t* d, std::size_t n)
{
    if (n < 8) return false;
    static constexpr std::uint32_t kTop[] = {
        fourcc("styp"), fourcc("sidx"), fourcc("moof"), fourcc("prft"), fourcc("emsg"), fourcc("free"),
        fourcc("skip"), fourcc("ftyp"), fourcc("moov"), fourcc("mdat"), fourcc("ssix"), fourcc("uuid"),
    };
    const std::uint32_t t = be32(d + 4);
    return std::find(std::begin(kTop), std::end(kTop), t) != std::end(kTop);
}

void parse_fmp4(const std::uint8_t* data, std::size_t n, const InitInfo* init, SegmentStart& out)
{
    out.format = SegmentStart::Format::Fmp4;
    const std::uint8_t* p = data;
    const std::uint8_t* end = data + n;
    Box moof{};
    bool have_moof = false;
    Box b;
    while (next_box(p, end, b)) {
        if (b.type == fourcc("moof")) { moof = b; have_moof = true; break; }
        if (b.type == fourcc("moov")) {
            out.error = "an init segment (moov), not a media segment";
            return;
        }
    }
    if (!have_moof) {
        out.error = std::format("no moof box in the first {} bytes", n);
        return;
    }
    for_children(moof.body, moof.size, [&](const Box& c) {
        if (c.type == fourcc("mfhd") && c.size >= 8) out.fragment_seq = be32(c.body + 4);
        if (c.type != fourcc("traf")) return true;

        std::uint32_t track_id = 0, def_dur = 0;
        bool have_tfdt = false;
        std::uint64_t tfdt = 0;
        std::int64_t dts_off = 0;              // decode time of the next sample past tfdt
        std::int64_t min_pts_off = INT64_MAX;  // min over samples of decode offset + cts
        std::int64_t min_cts = 0;
        bool any_sample = false, unknown_dur = false;
        for_children(c.body, c.size, [&](const Box& t) {
            if (t.type == fourcc("tfhd") && t.size >= 8) {
                const std::uint32_t flags = be32(t.body) & 0xffffff;
                track_id = be32(t.body + 4);
                std::size_t off = 8;
                if (flags & 0x01) off += 8;
                if (flags & 0x02) off += 4;
                if ((flags & 0x08) && off + 4 <= t.size) def_dur = be32(t.body + off);
            } else if (t.type == fourcc("tfdt") && t.size >= 8) {
                tfdt = t.body[0] == 1 && t.size >= 12 ? be64(t.body + 4) : be32(t.body + 4);
                have_tfdt = true;
            } else if (t.type == fourcc("trun") && t.size >= 8) {
                const std::uint32_t flags = be32(t.body) & 0xffffff;
                const std::uint32_t count = be32(t.body + 4);
                std::size_t off = 8;
                if (flags & 0x001) off += 4;
                if (flags & 0x004) off += 4;
                const std::size_t per = ((flags & 0x100) ? 4 : 0) + ((flags & 0x200) ? 4 : 0)
                                      + ((flags & 0x400) ? 4 : 0) + ((flags & 0x800) ? 4 : 0);
                std::uint32_t fallback = def_dur;
                if (!fallback && init)
                    if (const auto* it = init->track(track_id)) fallback = it->default_duration;
                for (std::uint32_t i = 0; i < count && (per == 0 ? i == 0 : off + per <= t.size); ++i) {
                    std::size_t q = off;
                    std::uint32_t dur = fallback;
                    if (flags & 0x100) { dur = be32(t.body + q); q += 4; }
                    if (flags & 0x200) q += 4;
                    if (flags & 0x400) q += 4;
                    // FFmpeg reads composition offsets as signed in both versions.
                    std::int32_t cts = 0;
                    if (flags & 0x800) { cts = static_cast<std::int32_t>(be32(t.body + q)); q += 4; }
                    min_pts_off = std::min<std::int64_t>(min_pts_off, dts_off + cts);
                    min_cts = std::min<std::int64_t>(min_cts, cts);
                    any_sample = true;
                    if (dur == 0 && count > 1) { unknown_dur = true; break; }
                    dts_off += dur;
                    off += per;
                }
            }
            return true;
        });

        SegmentStart::T tr;
        tr.id = track_id;
        const InitInfo::T* it = init ? init->track(track_id) : nullptr;
        if (it) {
            tr.kind  = it->kind;
            tr.codec = it->codec;
        }
        if (!have_tfdt) {
            tr.note = "no tfdt: the segment does not carry its decode time";
        } else if (!it || it->timescale == 0) {
            tr.note = std::format("tfdt {} — timescale unknown without the init segment", tfdt);
        } else {
            const double ts = it->timescale;
            const auto base = static_cast<std::int64_t>(tfdt) - it->edit_shift;
            // FFmpeg: pts = dts + dts_shift + cts, dts_shift = −(most negative cts).
            const std::int64_t shift = min_cts < 0 ? -min_cts : 0;
            tr.decode_start = static_cast<double>(base) / ts;
            tr.start = static_cast<double>(base + (any_sample ? min_pts_off : 0) + shift) / ts;
            if (it->edit_shift != 0)
                tr.note = std::format("edit list shifts it by {}", format_seconds(-static_cast<double>(it->edit_shift) / ts));
            if (unknown_dur) tr.note += std::string(tr.note.empty() ? "" : "; ") + "sample durations unknown: first sample only";
        }
        out.tracks.push_back(std::move(tr));
        return true;
    });
    if (out.tracks.empty()) out.error = "moof without traf";
    else if (!out.ok()) out.error = out.tracks.front().note;
}

// ── Segment start: MPEG-TS ───────────────────────────────────────────────────

std::int64_t read_pes_ts(const std::uint8_t* p)
{
    return (static_cast<std::int64_t>((p[0] >> 1) & 0x07) << 30) | (static_cast<std::int64_t>(p[1]) << 22)
         | (static_cast<std::int64_t>(p[2] >> 1) << 15) | (static_cast<std::int64_t>(p[3]) << 7)
         | static_cast<std::int64_t>(p[4] >> 1);
}

struct TsEs { Track kind = Track::Unknown; std::string codec; };

TsEs ts_stream_type(int type, const std::uint8_t* desc, std::size_t desc_len)
{
    switch (type) {
        case 0x01: case 0x02: return { Track::Video, "mpeg2video" };
        case 0x10: return { Track::Video, "mpeg4" };
        case 0x1B: return { Track::Video, "h264" };
        case 0x24: return { Track::Video, "hevc" };
        case 0x33: return { Track::Video, "vvc" };
        case 0xDB: return { Track::Video, "h264 (SAMPLE-AES)" };
        case 0x03: case 0x04: return { Track::Audio, "mp3" };
        case 0x0F: return { Track::Audio, "aac" };
        case 0x11: return { Track::Audio, "aac_latm" };
        case 0x81: return { Track::Audio, "ac3" };
        case 0x87: return { Track::Audio, "eac3" };
        case 0xCF: return { Track::Audio, "aac (SAMPLE-AES)" };
        case 0xC1: return { Track::Audio, "ac3 (SAMPLE-AES)" };
        case 0xC2: return { Track::Audio, "eac3 (SAMPLE-AES)" };
        case 0x15: return { Track::Data, "id3" };
        case 0x06: {
            for (std::size_t i = 0; i + 2 <= desc_len; i += 2u + desc[i + 1]) {
                const int tag = desc[i];
                if (tag == 0x6A) return { Track::Audio, "ac3" };
                if (tag == 0x7A) return { Track::Audio, "eac3" };
                if (tag == 0x59 || tag == 0x56) return { Track::Text, tag == 0x59 ? "dvb_subtitle" : "teletext" };
            }
            return { Track::Data, "private" };
        }
        default: break;
    }
    return { Track::Unknown, std::format("stream_type 0x{:02x}", type) };
}

bool find_ts_sync(const std::uint8_t* d, std::size_t n, std::size_t& at)
{
    for (std::size_t i = 0; i < std::min<std::size_t>(n, 188); ++i) {
        if (d[i] != 0x47) continue;
        if (i + 188 >= n || d[i + 188] == 0x47) { at = i; return true; }
    }
    return false;
}

void parse_ts(const std::uint8_t* data, std::size_t n, std::size_t sync, SegmentStart& out)
{
    out.format = SegmentStart::Format::Ts;
    int pmt_pid = -1;
    bool saw_pat = false, saw_pmt = false;
    std::map<int, TsEs> es;
    struct Pid { std::int64_t min_pts = -1, first = -1, first_dts = -1; int stream_id = 0; };
    std::map<int, Pid> pids;
    std::vector<int> order;                    // PIDs in order of their first PES
    std::map<int, std::vector<std::uint8_t>> sect;   // PSI being assembled

    auto parse_psi = [&](int pid, const std::vector<std::uint8_t>& s) {
        if (s.size() < 3) return;
        const std::size_t len = ((s[1] & 0x0f) << 8) | s[2];
        if (s.size() < 3 + len || len < 9) return;
        const std::size_t end = 3 + len - 4;   // CRC
        if (pid == 0 && s[0] == 0x00) {
            saw_pat = true;
            for (std::size_t i = 8; i + 4 <= end; i += 4) {
                const int prog = static_cast<int>(be16(&s[i]));
                const int p = static_cast<int>(be16(&s[i + 2]) & 0x1fff);
                if (prog != 0) { pmt_pid = p; break; }
            }
        } else if (pid == pmt_pid && s[0] == 0x02) {
            saw_pmt = true;
            const std::size_t pinfo = ((s[10] & 0x0f) << 8) | s[11];
            std::size_t i = 12 + pinfo;
            while (i + 5 <= end) {
                const int type = s[i];
                const int epid = static_cast<int>(be16(&s[i + 1]) & 0x1fff);
                const std::size_t elen = ((s[i + 3] & 0x0f) << 8) | s[i + 4];
                if (i + 5 + elen > end) break;
                es[epid] = ts_stream_type(type, &s[i + 5], elen);
                i += 5 + elen;
            }
        }
    };

    for (std::size_t off = sync; off + 188 <= n; off += 188) {
        const std::uint8_t* pk = data + off;
        if (pk[0] != 0x47) {
            // Lost sync (a truncated or damaged head): stop rather than guess.
            break;
        }
        const int pid = static_cast<int>(((pk[1] & 0x1f) << 8) | pk[2]);
        const bool pusi = (pk[1] & 0x40) != 0;
        const int afc = (pk[3] >> 4) & 0x3;
        if (!(afc & 1)) continue;
        std::size_t pl = 4;
        if (afc & 2) pl += 1 + pk[4];
        if (pl >= 188) continue;
        const std::uint8_t* pay = pk + pl;
        const std::size_t pay_len = 188 - pl;

        if (pid == 0 || pid == pmt_pid) {
            auto& acc = sect[pid];
            if (pusi) {
                const std::size_t ptr = pay[0];
                if (1 + ptr >= pay_len) continue;
                acc.assign(pay + 1 + ptr, pay + pay_len);
            } else if (!acc.empty()) {
                acc.insert(acc.end(), pay, pay + pay_len);
            } else {
                continue;
            }
            const std::size_t want = acc.size() >= 3 ? 3 + (((acc[1] & 0x0f) << 8) | acc[2]) : SIZE_MAX;
            if (acc.size() >= want) {
                parse_psi(pid, acc);
                acc.clear();
            }
            continue;
        }
        if (!pusi || pay_len < 9 || pay[0] != 0 || pay[1] != 0 || pay[2] != 1) continue;
        const int sid = pay[3];
        if (sid == 0xBC || sid == 0xBE || sid == 0xBF || sid == 0xF0 || sid == 0xF1 || sid == 0xFF
            || sid == 0xF2 || sid == 0xF8)
            continue;
        const int flags = pay[7] >> 6;
        if (!(flags & 2) || pay_len < 14) continue;
        const std::int64_t pts = read_pes_ts(pay + 9);
        std::int64_t dts = pts;
        if (flags == 3 && pay_len >= 19) dts = read_pes_ts(pay + 14);
        auto [it, fresh] = pids.try_emplace(pid);
        Pid& st = it->second;
        if (fresh) {
            order.push_back(pid);
            st.stream_id = sid;
            st.first = pts;
            st.first_dts = dts;
            st.min_pts = pts;
        } else {
            // Unwrap the 33-bit clock against the first PES of this PID.
            std::int64_t v = pts;
            if (v - st.first > (1LL << 32)) v -= 1LL << 33;
            else if (st.first - v > (1LL << 32)) v += 1LL << 33;
            st.min_pts = std::min(st.min_pts, v);
        }
    }

    for (const int pid : order) {
        const Pid& st = pids[pid];
        SegmentStart::T tr;
        tr.id = static_cast<std::uint32_t>(pid);
        if (auto e = es.find(pid); e != es.end()) {
            tr.kind  = e->second.kind;
            tr.codec = e->second.codec;
        } else {
            // No PMT before it: the PES stream id says video / audio.
            tr.kind = (st.stream_id & 0xF0) == 0xE0 ? Track::Video
                    : (st.stream_id & 0xE0) == 0xC0 || st.stream_id == 0xBD ? Track::Audio : Track::Unknown;
            tr.note = "no PMT before it: kind from the PES stream id";
        }
        tr.start        = static_cast<double>(st.min_pts) / 90000.0;
        tr.decode_start = static_cast<double>(st.first_dts) / 90000.0;
        out.tracks.push_back(std::move(tr));
    }
    if (out.tracks.empty())
        out.error = std::format("no PES with a PTS in the first {} bytes{}", n,
                                saw_pat ? (saw_pmt ? "" : " (PAT but no PMT)") : " (no PAT/PMT either)");
    else if (!saw_pat || !saw_pmt)
        for (auto& t : out.tracks)
            if (t.note.empty()) t.note = "the segment does not start with a PAT/PMT";
}

// ── Segment start: packed audio (ID3 timestamp) and WebVTT ───────────────────

std::uint32_t syncsafe(const std::uint8_t* p)
{
    return (std::uint32_t(p[0] & 0x7f) << 21) | (std::uint32_t(p[1] & 0x7f) << 14)
         | (std::uint32_t(p[2] & 0x7f) << 7) | std::uint32_t(p[3] & 0x7f);
}

std::string audio_sync_codec(const std::uint8_t* p, std::size_t n)
{
    if (n >= 2 && p[0] == 0xFF && (p[1] & 0xF6) == 0xF0) return "aac";
    if (n >= 6 && p[0] == 0x0B && p[1] == 0x77) return (p[5] >> 3) > 10 ? "eac3" : "ac3";
    if (n >= 2 && p[0] == 0xFF && (p[1] & 0xE0) == 0xE0) return "mp3";
    return {};
}

void parse_packed_audio(const std::uint8_t* data, std::size_t n, SegmentStart& out)
{
    out.format = SegmentStart::Format::PackedAudio;
    SegmentStart::T tr;
    tr.kind = Track::Audio;
    if (n >= 10 && std::memcmp(data, "ID3", 3) == 0) {
        const int ver = data[3];
        const std::size_t tag_end = std::min<std::size_t>(n, 10 + syncsafe(data + 6));
        std::size_t i = 10;
        if (data[5] & 0x40 && i + 4 <= tag_end)   // extended header
            i += ver >= 4 ? syncsafe(data + i) : 4 + be32(data + i);
        while (i + 10 <= tag_end) {
            const std::uint8_t* f = data + i;
            if (f[0] == 0) break;               // padding
            const std::size_t fsize = ver >= 4 ? syncsafe(f + 4) : be32(f + 4);
            const std::uint8_t* fp = f + 10;
            const std::size_t avail = std::min(fsize, tag_end - (i + 10));
            if (std::memcmp(f, "PRIV", 4) == 0) {
                static constexpr char kOwner[] = "com.apple.streaming.transportStreamTimestamp";
                const std::size_t ol = sizeof(kOwner);   // including the NUL
                if (avail >= ol + 8 && std::memcmp(fp, kOwner, ol) == 0) {
                    const std::uint64_t ts = be64(fp + ol) & 0x1FFFFFFFFULL;
                    tr.start = tr.decode_start = static_cast<double>(ts) / 90000.0;
                }
            }
            i += 10 + fsize;
        }
        tr.codec = audio_sync_codec(data + tag_end, n - tag_end);
        if (std::isnan(tr.start)) out.error = "ID3 tag without a transportStreamTimestamp PRIV frame";
    } else {
        tr.codec = audio_sync_codec(data, n);
        out.error = "packed audio without an ID3 timestamp (RFC 8216 §3.4 requires one)";
    }
    out.tracks.push_back(std::move(tr));
}

void parse_webvtt(const std::uint8_t* data, std::size_t n, SegmentStart& out)
{
    out.format = SegmentStart::Format::WebVtt;
    std::string_view s(reinterpret_cast<const char*>(data), n);
    if (s.starts_with("\xEF\xBB\xBF")) s.remove_prefix(3);
    SegmentStart::T tr;
    tr.kind = Track::Text;
    double mpegts = kNaN, local = kNaN;
    double first_cue = kNaN;
    bool in_header = true;
    std::size_t pos = 0;
    while (pos < s.size()) {
        const auto nl = s.find('\n', pos);
        std::string_view line = trim(s.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos));
        pos = nl == std::string_view::npos ? s.size() : nl + 1;
        if (in_header) {
            if (line.empty()) { in_header = false; continue; }
            if (line.starts_with("X-TIMESTAMP-MAP=")) {
                std::string_view v = line.substr(16);
                while (!v.empty()) {
                    const auto comma = v.find(',');
                    const std::string_view item = trim(v.substr(0, comma));
                    if (item.starts_with("MPEGTS:")) mpegts = to_double(item.substr(7));
                    else if (item.starts_with("LOCAL:")) local = parse_vtt_time(item.substr(6));
                    if (comma == std::string_view::npos) break;
                    v.remove_prefix(comma + 1);
                }
            }
            continue;
        }
        if (const auto arrow = line.find("-->"); arrow != std::string_view::npos) {
            first_cue = parse_vtt_time(line.substr(0, arrow));
            break;
        }
    }
    if (!std::isnan(mpegts) && !std::isnan(local)) {
        tr.local_zero = mpegts / 90000.0 - local;
    } else {
        tr.local_zero = 0.0;
        tr.note = "no X-TIMESTAMP-MAP: cue times taken as MPEGTS 0";
    }
    if (!std::isnan(first_cue)) tr.start = first_cue + tr.local_zero;
    out.tracks.push_back(std::move(tr));
}

} // namespace

std::string describe_boxes(const std::uint8_t* data, std::size_t n)
{
    if (!data || !looks_like_bmff(data, n)) return {};
    std::string out;
    const std::uint8_t* p = data;
    const std::uint8_t* end = data + n;
    Box b;
    while (next_box(p, end, b)) {
        if (!out.empty()) out += " \u00B7 ";
        out += fourcc_text(b.type) + " " + byte_text(b.declared);
        if (b.type == fourcc("moov") || b.type == fourcc("moof") || b.type == fourcc("traf")) {
            std::string kids;
            for_children(b.body, b.size, [&](const Box& c) {
                if (!kids.empty()) kids += " ";
                kids += fourcc_text(c.type);
                return true;
            });
            if (!kids.empty()) out += " (" + kids + ")";
        }
        if (b.truncated) out += std::format(" ({} here)", byte_text(b.size));
    }
    return out;
}

bool SegmentStart::ok() const
{
    return std::any_of(tracks.begin(), tracks.end(),
                       [](const T& t) { return !std::isnan(t.start) || !std::isnan(t.local_zero); });
}

double SegmentStart::start(Track kind) const
{
    double best = kNaN;
    for (const auto& t : tracks)
        if (t.kind == kind && !std::isnan(t.start) && (std::isnan(best) || t.start < best)) best = t.start;
    return best;
}

const SegmentStart::T* SegmentStart::first(Track kind) const
{
    for (const auto& t : tracks)
        if (t.kind == kind && (!std::isnan(t.start) || !std::isnan(t.local_zero))) return &t;
    return nullptr;
}

SegmentStart parse_segment_start(const std::uint8_t* data, std::size_t n, const InitInfo* init)
{
    SegmentStart out;
    if (!data || n == 0) {
        out.error = "no data";
        return out;
    }
    std::size_t sync = 0;
    std::string_view head(reinterpret_cast<const char*>(data), std::min<std::size_t>(n, 16));
    if (head.starts_with("\xEF\xBB\xBF")) head.remove_prefix(3);
    if (head.starts_with("WEBVTT")) {
        parse_webvtt(data, n, out);
    } else if (n >= 3 && std::memcmp(data, "ID3", 3) == 0) {
        parse_packed_audio(data, n, out);
    } else if (find_ts_sync(data, n, sync)) {
        parse_ts(data, n, sync, out);
    } else if (looks_like_bmff(data, n)) {
        parse_fmp4(data, n, init, out);
    } else if (!audio_sync_codec(data, n).empty()) {
        parse_packed_audio(data, n, out);
    } else {
        out.error = "not a media segment format this reads (fMP4, MPEG-TS, packed audio, WebVTT)";
    }
    return out;
}

// ── Alignment ────────────────────────────────────────────────────────────────

namespace {

struct Probe {
    std::int64_t seq = 0;
    double       at = 0.0;        // playlist time of the segment
    double       offset = 0.0;    // media time − playlist time
    std::int64_t period = 0;      // discontinuity sequence number
};

// Media offsets read from rendition `r`'s segments, ascending sequence.
std::vector<Probe> probes_of(const RenditionIn& r)
{
    std::vector<Probe> out;
    for (const auto& [seq, st] : r.starts) {
        const int i = r.tl.index_of(seq);
        if (i < 0) continue;
        const Segment& seg = r.tl.segments[static_cast<std::size_t>(i)];
        Probe p;
        p.seq = seq;
        p.at = seg.start;
        p.period = seg.disc_seq;
        if (r.kind == Track::Text) {
            const auto* t = st.first(Track::Text);
            if (!t || std::isnan(t->local_zero)) continue;
            p.offset = t->local_zero;       // cue times are playlist times
        } else {
            const double media = st.start(r.kind == Track::Audio ? Track::Audio : Track::Video);
            const double any = std::isnan(media) ? st.start(Track::Audio) : media;   // audio-only variant
            if (std::isnan(any)) continue;
            p.offset = any - seg.start;
        }
        out.push_back(p);
    }
    return out;
}

// Difference of two media offsets, unwrapping the 33-bit MPEG-TS clock.
double offset_delta(double a, double b)
{
    double d = a - b;
    if (std::fabs(d) > kTsWrap / 2) d -= std::round(d / kTsWrap) * kTsWrap;
    return d;
}

std::string lag_text(double d)   // d > 0: later
{
    return std::format("{} {}", format_seconds(std::fabs(d)), d > 0 ? "later" : "earlier");
}

} // namespace

AlignReport analyze_alignment(const std::vector<RenditionIn>& in)
{
    AlignReport rep;
    rep.rows.resize(in.size());
    const int n = static_cast<int>(in.size());
    auto add = [&](Severity sev, std::string title, std::string detail, std::vector<int> who, double t = kNaN) {
        for (const int w : who)
            if (w >= 0 && w < n && static_cast<int>(sev) < static_cast<int>(rep.rows[static_cast<std::size_t>(w)].worst))
                rep.rows[static_cast<std::size_t>(w)].worst = sev;
        rep.findings.push_back({ sev, std::move(title), std::move(detail), std::move(who), t });
    };

    // Reference: the video being played, else any video, else the first playlist read.
    for (int pass = 0; pass < 3 && rep.reference < 0; ++pass)
        for (int i = 0; i < n; ++i) {
            const auto& r = in[static_cast<std::size_t>(i)];
            if (!r.tl.ok) continue;
            if ((pass == 0 && r.kind == Track::Video && r.in_use) || (pass == 1 && r.kind == Track::Video) || pass == 2) {
                rep.reference = i;
                break;
            }
        }

    // ── Each playlist on its own ─────────────────────────────────────────────
    for (int i = 0; i < n; ++i) {
        const auto& r = in[static_cast<std::size_t>(i)];
        if (!r.tl.ok) {
            add(Severity::Error, std::format("{}: playlist not readable", r.label),
                r.tl.problems.empty() ? std::string("No media segments.") : r.tl.problems.front(), { i });
            continue;
        }
        for (const auto& p : r.tl.problems)
            add(Severity::Warning, std::format("{}: {}", r.label, p), "A spec violation (RFC 8216).", { i });
        int gaps = 0;
        for (const auto& s : r.tl.segments) gaps += s.gap ? 1 : 0;
        if (gaps)
            add(Severity::Info, std::format("{}: {} segment{} marked EXT-X-GAP", r.label, gaps, gaps == 1 ? "" : "s"),
                "A player skips them or shows a gap in this rendition only.", { i });
        // PROGRAM-DATE-TIME that jumps where the playlist is continuous.
        for (std::size_t k = 1; k < r.tl.segments.size(); ++k) {
            const auto& a = r.tl.segments[k - 1];
            const auto& b = r.tl.segments[k];
            if (!b.pdt_explicit || b.discontinuity || std::isnan(a.pdt)) continue;
            const double jump = b.pdt - (a.pdt + a.duration);
            if (std::fabs(jump) > kAlignBad) {
                add(Severity::Warning,
                    std::format("{}: PROGRAM-DATE-TIME jumps {} at segment #{}", r.label, format_seconds(jump), b.seq),
                    std::format("Segment #{} is dated {} after #{} ends, with no EXT-X-DISCONTINUITY between them. "
                                "Players that place renditions by date put this one {} off from here on.",
                                b.seq, format_seconds(jump), a.seq, format_seconds(std::fabs(jump))),
                    { i }, b.start);
                break;
            }
        }
    }
    if (rep.reference < 0) return rep;
    const int ref_i = rep.reference;
    const RenditionIn& ref = in[static_cast<std::size_t>(ref_i)];

    // ── Placement on the reference's timeline ───────────────────────────────
    // VOD / EVENT playlists all start at the start of the presentation. Live
    // windows (each fetched at its own moment) are placed by
    // PROGRAM-DATE-TIME when both carry it, else by the media sequence
    // number of a segment both list.
    bool by_seq = false, live_unplaced = false;
    for (int i = 0; i < n; ++i) {
        const auto& r = in[static_cast<std::size_t>(i)];
        if (!r.tl.ok || i == ref_i || !r.tl.live() || !ref.tl.live()) continue;
        const auto& a = r.tl.segments.front();
        const auto& b = ref.tl.segments.front();
        double& shift = rep.rows[static_cast<std::size_t>(i)].shift;
        if (!std::isnan(a.pdt) && !std::isnan(b.pdt)) {
            shift = a.pdt - b.pdt;
        } else if (const int k = ref.tl.index_of(a.seq); k >= 0) {
            shift = ref.tl.segments[static_cast<std::size_t>(k)].start - a.start;
            by_seq = true;
        } else if (const int k2 = r.tl.index_of(b.seq); k2 >= 0) {
            shift = b.start - r.tl.segments[static_cast<std::size_t>(k2)].start;
            by_seq = true;
        } else {
            live_unplaced = true;
        }
    }
    if (by_seq)
        add(Severity::Info, "Live playlists without PROGRAM-DATE-TIME are placed by media sequence number",
            "Their sliding windows are lined up on a segment both list, assuming the same sequence number holds the "
            "same moment in each — packagers usually number renditions together, but RFC 8216 does not promise it.",
            {});
    if (live_unplaced)
        add(Severity::Info, "Live playlists without PROGRAM-DATE-TIME are compared from their first segments",
            "No segment is listed by both under one sequence number: their sliding windows cannot be placed "
            "against each other exactly.", {});

    // ── Playlist against the reference ───────────────────────────────────────
    for (int i = 0; i < n; ++i) {
        const auto& r = in[static_cast<std::size_t>(i)];
        if (!r.tl.ok || i == ref_i) continue;
        auto& row = rep.rows[static_cast<std::size_t>(i)];
        const double shift = row.shift;

        if (r.tl.live() != ref.tl.live()) {
            add(Severity::Error,
                std::format("{} is {} but {} is {}", r.label, r.tl.live() ? "live" : "VOD", ref.label,
                            ref.tl.live() ? "live" : "VOD"),
                "One playlist has EXT-X-ENDLIST and the other does not: the player keeps reloading one and "
                "treats the other as complete, so they drift apart or one stops.", { i, ref_i });
        }

        // Duration (complete playlists).
        if (!r.tl.live() && !ref.tl.live()) {
            const double d = r.tl.total - ref.tl.total;
            const double target = std::max(r.tl.target, ref.tl.target);
            if (std::fabs(d) > std::max(1.0, 0.5 * target)) {
                const bool shorter = d < 0;
                std::string what;
                if (r.kind == Track::Audio && ref.kind == Track::Video)
                    what = shorter ? std::format("The last {} of the video play without sound.", format_seconds(-d))
                                   : std::format("Sound continues {} past the last video frame (frozen or black picture).",
                                                 format_seconds(d));
                else
                    what = std::format("The end of the presentation differs by {}.", format_seconds(std::fabs(d)));
                add(std::fabs(d) > 2.0 * std::max(target, 1.0) ? Severity::Error : Severity::Warning,
                    std::format("{} is {} {} than {}", r.label, format_seconds(std::fabs(d)), shorter ? "shorter" : "longer",
                                ref.label),
                    std::format("{} lasts {} ({} segments), {} {} ({} segments). {}", r.label, format_clock(r.tl.total),
                                r.tl.segments.size(), ref.label, format_clock(ref.tl.total), ref.tl.segments.size(), what),
                    { i, ref_i }, std::min(r.tl.total, ref.tl.total));
            }
        }

        // Discontinuities: same count, same places. Live windows: the ones
        // both windows cover.
        std::vector<double> da, db;
        const bool both_live = r.tl.live() && ref.tl.live();
        const double lo = both_live ? std::max(r.tl.segments.front().start + shift, ref.tl.segments.front().start) : -1e300;
        const double hi = both_live ? std::min(r.tl.total + shift, ref.tl.total) : 1e300;
        for (const auto& s : r.tl.segments)
            if (s.discontinuity && s.start + shift >= lo && s.start + shift <= hi) da.push_back(s.start + shift);
        for (const auto& s : ref.tl.segments)
            if (s.discontinuity && s.start >= lo && s.start <= hi) db.push_back(s.start);
        if (da.size() != db.size()) {
            std::string list_a, list_b;
            for (double t : da) list_a += (list_a.empty() ? "" : ", ") + format_clock(t);
            for (double t : db) list_b += (list_b.empty() ? "" : ", ") + format_clock(t);
            add(Severity::Error,
                std::format("{} has {} discontinuit{}, {} has {}", r.label, da.size(), da.size() == 1 ? "y" : "ies",
                            ref.label, db.size()),
                std::format("{}: {}. {}: {}. Each EXT-X-DISCONTINUITY starts a new timeline (an ad, a splice); "
                            "a rendition without the matching one keeps the old content and timestamps, so the "
                            "player shows one period's picture with another's sound, or stalls waiting for "
                            "timestamps that never come.",
                            r.label, list_a.empty() ? "none" : list_a, ref.label, list_b.empty() ? "none" : list_b),
                { i, ref_i }, da.empty() ? (db.empty() ? kNaN : db.front()) : da.front());
        } else if (da.size() == db.size()) {
            for (std::size_t k = 0; k < da.size(); ++k) {
                const double d = da[k] - db[k];
                if (std::fabs(d) <= 0.25) continue;
                add(std::fabs(d) > 1.0 ? Severity::Error : Severity::Warning,
                    std::format("Discontinuity {} is {} apart in {} and {}", k + 1, format_seconds(std::fabs(d)), r.label,
                                ref.label),
                    std::format("At {} in {}, at {} in {}. Between the two points one rendition has crossed into "
                                "the new period and the other has not: mismatched content (an ad's picture with "
                                "the programme's sound), then a jump.",
                                format_clock(da[k]), r.label, format_clock(db[k]), ref.label),
                    { i, ref_i }, std::min(da[k], db[k]));
            }
        }
        if (r.tl.live() && ref.tl.live() && r.tl.disc_seq != ref.tl.disc_seq && std::fabs(shift) < ref.tl.target)
            add(Severity::Warning,
                std::format("EXT-X-DISCONTINUITY-SEQUENCE {} in {}, {} in {}", r.tl.disc_seq, r.label, ref.tl.disc_seq,
                            ref.label),
                "Windows that start at the same time should count the same discontinuities (RFC 8216 §6.2.2); "
                "players that pair periods by this number pair the wrong ones.", { i, ref_i });

        // PROGRAM-DATE-TIME at the same playlist time (complete playlists).
        if (!r.tl.live() && r.tl.has_pdt() && ref.tl.has_pdt()) {
            for (const auto& b : ref.tl.segments) {
                if (!b.pdt_explicit) continue;
                const int k = r.tl.segment_at(b.start + 1e-6);
                if (k < 0) break;
                const auto& a = r.tl.segments[static_cast<std::size_t>(k)];
                if (std::isnan(a.pdt)) break;
                const double d = (a.pdt + (b.start - a.start)) - b.pdt;
                if (std::fabs(d) > kAlignBad)
                    add(Severity::Error,
                        std::format("PROGRAM-DATE-TIME of {} is {} off {}", r.label, format_seconds(d), ref.label),
                        std::format("At {} the playlists date the same position {} apart. Players that pair renditions "
                                    "by date misalign them by that much.", format_clock(b.start), format_seconds(std::fabs(d))),
                        { i, ref_i }, b.start);
                break;
            }
        }

        // Segment boundaries.
        double worst = 0.0;
        int mismatched = 0;
        for (const auto& s : r.tl.segments) {
            const double t = s.start + shift;
            const int k = ref.tl.segment_at(t);
            if (k < 0) continue;
            const auto& b = ref.tl.segments[static_cast<std::size_t>(k)];
            const double d = std::min(std::fabs(t - b.start), std::fabs(b.start + b.duration - t));
            worst = std::max(worst, d);
            if (d > 0.05) ++mismatched;
        }
        row.max_boundary_delta = worst;
        if (r.kind == Track::Video && ref.kind == Track::Video && mismatched > 0 && worst > kAlignOk)
            add(Severity::Warning,
                std::format("{} segment boundaries differ from {} by up to {}", r.label, ref.label, format_seconds(worst)),
                std::format("{} of {} boundaries do not line up. Variants of one presentation should cut at the same "
                            "times: a switch at a misaligned boundary skips or repeats up to {}.",
                            mismatched, r.tl.segments.size(), format_seconds(worst)),
                { i, ref_i });
    }

    // ── Media timestamps ─────────────────────────────────────────────────────
    std::vector<std::vector<Probe>> probes(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        const auto& r = in[static_cast<std::size_t>(i)];
        if (!r.tl.ok) continue;
        probes[static_cast<std::size_t>(i)] = probes_of(r);
        rep.rows[static_cast<std::size_t>(i)].probed = static_cast<int>(probes[static_cast<std::size_t>(i)].size());
        if (!probes[static_cast<std::size_t>(i)].empty())
            rep.rows[static_cast<std::size_t>(i)].offset = probes[static_cast<std::size_t>(i)].front().offset;

        // Within one rendition: steady offset inside a period.
        const auto& ps = probes[static_cast<std::size_t>(i)];
        for (std::size_t k = 1; k < ps.size(); ++k) {
            const Probe& a = ps[k - 1];
            const Probe& b = ps[k];
            if (a.period != b.period || r.kind == Track::Text) continue;
            const double d = offset_delta(b.offset, a.offset);
            if (std::fabs(d) <= kAlignOk) continue;
            const bool adjacent = b.seq == a.seq + 1;
            if (std::fabs(d) > kAlignBad && adjacent)
                add(Severity::Error,
                    std::format("{}: timestamps jump {} at segment #{} without a discontinuity", r.label,
                                format_seconds(d), b.seq),
                    std::format("Segment #{} should start where #{} ends, but its media timestamps are {} off. "
                                "With no EXT-X-DISCONTINUITY the player keeps the timeline and stalls or skips {} there.",
                                b.seq, a.seq, format_seconds(d), format_seconds(std::fabs(d))),
                    { i }, b.at + rep.rows[static_cast<std::size_t>(i)].shift);
            else
                add(std::fabs(d) > kAlignBad ? Severity::Error : Severity::Warning,
                    std::format("{}: media timestamps drift {} from EXTINF between #{} and #{}", r.label,
                                format_seconds(d), a.seq, b.seq),
                    std::format("The playlist puts #{} {} after #{}, the timestamps {}. EXTINF durations that do not "
                                "match the media make seeks land off target and renditions slide apart.",
                                b.seq, format_seconds(b.at - a.at), a.seq, format_seconds(b.at - a.at + d)),
                    { i }, b.at + rep.rows[static_cast<std::size_t>(i)].shift);
            break;
        }

        // Audio muxed into the video segments.
        if (r.kind == Track::Video) {
            for (const auto& [seq, st] : r.starts) {
                const double v = st.start(Track::Video), a = st.start(Track::Audio);
                if (std::isnan(v) || std::isnan(a)) continue;
                const double d = offset_delta(a, v);
                if (std::fabs(d) <= kAlignBad) continue;
                const int k = r.tl.index_of(seq);
                add(Severity::Error,
                    std::format("{}: muxed audio starts {} than video in segment #{}", r.label, lag_text(d), seq),
                    std::format("Inside one segment the audio PES starts at {} and the video at {}.",
                                format_clock(a), format_clock(v)),
                    { i }, k >= 0 ? r.tl.segments[static_cast<std::size_t>(k)].start : kNaN);
                break;
            }
        }
    }

    // Against the reference, period by period.
    const auto& rp = probes[static_cast<std::size_t>(ref_i)];
    int compared = 0;
    double worst_ok = 0.0;
    for (int i = 0; i < n; ++i) {
        const auto& r = in[static_cast<std::size_t>(i)];
        if (!r.tl.ok || i == ref_i) continue;
        const auto& ps = probes[static_cast<std::size_t>(i)];
        auto& row = rep.rows[static_cast<std::size_t>(i)];
        if (ps.empty()) {
            if (r.in_use)
                add(Severity::Info, std::format("{}: media timestamps not read yet", r.label),
                    "No segment of it has been downloaded or probed; its start times are unknown.", { i });
            continue;
        }
        bool first = true;
        for (const Probe& p : ps) {
            // The reference's first probe in the same period.
            const auto q = std::find_if(rp.begin(), rp.end(), [&](const Probe& x) { return x.period == p.period; });
            if (q == rp.end()) continue;
            const double d = offset_delta(p.offset, q->offset) - row.shift;
            if (first) { row.offset_vs_ref = d; first = false; ++compared; }
            const double ad = std::fabs(d);
            if (ad <= kAlignOk) { worst_ok = std::max(worst_ok, ad); continue; }
            const Severity sev = ad > kAlignBad ? Severity::Error : Severity::Warning;
            std::string title, detail;
            // Which period, when it is not the first one.
            const std::int64_t first_period = ref.tl.segments.front().disc_seq;
            const std::string where = p.period != first_period
                ? std::format(" after discontinuity {}", p.period - first_period) : std::string();
            if (r.kind == Track::Audio && ref.kind == Track::Video) {
                title = std::format("{}: audio timestamps {} than {}{}", r.label, lag_text(d), ref.label, where);
                detail = d > 0
                    ? std::format("At the same playlist position {}'s timestamps are {} later than {}'s. Players sync "
                                  "by timestamp: the sound runs {} behind the picture, and the first {} show picture "
                                  "without sound (or are dropped, with audio as the master clock).",
                                  r.label, format_seconds(ad), ref.label, format_seconds(ad), format_seconds(ad))
                    : std::format("At the same playlist position {}'s timestamps are {} earlier than {}'s. The sound "
                                  "runs {} ahead of the picture; the first {} play sound over a black or frozen frame "
                                  "until video timestamps begin.",
                                  r.label, format_seconds(ad), ref.label, format_seconds(ad), format_seconds(ad));
            } else if (r.kind == Track::Text) {
                title = std::format("{}: subtitles {} than {}{}", r.label, lag_text(d), ref.label, where);
                detail = std::format("X-TIMESTAMP-MAP places cue time 0 at media time {}, the video's playlist start "
                                     "is at {}: every cue shows {} {}.",
                                     format_seconds(p.offset), format_seconds(q->offset), format_seconds(ad),
                                     d > 0 ? "late" : "early");
            } else {
                title = std::format("{}: timestamps {} than {}{}", r.label, lag_text(d), ref.label, where);
                detail = std::format("The same playlist position carries media time {} in {} and {} in {}. Switching "
                                     "between them jumps {}, and audio paired with one is off by that much with the other.",
                                     format_clock(p.at + p.offset), r.label, format_clock(q->at + q->offset), ref.label,
                                     format_seconds(ad));
            }
            add(sev, std::move(title), std::move(detail), { i, ref_i }, p.at + row.shift);
            break;   // one finding per rendition: the first period that is off
        }
    }

    // ── Verdict ──────────────────────────────────────────────────────────────
    for (const auto& f : rep.findings) {
        if (f.sev == Severity::Error) ++rep.errors;
        if (f.sev == Severity::Warning) ++rep.warnings;
    }
    if (rep.errors == 0 && rep.warnings == 0) {
        int ok = 0;
        for (const auto& r : in) ok += r.tl.ok ? 1 : 0;
        if (ok <= 1) {
            std::string what = "Only one playlist to compare: no other rendition is in scope.";
            // Audio muxed into it (MPEG-TS): how far apart the two start in a segment.
            double worst_mux = -1.0;
            int muxed = 0;
            for (const auto& [seq, st] : ref.starts) {
                const double v = st.start(Track::Video), a = st.start(Track::Audio);
                if (std::isnan(v) || std::isnan(a)) continue;
                worst_mux = std::max(worst_mux, std::fabs(offset_delta(a, v)));
                ++muxed;
            }
            if (muxed > 0)
                what += std::format(" Its segments carry the audio too: in the {} read, audio and video start "
                                    "within {} of each other.", muxed, format_seconds(worst_mux));
            add(Severity::Ok, std::format("{}: playlist consistent", ref.label), what, {});
        } else {
            std::string what = "Same durations and discontinuities at the same times";
            if (compared > 0)
                what += std::format("; media timestamps within {} of {}", format_seconds(worst_ok), ref.label);
            else
                what += "; media timestamps not compared yet (no segment of two renditions read)";
            add(Severity::Ok, std::format("{} renditions aligned", ok), what + ".", {});
        }
    }
    std::stable_sort(rep.findings.begin(), rep.findings.end(),
                     [](const Finding& a, const Finding& b) { return static_cast<int>(a.sev) < static_cast<int>(b.sev); });
    return rep;
}

} // namespace hls
