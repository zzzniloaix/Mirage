#pragma once

// Frame-size / GOP analyzer (B): a bar per frame in display order, coloured
// by picture type, around the frame on screen — keyframes (GOP starts)
// marked, the rolling one-second bitrate over it, an overview of the whole
// stream below; click a bar (or the overview) to seek there. The frames come
// from FrameScan, a read of the stream of its own (nothing is decoded).
// Draw between PlayerUI's begin_frame() and end_frame().

#include "FrameScan.h"

#include <cstdint>
#include <future>
#include <optional>
#include <string>
#include <vector>

// What the panel shows, kept on the main thread: the scan's records copied
// as they come, and in display order with their bitrate and summary. The
// display-order view is rebuilt on a task of its own (sorting a long
// stream's frames takes longer than a frame of the render loop).
class FramesModel {
public:
    ~FramesModel();   // waits for a rebuild in flight

    void start(const std::string& url, int stream, const std::string& master = {});
    // Ends a running scan: the frames read so far stay (the card closed —
    // a network scan must not go on downloading).
    void stop() { scan_.stop(); }

    // Copy what the scan added; take a finished rebuild; start the next one
    // when the records changed (at most every `rebuild_s` while the scan
    // runs, at once when it ends). `now_s`: a steady clock in seconds.
    void update(double now_s, double rebuild_s = 0.5);

    [[nodiscard]] bool started() const { return started_; }
    [[nodiscard]] int  stream()  const { return stream_; }
    [[nodiscard]] FrameScan::State state() const { return scan_.state(); }
    [[nodiscard]] double      progress() const { return scan_.progress(); }
    [[nodiscard]] std::string error()    const { return scan_.error(); }
    [[nodiscard]] const std::optional<FrameScan::StreamInfo>& info() const { return info_; }

    // Display order, without the frames never shown (cut by an edit list).
    [[nodiscard]] const std::vector<framescan::Frame>& frames()  const { return view_.shown; }
    [[nodiscard]] const std::vector<double>&           bitrate() const { return view_.bitrate; }   // 1 s window
    // Per frame, a time that never decreases: its pts, else the last one
    // carried forward a frame at a time (frames without a pts — raw
    // elementary streams have none — keep their read position), so the
    // frame on screen can be looked up by time.
    [[nodiscard]] const std::vector<double>&           times()   const { return view_.times; }
    [[nodiscard]] const framescan::GopSummary&         summary() const { return view_.summary; }

    struct View {
        std::vector<framescan::Frame> shown;
        std::vector<double>           bitrate;
        std::vector<double>           times;
        framescan::GopSummary         summary;
        std::size_t                   count = 0;   // records it was built from
        bool timed_by_rate = false;   // some frames had no pts: times (and bitrates) from the frame rate
    };
    [[nodiscard]] bool timed_by_rate() const { return view_.timed_by_rate; }

private:
    FrameScan                              scan_;
    std::vector<framescan::Frame>          read_;      // read order, as copied
    View                                   view_;
    std::future<View>                      pending_;   // a rebuild in flight
    std::optional<FrameScan::StreamInfo>   info_;
    std::uint64_t seen_version_ = 0;
    double        built_at_     = -1.0;
    bool          started_      = false;
    int           stream_       = -1;
};

struct FramesPanelInputs {
    const FramesModel* model = nullptr;
    double cur_pts = 0.0;        // the frame on screen
    bool   network = false;      // a network input: a scan downloads all of it
    float  win_w = 0.0f, win_h = 0.0f;   // logical display size
    float  bottom_inset = 0.0f;  // logical px of the controls and strips
    float  right_inset  = 0.0f;  // logical px of the network sidebar
};

struct FramesPanelOut {
    bool   close      = false;
    bool   start_scan = false;   // network input: the user asked for the scan
    double seek_to    = -1.0;    // >= 0: a frame was clicked
    float  height     = 0.0f;    // logical px the card takes above the controls
};

class FramesPanel {
public:
    FramesPanelOut draw(const FramesPanelInputs& in);

private:
    int span_ = 240;   // frames in view; the mouse wheel zooms
    // The overview's columns (the highest bitrate in each), for over_n_ frames.
    std::vector<double> over_;
    int                 over_n_   = -1;
    double              over_max_ = 1.0;
};
