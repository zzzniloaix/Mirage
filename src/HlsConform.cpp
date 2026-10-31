#include "HlsConform.h"

#include "JsonText.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <unordered_map>

namespace hls {

namespace {

// ── Text helpers ─────────────────────────────────────────────────────────────

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))  s.remove_suffix(1);
    return s;
}

// A decimal-integer; `fallback` unless all of it is digits.
std::int64_t to_int(std::string_view s, std::int64_t fallback)
{
    s = trim(s);
    if (s.empty() || s.size() > 18) return fallback;
    std::int64_t v = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return fallback;
        v = v * 10 + (c - '0');
    }
    return v;
}

double to_double(std::string_view s, double fallback)
{
    const std::string t(trim(s));
    if (t.empty()) return fallback;
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    return end == t.c_str() ? fallback : v;
}

std::vector<std::string> split(std::string_view s, char sep)
{
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i <= s.size()) {
        const std::size_t j = std::min(s.find(sep, i), s.size());
        if (const auto part = trim(s.substr(i, j - i)); !part.empty()) out.emplace_back(part);
        i = j + 1;
    }
    return out;
}

std::string join(const std::vector<std::string>& v, std::string_view sep)
{
    std::string out;
    for (const auto& s : v) {
        if (!out.empty()) out += sep;
        out += s;
    }
    return out;
}

bool hex_value(std::string_view s, std::uint32_t& out)
{
    if (s.empty() || s.size() > 8) return false;
    std::uint32_t v = 0;
    for (const char c : s) {
        const int d = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
                    : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (d < 0) return false;
        v = v << 4 | static_cast<std::uint32_t>(d);
    }
    out = v;
    return true;
}

int to_small(std::string_view s)   // a short decimal, -1 if it is not one
{
    const std::int64_t v = to_int(s, -1);
    return v >= 0 && v < 1'000'000 ? static_cast<int>(v) : -1;
}

// The last two path segments of a URI, without query: "v5/prog_index.m3u8".
std::string short_name(const std::string& uri)
{
    std::string p = uri.substr(0, uri.find_first_of("?#"));
    const auto last = p.rfind('/');
    if (last == std::string::npos) return p;
    const auto prev = last == 0 ? std::string::npos : p.rfind('/', last - 1);
    return prev == std::string::npos ? p : p.substr(prev + 1);
}

const char* transfer_name(int t)
{
    switch (t) {
        case 1:  return "BT.709";
        case 2:  return "unspecified";
        case 4:  return "BT.470 M";
        case 5:  return "BT.470 BG";
        case 6:  return "BT.601";
        case 7:  return "SMPTE 240M";
        case 8:  return "linear";
        case 11: return "IEC 61966-2-4";
        case 13: return "sRGB";
        case 14: return "BT.2020 10-bit";
        case 15: return "BT.2020 12-bit";
        case 16: return "PQ (SMPTE ST 2084)";
        case 18: return "HLG (ARIB STD-B67)";
        default: break;
    }
    return "other";
}

// ── NAL units ────────────────────────────────────────────────────────────────

// The RBSP of a NAL unit's payload (emulation prevention bytes removed), at
// most `max_out` bytes of it.
std::vector<std::uint8_t> rbsp(const std::uint8_t* p, std::size_t n, std::size_t max_out)
{
    std::vector<std::uint8_t> out;
    out.reserve(std::min(n, max_out));
    int zeros = 0;
    for (std::size_t i = 0; i < n && out.size() < max_out; ++i) {
        if (zeros >= 2 && p[i] == 0x03) {
            zeros = 0;
            continue;
        }
        out.push_back(p[i]);
        zeros = p[i] == 0 ? zeros + 1 : 0;
    }
    return out;
}

// Exp-Golomb reader over an RBSP; -1 once it runs out.
struct Bits {
    const std::vector<std::uint8_t>& d;
    std::size_t bit = 0;
    int u1()
    {
        if (bit >= d.size() * 8) return -1;
        const int v = (d[bit >> 3] >> (7 - (bit & 7))) & 1;
        ++bit;
        return v;
    }
    int ue()
    {
        int zeros = 0;
        for (;;) {
            const int b = u1();
            if (b < 0 || zeros > 30) return -1;
            if (b == 1) break;
            ++zeros;
        }
        std::int64_t v = 0;
        for (int i = 0; i < zeros; ++i) {
            const int b = u1();
            if (b < 0) return -1;
            v = v << 1 | b;
        }
        return static_cast<int>((std::int64_t{ 1 } << zeros) - 1 + v);
    }
};

// Calls f(nal, size) for each NAL unit of `d` until it returns false:
// length-prefixed by `len_size` bytes (1..4) when the lengths account for
// all of the data, else Annex B start codes.
template <class F>
void for_each_nal(const std::uint8_t* d, std::size_t n, int len_size, F&& f)
{
    if (!d || n == 0) return;
    if (len_size >= 1 && len_size <= 4) {
        const auto L = static_cast<std::size_t>(len_size);
        auto length_at = [&](std::size_t i) {
            std::size_t len = 0;
            for (std::size_t k = 0; k < L; ++k) len = len << 8 | d[i + k];
            return len;
        };
        bool framed = true;
        for (std::size_t i = 0; i < n;) {
            if (n - i < L) { framed = false; break; }
            const std::size_t len = length_at(i);
            i += L;
            if (len == 0 || len > n - i) { framed = false; break; }
            i += len;
        }
        if (framed) {
            for (std::size_t i = 0; i < n;) {
                const std::size_t len = length_at(i);
                i += L;
                if (!f(d + i, len)) return;
                i += len;
            }
            return;
        }
    }
    auto next_start = [&](std::size_t from) {
        for (std::size_t k = from; k + 2 < n; ++k)
            if (d[k] == 0 && d[k + 1] == 0 && d[k + 2] == 1) return k;
        return n;
    };
    for (std::size_t s = next_start(0); s < n;) {
        const std::size_t b = s + 3;
        const std::size_t e = next_start(b);
        std::size_t end = e;
        while (end > b && d[end - 1] == 0) --end;   // the zero of a following 4-byte start code
        if (end > b && !f(d + b, end - b)) return;
        s = e;
    }
}

// An H.264 SEI NAL unit's payload (after the header byte) holds a recovery point.
bool sei_has_recovery_point(const std::uint8_t* p, std::size_t n)
{
    const auto r = rbsp(p, n, n);
    std::size_t i = 0;
    while (i < r.size() && r[i] != 0x80) {   // rbsp_trailing_bits
        int type = 0, size = 0;
        while (i < r.size() && r[i] == 0xFF) { type += 255; ++i; }
        if (i >= r.size()) break;
        type += r[i++];
        while (i < r.size() && r[i] == 0xFF) { size += 255; ++i; }
        if (i >= r.size()) break;
        size += r[i++];
        if (type == 6) return true;
        i += static_cast<std::size_t>(size);
    }
    return false;
}

std::uint32_t be32(const std::uint8_t* p)
{
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) | p[3];
}

// ── Codec families ───────────────────────────────────────────────────────────

enum class Family : std::uint8_t {
    Unknown, Avc, Hevc, Av1, Vp9, DolbyVision, Aac, Mp3, Ac3, Eac3, Ac4, Opus, Flac, Alac, Text
};

Family family_of(const CodecId& c)
{
    const std::string& f = c.fourcc;
    if (f == "avc1" || f == "avc2" || f == "avc3" || f == "avc4") return Family::Avc;
    if (f == "hvc1" || f == "hev1") return Family::Hevc;
    if (f == "av01") return Family::Av1;
    if (f == "vp09") return Family::Vp9;
    if (c.dolby_vision) return Family::DolbyVision;
    if (f == "mp4a") {
        if (c.object_type == 34 || c.object_type == 0x69 + 1000 || c.object_type == 0x6B + 1000) return Family::Mp3;
        if (c.object_type == 0xA5 + 1000) return Family::Ac3;
        if (c.object_type == 0xA6 + 1000) return Family::Eac3;
        return Family::Aac;
    }
    if (f == "ac-3") return Family::Ac3;
    if (f == "ec-3") return Family::Eac3;
    if (f == "ac-4") return Family::Ac4;
    if (f == "Opus" || f == "opus") return Family::Opus;
    if (f == "fLaC" || f == "flac") return Family::Flac;
    if (f == "alac") return Family::Alac;
    if (c.kind == Track::Text) return Family::Text;
    return Family::Unknown;
}

// From FFmpeg's codec name, for a stream whose codec string is unknown.
Family family_of_codec(const std::string& codec)
{
    if (codec == "h264") return Family::Avc;
    if (codec == "hevc") return Family::Hevc;
    if (codec == "av1")  return Family::Av1;
    if (codec == "vp9")  return Family::Vp9;
    if (codec == "aac")  return Family::Aac;
    if (codec == "mp3")  return Family::Mp3;
    if (codec == "ac3")  return Family::Ac3;
    if (codec == "eac3") return Family::Eac3;
    if (codec == "ac4")  return Family::Ac4;
    if (codec == "opus") return Family::Opus;
    if (codec == "flac") return Family::Flac;
    if (codec == "alac") return Family::Alac;
    return Family::Unknown;
}

const char* family_name(Family f)
{
    switch (f) {
        case Family::Avc:         return "H.264";
        case Family::Hevc:        return "HEVC";
        case Family::Av1:         return "AV1";
        case Family::Vp9:         return "VP9";
        case Family::DolbyVision: return "Dolby Vision";
        case Family::Aac:         return "AAC";
        case Family::Mp3:         return "MP3";
        case Family::Ac3:         return "AC-3";
        case Family::Eac3:        return "E-AC-3";
        case Family::Ac4:         return "AC-4";
        case Family::Opus:        return "Opus";
        case Family::Flac:        return "FLAC";
        case Family::Alac:        return "ALAC";
        case Family::Text:        return "text";
        case Family::Unknown:     break;
    }
    return "unknown";
}

