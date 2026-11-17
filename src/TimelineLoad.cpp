// The timeline panel's loader (eval/phase3-survey/design.md §10): written by WP12, a relative
// report path from the document's directory by WP12b (D-121 (a)), a session's spans that
// aren't scored by WP12c (D-124 (a)).

#include "TimelineLoad.h"

#include "FileHash.h"
#include "TimelineBuild.h"

#include <cerrno>
#include <filesystem>
#include <format>
#include <fstream>
#include <system_error>
#include <utility>

namespace timeline {

namespace {

// The chunk read_file() reads between two looks at its stop token.
constexpr std::size_t kChunk = std::size_t{ 1 } << 20;

LoadResult refused(std::string why)
{
    LoadResult r;
    r.error = std::move(why);
    return r;
}

LoadResult stopped()
{
    LoadResult r;
    r.error   = "the load was stopped";
    r.stopped = true;
    return r;
}

// A session's spans that aren't scored (D-105 (b)), the frames outside its
// curve's data (D-124 (a)), from the document's spans, in play order.
std::vector<Interval> unscored_spans(const Session& session)
{
    std::vector<Interval> out;
    for (const Span& s : session.spans)
        if (!s.scored) out.push_back({ .ref_index = s.ref_index, .ref_end = s.ref_end });
    return out;
}

} // namespace

bool read_file(const std::string& path, std::stop_token stop, std::string& text, std::string& err)
{
    text.clear();
    err.clear();
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        err = "it is a directory";
        return false;
    }
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        // std::generic_category(), not std::strerror(): this runs on a worker.
        err = std::generic_category().message(errno);
        return false;
    }
    std::string chunk(kChunk, '\0');
    while (f) {
        if (stop.stop_requested()) {
            err = "stopped";
            text.clear();
            return false;
        }
        f.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        text.append(chunk.data(), static_cast<std::size_t>(f.gcount()));
    }
    if (f.bad()) {
        err = "a read error";
        text.clear();
        return false;
    }
    return true;
}

