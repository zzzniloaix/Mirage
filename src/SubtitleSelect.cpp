#include "SubtitleSelect.h"
#include "Demuxer.h"   // SubtitleTrackInfo (plain struct)

#include <algorithm>
#include <array>
#include <cctype>
#include <system_error>

namespace {

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool is_sub_ext(const std::string& ext_lower)
{
    return ext_lower == ".srt" || ext_lower == ".vtt" || ext_lower == ".ass" || ext_lower == ".ssa";
}

std::string codec_for_ext(const std::string& ext_lower)
{
    if (ext_lower == ".srt") return "SRT";
    if (ext_lower == ".vtt") return "WebVTT";
    if (ext_lower == ".ass" || ext_lower == ".ssa") return "ASS";
    return {};
}

// [A-Za-z]{2,3}(-[A-Za-z0-9]+)?
bool is_lang_tag(const std::string& t)
{
    const std::size_t dash = t.find('-');
    const std::string primary = t.substr(0, dash);
    if (primary.size() < 2 || primary.size() > 3) return false;
    for (unsigned char c : primary) if (!std::isalpha(c)) return false;
    if (dash == std::string::npos) return true;
    const std::string rest = t.substr(dash + 1);
    if (rest.empty()) return false;
    for (unsigned char c : rest) if (!std::isalnum(c)) return false;
    return true;
}

struct TagInfo { bool ok = false; std::string lang; bool forced = false, sdh = false; };

// Parse "<lang>[.forced|.sdh]".
TagInfo parse_tag(const std::string& tag)
{
    TagInfo ti;
    std::string t = tag;
    const std::size_t dot = t.rfind('.');
    if (dot != std::string::npos) {
        const std::string flag = lower(t.substr(dot + 1));
        if (flag == "forced")   ti.forced = true;
        else if (flag == "sdh") ti.sdh = true;
        else return ti;
        t = t.substr(0, dot);
    }
    if (!is_lang_tag(t)) return ti;
    ti.ok   = true;
    ti.lang = t;
    return ti;
}

// ISO 639-1 → 639-2 (T and B forms) for the languages most often seen in
// subtitle tags. Anything else matches only by exact primary subtag.
constexpr std::array<std::array<const char*, 3>, 24> kLangs = {{
    {"en","eng","eng"}, {"ja","jpn","jpn"}, {"fr","fra","fre"}, {"de","deu","ger"},
    {"es","spa","spa"}, {"it","ita","ita"}, {"pt","por","por"}, {"zh","zho","chi"},
    {"ko","kor","kor"}, {"ru","rus","rus"}, {"nl","nld","dut"}, {"sv","swe","swe"},
    {"ar","ara","ara"}, {"hi","hin","hin"}, {"pl","pol","pol"}, {"tr","tur","tur"},
    {"da","dan","dan"}, {"no","nor","nor"}, {"fi","fin","fin"}, {"el","ell","gre"},
    {"he","heb","heb"}, {"th","tha","tha"}, {"cs","ces","cze"}, {"hu","hun","hun"},
}};

std::string canon_lang(const std::string& tag)
{
    std::string p = lower(tag.substr(0, tag.find('-')));
    for (const auto& row : kLangs)
        if (p == row[0] || p == row[1] || p == row[2]) return row[0];
    return p;
}

} // namespace

std::string SubChoice::label() const
{
    std::string s = (kind == Sidecar)
        ? std::filesystem::path(path).filename().string()
        : std::to_string(stream_idx);
    if (!lang.empty())  s += " · " + lang;
    if (!codec.empty()) s += " · " + codec;
    if (is_forced)      s += " (forced)";
    return s;
}

std::string sub_codec_display(const std::string& n)
{
    if (n == "subrip" || n == "srt")          return "SRT";
    if (n == "ass" || n == "ssa")             return "ASS";
    if (n == "webvtt")                        return "WebVTT";
    if (n == "mov_text")                      return "mov_text";
    if (n == "hdmv_pgs_subtitle")             return "PGS";
    if (n == "dvd_subtitle")                  return "DVD";
    if (n == "dvb_subtitle")                  return "DVB";
    return n;
}

bool sub_lang_matches(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty()) return false;
    return canon_lang(a) == canon_lang(b);
}

std::vector<std::string> find_sidecars(const std::filesystem::path& media)
{
    namespace fs = std::filesystem;
    struct Hit { int rank; std::string name; std::string path; };
    std::vector<Hit> hits;

    const std::string stem = media.stem().string();
    fs::path dir = media.parent_path();
    if (dir.empty()) dir = ".";
    if (stem.empty()) return {};

    std::error_code ec;
    fs::directory_iterator it(dir, ec), end;
    if (ec) return {};
    for (; it != end; it.increment(ec)) {
        if (ec) break;
        const fs::path& p = it->path();
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        if (!is_sub_ext(lower(p.extension().string()))) continue;

        const std::string cstem = p.stem().string();
        int rank;
        if (cstem == stem) {
            rank = 0;
        } else if (cstem.size() > stem.size() + 1 && cstem.starts_with(stem)
                   && cstem[stem.size()] == '.') {
            const TagInfo ti = parse_tag(cstem.substr(stem.size() + 1));
            if (!ti.ok) continue;
            rank = lower(ti.lang).starts_with("en") ? 1 : 2;
        } else {
            continue;
        }
        hits.push_back({ rank, p.filename().string(), p.string() });
    }

    std::sort(hits.begin(), hits.end(), [](const Hit& a, const Hit& b) {
        return a.rank != b.rank ? a.rank < b.rank : a.name < b.name;
    });
    std::vector<std::string> out;
    for (const auto& h : hits) {
        if (out.size() >= 16) break;
        out.push_back(h.path);
    }
    return out;
}