std::string avc_profile_name(int p, int flags)
{
    switch (p) {
        case 66:  return flags >= 0 && (flags & 0x40) ? "Constrained Baseline" : "Baseline";
        case 77:  return "Main";
        case 88:  return "Extended";
        case 100: return "High";
        case 110: return "High 10";
        case 122: return "High 4:2:2";
        case 244: return "High 4:4:4 Predictive";
        case 44:  return "CAVLC 4:4:4";
        default:  break;
    }
    return std::format("profile {}", p);
}

std::string hevc_profile_name(int p)
{
    switch (p) {
        case 1:  return "Main";
        case 2:  return "Main 10";
        case 3:  return "Main Still Picture";
        case 4:  return "Range Extensions";
        case 5:  return "High Throughput";
        case 9:  return "Screen Content";
        default: break;
    }
    return std::format("profile {}", p);
}

std::string av1_level_name(int idx)
{
    if (idx < 0 || idx == 31) return idx == 31 ? "max" : "?";
    return std::format("{}.{}", 2 + (idx >> 2), idx & 3);
}

std::string level_text(const CodecId& c, int level)
{
    if (level < 0) return "?";
    switch (family_of(c)) {
        case Family::Avc:  return level == 9 ? "1b" : std::format("{:.1f}", level / 10.0);
        case Family::Hevc: return std::format("{:g}", std::round(level / 3.0) / 10.0);
        case Family::Av1:  return av1_level_name(level);
        default:           break;
    }
    return std::to_string(level);
}

// ── Bit-rate windows ─────────────────────────────────────────────────────────

Bitrates bitrates_of(const PlaylistRead& pr)
{
    std::vector<SegmentBits> segs;
    segs.reserve(pr.tl.segments.size());
    for (std::size_t i = 0; i < pr.tl.segments.size(); ++i)
        segs.push_back({ pr.tl.segments[i].duration, i < pr.sizes.size() ? pr.sizes[i] : -1 });
    return segment_bitrates(segs, pr.tl.target);
}

std::string window_text(const PlaylistRead& pr, const Bitrates& b)
{
    if (b.peak_at < 0 || b.peak_n <= 0) return {};
    const auto& segs = pr.tl.segments;
    const auto first = static_cast<std::size_t>(b.peak_at);
    const std::size_t last = std::min(segs.size(), first + static_cast<std::size_t>(b.peak_n)) - 1;
    double d = 0.0;
    for (std::size_t i = first; i <= last; ++i) d += segs[i].duration;
    if (first == last) return std::format("segment #{}, {:.3f} s", segs[first].seq, d);
    return std::format("segments #{}–#{}, {:.3f} s", segs[first].seq, segs[last].seq, d);
}

std::int64_t seq_of(const PlaylistRead& pr, int index)
{
    return index >= 0 && static_cast<std::size_t>(index) < pr.tl.segments.size()
         ? pr.tl.segments[static_cast<std::size_t>(index)].seq : index;
}

double extinf_of(const PlaylistRead& pr, int index)
{
    return index >= 0 && static_cast<std::size_t>(index) < pr.tl.segments.size()
         ? pr.tl.segments[static_cast<std::size_t>(index)].duration : kNaN;
}

Severity worse(Severity a, Severity b) { return static_cast<int>(a) < static_cast<int>(b) ? a : b; }

} // namespace

// ── Attribute lists, the master ──────────────────────────────────────────────

std::vector<std::pair<std::string, std::string>> parse_attribute_list(std::string_view list)
{
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t i = 0;
    while (i < list.size()) {
        while (i < list.size() && (list[i] == ',' || std::isspace(static_cast<unsigned char>(list[i])))) ++i;
        if (i >= list.size()) break;
        const std::size_t eq = list.find('=', i);
        if (eq == std::string_view::npos) break;
        std::string name(trim(list.substr(i, eq - i)));
        std::size_t v0 = eq + 1;
        while (v0 < list.size() && list[v0] == ' ') ++v0;
        std::string value;
        if (v0 < list.size() && list[v0] == '"') {
            const std::size_t v1 = std::min(list.find('"', v0 + 1), list.size());
            value.assign(list.substr(v0 + 1, v1 - v0 - 1));
            const std::size_t comma = list.find(',', std::min(v1 + 1, list.size()));
            i = comma == std::string_view::npos ? list.size() : comma + 1;
        } else {
            const std::size_t v1 = std::min(list.find(',', v0), list.size());
            value.assign(trim(list.substr(v0, v1 - v0)));
            i = v1 + 1;
        }
        out.emplace_back(std::move(name), std::move(value));
    }
    return out;
}

std::vector<std::string> MasterDecl::playlists() const
{
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (const auto& v : variants)
        if (!v.uri.empty() && seen.insert(v.uri).second) out.push_back(v.uri);
    for (const auto& md : media)
        if (md.type == "AUDIO" && !md.uri.empty() && seen.insert(md.uri).second) out.push_back(md.uri);
    return out;
}

MasterDecl parse_master_decl(const std::string& text, const std::string& base_url)
{
    MasterDecl m;
    bool header = false, media_tags = false;
    std::optional<VariantDecl> pend;
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
                m.problems.push_back("not a playlist: no #EXTM3U header");
                return m;
            }
            header = true;
            continue;
        }
        if (line[0] == '#') {
            if (!line.starts_with("#EXT")) continue;   // comment
            const auto colon = line.find(':');
            const std::string_view tag = line.substr(0, colon);
            const std::string_view val = colon == std::string_view::npos ? std::string_view() : line.substr(colon + 1);
            if (tag == "#EXT-X-STREAM-INF") {
                if (pend) m.problems.push_back(std::format("line {}: EXT-X-STREAM-INF without a URI line", pend->line));
                VariantDecl v;
                v.line = line_no;
                for (const auto& [k, a] : parse_attribute_list(val)) {
                    if (k == "BANDWIDTH") v.bandwidth = to_int(a, -1);
                    else if (k == "AVERAGE-BANDWIDTH") v.avg_bandwidth = to_int(a, -1);
                    else if (k == "CODECS") { v.has_codecs = true; v.codecs = split(a, ','); }
                    else if (k == "RESOLUTION") {
                        const auto x = a.find_first_of("xX");
                        if (x != std::string::npos) {
                            v.width  = static_cast<int>(to_int(std::string_view(a).substr(0, x), 0));
                            v.height = static_cast<int>(to_int(std::string_view(a).substr(x + 1), 0));
                        }
                        if (v.width <= 0 || v.height <= 0) {
                            m.problems.push_back(std::format("line {}: RESOLUTION \"{}\" is not a decimal-resolution", line_no, a));
                            v.width = v.height = 0;
                        }
                    }
                    else if (k == "FRAME-RATE") v.frame_rate = to_double(a, 0.0);
                    else if (k == "VIDEO-RANGE") v.video_range = a;
                    else if (k == "AUDIO") v.audio = a;
                    else if (k == "VIDEO") v.video = a;
                    else if (k == "SUBTITLES") v.subtitles = a;
                }
                if (v.bandwidth < 0)
                    m.problems.push_back(std::format("line {}: EXT-X-STREAM-INF without BANDWIDTH, which every one "
                                                     "MUST include (RFC 8216 §4.3.4.2)", line_no));
                pend = std::move(v);
            } else if (tag == "#EXT-X-MEDIA") {
                MediaDecl md;
                md.line = line_no;
                for (const auto& [k, a] : parse_attribute_list(val)) {
                    if (k == "TYPE") md.type = a;
                    else if (k == "GROUP-ID") md.group_id = a;
                    else if (k == "NAME") md.name = a;
                    else if (k == "LANGUAGE") md.language = a;
                    else if (k == "URI") md.uri = resolve_uri(base_url, a);
                    else if (k == "CHANNELS") md.channels = a;
                }
                m.media.push_back(std::move(md));
            } else if (tag == "#EXT-X-INDEPENDENT-SEGMENTS") {
                m.independent = true;
            } else if (tag == "#EXTINF" || tag == "#EXT-X-TARGETDURATION" || tag == "#EXT-X-MEDIA-SEQUENCE") {
                media_tags = true;
            } else if (tag == "#EXT-X-DEFINE") {
                m.problems.push_back(std::format("line {}: variable substitution (EXT-X-DEFINE) is not applied here: "
                                                 "values that use it are read as written", line_no));
            }
            continue;
        }
        if (pend) {   // the URI line of the EXT-X-STREAM-INF before it
            pend->uri = resolve_uri(base_url, std::string(line));
            m.variants.push_back(std::move(*pend));
            pend.reset();
        }
    }
    if (pend) m.problems.push_back(std::format("line {}: EXT-X-STREAM-INF without a URI line", pend->line));
    if (m.variants.empty())
        m.problems.push_back(media_tags ? "a media playlist, not a master playlist: no variant is declared"
                                        : "no EXT-X-STREAM-INF");
    for (const auto& v : m.variants) {
        auto defined = [&](const char* type, const std::string& group) {
            return std::any_of(m.media.begin(), m.media.end(),
                               [&](const MediaDecl& md) { return md.type == type && md.group_id == group; });
        };
        if (!v.audio.empty() && !defined("AUDIO", v.audio))
            m.problems.push_back(std::format("line {}: AUDIO=\"{}\" matches no EXT-X-MEDIA TYPE=AUDIO GROUP-ID "
                                             "(RFC 8216 §4.3.4.2: it MUST)", v.line, v.audio));
        if (!v.video.empty() && !defined("VIDEO", v.video))
            m.problems.push_back(std::format("line {}: VIDEO=\"{}\" matches no EXT-X-MEDIA TYPE=VIDEO GROUP-ID "
                                             "(RFC 8216 §4.3.4.2: it MUST)", v.line, v.video));
    }
    m.ok = !m.variants.empty();
    return m;
}

// ── Codec strings ────────────────────────────────────────────────────────────