LoadResult load_snapshot(const LoadInput& input, std::stop_token stop)
{
    const ReadFile read = input.read ? input.read : ReadFile(&read_file);
    std::string    err;

    // 1. The document (Q05 (a): another schema_version is from_json()'s refusal).
    std::string doc_text;
    if (!read(input.document, stop, doc_text, err)) {
        if (stop.stop_requested()) return stopped();
        return refused(std::format("cannot read the timeline document {}: {}", input.document, err));
    }
    if (stop.stop_requested()) return stopped();
    auto snap = std::make_shared<Snapshot>();
    snap->document_path = input.document;
    if (!from_json(doc_text, snap->timeline, snap->attributions, snap->info, err))
        return refused(std::format("{} isn't a timeline document this reads: {}", input.document, err));
    doc_text = {};
    if (stop.stop_requested()) return stopped();

    // 2. The ladder report it names, unless the caller names another, whose
    // path is read as it is. mirage_vmaf timeline writes ladder_report.path
    // as its --report was given (src/TimelineCli.cpp), so a relative one is
    // read from the current directory where the file is there, else from
    // the document's directory, for a player started elsewhere (D-121 (a),
    // Q86 (c)), as mirage_vmaf attribute reads it without --report
    // (D-124 (g) 4): find_report() (src/TimelineDoc.h), which both programs
    // compile. Where neither directory holds the file, the refusal names
    // both paths. Which of the two holds it is decided on disk, whatever
    // `read` is, so that an injected reader is asked only for one that
    // exists there. The hashes are checked below on whichever file was read.
    snap->report_path = input.report;
    if (input.report.empty()) {
        const std::optional<std::string> found = find_report(input.document, snap->info.report.path, err);
        if (!found) return refused(err);
        snap->report_path = *found;
    }
    std::string report_text;
    if (!read(snap->report_path, stop, report_text, err)) {
        if (stop.stop_requested()) return stopped();
        return refused(std::format("cannot read the ladder report {}: {}", snap->report_path, err));
    }
    if (stop.stop_requested()) return stopped();
    snap->scores = read_ladder_report(report_text, err);
    if (!err.empty())
        return refused(std::format("{} isn't a ladder report the timeline reads: {}", snap->report_path,
                                   err));
    if (stop.stop_requested()) return stopped();

    // 3. By its bytes, or, for a redacted copy, by its scores digest (D-072,
    // Q06 (c); the design's P-Z), as mirage_vmaf attribute checks it.
    const std::string sha256 = filehash::sha256_hex(report_text);
    report_text = {};
    if (sha256 == snap->info.report.sha256) {
        snap->report_check = "its SHA-256 is the one the document names";
    } else {
        const std::string scores_sha256 = filehash::sha256_hex(scores_text(snap->scores));
        if (scores_sha256 != snap->info.report.scores_sha256)
            return refused(std::format(
                "{} isn't the ladder report the document was built from: neither its SHA-256, {}, nor "
                "its scores digest's, {}, is the one the document names, {} and {} (D-072)",
                snap->report_path, sha256, scores_sha256, snap->info.report.sha256,
                snap->info.report.scores_sha256));
        snap->report_redacted = true;
        snap->report_check    = "its bytes aren't the ones the document names, but its scores digest "
                                "is: a redacted copy (D-072)";
    }
    if (stop.stop_requested()) return stopped();

    // 4. The report's curves on the document's timeline.
    if (!attach_curves(snap->timeline, snap->scores, err))
        return refused(std::format("{} and {} don't fit: {}", input.document, snap->report_path, err));
    if (stop.stop_requested()) return stopped();

    // 5. What the panel draws: every pair's curve, then every session's
    // through curve_points() (design §4.4), each with its pyramid, a
    // session's with its spans that aren't scored, which the view takes as
    // outside its data (D-124 (a)).
    const Timeline& t = snap->timeline;
    for (std::size_t k = 0; k < t.curves.size(); ++k) {
        PanelCurve c;
        c.ref      = { .kind = CurveRef::Kind::Pair, .index = static_cast<int>(k) };
        c.points   = t.curves[k];
        c.envelope = build_pyramid(c.points.vmaf);
        snap->curves.push_back(std::move(c));
        if (stop.stop_requested()) return stopped();
    }
    for (std::size_t s = 0; s < t.sessions.size(); ++s) {
        PanelCurve c;
        c.ref      = { .kind = CurveRef::Kind::Session, .index = static_cast<int>(s) };
        c.points   = curve_points(t, c.ref);
        c.envelope = build_pyramid(c.points.vmaf);
        c.unscored = unscored_spans(t.sessions[s]);
        snap->curves.push_back(std::move(c));
        if (stop.stop_requested()) return stopped();
    }
    snap->scale = axis_scale(t.axis);

    LoadResult r;
    r.snapshot = std::move(snap);
    return r;
}

// ── Loader ───────────────────────────────────────────────────────────────────

Loader::~Loader()
{
    for (Worker& w : workers_) w.thread.request_stop();
    workers_.clear();   // each std::jthread joins as it goes
}

void Loader::start(LoadInput input)
{
    reap();
    for (Worker& w : workers_) w.thread.request_stop();
    std::uint64_t generation = 0;
    {
        std::lock_guard lk(shared_->mutex);
        generation = ++shared_->generation;
        shared_->result.reset();
        shared_->running = true;
    }
    Worker w;
    w.done = std::make_shared<std::atomic<bool>>(false);
    // The worker holds the shared state, not the Loader: a stopped worker
    // that ends after a newer start() finds a later generation and hands
    // nothing over.
    w.thread = std::jthread([shared = shared_, generation, done = w.done,
                             in = std::move(input)](std::stop_token stop) {
        LoadResult r = load_snapshot(in, stop);
        {
            std::lock_guard lk(shared->mutex);
            if (shared->generation == generation) {
                shared->running = false;
                if (!r.stopped && !stop.stop_requested()) shared->result = std::move(r);
            }
        }
        done->store(true);
    });
    workers_.push_back(std::move(w));
}

void Loader::cancel()
{
    for (Worker& w : workers_) w.thread.request_stop();
    std::lock_guard lk(shared_->mutex);
    ++shared_->generation;
    shared_->result.reset();
    shared_->running = false;
}

std::optional<LoadResult> Loader::take()
{
    reap();
    std::lock_guard lk(shared_->mutex);
    std::optional<LoadResult> r = std::move(shared_->result);
    shared_->result.reset();
    return r;
}

bool Loader::loading() const
{
    std::lock_guard lk(shared_->mutex);
    return shared_->running;
}

void Loader::reap()
{
    std::erase_if(workers_, [](const Worker& w) { return w.done->load(); });
}

} // namespace timeline
