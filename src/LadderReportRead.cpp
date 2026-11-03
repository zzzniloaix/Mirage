#include "LadderReportRead.h"
#include "JsonRead.h"
#include "JsonText.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <format>
#include <limits>
#include <span>
#include <system_error>
#include <tuple>
#include <utility>

namespace timeline {

namespace {

// ── 75cf6e0's layout ─────────────────────────────────────────────────────────

// The keys of each object of a ladder report that the reader opens, in the
// order vmafreport::ladder_to_json writes them (src/VMAFReport.cpp, whose
// layout src/VMAFReport.h lists): each must be there.
using Keys = std::span<const std::string_view>;
constexpr std::array<std::string_view, 22> kReportKeys = {
    "status", "error", "pool", "tool", "reference_mode", "libvmaf_version",
    "libvmaf_package_version", "ffmpeg_libraries", "config_file", "model", "model_source",
    "model_sha256", "scaler", "n_threads", "n_subsample", "frame_limit", "ladder", "reference",
    "skipped", "warnings", "pairs", "not_scored",
};
constexpr std::array<std::string_view, 7> kPoolKeys = {
    "jobs", "memory_limit", "baseline", "max_running", "pair_memory", "peak_resident", "warnings",
};
constexpr std::array<std::string_view, 4> kLadderKeys = { "url", "location", "sha256", "entries" };
constexpr std::array<std::string_view, 10> kEntryKeys = {
    "index", "line", "uri", "url", "bandwidth", "average_bandwidth", "resolution", "codecs",
    "frame_rate", "video",
};
constexpr std::array<std::string_view, 2> kReferenceKeys = { "url", "entries" };
constexpr std::array<std::string_view, 14> kPairKeys = {
    "index", "entries", "started_s", "ended_s", "scored_as", "dis_conversion", "ref_conversion",
    "warnings", "frames_scored", "pooled", "alignment", "ref", "dis", "frames",
};
constexpr std::array<std::string_view, 10> kInputKeys = {
    "url", "width", "height", "pix_fmt", "color_space", "color_range", "color_primaries",
    "color_trc", "time_base", "ended",
};
constexpr std::array<std::string_view, 22> kAlignmentKeys = {
    "rule", "timestamp_origin", "grid", "tolerance", "reference", "distorted", "paired", "failed",
    "failure_rate", "threshold", "exceeded", "unmatched_reference", "extra_distorted",
    "duplicate_distorted", "no_timestamp", "after_discontinuity", "discontinuities",
    "discontinuity_jump", "off_grid_reference", "off_grid_distorted", "vmaf_contexts", "failures",
};
constexpr std::array<std::string_view, 4> kClockKeys = { "frame_rate", "start", "start_from",
                                                         "sequences" };
constexpr std::array<std::string_view, 3> kSequenceKeys = { "first_index", "first_pts", "frames" };
constexpr std::array<std::string_view, 2> kContextKeys  = { "first_index", "pairs" };
constexpr std::array<std::string_view, 4> kFailureKeys  = { "input", "index", "pts", "reason" };
constexpr std::array<std::string_view, 6> kFrameKeys    = {
    "index", "ref_index", "dis_index", "ref_pts", "dis_pts", "vmaf",
};

constexpr std::string_view kLayouts =
    "the timeline reads ladder reports in 75cf6e0's layout or a later one (DECISIONS.md D-081 (b))";

// ── Where a value is, for messages ───────────────────────────────────────────

// A value's place in the report, made on the stack as the reader goes down
// into it and turned into text only for a message: "pairs[2].frames[17].vmaf".
struct Path {
    const Path*      parent  = nullptr;   // nullptr: the report itself
    std::string_view key;                 // a member's name
    std::size_t      index   = 0;         // an element's position
    bool             element = false;

