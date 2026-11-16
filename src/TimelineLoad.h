#pragma once

// The timeline panel's loader, ImGui-free: a timeline document and its ladder report, checked
// by their hashes (eval/phase3-survey/design.md §10; DECISIONS.md D-114 (a)): written by WP12,
// a relative report path from the document's directory by WP12b (D-121 (a)).
//
// What the panel (src/TimelinePanel.h, WP13) draws comes from here, as design
// §10 plans it: a worker thread reads the timeline document (src/TimelineDoc.h)
// and the ladder report it names (src/LadderReportRead.h), checks the report
// by its two hashes, builds the core's Timeline with the report's curves, and
// hands the main thread one immutable snapshot, once, under a mutex, as
// ThumbnailStrip hands over its thumbnails (eval/phase3-survey/ui.md §2.2); a
// new load stops the one before it through its stop token, and nothing of
// the stopped one is ever handed over. The steps, each a refusal the panel
// can show where it fails, in the words mirage_vmaf attribute uses for the
// same check (src/TimelineCli.cpp, run_attribute()):
//
// 1. The document, read and parsed by from_json(), which refuses another
//    schema or schema_version (Q05 (a)) and anything else it can't read,
//    naming the key.
// 2. The ladder report: at the path the caller gives, read as it is, or at
//    the path the document's ladder_report.path gives. mirage_vmaf timeline
//    writes that path as its --report was given, so a relative one is read
//    from the current directory where the file is there, as mirage_vmaf
//    attribute reads it without --report (D-081 (b)), else from the
//    document's directory, where a player started elsewhere finds the
//    report beside its document (D-121 (a), Q86 (c)); an absolute one, and
//    an empty one, which names no file, are read as they are. Where neither
//    directory holds the file, the refusal names both paths; where the
//    document's directory is the current one, lexically normalized, the one
//    path is tried. A directory holds the file where something is at its
//    path there on disk (std::filesystem::exists()), whatever
//    LoadInput::read reads files with, so that a file in the current
//    directory that can't be read, or a directory of the file's name, is
//    refused there, as mirage_vmaf attribute refuses it: a reading pending
//    the owner, whose alternatives are a regular file alone holding it, and
//    the first of the two paths that can be read. The report is read by
//    read_ladder_report(), which refuses a layout or a report the timeline
//    doesn't read (D-081 (b)).
// 3. The hashes (D-072, Q06 (c)), of whichever file step 2 read: the report
//    is the document's when the SHA-256 of its bytes is the one the document
//    names; otherwise when the SHA-256 of its scores digest (scores_text())
//    is, a copy redacted as the eval scripts redact one, which the snapshot
//    says (the design's P-Z); otherwise it is refused, both hashes named.
// 4. The report's curves put on the document's timeline (attach_curves(),
//    src/TimelineBuild.h), which refuses a report whose complete pairs and
//    the document's variants don't name each other one for one.
// 5. The curves the panel draws: every pair's, in the report's order, then
//    every session's, derived from its pairs' by curve_points() (WP07's;
//    design §4.4), each with its min/max pyramid (src/TimelineView.h), and
//    the axis's scale for x.
//
// The stop token is checked between the steps and between a file's chunks,
// so a stopped load ends at the next of them; a parse already running isn't
// interrupted. How long the largest committed reports take to load, and the
// memory they need, this package didn't measure (the design's P-Y): MEASUREMENT
// PENDING.
//
// It calls only what the player compiles (CMakeLists.txt's mirage target):
// the core, FileHash; nothing of ImGui, GL or GLFW (D-031). Unit-tested in
// tests/test_timeline_load.cpp.

#include "LadderReportRead.h"
#include "TimelineDoc.h"
#include "TimelineModel.h"
#include "TimelineQuery.h"
#include "TimelineView.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace timeline {

// One curve the panel draws: a pair's, as the report gives it, or a
// session's, derived from its pairs' over its spans (curve_points(); design
// §4.4), with the min/max pyramid of its scores.
struct PanelCurve {
    CurveRef ref;
    Curve    points;
    Pyramid  envelope;
};

