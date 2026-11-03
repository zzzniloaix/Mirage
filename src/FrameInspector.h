#pragma once

// Frame inspector card (P): the metadata of the frame on screen
// (FrameInfo.h), drawn with Dear ImGui in the player theme. Must be called
// between PlayerUI's begin_frame() and end_frame().

#include "FrameInfo.h"

struct FrameInspectorInputs {
    const frameinfo::Report* report = nullptr;   // null = no frame yet
    float win_w = 0.0f, win_h = 0.0f;            // logical display size
    float top_inset    = 0.0f;                   // logical px under the top bar
    float bottom_inset = 0.0f;                   // logical px of controls + strips
    float right_inset  = 0.0f;                   // logical px of the network sidebar
};

struct FrameInspectorOut {
    bool close  = false;
    bool copied = false;   // the report text went to the clipboard
};

FrameInspectorOut draw_frame_inspector(const FrameInspectorInputs& in);
