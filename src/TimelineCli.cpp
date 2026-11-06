#include "TimelineCli.h"

#include "FileHash.h"
#include "JsonText.h"
#include "LadderReportRead.h"
#include "PlaylistEvents.h"
#include "TimelineAxis.h"
#include "TimelineBuild.h"
#include "TimelineMedia.h"
#include "VMAFAnalyzer.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <numeric>
#include <sstream>
#include <system_error>
#include <utility>
#include <variant>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/log.h>
}

namespace timelinecli {

using timeline::Attribution;
using timeline::CurveRef;
using timeline::Event;
using timeline::Found;
using timeline::Interval;
using timeline::Payload;
using timeline::Source;
using timeline::Timeline;
using timeline::Value;

namespace {

// ── The option tables (the design's P-V) ─────────────────────────────────────

// Which query of attribute's an option belongs to; Any for those of every
// query, and for every option of timeline's.
enum class Form : std::uint8_t { Any, Interval, Frames, Drop, Auto };
constexpr std::size_t kFormCount = static_cast<std::size_t>(Form::Auto) + 1;   // Form's values

// One option. A new option is one more row of its subcommand's table:
// the parser, the config file and the help all work from it.
template <class Args>
struct Option {
    std::string_view name;       // "--report"; "report" in a config file
    std::string_view value;      // its value's placeholder in the help; empty: a flag, no value
    std::string_view help;
    Form             form;
    bool             query;      // its form's query: --interval, --frames, --drop or --auto
    bool             required;
    // Stores `v` in `a`; returns what is wrong with it, or "", in words that
    // follow the option's name ("wants a whole number ...").
    std::string (*set)(Args& a, std::string_view v);
};

constexpr std::string_view kConfig = "--config";   // the one option a config file can't set
constexpr std::string_view kFlagOn = "on";         // a flag's value in a config file: auto = on

bool is_help(std::string_view a) { return a == "-h" || a == "--help"; }

std::string_view trimmed(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t");
    if (first == std::string_view::npos) return {};
    return s.substr(first, s.find_last_not_of(" \t") - first + 1);
}

std::optional<std::int64_t> whole(std::string_view v)
{
    std::int64_t n = 0;
    const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
    if (ec != std::errc() || end != v.data() + v.size() || v.starts_with('-') || v.starts_with('+'))
        return std::nullopt;
    return n;
}

std::string set_threshold(timeline::DetectOptions& d, std::string_view v)
{
    double t = 0;
    const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), t);
    if (ec != std::errc() || end != v.data() + v.size() || !std::isfinite(t) || t < 0)
        return std::format("wants a number of VMAF points from 0, such as 6, not '{}'", v);
    d.threshold = t;
    return {};
}

std::string set_window(timeline::DetectOptions& d, std::string_view v)
{
    const std::optional<std::int64_t> n = whole(v);
    if (!n || *n < 1) return std::format("wants a whole number of points from 1, not '{}'", v);
    d.window = *n;
    return {};
}

std::string set_min_points(timeline::DetectOptions& d, std::string_view v)
{
    const std::optional<std::int64_t> n = whole(v);
    if (!n) return std::format("wants a whole number of points from 0, not '{}'", v);
    d.min_points = *n;
    return {};
}

std::string set_margin(AVRational& out, std::string_view v)
{
    const std::optional<AVRational> s = parse_seconds(v);
    if (!s) return std::format("wants a number of seconds from 0, such as 0.5, not '{}'", v);
    out = *s;
    return {};
}

std::string set_interval(AttributeArgs& a, std::string_view v)
{
    const std::size_t colon = v.find(':');
    const std::optional<AVRational> from =
        colon == std::string_view::npos ? std::nullopt : parse_seconds(v.substr(0, colon));
    const std::optional<AVRational> to =
        colon == std::string_view::npos ? std::nullopt : parse_seconds(v.substr(colon + 1));
    if (!from || !to)
        return std::format("wants <t0>:<t1>, two numbers of seconds from 0 on the axis, such as "
                           "3:5, not '{}'", v);
    if (av_cmp_q(*from, *to) > 0) return std::format("wants t0 at or before t1, not '{}'", v);
    a.query = Query::Interval;
    a.from  = *from;
    a.to    = *to;
    return {};
}

std::string set_frames(AttributeArgs& a, std::string_view v)
{
    const std::size_t dash = v.find('-');
    const std::optional<std::int64_t> first =
        dash == std::string_view::npos ? std::nullopt : whole(v.substr(0, dash));
    const std::optional<std::int64_t> last =
        dash == std::string_view::npos ? std::nullopt : whole(v.substr(dash + 1));
    if (!first || !last)
        return std::format("wants <first>-<last>, two reference frames from 0, such as 75-124, not "
                           "'{}'", v);
    if (*first > *last) return std::format("wants first at or before last, not '{}'", v);
    a.query = Query::Frames;
    a.first = *first;
    a.last  = *last;
    return {};
}

std::string set_drop(AttributeArgs& a, std::string_view v)
{
    const std::optional<std::int64_t> n = whole(v);
    if (!n || *n < 1) return std::format("wants a drop's number, a whole number from 1, not '{}'", v);
    a.query = Query::Drop;
    a.drop  = *n;
    return {};
}

std::string set_auto(AttributeArgs& a, std::string_view v)
{
    if (v != kFlagOn) return std::format("takes no value but on, as auto = on, not '{}'", v);
    a.query = Query::Auto;
    return {};
}

// --curve pair:<k>: k, the pair's number, the ladder report's pairs[].index
// (D-092 (b)), which answer_query() maps to its curve. Design §7's
// session:<id>, a session's curve, is WP09b's: refused here as any other
// text.
std::string set_curve(AttributeArgs& a, std::string_view v)
{
    constexpr std::string_view kPair = "pair:";
    const std::optional<std::int64_t> k =
        v.starts_with(kPair) ? whole(v.substr(kPair.size())) : std::nullopt;
    if (!k || *k > INT_MAX)
        return std::format("wants pair:<k>, the curve of the ladder report's pair k, as its "
                           "pairs[].index numbers it, not '{}'", v);
    a.curve = CurveName{ .kind = CurveRef::Kind::Pair, .number = static_cast<int>(*k) };
    return {};
}

std::string set_format(AttributeArgs& a, std::string_view v)
{
    if (v == "text") a.format = Format::Text;
    else if (v == "json") a.format = Format::Json;
    else return std::format("wants text or json, not '{}'", v);
    return {};
}

constexpr std::string_view kThresholdHelp =
    "a point more than T VMAF points below its baseline is low (default 6)";
constexpr std::string_view kWindowHelp =
    "the baseline is the median of the last W points that weren't low (no default)";
constexpr std::string_view kMinPointsHelp = "a drop is a run of more than N low points (no default)";

// The setters both tables share, for either subcommand's Args.
template <class Args>
std::string set_threshold_of(Args& a, std::string_view v) { return set_threshold(a.detect, v); }
template <class Args>
std::string set_window_of(Args& a, std::string_view v) { return set_window(a.detect, v); }
template <class Args>
std::string set_min_points_of(Args& a, std::string_view v) { return set_min_points(a.detect, v); }
template <class Args>
std::string set_margin_before(Args& a, std::string_view v)
{
    return set_margin(a.overlap.margin_before, v);
}
template <class Args>
std::string set_margin_after(Args& a, std::string_view v)
{
    return set_margin(a.overlap.margin_after, v);
}
template <class Args>
std::string set_config(Args& a, std::string_view v)
{
    a.config = v;
    return {};
}
template <class Args>
std::string set_report(Args& a, std::string_view v)
{
    a.report = v;
    return {};
}
template <class Args>
std::string set_out(Args& a, std::string_view v)
{
    a.out = v;
    return {};
}
std::string set_timeline(AttributeArgs& a, std::string_view v)
{
    a.timeline = v;
    return {};
}

constexpr std::string_view kConfigHelp =
    "read settings from a file of name = value lines first (see below)";

