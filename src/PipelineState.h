#pragma once

#include <atomic>
#include <cstdint>
#include <string>

// What each stage of a media session's pipeline is doing — for the startup
// watchdog (main.cpp): a session that has put no frame on screen ~10 s after
// it started logs this once, so a stall can be diagnosed from the log alone.
// The demux / decode threads publish their state in relaxed atomics; the
// render thread reads them.

enum class DemuxState : int {
    Starting,      // read_loop not entered yet
    Reading,       // in av_read_frame, or handling a seek / track switch
    PushVideo,     // waiting for room in the video packet queue
    PushAudio,     // waiting for room in the audio packet queue
    PushSubtitle,  // waiting for room in the subtitle packet queue
    Retrying,      // backing off after a failed network read
    AtEnd,         // end of media: idle until a seek / switch
    Stopped,       // read_loop returned (stop request or fatal error)
};

enum class DecodeState : int {
    Starting,      // thread not running yet
    WaitPacket,    // waiting for a packet from its queue
    Decoding,      // in the codec (send / receive / flush / reopen)
    WaitOutput,    // video: frame queue full; audio: device ring full
    Stopped,
};

const char* to_string(DemuxState s);
const char* to_string(DecodeState s);

// Published by a decode thread, read by the render thread.
struct StageStats {
    std::atomic<int>          state{ static_cast<int>(DecodeState::Starting) };
    std::atomic<std::int64_t> packets{ 0 };   // packets taken from the queue (sentinels excluded)
    std::atomic<std::int64_t> frames{ 0 };    // frames the codec produced

    void        set(DecodeState s) { state.store(static_cast<int>(s), std::memory_order_relaxed); }
    DecodeState get() const        { return static_cast<DecodeState>(state.load(std::memory_order_relaxed)); }
};

// Everything the watchdog reports, copied out at one instant.
struct PipelineSnapshot {
    double since_start_s = 0.0;   // wall seconds since the session started (open finished)

    // Render thread / user.
    bool paused    = false;
    bool scrubbing = false;
    bool focused   = true;        // the window has keyboard focus (keys reach it)

    // Queues: items / capacity.
    int videoq = 0, videoq_cap = 0;
    int audioq = 0, audioq_cap = 0;
    int subq   = 0;
    int frameq = 0, frameq_cap = 0;

    // Demux thread.
    DemuxState   demux = DemuxState::Starting;
    std::int64_t video_pkts = 0, audio_pkts = 0, sub_pkts = 0;   // routed to the queues
    bool         demux_ended = false;
    std::string  demux_error;

    // Video decode thread.
    DecodeState  video = DecodeState::Starting;
    std::int64_t video_in = 0, video_out = 0;   // packets taken / frames decoded
    bool         hw = false;                    // VideoToolbox frames seen

    // Audio decode thread + device.
    bool         has_audio = false;
    DecodeState  audio = DecodeState::Starting;
    std::int64_t audio_in = 0;
    bool         audio_device = false;          // output device opened and started
    std::int64_t audio_callbacks = 0;           // device callbacks so far
    double       audio_buffered_s = 0.0;        // audio waiting in the device ring
    double       audio_clock = 0.0;
};

// One line of "name=value" fields, e.g. "t=10.0s paused=1 scrubbing=0
// focused=1 | demux=push-video pkts v=90 a=55 s=0 ended=0 | videoq=64/64
// audioq=30/128 subq=0 frameq=8/8 | video=wait-output in=26 out=9 hw=1 | audio=...".
std::string describe_pipeline(const PipelineSnapshot& s);

// The most likely reason nothing is on screen, in words.
std::string diagnose_no_frame(const PipelineSnapshot& s);

// Fires once when a session has shown no frame `timeout_s` after it started.
// Times are seconds on any monotonic clock.
class StartupWatchdog {
public:
    explicit StartupWatchdog(double timeout_s = 10.0) : timeout_(timeout_s) {}

    void start(double t) { t0_ = t; armed_ = true; }
    void on_frame()      { armed_ = false; }
    // True exactly once: `timeout` passed since start() with no on_frame().
    bool due(double t)
    {
        if (!armed_ || t - t0_ < timeout_) return false;
        armed_ = false;
        return true;
    }
    double timeout() const { return timeout_; }

private:
    double timeout_;
    double t0_    = 0.0;
    bool   armed_ = false;
};
