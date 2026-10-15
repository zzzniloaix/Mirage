#include "TrackGroups.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <format>
#include <tuple>

std::string language_name(const std::string& code)
{
    struct L { const char* a; const char* b; const char* name; };
    static constexpr L kLangs[] = {
        { "en", "eng", "English" },   { "ja", "jpn", "Japanese" }, { "zh", "zho", "Chinese" },
        { "zh", "chi", "Chinese" },   { "ko", "kor", "Korean" },   { "es", "spa", "Spanish" },
        { "fr", "fra", "French" },    { "fr", "fre", "French" },   { "de", "deu", "German" },
        { "de", "ger", "German" },    { "it", "ita", "Italian" },  { "pt", "por", "Portuguese" },
        { "ru", "rus", "Russian" },   { "ar", "ara", "Arabic" },   { "hi", "hin", "Hindi" },
        { "nl", "nld", "Dutch" },     { "nl", "dut", "Dutch" },    { "sv", "swe", "Swedish" },
        { "pl", "pol", "Polish" },    { "tr", "tur", "Turkish" },  { "th", "tha", "Thai" },
    };
    std::string lc = code;
    for (auto& c : lc) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& l : kLangs)
        if (lc == l.a || lc == l.b) return l.name;
    return code;
}

std::string channel_label(int channels)
{
    switch (channels) {
        case 1:  return "Mono";
        case 2:  return "Stereo";
        case 6:  return "5.1";
        case 8:  return "7.1";
        default: return channels > 0 ? std::format("{} ch", channels) : std::string();
    }
}

std::string bitrate_label(int64_t bps)
{
    if (bps <= 0) return {};
    if (bps >= 1'000'000) return std::format("{:.1f} Mb/s", static_cast<double>(bps) / 1e6);
    return std::format("{} kb/s", (bps + 500) / 1000);
}

namespace {

// Join the non-empty parts with " · ".
std::string dot_join(std::initializer_list<std::string> parts)
{
    std::string out;
    for (const auto& p : parts) {
        if (p.empty()) continue;
        if (!out.empty()) out += " · ";
        out += p;
    }
    return out;
}

} // namespace

std::vector<VideoTrackGroup> group_video_tracks(const std::vector<VideoTrackInfo>& tracks,
                                                int cur_stream)
{
    // fps compared at 0.01 resolution: 59.94 and 60 are different renditions.
    auto key = [](const VideoTrackInfo& t) {
        return std::make_tuple(t.codec_name, t.width, t.height,
                               std::lround(t.fps * 100.0), t.bit_rate);
    };

    std::vector<VideoTrackGroup> groups;
    std::vector<const VideoTrackInfo*> reps;   // parallel to groups
    for (const auto& t : tracks) {
        auto it = std::find_if(reps.begin(), reps.end(),
                               [&](const VideoTrackInfo* r) { return key(*r) == key(t); });
        if (it != reps.end()) {
            auto& g = groups[static_cast<std::size_t>(it - reps.begin())];
            g.members.push_back(t.stream_idx);
            if (t.stream_idx == cur_stream) g.stream_idx = cur_stream;
            continue;
        }
        VideoTrackGroup g;
        g.stream_idx = t.stream_idx;
        g.members    = { t.stream_idx };
        const long fps = std::lround(t.fps);
        g.title  = t.height > 0
            ? (fps > 0 ? std::format("{}p · {} fps", t.height, fps) : std::format("{}p", t.height))
            : std::format("Track {}", t.stream_idx);
        g.detail = dot_join({ t.codec_name,
                              t.width > 0 ? std::format("{}×{}", t.width, t.height) : std::string(),
                              bitrate_label(t.bit_rate) });
        groups.push_back(std::move(g));
        reps.push_back(&t);
    }

    // Highest resolution first, then highest bitrate (stable for ties).
    std::vector<std::size_t> order(groups.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const auto& ra = *reps[a];
        const auto& rb = *reps[b];
        if (ra.height != rb.height) return ra.height > rb.height;
        return ra.bit_rate > rb.bit_rate;
    });
    std::vector<VideoTrackGroup> sorted;
    sorted.reserve(groups.size());
    for (std::size_t i : order) sorted.push_back(std::move(groups[i]));
    return sorted;
}

std::string audio_track_title(const AudioTrackInfo& t)
{
    return t.language.empty() ? std::format("Track {}", t.stream_idx) : language_name(t.language);
}

std::string audio_track_detail(const AudioTrackInfo& t)
{
    return dot_join({ t.codec_name, channel_label(t.channels),
                      t.sample_rate > 0 ? std::format("{:g} kHz", t.sample_rate / 1000.0) : std::string(),
                      bitrate_label(t.bit_rate) });
}

std::string subtitle_track_title(const SubtitleTrackInfo& t)
{
    return t.language.empty() ? std::format("Track {}", t.stream_idx) : language_name(t.language);
}

std::string subtitle_track_detail(const SubtitleTrackInfo& t)
{
    return dot_join({ t.codec_name, t.is_text ? "text" : "bitmap",
                      t.is_forced ? "forced" : "", t.is_hi ? "SDH" : "" });
}
