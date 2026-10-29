#include "VMAFCli.h"

#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

namespace vmafcli {

namespace {

// Which form of the inputs an option belongs to: a pair or a ladder
// (D-029); Any for the options of both.
enum class Form { Any, Pair, Ladder };

// One option that takes a value. A new option is one more row here: parse(),
// the config file and usage() all work from this table.
struct Option {
    std::string_view name;      // "--ref"; "ref" in a config file
    std::string_view value;     // placeholder shown in the help, "<url|path>"
    std::string_view help;
    Form             form;
    bool             required;  // in its form
    // Stores `v` in `a`; returns what is wrong with it, or "", in words that
    // follow the option's name ("wants a whole number ...").
    std::string (*set)(Args& a, std::string_view v);
};

std::string set_frames(Args& a, std::string_view v)
{
    unsigned n = 0;
    const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
    if (ec != std::errc() || end != v.data() + v.size() || n == 0)
        return std::format("wants a whole number from 1 to {}, not '{}'",
                           std::numeric_limits<unsigned>::max(), v);
    a.frames = n;
    return {};
}

std::string set_scaler(Args& a, std::string_view v)
{
    const std::optional<VMAFScaler> s = vmaf_scaler_named(v);
    if (!s) return std::format("wants bilinear, bicubic or lanczos, not '{}'", v);
    a.scaler = *s;
    return {};
}

// The most pairs --jobs lets a ladder run score at a time.
constexpr unsigned kMaxJobs = 256;

std::string set_jobs(Args& a, std::string_view v)
{
    unsigned n = 0;
    const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
    if (ec != std::errc() || end != v.data() + v.size() || n == 0 || n > kMaxJobs)
        return std::format("wants a whole number from 1 to {}, not '{}'", kMaxJobs, v);
    a.jobs = n;
    return {};
}

// SIZE: a whole number of bytes, or of KiB, MiB, GiB or TiB with the suffix
// K, M, G or T.
std::string set_memory_limit(Args& a, std::string_view v)
{
    std::uint64_t n = 0;
    const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
    const std::string_view suffix = v.substr(static_cast<std::size_t>(end - v.data()));
    unsigned shift = 0;
    if (suffix == "K") shift = 10;
    else if (suffix == "M") shift = 20;
    else if (suffix == "G") shift = 30;
    else if (suffix == "T") shift = 40;
    else if (!suffix.empty()) n = 0;
    if (ec != std::errc() || end == v.data() || n == 0 ||
        n > (std::numeric_limits<std::uint64_t>::max() >> shift))
        return std::format("wants a size: a whole number of bytes from 1, or one with K, M, G "
                           "or T after it for KiB, MiB, GiB or TiB, such as 8G; not '{}'", v);
    a.memory_limit = n << shift;
    return {};
}

constexpr std::string_view kConfig = "--config";   // the one option a config file can't set

const std::array kOptions = {
    Option{ "--ref", "<url|path>", "reference video: a file or any URL FFmpeg opens", Form::Pair,
            true, [](Args& a, std::string_view v) { a.ref = v; return std::string(); } },
    Option{ "--dis", "<url|path>", "distorted video, scored against the reference", Form::Pair,
            true, [](Args& a, std::string_view v) { a.dis = v; return std::string(); } },
    Option{ "--ladder", "<url|path>", "HLS master playlist: score each of its video playlists, "
            "against the top rung (see Ladder below)", Form::Ladder, true,
            [](Args& a, std::string_view v) { a.ladder = v; return std::string(); } },
    Option{ "--mezzanine", "<url|path>", "with --ladder: score every video playlist against this "
            "video instead", Form::Ladder, false,
            [](Args& a, std::string_view v) { a.mezzanine = v; return std::string(); } },
    Option{ "--jobs", "N", "with --ladder: score N pairs at a time (default: the physical cores "
            "minus one, at least 1)", Form::Ladder, false, set_jobs },
    Option{ "--memory-limit", "SIZE", "with --ladder: start a pair only while the memory the pool "
            "counts for its pairs stays under SIZE (see The pool below); bytes, or K, M, G or T "
            "for KiB, MiB, GiB or TiB (default: none)", Form::Ladder, false, set_memory_limit },
    Option{ "--out", "<report.json>", "report to write; written when the run succeeds, and with "
            "--ladder also when it is cancelled or a pair fails",
            Form::Any, true, [](Args& a, std::string_view v) { a.out = v; return std::string(); } },
    Option{ "--frames", "N", "score only the first N frame pairs (N >= 1), of each pair with "
            "--ladder", Form::Any, false, set_frames },
    Option{ "--scaler", "NAME", "scales the distorted frames: bilinear, bicubic (default) or lanczos",
            Form::Any, false, set_scaler },
    Option{ "--model", "NAME", "libvmaf built-in model: vmaf_v0.6.1 (default), vmaf_v0.6.1neg, "
            "vmaf_4k_v0.6.1, ...", Form::Any, false,
            [](Args& a, std::string_view v) { a.model.version = v; return std::string(); } },
    Option{ "--model-file", "<model.json>", "libvmaf model file, instead of a built-in model",
            Form::Any, false,
            [](Args& a, std::string_view v) { a.model.path = v; return std::string(); } },
    Option{ kConfig, "<file>", "read settings from a file of name = value lines first (see below)",
            Form::Any, false,
            [](Args& a, std::string_view v) { a.config = v; return std::string(); } },
};

// Options that can't be given together. Each pair sets one thing, so in a
// config file and on the command line it counts as one setting.
const std::array<std::pair<std::string_view, std::string_view>, 1> kExclusive = {{
    { "--model", "--model-file" },
}};

bool is_help(std::string_view a) { return a == "-h" || a == "--help"; }

const Option* find_option(std::string_view name)
{
    for (const Option& o : kOptions)
        if (o.name == name) return &o;
    return nullptr;
}

std::size_t index_of(std::string_view name)
{
    return static_cast<std::size_t>(find_option(name) - kOptions.data());
}

// The option's name in a config file: without the leading "--".
std::string_view file_name(const Option& o) { return o.name.substr(2); }

// The options `k` excludes, and `k` itself.
std::vector<std::size_t> setting_of(std::size_t k)
{
    std::vector<std::size_t> out{ k };
    for (const auto& [a, b] : kExclusive) {
        if (index_of(a) == k) out.push_back(index_of(b));
        if (index_of(b) == k) out.push_back(index_of(a));
    }
    return out;
}

// The first option of each form that `given` marks, by table order; null
// where there is none.
struct FormsGiven {
    const Option* pair   = nullptr;
    const Option* ladder = nullptr;
};

template <class Given>
FormsGiven forms_given(const Given& given)
{
    FormsGiven f;
    for (std::size_t k = 0; k < kOptions.size(); ++k) {
        if (!given[k]) continue;
        if (kOptions[k].form == Form::Pair && !f.pair) f.pair = &kOptions[k];
        if (kOptions[k].form == Form::Ladder && !f.ladder) f.ladder = &kOptions[k];
    }
    return f;
}

std::string_view trimmed(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t") - first + 1);
}

// One setting of a config file.
struct FileSetting {
    std::size_t      option;   // index into kOptions
    std::string_view value;    // into the file's text
    std::size_t      line;     // from 1
};

// The settings of a config file's text, each value checked as its option
// checks it; "" or "line N: what is wrong".
std::string read_settings(std::string_view text, std::vector<FileSetting>& out)
{
    if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);   // a UTF-8 byte order mark
    std::array<std::size_t, kOptions.size()> line_of{};             // 0 = not set yet
    std::size_t line = 0;
    while (!text.empty()) {
        const std::size_t eol = text.find('\n');
        std::string_view raw = text.substr(0, eol);
        text.remove_prefix(eol == std::string_view::npos ? text.size() : eol + 1);
        ++line;
        if (raw.ends_with('\r')) raw.remove_suffix(1);
        const std::string_view entry = trimmed(raw);
        if (entry.empty() || entry.starts_with('#')) continue;

        const std::size_t eq = entry.find('=');
        if (eq == std::string_view::npos)
            return std::format("line {}: expected name = value, not '{}'", line, entry);
        const std::string_view name  = trimmed(entry.substr(0, eq));
        const std::string_view value = trimmed(entry.substr(eq + 1));
        if (name.empty()) return std::format("line {}: no name before '='", line);
        if (name.starts_with('-'))
            return std::format("line {}: unknown setting '{}' (names are written without --)",
                               line, name);
        const Option* opt = find_option(std::format("--{}", name));
        if (!opt) return std::format("line {}: unknown setting '{}'", line, name);
        if (opt->name == kConfig)
            return std::format("line {}: {} can't be set in a config file", line, name);
        const std::size_t k = static_cast<std::size_t>(opt - kOptions.data());
        if (line_of[k])
            return std::format("line {}: {} is set more than once (first on line {})", line, name,
                               line_of[k]);
        line_of[k] = line;
        if (value.empty()) return std::format("line {}: {} needs a non-empty value", line, name);
        Args check;
        if (std::string err = opt->set(check, value); !err.empty())
            return std::format("line {}: {} {}", line, name, err);
        out.push_back({ k, value, line });
    }
    for (const auto& [a, b] : kExclusive)
        if (line_of[index_of(a)] && line_of[index_of(b)])
            return std::format("{} (line {}) and {} (line {}) can't both be set",
                               file_name(*find_option(a)), line_of[index_of(a)],
                               file_name(*find_option(b)), line_of[index_of(b)]);
    // Inputs of both forms: the first setting of each form, by its line.
    std::size_t first_pair = 0, first_ladder = 0;
    for (std::size_t k = 0; k < kOptions.size(); ++k) {
        if (!line_of[k] || kOptions[k].form == Form::Any) continue;
        std::size_t& first = kOptions[k].form == Form::Pair ? first_pair : first_ladder;
        if (!first || line_of[k] < first) first = line_of[k];
    }
    if (first_pair && first_ladder) {
        const auto set_on = [&](std::size_t l) {
            for (std::size_t k = 0; k < kOptions.size(); ++k)
                if (line_of[k] == l) return file_name(kOptions[k]);
            return std::string_view();
        };
        const std::size_t a = std::min(first_pair, first_ladder);
        const std::size_t b = std::max(first_pair, first_ladder);
        return std::format("{} (line {}) and {} (line {}) can't both be set", set_on(a), a,
                           set_on(b), b);
    }
    return {};
}

} // namespace

