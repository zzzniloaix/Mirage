#pragma once

// Reads what DashConform.h compares, for one MPD:
//  * the Segment Index (sidx at @indexRange) of every SegmentBase
//    Representation, which lists its subsegments;
//  * the size of every segment: the Segment Index's or @mediaRange's where
//    there is one, else a request for the segment's first byte (as
//    ConformProbe sizes HLS segments);
//  * some segments of each Representation read in full, its init segment
//    in front (conformprobe::read_segment), with the earliest presentation
//    time of their moof boxes (hls::parse_segment_start).
// A dynamic MPD is read at the clock of the check. Encrypted
// Representations (ContentProtection) are sized and their init segments
// read, not their segments; WebM and text Representations are only listed.
// With a NetworkLogger installed every request is logged as the
// inspector's. A check runs on its own thread with a few workers; stop()
// aborts the requests in flight.

#include "DashConform.h"
#include "HlsConformProbe.h"

class DashProbe {
public:
    // segments: read per video Representation, audio_segments per audio one.
    using Options = ConformProbe::Options;
    using State   = ConformProbe::State;

    struct Progress {
        int indexes = 0, indexes_total = 0;   // Segment Indexes and init segments
        int sizes = 0, sizes_total = 0;       // size requests
        int reads = 0, reads_total = 0;       // segments read in full
        std::int64_t bytes = 0;               // body bytes received
    };

    struct Result {
        std::string                source;    // the MPD's URL as given
        dash::Mpd                  mpd;
        std::vector<dash::RepRead> reads;     // parallel to mpd.reps
        dash::Report               report;    // once Done (Stopped: over what was read)
        std::string                error;     // Failed: why
        double                     seconds = 0.0;
    };

    DashProbe() = default;
    ~DashProbe();   // stop()
    DashProbe(const DashProbe&)            = delete;
    DashProbe& operator=(const DashProbe&) = delete;

    // Fetch the MPD at `url` (HTTP(S) or a local path), then check it.
    void start(const std::string& url, const Options& o);
    // Check an MPD already fetched: `text`, served from `final_url`.
    void start(const std::string& source, const std::string& text, const std::string& final_url, const Options& o);
    void stop();
    void clear();

    State    state() const { return state_.load(); }
    bool     running() const { return state_.load() == State::Running; }
    Progress progress() const;
    std::uint64_t version() const { return version_.load(); }
    Result   result() const;

private:
    struct Counters {
        std::atomic<int> indexes{ 0 }, indexes_total{ 0 }, sizes{ 0 }, sizes_total{ 0 }, reads{ 0 }, reads_total{ 0 };
        std::atomic<std::int64_t> bytes{ 0 };
    };

    void launch(std::string source, std::string text, std::string final_url, Options o);
    void check(const std::stop_token& st, const std::string& source, std::string text, std::string final_url,
               const Options& o);
    void finish(State s, std::string error = {});

    std::atomic<State>         state_{ State::Idle };
    std::atomic<std::uint64_t> version_{ 0 };
    mutable std::mutex         mtx_;
    Result                     result_;
    Counters                   count_;
    std::chrono::steady_clock::time_point t0_;
    std::jthread               thread_;
};
