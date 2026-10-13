#pragma once

#include "Demuxer.h"          // AudioTrackInfo
#include "SubtitleSelect.h"   // SubChoice
#include "SubtitleOverlay.h"  // SubStyle, SubtitleOverlay
#include "UiWidgets.h"        // SeekBarState

#include <cstdint>
#include <string>
#include <vector>

struct GLFWwindow;
class  ThumbnailStrip;

// Commands produced by PlayerUI::build() for one frame.
// Use sentinel values (-1 / -2) to indicate "no change this frame".
struct PlayerUIState {
    // Playback
    bool   play_pause_clicked = false;  // user clicked the play/pause button
    double seek_to            = -1.0;   // >= 0: absolute seek target (seconds)
    bool   seek_bar_dragging  = false;  // ImGui seek bar held this frame
    float  seek_bar_frac      = -1.0f;  // [0,1] drag position while held
    bool   seek_cancelled     = false;  // Esc during a seek-bar drag: drop it, no seek
    double speed_set          = -1.0;   // >= 0: new playback speed
    float  volume_set         = -1.0f;  // >= 0: new volume [0, 1]
    bool   mute_toggle        = false;  // volume icon clicked
    int    step_key           = 0;      // ±1: keyframe step (same as , / .)
    int    step_frame         = 0;      // ±1: frame step (same as Shift+, / Shift+.)
    bool   toggle_fullscreen  = false;
    int    audio_track_set    = -2;     // >= 0: stream index to switch to
    int    sub_choice_set     = -2;     // -2 no change, -1 Off, >= 0 index into sub_choices
    bool   sub_open_file      = false;  // "Open subtitle file..." chosen
    bool   sub_style_changed  = false;
    bool   quit               = false;  // Media > Quit selected

    // Debug panel toggles — true means "flip this flag"
    bool toggle_hud       = false;
    bool toggle_inspector = false;
    bool toggle_drift     = false;
    bool toggle_tracks    = false;
    bool toggle_waveform  = false;
    bool toggle_network   = false;
    bool toggle_help      = false;
    bool toggle_vmaf      = false;   // V key / View menu
    bool toggle_filmstrip = false;   // K key

    // VMAF analysis triggers (set to true for exactly one frame)
    bool vmaf_pick_ref         = false;  // open file picker to choose reference
    bool vmaf_analyze_manifest = false;  // start manifest variant comparison
    bool vmaf_export_json      = false;  // write JSON report to disk
};

// Everything PlayerUI::build() needs for one frame (replaces the old
// 15-parameter build()). Pointers are borrowed for the duration of build().
struct PlayerUIInputs {
    double duration = 0.0, cur_pts = 0.0, buffered_end_pts = -1.0, fps = 0.0;
    bool   paused = false;
    double speed  = 1.0;
    float  volume = 1.0f;
    bool   has_audio = false;
    bool   show_hud = false, show_inspector = false, show_drift = false, show_tracks = false,
           show_waveform = false, show_network = false, show_help = false, show_vmaf = false,
           show_filmstrip = false;
    bool   has_manifest_variants = false, fullscreen = false;

    struct Info {
        std::string title, title_full, codec, res, fps, range, decode;
        int  dropped = 0;
        bool hdr     = false;
    } info;

    const std::vector<AudioTrackInfo>* audio_tracks = nullptr;
    int                                audio_idx    = -1;
    const std::vector<SubChoice>*      sub_choices  = nullptr;
    int                                sub_active   = -1;     // index into sub_choices, -1 off

    ThumbnailStrip*            thumbs = nullptr;   // main thread only
    const std::vector<double>* kf_pts = nullptr;   // copy held by main
    float  video_aspect = 16.0f / 9.0f;            // display w/h after SAR + rotation
    ImVec4 video_rect   = {};                      // logical px: x, y_top, w, h

    // Logical px of engineer strips stacked above the controls (timeline,
    // waveform, filmstrip) — the hover card and subtitles sit above them.
    float  extra_bottom_logical = 0.0f;
    bool   filmstrip_visible    = false;            // strip drawn this frame

    // Subtitle text to draw this frame (text tracks; empty = none).
    std::string subtitle_text;

    // QA
    bool   force_visible    = false;
    bool   force_hidden     = false;   // QA: chrome hidden (overrides force_visible)
    float  debug_hover_frac = -1.0f;
};