using TOption = Option<TimelineArgs>;
using AOption = Option<AttributeArgs>;

const std::array kTimelineOptions = {
    TOption{ "--report", "<report.json>", "the ladder report mirage_vmaf --ladder wrote", Form::Any,
             false, true, set_report<TimelineArgs> },
    TOption{ "--out", "<timeline.json>", "the timeline document to write", Form::Any, false, true,
             set_out<TimelineArgs> },
    TOption{ "--threshold", "T", kThresholdHelp, Form::Any, false, false,
             set_threshold_of<TimelineArgs> },
    TOption{ "--window", "W", kWindowHelp, Form::Any, false, false, set_window_of<TimelineArgs> },
    TOption{ "--min-points", "N", kMinPointsHelp, Form::Any, false, false,
             set_min_points_of<TimelineArgs> },
    TOption{ "--margin-before", "S", "list the events up to S seconds before a drop too (default 0)",
             Form::Any, false, false, set_margin_before<TimelineArgs> },
    TOption{ "--margin-after", "S", "list the events up to S seconds after a drop's end too "
             "(default 0)", Form::Any, false, false, set_margin_after<TimelineArgs> },
    TOption{ kConfig, "<file>", kConfigHelp, Form::Any, false, false, set_config<TimelineArgs> },
};

const std::array kAttributeOptions = {
    AOption{ "--timeline", "<timeline.json>", "the timeline document to query", Form::Any, false, true,
             set_timeline },
    AOption{ "--interval", "<t0>:<t1>", "the interval from t0 to t1 seconds on the document's axis",
             Form::Interval, true, false, set_interval },
    AOption{ "--frames", "<first>-<last>", "the reference frames first to last, both included",
             Form::Frames, true, false, set_frames },
    AOption{ "--drop", "K", "the document's K-th detected drop (see Drops below)", Form::Drop, true,
             false, set_drop },
    AOption{ "--auto", "", "detect the drops again, with --window and --min-points", Form::Auto, true,
             false, set_auto },
    AOption{ "--report", "<report.json>", "the ladder report the document names (default: the path "
             "the document gives)", Form::Any, false, false, set_report<AttributeArgs> },
    AOption{ "--curve", "pair:<k>", "the curve of the report's pair k, by its pairs[].index (default: "
             "every pair's)", Form::Any, false, false, set_curve },
    AOption{ "--threshold", "T", "with --auto: a point more than T VMAF points below its baseline is "
             "low (default 6)", Form::Auto, false, false, set_threshold_of<AttributeArgs> },
    AOption{ "--window", "W", "with --auto: the baseline is the median of the last W points that "
             "weren't low", Form::Auto, false, false, set_window_of<AttributeArgs> },
    AOption{ "--min-points", "N", "with --auto: a drop is a run of more than N low points", Form::Auto,
             false, false, set_min_points_of<AttributeArgs> },
    AOption{ "--margin-before", "S", "list the events up to S seconds before the interval too "
             "(default 0)", Form::Any, false, false, set_margin_before<AttributeArgs> },
    AOption{ "--margin-after", "S", "list the events up to S seconds after its end too (default 0)",
             Form::Any, false, false, set_margin_after<AttributeArgs> },
    AOption{ "--format", "text|json", "the answer as text (default) or as a timeline document",
             Form::Any, false, false, set_format },
    AOption{ "--out", "<file>", "write the answer there instead of to stdout", Form::Any, false, false,
             set_out<AttributeArgs> },
    AOption{ kConfig, "<file>", kConfigHelp, Form::Any, false, false, set_config<AttributeArgs> },
};

// The parser of one option table, as vmafcli's parse() reads its own
// (src/VMAFCli.cpp), with attribute's queries: forms of which one at a time
// is given, each with the options that go with it.
template <class Args, std::size_t N>
class Parser {
public:
    explicit Parser(const std::array<Option<Args>, N>& table) : table_(table) {}

    std::string parse(const std::vector<std::string_view>& argv, Args& out,
                      const ConfigLoader& load) const
    {
        for (std::string_view a : argv) {
            if (is_help(a)) {
                out      = Args{};
                out.help = true;
                return {};
            }
        }

        Args args;
        std::array<bool, N> on_line{};   // given on the command line
        for (std::size_t i = 0; i < argv.size(); ++i) {
            const Option<Args>* opt = find(argv[i]);
            if (!opt) return std::format("unknown argument '{}'", argv[i]);
            const std::size_t k = index_of(opt);
            if (on_line[k]) return std::format("{} is given more than once", opt->name);
            on_line[k] = true;
            std::string_view v = kFlagOn;
            if (!opt->value.empty()) {
                // A value that is itself an option means the real value is missing.
                if (i + 1 >= argv.size() || find(argv[i + 1]))
                    return std::format("{} needs a value: {} {}", opt->name, opt->name, opt->value);
                v = argv[++i];
                if (v.empty()) return std::format("{} needs a non-empty value", opt->name);
            }
            if (std::string err = opt->set(args, v); !err.empty())
                return std::format("{} {}", opt->name, err);
        }
        if (std::string err = forms_apart(on_line); !err.empty()) return err;
        const Form line_form = form_of(on_line);

        // The config file's settings, but those the command line gives, and
        // those of another query than the command line's.
        std::array<bool, N> applied{};
        if (on_line[index_of(find(kConfig))]) {
            const vmafcli::ConfigFile file = load(args.config);
            if (!file.error.empty())
                return std::format("cannot read the config file {}: {}", args.config, file.error);
            std::vector<Setting> settings;
            if (std::string err = read_settings(file.text, settings); !err.empty())
                return std::format("config file {}, {}", args.config, err);
            for (const Setting& s : settings) {
                const Form f = table_[s.option].form;
                if (on_line[s.option] || (line_form != Form::Any && f != Form::Any && f != line_form))
                    continue;
                table_[s.option].set(args, s.value);   // checked by read_settings
                applied[s.option] = true;
            }
        }

        std::array<bool, N> given{};
        for (std::size_t k = 0; k < N; ++k) given[k] = on_line[k] || applied[k];
        for (std::size_t k = 0; k < N; ++k)
            if (table_[k].required && !given[k])
                return std::format("missing {} {}", table_[k].name, table_[k].value);
        if (std::string err = query_given(given); !err.empty()) return err;

        out = std::move(args);
        return {};
    }

private:
    const std::array<Option<Args>, N>& table_;

    // One setting of a config file.
    struct Setting {
        std::size_t      option;   // index into the table
        std::string_view value;    // into the file's text
        std::size_t      line;     // from 1
    };

    const Option<Args>* find(std::string_view name) const
    {
        for (const Option<Args>& o : table_)
            if (o.name == name) return &o;
        return nullptr;
    }
    std::size_t index_of(const Option<Args>* o) const
    {
        return static_cast<std::size_t>(o - table_.data());
    }
    static std::string_view file_name(const Option<Args>& o) { return o.name.substr(2); }

    bool has_queries() const
    {
        return std::any_of(table_.begin(), table_.end(), [](const Option<Args>& o) { return o.query; });
    }

    // The query of `form`.
    const Option<Args>* query_of(Form form) const
    {
        for (const Option<Args>& o : table_)
            if (o.form == form && o.query) return &o;
        return nullptr;
    }

    // The one form `given` marks options of, or Any.
    Form form_of(const std::array<bool, N>& given) const
    {
        for (std::size_t k = 0; k < N; ++k)
            if (given[k] && table_[k].form != Form::Any) return table_[k].form;
        return Form::Any;
    }