CodecId parse_codec(std::string_view s)
{
    CodecId c;
    s = trim(s);
    c.text.assign(s);
    const std::vector<std::string> p = split(s, '.');
    if (p.empty()) return c;
    c.fourcc = p[0];
    const std::string& f = c.fourcc;
    auto parts = [&](std::size_t n) { return p.size() >= n; };

    if (f == "avc1" || f == "avc2" || f == "avc3" || f == "avc4") {
        c.kind = Track::Video;
        std::uint32_t v = 0;
        if (parts(2) && p[1].size() == 6 && hex_value(p[1], v)) {
            c.profile = static_cast<int>(v >> 16);
            c.constraints = static_cast<int>((v >> 8) & 0xFF);
            c.level = static_cast<int>(v & 0xFF);
            c.ok = c.profile > 0;
        } else if (parts(3)) {   // the older decimal form, "avc1.66.30"
            c.profile = to_small(p[1]);
            c.level   = to_small(p[2]);
            c.ok = c.profile > 0 && c.level >= 0;
        }
    } else if (f == "hvc1" || f == "hev1") {
        c.kind = Track::Video;
        if (parts(4)) {
            std::string_view prof = p[1];
            if (!prof.empty() && prof[0] >= 'A' && prof[0] <= 'C') {
                c.profile_space = prof[0] - 'A' + 1;
                prof.remove_prefix(1);
            }
            c.profile = to_small(prof);
            std::uint32_t compat = 0;
            const bool compat_ok = hex_value(p[2], compat);
            c.compat = compat;
            std::string_view tl = p[3];
            if (!tl.empty() && (tl[0] == 'L' || tl[0] == 'H')) {
                c.tier = tl[0] == 'H' ? 1 : 0;
                c.level = to_small(tl.substr(1));
            }
            c.ok = c.profile >= 0 && compat_ok && c.tier >= 0 && c.level >= 0;
        }
    } else if (f == "av01") {
        c.kind = Track::Video;
        if (parts(4)) {
            c.profile = to_small(p[1]);
            const std::string& lt = p[2];
            if (lt.size() == 3 && (lt[2] == 'M' || lt[2] == 'H')) {
                c.level = to_small(std::string_view(lt).substr(0, 2));
                c.tier = lt[2] == 'H' ? 1 : 0;
            }
            c.bit_depth = to_small(p[3]);
            c.ok = c.profile >= 0 && c.level >= 0 && c.tier >= 0 && c.bit_depth > 0;
        }
    } else if (f == "dvh1" || f == "dvhe" || f == "dvav" || f == "dva1" || f == "dav1") {
        c.kind = Track::Video;
        c.dolby_vision = true;
        if (parts(3)) {
            c.profile = to_small(p[1]);
            c.level   = to_small(p[2]);
            c.ok = c.profile >= 0 && c.level >= 0;
        }
    } else if (f == "vp09") {
        c.kind = Track::Video;
        if (parts(4)) {
            c.profile = to_small(p[1]);
            c.level = to_small(p[2]);
            c.bit_depth = to_small(p[3]);
            c.ok = c.profile >= 0 && c.level >= 0 && c.bit_depth > 0;
        }
    } else if (f == "mp4a") {
        c.kind = Track::Audio;
        std::uint32_t oti = 0;
        if (parts(3) && hex_value(p[1], oti) && oti == 0x40) {        // MPEG-4 audio: the object type follows
            c.object_type = to_small(p[2]);
            c.ok = c.object_type > 0;
        } else if (parts(2) && hex_value(p[1], oti)) {
            // ObjectTypeIndication alone: 0x66–0x68 MPEG-2 AAC, 0x69 / 0x6B MP3,
            // 0xA5 AC-3, 0xA6 E-AC-3 (kept as 1000 + the OTI).
            if (oti >= 0x66 && oti <= 0x68) { c.object_type = 2; c.ok = true; }
            else if (oti == 0x69 || oti == 0x6B || oti == 0xA5 || oti == 0xA6) {
                c.object_type = 1000 + static_cast<int>(oti);
                c.ok = true;
            }
        }
    } else if (f == "ac-3" || f == "ec-3" || f == "ac-4" || f == "Opus" || f == "opus" || f == "fLaC"
               || f == "flac" || f == "alac") {
        c.kind = Track::Audio;
        c.ok = true;
    } else if (f == "wvtt" || f == "stpp" || f == "tx3g" || f == "c608" || f == "c708") {
        c.kind = Track::Text;
        c.ok = true;
    }
    return c;
}

std::string describe_codec(const CodecId& c)
{
    const Family fam = family_of(c);
    if (!c.ok) {
        if (fam != Family::Unknown) return family_name(fam);
        return c.text.empty() ? std::string("unknown") : c.text;
    }
    switch (fam) {
        case Family::Avc:
            return std::format("H.264 {}, level {}", avc_profile_name(c.profile, c.constraints), level_text(c, c.level));
        case Family::Hevc:
            return std::format("HEVC {}, {} tier, level {}", hevc_profile_name(c.profile), c.tier ? "High" : "Main",
                               level_text(c, c.level));
        case Family::Av1: {
            static const char* names[] = { "Main", "High", "Professional" };
            const std::string prof = c.profile >= 0 && c.profile < 3 ? names[c.profile] : std::format("profile {}", c.profile);
            return std::format("AV1 {}, level {}{}, {}-bit", prof, level_text(c, c.level), c.tier ? " High tier" : "",
                               c.bit_depth);
        }
        case Family::Vp9:
            return std::format("VP9 profile {}, level {:.1f}, {}-bit", c.profile, c.level / 10.0, c.bit_depth);
        case Family::DolbyVision:
            return std::format("Dolby Vision profile {}, level {}", c.profile, c.level);
        case Family::Aac:
            switch (c.object_type) {
                case 1:  return "AAC Main";
                case 2:  return "AAC-LC";
                case 5:  return "HE-AAC";
                case 23: return "AAC-LD";
                case 29: return "HE-AAC v2";
                case 39: return "AAC-ELD";
                default: return std::format("AAC (object type {})", c.object_type);
            }
        default:
            break;
    }
    return family_name(fam);
}

std::string avc_codec_string(const std::uint8_t* config, std::size_t n, std::string_view fourcc)
{
    if (!config || n < 4) return {};
    int profile = -1, flags = -1, level = -1;
    if (config[0] == 1 && n >= 7) {   // avcC: configurationVersion, then the SPS's first three bytes
        profile = config[1];
        flags   = config[2];
        level   = config[3];
    } else {
        for_each_nal(config, n, 0, [&](const std::uint8_t* nal, std::size_t len) {
            if (len < 4 || (nal[0] & 0x1F) != 7) return true;
            const auto r = rbsp(nal + 1, len - 1, 8);
            if (r.size() >= 3) {
                profile = r[0];
                flags   = r[1];
                level   = r[2];
            }
            return false;
        });
    }
    if (profile <= 0 || flags < 0 || level < 0) return {};
    return std::format("{}.{:02x}{:02x}{:02x}", fourcc.empty() ? std::string_view("avc1") : fourcc, profile, flags, level);
}

std::string hevc_codec_string(const std::uint8_t* config, std::size_t n, std::string_view fourcc)
{
    // general_profile_space … general_level_idc: 12 bytes, the same in hvcC
    // (from its second byte) and in the SPS's profile_tier_level (after its
    // first byte).
    std::array<std::uint8_t, 12> ptl{};
    bool have = false;
    if (config && n >= 23 && config[0] == 1) {
        std::copy(config + 1, config + 13, ptl.begin());
        have = true;
    } else if (config) {
        for_each_nal(config, n, 0, [&](const std::uint8_t* nal, std::size_t len) {
            if (len < 3 || ((nal[0] >> 1) & 0x3F) != 33) return true;
            const auto r = rbsp(nal + 2, len - 2, 16);
            if (r.size() >= 13) {
                std::copy(r.begin() + 1, r.begin() + 13, ptl.begin());
                have = true;
            }
            return false;
        });
    }
    if (!have) return {};
    const int space = ptl[0] >> 6, tier = (ptl[0] >> 5) & 1, idc = ptl[0] & 0x1F;
    // ISO/IEC 14496-15 Annex E: the compatibility flags in reverse bit
    // order (flag 31 the most significant), hexadecimal.
    const std::uint32_t be = be32(&ptl[1]);
    std::uint32_t rev = 0;
    for (int j = 0; j < 32; ++j)
        if (be & (1u << (31 - j))) rev |= 1u << j;
    std::string s = std::format("{}.{}{}.{:X}.{}{}", fourcc.empty() ? std::string_view("hvc1") : fourcc,
                                space ? std::string(1, static_cast<char>('A' + space - 1)) : std::string(), idc, rev,
                                tier ? 'H' : 'L', ptl[11]);
    int last = 5;   // the six constraint bytes, trailing zero bytes left out
    while (last >= 0 && ptl[static_cast<std::size_t>(5 + last)] == 0) --last;
    for (int k = 0; k <= last; ++k) s += std::format(".{:X}", ptl[static_cast<std::size_t>(5 + k)]);
    return s;
}

std::string av1_codec_string(const std::uint8_t* config, std::size_t n)
{
    if (!config || n < 4 || config[0] != 0x81) return {};   // av1C: marker 1, version 1
    const int  profile = config[1] >> 5, level = config[1] & 0x1F, tier = config[2] >> 7;
    const bool high = (config[2] >> 6) & 1, twelve = (config[2] >> 5) & 1;
    const int  depth = profile == 2 && high ? (twelve ? 12 : 10) : high ? 10 : 8;
    return std::format("av01.{}.{:02}{}.{:02}", profile, level, tier ? 'H' : 'M', depth);
}

int nal_length_size(const std::uint8_t* config, std::size_t n, bool hevc)
{
    if (!config || n == 0 || config[0] != 1) return 0;
    if (!hevc) return n >= 7 ? (config[4] & 3) + 1 : 0;
    return n >= 23 ? (config[21] & 3) + 1 : 0;
}

