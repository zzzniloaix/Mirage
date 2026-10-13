#pragma once

// Subtitle choices shown in the CC popover / settings, independent of where
// the cue text comes from (embedded stream or sidecar file), plus the
// "show when available" selection policy and sidecar discovery. Pure — no
// FFmpeg/ImGui — so the policy can be unit-tested.

#include <filesystem>
#include <string>
#include <vector>

struct SubtitleTrackInfo;   // Demuxer.h

struct SubChoice {
    enum Kind { Embedded, Sidecar } kind = Embedded;
    int         stream_idx = -1;     // Embedded
    std::string path;                // Sidecar
    std::string lang, codec, title;  // codec: "SRT","ASS","WebVTT","mov_text","PGS","DVD","DVB"
    bool        is_text    = true;
    bool        is_default = false;
    bool        is_forced  = false;
    bool        is_hi      = false;  // hearing impaired (SDH)

    // "2 · eng · SRT", "movie.en.srt · en · SRT", "3 · jpn · PGS (forced)"
    std::string label() const;
};

// Display name for an FFmpeg subtitle codec name ("subrip" → "SRT", ...).
std::string sub_codec_display(const std::string& ffmpeg_codec_name);

// Sidecar subtitle files next to a local media file: same stem
// ("movie.srt") or stem + "." + language tag ("movie.en.srt",
// "movie.pt-BR.vtt", "movie.en.forced.srt"). Extensions .srt/.vtt/.ass/.ssa
// (case-insensitive). Sorted: exact stem, then tags starting "en", then the
// rest alphabetically. At most 16. Returns full paths.
std::vector<std::string> find_sidecars(const std::filesystem::path& media);

// Choice for a sidecar file. media_stem (may be empty) tells which part of
// the file name is the language tag; without it the tag is guessed from the
// trailing dot-separated parts.
SubChoice sidecar_choice(const std::string& path, const std::string& media_stem = {});

// Embedded tracks (stream order) followed by sidecars (given order).
std::vector<SubChoice> build_choices(const std::vector<SubtitleTrackInfo>& tracks,
                                     const std::vector<std::string>& sidecars,
                                     const std::string& media_stem = {});

// True when two language tags name the same language ("en" == "eng",
// "pt-BR" ~ "por", case-insensitive). Empty never matches.
bool sub_lang_matches(const std::string& a, const std::string& b);

// Remembered user preference (process-wide; persisted to prefs.txt).
struct SubPref {
    enum Mode { Auto, Off, Lang } mode = Auto;
    std::string lang;
};

// Command-line overrides.
struct SubCli {
    int         stream_idx = -1;     // --sub N
    bool        off        = false;  // --sub off / --sub -1
    std::string file;                // --sub-file <path> (must also be in choices)
};

// Which policy rule picked the result (for logging / the auto-enable toast).
enum class SubRule {
    None, CliFile, CliStream, CliOff, PrefOff, PrefLang,
    Forced, Default, Sidecar, AnyText, AnyBitmap, Off
};

// Initial subtitle choice: index into `choices`, or -1 for Off. First match:
//  1 --sub-file  2 --sub N / off  3 pref Off  4 pref Lang  5 forced
//  6 embedded default  7 first sidecar  8 embedded text  9 embedded bitmap
//  10 off.
int choose_initial(const std::vector<SubChoice>& choices, const SubCli& cli,
                   const SubPref& pref, SubRule* rule = nullptr);

// Rules 5–9 enable a track the user never asked for — tell them how to undo it.
inline bool sub_rule_is_auto(SubRule r)
{
    return r == SubRule::Forced || r == SubRule::Default || r == SubRule::Sidecar
        || r == SubRule::AnyText || r == SubRule::AnyBitmap;
}