    // What is wrong with options of two queries given together on the
    // command line, or empty.
    std::string forms_apart(const std::array<bool, N>& given) const
    {
        // Each form's option to name: its query, if given, else its first.
        std::vector<const Option<Args>*> named;
        for (std::size_t k = 0; k < N; ++k) {
            const Option<Args>& o = table_[k];
            if (!given[k] || o.form == Form::Any) continue;
            auto same = std::find_if(named.begin(), named.end(),
                                     [&o](const Option<Args>* n) { return n->form == o.form; });
            if (same == named.end()) named.push_back(&o);
            else if (o.query) *same = &o;
        }
        if (named.size() < 2) return {};
        const Option<Args>* a = named[0];
        const Option<Args>* b = named[1];
        if (a->query && b->query)
            return std::format("{} and {} can't be given together: one query at a time", a->name,
                               b->name);
        const Option<Args>* lone  = a->query ? b : a;
        const Option<Args>* other = a->query ? a : b;
        return std::format("{} goes with {}, not with {}", lone->name, query_of(lone->form)->name,
                           other->name);
    }

    // With queries in the table, the one query given, and nothing that goes
    // with another.
    std::string query_given(const std::array<bool, N>& given) const
    {
        if (!has_queries()) return {};
        const Form form = form_of(given);
        if (form == Form::Any) {
            std::string list;
            std::vector<std::string_view> names;
            for (const Option<Args>& o : table_)
                if (o.query) names.push_back(o.name);
            for (std::size_t i = 0; i < names.size(); ++i)
                list += std::format("{}{}", i == 0 ? "" : i + 1 == names.size() ? " or " : ", ",
                                    names[i]);
            return std::format("missing a query: {}", list);
        }
        const Option<Args>* query = query_of(form);
        if (!given[index_of(query)]) {
            for (std::size_t k = 0; k < N; ++k)
                if (given[k] && table_[k].form == form)
                    return std::format("{} goes with {}, which isn't given", table_[k].name,
                                       query->name);
        }
        return {};
    }

