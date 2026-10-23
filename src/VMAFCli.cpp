#include "VMAFCli.h"

#include <array>
#include <charconv>
#include <format>
#include <limits>
#include <optional>
#include <system_error>
#include <utility>

namespace vmafcli {

namespace {

// One option that takes a value. A new option is one more row here: parse()
// and usage() both work from this table.
struct Option {
    std::string_view name;      // "--ref"
    std::string_view value;     // placeholder shown in the help, "<url|path>"
    std::string_view help;
    bool             required;
    // Stores `v` in `a`; returns what is wrong with it, or "".
    std::string (*set)(Args& a, std::string_view v);
};

std::string set_frames(Args& a, std::string_view v)
{
    unsigned n = 0;
    const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
    if (ec != std::errc() || end != v.data() + v.size() || n == 0)
        return std::format("--frames wants a whole number from 1 to {}, not '{}'",
                           std::numeric_limits<unsigned>::max(), v);
    a.frames = n;
    return {};
}

std::string set_scaler(Args& a, std::string_view v)
{
    const std::optional<VMAFScaler> s = vmaf_scaler_named(v);
    if (!s) return std::format("--scaler wants bilinear, bicubic or lanczos, not '{}'", v);
    a.scaler = *s;
    return {};
}

const std::array kOptions = {
    Option{ "--ref", "<url|path>", "reference video: a file or any URL FFmpeg opens", true,
            [](Args& a, std::string_view v) { a.ref = v; return std::string(); } },
    Option{ "--dis", "<url|path>", "distorted video, scored against the reference", true,
            [](Args& a, std::string_view v) { a.dis = v; return std::string(); } },
    Option{ "--out", "<report.json>", "report to write; written only when the run succeeds", true,
            [](Args& a, std::string_view v) { a.out = v; return std::string(); } },
    Option{ "--frames", "N", "score only the first N frame pairs (N >= 1)", false, set_frames },
    Option{ "--scaler", "NAME", "scales the distorted frames: bilinear, bicubic (default) or lanczos",
            false, set_scaler },
    Option{ "--model", "NAME", "libvmaf built-in model: vmaf_v0.6.1 (default), vmaf_v0.6.1neg, "
            "vmaf_4k_v0.6.1, ...", false,
            [](Args& a, std::string_view v) { a.model.version = v; return std::string(); } },
    Option{ "--model-file", "<model.json>", "libvmaf model file, instead of a built-in model", false,
            [](Args& a, std::string_view v) { a.model.path = v; return std::string(); } },
};

// Options that can't be given together.
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

} // namespace

std::string parse(const std::vector<std::string_view>& argv, Args& out)
{
    for (std::string_view a : argv) {
        if (is_help(a)) {
            out = Args{};
            out.help = true;
            return {};
        }
    }

    Args args;
    std::array<bool, kOptions.size()> seen{};
    for (std::size_t i = 0; i < argv.size(); ++i) {
        const Option* opt = find_option(argv[i]);
        if (!opt) return std::format("unknown argument '{}'", argv[i]);
        const std::size_t k = static_cast<std::size_t>(opt - kOptions.data());
        if (seen[k]) return std::format("{} is given more than once", opt->name);
        seen[k] = true;
        // A value that is itself an option means the real value is missing.
        if (i + 1 >= argv.size() || find_option(argv[i + 1]))
            return std::format("{} needs a value: {} {}", opt->name, opt->name, opt->value);
        const std::string_view v = argv[++i];
        if (v.empty()) return std::format("{} needs a non-empty value", opt->name);
        if (std::string err = opt->set(args, v); !err.empty()) return err;
    }
    for (std::size_t k = 0; k < kOptions.size(); ++k)
        if (kOptions[k].required && !seen[k])
            return std::format("missing {} {}", kOptions[k].name, kOptions[k].value);
    for (const auto& [a, b] : kExclusive)
        if (seen[index_of(a)] && seen[index_of(b)])
            return std::format("{} and {} can't be given together", a, b);

    out = std::move(args);
    return {};
}

std::string usage()
{
    std::string synopsis = "mirage_vmaf";
    std::string options;
    for (const Option& o : kOptions) {
        const std::string spec = std::format("{} {}", o.name, o.value);
        synopsis += o.required ? std::format(" {}", spec) : std::format(" [{}]", spec);
        options  += std::format("  {:<27}{}\n", spec, o.help);
    }
    options += std::format("  {:<27}{}\n", "-h, --help", "print this help and exit");

    return std::format(
        "Usage: {}\n"
        "\n"
        "Scores a distorted video against a reference with Mirage's VMAF analyzer\n"
        "(the scorer behind the player's VMAF panel), without opening a window, and\n"
        "writes a JSON report.\n"
        "\n"
        "Options:\n"
        "{}"
        "\n"
        "Scoring: the best video stream of each input is decoded, and frames are\n"
        "paired in the order the decoders output them until either input ends. Both\n"
        "frames of a pair become 8-bit yuv420p at the reference's size, in the color\n"
        "matrix and range FFmpeg 8.1.1's libvmaf filter graph negotiates for its two\n"
        "inputs, with FFmpeg's rules for missing tags. The distorted frame is scaled\n"
        "with the swscale scaler --scaler names (bicubic unless given, the scaler\n"
        "FFmpeg's scale filter uses without flags=), with swscale's default\n"
        "parameters for it, as FFmpeg's scale filter scales with flags=NAME, and\n"
        "converted to those tags. A yuv420p reference is used as it is, and the tags\n"
        "are its own. A reference in another pixel format is converted to them with\n"
        "swscale's bicubic flags, as the scale filter FFmpeg inserts for such a\n"
        "reference converts it, and the tags are then the distorted frames' own, as\n"
        "far as they have tags. Frames in a pixel format whose conversion isn't\n"
        "checked against FFmpeg's are scored, with a warning in the report. Pairs\n"
        "are scored on 4 libvmaf threads with the model --model or --model-file\n"
        "names: libvmaf's built-in vmaf_v0.6.1 unless given, as FFmpeg's libvmaf\n"
        "filter scores with model=version=NAME or model=path=FILE. An input whose\n"
        "color matrix swscale can't convert is not scored, as FFmpeg doesn't score\n"
        "it.\n"
        "\n"
        "With --frames N the last pair can score differently from the same pair in a\n"
        "longer run, because libvmaf's motion2 feature for a frame also uses the next\n"
        "frame. A run compared with this one has to stop at the same frame.\n"
        "\n"
        "The report records the reference mode (explicit-pair: --ref names the\n"
        "reference), the libvmaf version the library reports at run time and\n"
        "the libvmaf package version found when the build was configured, the\n"
        "versions of the FFmpeg libraries at run time (libavutil, libavcodec,\n"
        "libavformat, libswscale, libavfilter), these settings (a model file by its\n"
        "path as given and the SHA-256 of its contents), both inputs' stream\n"
        "parameters, how the distorted frames' and the reference's color tags were\n"
        "converted (dis_conversion, ref_conversion), what can make the scores\n"
        "differ from FFmpeg's (warnings), the pooled values, and for every frame\n"
        "pair its index, the reference and distorted PTS in seconds and the score,\n"
        "all at full precision.\n"
        "\n"
        "Pooled values: the mean, the harmonic mean (n / sum(1 / (x + 1)) - 1) and\n"
        "the minimum, each as libvmaf pools the per-frame scores and FFmpeg's\n"
        "libvmaf filter reports it; the minimum without the first frame, whose\n"
        "motion features are 0; and the 1st, 5th, 10th, 25th and 50th percentiles\n"
        "by linear interpolation between the closest ranks (Hyndman and Fan's\n"
        "definition 7).\n"
        "\n"
        "Exit status: 0 the report was written; 1 the analysis or the write failed\n"
        "(no report is left behind); 2 bad arguments.\n",
        synopsis, options);
}

} // namespace vmafcli