// What a load hands the main thread once; the panel only reads it.
struct Snapshot {
    std::string document_path;    // as given
    std::string report_path;      // the report read: the document's ladder_report.path, from the
                                  // document's directory where step 2 read it there, or the
                                  // path the load was given
    // The report was taken by its scores digest, its bytes not being the ones
    // the document names: a redacted copy (D-072; the design's P-Z).
    bool        report_redacted = false;
    std::string report_check;     // the check's outcome as a line the panel shows
    Timeline                 timeline;       // the document's, the report's curves attached
    std::vector<Attribution> attributions;   // the document's, its detected drops among them
    DocInfo                  info;           // the rest of the document: options, detection, margins
    // The report as read: the master's entries (MasterEntry), which the match
    // takes (src/TimelineMatch.h), and each complete pair's frames with their
    // PTS, which the playhead and the seek decision take.
    LadderScores             scores;
    std::vector<PanelCurve>  curves;         // every pair's, in the report's order, then every session's
    AxisScale                scale;          // the document's axis, for x (src/TimelineView.h)
};

// How the loader reads a file: its bytes into `text`, true; or false with
// `err` saying why, in a phrase without the path. A read stopped through
// `stop` may return false; the loader then reports the load as stopped.
// Where step 2 has two paths for a relative report path, the current
// directory's and the document's directory's, the one read is decided on
// disk by std::filesystem::exists(), whatever reads the files: a ReadFile
// is then asked for the report only at a path that exists on disk, and not
// at all where neither does.
using ReadFile = std::function<bool(const std::string& path, std::stop_token stop, std::string& text,
                                    std::string& err)>;

// A file from disk, as ReadFile: read in chunks, `stop` checked between them.
bool read_file(const std::string& path, std::stop_token stop, std::string& text, std::string& err);

// What to load.
struct LoadInput {
    std::string document;   // the timeline document's path, --timeline's (D-114 (d))
    // The ladder report's path, read as it is; empty: the one the document
    // names, read as step 2 says (D-121 (a)). The panel's only setting is the
    // document (D-114 (d)); this is for the caller that knows better, the
    // tests among them.
    std::string report;
    // How files are read; empty: read_file(). Which of step 2's two paths
    // for a relative report path is read is decided on disk all the same,
    // so that this is asked only for a path that exists there (ReadFile).
    ReadFile    read;
};

// What a load gives: a snapshot, or why there is none.
struct LoadResult {
    std::shared_ptr<const Snapshot> snapshot;   // nullptr when refused or stopped
    std::string error;                          // the refusal, a message the panel shows; empty with
                                                // a snapshot
    bool stopped = false;                       // stopped through the stop token
};

// The load of `input` on the calling thread, steps 1 to 5 above; stopped
// through `stop` between them.
LoadResult load_snapshot(const LoadInput& input, std::stop_token stop = {});

// The panel's loader: one load at a time, each on a worker thread of its own
// (std::jthread), its result handed over once. Its member functions are the
// main thread's; the workers touch only the shared result, under its mutex.
// Destroying it stops every worker and joins it.
class Loader {
public:
    Loader() = default;
    ~Loader();
    Loader(const Loader&) = delete;
    Loader& operator=(const Loader&) = delete;

    // Starts loading `input`. A load still running is stopped through its
    // stop token, and neither its result nor one not taken yet is handed
    // over: take() gives this load's alone.
    void start(LoadInput input);
    // Stops the load running, if any, and drops a result not taken: take()
    // gives nothing until the next start().
    void cancel();
    // The result of the last load started, once: its snapshot, or its
    // refusal. nullopt while it runs, once it has been taken, after cancel(),
    // and before any start().
    std::optional<LoadResult> take();
    // Whether the last load started is still running.
    bool loading() const;

private:
    struct Shared {
        mutable std::mutex        mutex;
        std::uint64_t             generation = 0;   // of the last start() or cancel()
        bool                      running = false;  // the last start()'s worker hasn't finished
        std::optional<LoadResult> result;           // its result, until taken
    };
    struct Worker {
        std::jthread                       thread;
        std::shared_ptr<std::atomic<bool>> done;
    };
    // Joins the workers that have finished, the stopped ones among them.
    void reap();

    std::shared_ptr<Shared> shared_ = std::make_shared<Shared>();
    std::vector<Worker>     workers_;
};

} // namespace timeline