ConfigFile read_config(const std::string& path)
{
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) return { {}, "it is a directory" };
    std::ifstream f(path, std::ios::binary);
    if (!f) return { {}, std::strerror(errno) };
    std::ostringstream text;
    text << f.rdbuf();
    if (f.bad()) return { {}, "a read error" };
    return { text.str(), {} };
}

std::string parse(const std::vector<std::string_view>& argv, Args& out,
                  const std::function<ConfigFile(const std::string&)>& load)
{
    for (std::string_view a : argv) {
        if (is_help(a)) {
            out = Args{};
            out.help = true;
            return {};
        }
    }

    Args args;
    std::array<bool, kOptions.size()> on_line{};   // given on the command line
    for (std::size_t i = 0; i < argv.size(); ++i) {
        const Option* opt = find_option(argv[i]);
        if (!opt) return std::format("unknown argument '{}'", argv[i]);
        const std::size_t k = static_cast<std::size_t>(opt - kOptions.data());
        if (on_line[k]) return std::format("{} is given more than once", opt->name);
        on_line[k] = true;
        // A value that is itself an option means the real value is missing.
        if (i + 1 >= argv.size() || find_option(argv[i + 1]))
            return std::format("{} needs a value: {} {}", opt->name, opt->name, opt->value);
        const std::string_view v = argv[++i];
        if (v.empty()) return std::format("{} needs a non-empty value", opt->name);
        if (std::string err = opt->set(args, v); !err.empty())
            return std::format("{} {}", opt->name, err);
    }

    // The inputs: a pair or a ladder, not both in one place.
    const FormsGiven line_forms = forms_given(on_line);
    if (line_forms.pair && line_forms.ladder)
        return std::format("{} and {} can't be given together", line_forms.pair->name,
                           line_forms.ladder->name);
    const Form line_form = line_forms.ladder ? Form::Ladder
                           : line_forms.pair ? Form::Pair
                                             : Form::Any;

    // The config file's settings, but those the command line gives, and those
    // of the other form of the inputs than the command line's.
    std::array<bool, kOptions.size()> applied{};
    if (on_line[index_of(kConfig)]) {
        const ConfigFile file = load(args.config);
        if (!file.error.empty())
            return std::format("cannot read the config file {}: {}", args.config, file.error);
        std::vector<FileSetting> settings;
        if (std::string err = read_settings(file.text, settings); !err.empty())
            return std::format("config file {}, {}", args.config, err);
        for (const FileSetting& s : settings) {
            const Form f = kOptions[s.option].form;
            bool replaced = line_form != Form::Any && f != Form::Any && f != line_form;
            for (std::size_t k : setting_of(s.option)) replaced = replaced || on_line[k];
            if (replaced) continue;
            kOptions[s.option].set(args, s.value);   // checked by read_settings
            applied[s.option] = true;
        }
    }

    std::array<bool, kOptions.size()> given{};
    for (std::size_t k = 0; k < kOptions.size(); ++k) given[k] = on_line[k] || applied[k];
    const Form form = forms_given(given).ladder ? Form::Ladder : Form::Pair;
    for (std::size_t k = 0; k < kOptions.size(); ++k) {
        const Option& o = kOptions[k];
        if (o.required && (o.form == Form::Any || o.form == form) && !given[k])
            return std::format("missing {} {}", o.name, o.value);
    }
    for (const auto& [a, b] : kExclusive)
        if (on_line[index_of(a)] && on_line[index_of(b)])
            return std::format("{} and {} can't be given together", a, b);

    out = std::move(args);
    return {};
}