// Dear ImGui–based player chrome: overlay top bar + bottom control pills that
// fade together, hover preview card, popovers, toasts and the launcher.
//
// Typical frame:
//   player_ui.begin_frame();
//   auto cmd = player_ui.build(inputs);
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
    // Rebuilds the font atlas first when the display scale changed or new
    // subtitle glyphs were requested.
    void begin_frame();

    // Build the overlay chrome for one frame. Returns commands issued this frame.
    PlayerUIState build(const PlayerUIInputs& in);

    // ── VMAF results window ────────────────────────────────────────────────────

    struct VMAFResultEntry {
        std::string label;
        double      mean    = -1.0;
        double      min_val = -1.0;
        double      p5      = -1.0;
        int         width   = 0, height = 0;
        int64_t     bandwidth = 0;
        bool        done    = false;
        std::string error;
        std::vector<double> per_frame;
    };

    // Draw the VMAF results ImGui window.  Call between begin_frame()/end_frame().
    // Returns true when the user clicks "Export JSON".
    bool draw_vmaf_results(const std::vector<VMAFResultEntry>& entries,
                           bool analyzing, float progress,
                           float pos_frac);

    // Submit the ImGui draw data to the GPU (call after all other drawing is done).
    void end_frame();

    // Call from cursor-move and key callbacks to reset the auto-hide timer.
    void notify_activity();

    // Height of the bottom control stack in logical px, eased with the fade
    // (kControlStackH when shown, 0 — or the 2 px mini-progress line — when
    // hidden). Feeds HUD bottom offsets and the subtitle lift.
    float visible_bar_height_logical() const;

    // Constant top inset (logical px) reserved for the top bar; HUD panels
    // start below it so they don't jump when the bar fades.
    static float top_inset_logical();

    // OSD toast at top-centre; a new toast replaces the current one.
    void toast(std::string text);

    // QA: open a popover ("cc", "settings", "overlays", "menu") or the help
    // card ("help") on the next build().
    void open_menu(const std::string& which) { pending_menu_ = which; }

    // Current eased subtitle lift (logical px) — applied by main to bitmap
    // subtitles in the lower part of the frame so they track the text cue.
    float subtitle_lift_logical() const { return sub_overlay_.lift(); }

    const SubStyle& sub_style() const { return sub_style_; }
    void set_sub_style(const SubStyle& s) { sub_style_ = s; }

    bool mini_progress() const { return mini_progress_; }

    // Show the launch screen. Returns non-empty path/URL when the user has chosen
    // a file (via Browse, Enter, or recent-file click), or empty string each frame
    // while still waiting.
    // error_msg: optional red banner shown above the URL input — used to surface
    //            "failed to open …" after a previous attempt failed.
    std::string draw_launcher(const std::vector<std::string>& recents,
                              const std::string& error_msg = {});

private:
    enum class SettingsPage { Root, Speed, Audio, Subtitles, Style };

    GLFWwindow* window_      = nullptr;
    bool   inited_           = false;
    double last_activity_    = 0.0;   // glfwGetTime() at last user activity
    float  ui_alpha_         = 1.0f;  // eased chrome opacity
    float  stack_vis_        = 1.0f;  // eased control-stack visibility (for layout)
    bool   mini_progress_    = true;
    bool   showing_mini_     = false; // mini-progress line drawn (controls hidden)

    char url_buf_[2048]      = {};    // persists InputText state between launcher frames

    ui::SeekBarState seek_;
    int    tc_mode_          = 0;     // timecode display mode (click cycles)
    float  vol_expand_       = 0.0f;  // eased volume-slider reveal
    bool   vol_dragging_     = false;
    SettingsPage settings_page_ = SettingsPage::Root;
    bool   cc_style_page_    = false; // CC popover showing the style sub-page
    SubStyle sub_style_;
    std::string pending_menu_;

    // Toast / centre flash
    std::string toast_text_;
    double      toast_t0_    = -1.0;
    double      flash_t0_    = -1.0;
    bool        flash_playing_ = false;
    int         prev_paused_ = -1;    // -1 unknown (first frame)

    SubtitleOverlay sub_overlay_;

    // Popover / card builders (return true while open).
    void build_top_bar(const PlayerUIInputs& in, PlayerUIState& out, float win_w);
    void build_controls(const PlayerUIInputs& in, PlayerUIState& out, float win_w, float win_h, float dt);
    void build_menu_popup(const PlayerUIInputs& in, PlayerUIState& out);
    void build_settings_popup(const PlayerUIInputs& in, PlayerUIState& out);
    void build_cc_popup(const PlayerUIInputs& in, PlayerUIState& out);
    void build_overlays_popup(const PlayerUIInputs& in, PlayerUIState& out);
    void build_help_card(PlayerUIState& out, float win_w, float win_h);
    void build_sub_rows(const PlayerUIInputs& in, PlayerUIState& out);
    void build_sub_style_rows(PlayerUIState& out);
    void draw_hover_card(const PlayerUIInputs& in, const ui::SeekBarOut& sb, float win_w);
    void draw_subtitle(const PlayerUIInputs& in, float win_w, float win_h, float dt);
};