    // The settings of a config file's text, each value checked as its option
    // checks it; "" or "line N: what is wrong".
    std::string read_settings(std::string_view text, std::vector<Setting>& out) const
    {
        if (text.starts_with("\xEF\xBB\xBF")) text.remove_prefix(3);   // a UTF-8 byte order mark
        std::array<std::size_t, N> line_of{};                           // 0 = not set yet
        std::size_t line = 0;
        while (!text.empty()) {
            const std::size_t eol = text.find('\n');
            std::string_view raw  = text.substr(0, eol);
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
                return std::format("line {}: unknown setting '{}' (names are written without --)", line,
                                   name);
            const Option<Args>* opt = find(std::format("--{}", name));
            if (!opt) return std::format("line {}: unknown setting '{}'", line, name);
            if (opt->name == kConfig)
                return std::format("line {}: {} can't be set in a config file", line, name);
            const std::size_t k = index_of(opt);
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
        // Settings of two queries: the first setting of each, by its line.
        std::array<std::size_t, kFormCount> first_line{};
        std::array<std::size_t, kFormCount> first_option{};
        for (std::size_t k = 0; k < N; ++k) {
            if (!line_of[k] || table_[k].form == Form::Any) continue;
            const auto f = static_cast<std::size_t>(table_[k].form);
            if (!first_line[f] || line_of[k] < first_line[f]) {
                first_line[f]   = line_of[k];
                first_option[f] = k;
            }
        }
        std::vector<std::pair<std::size_t, std::size_t>> forms;   // (line, option)
        for (std::size_t f = 0; f < kFormCount; ++f)
            if (first_line[f]) forms.emplace_back(first_line[f], first_option[f]);
        if (forms.size() >= 2) {
            std::sort(forms.begin(), forms.end());
            return std::format("{} (line {}) and {} (line {}) can't both be set: one query at a time, "
                               "with what goes with it",
                               file_name(table_[forms[0].second]), forms[0].first,
                               file_name(table_[forms[1].second]), forms[1].first);
        }
        return {};
    }
};

// ── Help ─────────────────────────────────────────────────────────────────────

// An option as the help shows it: "--window W", or a flag's name alone.
template <class Args>
std::string spec_of(const Option<Args>& o)
{
    return o.value.empty() ? std::string(o.name) : std::format("{} {}", o.name, o.value);
}

template <class Args, std::size_t N>
std::string synopsis(std::string_view word, const std::array<Option<Args>, N>& table)
{
    std::string out = std::format("mirage_vmaf {}", word);
    bool queries = false;
    for (const Option<Args>& o : table) {
        const std::string spec = spec_of(o);
        if (o.query) {
            out += queries ? std::format(" | {}", spec) : std::format(" ({}", spec);
            queries = true;
            continue;
        }
        if (queries) {
            out += ")";
            queries = false;
        }
        out += o.required ? std::format(" {}", spec) : std::format(" [{}]", spec);
    }
    return queries ? out + ")" : out;
}

template <class Args, std::size_t N>
std::string option_rows(const std::array<Option<Args>, N>& table)
{
    std::string out;
    for (const Option<Args>& o : table) out += std::format("  {:<29}{}\n", spec_of(o), o.help);
    return out + std::format("  {:<29}{}\n", "-h, --help", "print this help and exit");
}

// The first lines of a usage text, up to its first blank line: what a bad
// argument prints.
std::string usage_head(const std::string& usage) { return usage.substr(0, usage.find("\n\n") + 1); }

// ── Seconds and payloads ─────────────────────────────────────────────────────

double seconds_of(AVRational q) { return q.den == 0 ? 0.0 : static_cast<double>(q.num) / q.den; }

std::string rational_text(AVRational q) { return std::format("{}/{}", q.num, q.den); }

// The axis's clock in words (D-073; src/TimelineAxis.h).
std::string_view clock_text(const timeline::Axis& a)
{
    if (a.clock == "playlist-time") return "the reference's playlist time";
    return "the frame grid, each frame at its index times the frame duration";
}

// A signed offset of seconds, three decimals, never "-0.000".
std::string offset_text(double s)
{
    const double r = std::round(s * 1000.0) / 1000.0;
    return std::format("{:+.3f} s", r == 0.0 ? 0.0 : r);
}

const Value* member(const Payload& p, std::string_view name)
{
    for (const auto& [n, v] : p)
        if (n == name) return &v;
    return nullptr;
}

// A payload's value as short text: a string as it is, a number as
// json::number writes it, true or false, null; a list or an object as JSON.
std::string value_text(const Value& v);

std::string payload_text(const Payload& p)
{
    std::string out = "{";
    for (std::size_t i = 0; i < p.size(); ++i)
        out += std::format("{}{}: {}", i == 0 ? "" : ", ", json::quote(p[i].first),
                           std::holds_alternative<std::string>(p[i].second.v)
                               ? json::quote(std::get<std::string>(p[i].second.v))
                               : value_text(p[i].second));
    return out + "}";
}

std::string value_text(const Value& v)
{
    if (const bool* b = std::get_if<bool>(&v.v)) return *b ? "true" : "false";
    if (const auto* i = std::get_if<std::int64_t>(&v.v)) return std::format("{}", *i);
    if (const auto* d = std::get_if<double>(&v.v)) return json::number(*d);
    if (const auto* s = std::get_if<std::string>(&v.v)) return *s;
    if (const auto* l = std::get_if<Value::List>(&v.v)) {
        std::string out = "[";
        for (std::size_t i = 0; i < l->size(); ++i) out += (i == 0 ? "" : ", ") + value_text((*l)[i]);
        return out + "]";
    }
    if (const auto* o = std::get_if<Value::Object>(&v.v)) return payload_text(*o);
    return "null";
}

// A member's text, "?" when the payload lacks it.
std::string member_text(const Payload& p, std::string_view name)
{
    const Value* v = member(p, name);
    return v ? value_text(*v) : std::string("?");
}

// What an event's payload says, in a few words (the design's P-AD); the
// payload itself for a type this doesn't know.
std::string event_detail(const Event& e)
{
    const Payload& p = e.payload;
    const std::string line = std::format("line {}", member_text(p, "line"));
    if (e.type == timeline::kTypeSegment) {
        std::string out =
            std::format("{}, EXTINF {} s", member_text(p, "uri"), member_text(p, "extinf"));
        const Value* r = member(p, "byte_range");
        if (r && std::holds_alternative<Value::Object>(r->v)) {
            const auto& o = std::get<Value::Object>(r->v);
            out += std::format(", bytes {} at {}", member_text(o, "length"), member_text(o, "offset"));
        }
        return out;
    }
    if (e.type == timeline::kTypeDiscontinuity)
        return std::format("{}, discontinuity sequence {}", line,
                           member_text(p, "discontinuity_sequence"));
    if (e.type == timeline::kTypeProgramDateTime)
        return std::format("{}, {}", line, member_text(p, "date"));
    if (e.type == timeline::kTypeDaterange || e.type == timeline::kTypeCueOut ||
        e.type == timeline::kTypeCueOutCont || e.type == timeline::kTypeCueIn)
        return std::format("{}, {}", line, member_text(p, "raw"));
    if (e.type == timeline::kTypeTag) {
        const Value* value = member(p, "value");
        const bool   none  = !value || std::holds_alternative<std::monostate>(value->v);
        return std::format("{}, #{}{}", line, member_text(p, "name"),
                           none ? "" : ":" + value_text(*value));
    }
    if (e.type == timeline::kTypeAdBreak) {
        const std::string end = member_text(p, "end");
        if (end == "cue_in")
            return std::format("from the cue_out at line {} to the cue_in at line {}",
                               member_text(p, "cue_out_line"), member_text(p, "cue_in_line"));
        std::string declared = "?";
        if (const Value* d = member(p, "durations"); d && std::holds_alternative<Value::Object>(d->v))
            declared = member_text(std::get<Value::Object>(d->v), end);
        return std::format("from the cue_out at line {}, ended by its {} of {} s",
                           member_text(p, "cue_out_line"), end, declared);
    }
    if (e.type == timeline::kTypeContextStart)
        return std::format("libvmaf context {}, from frame pair {}", member_text(p, "context"),
                           member_text(p, "first_index"));
    if (e.type == timeline::kTypeUnpairedFrame)
        return std::format("{} frame {}, {}", member_text(p, "input"), member_text(p, "index"),
                           member_text(p, "reason"));
    if (e.type == timeline::kTypeKeyframe)
        return std::format("frame {} of its input", member_text(p, "index"));
    if (e.type == timeline::kTypeCorruptPacket) {
        std::string out = std::format("frame {} of its input", member_text(p, "index"));
        const Value* b = member(p, "next_boundary");
        if (!b || !std::holds_alternative<Value::Object>(b->v)) return out + ", in its last segment";
        const auto& o = std::get<Value::Object>(b->v);
        const Value* disc = member(o, "discontinuity");
        const bool   after = disc && std::holds_alternative<bool>(disc->v) && std::get<bool>(disc->v);
        return out + std::format("; the next segment, {}, starts at reference frame {}{}",
                                 member_text(o, "media_sequence"), member_text(o, "ref_index"),
                                 after ? ", after an EXT-X-DISCONTINUITY" : "");
    }
    return payload_text(p);
}

// A variant by its identity (D-075): its master entries, or for a variant
// without any, the reference's role or its index.
std::string variant_name(const Timeline& t, int v)
{
    if (v < 0 || static_cast<std::size_t>(v) >= t.variants.size()) return "no variant";
    const timeline::Variant& var = t.variants[static_cast<std::size_t>(v)];
    if (var.entries.empty()) return var.pair < 0 ? "the reference" : std::format("variant {}", v);
    std::string out = var.entries.size() == 1 ? "entry" : "entries";
    for (std::size_t i = 0; i < var.entries.size(); ++i)
        out += std::format("{}{}", i == 0 ? " " : ", ", var.entries[i]);
    return out;
}

// A variant with its URL, RESOLUTION, BANDWIDTH and both ranks (D-075).
std::string variant_text(const Timeline& t, int v)
{
    const timeline::Variant& var = t.variants[static_cast<std::size_t>(v)];
    std::string out = std::format("{} ({}", variant_name(t, v), var.url);
    if (var.resolution) out += std::format(", {}", *var.resolution);
    if (!var.entries.empty()) out += std::format(", BANDWIDTH {}", var.bandwidth);
    if (var.rank_from_top > 0)
        out += std::format("; rank {} from the top, {} from the bottom", var.rank_from_top,
                           var.rank_from_bottom);
    if (var.segment_map != "ok") out += std::format("; segment map {}", var.segment_map);
    return out + ")";
}

int reference_variant(const Timeline& t)
{
    for (std::size_t i = 0; i < t.variants.size(); ++i)
        if (t.variants[i].pair < 0) return static_cast<int>(i);
    return -1;
}

// A curve by its pair's number (D-092 (b)), as --curve names it.
std::string curve_text(const Timeline& t, CurveRef c)
{
    const std::optional<int> pair = timeline::pair_number(t, c);
    return pair ? std::format("pair:{}", *pair) : std::string("of no pair");
}

std::string curve_header(const Timeline& t, CurveRef c)
{
    const int v   = t.curves[static_cast<std::size_t>(c.index)].variant;
    const int ref = reference_variant(t);
    return std::format("Curve {}: {}, scored against {}\n", curve_text(t, c), variant_text(t, v),
                       ref < 0 ? std::string("no reference") : variant_text(t, ref));
}

std::string frames_text(Interval i)
{
    if (i.ref_end <= i.ref_index)
        return std::format("no reference frame, an empty interval at {}", i.ref_index);
    const std::int64_t n = i.ref_end - i.ref_index;
    return std::format("reference frames {}-{} ({} frame{})", i.ref_index, i.ref_end - 1, n,
                       n == 1 ? "" : "s");
}

std::string score_text(const std::optional<double>& v)
{
    return v ? std::format("{:.3f}", *v) : std::string("none");
}

std::string answer_block(const Timeline& t, const Attribution& a, std::int64_t number)
{
    const timeline::Axis& axis = t.axis;
    std::string out = a.detected ? std::format("Drop {}: ", number) : std::string("Interval: ");
    out += std::format("{}, {:.3f}-{:.3f} s; {} point{}", frames_text(a.interval),
                       axis.seconds(a.interval.ref_index), axis.seconds(a.interval.ref_end), a.points,
                       a.points == 1 ? "" : "s");
    if (a.detected) out += std::format(", baseline {}", score_text(a.baseline));
    if (a.points > 0) out += std::format(", min {}, mean {}", score_text(a.min), score_text(a.mean));
    if (a.detected)
        out += std::format(", drop_magnitude {}, mean_drop {}", score_text(a.drop_magnitude),
                           score_text(a.mean_drop));
    if (a.searched.ref_index != a.interval.ref_index || a.searched.ref_end != a.interval.ref_end)
        out += std::format("; events looked for in {} with the margins", frames_text(a.searched));
    out += "\n";
    bool any = false;
    for (std::size_t s = 0; s < timeline::kSourceCount; ++s) {
        for (const Found& f : a.by_source[s]) {
            if (f.event >= t.events.size()) continue;
            const Event& e = t.events[f.event];
            any = true;
            std::string where = variant_name(t, e.variant);
            if (e.segment) where += std::format(", segment {}", *e.segment);
            std::string seconds = offset_text(f.offset_s);
            if (f.offset_end_s) seconds += " to " + offset_text(*f.offset_end_s);
            const std::string frames =
                f.offset_end_frames
                    ? std::format("{:+} to {:+} frames", f.offset_frames, *f.offset_end_frames)
                    : std::format("{:+} frames", f.offset_frames);
            out += std::format("  {:<9}{:<16} {} at {} ({}): {}\n",
                               timeline::source_name(static_cast<Source>(s)), e.type, where, seconds,
                               frames, event_detail(e));
        }
    }
    if (!any) out += "  no event\n";
    return out;
}

// ── Files and messages ───────────────────────────────────────────────────────

struct FileText {
    std::string text;
    std::string error;   // empty = read
};

// A file's bytes, as mirage_vmaf reads its config file (vmafcli::read_config).
FileText read_file(const std::string& path)
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

int bad_arguments(std::string_view tool, std::string_view word, const std::string& err,
                  const std::string& usage)
{
    std::fprintf(stderr, "%.*s: %s\n%sRun mirage_vmaf %.*s --help for details.\n",
                 static_cast<int>(tool.size()), tool.data(), err.c_str(), usage_head(usage).c_str(),
                 static_cast<int>(word.size()), word.data());
    return 2;
}

int failed(std::string_view tool, const std::string& err, int status = 1)
{
    std::fprintf(stderr, "%.*s: %s\n", static_cast<int>(tool.size()), tool.data(), err.c_str());
    return status;
}

void warn(std::string_view tool, const std::string& w)
{
    std::fprintf(stderr, "%.*s: warning: %s\n", static_cast<int>(tool.size()), tool.data(), w.c_str());
}

// FFmpeg's warnings and errors only, and its network ready, as mirage_vmaf
// sets them after its own parse (tools/mirage_vmaf.cpp; the design's P-N).
void setup_ffmpeg()
{
    av_log_set_level(AV_LOG_WARNING);
    avformat_network_init();
}

std::vector<std::pair<std::string, std::string>> ffmpeg_libraries()
{
    std::vector<std::pair<std::string, std::string>> out;
    for (const VMAFLibraryVersion& l : vmaf_ffmpeg_libraries()) out.emplace_back(l.name, l.version);
    return out;
}

Value int_or_null(const std::optional<std::int64_t>& v) { return v ? Value{ *v } : Value{}; }

// Reads `text` back as the reader would, the check before a document is
// written (src/TimelineDoc.h).
std::string reads_back(const std::string& text)
{
    Timeline t;
    std::vector<Attribution> a;
    timeline::DocInfo i;
    std::string err;
    timeline::from_json(text, t, a, i, err);
    return err;
}

constexpr std::string_view kTimelineTool  = "mirage_vmaf timeline";
constexpr std::string_view kAttributeTool = "mirage_vmaf attribute";

} // namespace