std::string usage()
{
    // One synopsis per form of the inputs.
    const auto synopsis = [](Form form) {
        std::string out = "mirage_vmaf";
        for (const Option& o : kOptions) {
            if (o.form != Form::Any && o.form != form) continue;
            const std::string spec = std::format("{} {}", o.name, o.value);
            out += o.required ? std::format(" {}", spec) : std::format(" [{}]", spec);
        }
        return out;
    };
    std::string options;
    for (const Option& o : kOptions)
        options += std::format("  {:<27}{}\n", std::format("{} {}", o.name, o.value), o.help);
    options += std::format("  {:<27}{}\n", "-h, --help", "print this help and exit");

    return std::format(
        "Usage: {}\n"
        "       {}\n"
        "\n"
        "Scores a distorted video against a reference with Mirage's VMAF analyzer\n"
        "(the scorer behind the player's VMAF panel), without opening a window, and\n"
        "writes a JSON report; with --ladder, every video playlist of an HLS master\n"
        "playlist, --jobs pairs at a time (see Ladder and The pool below).\n"
        "\n"
        "Options:\n"
        "{}"
        "\n"
        "Scoring: the best video stream of each input is decoded, and the frames of\n"
        "the two are paired by timestamp (see Pairing below). Both frames of a pair\n"
        "become 8-bit yuv420p at the reference's size, in the color matrix and range\n"
        "FFmpeg 8.1.1's libvmaf filter graph negotiates for its two inputs, with\n"
        "FFmpeg's rules for missing tags. The distorted frame is scaled with the\n"
        "swscale scaler --scaler names (bicubic unless given, the scaler FFmpeg's\n"
        "scale filter uses without flags=), with swscale's default parameters for it,\n"
        "as FFmpeg's scale filter scales with flags=NAME, and converted to those\n"
        "tags. A yuv420p reference is used as it is, and the tags are its own. A\n"
        "reference in another pixel format is converted to them with swscale's\n"
        "bicubic flags, as the scale filter FFmpeg inserts for such a reference\n"
        "converts it, and the tags are then the distorted frames' own, as far as they\n"
        "have tags. Frames in a pixel format whose conversion isn't checked against\n"
        "FFmpeg's are scored, with a warning in the report. Pairs are scored on 4\n"
        "libvmaf threads with the model --model or --model-file names: libvmaf's\n"
        "built-in vmaf_v0.6.1 unless given, as FFmpeg's libvmaf filter scores with\n"
        "model=version=NAME or model=path=FILE. An input whose color matrix swscale\n"
        "can't convert is not scored, as FFmpeg doesn't score it, nor is a reference\n"
        "whose frames change size: the pairs are scored at the reference's size, and\n"
        "FFmpeg's libvmaf filter takes inputs of one size only.\n"
        "\n"
        "Network inputs: an http(s) or rtsp URL, an input's or a ladder's master's,\n"
        "is opened with FFmpeg's timeout and rw_timeout options of 5 s, and an rtmp\n"
        "URL with rw_timeout alone, since rtmp's own timeout is how long to wait for\n"
        "a connection to it: a connection attempt, or a read or write, that waits\n"
        "longer fails.\n"
        "\n"
        "HLS inputs: FFmpeg's hls demuxer tries a segment it can't open 10 more\n"
        "times, as the player's demuxer does, and reads each file of the stream on a\n"
        "connection of its own. A segment it still goes on without, or the rest of a\n"
        "file whose read fails, ends the input there, as a read error does: the\n"
        "analysis fails, and with --ladder so does the run.\n"
        "\n"
        "Pairing: a frame's timestamp is measured from its input's start: the\n"
        "container's start time, as ffmpeg shifts an input's timestamps without\n"
        "-copyts, or the input's first frame when the container has none. A reference\n"
        "frame and a distorted frame pair when their times differ by at most half the\n"
        "reference's frame duration, each frame with the nearest frame of the other\n"
        "input; the frame rates are those ffmpeg gives its filter graph. When the\n"
        "distorted input's frame rate is lower than the reference's, its frames are\n"
        "the grid and the reference frames between them are skipped; when it is\n"
        "higher, the distorted frames between the reference's frames are. Reference\n"
        "frames no distorted frame pairs with, distorted frames that pair with no\n"
        "reference frame or repeat the timestamp before them, and frames without a\n"
        "timestamp fail. A timestamp that goes back, or forward by more than 10\n"
        "seconds, is a discontinuity: it starts its input's next discontinuity\n"
        "sequence, whose times are measured from it, and the k-th sequence of the\n"
        "reference pairs with the k-th of the distorted input where the jump before\n"
        "them is at the same place in both inputs, within one frame of the coarser of\n"
        "the two frame rates. Where it isn't, pairing stops there, and every frame\n"
        "not paired or counted yet, and every frame after it, fails. The sequences of\n"
        "an input left after the other has ended pair with nothing. Both inputs are\n"
        "read to their ends, unless --frames stops the run, so that every failure is\n"
        "counted. After a reference frame that fails between two pairs, and at each\n"
        "discontinuity sequence, libvmaf starts again, so that no motion feature\n"
        "spans the gap. The failure rate is failed / (paired + failed); above 1% the\n"
        "pair is still scored and reported, with a warning on stderr and in the\n"
        "report.\n"
        "\n"
        "With --frames N the last pair can score differently from the same pair in a\n"
        "longer run, because libvmaf's motion2 feature for a frame also uses the next\n"
        "frame. A run compared with this one has to stop at the same frame.\n"
        "\n"
        "Ladder: --ladder reads an HLS master playlist, a file or any URL FFmpeg\n"
        "opens, and scores its video playlists, --jobs pairs at a time (see The pool\n"
        "below), each as a pair given with --ref and --dis is scored. Its entries are\n"
        "its EXT-X-STREAM-INF tags, each with the URI line after it, resolved against\n"
        "where the master was read from, after HTTP redirects; I-frame playlists and\n"
        "EXT-X-MEDIA renditions aren't entries. An entry has video when it has a\n"
        "RESOLUTION, or a video codec in CODECS (avc1, avc3, hvc1, hev1, dvh1, dvhe,\n"
        "av01, vp09, vp08, mp4v); the others are skipped, and the report lists them.\n"
        "Entries that name the same playlist are scored once. The reference is the\n"
        "top rung, the playlist of the entry with the highest BANDWIDTH, the first in\n"
        "the master among equals, with a warning when they name other playlists, and\n"
        "every other video playlist is scored against it (reference mode top-rung).\n"
        "With --mezzanine the reference is that video instead, and every video\n"
        "playlist, the top rung's included, is scored against it (reference mode\n"
        "mezzanine). --frames limits each pair. A master playlist is refused when its\n"
        "first line isn't #EXTM3U, an entry has no BANDWIDTH of digits only or no URI\n"
        "line after it, a URI line has no entry before it, or a URI isn't one it\n"
        "resolves (from an http(s) master: a relative or absolute path or an http(s)\n"
        "URL, without dot segments or white space); so is a master with nothing to\n"
        "score: no entry with video or, without --mezzanine, no video playlist but\n"
        "the top rung's. A refused master stops the run, which then writes no report.\n"
        "\n"
        "The pool: --jobs pairs are scored at a time, each as a pair given with --ref\n"
        "and --dis is scored, so a pair's scores don't depend on the pool; by default\n"
        "the machine's physical cores minus one, at least 1. With --memory-limit a\n"
        "pair starts only once every running pair has scored 30 frame pairs, or all\n"
        "of its frame pairs if it has fewer, and only while the process's resident\n"
        "memory before the run, plus what a pair uses as measured then and 25% more,\n"
        "for each running pair and the new one, stays under the limit. Running pairs\n"
        "aren't stopped, and with none running a pair always starts. SIGINT or\n"
        "SIGTERM, or a pair that can't be scored, stops the pairs still running and\n"
        "starts no other: the report then lists the complete pairs, says the run was\n"
        "cancelled or failed, and lists the other pairs with their state, and the\n"
        "exit status is 1. The report lists the pairs in the master's order, with the\n"
        "pool's size, the limit, the most pairs scored at once, what a pair was\n"
        "measured to use and the peak resident memory.\n"
        "\n"
        "Config file: --config FILE reads settings before the command line, one per\n"
        "line as name = value, where name is a long option's name without the\n"
        "leading --, such as scaler = lanczos, and value is the rest of the line\n"
        "without the white space around it, as it is: no quotes, no escapes, no\n"
        "comment after it. Blank lines and lines starting with # are skipped. Every\n"
        "option but --config can be set there, once, and is checked as on the\n"
        "command line; --ref, --dis, --ladder, --mezzanine and --out can come from\n"
        "either. An option on the command line replaces the file's setting, and\n"
        "--model and --model-file replace each other, since both set the model;\n"
        "the inputs are either a pair, --ref and --dis, or a ladder, --ladder and\n"
        "--mezzanine, and those of one on the command line leave out the file's of\n"
        "the other. Relative paths are taken from the current directory, as on the\n"
        "command line.\n"
        "\n"
        "The report records the reference mode (explicit-pair: --ref names the\n"
        "reference), the libvmaf version the library reports at run time and\n"
        "the libvmaf package version found when the build was configured, the\n"
        "versions of the FFmpeg libraries at run time (libavutil, libavcodec,\n"
        "libavformat, libswscale, libavfilter), these settings (a model file, and\n"
        "a --config file, each by its path as given and the SHA-256 of its\n"
        "contents), both inputs' stream parameters, how the distorted frames' and\n"
        "the reference's color tags were converted (dis_conversion,\n"
        "ref_conversion), what can make the scores differ from FFmpeg's\n"
        "(warnings), the pooled values, how the frames were paired (alignment: the\n"
        "counts, the failure rate and its threshold, the libvmaf contexts, each\n"
        "input's discontinuity sequences, with the position and PTS of each one's\n"
        "first frame and its number of frames, and each frame that failed, with its\n"
        "input, position, PTS and reason), and for every frame pair its index, the\n"
        "positions of its two frames in their inputs, the reference and distorted PTS\n"
        "in seconds and the score, all at full precision. With --ladder the report\n"
        "has the reference mode, top-rung or mezzanine, one per report, and the\n"
        "settings once; the master playlist, as given, where it was read from, its\n"
        "SHA-256 and its entries with their attributes; the reference, the skipped\n"
        "entries and the warnings about the top rung; and a list of pairs, each with\n"
        "the entries that name its playlist and the fields above that are a pair's\n"
        "own.\n"
        "\n"
        "Pooled values: the mean, the harmonic mean (n / sum(1 / (x + 1)) - 1) and\n"
        "the minimum, each as libvmaf pools the per-frame scores and FFmpeg's\n"
        "libvmaf filter reports it; the minimum without the first frame, whose\n"
        "motion features are 0; the minimum without the first pair of each libvmaf\n"
        "context, which has no motion to a frame before it either (the same value\n"
        "for a pair scored in one context); and the 1st, 5th, 10th, 25th and 50th\n"
        "percentiles by linear interpolation between the closest ranks (Hyndman and\n"
        "Fan's definition 7).\n"
        "\n"
        "Exit status: 0 the report was written, a warning about the pairing included;\n"
        "1 the analysis or the write failed, or with --ladder the master playlist\n"
        "couldn't be read or was refused (no report is left behind, also when SIGINT\n"
        "or SIGTERM ended its read), or the run was cancelled by SIGINT or SIGTERM,\n"
        "or a pair couldn't be scored (the report is written, with the complete pairs\n"
        "and the other pairs' states); 2 bad arguments, or a config file that can't\n"
        "be read or has an error.\n",
        synopsis(Form::Pair), synopsis(Form::Ladder), options);
}

} // namespace vmafcli