// ── Segment starts ───────────────────────────────────────────────────────────

const char* first_picture_name(FirstPicture p)
{
    switch (p) {
        case FirstPicture::Idr:      return "IDR";
        case FirstPicture::Cra:      return "CRA";
        case FirstPicture::Bla:      return "BLA";
        case FirstPicture::Recovery: return "recovery point";
        case FirstPicture::Intra:    return "I (not IDR)";
        case FirstPicture::Other:    return "P/B";
        case FirstPicture::Unknown:  break;
    }
    return "unknown";
}

bool is_random_access(FirstPicture p)
{
    return p == FirstPicture::Idr || p == FirstPicture::Cra || p == FirstPicture::Bla;
}

FirstPicture first_picture_h264(const std::uint8_t* au, std::size_t n, int nal_length)
{
    FirstPicture out = FirstPicture::Unknown;
    bool recovery = false;
    for_each_nal(au, n, nal_length, [&](const std::uint8_t* nal, std::size_t len) {
        const int type = nal[0] & 0x1F;
        if (type == 6) {
            if (len > 1 && sei_has_recovery_point(nal + 1, len - 1)) recovery = true;
            return true;
        }
        if (type == 5) {
            out = FirstPicture::Idr;
            return false;
        }
        if (type == 1 || type == 2) {   // a non-IDR slice (2: data partition A)
            const auto r = rbsp(nal + 1, len - 1, 16);
            Bits b{ r };
            b.ue();                      // first_mb_in_slice
            const int slice_type = b.ue();
            const bool intra = slice_type >= 0 && (slice_type % 5 == 2 || slice_type % 5 == 4);
            out = recovery ? FirstPicture::Recovery : intra ? FirstPicture::Intra : FirstPicture::Other;
            return false;
        }
        return true;                     // AUD, SPS, PPS, other SEI, …
    });
    return out;
}

FirstPicture first_picture_hevc(const std::uint8_t* au, std::size_t n, int nal_length)
{
    FirstPicture out = FirstPicture::Unknown;
    for_each_nal(au, n, nal_length, [&](const std::uint8_t* nal, std::size_t len) {
        if (len < 2) return true;
        const int type = (nal[0] >> 1) & 0x3F;
        if (type >= 32) return true;     // VPS, SPS, PPS, AUD, SEI, …: not a picture
        if (type >= 16 && type <= 18)       out = FirstPicture::Bla;
        else if (type == 19 || type == 20)  out = FirstPicture::Idr;
        else if (type == 21)                out = FirstPicture::Cra;
        else                                out = FirstPicture::Other;   // TRAIL, TSA, STSA, RADL, RASL, reserved
        return false;
    });
    return out;
}

// ── Bit rates ────────────────────────────────────────────────────────────────

Bitrates segment_bitrates(const std::vector<SegmentBits>& segs, double target)
{
    Bitrates b;
    b.total = static_cast<int>(segs.size());
    double bytes = 0.0, secs = 0.0;
    for (const auto& s : segs) {
        if (s.bytes < 0) continue;
        ++b.sized;
        if (s.duration > 0.0) {
            bytes += static_cast<double>(s.bytes);
            secs  += s.duration;
        }
    }
    if (secs > 0.0) b.average = bytes * 8.0 / secs;

    // Every run of contiguous sized segments lasting 0.5–1.5 target durations.
    constexpr double kEps = 1e-9;
    const double lo = 0.5 * target - kEps, hi = 1.5 * target + kEps;
    for (std::size_t i = 0; target > 0.0 && i < segs.size(); ++i) {
        double d = 0.0, n = 0.0;
        for (std::size_t j = i; j < segs.size() && segs[j].bytes >= 0; ++j) {
            d += segs[j].duration;
            n += static_cast<double>(segs[j].bytes);
            if (d > hi) break;
            if (d < lo || d <= 0.0) continue;
            const double rate = n * 8.0 / d;
            if (rate > b.peak) {
                b.peak    = rate;
                b.peak_at = static_cast<int>(i);
                b.peak_n  = static_cast<int>(j - i + 1);
            }
        }
    }
    if (b.peak_at < 0) {   // no run fits (or no target): the highest single segment
        for (std::size_t i = 0; i < segs.size(); ++i) {
            if (segs[i].bytes < 0 || segs[i].duration <= 0.0) continue;
            const double rate = static_cast<double>(segs[i].bytes) * 8.0 / segs[i].duration;
            if (rate > b.peak) {
                b.peak    = rate;
                b.peak_at = static_cast<int>(i);
                b.peak_n  = 1;
                b.peak_single = true;
            }
        }
    }
    return b;
}

std::string format_bitrate(double bps)
{
    if (!(bps > 0.0)) return "—";
    if (bps >= 1e6) return std::format("{:.2f} Mb/s", bps / 1e6);
    if (bps >= 1e3) return std::format("{:.0f} kb/s", bps / 1e3);
    return std::format("{:.0f} b/s", bps);
}

std::string format_list(const std::vector<int>& v)
{
    std::string out;
    for (std::size_t i = 0; i < v.size();) {
        std::size_t j = i;
        while (j + 1 < v.size() && v[j + 1] == v[j] + 1) ++j;
        out += (out.empty() ? "" : ", ") + (j >= i + 2 ? std::format("{}–{}", v[i], v[j])
                                                        : j == i + 1 ? std::format("{}, {}", v[i], v[j])
                                                                     : std::to_string(v[i]));
        i = j + 1;
    }
    return out;
}

const char* check_name(Check c)
{
    switch (c) {
        case Check::Bandwidth:        return "BANDWIDTH";
        case Check::AverageBandwidth: return "AVERAGE-BANDWIDTH";
        case Check::Codecs:           return "CODECS";
        case Check::Resolution:       return "RESOLUTION";
        case Check::FrameRate:        return "FRAME-RATE";
        case Check::VideoRange:       return "VIDEO-RANGE";
        case Check::SegmentStart:     return "segment starts";
        case Check::Duration:         return "segment durations";
        case Check::Count:            break;
    }
    return "?";
}

// ── The verdict ──────────────────────────────────────────────────────────────

