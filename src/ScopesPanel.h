#pragma once

// Scopes card (S) and pixel probe (X), drawn with Dear ImGui over the video.
// Call between PlayerUI's begin_frame() and end_frame(), with the GL context
// current (the scope image is a texture).

#include "PixelView.h"
#include "Scopes.h"

#include "imgui.h"

#include <cstdint>

struct ScopesInputs {
    const scopes::Result* result = nullptr;   // latest measure; null = none yet
    scopes::Mode mode = scopes::Mode::Waveform;   // selected (the result may still be of the last)
    bool  measuring = false;                  // a newer measure is on its way
    AVColorSpace                  cs    = AVCOL_SPC_BT709;    // vectorscope targets
    AVColorRange                  range = AVCOL_RANGE_MPEG;
    AVColorTransferCharacteristic trc   = AVCOL_TRC_BT709;    // PQ: a nits scale
    float win_w = 0.0f, win_h = 0.0f;         // logical display size
    float top_inset = 0.0f, bottom_inset = 0.0f, right_inset = 0.0f;
};

struct ScopesOut {
    bool close = false;
    int  mode  = -1;   // >= 0: the scope picked
};

class ScopesPanel {
public:
    ScopesPanel() = default;
    ~ScopesPanel();    // deletes the texture: the GL context must be current
    ScopesPanel(const ScopesPanel&)            = delete;
    ScopesPanel& operator=(const ScopesPanel&) = delete;

    ScopesOut draw(const ScopesInputs& in);

private:
    unsigned      tex_ = 0;
    int           tex_w_ = 0, tex_h_ = 0;
    std::uint64_t tex_serial_ = 0;   // result uploaded
    bool          moved_ = false;    // dragged by the user: no longer docked
};

struct ProbeInputs {
    const PixelView* view = nullptr;   // the frame on screen, mapped
    ImVec4 video_rect;                 // logical px: x, y_top, w, h
    ImVec2 at = ImVec2(-1.0f, -1.0f);  // QA: probe here (fractions of the video) instead of at the mouse
    AVColorSpace                  cs    = AVCOL_SPC_BT709;
    AVColorRange                  range = AVCOL_RANGE_MPEG;
    AVColorTransferCharacteristic trc   = AVCOL_TRC_BT709;
};

// Crosshair and readout at the mouse while it is over the video and not
// over a panel: the coded pixel, its Y′CbCr codes and what they mean.
void draw_pixel_probe(const ProbeInputs& in);
