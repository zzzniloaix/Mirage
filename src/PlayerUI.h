#pragma once

#include <string>
#include <vector>

struct GLFWwindow;

// Commands produced by PlayerUI::build() for one frame.
// Use sentinel values (-1) to indicate "no change this frame".
struct PlayerUIState {
    // Playback
    bool   play_pause_clicked = false;  // user clicked the play/pause button
    double seek_to            = -1.0;   // >= 0: absolute seek target (seconds)
    double speed_set          = -1.0;   // >= 0: new playback speed
    float  volume_set         = -1.0f;  // >= 0: new volume [0, 1]
    bool   quit               = false;  // File > Quit selected

    // Debug panel toggles — true means "flip this flag"
    bool toggle_hud       = false;
    bool toggle_inspector = false;
    bool toggle_drift     = false;
    bool toggle_tracks    = false;
    bool toggle_waveform  = false;
    bool toggle_network   = false;
    bool toggle_help      = false;
};

// Dear ImGui–based player UI: top menu bar + auto-hiding floating control bar.
//
// Typical frame:
//   player_ui.begin_frame();
//   auto cmd = player_ui.build(...);
//   player_ui.end_frame();
//   // act on cmd
class PlayerUI {
public:
    PlayerUI()  = default;
    ~PlayerUI();
    PlayerUI(const PlayerUI&) = delete;
    PlayerUI& operator=(const PlayerUI&) = delete;

    // Call AFTER registering your own GLFW callbacks so ImGui can chain to them.
    [[nodiscard]] bool init(GLFWwindow* window);
    void shutdown();

    // Call at the top of each render loop iteration (before drawing anything).
    void begin_frame();

    // Build the menu bar and control bar for one frame.
    // Returns commands the user issued this frame.
    PlayerUIState build(
        double duration_s,   double cur_pts,
        bool   paused,       double speed,    float  volume,
        bool   has_audio,
        bool   show_hud,     bool show_inspector, bool show_drift,
        bool   show_tracks,  bool show_waveform,  bool show_network,
        bool   show_help);

    // Submit the ImGui draw data to the GPU (call after all other drawing is done).
    void end_frame();

    // Height of the menu bar in logical window pixels (0 before the first frame).
    float menu_bar_height() const { return menu_bar_h_; }

    // Call from cursor-move and key callbacks to reset the auto-hide timer.
    void notify_activity();

    // Height of the VLC-style control bar in logical window pixels.
    // Returns 0 when the bar is fully hidden (use for bottom-stack offsets).
    float visible_bar_height_logical() const;

    // The constant full height of the control bar (even when faded).
    static constexpr float kVlcBarH = 72.0f;

    // Show the launch screen. Returns non-empty path/URL when the user has chosen
    // a file (via Browse, Enter, or recent-file click), or empty string each frame
    // while still waiting.
    std::string draw_launcher(const std::vector<std::string>& recents);

private:
    bool   inited_         = false;
    float  menu_bar_h_     = 0.0f;
    double last_activity_  = 0.0;   // glfwGetTime() at last user activity
    float  bar_alpha_      = 1.0f;  // control bar opacity: 1 = visible, 0 = hidden

    char url_buf_[2048]    = {};    // persists InputText state between launcher frames
    bool drop_hovered_     = false; // set from GLFW drop callback
};