SubChoice sidecar_choice(const std::string& path, const std::string& media_stem)
{
    namespace fs = std::filesystem;
    SubChoice c;
    c.kind    = SubChoice::Sidecar;
    c.path    = path;
    c.is_text = true;
    const fs::path p(path);
    c.codec = codec_for_ext(lower(p.extension().string()));

    const std::string cstem = p.stem().string();
    TagInfo ti;
    if (!media_stem.empty() && cstem.size() > media_stem.size() + 1
        && cstem.starts_with(media_stem) && cstem[media_stem.size()] == '.') {
        ti = parse_tag(cstem.substr(media_stem.size() + 1));
    } else if (media_stem.empty() || cstem != media_stem) {
        // Unknown media: try "<lang>.<flag>" then "<lang>" at the end.
        const std::size_t d1 = cstem.rfind('.');
        if (d1 != std::string::npos && d1 > 0) {
            const std::size_t d2 = cstem.rfind('.', d1 - 1);
            if (d2 != std::string::npos) ti = parse_tag(cstem.substr(d2 + 1));
            if (!ti.ok) ti = parse_tag(cstem.substr(d1 + 1));
        }
    }
    if (ti.ok) {
        c.lang      = ti.lang;
        c.is_forced = ti.forced;
        c.is_hi     = ti.sdh;
    }
    return c;
}

std::vector<SubChoice> build_choices(const std::vector<SubtitleTrackInfo>& tracks,
                                     const std::vector<std::string>& sidecars,
                                     const std::string& media_stem)
{
    std::vector<SubChoice> out;
    for (const auto& t : tracks) {
        SubChoice c;
        c.kind       = SubChoice::Embedded;
        c.stream_idx = t.stream_idx;
        c.lang       = t.lang;
        c.title      = t.title;
        c.codec      = sub_codec_display(t.codec_name);
        c.is_text    = t.is_text;
        c.is_default = t.is_default;
        c.is_forced  = t.is_forced;
        c.is_hi      = t.is_hi;
        out.push_back(std::move(c));
    }
    for (const auto& s : sidecars)
        out.push_back(sidecar_choice(s, media_stem));
    return out;
}

int choose_initial(const std::vector<SubChoice>& choices, const SubCli& cli,
                   const SubPref& pref, SubRule* rule)
{
    auto ret = [&](int idx, SubRule r) { if (rule) *rule = r; return idx; };
    const int n = static_cast<int>(choices.size());
    auto first = [&](auto&& pred) -> int {
        for (int i = 0; i < n; ++i)
            if (pred(choices[static_cast<std::size_t>(i)])) return i;
        return -1;
    };
    using K = SubChoice::Kind;
    int i;

    // 1. --sub-file
    if (!cli.file.empty()
        && (i = first([&](const SubChoice& c) { return c.kind == K::Sidecar && c.path == cli.file; })) >= 0)
        return ret(i, SubRule::CliFile);
    // 2. --sub N / --sub off
    if (cli.off) return ret(-1, SubRule::CliOff);
    if (cli.stream_idx >= 0
        && (i = first([&](const SubChoice& c) { return c.kind == K::Embedded && c.stream_idx == cli.stream_idx; })) >= 0)
        return ret(i, SubRule::CliStream);
    // 3. user said Off
    if (pref.mode == SubPref::Off) return ret(-1, SubRule::PrefOff);
    // 4. preferred language: embedded text, sidecar, embedded bitmap
    if (pref.mode == SubPref::Lang && !pref.lang.empty()) {
        auto lang_ok = [&](const SubChoice& c) { return sub_lang_matches(c.lang, pref.lang); };
        if ((i = first([&](const SubChoice& c) { return lang_ok(c) && c.kind == K::Embedded && c.is_text; })) >= 0
         || (i = first([&](const SubChoice& c) { return lang_ok(c) && c.kind == K::Sidecar; })) >= 0
         || (i = first([&](const SubChoice& c) { return lang_ok(c); })) >= 0)
            return ret(i, SubRule::PrefLang);
    }
    // 5. forced (embedded first, then sidecar)
    if ((i = first([](const SubChoice& c) { return c.is_forced && c.kind == K::Embedded; })) >= 0
     || (i = first([](const SubChoice& c) { return c.is_forced; })) >= 0)
        return ret(i, SubRule::Forced);
    // 6. embedded default
    if ((i = first([](const SubChoice& c) { return c.is_default && c.kind == K::Embedded; })) >= 0)
        return ret(i, SubRule::Default);
    // 7. sidecar
    if ((i = first([](const SubChoice& c) { return c.kind == K::Sidecar; })) >= 0)
        return ret(i, SubRule::Sidecar);
    // 8. / 9. any embedded text, then bitmap
    if ((i = first([](const SubChoice& c) { return c.kind == K::Embedded && c.is_text; })) >= 0)
        return ret(i, SubRule::AnyText);
    if ((i = first([](const SubChoice& c) { return c.kind == K::Embedded; })) >= 0)
        return ret(i, SubRule::AnyBitmap);
    // 10.
    return ret(-1, SubRule::Off);
}