    Path member(std::string_view k) const { return { this, k, 0, false }; }
    Path item(std::size_t i) const { return { this, {}, i, true }; }
};

std::string text_of(const Path& p)
{
    std::vector<const Path*> chain;
    for (const Path* q = &p; q->parent; q = q->parent) chain.push_back(q);
    if (chain.empty()) return "the report";
    std::string out;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if ((*it)->element) {
            out += std::format("[{}]", (*it)->index);
        } else {
            if (!out.empty()) out += '.';
            out += (*it)->key;
        }
    }
    return out;
}

std::string text_of(const Path& p, std::string_view key) { return text_of(p.member(key)); }

// "num/den" as integers, as the report writes a rational; nullopt otherwise.
std::optional<AVRational> rational_of(std::string_view s)
{
    const std::size_t slash = s.find('/');
    if (slash == std::string_view::npos) return std::nullopt;
    AVRational r{ 0, 1 };
    const char* mid = s.data() + slash;
    const char* end = s.data() + s.size();
    const auto [p1, e1] = std::from_chars(s.data(), mid, r.num);
    const auto [p2, e2] = std::from_chars(mid + 1, end, r.den);
    if (e1 != std::errc() || p1 != mid || e2 != std::errc() || p2 != end) return std::nullopt;
    return r;
}

// An input's frames the analysis read: the frames of its sequences in all,
// as the aligner counts every frame of an input in one of them
// (src/VMAFAlign.cpp, Aligner::push); nullopt beyond INT64_MAX, more than
// any position.
std::optional<std::int64_t> frames_read(const std::vector<Sequence>& sequences)
{
    std::int64_t n = 0;
    for (const Sequence& s : sequences) {
        if (s.frames > std::numeric_limits<std::int64_t>::max() - n) return std::nullopt;
        n += s.frames;
    }
    return n;
}

// ── The reader ───────────────────────────────────────────────────────────────

// Reads a report's tree into a LadderScores. Each function returns false
// with err saying why on the first thing wrong.
class Reader {
public:
    std::string err;

    bool report(const json::Value& doc, LadderScores& out)
    {
        const Path root;
        if (!doc.object())
            return fail(std::format("not a ladder report: the text holds {}, not an object",
                                    json::kind_name(doc)));
        if (!doc.find("pairs") && doc.find("frames"))
            return fail("a pair's report (mirage_vmaf --ref/--dis), not a ladder report: only "
                        "ladder reports are read (DECISIONS.md D-081 (b))");
        if (!doc.find("pairs") && doc.find("results"))
            return fail("the player's VMAF report, not a ladder report of mirage_vmaf");
        if (!keys(doc, kReportKeys, root)) return false;
        if (!keys(*doc.find("pool"), kPoolKeys, root.member("pool"))) return false;

        if (!str(doc, "status", root, out.status) ||
            !str(doc, "reference_mode", root, out.reference_mode) ||
            !str(doc, "libvmaf_version", root, out.libvmaf_version) ||
            !str(doc, "libvmaf_package_version", root, out.libvmaf_package_version) ||
            !str(doc, "model", root, out.model) || !str(doc, "model_source", root, out.model_source) ||
            !str(doc, "scaler", root, out.scaler) || !small(doc, "n_threads", root, out.n_threads) ||
            !small(doc, "n_subsample", root, out.n_subsample))
            return false;
        if (out.reference_mode != "top-rung" && out.reference_mode != "mezzanine")
            return fail(std::format("reference_mode is {}, not \"top-rung\" or \"mezzanine\", a "
                                    "ladder report's",
                                    json::quote(out.reference_mode)));
        std::optional<std::string> model_sha256;
        if (!opt_str(doc, "model_sha256", root, model_sha256)) return false;
        out.model_sha256 = model_sha256.value_or("");
        const json::Value& limit = *doc.find("frame_limit");
        if (!limit.is_null()) {
            std::int64_t n = 0;
            if (!integer(limit, root.member("frame_limit"), n)) return false;
            out.frame_limit = n;
        }

        const json::Value& ladder = *doc.find("ladder");
        const Path         ladder_at = root.member("ladder");
        if (!keys(ladder, kLadderKeys, ladder_at) || !str(ladder, "url", ladder_at, out.master_url) ||
            !str(ladder, "location", ladder_at, out.master_location) ||
            !str(ladder, "sha256", ladder_at, out.master_sha256) || !entries(ladder, ladder_at, out))
            return false;

        const json::Value& ref = *doc.find("reference");
        const Path         ref_at = root.member("reference");
        if (!keys(ref, kReferenceKeys, ref_at) || !str(ref, "url", ref_at, out.reference_url) ||
            !entry_list(ref, ref_at, out.entries.size(), out.reference_entries))
            return false;

        const Path pairs_at = root.member("pairs");
        const json::Value::Array* pairs = array(*doc.find("pairs"), pairs_at);
        if (!pairs) return false;
        out.pairs.resize(pairs->size());
        for (std::size_t k = 0; k < pairs->size(); ++k) {
            const Path at = pairs_at.item(k);
            if (!pair((*pairs)[k], at, out.entries.size(), out.pairs[k])) return false;
            if (k > 0 && out.pairs[k].index <= out.pairs[k - 1].index)
                return fail(std::format("{} is {}, not after the pair before it ({}): pairs are "
                                        "listed in the plan's order",
                                        text_of(at, "index"), out.pairs[k].index,
                                        out.pairs[k - 1].index));
        }
        return true;
    }

private:
    bool fail(std::string what)
    {
        err = std::move(what);
        return false;
    }

