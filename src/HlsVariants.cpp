#include "HlsVariants.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/dict.h>
}

namespace hls {

int variant_of_stream(const std::vector<ProgramInfo>& programs, int stream,
                      std::int64_t stream_bitrate,
                      const std::vector<VariantStream>& variants,
                      const std::vector<MediaRendition>& renditions)
{
    if (stream < 0 || variants.size() < 2 || programs.size() != variants.size()) return -1;
    if (std::any_of(renditions.begin(), renditions.end(),
                    [](const MediaRendition& r) { return r.type == "VIDEO" && !r.url.empty(); }))
        return -1;
    for (std::size_t p = 0; p < programs.size(); ++p)
        if (programs[p].variant_bitrate != variants[p].bandwidth) return -1;

    int found = -1;
    for (std::size_t p = 0; p < programs.size(); ++p) {
        const auto& s = programs[p].streams;
        if (std::find(s.begin(), s.end(), stream) == s.end()) continue;
        if (found >= 0) return -1;   // in several programs: a shared rendition
        found = static_cast<int>(p);
    }
    if (found < 0 || stream_bitrate != variants[static_cast<std::size_t>(found)].bandwidth
        || variants[static_cast<std::size_t>(found)].url.empty())
        return -1;
    return found;
}

std::vector<VariantStream> opened_variants(const std::vector<VariantStream>& variants,
                                           const std::vector<int>& kept)
{
    if (kept.empty()) return variants;
    std::vector<VariantStream> out;
    out.reserve(kept.size());
    for (const int k : kept) {
        if (k < 0 || k >= static_cast<int>(variants.size())) return {};
        out.push_back(variants[static_cast<std::size_t>(k)]);
    }
    return out;
}

int playing_variant(int video_variant, int audio, const std::vector<ProgramInfo>& programs,
                    const std::vector<VariantStream>& variants, const std::vector<int>& kept)
{
    if (video_variant < 0 || video_variant >= static_cast<int>(variants.size())) return -1;
    if (audio < 0) return video_variant;
    if (!kept.empty() && kept.size() != programs.size()) return video_variant;
    if (kept.empty() && variants.size() != programs.size()) return video_variant;

    // The audio's group: that of every variant whose program holds the stream.
    const std::string* group = nullptr;
    for (std::size_t p = 0; p < programs.size(); ++p) {
        const auto& s = programs[p].streams;
        if (std::find(s.begin(), s.end(), audio) == s.end()) continue;
        const int k = kept.empty() ? static_cast<int>(p) : kept[p];
        if (k < 0 || k >= static_cast<int>(variants.size())) return video_variant;
        const std::string& g = variants[static_cast<std::size_t>(k)].audio_group;
        if (group && *group != g) return video_variant;   // not one group: can't tell
        group = &g;
    }
    if (!group) return video_variant;
    const std::string& url = variants[static_cast<std::size_t>(video_variant)].url;
    if (variants[static_cast<std::size_t>(video_variant)].audio_group == *group) return video_variant;
    for (std::size_t i = 0; i < variants.size(); ++i)
        if (variants[i].url == url && variants[i].audio_group == *group) return static_cast<int>(i);
    return video_variant;
}

int current_variant(const std::vector<ProgramInfo>& programs, const std::vector<int>& video_variant,
                    int video, int audio, const std::vector<VariantStream>& variants,
                    const std::vector<int>& kept)
{
    if (variants.empty()) return -1;
    if (kept.empty() && programs.size() == variants.size()) {
        auto has = [](const std::vector<int>& s, int idx) {
            return idx < 0 || std::find(s.begin(), s.end(), idx) != s.end();
        };
        for (std::size_t p = 0; p < programs.size(); ++p)
            if (has(programs[p].streams, video) && has(programs[p].streams, audio))
                return static_cast<int>(p);
    }
    const int vv = (video >= 0 && video < static_cast<int>(video_variant.size()))
                 ? video_variant[static_cast<std::size_t>(video)] : -1;
    return playing_variant(vv, audio, programs, variants, kept);
}

MediaPlaylist parse_media_playlist(const std::string& text)
{
    MediaPlaylist out;
    bool header = false, bad = false;
    std::size_t pos = 0;
    while (pos < text.size() && !bad) {
        std::size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string_view line(text.data() + pos, eol - pos);
        pos = eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
            line.remove_suffix(1);
        if (line.empty()) continue;
        if (!header) {
            // The first line of a playlist is its header (a UTF-8 BOM aside).
            if (line.starts_with("\xEF\xBB\xBF")) line.remove_prefix(3);
            header = line == "#EXTM3U";
            if (!header) bad = true;
            continue;
        }
        if (line.starts_with("#EXTINF:")) {
            // "#EXTINF:<duration>,[<title>]"
            std::string_view dur = line.substr(8);
            dur = dur.substr(0, dur.find(','));
            const std::string num(dur);
            char* end = nullptr;
            const double d = std::strtod(num.c_str(), &end);
            if (num.empty() || end != num.c_str() + num.size() || !(d >= 0.0)) bad = true;
            else out.durations.push_back(d);
        } else if (line.starts_with("#EXT-X-STREAM-INF:")) {
            bad = true;   // a master playlist
        } else if (line == "#EXT-X-ENDLIST" || line == "#EXT-X-PLAYLIST-TYPE:VOD") {
            out.vod = true;
        } else if (line == "#EXT-X-DISCONTINUITY") {
            out.discontinuity = true;
        } else if (line == "#EXT-X-GAP") {
            out.gap = true;
        } else if (line == "#EXT-X-INDEPENDENT-SEGMENTS") {
            out.independent = true;
        }
    }
    if (bad || !header) out.durations.clear();
    return out;
}

static std::int64_t dict_int(const AVDictionary* d, const char* key)
{
    const AVDictionaryEntry* e = av_dict_get(d, key, nullptr, 0);
    if (!e || !e->value || !*e->value) return -1;
    char* end = nullptr;
    const long long v = std::strtoll(e->value, &end, 10);
    return (end && *end == '\0') ? static_cast<std::int64_t>(v) : -1;
}

std::vector<ProgramInfo> programs_of(const AVFormatContext* fc)
{
    std::vector<ProgramInfo> out;
    if (!fc || !fc->iformat || !fc->iformat->name || std::string_view(fc->iformat->name) != "hls")
        return out;
    out.reserve(fc->nb_programs);
    for (unsigned p = 0; p < fc->nb_programs; ++p) {
        const AVProgram* prog = fc->programs[p];
        ProgramInfo pi;
        pi.variant_bitrate = dict_int(prog->metadata, "variant_bitrate");
        pi.streams.assign(prog->stream_index, prog->stream_index + prog->nb_stream_indexes);
        out.push_back(std::move(pi));
    }
    return out;
}

std::int64_t stream_variant_bitrate(const AVFormatContext* fc, int idx)
{
    if (!fc || idx < 0 || idx >= static_cast<int>(fc->nb_streams)) return -1;
    return dict_int(fc->streams[idx]->metadata, "variant_bitrate");
}

int variant_index_for(const AVFormatContext* fc, int idx, const ManifestParser& m,
                      const std::vector<int>& kept)
{
    // Video only: an audio / subtitle rendition that a single variant uses
    // (common in a deduplicated copy) is in one program too.
    if (!fc || idx < 0 || idx >= static_cast<int>(fc->nb_streams)
        || fc->streams[idx]->codecpar->codec_type != AVMEDIA_TYPE_VIDEO)
        return -1;
    const std::vector<VariantStream> opened = opened_variants(m.variants(), kept);
    const int v = variant_of_stream(programs_of(fc), idx, stream_variant_bitrate(fc, idx),
                                    opened, m.renditions());
    if (v < 0) return -1;
    return kept.empty() ? v : kept[static_cast<std::size_t>(v)];
}

} // namespace hls