// ── Parsing ──────────────────────────────────────────────────────────────────

std::string parse_timeline(const std::vector<std::string_view>& argv, TimelineArgs& out,
                           const ConfigLoader& load)
{
    TimelineArgs args;
    if (std::string err = Parser(kTimelineOptions).parse(argv, args, load); !err.empty()) return err;
    if (std::string err = timeline::check_options(args.detect); !err.empty()) return err;
    out = std::move(args);
    return {};
}

std::string parse_attribute(const std::vector<std::string_view>& argv, AttributeArgs& out,
                            const ConfigLoader& load)
{
    AttributeArgs args;
    if (std::string err = Parser(kAttributeOptions).parse(argv, args, load); !err.empty()) return err;
    if (args.help) {
        out = std::move(args);
        return {};
    }
    // D-077: W and N have no default, so detection again needs both.
    if (args.query == Query::Auto && !args.detect.given())
        return std::format("--auto needs --window and --min-points: W and N have no default (D-077)");
    if (std::string err = timeline::check_options(args.detect); !err.empty()) return err;
    out = std::move(args);
    return {};
}

std::optional<AVRational> parse_seconds(std::string_view text)
{
    const std::size_t point = text.find('.');
    const std::string_view whole_part = text.substr(0, point);
    const std::string_view fraction =
        point == std::string_view::npos ? std::string_view() : text.substr(point + 1);
    const auto digits = [](std::string_view s) {
        return !s.empty() &&
               std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; });
    };
    if (!digits(whole_part) || (point != std::string_view::npos && !digits(fraction)))
        return std::nullopt;
    if (whole_part.size() + fraction.size() > 18) return std::nullopt;   // within an int64's digits
    std::int64_t num = 0, den = 1;
    for (char c : whole_part) num = num * 10 + (c - '0');
    for (char c : fraction) {
        num = num * 10 + (c - '0');
        den *= 10;
    }
    const std::int64_t g = std::gcd(num, den);
    num /= g;
    den /= g;
    if (num > INT_MAX || den > INT_MAX) return std::nullopt;
    return AVRational{ static_cast<int>(num), static_cast<int>(den) };
}

std::string check_interval(const timeline::Axis& axis, AVRational from, AVRational to)
{
    const std::string asked =
        std::format("--interval {}:{}", json::number(seconds_of(from)), json::number(seconds_of(to)));
    if (from.num < 0 || from.den <= 0 || to.num < 0 || to.den <= 0)
        return asked + ": not two numbers of seconds from 0";
    if (axis.frames <= 0) return asked + ": the document's axis has no reference frame";
    using i128 = __int128;
    // Its first frame, D-081 (g)'s, one of the axis's; t1 at or before the
    // seconds of one past the last frame, which a span to the end has.
    const bool starts = axis.frame_at_or_after(from) < axis.frames;
    const bool ends   = static_cast<i128>(to.num) * axis.tick_den <=
                      static_cast<i128>(axis.ticks_at(axis.frames)) * to.den;
    if (starts && ends) return {};
    if (axis.frame_duration.num <= 0 || axis.frame_duration.den <= 0)
        return std::format("{}: the document's axis has no seconds, each of its {} reference frames at "
                           "0 s (its warnings say why): give the frames with --frames", asked,
                           axis.frames);
    const std::int64_t last = axis.frames - 1;
    if (!starts)
        return std::format("{}: no reference frame of the document's axis starts at or after {} s; its "
                           "last, {}, starts at {} s", asked, json::number(seconds_of(from)), last,
                           json::number(axis.seconds(last)));
    return std::format("{}: the document's axis ends at {} s, where its last reference frame, {}, ends",
                       asked, json::number(axis.seconds(axis.frames)), last);
}

// ── Help ─────────────────────────────────────────────────────────────────────

std::string timeline_usage()
{
    return std::format(
        "Usage: {}\n"
        "\n"
        "Builds the event timeline of a ladder report that mirage_vmaf --ladder wrote,\n"
        "and writes it as a timeline document of its own (docs/TIMELINE_FORMAT.md),\n"
        "which names the report by the SHA-256 of its bytes and of its scores digest\n"
        "and copies none of its scores. It reads each scored media playlist again, and\n"
        "each scored input once more as the scorer opened it, without decoding, to\n"
        "place every frame in its segment by byte position; a frame whose PTS isn't\n"
        "the report's leaves its variant without a segment map, and its segments,\n"
        "tags, cues, keyframes and corrupt packets out, with a warning. Every event\n"
        "sits at a reference frame: a distorted frame at the reference frame it is\n"
        "paired with, one that isn't paired at the next paired frame of its input,\n"
        "a tag where the segment whose URI follows it starts. Offsets are seconds on\n"
        "the reference's playlist time, or the frame index times the frame duration\n"
        "where the reference has no segment map. Anchors that can't be placed are\n"
        "listed in the document's warnings, and printed.\n"
        "\n"
        "Options:\n"
        "{}"
        "\n"
        "Detection: on each pair's curve, when --window and --min-points are both\n"
        "given: a point is low when its score is more than T below the median of the\n"
        "last W points before it that weren't low, which is used once W/2 such points\n"
        "exist, so that the first W/2 points of a curve are never low; a drop is a run\n"
        "of more than N low points, from its first point's reference frame to the\n"
        "frame of the curve's next point. W and N count points of the curve and have\n"
        "no default. Without both, the document has \"detection\": null and no\n"
        "attributions, and a line on stderr says so. Each drop's attribution lists the\n"
        "events of the pair's distorted playlist and of the reference that meet its\n"
        "frames, half-open, widened by the margins, grouped by source.\n"
        "\n"
        "Config file: --config FILE reads settings before the command line, one per\n"
        "line as name = value, where name is a long option's name without the\n"
        "leading --, such as window = 50, and value is the rest of the line without\n"
        "the white space around it. Blank lines and lines starting with # are\n"
        "skipped. Every option but --config can be set there, once, and is checked as\n"
        "on the command line, which replaces the file's setting. The document records\n"
        "every option with its value, and the file by its path and SHA-256. Relative\n"
        "paths, the report's own among them, are taken from the current directory.\n"
        "\n"
        "Exit status: 0 the document was written; 1 an input couldn't be read or was\n"
        "refused (a live playlist, a report of another layout or of a pair) or the\n"
        "document couldn't be written: no new document is left at --out; 2 bad\n"
        "arguments, or a config file that can't be read or has an error.\n",
        synopsis("timeline", kTimelineOptions), option_rows(kTimelineOptions));
}