    // `v` is an object with each of `list` among its keys.
    bool keys(const json::Value& v, Keys list, const Path& at)
    {
        if (!v.object())
            return fail(std::format("{} is {}, not an object", text_of(at), json::kind_name(v)));
        for (std::string_view k : list)
            if (!v.find(k)) return missing(at, k);
        return true;
    }

    bool missing(const Path& at, std::string_view key)
    {
        return fail(std::format("no {}: {}", text_of(at, key), kLayouts));
    }

    bool wrong(const json::Value& v, const Path& at, std::string_view expected)
    {
        return fail(std::format("{} is {}, not {}", text_of(at), json::kind_name(v), expected));
    }

    const json::Value* get(const json::Value& obj, std::string_view key, const Path& at)
    {
        const json::Value* v = obj.find(key);
        if (!v) missing(at, key);
        return v;
    }

    const json::Value::Array* array(const json::Value& v, const Path& at)
    {
        const json::Value::Array* a = v.array();
        if (!a) wrong(v, at, "an array");
        return a;
    }

    // ── Values of one kind; `at` is where the value is ──

    bool string(const json::Value& v, const Path& at, std::string& out)
    {
        const std::string* s = v.string();
        if (!s) return wrong(v, at, "a string");
        out = *s;
        return true;
    }

    bool integer(const json::Value& v, const Path& at, std::int64_t& out)
    {
        const std::int64_t* n = v.integer();
        if (!n) return wrong(v, at, "an integer");
        out = *n;
        return true;
    }

    bool count(const json::Value& v, const Path& at, std::int64_t& out)
    {
        if (!integer(v, at, out)) return false;
        if (out < 0) return fail(std::format("{} is {}, not a count from 0", text_of(at), out));
        return true;
    }

    bool count(const json::Value& v, const Path& at, std::size_t& out)
    {
        std::int64_t n = 0;
        if (!count(v, at, n)) return false;
        out = static_cast<std::size_t>(n);
        return true;
    }

    // PTS seconds in ticks of `tb`; `pts` nullopt for null when `nullable`.
    bool ticks(const json::Value& v, const Path& at, AVRational tb, bool nullable,
               std::optional<std::int64_t>& pts)
    {
        pts.reset();
        if (v.is_null()) {
            if (nullable) return true;
            return fail(std::format("{} is null, but a scored frame has a PTS, as only frames with "
                                    "a timestamp pair (src/VMAFAlign.h)",
                                    text_of(at)));
        }
        const std::optional<double> s = v.number();
        if (!s) return wrong(v, at, nullable ? "a number or null" : "a number");
        pts = pts_ticks(*s, tb);
        if (!pts)
            return fail(std::format("{} is {} s, which is no PTS in ticks of {}/{}", text_of(at),
                                    json::number(*s), tb.num, tb.den));
        return true;
    }

    // ── Members of an object ──

    bool str(const json::Value& obj, std::string_view key, const Path& at, std::string& out)
    {
        const json::Value* v = get(obj, key, at);
        return v && string(*v, at.member(key), out);
    }

    bool opt_str(const json::Value& obj, std::string_view key, const Path& at,
                 std::optional<std::string>& out)
    {
        const json::Value* v = get(obj, key, at);
        if (!v) return false;
        out.reset();
        if (v->is_null()) return true;
        if (!v->string()) return wrong(*v, at.member(key), "a string or null");
        out = *v->string();
        return true;
    }

    bool boolean(const json::Value& obj, std::string_view key, const Path& at, bool& out)
    {
        const json::Value* v = get(obj, key, at);
        if (!v) return false;
        if (!v->boolean()) return wrong(*v, at.member(key), "true or false");
        out = *v->boolean();
        return true;
    }

    template <typename T>
    bool count(const json::Value& obj, std::string_view key, const Path& at, T& out)
    {
        const json::Value* v = get(obj, key, at);
        return v && count(*v, at.member(key), out);
    }

