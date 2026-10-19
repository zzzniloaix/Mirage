#include "NetDescribe.h"

#include "HlsAlign.h"

#include <algorithm>
#include <cctype>
#include <format>

namespace {

std::string lower(std::string s)
{
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// A variant whose CODECS lists no video codec: an audio-only EXT-X-STREAM-INF.
bool audio_only(const std::string& codecs)
{
    if (codecs.empty()) return false;
    static constexpr const char* kVideo[] = { "avc1", "avc3", "hvc1", "hev1", "dvh1", "dvhe", "dva1", "dvav",
                                              "av01", "vp09", "vp08", "mp4v" };
    const std::string c = lower(codecs);
    return std::none_of(std::begin(kVideo), std::end(kVideo),
                        [&](const char* v) { return c.find(v) != std::string::npos; });
}

// URL without query / fragment.
std::string strip_query(const std::string& url)
{
    const auto q = url.find_first_of("?#");
    return q == std::string::npos ? url : url.substr(0, q);
}

std::string dir_of(const std::string& url)
{
    const std::string u = strip_query(url);
    const auto slash = u.rfind('/');
    return slash == std::string::npos ? std::string() : u.substr(0, slash + 1);
}

// Last directory component of a playlist URL: ".../v9/prog_index.m3u8" → "v9".
std::string last_dir_name(const std::string& url)
{
    std::string d = dir_of(url);
    if (!d.empty()) d.pop_back();
    const auto slash = d.rfind('/');
    std::string name = slash == std::string::npos ? d : d.substr(slash + 1);
    if (name.find(':') != std::string::npos) name.clear();   // "https:" — no real dir
    return name;
}

std::string stem_of(const std::string& url)
{
    std::string n = net_request_name(url);
    const auto dot = n.rfind('.');
    return dot == std::string::npos ? n : n.substr(0, dot);
}

std::string ext_of(const std::string& url)
{
    const std::string n = net_request_name(url);
    const auto dot = n.rfind('.');
    return dot == std::string::npos ? std::string() : lower(n.substr(dot));
}

} // namespace

const char* net_kind_name(NetKind k)
{
    switch (k) {
        case NetKind::Manifest: return "manifest";
        case NetKind::Playlist: return "playlist";
        case NetKind::Init:     return "init";
        case NetKind::Segment:  return "segment";
        case NetKind::Subtitle: return "subtitle";
        case NetKind::Key:      return "key";
        case NetKind::Media:    return "media";
        case NetKind::File:     return "file";
        case NetKind::Other:    break;
    }
    return "other";
}

std::string net_request_name(const std::string& url)
{
    const std::string u = strip_query(url);
    std::string_view v(u);
    const auto scheme = v.find("://");
    std::string_view host;
    if (scheme != std::string_view::npos) {
        const auto host_start = scheme + 3;
        const auto slash = v.find('/', host_start);
        host = v.substr(host_start, slash == std::string_view::npos ? std::string_view::npos : slash - host_start);
    }
    while (v.size() > 1 && v.back() == '/') v.remove_suffix(1);
    const auto slash = v.rfind('/');
    std::string_view name = slash == std::string_view::npos ? v : v.substr(slash + 1);
    if (name.empty() || (scheme != std::string_view::npos && name == host)) return std::string(host.empty() ? v : host);
    return std::string(name);
}

std::string net_format_bytes(std::int64_t b)
{
    if (b < 0)            return "—";
    if (b < 1000)         return std::format("{} B", b);
    if (b < 1000 * 1000)  return std::format("{:.1f} kB", static_cast<double>(b) / 1e3);
    if (b < 1000000000LL) return std::format("{:.2f} MB", static_cast<double>(b) / 1e6);
    return std::format("{:.2f} GB", static_cast<double>(b) / 1e9);
}

std::string net_format_duration_us(std::int64_t us)
{
    if (us < 0)            return "—";
    if (us < 1000)         return std::format("{:.2f} ms", static_cast<double>(us) / 1e3);
    if (us < 1000 * 1000)  return std::format("{} ms", (us + 500) / 1000);
    if (us < 60000000LL)   return std::format("{:.2f} s", static_cast<double>(us) / 1e6);
    const std::int64_t s = (us + 500000) / 1000000;
    return std::format("{}:{:02} min", s / 60, s % 60);
}

// ── Catalog ──────────────────────────────────────────────────────────────────

NetCatalog NetCatalog::build(const std::string& manifest_url,
                             const std::vector<VariantStream>& variants,
                             const std::vector<MediaRendition>& media)
{
    NetCatalog cat;
    cat.manifest_url = strip_query(manifest_url);

    // Label a playlist by its directory ("v9") when directories are unique
    // per playlist, else by the file stem ("720p" of ".../720p.m3u8").
    auto name_for = [&](const std::string& url, auto&& all_urls) {
        const std::string d = dir_of(url);
        int same_dir = 0;
        for (const auto& u : all_urls) if (dir_of(u) == d && strip_query(u) != strip_query(url)) ++same_dir;
        std::string n = (same_dir == 0) ? last_dir_name(url) : std::string();
        return n.empty() ? stem_of(url) : n;
    };
    std::vector<std::string> all;
    for (const auto& v : variants) if (!v.url.empty()) all.push_back(v.url);
    for (const auto& m : media)    if (!m.url.empty()) all.push_back(m.url);

    for (std::size_t i = 0; i < variants.size(); ++i) {
        const auto& v = variants[i];
        if (v.url.empty()) continue;
        const std::string key = strip_query(v.url);
        if (std::any_of(cat.renditions.begin(), cat.renditions.end(),
                        [&](const NetRendition& r) { return r.url == key; }))
            continue;   // same playlist listed under several audio groups
        NetRendition r;
        r.url     = key;
        r.dir     = dir_of(v.url);
        r.media   = audio_only(v.codecs) ? NetMedia::Audio : NetMedia::Video;
        r.variant = static_cast<int>(i);
        r.label   = name_for(v.url, all);
        if (v.height > 0) {
            r.label += std::format(" {}p", v.height);
            if (v.frame_rate >= 47.0) r.label += std::format("{}", static_cast<int>(v.frame_rate + 0.5));
        } else if (v.bandwidth > 0) {
            r.label += std::format(" {} kb/s", v.bandwidth / 1000);
        }
        cat.renditions.push_back(std::move(r));
    }
    for (const auto& m : media) {
        if (m.url.empty()) continue;
        NetRendition r;
        r.url   = strip_query(m.url);
        r.dir   = dir_of(m.url);
        r.media = m.type == "AUDIO" ? NetMedia::Audio
                : m.type == "SUBTITLES" ? NetMedia::Subs
                : m.type == "VIDEO" ? NetMedia::Video : NetMedia::None;
        r.label = name_for(m.url, all);
        if (!m.name.empty()) r.label += " " + m.name;
        if (!m.channels.empty() && r.media == NetMedia::Audio) r.label += " " + m.channels + "ch";
        cat.renditions.push_back(std::move(r));
    }
    return cat;
}

static std::string init_key(const std::string& url, std::int64_t offset)
{
    return std::format("{}@{}", url, offset < 0 ? 0 : offset);
}

bool NetCatalog::is_listed(const std::string& url) const
{
    return listed.contains(strip_query(url));
}

bool NetCatalog::is_init(const std::string& url, std::int64_t range_start) const
{
    return !inits.empty() && inits.contains(init_key(strip_query(url), range_start));
}

bool NetCatalog::learn(const std::string& playlist_url, const std::string& text)
{
    const std::string key = strip_query(playlist_url);
    int idx = -1;
    for (std::size_t i = 0; i < renditions.size(); ++i)
        if (renditions[i].url == key) idx = static_cast<int>(i);
    if (idx < 0) return false;
    const hls::MediaTimeline tl = hls::parse_media_timeline(text, playlist_url);
    if (!tl.ok) return false;
    // A live playlist keeps listing new segments: bounded, the newest
    // playlists teach it again.
    if (listed.size() > 200'000) {
        listed.clear();
        inits.clear();
    }
    bool added = false;
    for (const auto& m : tl.maps) {
        const std::string u = strip_query(m.uri);
        added |= listed.try_emplace(u, idx).second;
        inits.insert(init_key(u, m.range.offset));
    }
    for (const auto& sg : tl.segments)
        added |= listed.try_emplace(strip_query(sg.uri), idx).second;
    if (added) ++learned;
    return added;
}

const NetRendition* NetCatalog::match(const std::string& url) const
{
    const std::string u = strip_query(url);
    for (const auto& r : renditions)
        if (r.url == u) return &r;
    if (const auto it = listed.find(u); it != listed.end() && it->second < static_cast<int>(renditions.size()))
        return &renditions[static_cast<std::size_t>(it->second)];
    const NetRendition* best = nullptr;
    std::size_t best_len = 0;
    for (const auto& r : renditions) {
        // A shared directory (all playlists next to the master) says nothing.
        if (r.dir.empty() || r.dir == dir_of(manifest_url)) continue;
        if (u.starts_with(r.dir) && r.dir.size() > best_len) {
            best = &r;
            best_len = r.dir.size();
        }
    }
    return best;
}

// ── Classification ───────────────────────────────────────────────────────────

NetDesc net_describe(const NetRequest& r, const NetCatalog& cat)
{
    NetDesc d;
    const std::string url  = strip_query(r.url);
    const std::string ext  = ext_of(url);
    const std::string mime = lower(r.mime);
    const std::string path = lower(url);
    const NetRendition* rend = cat.match(url);
    if (rend) {
        d.media     = rend->media;
        d.rendition = rend->label;
    }

    const bool is_playlist_ext = ext == ".m3u8" || ext == ".m3u" || mime.find("mpegurl") != std::string::npos;
    const bool is_mpd = ext == ".mpd" || mime.find("dash+xml") != std::string::npos;

    if ((!cat.manifest_url.empty() && url == cat.manifest_url) || is_mpd) {
        d.kind = NetKind::Manifest;
        d.media = NetMedia::None;
        d.rendition.clear();
        return d;
    }
    if (is_playlist_ext) {
        // Without a parsed manifest, fall back to the URL heuristic.
        if (cat.renditions.empty() && cat.manifest_url.empty()) {
            float cr, cg, cb;
            d.kind = NetworkLogger::classify(url, cr, cg, cb) == "manifest" ? NetKind::Manifest : NetKind::Playlist;
        } else {
            d.kind = NetKind::Playlist;
        }
        return d;
    }
    if (ext == ".key" || mime == "application/octet-stream" && path.find("key") != std::string::npos
        || path.find("/key/") != std::string::npos || path.find("/keys/") != std::string::npos) {
        d.kind = NetKind::Key;
        return d;
    }
    if (ext == ".vtt" || ext == ".webvtt" || ext == ".srt" || ext == ".ttml" || ext == ".dfxp"
        || mime.starts_with("text/vtt") || mime.find("ttml") != std::string::npos) {
        d.kind  = NetKind::Subtitle;
        d.media = NetMedia::Subs;
        return d;
    }
    // A local file being played — unless a playlist lists it (local HLS).
    if (r.protocol == "file" && !rend && !cat.is_listed(url)) {
        d.kind = NetKind::File;
        return d;
    }

    // Media kind from the extension when the manifest could not say.
    if (d.media == NetMedia::None) {
        if (ext == ".aac" || ext == ".m4a" || ext == ".mp4a" || ext == ".cmfa" || ext == ".ac3"
            || ext == ".ec3" || mime.starts_with("audio/"))
            d.media = NetMedia::Audio;
        else if (ext == ".m4v" || ext == ".cmfv" || ext == ".ts" || mime.starts_with("video/"))
            d.media = NetMedia::Video;
    }

    const bool named_init = path.find("init") != std::string::npos
                         || path.find("header") != std::string::npos;
    // EXT-X-MAP with BYTERANGE: a small range at offset 0 of the media file.
    const std::int64_t len = r.range_end >= 0 && r.range_start >= 0
                           ? r.range_end - r.range_start + 1 : r.content_length;
    const bool small_head = r.range_start == 0 && len > 0 && len < 64 * 1024;
    const bool media_ext  = ext == ".mp4" || ext == ".m4s" || ext == ".m4v" || ext == ".m4a"
                         || ext == ".cmfv" || ext == ".cmfa" || ext == ".mp4a" || ext == ".ts"
                         || ext == ".aac";

    // What the playlists list settles it; the URL and size only guess.
    if (cat.is_init(url, r.range_start)) {
        d.kind = NetKind::Init;
        return d;
    }
    if (!cat.is_listed(url) && ((named_init && (media_ext || ext.empty())) || (rend && small_head))) {
        d.kind = NetKind::Init;
        return d;
    }
    if (rend || ext == ".m4s" || ext == ".ts" || ext == ".aac" || ext == ".m4a" || ext == ".cmfv"
        || ext == ".cmfa" || ext == ".mp4a" || ext == ".m4v"
        || (ext == ".mp4" && (path.find("seg") != std::string::npos || path.find("frag") != std::string::npos
                              || path.find("chunk") != std::string::npos))) {
        d.kind = NetKind::Segment;
        return d;
    }
    if (ext == ".mp4" || ext == ".mkv" || ext == ".mov" || ext == ".webm" || mime.starts_with("video/")) {
        d.kind = NetKind::Media;
        return d;
    }
    d.kind = NetKind::Other;
    return d;
}