std::string attribute_usage()
{
    return std::format(
        "Usage: {}\n"
        "\n"
        "Answers which events of a timeline document, which mirage_vmaf timeline wrote,\n"
        "meet an interval of a pair's curve, without reading media. It reads the\n"
        "ladder report the document names and checks that its SHA-256 is the\n"
        "document's, or for a redacted copy that its scores digest is, and takes the\n"
        "pairs' curves from it. The interval is one query: seconds on the document's\n"
        "axis (--interval), reference frames (--frames), one of the document's drops\n"
        "(--drop), or each drop that detection finds again with the parameters given\n"
        "(--auto, as mirage_vmaf timeline detects; W and N have no default). Without\n"
        "--curve every pair's curve is answered. An event meets [a, b) of reference\n"
        "frames when it is a point at a frame f with a <= f < b, or a span [s, e)\n"
        "with s < b and e > a; the margins widen the interval first. The events are\n"
        "those of the pair's distorted playlist and of the reference, in groups by\n"
        "source in this order: vmaf, network, playlist, scte35, player, decoder; and by\n"
        "offset within a group, with their offsets in seconds and in reference frames.\n"
        "\n"
        "Options:\n"
        "{}"
        "\n"
        "Drops are numbered from 1 in the order the answer lists them, each curve's in\n"
        "turn and along it, or with --curve that curve's only; --drop K counts the\n"
        "document's drops so.\n"
        "\n"
        "An --interval t0:t1 is the reference frames from the first whose seconds are\n"
        "at or after t0 to the first at or after t1. As --frames can't go past the\n"
        "axis's last frame, a frame has to start at or after t0, and t1 can't be after\n"
        "the axis's end, where its last frame ends. The margins stop at the axis's ends.\n"
        "\n"
        "Output: as text, by default, one block per drop or interval, with its curve,\n"
        "frames, seconds, points, baseline, minimum, mean, drop_magnitude (the\n"
        "baseline minus the minimum) and mean_drop (the baseline minus the mean), then\n"
        "a line per event; or with --format json as a timeline document whose\n"
        "\"command\" is \"attribute\" and whose attributions are the answer.\n"
        "\n"
        "Config file: --config FILE reads settings before the command line, one per\n"
        "line as name = value, where name is a long option's name without the\n"
        "leading --, such as format = json; --auto is auto = on. Blank lines and lines\n"
        "starting with # are skipped. Every option but --config can be set there,\n"
        "once, and is checked as on the command line, which replaces the file's\n"
        "setting: a query on the command line replaces the file's, and leaves out its\n"
        "threshold, window and min-points unless the query is --auto. Relative paths,\n"
        "the report's path in the document among them, are taken from the current\n"
        "directory.\n"
        "\n"
        "Exit status: 0 the answer was written, a drop or none; 1 the document or the\n"
        "report couldn't be read, or the report isn't the one the document names, or\n"
        "the answer couldn't be written; 2 bad arguments, a config file that can't be\n"
        "read or has an error, or a curve, a drop, frames or an interval that the\n"
        "document doesn't have.\n",
        synopsis("attribute", kAttributeOptions), option_rows(kAttributeOptions));
}

// ── Records and text ─────────────────────────────────────────────────────────

Payload timeline_options(const TimelineArgs& a)
{
    return {
        { "report", Value{ a.report } },
        { "out", Value{ a.out } },
        { "threshold", Value{ a.detect.threshold } },
        { "window", int_or_null(a.detect.window) },
        { "min-points", int_or_null(a.detect.min_points) },
        { "margin-before", Value{ rational_text(a.overlap.margin_before) } },
        { "margin-after", Value{ rational_text(a.overlap.margin_after) } },
    };
}

Payload attribute_options(const AttributeArgs& a, const std::string& report)
{
    const bool interval = a.query == Query::Interval, frames = a.query == Query::Frames;
    return {
        { "timeline", Value{ a.timeline } },
        { "interval", interval ? Value{ Value::List{ Value{ rational_text(a.from) },
                                                     Value{ rational_text(a.to) } } }
                               : Value{} },
        { "frames", frames ? Value{ Value::List{ Value{ a.first }, Value{ a.last } } } : Value{} },
        { "drop", a.query == Query::Drop ? Value{ a.drop } : Value{} },
        { "auto", Value{ a.query == Query::Auto } },
        { "report", Value{ report } },
        // A session's curve, design §7's session:<id>, is WP09b's to record:
        // none is recorded for it here, and answer_query() refuses it.
        { "curve", a.curve && a.curve->kind == CurveRef::Kind::Pair
                       ? Value{ std::format("pair:{}", a.curve->number) }
                       : Value{} },
        { "threshold", Value{ a.detect.threshold } },
        { "window", int_or_null(a.detect.window) },
        { "min-points", int_or_null(a.detect.min_points) },
        { "margin-before", Value{ rational_text(a.overlap.margin_before) } },
        { "margin-after", Value{ rational_text(a.overlap.margin_after) } },
        { "format", Value{ std::string(a.format == Format::Json ? "json" : "text") } },
        { "out", a.out.empty() ? Value{} : Value{ a.out } },
    };
}

std::string no_detection_note(const timeline::DetectOptions& detect)
{
    if (detect.given()) return {};
    const char* missing = !detect.window && !detect.min_points
                              ? "neither --window nor --min-points was given"
                          : !detect.window ? "--window wasn't given"
                                           : "--min-points wasn't given";
    return std::format("{}: no detection: {}, and W and N have no default (D-077): the document has "
                       "\"detection\": null and no attributions\n", kTimelineTool, missing);
}

std::string answer_text(const Timeline& timeline, const std::vector<CurveRef>& curves,
                        const std::vector<Attribution>& answers,
                        const std::vector<std::int64_t>& numbers)
{
    std::string out;
    for (const CurveRef c : curves) {
        // A pair's curve; a session's, and its answers, are WP09b's to print:
        // nothing is printed for them here.
        if (c.kind != CurveRef::Kind::Pair || c.index < 0 ||
            static_cast<std::size_t>(c.index) >= timeline.curves.size())
            continue;
        out += "\n" + curve_header(timeline, c);
        bool any = false;
        for (std::size_t i = 0; i < answers.size(); ++i) {
            if (answers[i].curve.kind != c.kind || answers[i].curve.index != c.index) continue;
            any = true;
            out += answer_block(timeline, answers[i], i < numbers.size() ? numbers[i] : 0);
        }
        if (!any) out += "  no drop\n";
    }
    return out;
}

// ── The subcommands ──────────────────────────────────────────────────────────