    // An integer that fits in an int.
    bool small(const json::Value& obj, std::string_view key, const Path& at, int& out)
    {
        const json::Value* v = get(obj, key, at);
        std::int64_t n = 0;
        if (!v || !integer(*v, at.member(key), n)) return false;
        if (n < std::numeric_limits<int>::min() || n > std::numeric_limits<int>::max())
            return fail(std::format("{} is {}, beyond an int", text_of(at, key), n));
        out = static_cast<int>(n);
        return true;
    }

    // A rational "num/den"; `positive` for a time base, else a frame rate,
    // which is {0, 1} where its numerator or denominator isn't positive:
    // the analyzer's unknown rate (src/VMAFAlign.h, Clock).
    bool rational(const json::Value& obj, std::string_view key, const Path& at, bool positive,
                  AVRational& out)
    {
        std::string s;
        if (!str(obj, key, at, s)) return false;
        const std::optional<AVRational> r = rational_of(s);
        if (!r || (positive && (r->num <= 0 || r->den <= 0)))
            return fail(std::format("{} is {}, not {}", text_of(at, key), json::quote(s),
                                    positive ? "a time base, num/den with both above 0"
                                             : "a frame rate, num/den"));
        out = r->num > 0 && r->den > 0 ? *r : AVRational{ 0, 1 };
        return true;
    }

    // A list of master entries' indices, each one of the master's `n`.
    bool entry_list(const json::Value& obj, const Path& at, std::size_t n,
                    std::vector<std::size_t>& out)
    {
        const json::Value*        v    = get(obj, "entries", at);
        const Path                list = at.member("entries");
        const json::Value::Array* a    = v ? array(*v, list) : nullptr;
        if (!a) return false;
        out.resize(a->size());
        for (std::size_t k = 0; k < a->size(); ++k) {
            if (!count((*a)[k], list.item(k), out[k])) return false;
            if (out[k] >= n)
                return fail(std::format("{} is {}, not an entry of the master, which has {}",
                                        text_of(list.item(k)), out[k], n));
        }
        return true;
    }

    // ── The parts of a report ──

    bool entries(const json::Value& ladder, const Path& ladder_at, LadderScores& out)
    {
        const Path                list = ladder_at.member("entries");
        const json::Value::Array* a    = array(*ladder.find("entries"), list);
        if (!a) return false;
        out.entries.resize(a->size());
        for (std::size_t k = 0; k < a->size(); ++k) {
            const json::Value& e  = (*a)[k];
            const Path         at = list.item(k);
            MasterEntry&       m  = out.entries[k];
            if (!keys(e, kEntryKeys, at) || !count(e, "index", at, m.index) ||
                !count(e, "line", at, m.line) || !str(e, "uri", at, m.uri) ||
                !str(e, "url", at, m.url) ||
                !integer(*e.find("bandwidth"), at.member("bandwidth"), m.bandwidth) ||
                !opt_str(e, "average_bandwidth", at, m.average_bandwidth) ||
                !opt_str(e, "resolution", at, m.resolution) || !opt_str(e, "codecs", at, m.codecs) ||
                !opt_str(e, "frame_rate", at, m.frame_rate) || !boolean(e, "video", at, m.video))
                return false;
            if (m.index != k)
                return fail(std::format("{} is {}, not its position in the master's entries, {}",
                                        text_of(at, "index"), m.index, k));
        }
        return true;
    }

