#include "ManifestParser.h"
#include "HlsMaster.h"
#include "Logger.h"
#include "NetworkLogger.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <cstring>
#include <cstdlib>

extern "C" {
#include <libavformat/avio.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
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

bool ManifestParser::is_dash(const std::string& url)
{
    const std::string low = str_lower(url);
    return str_ends_with(low, ".mpd") || low.find(".mpd?") != std::string::npos;
}

bool ManifestParser::parse(const std::string& url, const std::atomic<bool>* abort)
{
    tags_.clear();
    variants_.clear();
    renditions_.clear();
    text_.clear();
    final_url_.clear();
    set_cookies_ = false;
    if (is_dash(url))
        return parse_dash(url, abort);
    return parse_hls(url, abort);
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

static int fetch_interrupt_cb(void* opaque)
{
    return static_cast<const std::atomic<bool>*>(opaque)->load() ? 1 : 0;
}

bool ManifestParser::fetch_text(const std::string& url, std::string& out, std::string& final_url,
                                bool& cookies, const std::atomic<bool>* abort)
{
    final_url = url;
    cookies   = false;
    AVIOContext* ctx = nullptr;
    const AVIOInterruptCB cb{ fetch_interrupt_cb, const_cast<std::atomic<bool>*>(abort) };
    AVDictionary* opts = nullptr;
    if (url.starts_with("http://") || url.starts_with("https://")) {
        av_dict_set(&opts, "timeout",    "5000000", 0);   // 5 s socket connect/recv
        av_dict_set(&opts, "rw_timeout", "8000000", 0);   // 8 s cap on any single I/O
    }
    NetworkLogger::track_open_begin(NetInitiator::Manifest, url.c_str());
    const int ret = avio_open2(&ctx, url.c_str(), AVIO_FLAG_READ, abort ? &cb : nullptr, &opts);
    av_dict_free(&opts);
    NetworkLogger::track_open_end(ret, ctx);
    if (ret < 0) {
        if (!(abort && abort->load()))
            logger::warn("ManifestParser: cannot open {}", url);
        return false;
    }
    // HTTP: the URL the body came from after redirects.
    if (uint8_t* loc = nullptr; av_opt_get(ctx, "location", AV_OPT_SEARCH_CHILDREN, &loc) >= 0 && loc) {
        if (*loc) final_url = reinterpret_cast<const char*>(loc);
        av_free(loc);
    }

    std::string result;
    result.reserve(65536);
    uint8_t buf[4096];
    int n;
    while ((n = avio_read(ctx, buf, sizeof(buf))) > 0)
        result.append(reinterpret_cast<char*>(buf), static_cast<size_t>(n));
    const bool aborted = abort && abort->load();
    // A body cut short (timeout, dropped connection) is a failed fetch, not a
    // shorter manifest: playback would otherwise see some variants only.
    const int read_err = ctx->error < 0 ? ctx->error : (n < 0 && n != AVERROR_EOF ? n : 0);
    if (read_err < 0 && !aborted) {
        char errbuf[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(read_err, errbuf, sizeof(errbuf));
        logger::warn("ManifestParser: reading {} failed after {} bytes: {}", url, result.size(), errbuf);
    }
    // HTTP: Set-Cookie values received (http.c keeps them in "cookies").
    if (uint8_t* c = nullptr; av_opt_get(ctx, "cookies", AV_OPT_SEARCH_CHILDREN, &c) >= 0 && c) {
        cookies = *c != '\0';
        av_free(c);
    }

    NetworkLogger::track_close(ctx);
    avio_close(ctx);
    if (aborted || read_err < 0) return false;
    out = std::move(result);
    return true;
}

bool ManifestParser::fetch(const std::string& url, std::string& out, const std::atomic<bool>* abort)
{
    std::string final_url;
    bool        cookies = false;
    return fetch_text(url, out, final_url, cookies, abort);
}

// ── URL resolution ────────────────────────────────────────────────────────────

std::string ManifestParser::resolve_url(const std::string& base_url,
                                        const std::string& variant_url)
{
    // HTTP(S): RFC 3986 forms FFmpeg resolves too ("/root/relative" included).
    if (std::string abs = hls::absolute_uri(base_url, variant_url); !abs.empty())
        return abs;

    // Already absolute
    if (variant_url.starts_with("http://") || variant_url.starts_with("https://") ||
        variant_url.starts_with("file://")  || variant_url.starts_with('/'))
        return variant_url;

    // Relative: take everything up to and including the last '/' in base_url
    auto pos = base_url.rfind('/');
    if (pos == std::string::npos) return variant_url;
    return base_url.substr(0, pos + 1) + variant_url;
}

// ── HLS fetch wrapper ─────────────────────────────────────────────────────────

bool ManifestParser::parse_hls(const std::string& url, const std::atomic<bool>* abort)
{
    if (!fetch_text(url, text_, final_url_, set_cookies_, abort)) return false;
    bool ok = parse_hls_text(text_, final_url_);
    if (ok)
        logger::info("ManifestParser: parsed HLS — {} tags, {} variants from {}",
                     tags_.size(), variants_.size(), url);
    return ok;
}

// ── HLS line-by-line parser ───────────────────────────────────────────────────

// Parse a name=value attribute from an HLS attribute list string.
// e.g. given "BANDWIDTH=2000000,RESOLUTION=1280x720" and name "BANDWIDTH", returns "2000000".
static std::string hls_attr(const std::string& attrs, const char* name)
{
    // The key must start an attribute: "BANDWIDTH=" must not match inside
    // "AVERAGE-BANDWIDTH=".
    std::string key = std::string(name) + "=";
    std::size_t pos = 0;
    for (;; ++pos) {
        pos = attrs.find(key, pos);
        if (pos == std::string::npos) return {};
        if (pos == 0 || attrs[pos - 1] == ',') break;
    }
    pos += key.size();
    if (pos >= attrs.size()) return {};
    // Quoted value
    if (attrs[pos] == '"') {
        auto end = attrs.find('"', pos + 1);
        return (end != std::string::npos) ? attrs.substr(pos + 1, end - pos - 1) : "";
    }
    // Unquoted: up to next comma or end
    auto end = attrs.find(',', pos);
    return attrs.substr(pos, (end != std::string::npos) ? end - pos : std::string::npos);
}

bool ManifestParser::parse_hls_text(const std::string& text, const std::string& base_url)
{
    tags_.clear();
    variants_.clear();
    renditions_.clear();

    double running_pts = 0.0;
    int    disc_seq    = 0;

    bool        in_stream_inf = false;  // previous line was #EXT-X-STREAM-INF
    std::string stream_inf_attrs;       // attribute string from that line

    std::istringstream ss(text);
    std::string line;
    while (std::getline(ss, line)) {
        // Strip Windows-style CR
        if (!line.empty() && line.back() == '\r') line.pop_back();

        // ── Variant stream (master playlist) ─────────────────────────────────
        if (line.starts_with("#EXT-X-STREAM-INF:")) {
            in_stream_inf   = true;
            stream_inf_attrs = line.substr(18);  // after "#EXT-X-STREAM-INF:"
            continue;
        }
        if (in_stream_inf && !line.empty() && line[0] != '#') {
            in_stream_inf = false;
            VariantStream vs;
            vs.url = base_url.empty() ? line : resolve_url(base_url, line);

            std::string bw_s = hls_attr(stream_inf_attrs, "BANDWIDTH");
            if (!bw_s.empty()) vs.bandwidth = std::stoll(bw_s);

            std::string res_s = hls_attr(stream_inf_attrs, "RESOLUTION");
            if (!res_s.empty()) {
                auto x = res_s.find('x');
                if (x != std::string::npos) {
                    vs.width  = std::stoi(res_s.substr(0, x));
                    vs.height = std::stoi(res_s.substr(x + 1));
                }
            }

            vs.codecs = hls_attr(stream_inf_attrs, "CODECS");
            if (auto avg = hls_attr(stream_inf_attrs, "AVERAGE-BANDWIDTH"); !avg.empty())
                vs.avg_bandwidth = std::strtoll(avg.c_str(), nullptr, 10);
            if (auto fr = hls_attr(stream_inf_attrs, "FRAME-RATE"); !fr.empty())
                vs.frame_rate = std::strtod(fr.c_str(), nullptr);
            vs.audio_group    = hls_attr(stream_inf_attrs, "AUDIO");
            vs.subtitle_group = hls_attr(stream_inf_attrs, "SUBTITLES");
            variants_.push_back(std::move(vs));
            continue;
        }
        in_stream_inf = false;

        // ── Alternative renditions / master-only metadata (not timeline events) ─
        if (line.starts_with("#EXT-X-MEDIA:")) {
            const std::string a = line.substr(13);
            MediaRendition mr;
            mr.type       = hls_attr(a, "TYPE");
            mr.group_id   = hls_attr(a, "GROUP-ID");
            mr.name       = hls_attr(a, "NAME");
            mr.language   = hls_attr(a, "LANGUAGE");
            mr.channels   = hls_attr(a, "CHANNELS");
            mr.is_default = hls_attr(a, "DEFAULT") == "YES";
            if (auto uri = hls_attr(a, "URI"); !uri.empty())
                mr.url = base_url.empty() ? uri : resolve_url(base_url, uri);
            renditions_.push_back(std::move(mr));
            continue;
        }
        if (line.starts_with("#EXT-X-I-FRAME-STREAM-INF:") ||
            line == "#EXT-X-INDEPENDENT-SEGMENTS")
            continue;

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

bool ManifestParser::parse_dash(const std::string& url, const std::atomic<bool>* abort)
{
    if (!fetch_text(url, text_, final_url_, set_cookies_, abort)) return false;
    bool ok = parse_dash_text(text_, final_url_);
    if (ok)
        logger::info("ManifestParser: parsed DASH MPD — {} tags, {} variants from {}",
                     tags_.size(), variants_.size(), url);
    return ok;
}

// ── DASH MPD XML parser (pugixml) ─────────────────────────────────────────────

// Read first <BaseURL> child element's text, if any. DASH spec allows multiple
// (alternative locations); we take the first.
static std::string dash_base_url(pugi::xml_node parent)
{
    auto bu = parent.child("BaseURL");
    return bu ? std::string(bu.text().as_string()) : std::string{};
}

// Stack base URLs hierarchically: each level may be absolute (replaces) or
// relative (resolves against the parent). Per ISO/IEC 23009-1.
static std::string stack_base(const std::string& parent_base, const std::string& child)
{
    if (child.empty())                      return parent_base;
    if (child.starts_with("http://")  ||
        child.starts_with("https://") ||
        child.starts_with("file://"))       return child;
    if (parent_base.empty())                return child;
    auto pos = parent_base.rfind('/');
    if (pos == std::string::npos)           return child;
    return parent_base.substr(0, pos + 1) + child;
}

bool ManifestParser::parse_dash_text(const std::string& text, const std::string& base_url)
{
    tags_.clear();
    variants_.clear();

    pugi::xml_document doc;
    pugi::xml_parse_result result = doc.load_buffer(text.data(), text.size());
    if (!result) {
        logger::warn("ManifestParser: MPD XML parse error: {}", result.description());
        return false;
    }

    // Walk <MPD> → <Period> elements
    pugi::xml_node mpd = doc.child("MPD");
    if (!mpd) mpd = doc.first_child();

    // Hierarchical BaseURL: MPD → Period → AdaptationSet → Representation.
    // Manifest URL forms the implicit root for relative resolution.
    const std::string mpd_base = stack_base(base_url, dash_base_url(mpd));

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

        // Walk AdaptationSet → Representation, collect video variants only.
        // Direct-URL Representations (those carrying a <BaseURL> resolvable to a
        // single playable file) get their URL filled in. SegmentTemplate-based
        // ones have metadata but empty url — VMAF mode skips empty urls upstream.
        const std::string period_base = stack_base(mpd_base, dash_base_url(period));

        for (pugi::xml_node aset : period.children("AdaptationSet")) {
            // Filter to video adaptation sets.
            std::string mime    = aset.attribute("mimeType").as_string();
            std::string content = aset.attribute("contentType").as_string();
            const bool is_video =
                mime.starts_with("video/") || content == "video" ||
                (mime.empty() && content.empty());   // some MPDs put mimeType on Representation

            if (!is_video) continue;

            const std::string aset_base = stack_base(period_base, dash_base_url(aset));

            // Inherit attributes from AdaptationSet when Representation omits them.
            const std::string aset_codecs = aset.attribute("codecs").as_string();
            const int         aset_w      = aset.attribute("width").as_int();
            const int         aset_h      = aset.attribute("height").as_int();
            const std::string aset_mime   = mime;

            for (pugi::xml_node rep : aset.children("Representation")) {
                std::string rep_mime = rep.attribute("mimeType").as_string();
                if (rep_mime.empty()) rep_mime = aset_mime;
                // Skip non-video reps slipped under a generic AdaptationSet.
                if (!rep_mime.empty() && !rep_mime.starts_with("video/"))
                    continue;

                VariantStream vs;
                vs.bandwidth = rep.attribute("bandwidth").as_llong(0);
                vs.width     = rep.attribute("width").as_int(aset_w);
                vs.height    = rep.attribute("height").as_int(aset_h);

                std::string codecs = rep.attribute("codecs").as_string();
                if (codecs.empty()) codecs = aset_codecs;
                vs.codecs = std::move(codecs);

                std::string fr = rep.attribute("frameRate").as_string();
                if (fr.empty()) fr = aset.attribute("frameRate").as_string();
                if (!fr.empty()) {
                    const double num = std::strtod(fr.c_str(), nullptr);
                    const auto slash = fr.find('/');
                    const double den = slash == std::string::npos ? 1.0 : std::strtod(fr.c_str() + slash + 1, nullptr);
                    if (den > 0.0) vs.frame_rate = num / den;
                }

                // URL: direct BaseURL inside Representation makes it playable.
                // Otherwise leave empty (SegmentTemplate / SegmentList not supported).
                const std::string rep_base = dash_base_url(rep);
                if (!rep_base.empty())
                    vs.url = stack_base(aset_base, rep_base);

                variants_.push_back(std::move(vs));
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
