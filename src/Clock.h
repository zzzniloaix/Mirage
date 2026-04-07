#pragma once

#include <chrono>
#include <atomic>

using SteadyClock = std::chrono::steady_clock;
using TimePoint   = std::chrono::time_point<SteadyClock>;

// Tracks a media clock: a PTS value that extrapolates forward in wall time.
// set() stamps the current PTS + wall time; get() returns the extrapolated PTS.
class Clock {
public:
    void set(double pts)
    {
        pts_      = pts;
        set_time_ = SteadyClock::now();
    }

    double get() const
    {
        if (paused_) return pts_;
        double elapsed = std::chrono::duration<double>(SteadyClock::now() - set_time_).count();
        return pts_ + elapsed;
    }

    void pause(bool p)
    {
        if (paused_ && !p) set_time_ = SteadyClock::now();  // resume: re-anchor wall time
        paused_ = p;
    }

    bool is_paused() const { return paused_; }

private:
    double    pts_      = 0.0;
    TimePoint set_time_ = SteadyClock::now();
    bool      paused_   = false;
};

// Owns audio and video clocks. Video is slaved to audio by default.
class MasterClock {
public:
    void   set_audio(double pts) { audio_.set(pts); }
    void   set_video(double pts) { video_.set(pts); }
    double get()          const  { return audio_.get(); }  // master = audio clock
    Clock& audio_clock()         { return audio_; }
    Clock& video_clock()         { return video_; }
    void   pause(bool p)         { audio_.pause(p); video_.pause(p); }

private:
    Clock audio_;
    Clock video_;
};