    bool pair(const json::Value& p, const Path& at, std::size_t n_entries, PairScores& out)
    {
        if (!keys(p, kPairKeys, at) || !count(p, "index", at, out.index) ||
            !entry_list(p, at, n_entries, out.entries))
            return false;
        for (const auto& [key, url, tb] : { std::tuple{ "ref", &out.ref_url, &out.ref_time_base },
                                            std::tuple{ "dis", &out.dis_url, &out.dis_time_base } }) {
            const json::Value& in    = *p.find(key);
            const Path         in_at = at.member(key);
            if (!keys(in, kInputKeys, in_at) || !str(in, "url", in_at, *url) ||
                !rational(in, "time_base", in_at, true, *tb))
                return false;
        }

        const json::Value& al    = *p.find("alignment");
        const Path         al_at = at.member("alignment");
        if (!keys(al, kAlignmentKeys, al_at)) return false;
        for (const auto& [key, rate, tb, seqs] :
             { std::tuple{ "reference", &out.ref_rate, out.ref_time_base, &out.ref_sequences },
               std::tuple{ "distorted", &out.dis_rate, out.dis_time_base, &out.dis_sequences } }) {
            const json::Value& clock    = *al.find(key);
            const Path         clock_at = al_at.member(key);
            if (!keys(clock, kClockKeys, clock_at) ||
                !rational(clock, "frame_rate", clock_at, false, *rate) ||
                !sequences(clock, clock_at, tb, *seqs))
                return false;
        }
        if (!failures(al, al_at, out)) return false;

        std::size_t frames_scored = 0;
        if (!count(p, "frames_scored", at, frames_scored) || !frames(p, at, out) ||
            !within_inputs(at, out))
            return false;
        if (frames_scored != out.vmaf.size())
            return fail(std::format("{} is {}, but frames lists {}", text_of(at, "frames_scored"),
                                    frames_scored, out.vmaf.size()));
        return contexts(al, al_at, out);
    }

    bool sequences(const json::Value& clock, const Path& clock_at, AVRational tb,
                   std::vector<Sequence>& out)
    {
        const Path                list = clock_at.member("sequences");
        const json::Value::Array* a    = array(*clock.find("sequences"), list);
        if (!a) return false;
        out.resize(a->size());
        for (std::size_t k = 0; k < a->size(); ++k) {
            const json::Value& s  = (*a)[k];
            const Path         at = list.item(k);
            if (!keys(s, kSequenceKeys, at) || !count(s, "first_index", at, out[k].first_index) ||
                !ticks(*s.find("first_pts"), at.member("first_pts"), tb, true, out[k].first_pts) ||
                !count(s, "frames", at, out[k].frames))
                return false;
        }
        return true;
    }

    bool failures(const json::Value& al, const Path& al_at, PairScores& out)
    {
        const Path                list = al_at.member("failures");
        const json::Value::Array* a    = array(*al.find("failures"), list);
        if (!a) return false;
        out.failures.resize(a->size());
        for (std::size_t k = 0; k < a->size(); ++k) {
            const json::Value& f  = (*a)[k];
            const Path         at = list.item(k);
            Failure&           x  = out.failures[k];
            if (!keys(f, kFailureKeys, at) || !str(f, "input", at, x.input) ||
                !count(f, "index", at, x.index) || !str(f, "reason", at, x.reason))
                return false;
            if (x.input != "reference" && x.input != "distorted")
                return fail(std::format("{} is {}, not \"reference\" or \"distorted\"",
                                        text_of(at, "input"), json::quote(x.input)));
            const AVRational tb = x.input == "reference" ? out.ref_time_base : out.dis_time_base;
            if (!ticks(*f.find("pts"), at.member("pts"), tb, true, x.pts)) return false;
        }
        return true;
    }

    bool frames(const json::Value& p, const Path& pair_at, PairScores& out)
    {
        const Path                list = pair_at.member("frames");
        const json::Value::Array* a    = array(*p.find("frames"), list);
        if (!a) return false;
        const std::size_t n = a->size();
        out.ref_index.resize(n);
        out.dis_index.resize(n);
        out.ref_pts.resize(n);
        out.dis_pts.resize(n);
        out.vmaf.resize(n);
        std::optional<std::int64_t> pts;
        for (std::size_t i = 0; i < n; ++i) {
            const json::Value& f  = (*a)[i];
            const Path         at = list.item(i);
            std::int64_t index = 0;
            if (!keys(f, kFrameKeys, at) || !count(f, "index", at, index) ||
                !count(f, "ref_index", at, out.ref_index[i]) ||
                !count(f, "dis_index", at, out.dis_index[i]))
                return false;
            if (index != static_cast<std::int64_t>(i))
                return fail(std::format("{} is {}, not its position in frames, {}",
                                        text_of(at, "index"), index, i));
            for (const auto& [key, positions] :
                 { std::pair{ "ref_index", &out.ref_index }, std::pair{ "dis_index", &out.dis_index } })
                if (i > 0 && (*positions)[i] <= (*positions)[i - 1])
                    return fail(std::format("{} is {}, not after the frame before it ({}), as the "
                                            "aligner pairs frames (src/VMAFAlign.h)",
                                            text_of(at, key), (*positions)[i], (*positions)[i - 1]));
            if (!ticks(*f.find("ref_pts"), at.member("ref_pts"), out.ref_time_base, false, pts))
                return false;
            out.ref_pts[i] = *pts;
            if (!ticks(*f.find("dis_pts"), at.member("dis_pts"), out.dis_time_base, false, pts))
                return false;
            out.dis_pts[i] = *pts;
            const json::Value& score = *f.find("vmaf");
            if (score.is_null()) {
                out.vmaf[i] = std::numeric_limits<double>::quiet_NaN();
            } else if (const std::optional<double> s = score.number()) {
                out.vmaf[i] = *s;
            } else {
                return wrong(score, at.member("vmaf"), "a number or null");
            }
        }
        return true;
    }

