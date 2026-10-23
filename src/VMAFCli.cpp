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
    Option{ "--scaler", "NAME", "scales the distorted frames: bilinear (default), bicubic or lanczos",
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
        "frames of a pair become 8-bit yuv420p at the reference's size. The\n"
        "distorted one is scaled with the swscale scaler --scaler names (bilinear\n"
        "unless given), with swscale's default parameters for it, as FFmpeg's scale\n"
        "filter scales with flags=NAME. Its color matrix and range are converted as\n"
        "FFmpeg 8.1.1's libvmaf filter graph converts them: to the reference's, with\n"
        "FFmpeg's rules for missing tags. The reference frame keeps its size and\n"
        "color. Pairs are scored on 4 libvmaf threads with the model --model or\n"
        "--model-file names: libvmaf's built-in vmaf_v0.6.1 unless given, as\n"
        "FFmpeg's libvmaf filter scores with model=version=NAME or model=path=FILE.\n"
        "An input whose color matrix swscale can't convert is not scored, as FFmpeg\n"
        "doesn't score it.\n"
        "\n"
        "With --frames N the last pair can score differently from the same pair in a\n"
        "longer run, because libvmaf's motion2 feature for a frame also uses the next\n"
        "frame. A run compared with this one has to stop at the same frame.\n"
        "\n"
        "The report records the libvmaf version the library reports at run time and\n"
        "the libvmaf package version found when the build was configured, these\n"
        "settings (a model file by its path as given and the SHA-256 of its\n"
        "contents), both inputs' stream parameters, how the distorted frames' color\n"
        "tags were converted (dis_conversion), libvmaf's pooled mean and min, and\n"
        "for every frame pair its index, the reference and distorted PTS in seconds\n"
        "and the score, all at full precision.\n"
        "\n"
        "Exit status: 0 the report was written; 1 the analysis or the write failed\n"
        "(no report is left behind); 2 bad arguments.\n",
        synopsis, options);
}

} // namespace vmafcli