namespace {

// A failure's message and its exit status (the design's P-W): 1 an input,
// 2 an argument.
struct Failure {
    std::string message;
    int         status = 1;
};

// The SHA-256 of a --config file's contents, of the bytes the parse reads,
// kept by the loader that reads them.
struct HashingLoader {
    std::string* sha256;
    vmafcli::ConfigFile operator()(const std::string& path) const
    {
        vmafcli::ConfigFile file = vmafcli::read_config(path);
        if (file.error.empty()) *sha256 = filehash::sha256_hex(file.text);
        return file;
    }
};

// Each scored input of `in.scores`: its media playlist read again (P-D)
// and parsed, and its demux pass (D-074). The reference is a playlist unless
// it is a mezzanine (design §5.2). Prints a line for each.
std::optional<Failure> read_variants(timeline::BuildInputs& in)
{
    const timeline::LadderScores& scores = in.scores;
    const std::size_t variants = scores.pairs.size() + 1;
    in.variants.resize(variants);
    for (std::size_t v = 0; v < variants; ++v) {
        const std::string& url = v == 0 ? scores.reference_url : scores.pairs[v - 1].dis_url;
        timeline::VariantRead& read = in.variants[v];
        const bool playlist = v > 0 || scores.reference_mode != "mezzanine";
        if (playlist) {
            const timeline::PlaylistText text = timeline::read_playlist(url);
            if (!text.error.empty())
                return Failure{ std::format("cannot read variant {}'s playlist {}: {}", v, url,
                                            text.error) };
            read.playlist =
                timeline::parse_playlist_events(text.text, text.location, static_cast<int>(v));
            if (!read.playlist->error.empty())
                return Failure{ std::format("variant {}'s playlist {}: {}", v, url,
                                            read.playlist->error) };
            read.playlist_location = text.location;
            read.playlist_sha256   = text.sha256;
        }
        // Views of the scores, which outlive the pass (src/TimelineMedia.h).
        std::vector<timeline::ScoredFrames> scored;
        if (v == 0) scored = timeline::reference_frames(scores);
        else scored.push_back(timeline::distorted_frames(scores.pairs[v - 1]));
        std::string err;
        const hls::MediaTimeline* segments = playlist ? &read.playlist->timeline : nullptr;
        read.media = timeline::read_media(url, segments, scored, {}, err);
        if (!err.empty())
            return Failure{ std::format("the demux pass of variant {} ({}) failed: {}", v, url, err) };

        const timeline::VariantMedia& m = read.media;
        std::string what;
        if (!m.refused.empty()) what = std::format("no segment map, {}", m.refused);
        else if (playlist)
            what = std::format("{} frames in {} segments", m.pts.size(), m.segments.size());
        else what = std::format("{} frames, not a playlist", m.pts.size());
        // The variant's index, as the document, the warnings and the errors
        // number it, then its place in the count.
        std::fputs(std::format("{}: variant {} ({} of {}), {}: {}\n", kTimelineTool, v, v + 1, variants,
                               url, what).c_str(),
                   stdout);
        std::fflush(stdout);
    }
    return std::nullopt;
}

// What attribute answers.
struct Answer {
    std::vector<CurveRef>              curves;    // the curves it answers for, in order
    std::vector<Attribution>           answers;
    std::vector<std::int64_t>          numbers;   // each answer's drop number; 0: an interval
    std::optional<timeline::Detection> detection; // what its drops come from; nullopt: none
    std::string                        query;     // the query in words
};

// `args`' query on `t`, the document's `recorded` attributions and `info`.
std::optional<Failure> answer_query(const AttributeArgs& args, const Timeline& t,
                                    const std::vector<Attribution>& recorded,
                                    const timeline::DocInfo& info, Answer& out)
{
    // A session's curve, design §7's --curve session:<id>, is WP09b's to
    // answer: set_curve() makes none, and one given here is refused, since a
    // document this reader takes has no session (src/TimelineDoc.h).
    if (args.curve && args.curve->kind != CurveRef::Kind::Pair)
        return Failure{ "--curve names a session's curve: the document has no session", 2 };
    // --curve names a pair by its number, the report's pairs[].index: the
    // curve whose variant has it (D-092 (b)); a number no complete pair of
    // the document has is an argument it doesn't fit (D-099 (c)).
    std::optional<CurveRef> asked;
    if (args.curve) {
        asked = timeline::curve_of_pair(t, args.curve->number);
        if (!asked) {
            const std::vector<CurveRef> all = timeline::pair_curve_refs(t);
            std::string has = all.empty() ? std::string("it has none")
                                          : all.size() == 1 ? std::string("its curve is ")
                                                            : std::string("its curves are ");
            for (std::size_t i = 0; i < all.size(); ++i)
                has += std::format("{}{}", i == 0 ? "" : i + 1 == all.size() ? " and " : ", ",
                                   curve_text(t, all[i]));
            return Failure{ std::format("--curve pair:{}: the document has no curve of pair {}: {}",
                                        args.curve->number, args.curve->number, has), 2 };
        }
        out.curves = { *asked };
    } else {
        out.curves = timeline::pair_curve_refs(t);
    }
    const std::string on_curve =
        args.curve ? std::format(" on curve pair:{}", args.curve->number) : std::string();

    switch (args.query) {
    case Query::Auto: {
        std::string err;
        out.answers = timeline::attribute_drops(t, out.curves, args.detect, args.overlap, err);
        if (!err.empty()) return Failure{ err };
        for (std::size_t i = 0; i < out.answers.size(); ++i)
            out.numbers.push_back(static_cast<std::int64_t>(i) + 1);
        out.detection = timeline::Detection{ .options = args.detect, .curves = out.curves };
        out.query     = std::format("the drops detection finds again, T {}, W {}, N {} (--auto)",
                                    json::number(args.detect.threshold), *args.detect.window,
                                    *args.detect.min_points);
        return std::nullopt;
    }
    case Query::Drop: {
        // The document's detected drops, numbered as the answer lists them.
        std::vector<const Attribution*> drops;
        for (const Attribution& a : recorded)
            if (a.detected &&
                (!asked || (a.curve.kind == asked->kind && a.curve.index == asked->index)))
                drops.push_back(&a);
        if (static_cast<std::size_t>(args.drop) > drops.size()) {
            // Why its detection is null, by the command that wrote it:
            // mirage_vmaf timeline's document was built without W and N, and
            // an answer of attribute's has it null only for an interval,
            // since --drop and --auto write theirs.
            std::string why;
            if (!info.detection) {
                why = ": its detection is null";
                if (info.command == "timeline")
                    why += ", W and N weren't given when it was built (D-077)";
                else if (info.command == "attribute")
                    why += ", the answer of mirage_vmaf attribute to an interval, which detects "
                           "nothing";
            }
            return Failure{ std::format("--drop {}: the document has {} detected drop{}{}{}", args.drop,
                                        drops.size(), drops.size() == 1 ? "" : "s", on_curve, why),
                            2 };
        }
        const Attribution& d = *drops[static_cast<std::size_t>(args.drop - 1)];
        Attribution a    = timeline::attribute(t, d.curve, d.interval, args.overlap);
        a.detected       = true;
        a.baseline       = d.baseline;
        a.drop_magnitude = d.drop_magnitude;
        a.mean_drop      = d.mean_drop;
        out.answers   = { std::move(a) };
        out.numbers   = { args.drop };
        out.curves    = { d.curve };
        out.detection = info.detection;
        out.query     = std::format("the document's drop {}{} (--drop)", args.drop, on_curve);
        return std::nullopt;
    }
    case Query::Frames:
    case Query::Interval: {
        Interval interval;
        if (args.query == Query::Frames) {
            if (args.last >= t.axis.frames)
                return Failure{ std::format("--frames {}-{}: the document's axis has {} reference "
                                            "frames, from 0", args.first, args.last, t.axis.frames),
                                2 };
            interval  = { .ref_index = args.first, .ref_end = args.last + 1 };
            out.query = std::format("{} (--frames)", frames_text(interval));
        } else {
            if (std::string why = check_interval(t.axis, args.from, args.to); !why.empty())
                return Failure{ std::move(why), 2 };
            interval  = timeline::seconds_interval(t.axis, args.from, args.to);
            out.query = std::format("{} to {} s on the axis, {} (--interval)",
                                    json::number(seconds_of(args.from)),
                                    json::number(seconds_of(args.to)), frames_text(interval));
        }
        for (const CurveRef c : out.curves) {
            out.answers.push_back(timeline::attribute(t, c, interval, args.overlap));
            out.numbers.push_back(0);
        }
        return std::nullopt;
    }
    case Query::None:
        break;
    }
    return Failure{ "no query", 2 };   // parse_attribute() refuses it
}

} // namespace