    // After sequences() and frames(): each scored frame's position in an
    // input is one of the input's frames the analysis read, so it is below
    // frames_read() of the input's sequences. The positions rise (frames()),
    // so the first one at or beyond that number is the one a refusal names.
    bool within_inputs(const Path& pair_at, const PairScores& out)
    {
        const Path frames_at = pair_at.member("frames");
        const Path al_at     = pair_at.member("alignment");
        for (const auto& [key, input, list, seqs] :
             { std::tuple{ "ref_index", "reference", &out.ref_index, &out.ref_sequences },
               std::tuple{ "dis_index", "distorted", &out.dis_index, &out.dis_sequences } }) {
            const std::optional<std::int64_t> read = frames_read(*seqs);
            if (!read) continue;   // beyond INT64_MAX: no position is at or beyond it
            const auto beyond = std::lower_bound(list->begin(), list->end(), *read);
            if (beyond != list->end())
                return fail(std::format(
                    "{} is {}, not a frame the analysis read of its input: {} have {} frames in all",
                    text_of(frames_at.item(static_cast<std::size_t>(beyond - list->begin())), key),
                    *beyond, text_of(al_at.member(input), "sequences"), *read));
        }
        return true;
    }

    // After frames(): each context's first frame is a frame of the pair,
    // after the first frame of the context before it.
    bool contexts(const json::Value& al, const Path& al_at, PairScores& out)
    {
        const Path                list = al_at.member("vmaf_contexts");
        const json::Value::Array* a    = array(*al.find("vmaf_contexts"), list);
        if (!a) return false;
        out.context_first.resize(a->size());
        for (std::size_t k = 0; k < a->size(); ++k) {
            const json::Value& c  = (*a)[k];
            const Path         at = list.item(k);
            std::int64_t&      first = out.context_first[k];
            if (!keys(c, kContextKeys, at) || !count(c, "first_index", at, first)) return false;
            if (first >= static_cast<std::int64_t>(out.vmaf.size()))
                return fail(std::format("{} is {}, not a frame of the pair, which has {}",
                                        text_of(at, "first_index"), first, out.vmaf.size()));
            if (k > 0 && first <= out.context_first[k - 1])
                return fail(std::format("{} is {}, not after the context before it ({})",
                                        text_of(at, "first_index"), first,
                                        out.context_first[k - 1]));
        }
        return true;
    }
};

// ── The digest's text ────────────────────────────────────────────────────────

std::string quoted_or_null(const std::optional<std::string>& s)
{
    return s ? json::quote(*s) : std::string("null");
}

std::string integer_or_null(const std::optional<std::int64_t>& t)
{
    return t ? std::to_string(*t) : std::string("null");
}

std::string rational_text(AVRational r) { return json::quote(std::format("{}/{}", r.num, r.den)); }

template <typename T>
std::string list_text(const std::vector<T>& v)
{
    std::string out = "[";
    for (std::size_t k = 0; k < v.size(); ++k) out += std::format("{}{}", k == 0 ? "" : ",", v[k]);
    return out + "]";
}

std::string sequences_text(const std::vector<Sequence>& v)
{
    std::string out = "[";
    for (std::size_t k = 0; k < v.size(); ++k)
        out += std::format("{}[{},{},{}]", k == 0 ? "" : ",", v[k].first_index,
                           integer_or_null(v[k].first_pts), v[k].frames);
    return out + "]";
}

} // namespace

LadderScores read_ladder_report(std::string_view text, std::string& err)
{
    err.clear();
    json::Error jerr;
    const std::optional<json::Value> doc = json::parse(text, jerr);
    if (!doc) {
        err = "not JSON: " + json::error_text(jerr);
        return {};
    }
    Reader       r;
    LadderScores out;
    if (!r.report(*doc, out)) {
        err = std::move(r.err);
        return {};
    }
    return out;
}