ConformReport analyze_conformance(const MasterDecl& m, const std::vector<PlaylistRead>& reads)
{
    ConformReport rep;
    std::unordered_map<std::string, const PlaylistRead*> by_uri;
    for (const auto& r : reads) by_uri.emplace(r.uri, &r);
    auto read_of = [&](const std::string& uri) -> const PlaylistRead* {
        const auto it = by_uri.find(uri);
        return it == by_uri.end() ? nullptr : it->second;
    };

    // Findings about one playlist come once per variant that uses it (bipbop
    // lists each video playlist once per audio group): the same text merges
    // into one finding naming every such variant.
    std::vector<Finding> found;
    auto add = [&](Severity sev, int variant, std::string title, std::string detail) {
        for (auto& f : found) {
            if (f.sev != sev || f.title != title || f.detail != detail) continue;
            if (variant >= 0 && std::find(f.renditions.begin(), f.renditions.end(), variant) == f.renditions.end())
                f.renditions.push_back(variant);
            return;
        }
        Finding f;
        f.sev    = sev;
        f.title  = std::move(title);
        f.detail = std::move(detail);
        if (variant >= 0) f.renditions.push_back(variant);
        found.push_back(std::move(f));
    };

    for (const auto& p : m.problems) add(Severity::Error, -1, "The master playlist: " + p, {});

    // Every variant playlist and audio rendition has one target duration (§6.2.4).
    {
        std::vector<std::pair<double, std::string>> targets;
        for (const auto& uri : m.playlists())
            if (const PlaylistRead* pr = read_of(uri); pr && pr->error.empty() && pr->tl.ok && pr->tl.target > 0)
                targets.emplace_back(pr->tl.target, short_name(uri));
        std::set<double> distinct;
        for (const auto& t : targets) distinct.insert(t.first);
        if (distinct.size() > 1) {
            std::string detail;
            for (const double t : distinct) {
                std::vector<std::string> names;
                for (const auto& [tt, name] : targets) if (tt == t) names.push_back(name);
                detail += std::format("{}{:g} s: {}", detail.empty() ? "" : "; ", t, join(names, ", "));
            }
            add(Severity::Error, -1, "The playlists' EXT-X-TARGETDURATION differ",
                detail + ". RFC 8216 §6.2.4: each Media Playlist in each Variant Stream MUST have the same target duration.");
        }
    }

    // BANDWIDTHs short of the peak by kPeakSlack at most: one note for all.
    struct Slight { int variant; double frac, bps; };
    std::vector<Slight> slight;

    rep.rows.resize(m.variants.size());
    for (std::size_t vi = 0; vi < m.variants.size(); ++vi) {
        const VariantDecl& v = m.variants[vi];
        VariantRow&        row = rep.rows[vi];
        const int          idx = static_cast<int>(vi);
        const std::string  name = short_name(v.uri);
        const PlaylistRead* pr = read_of(v.uri);
        const bool          pr_ok = pr && pr->error.empty() && pr->tl.ok;

        if (!pr) {
            add(Severity::Info, idx, name + ": not read", {});
        } else if (!pr->error.empty()) {
            add(Severity::Warning, idx, name + ": could not be read", pr->error);
        } else {
            for (const auto& p : pr->tl.problems) {
                if (p == "no EXT-X-TARGETDURATION" && pr->target_tag) {   // there, and 0
                    add(Severity::Warning, idx, name + ": EXT-X-TARGETDURATION is 0",
                        "Every EXTINF rounds to 0 s. RFC 8216 §4.3.3.1 makes the target duration the maximum segment "
                        "duration; live clients pace their playlist reloads by it (§6.3.4).");
                    continue;
                }
                const bool must = p.find("longer than EXT-X-TARGETDURATION") != std::string::npos
                               || p.find("no EXT-X-TARGETDURATION") != std::string::npos;
                add(must ? Severity::Error : Severity::Warning, idx, name + ": " + p,
                    must ? "RFC 8216 §4.3.3.1: EXT-X-TARGETDURATION is REQUIRED, and every EXTINF rounded to the "
                           "nearest integer MUST be no more than it." : std::string());
            }
            if (!pr->key.empty())
                add(Severity::Info, idx, std::format("{}: segments encrypted ({})", name, pr->key),
                    "Their contents are not read: only sizes, bit rates and the playlist count.");
        }
        row.read = pr ? static_cast<int>(pr->reads.size()) : 0;

        // The audio renditions of its group that have playlists of their own.
        std::vector<const PlaylistRead*> audio_reads;
        bool audio_missing = false;
        for (const auto& md : m.media) {
            if (md.type != "AUDIO" || v.audio.empty() || md.group_id != v.audio || md.uri.empty()) continue;
            const PlaylistRead* ar = read_of(md.uri);
            if (ar && ar->error.empty() && ar->tl.ok) audio_reads.push_back(ar);
            else audio_missing = true;
        }

        // ── Bit rates ────────────────────────────────────────────────────────
        const Bitrates vb = pr_ok ? bitrates_of(*pr) : Bitrates{};
        double apeak = 0.0, aavg = 0.0;
        bool   acomplete = true;
        for (const PlaylistRead* ar : audio_reads) {
            const Bitrates ab = bitrates_of(*ar);
            apeak = std::max(apeak, ab.peak);
            aavg  = std::max(aavg, ab.average);
            acomplete = acomplete && ab.complete();
        }
        row.peak = vb.peak + apeak;
        row.average = vb.average + aavg;
        row.rates_complete = vb.complete() && acomplete && !audio_missing;
        const std::string split_text = apeak > 0.0 ? std::format(" ({} the variant's playlist + {} its largest audio rendition)",
                                                                 format_bitrate(vb.peak), format_bitrate(apeak))
                                                   : std::string();
        const std::string scope_text = row.rates_complete ? std::string()
            : std::format(" Over the segments whose size is known ({} of {} in its playlist{}): a lower bound.", vb.sized,
                          vb.total, audio_missing ? ", an audio rendition unread" : "");
        {
            Cell& c = row.cell(Check::Bandwidth);
            c.declared = v.bandwidth >= 0 ? format_bitrate(static_cast<double>(v.bandwidth)) : "absent";
            if (vb.sized > 0) c.measured = format_bitrate(row.peak);
            if (v.bandwidth < 0) {
                c.checked = true;
                c.sev = Severity::Error;
                c.note = "Every EXT-X-STREAM-INF MUST include BANDWIDTH (RFC 8216 §4.3.4.2).";
            } else if (vb.sized == 0) {
                c.note = "No segment size is known.";
            } else {
                c.checked = true;
                const std::string where = window_text(*pr, vb);
                const std::string peak_text = std::format("peak segment bit rate {}{}{}", format_bitrate(row.peak), split_text,
                                                          where.empty() ? "" : ", " + where);
                if (static_cast<double>(v.bandwidth) < row.peak) {
                    const double short_bps = row.peak - static_cast<double>(v.bandwidth);
                    const double frac = short_bps / row.peak;
                    const std::string by = frac < 0.001 ? std::format("{:.3f}% ({:.0f} b/s)", frac * 100.0, short_bps)
                                                        : std::format("{:.1f}%", frac * 100.0);
                    c.sev = frac > kPeakSlack ? Severity::Error : Severity::Info;
                    c.note = std::format("BANDWIDTH {} ({} b/s) is {} below the {} ({:.0f} b/s). RFC 8216 §4.3.4.2: "
                                         "BANDWIDTH MUST be the largest sum of peak segment bit rates of any playable "
                                         "combination of renditions; a player that fits BANDWIDTH to its link can stall "
                                         "on this variant.{}{}",
                                         format_bitrate(static_cast<double>(v.bandwidth)), v.bandwidth, by, peak_text,
                                         row.peak, scope_text,
                                         c.sev == Severity::Info
                                             ? std::format(" Short by no more than {:g}% of the peak: a peak computed over "
                                                           "other windows (per segment, say) lands this close.",
                                                           kPeakSlack * 100.0)
                                             : std::string());
                    if (c.sev == Severity::Error)
                        add(c.sev, idx,
                            std::format("{}: BANDWIDTH {} is {} below the measured peak {}", name,
                                        format_bitrate(static_cast<double>(v.bandwidth)), by, format_bitrate(row.peak)),
                            c.note);
                    else
                        slight.push_back({ idx, frac, short_bps });
                } else if (row.rates_complete && static_cast<double>(v.bandwidth) > kOverDeclared * row.peak) {
                    c.sev = Severity::Info;
                    c.note = std::format("BANDWIDTH {} is {:.2f} × the {}: players wait for a faster link than the variant "
                                         "needs.", format_bitrate(static_cast<double>(v.bandwidth)),
                                         static_cast<double>(v.bandwidth) / row.peak, peak_text);
                    add(Severity::Info, idx, std::format("{}: BANDWIDTH {:.2f} × the measured peak", name,
                                                         static_cast<double>(v.bandwidth) / row.peak), c.note);
                } else {
                    c.sev = Severity::Ok;
                    c.note = std::format("BANDWIDTH {} covers the {}.{}", format_bitrate(static_cast<double>(v.bandwidth)),
                                         peak_text, scope_text);
                }
            }
        }
        {
            Cell& c = row.cell(Check::AverageBandwidth);
            c.declared = v.avg_bandwidth >= 0 ? format_bitrate(static_cast<double>(v.avg_bandwidth)) : "—";
            if (vb.sized > 0) c.measured = format_bitrate(row.average);
            if (v.avg_bandwidth < 0) {
                c.note = "Not declared (OPTIONAL).";
            } else if (!row.rates_complete) {
                c.note = std::format("Needs every segment's size ({} of {} known).", vb.sized, vb.total);
            } else if (row.average > 0.0) {
                c.checked = true;
                const double off = (static_cast<double>(v.avg_bandwidth) - row.average) / row.average;
                c.note = std::format("AVERAGE-BANDWIDTH {} is {:+.1f}% off the average segment bit rate {}{}.",
                                     c.declared, off * 100.0, format_bitrate(row.average),
                                     aavg > 0.0 ? std::format(" ({} + {} audio)", format_bitrate(vb.average), format_bitrate(aavg))
                                                : std::string());
                c.sev = std::fabs(off) > kAverageSlack ? Severity::Warning : Severity::Ok;
                if (c.sev == Severity::Warning)
                    add(Severity::Warning, idx, std::format("{}: AVERAGE-BANDWIDTH {:+.0f}% off the measured average", name,
                                                            off * 100.0), c.note);
            }
        }

        // ── What the segments hold ───────────────────────────────────────────
        std::vector<const SegmentRead*> vreads;     // segments of its playlist with video
        std::vector<const AudioSeen*>   audios;     // muxed in them, or in its audio renditions
        if (pr_ok)
            for (const auto& s : pr->reads) {
                if (s.video) vreads.push_back(&s);
                if (s.audio) audios.push_back(&*s.audio);
            }
        bool audio_read = !audio_reads.empty();
        for (const PlaylistRead* ar : audio_reads) {
            bool any = false;
            for (const auto& s : ar->reads)
                if (s.audio) { audios.push_back(&*s.audio); any = true; }
            audio_read = audio_read && any;
        }

        // ── CODECS ───────────────────────────────────────────────────────────
        {
            Cell& c = row.cell(Check::Codecs);
            c.declared = v.has_codecs ? join(v.codecs, ",") : "absent";
            std::vector<CodecId> decl;
            for (const auto& s : v.codecs) decl.push_back(parse_codec(s));
            std::vector<bool> used(decl.size(), false);
            std::vector<std::string> seen_strings, notes;
            Severity sev = Severity::Ok;
            auto issue = [&](Severity s, std::string title, std::string detail) {
                sev = worse(sev, s);
                notes.push_back(title.substr(title.find(": ") + 2) + (detail.empty() ? "" : " — " + detail));
                add(s, idx, std::move(title), std::move(detail));
            };
            if (!v.has_codecs) {
                sev = Severity::Warning;
                notes.push_back("Every EXT-X-STREAM-INF SHOULD include CODECS (RFC 8216 §4.3.4.2).");
                add(Severity::Warning, idx, name + ": no CODECS", notes.back());
            }
            auto find_decl = [&](Family fam, const CodecId* prefer) {
                int any = -1;
                for (std::size_t j = 0; j < decl.size(); ++j) {
                    if (family_of(decl[j]) != fam) continue;
                    if (prefer && prefer->object_type >= 0 && decl[j].object_type == prefer->object_type)
                        return static_cast<int>(j);
                    if (any < 0) any = static_cast<int>(j);
                }
                return any;
            };

            // Video: every distinct configuration seen.
            std::set<std::string> done_video;
            for (const SegmentRead* s : vreads) {
                const VideoSeen& vs = *s->video;
                const std::string key = std::format("{}|{}|{}|{}", vs.codec, vs.codec_string, vs.dv_profile, vs.dv_level);
                if (!done_video.insert(key).second) continue;
                const CodecId act = parse_codec(vs.codec_string);
                const Family  fam = act.ok ? family_of(act) : family_of_codec(vs.codec);
                std::string shown = vs.codec_string.empty() ? vs.codec : vs.codec_string;
                if (vs.dv_profile >= 0) shown += std::format(" + Dolby Vision {}.{}", vs.dv_profile, vs.dv_level);
                seen_strings.push_back(shown);
                if (!v.has_codecs) continue;
                const std::string what = act.ok ? std::format("{} ({})", describe_codec(act), vs.codec_string)
                                                : family_name(fam);
                // Dolby Vision streams are matched to the format CODECS gives them.
                int j = vs.dv_profile >= 0 ? find_decl(Family::DolbyVision, nullptr) : -1;
                if (j >= 0) {
                    used[static_cast<std::size_t>(j)] = true;
                    const CodecId& d = decl[static_cast<std::size_t>(j)];
                    if (d.ok && d.profile != vs.dv_profile)
                        issue(Severity::Error, std::format("{}: CODECS Dolby Vision profile {} but the stream's is {}", name,
                                                           d.profile, vs.dv_profile), {});
                    else if (d.ok && d.level < vs.dv_level)
                        issue(Severity::Error, std::format("{}: CODECS Dolby Vision level {} below the stream's {}", name,
                                                           d.level, vs.dv_level), {});
                    else if (d.ok && d.level > vs.dv_level)
                        issue(Severity::Info, std::format("{}: CODECS Dolby Vision level {} above the stream's {}", name,
                                                          d.level, vs.dv_level), {});
                    // A backward-compatible profile also names its base layer.
                    if (const int base = find_decl(fam, nullptr); base >= 0) used[static_cast<std::size_t>(base)] = true;
                    continue;
                }
                j = find_decl(fam, nullptr);
                if (j < 0) {
                    issue(Severity::Error, std::format("{}: CODECS does not list the video format", name),
                          std::format("The stream is {}; CODECS is \"{}\". RFC 8216 §6.2.4: CODECS MUST include every media "
                                      "format present in any segment of the variant's renditions.", what, c.declared));
                    continue;
                }
                used[static_cast<std::size_t>(j)] = true;
                const CodecId& d = decl[static_cast<std::size_t>(j)];
                if (!d.ok || !act.ok) {
                    if (!act.ok)
                        notes.push_back(std::format("The stream's {} configuration could not be read: the format alone "
                                                    "was compared.", family_name(fam)));
                    else
                        issue(Severity::Warning, std::format("{}: CODECS \"{}\" does not parse", name, d.text),
                              std::format("The stream is {}.", what));
                    continue;
                }
                const std::string decl_what = std::format("{} ({})", describe_codec(d), d.text);
                if (d.profile != act.profile || d.profile_space != act.profile_space
                    || (fam == Family::Av1 && d.bit_depth != act.bit_depth)) {
                    issue(Severity::Error, std::format("{}: CODECS names another profile than the stream's", name),
                          std::format("CODECS says {}; the stream is {}. Devices choose variants by the profile they "
                                      "can decode.", decl_what, what));
                    continue;
                }
                if ((fam == Family::Hevc || fam == Family::Av1) && d.tier != act.tier) {
                    const bool low = d.tier < act.tier;
                    issue(low ? Severity::Error : Severity::Info,
                          std::format("{}: CODECS {} tier, the stream is {} tier", name, d.tier ? "High" : "Main",
                                      act.tier ? "High" : "Main"),
                          std::format("CODECS says {}; the stream is {}.", decl_what, what));
                }
                if (d.level < act.level)
                    issue(Severity::Error, std::format("{}: CODECS level {} below the stream's {}", name,
                                                       level_text(d, d.level), level_text(act, act.level)),
                          std::format("CODECS says {}; the stream is {}: a device limited to level {} can choose a variant "
                                      "it cannot decode.", decl_what, what, level_text(d, d.level)));
                else if (d.level > act.level)
                    issue(Severity::Info, std::format("{}: CODECS level {} above the stream's {}", name,
                                                      level_text(d, d.level), level_text(act, act.level)),
                          std::format("CODECS says {}; the stream is {}: a device limited to level {} skips a variant it "
                                      "could decode.", decl_what, what, level_text(act, act.level)));
                if (fam == Family::Avc && d.constraints >= 0 && act.constraints >= 0 && d.constraints != act.constraints)
                    issue(Severity::Info, std::format("{}: CODECS constraint flags 0x{:02x}, the stream's 0x{:02x}", name,
                                                      d.constraints, act.constraints), {});
                const bool base_entry = vs.sample_entry == "avc1" || vs.sample_entry == "avc3" || vs.sample_entry == "hvc1"
                                     || vs.sample_entry == "hev1";
                if (base_entry && d.fourcc != vs.sample_entry && (fam == Family::Avc || fam == Family::Hevc))
                    issue(Severity::Warning, std::format("{}: CODECS {} but the init segment's sample entry is {}", name,
                                                         d.fourcc, vs.sample_entry),
                          std::format("{} and {} differ in where the parameter sets are: {} carries them in the sample "
                                      "entry only, {} lets them come in-band too.", d.fourcc, vs.sample_entry,
                                      fam == Family::Hevc ? "hvc1" : "avc1", fam == Family::Hevc ? "hev1" : "avc3"));
            }

            // Audio: every distinct format seen.
            std::set<std::string> done_audio;
            for (const AudioSeen* a : audios) {
                const std::string shown = a->codec_string.empty() ? a->codec : a->codec_string;
                if (!done_audio.insert(shown).second) continue;
                seen_strings.push_back(shown);
                if (!v.has_codecs) continue;
                const CodecId act = parse_codec(a->codec_string);
                const Family  fam = act.ok ? family_of(act) : family_of_codec(a->codec);
                const int j = find_decl(fam, act.ok ? &act : nullptr);
                if (j < 0) {
                    issue(Severity::Error, std::format("{}: CODECS does not list the audio format", name),
                          std::format("The audio is {} ({}); CODECS is \"{}\". RFC 8216 §6.2.4: CODECS MUST include every "
                                      "media format present in any segment of the variant's renditions.",
                                      act.ok ? describe_codec(act) : family_name(fam), shown, c.declared));
                    continue;
                }
                used[static_cast<std::size_t>(j)] = true;
                const CodecId& d = decl[static_cast<std::size_t>(j)];
                if (fam == Family::Aac && d.ok && act.ok && d.object_type != act.object_type)
                    issue(Severity::Warning, std::format("{}: CODECS {} but the audio is {}", name, describe_codec(d),
                                                         describe_codec(act)),
                          std::format("CODECS says {}; the audio decodes as {}.", d.text, a->codec_string));
            }

            // Formats CODECS lists that nothing read has — only said when
            // everything of the variant was read.
            const bool all_read = pr_ok && pr->key.empty() && !pr->reads.empty() && !audio_missing
                               && (audio_reads.empty() || audio_read);
            for (std::size_t j = 0; j < decl.size(); ++j) {
                if (used[j]) continue;
                if (decl[j].kind == Track::Text) continue;   // subtitles / captions are not read
                if (decl[j].kind == Track::Unknown) {
                    issue(Severity::Warning, std::format("{}: CODECS format \"{}\" is not one this check knows", name,
                                                         decl[j].text), {});
                } else if (all_read) {
                    issue(Severity::Warning, std::format("{}: CODECS lists {} but no segment read holds it", name,
                                                         decl[j].text),
                          std::format("{} segment(s) of its playlist{} were read.", pr->reads.size(),
                                      audio_reads.empty() ? "" : " and of its audio renditions"));
                }
            }
            c.checked = v.has_codecs || !seen_strings.empty();
            c.sev = c.checked ? sev : Severity::Info;
            c.measured = join(seen_strings, ",");
            c.note = notes.empty() ? (seen_strings.empty() ? "Nothing was read to compare with."
                                                           : "Every format read is listed, at the profile and level read.")
                                   : join(notes, "\n");
        }

        // ── RESOLUTION ───────────────────────────────────────────────────────
        {
            Cell& c = row.cell(Check::Resolution);
            c.declared = v.width > 0 ? std::format("{}×{}", v.width, v.height) : "absent";
            std::vector<std::string> sizes;
            bool mismatch = false;
            for (const SegmentRead* s : vreads) {
                const VideoSeen& vs = *s->video;
                if (vs.width <= 0) continue;
                const std::string t = std::format("{}×{}", vs.width, vs.height);
                if (std::find(sizes.begin(), sizes.end(), t) == sizes.end()) sizes.push_back(t);
                if (v.width <= 0) continue;
                // The decoded size, or the size it is displayed at (non-square pixels).
                const int disp_w = vs.sar_num > 0 && vs.sar_den > 0
                                 ? static_cast<int>(std::lround(static_cast<double>(vs.width) * vs.sar_num / vs.sar_den))
                                 : vs.width;
                if ((v.width != vs.width && v.width != disp_w) || v.height != vs.height) mismatch = true;
            }
            c.measured = join(sizes, " / ");
            if (sizes.empty()) {
                c.note = "No video was read.";
            } else if (v.width <= 0) {
                c.note = "Not declared: OPTIONAL, but recommended for video (RFC 8216 §4.3.4.2).";
                add(Severity::Info, idx, name + ": no RESOLUTION", c.note);
            } else {
                c.checked = true;
                c.sev = mismatch ? Severity::Warning : Severity::Ok;
                c.note = mismatch ? std::format("RESOLUTION {} but the video decodes at {}.", c.declared, c.measured)
                                  : std::format("The video decodes at {}.", c.measured);
                if (mismatch) add(Severity::Warning, idx, std::format("{}: RESOLUTION {}, the video is {}", name, c.declared,
                                                                      c.measured), c.note);
            }
        }

        // ── FRAME-RATE ───────────────────────────────────────────────────────
        {
            Cell& c = row.cell(Check::FrameRate);
            c.declared = v.frame_rate > 0.0 ? std::format("{:.3f}", v.frame_rate) : "absent";
            double fps = 0.0;
            for (const SegmentRead* s : vreads) fps = std::max(fps, s->frame_rate);
            if (fps > 0.0) c.measured = std::format("{:.3f}", fps);
            if (fps <= 0.0) {
                c.note = "No frame rate was measured.";
            } else if (v.frame_rate <= 0.0) {
                const bool over30 = fps > 30.0 + kRateSlack;
                c.checked = over30;
                c.sev = over30 ? Severity::Warning : Severity::Info;
                c.note = over30 ? std::format("Not declared, and the video runs at {:.3f} fps: FRAME-RATE SHOULD be "
                                              "included above 30 (RFC 8216 §4.3.4.2).", fps)
                                : "Not declared: OPTIONAL, but recommended for video (RFC 8216 §4.3.4.2).";
                add(c.sev, idx, name + ": no FRAME-RATE", c.note);
            } else {
                c.checked = true;
                const double off = v.frame_rate - fps;
                const bool ok = std::fabs(off) <= 0.0005 + kRateSlack;
                c.sev = ok ? Severity::Ok : Severity::Warning;
                c.note = std::format("FRAME-RATE {:.3f}; the segments read run at up to {:.3f} fps.", v.frame_rate, fps);
                if (!ok) add(Severity::Warning, idx, std::format("{}: FRAME-RATE {:.3f}, the video is {:.3f} fps", name,
                                                                 v.frame_rate, fps),
                             c.note + " FRAME-RATE is the maximum frame rate, rounded to three decimal places "
                                      "(RFC 8216 §4.3.4.2).");
            }
        }

        // ── VIDEO-RANGE ──────────────────────────────────────────────────────
        {
            Cell& c = row.cell(Check::VideoRange);
            c.declared = v.video_range.empty() ? "absent (SDR)" : v.video_range;
            std::set<int> transfers;
            std::set<std::string> dv_pq;   // Dolby Vision with no compatible base layer: PQ
            for (const SegmentRead* s : vreads) {
                const VideoSeen& vs = *s->video;
                if (vs.dv_profile >= 0 && vs.dv_compat == 0) dv_pq.insert(std::format("{}.{}", vs.dv_profile, vs.dv_level));
                else transfers.insert(vs.transfer);
            }
            if (transfers.empty() && dv_pq.empty()) {
                c.note = "No video was read.";
            } else {
                c.checked = true;
                const bool pq = !dv_pq.empty() || transfers.contains(16), hlg = transfers.contains(18);
                const std::string want = pq ? "PQ" : hlg ? "HLG" : "SDR";
                std::string tlist;
                for (const int t : transfers) tlist += std::format("{}{} ({})", tlist.empty() ? "" : ", ", t, transfer_name(t));
                for (const auto& d : dv_pq)
                    tlist += std::format("{}Dolby Vision {} without a compatible base layer (IPT-PQ-c2: PQ, whatever "
                                         "the VUI says)", tlist.empty() ? "" : ", ", d);
                c.measured = want;
                const std::string got = v.video_range.empty() ? "SDR" : v.video_range;
                c.note = std::format("The video: {}.", tlist);
                if (transfers.contains(2) && want == "SDR") c.note += " Unspecified is taken as SDR.";
                if (got != "SDR" && got != "PQ" && got != "HLG") {
                    c.sev = Severity::Warning;
                    add(Severity::Warning, idx, std::format("{}: VIDEO-RANGE \"{}\" is not SDR, HLG or PQ", name, got),
                        "Clients that do not recognise the value SHOULD NOT select the variant "
                        "(draft-pantos-hls-rfc8216bis-22).");
                } else if (got != want) {
                    c.sev = Severity::Error;
                    const std::string detail = std::format(
                        "{} {}, but the video is {}: {}. draft-pantos-hls-rfc8216bis-22: VIDEO-RANGE MUST be PQ for "
                        "transfer 16 and HLG for 18 (also mixed with SDR), SDR for 1, 6, 13, 14, 15; absent means SDR.",
                        v.video_range.empty() ? "No VIDEO-RANGE, which means" : "VIDEO-RANGE", got, want, tlist);
                    c.note = detail;
                    add(Severity::Error, idx, std::format("{}: VIDEO-RANGE {}, the video is {}", name, c.declared, want), detail);
                } else {
                    c.sev = Severity::Ok;
                }
            }
        }

        // ── Segment starts ───────────────────────────────────────────────────
        // One finding per kind of start, naming the segments.
        {
            Cell& c = row.cell(Check::SegmentStart);
            const bool independent = m.independent || (pr_ok && pr->tl.independent);
            int ra = 0, n = 0;
            Severity sev = Severity::Ok;
            std::map<std::string, std::vector<int>> bad;   // what they start with → sequence numbers
            std::vector<int> cra, keyless;
            for (const SegmentRead* s : vreads) {
                ++n;
                const FirstPicture fp = s->first;
                // A codec the NAL parser does not read: the container's keyframe flag.
                const bool random = fp == FirstPicture::Unknown ? s->first_key : is_random_access(fp);
                const int seq = static_cast<int>(seq_of(*pr, s->index));
                if (random) ++ra;
                if (fp == FirstPicture::Cra) cra.push_back(seq);
                if (random) continue;
                const Severity s_sev = fp == FirstPicture::Other && independent ? Severity::Error : Severity::Warning;
                sev = worse(sev, s_sev);
                bad[fp == FirstPicture::Unknown ? std::string("a picture without the keyframe flag")
                                                : std::format("a {} picture", first_picture_name(fp))].push_back(seq);
                if (s->keyframes == 0) keyless.push_back(seq);
            }
            for (const auto& [what, seqs] : bad) {
                const bool p_b = what == std::format("a {} picture", first_picture_name(FirstPicture::Other));
                const Severity s_sev = p_b && independent ? Severity::Error : Severity::Warning;
                std::string title = seqs.size() == 1
                    ? std::format("{}: segment #{} starts with {}", name, seqs[0], what)
                    : std::format("{}: {} of {} segments read start with {} (#{})", name, seqs.size(), n, what,
                                  format_list(seqs));
                std::string none;
                if (!keyless.empty())
                    none = std::format(" {} no random access point at all: #{}.",
                                       keyless.size() == 1 ? "One has" : std::format("{} have", keyless.size()),
                                       format_list(keyless));
                add(s_sev, idx, std::move(title),
                    std::format("A player that starts or switches there cannot show anything until the next random "
                                "access point.{}{}", none,
                                independent ? " EXT-X-INDEPENDENT-SEGMENTS says every segment decodes without the "
                                              "ones before it (RFC 8216 §4.3.5.1)."
                                            : " RFC 8216 §3: a segment SHOULD hold what initialises a decoder; an "
                                              "H.264 segment an IDR."));
            }
            if (!cra.empty())
                add(Severity::Info, idx, std::format("{}: segments start with CRA pictures", name),
                    std::format("#{}: clean random access points. The RASL pictures that follow one reference the "
                                "segment before, and are dropped when playback starts or switches there.",
                                format_list(cra)));
            if (n == 0) {
                c.note = "No video was read.";
            } else {
                c.checked = true;
                c.sev = sev;
                std::map<std::string, int> kinds;
                for (const SegmentRead* s : vreads) ++kinds[first_picture_name(s->first)];
                std::string ktext;
                for (const auto& [k, cnt] : kinds) ktext += std::format("{}{} {}", ktext.empty() ? "" : ", ", cnt, k);
                c.measured = std::format("{}/{} random access", ra, n);
                c.declared = independent ? "independent" : "—";
                c.note = std::format("First pictures of the {} segment(s) read: {}.", n, ktext);
            }
        }

        // ── Segment durations ────────────────────────────────────────────────
        // A segment's pictures span its EXTINF when its GOPs are closed. With
        // leading pictures (an open GOP, or a cut inside a GOP), some of the
        // next segment's pictures show before its first one, inside this
        // segment's time: the spans are then noted, not reported.
        {
            Cell& c = row.cell(Check::Duration);
            double worst_d = 0.0;
            int n = 0;
            bool leading = false;
            for (const SegmentRead* s : vreads) leading = leading || s->leading > 0;
            std::vector<int> off;
            std::string list;
            for (const SegmentRead* s : vreads) {
                const double extinf = extinf_of(*pr, s->index);
                if (!std::isfinite(s->media_duration) || !std::isfinite(extinf)) continue;
                ++n;
                const double d = s->media_duration - extinf;
                if (std::fabs(d) > std::fabs(worst_d)) worst_d = d;
                if (std::fabs(d) <= kDurationSlack) continue;
                const int seq = static_cast<int>(seq_of(*pr, s->index));
                off.push_back(seq);
                if (off.size() <= 8)
                    list += std::format("{}#{} {:.3f} s against {:.3f} s", list.empty() ? "" : "; ", seq,
                                        s->media_duration, extinf);
            }
            const Severity sev = off.empty() ? Severity::Ok : leading ? Severity::Info : Severity::Warning;
            if (!off.empty()) {
                const std::string title = off.size() == 1
                    ? std::format("{}: segment #{}'s pictures span {:+.3f} s off its EXTINF", name, off[0], worst_d)
                    : std::format("{}: {} of {} segments' pictures span more than {:g} ms off their EXTINF (#{})", name,
                                  off.size(), n, kDurationSlack * 1000.0, format_list(off));
                add(sev, idx, title,
                    std::format("{}{}.{}", list, off.size() > 8 ? "; …" : "",
                                leading ? " The segments have leading pictures (an open GOP, or cuts inside GOPs): "
                                          "the next segment's first pictures show in this one's time, so the spans "
                                          "differ from EXTINF by them."
                                        : " EXTINF is a segment's duration (RFC 8216 §4.3.2.1): players lay out "
                                          "the timeline, and seek, by it."));
            }
            if (n == 0) {
                c.note = "No video duration was measured.";
            } else {
                c.checked = true;
                c.sev = sev;
                c.declared = "EXTINF";
                c.measured = std::format("{:+.3f} s", worst_d);
                c.note = std::format("Largest difference between a segment's pictures' span and its EXTINF, over {} "
                                     "segment(s): {:+.3f} s.{}", n, worst_d,
                                     leading ? " Leading pictures: spans differ from EXTINF by design." : "");
            }
        }

        // ── Audio renditions: CHANNELS ───────────────────────────────────────
        for (const auto& md : m.media) {
            if (md.type != "AUDIO" || v.audio.empty() || md.group_id != v.audio || md.uri.empty()) continue;
            const PlaylistRead* ar = read_of(md.uri);
            if (!ar || !ar->error.empty()) {
                if (ar) add(Severity::Warning, idx, short_name(md.uri) + ": could not be read", ar->error);
                continue;
            }
            const AudioSeen* a = nullptr;
            for (const auto& s : ar->reads) if (s.audio) { a = &*s.audio; break; }
            if (!a || a->channels <= 0) continue;
            const std::string rname = std::format("audio \"{}\" ({})", md.name, short_name(md.uri));
            if (md.channels.empty()) {
                add(Severity::Info, idx, rname + ": no CHANNELS",
                    std::format("It decodes to {} channels. RFC 8216 §4.3.4.1: every audio EXT-X-MEDIA SHOULD have "
                                "CHANNELS.", a->channels));
                continue;
            }
            const auto parts = split(md.channels, '/');
            const int declared = parts.empty() ? -1 : to_small(parts[0]);
            if (parts.size() > 1) continue;   // "16/JOC": objects beyond the channels decoded
            if (declared != a->channels)
                add(Severity::Warning, idx, std::format("{}: CHANNELS \"{}\", it decodes to {} channels", rname, md.channels,
                                                       a->channels),
                    "RFC 8216 §4.3.4.1: the first CHANNELS parameter is the largest number of independent, simultaneous "
                    "channels in any segment of the rendition.");
        }

        for (const Cell& c : row.cells) if (c.checked) row.worst = worse(row.worst, c.sev);
    }

    if (!slight.empty()) {
        const auto most = std::max_element(slight.begin(), slight.end(),
                                           [](const Slight& a, const Slight& b) { return a.frac < b.frac; });
        Finding f;
        f.sev = Severity::Info;
        f.title = std::format("BANDWIDTH of {} variant{} up to {:.2g}% below the measured peak", slight.size(),
                              slight.size() == 1 ? "" : "s", most->frac * 100.0);
        f.detail = std::format("Short by no more than {:g}% of the peak segment bit rate, which RFC 8216 §4.3.4.2 makes "
                               "the sum of each rendition's own peak: a peak computed per segment, or over the video and "
                               "audio of the same time together, lands this close. The most: variant #{} ({}), {:.0f} "
                               "b/s.", kPeakSlack * 100.0, most->variant,
                               short_name(m.variants[static_cast<std::size_t>(most->variant)].uri), most->bps);
        for (const auto& s : slight) f.renditions.push_back(s.variant);
        found.push_back(std::move(f));
    }

    std::stable_sort(found.begin(), found.end(),
                     [](const Finding& a, const Finding& b) { return static_cast<int>(a.sev) < static_cast<int>(b.sev); });
    for (const auto& f : found) {
        if (f.sev == Severity::Error) ++rep.errors;
        else if (f.sev == Severity::Warning) ++rep.warnings;
    }
    rep.findings = std::move(found);
    return rep;
}

