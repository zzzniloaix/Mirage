#include "HlsMaster.h"

#include <cctype>
#include <cstdlib>
#include <set>
#include <sstream>

namespace hls {

namespace {

bool is_http(const std::string& s)
{
    return s.starts_with("http://") || s.starts_with("https://");
}

// Value of attribute `name` in an HLS attribute list (quotes stripped),
// matched only at the start of an attribute: "BANDWIDTH" never matches
// inside "AVERAGE-BANDWIDTH".
std::string attr(const std::string& attrs, const char* name)
{
    const std::string key = std::string(name) + "=";
    for (std::size_t pos = 0;; ++pos) {
        pos = attrs.find(key, pos);
        if (pos == std::string::npos) return {};
        if (pos != 0 && attrs[pos - 1] != ',') continue;
        pos += key.size();
        if (pos < attrs.size() && attrs[pos] == '"') {
            const auto end = attrs.find('"', pos + 1);
            return end == std::string::npos ? std::string() : attrs.substr(pos + 1, end - pos - 1);
        }
        const auto end = attrs.find(',', pos);
        return attrs.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    }
}

// `line` with the quoted URI="..." attribute value made absolute; false if it
// has one that can't be.
bool absolutize_uri_attr(std::string& line, const std::string& base)
{
    const auto colon = line.find(':');
    if (colon == std::string::npos) return true;
    for (std::size_t pos = colon + 1;; ++pos) {
        pos = line.find("URI=\"", pos);
        if (pos == std::string::npos) return true;
        if (line[pos - 1] != ',' && pos != colon + 1) continue;
        const std::size_t v0 = pos + 5;
        const auto v1 = line.find('"', v0);
        if (v1 == std::string::npos) return false;
        const std::string abs = absolute_uri(base, line.substr(v0, v1 - v0));
        if (abs.empty()) return false;
        line.replace(v0, v1 - v0, abs);
        return true;
    }
}

std::string trim(const std::string& s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

} // namespace

std::string absolute_uri(const std::string& base, const std::string& uri)
{
    if (!is_http(base) || uri.empty()) return {};
    for (const char c : uri)
        if (std::isspace(static_cast<unsigned char>(c)) || c == '\\') return {};
    // Dot segments: FFmpeg normalises them; this does not try to.
    const std::string path = uri.substr(0, uri.find_first_of("?#"));
    if (path == "." || path == ".." || path.starts_with("./") || path.starts_with("../")
        || path.find("/./") != std::string::npos || path.find("/../") != std::string::npos
        || path.ends_with("/.") || path.ends_with("/.."))
        return {};
    if (is_http(uri)) return uri;
    if (uri.starts_with("//") || uri[0] == '?' || uri[0] == '#') return {};
    // Any other scheme ("data:", "skd:", ...): a ':' before the first '/'.
    if (const auto c = uri.find(':'); c != std::string::npos && c < uri.find('/')) return {};

    const std::string b = base.substr(0, base.find_first_of("?#"));
    const std::size_t host = b.find("://") + 3;
    const std::size_t root = b.find('/', host);
    if (uri[0] == '/') return (root == std::string::npos ? b : b.substr(0, root)) + uri;
    if (root == std::string::npos) return b + "/" + uri;
    return b.substr(0, b.rfind('/') + 1) + uri;
}

std::optional<MasterCopy> dedupe_master(const std::string& text, const std::string& base_url)
{
    if (!is_http(base_url)) return std::nullopt;

    struct Entry {
        std::size_t inf_line = 0, uri_line = 0;   // indices into `lines`
        std::string uri;                          // absolute
        long long   bandwidth = 0;
        std::string groups[3];                    // AUDIO, VIDEO, SUBTITLES
    };
    std::vector<std::string> lines;   // output candidates, in order
    std::vector<bool>        keep;    // parallel to lines
    std::vector<Entry>       entries;

    std::istringstream ss(text);
    std::string raw;
    bool seen_header = false;
    bool want_uri    = false;   // previous line was EXT-X-STREAM-INF
    while (std::getline(ss, raw)) {
        const std::string line = trim(raw);
        if (line.empty()) {
            if (want_uri) return std::nullopt;   // FFmpeg would still pair them; keep it simple
            continue;
        }
        if (!seen_header) {
            if (line != "#EXTM3U") return std::nullopt;
            seen_header = true;
            lines.push_back(line);
            keep.push_back(true);
            continue;
        }
        if (want_uri) {
            if (line[0] == '#') return std::nullopt;
            Entry& e = entries.back();
            e.uri = absolute_uri(base_url, line);
            if (e.uri.empty()) return std::nullopt;
            e.uri_line = lines.size();
            lines.push_back(e.uri);
            keep.push_back(true);
            want_uri = false;
            continue;
        }
        if (line[0] != '#') return std::nullopt;   // a URI without EXT-X-STREAM-INF
        if (!line.starts_with("#EXT")) continue;    // comment
        if (line.starts_with("#EXT-X-STREAM-INF:")) {
            Entry e;
            const std::string a = line.substr(18);
            e.inf_line  = lines.size();
            e.bandwidth = std::atoll(attr(a, "BANDWIDTH").c_str());
            e.groups[0] = attr(a, "AUDIO");
            e.groups[1] = attr(a, "VIDEO");
            e.groups[2] = attr(a, "SUBTITLES");
            entries.push_back(std::move(e));
            lines.push_back(line);
            keep.push_back(true);
            want_uri = true;
        } else if (line.starts_with("#EXT-X-MEDIA:")) {
            std::string l = line;
            if (!absolutize_uri_attr(l, base_url)) return std::nullopt;
            lines.push_back(std::move(l));
            keep.push_back(true);
        } else if (line.starts_with("#EXT-X-VERSION:") || line == "#EXT-X-INDEPENDENT-SEGMENTS") {
            lines.push_back(line);
            keep.push_back(true);
        } else if (line.starts_with("#EXT-X-I-FRAME-STREAM-INF:") || line.starts_with("#EXT-X-SESSION-DATA:")) {
            continue;   // FFmpeg ignores both
        } else {
            // SESSION-KEY, DEFINE, CONTENT-STEERING, EXTINF (a media playlist),
            // START (in a master, FFmpeg turns the master URL itself into a
            // playlist — which would be the data: URL), ...
            return std::nullopt;
        }
    }
    if (want_uri || entries.empty()) return std::nullopt;

    // First entry of every URI.
    std::vector<bool> kept(entries.size(), false);
    std::set<std::string> uris;
    for (std::size_t i = 0; i < entries.size(); ++i)
        kept[i] = uris.insert(entries[i].uri).second;
    if (uris.size() == entries.size()) return std::nullopt;   // no duplicates

    // Every group referenced at all stays referenced by a kept entry.
    std::set<std::string> covered;
    auto group_key = [](int kind, const std::string& g) { return std::to_string(kind) + ":" + g; };
    auto cover = [&](const Entry& e) {
        for (int k = 0; k < 3; ++k)
            if (!e.groups[k].empty()) covered.insert(group_key(k, e.groups[k]));
    };
    for (std::size_t i = 0; i < entries.size(); ++i)
        if (kept[i]) cover(entries[i]);
    for (std::size_t i = 0; i < entries.size(); ++i) {
        for (int k = 0; k < 3; ++k) {
            const std::string& g = entries[i].groups[k];
            if (g.empty() || covered.count(group_key(k, g))) continue;
            std::size_t best = i;
            for (std::size_t j = i + 1; j < entries.size(); ++j)
                if (entries[j].groups[k] == g && entries[j].bandwidth < entries[best].bandwidth) best = j;
            kept[best] = true;
            cover(entries[best]);
        }
    }

    MasterCopy out;
    for (std::size_t i = 0; i < entries.size(); ++i) {
        if (kept[i]) { out.kept.push_back(static_cast<int>(i)); continue; }
        keep[entries[i].inf_line] = false;
        keep[entries[i].uri_line] = false;
    }
    for (std::size_t l = 0; l < lines.size(); ++l)
        if (keep[l]) out.text += lines[l] + "\n";
    return out;
}

} // namespace hls
