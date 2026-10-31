#pragma once

// Reads what HlsConform.h compares, for one HLS master playlist: every media
// playlist it lists (variants, then audio renditions), the size of every
// segment, and some segments of each read in full.
//  * Sizes: the EXT-X-BYTERANGE length where the playlist gives one, else a
//    request for the segment's first byte only — the server's Content-Range
//    ("bytes 0-0/<size>"), or the Content-Length of a server that ignores
//    the range, gives the size without the body.
//  * Segments read: the first, the last and evenly spaced ones between
//    (Options::segments of them), each with its EXT-X-MAP init segment in
//    front, demuxed from memory: the decoder configuration (the codec
//    string), the first picture's NAL unit types and keyframe flag, every
//    video packet's timestamps; the first video frame and a few audio frames
//    decoded for the decoded size, transfer characteristics, channels and
//    AAC object type (implicit SBR / PS shows only once decoded).
// Encrypted segments (EXT-X-KEY other than NONE) are sized but not read.
// With a NetworkLogger installed, every request is logged as the
// inspector's (NetInitiator::Inspector). A check runs on its own thread
// with a few workers; stop() aborts the requests in flight.

#include "HlsConform.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

class ConformProbe {
public:
    struct Options {
        int  segments = 3;          // read per variant playlist; <= 0 = every segment
        int  audio_segments = 1;    // read per audio rendition playlist; <= 0 = every segment
        bool sizes = true;          // size every segment (BANDWIDTH needs the peak); false = the ones read
        int  workers = 4;           // requests at a time
    };
    enum class State : std::uint8_t { Idle, Running, Done, Failed, Stopped };

    struct Progress {
        int playlists = 0, playlists_total = 0;
        int sizes = 0, sizes_total = 0;     // size requests
        int reads = 0, reads_total = 0;     // segments read in full
        std::int64_t bytes = 0;             // body bytes received
    };

    struct Result {
        std::string                    source;      // the master's URL as given
        hls::MasterDecl                master;
        std::vector<hls::PlaylistRead> playlists;   // in MasterDecl::playlists() order
        hls::ConformReport             report;      // once Done (Stopped: over what was read)
        std::string                    error;       // Failed: why
        double                         seconds = 0.0;
    };

    ConformProbe() = default;
    ~ConformProbe();   // stop()
    ConformProbe(const ConformProbe&)            = delete;
    ConformProbe& operator=(const ConformProbe&) = delete;

    // Fetch the master at `url` (HTTP(S) or a local path), then check it.
    void start(const std::string& url, const Options& o);
    // Check a master already fetched: `text`, served from `final_url` (after
    // redirects; relative URIs resolve against it). `source`: as given.
    void start(const std::string& source, const std::string& text, const std::string& final_url, const Options& o);
    // Ends the check and joins its thread; the result keeps what was read.
    void stop();
    // stop(), then forget the result: Idle again (new media).
    void clear();

    State    state() const { return state_.load(); }
    bool     running() const { return state_.load() == State::Running; }
    Progress progress() const;
    // Bumped with every change of state() and of the result.
    std::uint64_t version() const { return version_.load(); }
    Result   result() const;

private:
    struct Counters {
        std::atomic<int> playlists{ 0 }, playlists_total{ 0 }, sizes{ 0 }, sizes_total{ 0 }, reads{ 0 },
                         reads_total{ 0 };
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

// The FFmpeg-facing parts, exposed for the tests.
namespace conformprobe {

// What one segment's bytes hold (`data`: its init segment, if any, then
// the segment), demuxed from memory. `bytes` is the segment's own size.
hls::SegmentRead read_segment(const std::uint8_t* data, std::size_t n, int index, std::int64_t bytes);

// The segment indices read out of `n`: the first, the last and evenly
// spaced ones between, `k` in all (<= 0 or >= n: every one).
std::vector<int> pick_segments(int n, int k);

} // namespace conformprobe
