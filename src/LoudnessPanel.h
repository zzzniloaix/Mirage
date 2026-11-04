#pragma once

// Loudness card (L): BS.1770 / EBU R128 loudness of what is heard —
// momentary, short-term and integrated loudness against a delivery target,
// loudness range, maximum true peak, a minute of short-term history, peak /
// RMS meters per channel labelled from the layout, and the phase correlation
// of the first two channels. Draw between PlayerUI's begin_frame() and
// end_frame().

#include "LoudnessTap.h"

#include <vector>

struct LoudnessInputs {
    const loudness::Block*              block   = nullptr;   // heard now; null = none yet
    const std::vector<loudness::Block>* history = nullptr;   // the last minute, oldest first
    const LoudnessTap::Layout*          layout  = nullptr;
    double now_pts   = 0.0;     // the audio clock
    bool   has_audio = true;
    float  win_w = 0.0f, win_h = 0.0f;                        // logical display size
    float  top_inset = 0.0f, bottom_inset = 0.0f, right_inset = 0.0f;
};

struct LoudnessOut {
    bool close = false;
    bool reset = false;   // restart integrated loudness, LRA and the true-peak maximum
};

class LoudnessPanel {
public:
    LoudnessOut draw(const LoudnessInputs& in);

private:
    int  target_ = 0;      // index into the delivery targets
    bool moved_  = false;  // dragged by the user: no longer docked
};