std::string scores_text(const LadderScores& s)
{
    using json::quote;
    std::string out;
    std::size_t frames = 0;
    for (const PairScores& p : s.pairs) frames += p.vmaf.size();
    out.reserve(1024 + 64 * frames);

    out += std::format("{{\"digest\":\"mirage-ladder-scores\",\"version\":{},", kScoresTextVersion);
    out += std::format("\"status\":{},\"reference_mode\":{},", quote(s.status), quote(s.reference_mode));
    // A model file is named by its path, which is left out; its SHA-256 stays.
    out += std::format("\"model\":{},\"model_source\":{},\"model_sha256\":{},\"scaler\":{},",
                       s.model_source == "built-in" ? quote(s.model) : std::string("null"),
                       quote(s.model_source),
                       s.model_sha256.empty() ? std::string("null") : quote(s.model_sha256),
                       quote(s.scaler));
    out += std::format("\"n_threads\":{},\"n_subsample\":{},\"frame_limit\":{},", s.n_threads,
                       s.n_subsample, integer_or_null(s.frame_limit));
    out += std::format("\"libvmaf_version\":{},\"libvmaf_package_version\":{},\"master_sha256\":{},",
                       quote(s.libvmaf_version), quote(s.libvmaf_package_version),
                       quote(s.master_sha256));
    out += "\"entries\":[";
    for (std::size_t k = 0; k < s.entries.size(); ++k) {
        const MasterEntry& e = s.entries[k];
        out += std::format("{}{{\"index\":{},\"line\":{},\"bandwidth\":{},\"average_bandwidth\":{},"
                           "\"resolution\":{},\"codecs\":{},\"frame_rate\":{},\"video\":{}}}",
                           k == 0 ? "" : ",", e.index, e.line, e.bandwidth,
                           quoted_or_null(e.average_bandwidth), quoted_or_null(e.resolution),
                           quoted_or_null(e.codecs), quoted_or_null(e.frame_rate), e.video);
    }
    out += std::format("],\"reference_entries\":{},\"pairs\":[", list_text(s.reference_entries));
    for (std::size_t k = 0; k < s.pairs.size(); ++k) {
        const PairScores& p = s.pairs[k];
        out += std::format("{}{{\"index\":{},\"entries\":{},\"ref_time_base\":{},"
                           "\"dis_time_base\":{},\"ref_rate\":{},\"dis_rate\":{},",
                           k == 0 ? "" : ",", p.index, list_text(p.entries),
                           rational_text(p.ref_time_base), rational_text(p.dis_time_base),
                           rational_text(p.ref_rate), rational_text(p.dis_rate));
        out += std::format("\"ref_sequences\":{},\"dis_sequences\":{},\"context_first\":{},"
                           "\"failures\":[",
                           sequences_text(p.ref_sequences), sequences_text(p.dis_sequences),
                           list_text(p.context_first));
        for (std::size_t j = 0; j < p.failures.size(); ++j) {
            const Failure& f = p.failures[j];
            out += std::format("{}[{},{},{},{}]", j == 0 ? "" : ",", quote(f.input), f.index,
                               integer_or_null(f.pts), quote(f.reason));
        }
        out += "],\"frames\":[";
        for (std::size_t i = 0; i < p.vmaf.size(); ++i) {
            // A pair read by read_ladder_report() has every per-frame list
            // as long as vmaf; one built by hand may not, and a missing value
            // is written as null.
            const auto at = [i](const std::vector<std::int64_t>& v) {
                return i < v.size() ? std::to_string(v[i]) : std::string("null");
            };
            out += std::format("{}[{},{},{},{},{}]", i == 0 ? "" : ",", at(p.ref_index),
                               at(p.dis_index), at(p.ref_pts), at(p.dis_pts),
                               json::number(p.vmaf[i]));
        }
        out += "]}";
    }
    out += "]}\n";
    return out;
}

std::optional<std::int64_t> pts_ticks(double seconds, AVRational time_base)
{
    if (!std::isfinite(seconds) || time_base.num <= 0 || time_base.den <= 0) return std::nullopt;
    const double t = seconds * time_base.den / time_base.num;
    // Within 64 bits: |t| below 2^63, which llround() then rounds without overflow.
    if (!(std::fabs(t) < 0x1p63)) return std::nullopt;
    return std::llround(t);
}

} // namespace timeline