int run_timeline(const std::vector<std::string_view>& argv)
{
    constexpr std::string_view tool = kTimelineTool;
    TimelineArgs args;
    std::string  config_sha256;
    const std::string bad = parse_timeline(argv, args, HashingLoader{ &config_sha256 });
    if (!bad.empty()) return bad_arguments(tool, "timeline", bad, timeline_usage());
    if (args.help) {
        std::fputs(timeline_usage().c_str(), stdout);
        return 0;
    }
    setup_ffmpeg();

    // The ladder report (D-081 (b)), named by two hashes (D-072).
    const FileText report = read_file(args.report);
    if (!report.error.empty())
        return failed(tool, std::format("cannot read the ladder report {}: {}", args.report,
                                        report.error));
    std::string err;
    timeline::BuildInputs in;
    in.scores = timeline::read_ladder_report(report.text, err);
    if (!err.empty())
        return failed(tool, std::format("{} isn't a ladder report the timeline reads: {}", args.report,
                                        err));
    if (const std::optional<Failure> f = read_variants(in)) return failed(tool, f->message, f->status);
    const timeline::LadderScores& scores = in.scores;

    const Timeline t = timeline::build(in, err);
    if (!err.empty()) return failed(tool, err);
    for (const std::string& w : t.axis.warnings) warn(tool, "the axis: " + w);
    for (const std::string& w : t.warnings) warn(tool, w);

    // Detection, only with W and N (D-077).
    std::vector<Attribution> attributions;
    if (args.detect.given()) {
        attributions =
            timeline::attribute_drops(t, timeline::pair_curve_refs(t), args.detect, args.overlap, err);
        if (!err.empty()) return failed(tool, err);
    } else {
        std::fputs(no_detection_note(args.detect).c_str(), stderr);
    }

    timeline::DocInfo info;
    info.command          = "timeline";
    info.ffmpeg_libraries = ffmpeg_libraries();
    info.options          = timeline_options(args);
    if (!args.config.empty()) info.config_file = timeline::FileRef{ args.config, config_sha256 };
    info.report               = timeline::report_info(scores);
    info.report.path          = args.report;
    info.report.sha256        = filehash::sha256_hex(report.text);
    info.report.scores_sha256 = filehash::sha256_hex(timeline::scores_text(scores));
    if (!timeline::reference_info(scores, t.axis, info.reference, err)) return failed(tool, err);
    if (args.detect.given())
        info.detection = timeline::Detection{ .options = args.detect,
                                              .curves  = timeline::pair_curve_refs(t) };
    info.overlap = args.overlap;

    const std::string text = timeline::to_json(t, attributions, info);
    if (const std::string back = reads_back(text); !back.empty())
        return failed(tool, "the document wouldn't read back, so it isn't written: " + back);
    if (!timeline::write_document(args.out, text, err)) return failed(tool, err);

    const std::string detected =
        args.detect.given() ? std::format("{} drop{} on {} curve{}", attributions.size(),
                                          attributions.size() == 1 ? "" : "s", t.curves.size(),
                                          t.curves.size() == 1 ? "" : "s")
                            : std::string("no detection");
    const std::size_t warnings = t.axis.warnings.size() + t.warnings.size();
    std::fputs(std::format("{}: {} events on {} reference frames, offsets in seconds on {}; {}; {} "
                           "warning{}; document written to {}\n",
                           tool, t.events.size(), t.axis.frames, clock_text(t.axis), detected, warnings,
                           warnings == 1 ? "" : "s", args.out)
                   .c_str(),
               stdout);
    return 0;
}

int run_attribute(const std::vector<std::string_view>& argv)
{
    constexpr std::string_view tool = kAttributeTool;
    AttributeArgs args;
    std::string   config_sha256;
    const std::string bad = parse_attribute(argv, args, HashingLoader{ &config_sha256 });
    if (!bad.empty()) return bad_arguments(tool, "attribute", bad, attribute_usage());
    if (args.help) {
        std::fputs(attribute_usage().c_str(), stdout);
        return 0;
    }
    setup_ffmpeg();

    const FileText doc = read_file(args.timeline);
    if (!doc.error.empty())
        return failed(tool, std::format("cannot read the timeline document {}: {}", args.timeline,
                                        doc.error));
    Timeline                 t;
    std::vector<Attribution> recorded;
    timeline::DocInfo        info;
    std::string              err;
    if (!timeline::from_json(doc.text, t, recorded, info, err))
        return failed(tool, std::format("{} isn't a timeline document this reads: {}", args.timeline,
                                        err));

    // The ladder report the document names, by its bytes or, for a redacted
    // copy, by its scores digest (D-072); its path taken from the current
    // directory unless --report gives another (D-081 (b)).
    const std::string report_path = args.report.empty() ? info.report.path : args.report;
    const FileText    report      = read_file(report_path);
    if (!report.error.empty())
        return failed(tool, std::format("cannot read the ladder report {}: {}", report_path,
                                        report.error));
    const timeline::LadderScores scores = timeline::read_ladder_report(report.text, err);
    if (!err.empty())
        return failed(tool, std::format("{} isn't a ladder report the timeline reads: {}", report_path,
                                        err));
    const std::string sha256        = filehash::sha256_hex(report.text);
    const std::string scores_sha256 = filehash::sha256_hex(timeline::scores_text(scores));
    std::string report_check = "its SHA-256 is the one the document names";
    if (sha256 != info.report.sha256) {
        if (scores_sha256 != info.report.scores_sha256)
            return failed(tool, std::format(
                "{} isn't the ladder report the document was built from: neither its SHA-256, {}, nor "
                "its scores digest's, {}, is the one the document names, {} and {} (D-072)",
                report_path, sha256, scores_sha256, info.report.sha256, info.report.scores_sha256));
        report_check = "its bytes aren't the ones the document names, but its scores digest is: a "
                       "redacted copy (D-072)";
        warn(tool, std::format("{}: {}", report_path, report_check));
    }
    if (!timeline::attach_curves(t, scores, err))
        return failed(tool, std::format("{} and {} don't fit: {}", args.timeline, report_path, err));

    Answer a;
    if (const std::optional<Failure> f = answer_query(args, t, recorded, info, a))
        return failed(tool, f->message, f->status);

    std::string text;
    if (args.format == Format::Json) {
        // A timeline document whose attributions are the answer, its own
        // options and report named (the design's §8 layout).
        timeline::DocInfo out = info;
        out.command          = "attribute";
        out.ffmpeg_libraries = ffmpeg_libraries();
        out.options          = attribute_options(args, report_path);
        out.config_file.reset();
        if (!args.config.empty()) out.config_file = timeline::FileRef{ args.config, config_sha256 };
        out.report               = timeline::report_info(scores);
        out.report.path          = report_path;
        out.report.sha256        = sha256;
        out.report.scores_sha256 = scores_sha256;
        out.detection            = a.detection;
        out.overlap              = args.overlap;
        text = timeline::to_json(t, a.answers, out);
        if (const std::string back = reads_back(text); !back.empty())
            return failed(tool, "the answer wouldn't read back as a timeline document: " + back);
    } else {
        text = std::format("Timeline {}: {} events on {} reference frames, offsets in seconds on {}\n",
                           args.timeline, t.events.size(), t.axis.frames, clock_text(t.axis));
        text += std::format("Ladder report {}: {}\n", report_path, report_check);
        text += std::format("Query: {}\n", a.query);
        text += std::format("Overlap: half-open on reference frames, margins {} s before and {} s "
                            "after\n", json::number(seconds_of(args.overlap.margin_before)),
                            json::number(seconds_of(args.overlap.margin_after)));
        if (a.detection) {
            const timeline::DetectOptions& d = a.detection->options;
            text += std::format("Detection: T {}, W {}, N {}; the first {} points of a curve can't "
                                "be low\n", json::number(d.threshold), *d.window, *d.min_points,
                                timeline::warm_up(*d.window));
        }
        text += answer_text(t, a.curves, a.answers, a.numbers);
    }

    if (args.out.empty()) {
        std::fputs(text.c_str(), stdout);
        return 0;
    }
    if (!timeline::write_document(args.out, text, err)) return failed(tool, err);
    std::fputs(std::format("{}: {} answer{} written to {}\n", tool, a.answers.size(),
                           a.answers.size() == 1 ? "" : "s", args.out).c_str(),
               stdout);
    return 0;
}

} // namespace timelinecli
