#include "DashConformProbe.h"

#include "Logger.h"
#include "NetworkLogger.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>

using conformprobe::Fetched;
using conformprobe::fetch;
using conformprobe::parallel_for;

namespace {

const std::uint8_t* bytes_of(const std::string& s) { return reinterpret_cast<const std::uint8_t*>(s.data()); }

} // namespace

DashProbe::~DashProbe() { stop(); }

void DashProbe::start(const std::string& url, const Options& o)
{
    launch(url, {}, {}, o);
}

void DashProbe::start(const std::string& source, const std::string& text, const std::string& final_url, const Options& o)
{
    launch(source, text, final_url, o);
}

void DashProbe::stop()
{
    if (thread_.joinable()) {
        thread_.request_stop();
        thread_.join();
    }
}

void DashProbe::clear()
{
    stop();
    {
        std::lock_guard lk(mtx_);
        result_ = Result{};
    }
    state_.store(State::Idle);
    ++version_;
}

DashProbe::Progress DashProbe::progress() const
{
    Progress p;
    p.indexes = count_.indexes.load();
    p.indexes_total = count_.indexes_total.load();
    p.sizes = count_.sizes.load();
    p.sizes_total = count_.sizes_total.load();
    p.reads = count_.reads.load();
    p.reads_total = count_.reads_total.load();
    p.bytes = count_.bytes.load();
    return p;
}

DashProbe::Result DashProbe::result() const
{
    std::lock_guard lk(mtx_);
    return result_;
}

void DashProbe::launch(std::string source, std::string text, std::string final_url, Options o)
{
    stop();
    {
        std::lock_guard lk(mtx_);
        result_ = Result{};
        result_.source = source;
    }
    for (auto* a : { &count_.indexes, &count_.indexes_total, &count_.sizes, &count_.sizes_total, &count_.reads,
                     &count_.reads_total })
        a->store(0);
    count_.bytes.store(0);
    t0_ = std::chrono::steady_clock::now();
    state_.store(State::Running);
    ++version_;
    thread_ = std::jthread([this, source = std::move(source), text = std::move(text), final_url = std::move(final_url),
                            o](std::stop_token st) mutable {
        check(st, source, std::move(text), std::move(final_url), o);
    });
}

void DashProbe::finish(State s, std::string error)
{
    {
        std::lock_guard lk(mtx_);
        result_.error = std::move(error);
        result_.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count();
    }
    state_.store(s);
    ++version_;
}