// ── JSON ─────────────────────────────────────────────────────────────────────

std::string conformance_json(const MasterDecl& m, const std::vector<PlaylistRead>& reads, const ConformReport& r,
                             const std::string& source)
{
    using json::number;
    using json::quote;
    auto num_or_null = [](double v, bool have) { return have ? number(v) : std::string("null"); };
    std::string o = "{\n";
    o += std::format("  \"source\": {},\n", quote(source));
    o += std::format("  \"summary\": {{\"variants\": {}, \"playlists\": {}, \"errors\": {}, \"warnings\": {}}},\n",
                     m.variants.size(), reads.size(), r.errors, r.warnings);
    o += std::format("  \"independent_segments\": {},\n", m.independent ? "true" : "false");

    o += "  \"variants\": [";
    for (std::size_t i = 0; i < m.variants.size(); ++i) {
        const VariantDecl& v = m.variants[i];
        const VariantRow*  row = i < r.rows.size() ? &r.rows[i] : nullptr;
        std::string codecs;
        for (const auto& c : v.codecs) codecs += (codecs.empty() ? "" : ", ") + quote(c);
        o += i ? ",\n    {" : "\n    {";
        o += std::format("\"index\": {}, \"line\": {}, \"uri\": {},\n", i, v.line, quote(v.uri));
        o += std::format("     \"declared\": {{\"bandwidth\": {}, \"average_bandwidth\": {}, \"codecs\": {}, "
                         "\"resolution\": {}, \"frame_rate\": {}, \"video_range\": {}, \"audio\": {}}},\n",
                         v.bandwidth >= 0 ? std::to_string(v.bandwidth) : "null",
                         v.avg_bandwidth >= 0 ? std::to_string(v.avg_bandwidth) : "null",
                         v.has_codecs ? "[" + codecs + "]" : "null",
                         v.width > 0 ? quote(std::format("{}x{}", v.width, v.height)) : "null",
                         num_or_null(v.frame_rate, v.frame_rate > 0.0),
                         v.video_range.empty() ? "null" : quote(v.video_range),
                         v.audio.empty() ? "null" : quote(v.audio));
        if (row) {
            o += std::format("     \"measured\": {{\"peak_bps\": {}, \"average_bps\": {}, \"rates_complete\": {}, "
                             "\"segments_read\": {}}},\n",
                             num_or_null(std::round(row->peak), row->peak > 0.0),
                             num_or_null(std::round(row->average), row->average > 0.0),
                             row->rates_complete ? "true" : "false", row->read);
            o += "     \"checks\": {";
            for (std::size_t k = 0; k < row->cells.size(); ++k) {
                const Cell& c = row->cells[k];
                o += std::format("{}\n       {}: {{\"checked\": {}, \"severity\": {}, \"declared\": {}, \"measured\": {}, "
                                 "\"note\": {}}}", k ? "," : "", quote(check_name(static_cast<Check>(k))),
                                 c.checked ? "true" : "false", quote(c.checked ? severity_name(c.sev) : "not checked"),
                                 quote(c.declared), quote(c.measured), quote(c.note));
            }
            o += std::format("}},\n     \"worst\": {}}}", quote(severity_name(row->worst)));
        } else {
            o += "     \"measured\": null}";
        }
    }
    o += "\n  ],\n";

    o += "  \"findings\": [";
    for (std::size_t i = 0; i < r.findings.size(); ++i) {
        const Finding& f = r.findings[i];
        std::string vs;
        for (const int k : f.renditions) vs += (vs.empty() ? "" : ", ") + std::to_string(k);
        o += std::format("{}\n    {{\"severity\": {}, \"title\": {}, \"detail\": {}, \"variants\": [{}]}}", i ? "," : "",
                         quote(severity_name(f.sev)), quote(f.title), quote(f.detail), vs);
    }
    o += "\n  ],\n";

    o += "  \"playlists\": [";
    for (std::size_t i = 0; i < reads.size(); ++i) {
        const PlaylistRead& p = reads[i];
        int sized = 0;
        std::int64_t bytes = 0;
        for (const auto b : p.sizes) if (b >= 0) { ++sized; bytes += b; }
        o += std::format("{}\n    {{\"uri\": {}, \"error\": {}, \"target_duration\": {}, \"segments\": {}, \"sized\": {}, "
                         "\"bytes\": {}, \"duration\": {}, \"key\": {}, \"reads\": [",
                         i ? "," : "", quote(p.uri), p.error.empty() ? "null" : quote(p.error),
                         num_or_null(p.tl.target, p.tl.ok), p.tl.segments.size(), sized, bytes,
                         num_or_null(p.tl.total, p.tl.ok), p.key.empty() ? "null" : quote(p.key));
        for (std::size_t k = 0; k < p.reads.size(); ++k) {
            const SegmentRead& s = p.reads[k];
            o += std::format("{}\n      {{\"index\": {}, \"sequence\": {}, \"bytes\": {}, \"extinf\": {}", k ? "," : "",
                             s.index, seq_of(p, s.index), s.bytes, num_or_null(extinf_of(p, s.index),
                                                                                std::isfinite(extinf_of(p, s.index))));
            if (!s.error.empty()) o += std::format(", \"error\": {}", quote(s.error));
            if (s.video) {
                const VideoSeen& v = *s.video;
                o += std::format(", \"first_picture\": {}, \"first_keyframe_flag\": {}, \"frames\": {}, \"keyframes\": {}, "
                                 "\"leading\": {}, \"media_duration\": {}, \"frame_rate\": {},\n       \"video\": {{\"codec\": {}, "
                                 "\"codec_string\": {}, \"sample_entry\": {}, \"width\": {}, \"height\": {}, \"sar\": {}, "
                                 "\"transfer\": {}, \"bit_depth\": {}, \"dolby_vision\": {}}}",
                                 quote(first_picture_name(s.first)), s.first_key ? "true" : "false", s.frames, s.keyframes,
                                 s.leading,
                                 num_or_null(s.media_duration, std::isfinite(s.media_duration)),
                                 num_or_null(s.frame_rate, s.frame_rate > 0.0), quote(v.codec), quote(v.codec_string),
                                 v.sample_entry.empty() ? "null" : quote(v.sample_entry), v.width, v.height,
                                 v.sar_num > 0 ? quote(std::format("{}:{}", v.sar_num, v.sar_den)) : "null", v.transfer,
                                 v.bit_depth, v.dv_profile >= 0 ? quote(std::format("{}.{} compatibility {}", v.dv_profile,
                                                                                    v.dv_level, v.dv_compat))
                                                                : "null");
            }
            if (s.audio)
                o += std::format(",\n       \"audio\": {{\"codec\": {}, \"codec_string\": {}, \"channels\": {}, "
                                 "\"sample_rate\": {}}}", quote(s.audio->codec), quote(s.audio->codec_string),
                                 s.audio->channels, s.audio->sample_rate);
            o += "}";
        }
        o += p.reads.empty() ? "]}" : "\n    ]}";
    }
    o += "\n  ]\n}\n";
    return o;
}

} // namespace hls