void DashProbe::check(const std::stop_token& st, const std::string& source, std::string text, std::string final_url,
                      const Options& o)
{
    std::atomic<std::int64_t>& received = count_.bytes;
    if (text.empty()) {
        Fetched f = fetch(source, -1, -1, static_cast<std::int64_t>(NetworkLogger::kMaxTextBody), st, received);
        if (st.stop_requested()) return finish(State::Stopped);
        if (!f.ok()) return finish(State::Failed, "cannot fetch the MPD: " + f.error);
        text = std::move(f.body);
        final_url = std::move(f.final_url);
    }
    const double now = std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    dash::Mpd m = dash::parse_mpd(text, final_url.empty() ? source : final_url, now);
    {
        std::lock_guard lk(mtx_);
        result_.mpd = m;
    }
    ++version_;
    if (!m.ok) {
        std::string why;
        for (const auto& p : m.problems) why += (why.empty() ? "" : "; ") + p;
        return finish(State::Failed, why.empty() ? "not an MPD" : why);
    }

    // ── What is not read ─────────────────────────────────────────────────────
    std::vector<dash::RepRead> rr(m.reps.size());
    std::vector<bool> listed_only(m.reps.size(), false);
    for (std::size_t i = 0; i < m.reps.size(); ++i) {
        const dash::Representation& r = m.reps[i];
        if (r.kind == hls::Track::Text) {
            rr[i].skipped = "text";
            listed_only[i] = true;
        } else if (r.mime_type.find("webm") != std::string::npos) {
            rr[i].skipped = "WebM (its Cues and Clusters are not read here)";
            listed_only[i] = true;
        } else if (!r.protection.empty()) {
            std::set<std::string> schemes;
            for (const auto& d : r.protection) schemes.insert(d.scheme);
            std::string list;
            for (const auto& s : schemes) list += (list.empty() ? "" : ", ") + s;
            rr[i].skipped = std::format("encrypted ({}): their contents are not decoded, the init segment's sample "
                                        "entries are", list);
        }
    }

    // ── Segment Indexes and init segments ────────────────────────────────────
    struct Init {
        std::string    uri;
        hls::ByteRange range;
        Fetched        f;
        hls::InitInfo  info;
    };
    std::vector<Init> inits;
    std::map<std::pair<std::string, std::int64_t>, std::size_t> init_index;
    std::vector<int> init_of(m.reps.size(), -1);
    std::vector<std::size_t> indexed;
    for (std::size_t i = 0; i < m.reps.size(); ++i) {
        const dash::Representation& r = m.reps[i];
        if (listed_only[i]) continue;
        if (r.addressing == dash::Addressing::Base && !r.index_uri.empty() && r.segments.empty()) indexed.push_back(i);
        if (r.init_uri.empty()) continue;
        const auto key = std::make_pair(r.init_uri, r.init_range.offset);
        auto it = init_index.find(key);
        if (it == init_index.end()) {
            it = init_index.emplace(key, inits.size()).first;
            inits.push_back({ r.init_uri, r.init_range, {}, {} });
        }
        init_of[i] = static_cast<int>(it->second);
    }
    count_.indexes_total = static_cast<int>(indexed.size() + inits.size());
    parallel_for(indexed.size(), o.workers, st, [&](std::size_t k) {
        const std::size_t i = indexed[k];
        dash::Representation& r = m.reps[i];
        const Fetched f = fetch(r.index_uri, r.index_range.offset, r.index_range.length, -1, st, received);
        if (!f.ok()) {
            rr[i].error = "its Segment Index could not be fetched: " + f.error;
        } else {
            const dash::Sidx s = dash::parse_sidx(bytes_of(f.body), f.body.size(), std::max<std::int64_t>(r.index_range.offset, 0));
            if (!s.ok) rr[i].error = "its Segment Index: " + s.error;
            else dash::apply_sidx(r, s, m.periods[static_cast<std::size_t>(r.period)].start);
        }
        ++count_.indexes;
    });
    parallel_for(inits.size(), o.workers, st, [&](std::size_t k) {
        Init& in = inits[k];
        in.f = fetch(in.uri, in.range.offset, in.range.length, -1, st, received);
        if (in.f.ok()) in.info = hls::parse_init_segment(bytes_of(in.f.body), in.f.body.size());
        ++count_.indexes;
    });
    {
        std::lock_guard lk(mtx_);
        result_.mpd = m;   // SegmentBase subsegments listed now
    }
    ++version_;

    // ── What to request ──────────────────────────────────────────────────────
    struct Job { std::size_t rep; int seg; bool read; };
    std::vector<Job> jobs;
    for (std::size_t i = 0; i < m.reps.size(); ++i) {
        const dash::Representation& r = m.reps[i];
        if (listed_only[i]) continue;
        auto& sizes = rr[i].sizes;
        sizes.assign(r.segments.size(), -1);
        for (std::size_t s = 0; s < r.segments.size(); ++s)
            if (!r.segments[s].range.whole()) sizes[s] = r.segments[s].range.length;
        std::set<int> picked;
        if (r.protection.empty())
            for (const int s : conformprobe::pick_segments(static_cast<int>(r.segments.size()),
                                                           r.kind == hls::Track::Audio ? o.audio_segments : o.segments)) {
                picked.insert(s);
                jobs.push_back({ i, s, true });
            }
        if (o.sizes)
            for (std::size_t s = 0; s < r.segments.size(); ++s)
                if (sizes[s] < 0 && !picked.contains(static_cast<int>(s))) jobs.push_back({ i, static_cast<int>(s), false });
    }
    int nreads = 0;
    for (const auto& j : jobs) nreads += j.read ? 1 : 0;
    count_.reads_total = nreads;
    count_.sizes_total = static_cast<int>(jobs.size()) - nreads;

    std::mutex reads_mtx;
    parallel_for(jobs.size(), o.workers, st, [&](std::size_t k) {
        const Job& j = jobs[k];
        const dash::Representation& r = m.reps[j.rep];
        const dash::SegmentRef& s = r.segments[static_cast<std::size_t>(j.seg)];
        std::int64_t& size = rr[j.rep].sizes[static_cast<std::size_t>(j.seg)];
        if (!j.read) {   // its size only: the first byte
            const Fetched f = fetch(s.uri, 0, 1, 1, st, received);
            if (f.ok() && f.total > 0) size = f.total;
            ++count_.sizes;
            return;
        }
        const Fetched f = fetch(s.uri, s.range.offset, s.range.length, -1, st, received);
        dash::SegmentSeen seen;
        seen.read.index = j.seg;
        if (!f.ok()) {
            seen.read.error = f.error;
        } else {
            const auto got = static_cast<std::int64_t>(f.body.size());
            size = s.range.whole() ? (f.total > 0 ? f.total : got) : s.range.length;
            std::string data;
            const Init* in = init_of[j.rep] >= 0 ? &inits[static_cast<std::size_t>(init_of[j.rep])] : nullptr;
            if (in && !in->f.ok()) {
                seen.read.bytes = got;
                seen.read.error = "its init segment could not be fetched: " + in->f.error;
            } else {
                if (in) data = in->f.body;
                data += f.body;
                seen.read = conformprobe::read_segment(bytes_of(data), data.size(), j.seg, size);
                const hls::SegmentStart start = hls::parse_segment_start(bytes_of(f.body), f.body.size(), in ? &in->info : nullptr);
                if (const hls::SegmentStart::T* t = start.first(r.kind)) {
                    seen.start = std::isfinite(t->iso_start) ? t->iso_start : t->start;
                    seen.edit = t->edit;
                }
            }
        }
        {
            std::lock_guard lk(reads_mtx);
            rr[j.rep].reads.push_back(std::move(seen));
        }
        ++count_.reads;
    });
    // Encrypted: what the init segment alone says.
    for (std::size_t i = 0; i < m.reps.size(); ++i)
        if (!listed_only[i] && !m.reps[i].protection.empty() && init_of[i] >= 0) {
            const Init& in = inits[static_cast<std::size_t>(init_of[i])];
            if (in.f.ok()) rr[i].init = conformprobe::read_segment(bytes_of(in.f.body), in.f.body.size(), -1, -1);
        }
    for (auto& r : rr)
        std::sort(r.reads.begin(), r.reads.end(),
                  [](const dash::SegmentSeen& a, const dash::SegmentSeen& b) { return a.read.index < b.read.index; });

    dash::Report rep = dash::analyze_conformance(m, rr);
    const bool stopped = st.stop_requested();
    logger::info("DashProbe: {} — {} Representations, {} segments read, {} sized, {:.1f} MB in {:.1f} s: {} errors, "
                 "{} warnings{}", source, m.reps.size(), count_.reads.load(), count_.sizes.load(),
                 static_cast<double>(received.load()) / 1e6,
                 std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count(), rep.errors, rep.warnings,
                 stopped ? " (stopped)" : "");
    {
        std::lock_guard lk(mtx_);
        result_.mpd = std::move(m);
        result_.reads = std::move(rr);
        result_.report = std::move(rep);
    }
    finish(stopped ? State::Stopped : State::Done);
}
