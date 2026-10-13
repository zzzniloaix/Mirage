#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "Demuxer.h"
#include "Decoder.h"
#include "AudioDecoder.h"
#include "AudioPlayer.h"
#include "VideoRenderer.h"
#include "ScrubBar.h"
#include "ThumbnailStrip.h"
#include "ScrubDecoder.h"
#include "DebugHUD.h"
#include "Clock.h"
#include "Sync.h"
#include "Queue.h"
#include "Logger.h"
#include "NetworkLogger.h"
#include "ManifestParser.h"
#include "PlayerUI.h"
#include "VMAFAnalyzer.h"
#include "SubtitleDecoder.h"
#include "SpeedPresets.h"
#include "SubtitleSelect.h"
#include "SidecarSubtitles.h"
#include "FfOpen.h"
#include "UiFormat.h"
#include "MediaSession.h"

// imgui headers (needed for WantCapture* guards in GLFW callbacks)
#include "imgui.h"
#include <nfd.h>

#include <thread>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <fstream>
#include <filesystem>
#include <map>
#include <memory>
#include <algorithm>
#include <cctype>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
#include <libavutil/pixdesc.h>
#include <libavutil/pixfmt.h>
#include <libavutil/samplefmt.h>
#include <libavutil/channel_layout.h>
}

// File-scope pointer so GLFW callbacks can call notify_activity().
static PlayerUI* g_player_ui = nullptr;

// Set by GLFW drop callback (main thread only). Read by the launcher and,
// during playback, by run_session (opens the file in place of the current one).
static std::string g_drop_pending;

static std::atomic<double> s_seek_delta{ 0.0 };
static std::atomic<bool>   s_paused{ false };
static std::atomic<double> s_speed{ 1.0 };
static std::atomic<bool>   s_scrubbing{ false };
static std::atomic<double> s_scrub_frac{ -1.0 };  // [0,1], negative = no pending scrub
static std::atomic<bool>   s_show_hud{ false };
static std::atomic<bool>   s_show_inspector{ false };
static std::atomic<bool>   s_show_drift{ false };
static std::atomic<bool>   s_show_tracks{ false };
static std::atomic<bool>   s_show_waveform{ false };
static std::atomic<bool>   s_show_network{ false };
static std::atomic<bool>   s_show_help{ false };
static std::atomic<bool>   s_show_vmaf{ false };
static std::atomic<double> s_cursor_x{ -1.0 };   // window-space cursor, always updated
static std::atomic<double> s_cursor_y{ -1.0 };
static std::atomic<double> s_click_x{ -1.0 };    // window-space left-click (consumed once)
static std::atomic<double> s_click_y{ -1.0 };
static std::atomic<int>    s_step_frames{ 0 };  // +1 = fwd, -1 = bwd; computed in render loop
static std::atomic<int>    s_single_steps{ 0 }; // Shift+. requests; one decoded frame per count
static std::atomic<int>    s_back_steps{ 0 };   // Shift+, requests; one frame backward per count
static std::atomic<bool>   s_show_filmstrip{ false };  // K: keyframe filmstrip (off by default)
static std::atomic<bool>   s_fullscreen{ false };      // mirrors glfwGetWindowMonitor() != nullptr
static std::atomic<int>    s_fullscreen_toggle{ 0 };   // F / Esc-in-fullscreen requests
static std::atomic<int>    s_mute_toggle{ 0 };         // M requests
static std::atomic<int>    s_sub_toggle{ 0 };          // C requests (on/off)
static std::atomic<int>    s_sub_cycle{ 0 };           // Shift+C requests (next track)
static std::atomic<int>    s_audio_cycle{ 0 };         // A requests (next audio track)
static std::atomic<int>    s_open_dialog{ 0 };         // O requests (Open dialog)

// Video frames more than this far (wall seconds, i.e. media / speed) behind the
// audio clock are dropped rather than shown. Above Sync's 100 ms correction
// window so normal drift is still handled by compute_video_delay().
static constexpr double kLateFrameDrop = 0.15;

// ── QA: --speed-seq / --av-report ─────────────────────────────────────────────
// Scripted speed changes and an A/V-sync report, in the style of --snapshot:
// run for N wall seconds, write JSON, exit. Lets speed/sync regressions be
// measured without a human watching the HUD.

// One scripted event at at_sec seconds since playback start. Besides speed
// changes, "pause@t" / "play@t" / "seek:<secs>@t" exercise the audio flush and
// blocked-push paths that rapid speed switching interacts with; "kf+@t" /
// "kf-@t" are keyframe steps (the . / , keys).
struct SpeedStep {
    enum Kind { Speed, Pause, Play, Seek, KfNext, KfPrev } kind = Speed;
    double at_sec;
    double speed;   // Speed: new speed; Seek: target seconds
};

// Parse "1@0,4@2,pause@3,seek:30@4,play@5". Every speed must be a preset.
// Returns false on error.
static bool parse_speed_seq(const std::string& spec, std::vector<SpeedStep>& out)
{
    std::size_t pos = 0;
    while (pos < spec.size()) {
        std::size_t comma = spec.find(',', pos);
        std::string item  = spec.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        std::size_t at    = item.find('@');
        if (at == std::string::npos) return false;
        const std::string what = item.substr(0, at);
        SpeedStep step{};
        char* end = nullptr;
        if (what == "pause" || what == "play") {
            step.kind = (what == "pause") ? SpeedStep::Pause : SpeedStep::Play;
        } else if (what == "kf+" || what == "kf-") {
            step.kind = (what == "kf+") ? SpeedStep::KfNext : SpeedStep::KfPrev;
        } else if (what.starts_with("seek:")) {
            step.kind  = SpeedStep::Seek;
            step.speed = std::strtod(what.c_str() + 5, &end);
            if (*end != '\0' || step.speed < 0.0) return false;
        } else {
            step.speed = std::strtod(what.c_str(), &end);
            if (*end != '\0' || !is_speed_preset(step.speed)) return false;
        }
        step.at_sec = std::strtod(item.c_str() + at + 1, &end);
        if (*end != '\0' || step.at_sec < 0.0) return false;
        out.push_back(step);
        if (comma == std::string::npos) break;
        pos = comma + 1;
    }
    std::stable_sort(out.begin(), out.end(),
                     [](const SpeedStep& a, const SpeedStep& b) { return a.at_sec < b.at_sec; });
    return !out.empty();
}

// Accumulates playback stats over one stretch of constant requested speed.
struct AvSegment {
    double speed      = 1.0;
    double t_start    = -1.0;   // wall seconds since playback start (first presented frame)
    double t_end      = 0.0;
    double v_start    = 0.0, v_end = 0.0;   // presented video PTS (media sec)
    double a_start    = 0.0, a_end = 0.0;   // audio clock (media sec)
    bool   has_a      = false;
    int64_t presented = 0;
    int64_t dropped   = 0;
    std::vector<double> av_media;           // video pts − audible audio pts (media sec)
    std::vector<double> av_wall_ms;         // same, / speed at sample time, in ms
    std::vector<double> av_wall_ms_settled; // av_wall_ms minus the first kAvSettle s
    int64_t underruns_settled = -1;         // underrun / silent-fill counts since settle
    int64_t silent_settled    = -1;
    int64_t u_mark = 0, s_mark = 0;
    double  t_set = -1.0, v_set = 0.0, a_set = 0.0;   // first settled sample

    void on_frame(double t, double vpts, bool audio_ok, double aclk)
    {
        if (t_start < 0.0) { t_start = t; v_start = vpts; }
        if (audio_ok && !has_a) { a_start = aclk; has_a = true; }
        t_end = t; v_end = vpts;
        if (audio_ok) a_end = aclk;
        ++presented;
    }
};

// {"mean":..,"p95_abs":..,"max_abs":..,"n":..} of v.
static std::string av_stats_json(const std::vector<double>& v)
{
    if (v.empty()) return R"({"n":0})";
    double sum = 0.0, mx = 0.0;
    std::vector<double> a;
    a.reserve(v.size());
    for (double d : v) { sum += d; a.push_back(std::abs(d)); mx = std::max(mx, std::abs(d)); }
    std::sort(a.begin(), a.end());
    double p95 = a[std::min(a.size() - 1, static_cast<std::size_t>(0.95 * a.size()))];
    return std::format(R"({{"mean":{:.6f},"p95_abs":{:.6f},"max_abs":{:.6f},"n":{}}})",
                       sum / v.size(), p95, mx, v.size());
}

static std::string av_segment_json(const AvSegment& s)
{
    const double wall   = std::max(0.0, s.t_end - s.t_start);
    const double vmedia = s.v_end - s.v_start;
    const double amedia = s.has_a ? s.a_end - s.a_start : 0.0;
    return std::format(
        R"({{"speed":{},"t_start":{:.3f},"wall_s":{:.3f},"video_media_s":{:.3f},"audio_media_s":{:.3f},)"
        R"("video_rate":{:.4f},"audio_rate":{:.4f},"frames_presented":{},"frames_dropped_late":{},)"
        R"("av_diff_media_s":{},"av_diff_wall_ms":{},"av_diff_wall_ms_settled":{},)"
        R"("underruns_settled":{},"silent_frames_settled":{},"video_rate_settled":{:.4f},"audio_rate_settled":{:.4f}}})",
        s.speed, std::max(0.0, s.t_start), wall, vmedia, amedia,
        wall > 0.0 ? vmedia / wall : 0.0, wall > 0.0 ? amedia / wall : 0.0,
        s.presented, s.dropped,
        av_stats_json(s.av_media), av_stats_json(s.av_wall_ms), av_stats_json(s.av_wall_ms_settled),
        s.underruns_settled, s.silent_settled,
        (s.t_set >= 0.0 && s.t_end > s.t_set) ? (s.v_end - s.v_set) / (s.t_end - s.t_set) : 0.0,
        (s.t_set >= 0.0 && s.t_end > s.t_set) ? (s.a_end - s.a_set) / (s.t_end - s.t_set) : 0.0);
}

static void on_key(GLFWwindow* window, int key, int /*scancode*/, int action, int mods)
{
    if (g_player_ui) g_player_ui->notify_activity();
    // Let ImGui consume keyboard events when it wants exclusive input (e.g. text fields).
    if (ImGui::GetIO().WantCaptureKeyboard) return;
    if (action != GLFW_PRESS) return;
    // Esc: a seek-bar drag in progress is cancelled by ImGui (it owns the
    // keyboard while the bar is held, so we never get here); otherwise close
    // the Open dialog, then leave fullscreen, and only then quit.
    if (key == GLFW_KEY_ESCAPE) {
        if (g_player_ui && g_player_ui->open_dialog_visible()) g_player_ui->close_open_dialog();
        else if (s_fullscreen.load()) s_fullscreen_toggle.fetch_add(1);
        else                     glfwSetWindowShouldClose(window, GLFW_TRUE);
    }
    else if (key == GLFW_KEY_Q) glfwSetWindowShouldClose(window, GLFW_TRUE);
    else if (key == GLFW_KEY_SPACE) s_paused.store(!s_paused.load());
    else if (key == GLFW_KEY_RIGHT) s_seek_delta.store(+10.0);
    else if (key == GLFW_KEY_LEFT)  s_seek_delta.store(-10.0);
    else if (key == GLFW_KEY_UP)    s_seek_delta.store(+60.0);
    else if (key == GLFW_KEY_DOWN)  s_seek_delta.store(-60.0);
    else if (key == GLFW_KEY_D) s_show_hud.store(!s_show_hud.load());
    else if (key == GLFW_KEY_I) s_show_inspector.store(!s_show_inspector.load());
    else if (key == GLFW_KEY_G) s_show_drift.store(!s_show_drift.load());
    else if (key == GLFW_KEY_T) s_show_tracks.store(!s_show_tracks.load());
    else if (key == GLFW_KEY_W) s_show_waveform.store(!s_show_waveform.load());
    else if (key == GLFW_KEY_N) s_show_network.store(!s_show_network.load());
    else if (key == GLFW_KEY_H) s_show_help.store(!s_show_help.load());
    else if (key == GLFW_KEY_SLASH && (mods & GLFW_MOD_SHIFT)) s_show_help.store(!s_show_help.load());
    else if (key == GLFW_KEY_K) s_show_filmstrip.store(!s_show_filmstrip.load());
    else if (key == GLFW_KEY_F) s_fullscreen_toggle.fetch_add(1);
    else if (key == GLFW_KEY_M) s_mute_toggle.fetch_add(1);
    else if (key == GLFW_KEY_C && (mods & GLFW_MOD_SHIFT)) s_sub_cycle.fetch_add(1);
    else if (key == GLFW_KEY_C) s_sub_toggle.fetch_add(1);
    else if (key == GLFW_KEY_A) s_audio_cycle.fetch_add(1);
    else if (key == GLFW_KEY_V) s_show_vmaf.store(!s_show_vmaf.load());
    else if (key == GLFW_KEY_O) s_open_dialog.fetch_add(1);
    // Shift+. / Shift+, → single-frame advance / step-back (frame-accurate).
    // Plain . / , = keyframe step.
    else if (key == GLFW_KEY_PERIOD && (mods & GLFW_MOD_SHIFT)) {
        s_paused.store(true); s_single_steps.fetch_add(1);
    }
    else if (key == GLFW_KEY_COMMA && (mods & GLFW_MOD_SHIFT)) {
        s_paused.store(true); s_back_steps.fetch_add(1);
    }
    else if (key == GLFW_KEY_PERIOD) { s_paused.store(true); s_step_frames.fetch_add(1); }
    else if (key == GLFW_KEY_COMMA)  { s_paused.store(true); s_step_frames.fetch_add(-1); }
    else if (key == GLFW_KEY_RIGHT_BRACKET || key == GLFW_KEY_LEFT_BRACKET) {
        double cur = s_speed.load();
        // Find current preset index (or nearest)
        int idx = 3;  // default: 1.0x
        for (int i = 0; i < kNumPresets; ++i)
            if (kSpeedPresets[i] >= cur) { idx = i; break; }
        if (key == GLFW_KEY_RIGHT_BRACKET) idx = std::min(idx + 1, kNumPresets - 1);
        else                               idx = std::max(idx - 1, 0);
        s_speed.store(kSpeedPresets[idx]);
    }
}

static void on_framebuffer_resize(GLFWwindow* /*window*/, int /*width*/, int /*height*/)
{
    // Viewport is recomputed each frame in the render loop.
}

// Mouse clicks on the video area (ImGui owns the chrome). All seeking goes
// through the ImGui seek bar; s_scrubbing is kept for the scrub pipeline but
// is only ever driven by ui_seek_dragging now.
static void on_mouse_button(GLFWwindow* window, int button, int action, int /*mods*/)
{
    if (g_player_ui) g_player_ui->notify_activity();
    // Let ImGui consume mouse events when it owns a window under the cursor.
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;

    if (action == GLFW_PRESS) {
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        // General click — track selector hit testing handled in render loop.
        s_click_x.store(mx);
        s_click_y.store(my);
    } else if (action == GLFW_RELEASE) {
        s_scrubbing.store(false);
    }
}

static void on_cursor_pos(GLFWwindow* window, double mx, double my)
{
    if (g_player_ui) g_player_ui->notify_activity();
    s_cursor_x.store(mx);
    s_cursor_y.store(my);
    if (!s_scrubbing.load()) return;
    int win_w, win_h;
    glfwGetWindowSize(window, &win_w, &win_h);
    if (win_w > 0)
        s_scrub_frac.store(std::clamp(mx / win_w, 0.0, 1.0));
}

// Returns the largest AR-correct rect that fits inside (win_w × win_h).
static void compute_video_rect(int win_w, int win_h, int vid_w, int vid_h,
                                int& x, int& y, int& w, int& h)
{
    float vid_ar = static_cast<float>(vid_w) / static_cast<float>(vid_h);
    float win_ar = static_cast<float>(win_w) / static_cast<float>(win_h);
    if (vid_ar > win_ar) {          // letterbox (bars top/bottom)
        w = win_w;
        h = static_cast<int>(win_w / vid_ar);
        x = 0;
        y = (win_h - h) / 2;
    } else {                        // pillarbox (bars left/right)
        h = win_h;
        w = static_cast<int>(win_h * vid_ar);
        x = (win_w - w) / 2;
        y = 0;
    }
}

// ── Recent files helpers ──────────────────────────────────────────────────────

static std::string recent_file_path()
{
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.config/mirage/recent.txt";
}

static std::vector<std::string> load_recents()
{
    std::vector<std::string> v;
    auto p = recent_file_path();
    if (p.empty()) return v;
    std::ifstream f(p);
    std::string line;
    while (std::getline(f, line) && v.size() < 8)
        if (!line.empty()) v.push_back(line);
    return v;
}

static void save_recent(const std::string& entry)
{
    auto p = recent_file_path();
    if (p.empty() || entry.empty()) return;
    auto v = load_recents();
    v.erase(std::remove(v.begin(), v.end(), entry), v.end());
    v.insert(v.begin(), entry);
    if (v.size() > 8) v.resize(8);
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(p).parent_path(), ec);
    std::ofstream f(p);
    for (const auto& r : v) f << r << "\n";
}

// ── Preferences (~/.config/mirage/prefs.txt, key=value lines) ────────────────

struct Prefs {
    SubPref  sub;
    SubStyle sub_style;
};

static std::string prefs_file_path()
{
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.config/mirage/prefs.txt";
}

static Prefs load_prefs()
{
    Prefs pr;
    auto p = prefs_file_path();
    if (p.empty()) return pr;
    std::ifstream f(p);
    std::string line;
    while (std::getline(f, line)) {
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "sub_mode") {
            pr.sub.mode = v == "off" ? SubPref::Off : v == "lang" ? SubPref::Lang : SubPref::Auto;
        } else if (k == "sub_lang") {
            pr.sub.lang = v;
        } else if (k == "sub_size") {
            pr.sub_style.size_pct = std::clamp(std::atoi(v.c_str()), 50, 200);
        } else if (k == "sub_edge") {
            pr.sub_style.edge = v == "outline" ? SubStyle::Outline : v == "shadow" ? SubStyle::DropShadow
                              : v == "none" ? SubStyle::None : SubStyle::Box;
        } else if (k == "sub_box") {
            pr.sub_style.box_opacity = std::clamp(std::atoi(v.c_str()), 0, 100);
        }
    }
    if (pr.sub.mode == SubPref::Lang && pr.sub.lang.empty()) pr.sub.mode = SubPref::Auto;
    return pr;
}

static void save_prefs(const Prefs& pr)
{
    auto p = prefs_file_path();
    if (p.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(std::filesystem::path(p).parent_path(), ec);
    std::ofstream f(p);
    static constexpr const char* kEdge[] = { "box", "outline", "shadow", "none" };
    f << "sub_mode=" << (pr.sub.mode == SubPref::Off ? "off" : pr.sub.mode == SubPref::Lang ? "lang" : "auto") << "\n"
      << "sub_lang=" << pr.sub.lang << "\n"
      << "sub_size=" << pr.sub_style.size_pct << "\n"
      << "sub_edge=" << kEdge[std::clamp(static_cast<int>(pr.sub_style.edge), 0, 3)] << "\n"
      << "sub_box=" << pr.sub_style.box_opacity << "\n";
}

// ── UI helpers ────────────────────────────────────────────────────────────────

// Monitor containing the window centre (falls back to the primary monitor).
static GLFWmonitor* monitor_for_window(GLFWwindow* w)
{
    int wx, wy, ww, wh;
    glfwGetWindowPos(w, &wx, &wy);
    glfwGetWindowSize(w, &ww, &wh);
    const int cx = wx + ww / 2, cy = wy + wh / 2;
    int n = 0;
    GLFWmonitor** mons = glfwGetMonitors(&n);
    for (int i = 0; i < n; ++i) {
        int mx, my;
        glfwGetMonitorPos(mons[i], &mx, &my);
        const GLFWvidmode* m = glfwGetVideoMode(mons[i]);
        if (m && cx >= mx && cx < mx + m->width && cy >= my && cy < my + m->height) return mons[i];
    }
    return glfwGetPrimaryMonitor();
}

// Top-bar title: basename for files, host + last path segment for URLs.
static std::string media_title(const std::string& path)
{
    const auto scheme = path.find("://");
    if (scheme == std::string::npos)
        return std::filesystem::path(path).filename().string();
    std::string rest = path.substr(scheme + 3);
    if (auto q = rest.find_first_of("?#"); q != std::string::npos) rest.resize(q);
    const auto slash = rest.find('/');
    const std::string host = rest.substr(0, slash);
    const auto last = rest.rfind('/');
    if (slash == std::string::npos || last + 1 >= rest.size()) return host;
    return host + " \u2026/" + rest.substr(last + 1);
}

// "HH:MM:SS.mmm" for OSD toasts.
static std::string toast_clock(double t)
{
    if (!(t > 0.0)) t = 0.0;
    const long long ms = std::llround(t * 1000.0);
    return std::format("{:02d}:{:02d}:{:02d}.{:03d}",
                       ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
}

// "23.976", "30", "29.97"
static std::string fps_text(double fps)
{
    std::string s = std::format("{:.3f}", fps);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

// ── App-level state shared by every media session ────────────────────────────

// Command-line options. QA flags marked "first" stage the first media only;
// captures (--snapshot / --ui-snapshot / --av-report) wait until every
// --qa-open-after switch has happened, so they describe the last source.
struct CliOptions {
    bool        auto_vmaf     = false;    // first
    std::string vmaf_ref_path;   // --vmaf ref.mp4 for single-file mode
    int         cli_sub_idx   = -2;   // first: -2 = unset, -1 = explicit off, >=0 = stream idx
    std::string cli_sub_file;         // first: --sub-file <path>
    bool        no_auto_subs  = false; // --no-auto-subs: pref Off for this run
    bool        test_bitmap_sub = false;  // QA: inject synthetic bitmap sub event
    bool        try_hw          = true;   // VideoToolbox HW decode on by default; --no-hw to disable
    // QA snapshot hook: `--snapshot <out.ppm> <frame_number>` captures the video
    // viewport after N frames have been drawn, writes a PPM, and exits. Used
    // to visually diff HW vs SW rendering paths without needing a human eye.
    std::string snapshot_path;
    int         snapshot_at_frame = 30;
    // QA A/V hooks: `--speed <x>` (first), `--speed-seq <x@t,...>`, `--av-report <out.json> <secs>`.
    double                 cli_speed = 1.0;
    std::vector<SpeedStep> speed_seq;
    std::string            av_report_path;
    double                 av_report_secs = 10.0;
    // QA UI hooks: `--ui-snapshot <out.png> <frame>` captures the whole window
    // (video + chrome) after the UI has settled; modifiers stage what to show.
    std::string ui_snap_path;
    int         ui_snap_frame = 30;
    float       ui_hover_frac = -1.0f;      // --ui-hover <frac>
    std::string ui_menu;                    // first: --ui-menu cc|settings|overlays|help|menu|open
    double      ui_sub_at     = -1.0;       // --ui-sub-at <seconds>
    std::string ui_osd;                     // --ui-osd "<text>"
    std::string ui_overlays;                // first: --ui-overlays DIGW...
    bool        ui_hide_controls = false;   // --ui-hide-controls: capture with chrome hidden
    // QA source switching: `--qa-open-after <secs> <path>` (repeatable) opens
    // <path> once the current media has played <secs> wall seconds — the same
    // path a drop / the Open dialog takes.
    std::vector<std::pair<double, std::string>> qa_open;
};

struct App {
    GLFWwindow*    window = nullptr;
    PlayerUI&      player_ui;
    DebugHUD&      debug_hud;
    NetworkLogger& net_logger;
    CliOptions&    cli;
    // QA ui-snapshot runs ignore (and never write) prefs.txt for determinism.
    bool           prefs_enabled = true;
    Prefs          prefs;
    SubPref        sub_pref;                  // user subtitle preference (C, menus)
    std::vector<std::string> recents;
    float          volume          = 1.0f;    // carried from one media to the next
    float          pre_mute_volume = 1.0f;
    int            fs_x = 0, fs_y = 0, fs_w = 0, fs_h = 0;   // windowed geometry
    int            sessions_played = 0;
    std::size_t    qa_open_next    = 0;       // next --qa-open-after event
};

// Opens `path` as a new MediaSession and records it in the recent list. On
// failure returns nullptr with a user-facing message in `err`.
static std::unique_ptr<MediaSession> open_session(App& app, const std::string& path, std::string& err)
{
    auto ms = std::make_unique<MediaSession>();
    MediaSessionOptions opt;
    opt.try_hw          = app.cli.try_hw;
    opt.test_bitmap_sub = app.cli.test_bitmap_sub;
    if (!ms->open(path, opt, err)) return nullptr;
    // Local paths are stored absolute so a recent entry works from any cwd.
    std::error_code ec;
    std::string entry = path;
    if (!ffopen::is_network_url(path)) {
        auto abs = std::filesystem::absolute(path, ec);
        if (!ec) entry = abs.lexically_normal().string();
    }
    save_recent(entry);
    app.recents = load_recents();
    if (ms->has_audio) ms->audio_player.set_volume(app.volume);
    return ms;
}

// Plays one media source until the window should close (returns nullptr) or
// another source has been opened successfully (returns it; the caller then
// destroys `ms` and plays the new session). All render-loop state below is
// per media and starts fresh with each call.
static std::unique_ptr<MediaSession> run_session(App& app, MediaSession& ms);

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    // --window WxH must be known before the window exists.
    int init_win_w = 1280, init_win_h = 720;
    for (int i = 1; i + 1 < argc; ++i)
        if (std::string(argv[i]) == "--window") {
            int w = 0, h = 0;
            if (std::sscanf(argv[i + 1], "%dx%d", &w, &h) == 2 && w >= 200 && h >= 150) {
                init_win_w = w; init_win_h = h;
            }
        }

    // ── GLFW + OpenGL ─────────────────────────────────────────────────────────
    if (!glfwInit()) {
        logger::error("Failed to initialise GLFW");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(init_win_w, init_win_h, "Mirage", nullptr, nullptr);
    if (!window) {
        logger::error("Failed to create GLFW window");
        glfwTerminate();
        return EXIT_FAILURE;
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);  // vsync

    if (!gladLoadGL(glfwGetProcAddress)) {
        logger::error("Failed to load OpenGL via GLAD");
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    logger::info("OpenGL {} — GLSL {}",
        reinterpret_cast<const char*>(glGetString(GL_VERSION)),
        reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION)));

    // Register app callbacks BEFORE ImGui so ImGui can chain to them.
    glfwSetKeyCallback(window, on_key);
    glfwSetFramebufferSizeCallback(window, on_framebuffer_resize);
    glfwSetMouseButtonCallback(window, on_mouse_button);
    glfwSetCursorPosCallback(window, on_cursor_pos);
    glfwSetDropCallback(window, [](GLFWwindow*, int count, const char** paths) {
        if (count > 0) g_drop_pending = paths[0];
    });

    // ImGui UI — init after callbacks so it can install wrapper callbacks that chain to ours.
    PlayerUI player_ui;
    g_player_ui = &player_ui;
    if (!player_ui.init(window)) {
        logger::error("PlayerUI: failed to init Dear ImGui");
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    {
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
    }

    ScrubBar scrub_bar;
    if (!scrub_bar.init()) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    DebugHUD debug_hud;
    if (!debug_hud.init()) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
    }

    // ── Choose file: command-line arg or launcher screen ──────────────────────
    std::string open_path;
    CliOptions  cli;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--vmaf") {
            cli.auto_vmaf = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                cli.vmaf_ref_path = argv[++i];
        } else if (arg == "--sub" && i + 1 < argc) {
            const std::string v = argv[++i];
            cli.cli_sub_idx = (v == "off") ? -1 : std::max(-1, std::atoi(v.c_str()));
        } else if (arg == "--sub-file" && i + 1 < argc) {
            cli.cli_sub_file = argv[++i];
        } else if (arg == "--no-auto-subs") {
            cli.no_auto_subs = true;
        } else if (arg == "--window" && i + 1 < argc) {
            ++i;   // handled before window creation
        } else if (arg == "--ui-snapshot" && i + 2 < argc) {
            cli.ui_snap_path  = argv[++i];
            cli.ui_snap_frame = std::atoi(argv[++i]);
        } else if (arg == "--ui-hover" && i + 1 < argc) {
            cli.ui_hover_frac = std::clamp(static_cast<float>(std::atof(argv[++i])), 0.0f, 1.0f);
        } else if (arg == "--ui-menu" && i + 1 < argc) {
            cli.ui_menu = argv[++i];
        } else if (arg == "--ui-sub-at" && i + 1 < argc) {
            cli.ui_sub_at = std::max(0.0, std::atof(argv[++i]));
        } else if (arg == "--ui-osd" && i + 1 < argc) {
            cli.ui_osd = argv[++i];
        } else if (arg == "--ui-hide-controls") {
            cli.ui_hide_controls = true;
        } else if (arg == "--ui-overlays" && i + 1 < argc) {
            cli.ui_overlays = argv[++i];
        } else if (arg == "--test-bitmap-sub") {
            cli.test_bitmap_sub = true;
        } else if (arg == "--no-hw") {
            cli.try_hw = false;
        } else if (arg == "--snapshot" && i + 2 < argc) {
            cli.snapshot_path     = argv[++i];
            cli.snapshot_at_frame = std::atoi(argv[++i]);
        } else if (arg == "--speed" && i + 1 < argc) {
            cli.cli_speed = std::atof(argv[++i]);
            if (!is_speed_preset(cli.cli_speed)) {
                logger::error("--speed {}: not a preset speed", argv[i]);
                return EXIT_FAILURE;
            }
        } else if (arg == "--speed-seq" && i + 1 < argc) {
            if (!parse_speed_seq(argv[++i], cli.speed_seq)) {
                logger::error("--speed-seq '{}': expected speed@secs[,speed@secs...] with preset speeds",
                              argv[i]);
                return EXIT_FAILURE;
            }
        } else if (arg == "--av-report" && i + 2 < argc) {
            cli.av_report_path = argv[++i];
            cli.av_report_secs = std::atof(argv[++i]);
        } else if (arg == "--qa-open-after" && i + 2 < argc) {
            const double secs = std::max(0.0, std::atof(argv[++i]));
            cli.qa_open.emplace_back(secs, argv[++i]);
        } else if (open_path.empty()) {
            open_path = arg;
        }
    }

    // ── Network logger (must be installed before any FFmpeg call) ─────────────
    NetworkLogger net_logger;
    net_logger.install();

    App app{ .window = window, .player_ui = player_ui, .debug_hud = debug_hud,
             .net_logger = net_logger, .cli = cli };
    app.fs_w = init_win_w;
    app.fs_h = init_win_h;
    app.recents = load_recents();
    // QA ui-snapshot runs ignore (and never write) prefs.txt for determinism.
    app.prefs_enabled = cli.ui_snap_path.empty();
    app.prefs    = app.prefs_enabled ? load_prefs() : Prefs{};
    player_ui.set_sub_style(app.prefs.sub_style);
    app.sub_pref = app.prefs.sub;
    if (cli.no_auto_subs) app.sub_pref.mode = SubPref::Off;

    // ── Pick a source + open it.  On failure, fall back to the launcher
    // with an error banner instead of exiting — a bad URL shouldn't kill the app.
    std::unique_ptr<MediaSession> session;
    std::string   launcher_err;

    while (!session) {
        if (open_path.empty()) {
            while (!glfwWindowShouldClose(window) && open_path.empty()) {
                if (!g_drop_pending.empty()) {
                    open_path = g_drop_pending;
                    g_drop_pending.clear();
                    break;
                }
                glfwPollEvents();

                int fb_w, fb_h;
                glfwGetFramebufferSize(window, &fb_w, &fb_h);
                glViewport(0, 0, fb_w, fb_h);
                glClearColor(0.13f, 0.14f, 0.16f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);

                player_ui.begin_frame();
                open_path = player_ui.draw_launcher(app.recents, launcher_err);
                player_ui.end_frame();

                glfwSwapBuffers(window);
            }
            if (open_path.empty()) {
                // User closed the window without choosing
                net_logger.uninstall();
                g_player_ui = nullptr;
                player_ui.shutdown();
                glfwDestroyWindow(window);
                glfwTerminate();
                return EXIT_SUCCESS;
            }
        }

        session = open_session(app, open_path, launcher_err);
        // Retry: clear the path so the launcher loop above re-runs.
        open_path.clear();
    }

    // ── VMAF analyzer: --vmaf auto-starts on the first media ──────────────────
    if (cli.auto_vmaf) {
        s_show_vmaf.store(true);
        VMAFAnalyzer& vmaf_analyzer = session->vmaf_analyzer;
        if (!session->manifest.variants().empty()) {
            logger::info("VMAFAnalyzer: auto-starting manifest variant analysis");
            vmaf_analyzer.start_manifest(session->manifest.variants());
        } else if (!cli.vmaf_ref_path.empty()) {
            logger::info("VMAFAnalyzer: auto-starting single-file analysis vs {}",
                         cli.vmaf_ref_path);
            vmaf_analyzer.start(cli.vmaf_ref_path, session->path);
        } else {
            logger::warn("VMAFAnalyzer: --vmaf used but no ref file or manifest variants "
                         "— open VMAF panel (V) and use Analyze menu");
        }
    }

    // ── Play; a successful Open / drop hands back the next session ────────────
    // Assigning the new session destroys the old one: its threads stop and
    // join, then its FFmpeg / GL / audio resources are freed (GL context current).
    while (session)
        session = run_session(app, *session);

    // ── Shutdown ──────────────────────────────────────────────────────────────
    net_logger.uninstall();
    g_player_ui = nullptr;
    player_ui.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}

// ── Playback of one media source ─────────────────────────────────────────────

// Plays one media source until the window should close (returns nullptr) or
// another source has been opened successfully (returns it; the caller then
// destroys `ms` and plays the new session). All render-loop state below is
// per media and starts fresh with each call.
static std::unique_ptr<MediaSession> run_session(App& app, MediaSession& ms)
{
    const bool first_session = app.sessions_played++ == 0;

    // A new source starts playing from its beginning: drop transport requests
    // aimed at the previous one. Overlay toggles, speed and fullscreen carry over.
    s_paused.store(false);
    s_seek_delta.store(0.0);
    s_scrubbing.store(false);
    s_scrub_frac.store(-1.0);
    s_click_x.store(-1.0);
    s_click_y.store(-1.0);
    s_step_frames.store(0);
    s_single_steps.store(0);
    s_back_steps.store(0);
    s_sub_toggle.store(0);
    s_sub_cycle.store(0);
    s_audio_cycle.store(0);
    s_open_dialog.store(0);

    // App-level objects and per-media components, under the names the loop
    // below has always used.
    GLFWwindow*      window           = app.window;
    PlayerUI&        player_ui        = app.player_ui;
    DebugHUD&        debug_hud        = app.debug_hud;
    NetworkLogger&   net_logger       = app.net_logger;
    const std::string& open_path      = ms.path;
    Demuxer&         demuxer          = ms.demuxer;
    ManifestParser&  manifest         = ms.manifest;
    VMAFAnalyzer&    vmaf_analyzer    = ms.vmaf_analyzer;
    Decoder&         video_decoder    = ms.video_decoder;
    AudioPlayer&     audio_player     = ms.audio_player;
    AudioDecoder&    audio_decoder    = ms.audio_decoder;
    VideoRenderer&   renderer         = ms.renderer;
    ThumbnailStrip&  thumb_strip      = ms.thumb_strip;
    ScrubDecoder&    scrub_decoder    = ms.scrub_decoder;
    SubtitleDecoder& subtitle_decoder = ms.subtitle_decoder;
    Queue<AVFrame*>& frameq           = ms.frameq;
    const bool       has_audio        = ms.has_audio;
    const bool       local_media      = ms.local_media;
    const double     duration         = ms.duration;
    const AVRational video_tb         = ms.video_tb;
    const int        coded_w = ms.coded_w, coded_h = ms.coded_h;
    const int        vid_w   = ms.vid_w,   vid_h   = ms.vid_h;
    const double     frame_dt         = ms.frame_dt;
    const double     video_start_s    = ms.video_start_s;
    auto&            inspector_lines  = ms.inspector_lines;
    const std::size_t decode_line_idx = ms.decode_line_idx;
    const auto&      audio_track_items    = ms.audio_track_items;
    const auto&      video_track_items    = ms.video_track_items;
    const auto&      subtitle_track_items = ms.subtitle_track_items;

    const CliOptions&  cli              = app.cli;
    const bool         auto_vmaf        = cli.auto_vmaf;
    const bool         test_bitmap_sub  = cli.test_bitmap_sub;
    const std::string& snapshot_path    = cli.snapshot_path;
    const int          snapshot_at_frame = cli.snapshot_at_frame;
    const double       cli_speed        = cli.cli_speed;
    const auto&        speed_seq        = cli.speed_seq;
    std::string&       av_report_path   = app.cli.av_report_path;   // cleared once written
    const double       av_report_secs   = cli.av_report_secs;
    const std::string& ui_snap_path     = cli.ui_snap_path;
    const int          ui_snap_frame    = cli.ui_snap_frame;
    const float        ui_hover_frac    = cli.ui_hover_frac;
    const double       ui_sub_at        = cli.ui_sub_at;
    const std::string& ui_osd           = cli.ui_osd;
    const bool         ui_hide_controls = cli.ui_hide_controls;
    const std::string  ui_menu      = first_session ? cli.ui_menu      : std::string();
    const std::string  ui_overlays  = first_session ? cli.ui_overlays  : std::string();
    const int          cli_sub_idx  = first_session ? cli.cli_sub_idx  : -2;
    const std::string  cli_sub_file = first_session ? cli.cli_sub_file : std::string();
    float& pre_mute_volume = app.pre_mute_volume;
    int& fs_x = app.fs_x;
    int& fs_y = app.fs_y;
    int& fs_w = app.fs_w;
    int& fs_h = app.fs_h;

    bool vmaf_was_running = vmaf_analyzer.running();  // for detecting completion (auto-export)
    // ── A/V sync state ────────────────────────────────────────────────────────
    MasterClock master_clock;
    double  last_pts    = 0.0;
    // Media-time duration of the last shown frame (PTS delta). Used to synthesize
    // PTS for frames without one and as the delay fallback on PTS gaps. Never a
    // wall-time value: scheduling divides by speed separately.
    double  last_frame_dur = 1.0 / 30.0;   // refined from avg_frame_rate below
    bool    has_frame   = false;
    bool    was_paused  = false;
    double  cur_speed   = 1.0;
    int64_t frame_count = 0;

    // Currently active stream indices (updated when track switches complete).
    int cur_audio_stream    = demuxer.audio_stream_index();
    int cur_video_stream    = demuxer.video_stream_index();
    int cur_subtitle_stream = demuxer.subtitle_stream_index();

    // ── Subtitle selection: "show when available" (SubtitleSelect) ──────────
    // Choices = embedded tracks + same-stem sidecar files (local media only)
    // + --sub-file. choose_initial() picks one unless the user said Off.
    // Prefs and the subtitle preference are app-level (loaded once in main).
    const bool prefs_enabled = app.prefs_enabled;
    Prefs&     prefs         = app.prefs;
    SubPref&   sub_pref      = app.sub_pref;
    auto persist_prefs = [&] {
        if (!prefs_enabled) return;
        prefs.sub       = sub_pref;
        prefs.sub_style = player_ui.sub_style();
        save_prefs(prefs);
    };

    const std::string media_stem  = local_media ? std::filesystem::path(open_path).stem().string() : std::string();
    std::vector<SubChoice> sub_choices = build_choices(
        demuxer.subtitle_tracks(),
        local_media ? find_sidecars(open_path) : std::vector<std::string>{},
        media_stem);

    // Loaded sidecar stores, kept so switching back is instant.
    std::map<std::string, std::unique_ptr<SidecarSubtitles>> sidecar_stores;
    SidecarSubtitles* active_sidecar = nullptr;
    int sub_active      = -1;   // index into sub_choices, -1 = off
    int last_sub_choice = -1;   // last non-Off choice (for C)

    auto same_file = [](const std::string& a, const std::string& b) {
        std::error_code ec;
        return a == b || std::filesystem::equivalent(a, b, ec);
    };
    // Sidecar choice index for `path`, appending it if not discovered.
    auto add_sidecar_choice = [&](const std::string& path) -> int {
        for (std::size_t i = 0; i < sub_choices.size(); ++i)
            if (sub_choices[i].kind == SubChoice::Sidecar && same_file(sub_choices[i].path, path))
                return static_cast<int>(i);
        sub_choices.push_back(sidecar_choice(path, media_stem));
        return static_cast<int>(sub_choices.size()) - 1;
    };

    SubCli sub_cli;
    sub_cli.off        = cli_sub_idx == -1;
    sub_cli.stream_idx = cli_sub_idx >= 0 ? cli_sub_idx : -1;
    if (cli_sub_idx >= 0) {
        bool valid = false;
        for (const auto& t : demuxer.subtitle_tracks())
            if (t.stream_idx == cli_sub_idx) { valid = true; break; }
        if (!valid) logger::warn("--sub {}: no such subtitle stream", cli_sub_idx);
    }
    if (!cli_sub_file.empty()) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(cli_sub_file, ec)) {
            sub_cli.file = sub_choices[static_cast<std::size_t>(add_sidecar_choice(cli_sub_file))].path;
        } else {
            logger::warn("--sub-file {}: not found", cli_sub_file);
            player_ui.toast("Subtitle file failed: "
                            + std::filesystem::path(cli_sub_file).filename().string());
        }
    }

    // Single entry point for every subtitle selection change (CC menu,
    // settings, C / Shift+C, T panel, startup policy, sidecar failure).
    // idx: -1 = Off, >= 0 = index into sub_choices. User changes update and
    // persist SubPref; Auto shows the "C to turn off" hint; Silent no toast.
    enum class SubBy { User, Auto, Silent };
    auto apply_sub_choice = [&](int idx, SubBy by) {
        if (idx >= static_cast<int>(sub_choices.size())) idx = -1;
        if (idx < 0) {
            if (cur_subtitle_stream >= 0) demuxer.request_subtitle_switch(-1);
            cur_subtitle_stream = -1;
            active_sidecar      = nullptr;
            sub_active          = -1;
            if (by == SubBy::User) { sub_pref = { SubPref::Off, {} }; persist_prefs(); }
            if (by != SubBy::Silent) player_ui.toast("Subtitles: off");
            return;
        }
        const SubChoice& c = sub_choices[static_cast<std::size_t>(idx)];
        std::string what;
        if (c.kind == SubChoice::Embedded) {
            if (c.stream_idx != cur_subtitle_stream) {
                demuxer.request_subtitle_switch(c.stream_idx);
                cur_subtitle_stream = c.stream_idx;
            }
            active_sidecar = nullptr;
            what = c.label();
        } else {
            if (cur_subtitle_stream >= 0) demuxer.request_subtitle_switch(-1);
            cur_subtitle_stream = -1;
            auto& store = sidecar_stores[c.path];
            if (!store) {
                store = std::make_unique<SidecarSubtitles>();
                store->load(c.path, video_start_s);
            }
            active_sidecar = store.get();
            what = "sidecar \u00B7 " + std::filesystem::path(c.path).filename().string();
        }
        sub_active      = idx;
        last_sub_choice = idx;
        if (by == SubBy::User) {
            sub_pref = c.lang.empty() ? SubPref{} : SubPref{ SubPref::Lang, c.lang };
            persist_prefs();
        }
        if (by == SubBy::Auto)
            player_ui.toast("Subtitles: " + what + " \u2014 C to turn off");
        else if (by == SubBy::User)
            player_ui.toast("Subtitles: " + what);
    };

    // A sidecar that fails to load is dropped from the list and the policy
    // continues with the remaining choices (rule 1 fallback).
    auto check_sidecar_failure = [&] {
        if (!active_sidecar || !active_sidecar->failed()) return;
        const std::string bad = active_sidecar->path();
        logger::warn("Subtitle file failed: {}", bad);
        player_ui.toast("Subtitle file failed: " + std::filesystem::path(bad).filename().string());
        active_sidecar = nullptr;
        for (std::size_t i = 0; i < sub_choices.size(); ++i) {
            if (sub_choices[i].kind != SubChoice::Sidecar || sub_choices[i].path != bad) continue;
            const int ei = static_cast<int>(i);
            sub_choices.erase(sub_choices.begin() + ei);
            if (last_sub_choice == ei) last_sub_choice = -1;
            else if (last_sub_choice > ei) --last_sub_choice;
            break;
        }
        sidecar_stores.erase(bad);
        if (sub_cli.file == bad) sub_cli.file.clear();
        sub_active = -1;
        SubRule rule{};
        const int next = choose_initial(sub_choices, sub_cli, sub_pref, &rule);
        apply_sub_choice(next, next >= 0 && sub_rule_is_auto(rule) ? SubBy::Auto : SubBy::Silent);
    };

    {
        SubRule rule{};
        const int first = choose_initial(sub_choices, sub_cli, sub_pref, &rule);
        static constexpr const char* kRuleName[] = {
            "none", "--sub-file", "--sub", "--sub off", "pref off", "pref lang",
            "forced", "default", "sidecar", "first text", "first bitmap", "none available" };
        logger::info("Subtitles: {} choice(s); initial {} ({})", sub_choices.size(),
                     first >= 0 ? sub_choices[static_cast<std::size_t>(first)].label() : std::string("off"),
                     kRuleName[static_cast<int>(rule)]);
        if (first >= 0)
            apply_sub_choice(first, sub_rule_is_auto(rule) ? SubBy::Auto : SubBy::Silent);
    }

    // Audio track switch (settings, A key, T panel).
    auto switch_audio = [&](int stream_idx, bool paused_now, std::chrono::steady_clock::time_point& valid_at) {
        if (stream_idx < 0 || stream_idx == cur_audio_stream || !has_audio) return;
        cur_audio_stream = stream_idx;
        audio_player.set_paused(true);
        demuxer.request_audio_switch(stream_idx);
        valid_at = std::chrono::steady_clock::now() + std::chrono::milliseconds(400);
        if (!paused_now) audio_player.set_paused(false);
        for (const auto& t : demuxer.audio_tracks())
            if (t.stream_idx == stream_idx)
                player_ui.toast(std::format("Audio: {}{}{} \u00B7 {} {}ch", t.stream_idx,
                    t.language.empty() ? "" : " \u00B7 ", t.language, t.codec_name, t.channels));
    };


    // When to show the next frame (wall clock)
    const auto startup_time = std::chrono::steady_clock::now();
    auto next_pts_time  = startup_time;
    auto last_title_upd = startup_time;
    // After a seek, ignore the audio clock until it has settled at the new position.
    auto audio_clock_valid_at = std::chrono::steady_clock::now();
    // A seek while paused can't flush audio until playback resumes: the audio
    // thread sits in push() on the full ring of pre-seek audio (the device is
    // stopped) and only reaches the seek's flush sentinel once it drains.
    bool seeked_while_paused = false;
    // While paused the device is stopped, so the audio thread blocks in push()
    // on a full ring, audioq fills, and the demuxer blocks on audioq — it then
    // never services a seek, and frame steps starve. Paused navigation makes
    // the buffered audio stale anyway: flush it (safe, the device is stopped)
    // so the audio thread drains audioq and the demuxer can move again.
    auto release_paused_audio = [&] {
        if (has_audio) audio_player.flush();
        seeked_while_paused = true;
    };

    // Scrub state
    bool   was_scrubbing      = false;
    bool   pending_step_frame = false;  // true = show next arriving frame (step while paused)
    double last_scrub_frac   = -1.0;   // latest drag position
    double last_seek_pos     = 0.0;    // pts of the most recent seek; freezes thumbnail highlight
    double scrub_display_pts = -1.0;   // PTS of the last frame actually uploaded from ScrubDecoder
    bool   ui_seek_dragging  = false;  // ImGui seek bar was dragging last frame (1-frame carry)
    bool   pending_back_step = false;  // true while waiting on ScrubDecoder for backward step

    last_frame_dur = frame_dt;   // one frame from avg_frame_rate (MediaSession)

    // Seconds for a decoded frame: its PTS, else libavcodec's best-effort guess
    // (MPEG-TS without PTS on every frame), else one frame after the last one.
    auto frame_pts_sec = [&](const AVFrame* f) {
        int64_t ts = (f->pts != AV_NOPTS_VALUE) ? f->pts : f->best_effort_timestamp;
        return (ts != AV_NOPTS_VALUE) ? ts * av_q2d(video_tb) : last_pts + last_frame_dur;
    };

    // ── QA: --speed / --speed-seq / --av-report state ─────────────────────────
    if (first_session && cli_speed != 1.0) s_speed.store(cli_speed);
    std::size_t            speed_seq_next = 0;
    bool                   qa_started     = false;  // first frame presented
    auto                   qa_t0          = startup_time;
    std::vector<AvSegment> av_segs;
    AvSegment              av_total;
    int64_t qa_underruns0 = 0, qa_silent0 = 0, qa_dropped_audio0 = 0;
    static constexpr double kAvWarmup = 0.5;   // wall s excluded from overall A/V stats
    static constexpr double kAvSettle = 1.0;   // wall s after each speed change excluded from *_settled
    auto   last_any_seek_at   = std::chrono::steady_clock::time_point{};
    // Delayed unmute: after scrub release we wait for the post-seek sentinel to
    // flush the audio pipeline before un-muting, preventing a brief burst of
    // wrong-position audio from the decode thread filling the ring buffer.
    auto   unmute_at = std::chrono::steady_clock::time_point::max();

    // ── UI state ──────────────────────────────────────────────────────────────
    const bool          ui_snap_mode = !ui_snap_path.empty();
    std::vector<double> kf_pts_ui;               // main-thread copy for seek-bar ticks
    bool                kf_pts_final = false;    // re-copied once the thumb thread finished
    int64_t             total_dropped = 0;       // late frames dropped (top-bar chip)
    double              pre_scrub_pts = 0.0;     // restored when a drag is cancelled (Esc)
    PlayerUIInputs::Info ui_info;
    ui_info.title      = media_title(open_path);
    ui_info.title_full = open_path;
    auto last_info_upd = std::chrono::steady_clock::time_point{};
    const double stream_fps = (frame_dt > 0.0) ? 1.0 / frame_dt : 0.0;
    const float  video_aspect = (vid_h > 0) ? static_cast<float>(vid_w) / static_cast<float>(vid_h) : 16.0f / 9.0f;

    // --ui-overlays DIGW...: switch engineer panels on for the screenshot.
    for (char c : ui_overlays) {
        switch (std::toupper(static_cast<unsigned char>(c))) {
            case 'D': s_show_hud.store(true);       break;
            case 'I': s_show_inspector.store(true); break;
            case 'G': s_show_drift.store(true);     break;
            case 'T': s_show_tracks.store(true);    break;
            case 'W': s_show_waveform.store(true);  break;
            case 'N': s_show_network.store(true);   break;
            case 'V': s_show_vmaf.store(true);      break;
            case 'K': s_show_filmstrip.store(true); break;
            default: break;
        }
    }
    if (!ui_menu.empty()) player_ui.open_menu(ui_menu);
    // --ui-sub-at: seek there, play until the target is reached, then pause.
    bool ui_sub_at_landed = ui_sub_at < 0.0;
    if (ui_sub_at >= 0.0) s_seek_delta.store(ui_sub_at > 0.0 ? ui_sub_at : 1e-6);
    int ui_frames = 0, ui_total_frames = 0;

    // Previous overlay flags → "HUD on" / "Inspector off" toasts.
    struct OverlayFlag { std::atomic<bool>* flag; const char* name; bool prev; };
    OverlayFlag overlay_flags[] = {
        { &s_show_hud,       "HUD",       false }, { &s_show_inspector, "Inspector", false },
        { &s_show_drift,     "A/V drift", false }, { &s_show_tracks,    "Tracks",    false },
        { &s_show_waveform,  "Waveform",  false }, { &s_show_network,   "Network",   false },
        { &s_show_vmaf,      "VMAF",      false }, { &s_show_filmstrip, "Filmstrip", false },
    };
    for (auto& f : overlay_flags) f.prev = f.flag->load();

    auto toggle_fullscreen = [&] {
        if (!glfwGetWindowMonitor(window)) {
            glfwGetWindowPos(window, &fs_x, &fs_y);
            glfwGetWindowSize(window, &fs_w, &fs_h);
            GLFWmonitor* mon = monitor_for_window(window);
            const GLFWvidmode* m = glfwGetVideoMode(mon);
            if (!m) return;
            glfwSetWindowMonitor(window, mon, 0, 0, m->width, m->height, m->refreshRate);
            s_fullscreen.store(true);
        } else {
            glfwSetWindowMonitor(window, nullptr, fs_x, fs_y, fs_w, fs_h, 0);
            s_fullscreen.store(false);
        }
    };

    // ── Open another source (drop, Open dialog / O, --qa-open-after) ───────
    // The request is opened one frame later so the "Opening …" toast is on
    // screen during the (blocking) open. This session stays alive until the
    // new one is ready, so a failed open just keeps playing it.
    std::string open_req;
    int         open_req_frames = 0;
    auto request_open = [&](const std::string& p) {
        if (p.empty()) return;
        open_req        = p;
        open_req_frames = 0;
        player_ui.toast("Opening " + media_title(p) + " \u2026");
    };

    // No vsync — we manage timing ourselves via glfwWaitEventsTimeout
    glfwSwapInterval(0);

    // ── Render loop ───────────────────────────────────────────────────────────
    while (!glfwWindowShouldClose(window)) {
        auto now    = std::chrono::steady_clock::now();
        bool paused = s_paused.load();

        // ── Auto-export VMAF report when --vmaf analysis finishes ────────────
        {
            bool vmaf_running_now = vmaf_analyzer.running();
            if (auto_vmaf && vmaf_was_running && !vmaf_running_now) {
                std::string json_path = open_path + ".vmaf.json";
                vmaf_analyzer.write_json(json_path);
                logger::info("VMAF report written to {}", json_path);
            }
            vmaf_was_running = vmaf_running_now;
        }

        // ── Pause state change ────────────────────────────────────────────
        if (paused != was_paused) {
            was_paused = paused;
            // Resuming after a paused seek/step: drop the stale pre-seek audio
            // still in the ring (device is stopped, so flushing here is safe)
            // and restart the settle grace period until the seek's flush lands.
            if (!paused && seeked_while_paused) {
                if (has_audio) audio_player.flush();
                audio_clock_valid_at = now + std::chrono::milliseconds(300);
            }
            seeked_while_paused = false;
            if (has_audio) audio_player.set_paused(paused);
        }

        // ── Key commands that need main-thread state ──────────────────────
        if (s_fullscreen_toggle.exchange(0) > 0) toggle_fullscreen();
        if (s_mute_toggle.exchange(0) > 0 && has_audio) {
            const float v = audio_player.volume();
            if (v > 0.0f) { pre_mute_volume = v; audio_player.set_volume(0.0f); player_ui.toast("Muted"); }
            else {
                const float nv = pre_mute_volume > 0.0f ? pre_mute_volume : 1.0f;
                audio_player.set_volume(nv);
                player_ui.toast(std::format("Volume {}%", static_cast<int>(std::lround(nv * 100.0f))));
            }
        }
        check_sidecar_failure();
        if (s_sub_toggle.exchange(0) > 0) {
            // C: Off <-> last non-Off choice (or the Auto policy's pick).
            if (sub_active >= 0) apply_sub_choice(-1, SubBy::User);
            else if (last_sub_choice >= 0) apply_sub_choice(last_sub_choice, SubBy::User);
            else if (const int i = choose_initial(sub_choices, {}, {}); i >= 0) apply_sub_choice(i, SubBy::User);
            else player_ui.toast("No subtitles");
        }
        if (s_sub_cycle.exchange(0) > 0) {
            // Shift+C: Off -> 0 -> 1 -> ... -> Off.
            if (sub_choices.empty()) player_ui.toast("No subtitles");
            else apply_sub_choice(sub_active + 1 >= static_cast<int>(sub_choices.size()) ? -1 : sub_active + 1,
                                  SubBy::User);
        }
        if (s_audio_cycle.exchange(0) > 0) {
            const auto& at = demuxer.audio_tracks();
            if (at.size() > 1) {
                std::size_t i = 0;
                while (i < at.size() && at[i].stream_idx != cur_audio_stream) ++i;
                switch_audio(at[(i + 1) % at.size()].stream_idx, paused, audio_clock_valid_at);
            } else {
                player_ui.toast(at.empty() ? "No audio" : "Only one audio track");
            }
        }

        // ── Source switching requests ─────────────────────────────────────
        if (!g_drop_pending.empty()) {
            request_open(g_drop_pending);   // file dropped onto the window
            g_drop_pending.clear();
        }
        if (s_open_dialog.exchange(0) > 0) player_ui.show_open_dialog();
        // QA: --qa-open-after, timed from this media's start.
        if (open_req.empty() && app.qa_open_next < cli.qa_open.size()
            && now - startup_time >= std::chrono::duration<double>(cli.qa_open[app.qa_open_next].first)) {
            const auto& ev = cli.qa_open[app.qa_open_next++];
            logger::info("--qa-open-after: t={:.1f} opening {}", ev.first, ev.second);
            request_open(ev.second);
        }
        // Captures wait for the last scripted switch (see CliOptions).
        const bool qa_open_done = app.qa_open_next >= cli.qa_open.size();

        // ── Scrub state transitions ───────────────────────────────────────
        // ui_seek_dragging carries the ImGui seek bar drag state from the
        // previous frame (1-frame delay because player_ui.build() runs late).
        // Combining it with the GLFW scrub flag ensures thumbnail strip,
        // ScrubDecoder, and audio mute all activate during ImGui bar drags.
        bool scrubbing = s_scrubbing.load() || ui_seek_dragging;

        // Consume the latest drag position.  scrub_moved is true only when a
        // new value arrived AND the cursor moved at least ~0.1% of screen width
        // (≈2px on a 2560px display) — filters trackpad micro-jitter that would
        // otherwise cause the thumbnail highlight to flicker between adjacent
        // keyframes while the mouse appears physically still.
        static constexpr double kScrubMinDelta = 0.001;
        bool scrub_moved = false;
        {
            double f = s_scrub_frac.exchange(-1.0);
            if (f >= 0.0) {
                if (last_scrub_frac < 0.0 || std::abs(f - last_scrub_frac) >= kScrubMinDelta) {
                    last_scrub_frac = f;
                    scrub_moved = true;
                }
            }
        }

        bool scrub_started = scrubbing && !was_scrubbing;
        bool scrub_ended   = !scrubbing && was_scrubbing;

        if (scrub_started) {
            // Mute at the sample level — takes effect within the current callback
            // period, unlike ma_device_stop which has CoreAudio-level drain latency.
            if (has_audio) {
                audio_player.set_muted(true);
                audio_player.flush();
                unmute_at = std::chrono::steady_clock::time_point::max();
            }
            // Drain stale frames so the video decode thread keeps running
            // while we bypass frameq during the drag.
            { AVFrame* s; while (frameq.try_pop(s)) av_frame_free(&s); }
            // Reset scrub display PTS so we don't show stale pts from the
            // previous drag before the first frame of this drag arrives.
            scrub_display_pts = -1.0;
        }

        // Delayed unmute: lift mute once the post-seek audio pipeline has settled.
        if (has_audio && now >= unmute_at) {
            audio_player.set_muted(false);
            unmute_at = std::chrono::steady_clock::time_point::max();
        }

        // ── Seek handling (keyboard delta + scrub bar) ────────────────────
        double seek_target = -1.0;

        double delta = s_seek_delta.exchange(0.0);
        if (delta != 0.0) {
            seek_target = std::max(0.0, last_pts + delta);
            if (!ui_snap_mode)
                player_ui.toast(std::format("{}{:g}s \u00B7 {}", delta < 0.0 ? "\u2212" : "+",
                                            std::round(std::abs(delta)), toast_clock(seek_target)));
        }

        if (scrubbing && scrub_moved && last_scrub_frac >= 0.0 && duration > 0.0) {
            // Route scrub requests through the dedicated ScrubDecoder instead
            // of the main pipeline — no flush/restart, sub-frame latency.
            // Only fire when the mouse actually moved; without this guard the
            // decoder keeps seeking the same position every frame, causing jitter.
            const double target = last_scrub_frac * duration;
            scrub_decoder.request(target);
            last_pts = target;
        }

        // On scrub release: always seek to the exact drop position.
        if (scrub_ended && last_scrub_frac >= 0.0 && duration > 0.0) {
            seek_target    = last_scrub_frac * duration;
            last_scrub_frac = -1.0;
        }

        // ── Keyframe stepping (.  /  ,) ───────────────────────────────────
        {
            int step = s_step_frames.exchange(0);
            if (step != 0) {
                double kf = (step > 0)
                    ? thumb_strip.next_keyframe_pts(last_pts)
                    : thumb_strip.prev_keyframe_pts(last_pts);
                if (kf >= 0.0) {
                    seek_target = kf;
                    player_ui.toast("KF \u25C6 " + toast_clock(kf));
                }
                pending_step_frame = true;
                last_any_seek_at   = now;
            }
        }

        if (seek_target >= 0.0) {
            last_any_seek_at = now;
            last_seek_pos    = seek_target;
            demuxer.request_seek(seek_target);
            if (paused) release_paused_audio();
            AVFrame* stale;
            while (frameq.try_pop(stale)) av_frame_free(&stale);
            // Keep has_frame = true so the last decoded frame stays on screen
            // while the pipeline seeks. This avoids blank-screen flicker
            // (VLC-style: show last frame until a new one arrives).
            last_pts      = seek_target;
            next_pts_time = now;
            audio_clock_valid_at = now + std::chrono::milliseconds(300);
        }

        // Scrub ended: flush stale audio, then schedule a delayed unmute so the
        // post-seek sentinel has time to clear the audio pipeline before we play.
        if (scrub_ended) {
            if (has_audio) {
                audio_player.flush();
                // Unmute after 300 ms — enough for the seek sentinel to propagate
                // through audioq → AudioDecoder → flush → fresh audio at seek pos.
                unmute_at = now + std::chrono::milliseconds(300);
            }
            audio_clock_valid_at = now + std::chrono::milliseconds(400);
            next_pts_time = now;  // immediately resume normal frame advance
        }

        was_scrubbing = scrubbing;

        // ── QA: scripted speed changes (--speed-seq), timed from playback start ──
        if (qa_started) {
            const double t = std::chrono::duration<double>(now - qa_t0).count();
            while (speed_seq_next < speed_seq.size() && speed_seq[speed_seq_next].at_sec <= t) {
                const SpeedStep& ev = speed_seq[speed_seq_next++];
                switch (ev.kind) {
                    case SpeedStep::Speed: s_speed.store(ev.speed);                    break;
                    case SpeedStep::Pause: s_paused.store(true);                       break;
                    case SpeedStep::Play:  s_paused.store(false);                      break;
                    case SpeedStep::Seek:  s_seek_delta.store(ev.speed - last_pts);    break;
                    case SpeedStep::KfNext: s_paused.store(true); s_step_frames.fetch_add(1);  break;
                    case SpeedStep::KfPrev: s_paused.store(true); s_step_frames.fetch_add(-1); break;
                }
                logger::info("--speed-seq: t={:.2f} event {} {}", t, static_cast<int>(ev.kind), ev.speed);
            }
        }

        // ── Speed change ──────────────────────────────────────────────────
        double speed = s_speed.load();
        if (speed != cur_speed) {
            cur_speed = speed;
            player_ui.toast(std::format("Speed {:g}\u00D7", speed));
            if (has_audio) audio_decoder.set_speed(speed);
        }

        // ── Frame advance ─────────────────────────────────────────────────
        if (scrubbing) {
            // ScrubDecoder handles seek+decode on its own thread; just upload
            // whatever it has decoded since the last render iteration.
            AVFrame* scrub_frame = scrub_decoder.poll_frame();
            if (scrub_frame) {
                // Track the PTS of what's actually on screen so the thumbnail
                // highlight matches the decoded keyframe, not the cursor position.
                // (avformat_seek_file snaps to the keyframe *before* the target,
                //  so bar_pts and the displayed keyframe can differ by up to one
                //  keyframe interval — causing the highlight to flicker as the
                //  cursor crosses the keyframe midpoint.)
                if (scrub_frame->pts != AV_NOPTS_VALUE)
                    scrub_display_pts = scrub_frame->pts * av_q2d(video_tb);
                renderer.upload(scrub_frame);
                av_frame_free(&scrub_frame);
                has_frame = true;
                ++frame_count;
            }
        } else if (!paused && now >= next_pts_time) {
            // Normal playback
            AVFrame* frame = nullptr;
            if (frameq.try_pop(frame)) {
                double pts = frame_pts_sec(frame);

                // Audio clock is master once it has settled after a seek.
                // During the grace period use video PTS to avoid the sync
                // algorithm stalling on the clock-at-zero transient.
                // Sync against what is audible: the device plays output_latency
                // (wall) behind the ring clock, i.e. latency × speed of media.
                bool audio_settled = has_audio && now >= audio_clock_valid_at;
                double master = audio_settled
                    ? audio_player.clock() - audio_player.output_latency_seconds() * speed
                    : pts;

                // Drop late frames. At high speed (e.g. 4x of 60fps = 240 fps)
                // the loop can't present every frame, so skip straight to the
                // newest one that isn't already behind the audio clock.
                // Diffs past 10 s are seek artifacts (see compute_video_delay).
                // Threshold is wall time (scaled to media by speed), and never
                // under two frames so slow-motion doesn't drop on ordinary jitter.
                int64_t dropped_now = 0;   // added to total_dropped below
                if (audio_settled) {
                    const double drop_behind = std::max(kLateFrameDrop * speed, 2.0 * last_frame_dur);
                    AVFrame* next = nullptr;
                    while (pts < master - drop_behind && master - pts < 10.0
                           && frameq.try_pop(next)) {
                        last_frame_dur = frame_duration(pts, last_pts, last_frame_dur);
                        last_pts = pts;
                        av_frame_free(&frame);
                        frame = next;
                        pts = frame_pts_sec(frame);
                        ++dropped_now;
                    }
                }

                total_dropped += dropped_now;
                master_clock.set_audio(master);
                master_clock.set_video(pts);

                // --ui-sub-at: pause once playback reaches the requested time.
                if (!ui_sub_at_landed && pts >= ui_sub_at - 0.5 * frame_dt) {
                    ui_sub_at_landed = true;
                    s_paused.store(true);
                }

                // Wall-time scheduling value — not carried over to the next frame.
                double delay = compute_video_delay(pts, last_pts, last_frame_dur, master, speed);
                delay = std::clamp(delay, 0.001, 0.5);

                // Schedule from the previous deadline, not from `now`, so per-frame
                // loop overhead doesn't accumulate. Re-anchor if we've fallen
                // far behind (seek, stall, window drag).
                auto delay_dur = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                     std::chrono::duration<double>(delay));
                next_pts_time += delay_dur;
                if (next_pts_time < now - std::chrono::milliseconds(100))
                    next_pts_time = now + delay_dur;
                last_frame_dur = frame_duration(pts, last_pts, last_frame_dur);
                last_pts       = pts;

                // QA: per-frame A/V sample = presented video PTS − audible audio PTS.
                if (!av_report_path.empty()) {
                    if (!qa_started) {
                        qa_started        = true;
                        qa_t0             = now;
                        qa_underruns0     = has_audio ? audio_player.underrun_count() : 0;
                        qa_silent0        = has_audio ? audio_player.silent_frames()  : 0;
                        qa_dropped_audio0 = has_audio ? audio_player.dropped_frames() : 0;
                    }
                    const double t = std::chrono::duration<double>(now - qa_t0).count();
                    if (av_segs.empty() || av_segs.back().speed != speed) {
                        av_segs.emplace_back();
                        av_segs.back().speed = speed;
                    }
                    const double aclk = audio_settled ? master : 0.0;
                    for (AvSegment* seg : { &av_segs.back(), &av_total }) {
                        seg->on_frame(t, pts, audio_settled, aclk);
                        seg->dropped += dropped_now;
                    }
                    // Overall speed is only meaningful when it never changed.
                    av_total.speed = (av_segs.size() == 1) ? speed : 0.0;
                    if (audio_settled) {
                        // Wall ms = media / speed: the offset the viewer perceives.
                        const double d  = pts - aclk;
                        const double ms = d / speed * 1000.0;
                        av_segs.back().av_media.push_back(d);
                        av_segs.back().av_wall_ms.push_back(ms);
                        AvSegment& sg = av_segs.back();
                        if (t - sg.t_start >= kAvSettle) {
                            sg.av_wall_ms_settled.push_back(ms);
                            if (sg.underruns_settled < 0) {
                                sg.u_mark = audio_player.underrun_count();
                                sg.s_mark = audio_player.silent_frames();
                                sg.underruns_settled = sg.silent_settled = 0;
                                sg.t_set = t; sg.v_set = pts; sg.a_set = aclk;
                            }
                            sg.underruns_settled = audio_player.underrun_count() - sg.u_mark;
                            sg.silent_settled    = audio_player.silent_frames()  - sg.s_mark;
                        }
                        if (t >= kAvWarmup) {
                            av_total.av_media.push_back(d);
                            av_total.av_wall_ms.push_back(ms);
                        }
                    }
                } else if (!qa_started) {
                    qa_started = true;   // --speed-seq without --av-report
                    qa_t0      = now;
                }

                renderer.upload(frame);
                av_frame_free(&frame);
                has_frame = true;
                ++frame_count;
            } else {
                next_pts_time = now + std::chrono::milliseconds(10);
            }
        }

        // ── Step frame: pop one frame while paused ───────────────────────
        if (pending_step_frame && !scrubbing) {
            AVFrame* frame = nullptr;
            if (frameq.try_pop(frame)) {
                double pts = (frame->pts != AV_NOPTS_VALUE)
                    ? frame->pts * av_q2d(video_tb)
                    : last_pts;
                renderer.upload(frame);
                av_frame_free(&frame);
                has_frame          = true;
                last_pts           = pts;
                ++frame_count;
                pending_step_frame = false;
                if (qa_started) logger::info("--speed-seq: step landed at pts {:.3f}", pts);
            }
            // else: frame not decoded yet — retry next iteration
        }

        // ── Frame-accurate forward step (Shift+.) ─────────────────────────
        // Pop the next decoded frame from frameq directly — no seek, no
        // discard. Advances exactly one decoded frame per request.
        while (s_single_steps.load() > 0 && paused && !scrubbing) {
            AVFrame* frame = nullptr;
            if (!frameq.try_pop(frame)) {          // wait for the decoder
                // Stepping consumes video only; once audioq fills the demuxer
                // stalls and so would the steps. Unblock the audio side.
                release_paused_audio();
                break;
            }
            double pts = frame_pts_sec(frame);
            renderer.upload(frame);
            av_frame_free(&frame);
            has_frame      = true;
            last_frame_dur = frame_duration(pts, last_pts, last_frame_dur);
            last_pts       = pts;
            ++frame_count;
            s_single_steps.fetch_sub(1);
            player_ui.toast(std::format("Frame {} \u00B7 {}", std::llround(pts * stream_fps), toast_clock(pts)));
        }

        // ── Frame-accurate backward step (Shift+,) ────────────────────────
        // Goes through ScrubDecoder: seek to keyframe before target, decode
        // forward to (last_pts - 1.5 * frame_dt). The 1.5× margin guards
        // against the >= comparison snapping back onto the current frame.
        // ScrubDecoder is stateful — issue request once, poll until result.
        if (s_back_steps.load() > 0 && paused && !scrubbing) {
            if (!pending_back_step) {
                double target = std::max(0.0, last_pts - frame_dt * 1.5);
                scrub_decoder.request(target);
                pending_back_step = true;
            }
            AVFrame* frame = scrub_decoder.poll_frame();
            if (frame) {
                double pts = (frame->pts != AV_NOPTS_VALUE)
                    ? frame->pts * av_q2d(video_tb)
                    : last_pts;
                if (pts != AV_NOPTS_VALUE)
                    scrub_display_pts = pts;     // keep thumb highlight in sync
                renderer.upload(frame);
                av_frame_free(&frame);
                has_frame         = true;
                last_pts          = pts;
                last_seek_pos     = pts;         // freeze seek-pos for thumbnail
                last_any_seek_at  = now;         // show thumb strip briefly
                ++frame_count;
                pending_back_step = false;
                s_back_steps.fetch_sub(1);
                player_ui.toast(std::format("Frame {} \u00B7 {}", std::llround(pts * stream_fps), toast_clock(pts)));
            }
        }

        // ── ImGui: begin frame (before any GL drawing) ────────────────────
        player_ui.begin_frame();

        // ── Draw with correct aspect ratio ────────────────────────────────
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);

        int win_w_for_scale, win_h_for_scale;
        glfwGetWindowSize(window, &win_w_for_scale, &win_h_for_scale);
        float global_ps = (win_w_for_scale > 0)
            ? static_cast<float>(fb_w) / static_cast<float>(win_w_for_scale) : 1.0f;

        // The video always fills the whole window; all chrome is overlay.
        int vx, vy, vw, vh;
        compute_video_rect(fb_w, fb_h, vid_w, vid_h, vx, vy, vw, vh);

        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (has_frame) {
            glViewport(vx, vy, vw, vh);
            renderer.draw();

            // QA snapshot: grab the video-viewport region after N frames drawn,
            // write PNG, exit. glFinish() ensures the draw is done; the negative
            // stride passed to stbi_write_png flips GL's bottom-up rows.
            if (!snapshot_path.empty() && frame_count >= snapshot_at_frame && qa_open_done) {
                glFinish();
                std::vector<std::uint8_t> px(static_cast<std::size_t>(vw) * vh * 3);
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(vx, vy, vw, vh, GL_RGB, GL_UNSIGNED_BYTE, px.data());
                stbi_flip_vertically_on_write(1);
                int ok = stbi_write_png(snapshot_path.c_str(), vw, vh, 3, px.data(), vw * 3);
                if (ok) {
                    logger::info("--snapshot: wrote {} ({}×{}, frame {})",
                        snapshot_path, vw, vh, frame_count);
                } else {
                    logger::error("--snapshot: stbi_write_png failed for {}", snapshot_path);
                }
                std::exit(0);
            }
        }

        // ── Bottom stack layout (fb px) ───────────────────────────────────
        // One base for every bottom-docked engineer strip/panel: the eased
        // control-stack height (or the mini-progress line when hidden).
        const float bottom_stack_base_fb = player_ui.visible_bar_height_logical() * global_ps;
        const bool  show_timeline  = s_show_network.load() && !manifest.tags().empty() && duration > 0.0;
        const bool  show_wave      = s_show_waveform.load() && has_audio;
        const bool  show_filmstrip = s_show_filmstrip.load() && duration > 0.0
            && (scrubbing || now - last_any_seek_at < std::chrono::milliseconds(1500));
        float strips_fb = 0.0f;   // engineer strips stacked on top of the controls
        if (show_timeline)  strips_fb += static_cast<float>(DebugHUD::kTimelinePx) * global_ps;
        if (show_wave)      strips_fb += static_cast<float>(DebugHUD::kWaveformPx) * global_ps;
        if (show_filmstrip) strips_fb += static_cast<float>(ThumbnailStrip::kStripPx) + 10.0f * global_ps;
        debug_hud.set_insets(PlayerUI::top_inset_logical() * global_ps, bottom_stack_base_fb);

        // ── Debug HUD (D key) ─────────────────────────────────────────────
        if (s_show_hud.load()) {
            // av_diff: meaningful only during active playback.
            // paused  → NaN displayed as "--"
            // no audio → NaN displayed as "N/A" (handled in DebugHUD via separate flag)
            // Device latency is wall time; at speed× it covers speed× as much media.
            double av_diff = (has_audio && !paused)
                ? (last_pts - audio_player.clock() + audio_player.output_latency_seconds() * speed) * 1000.0
                : std::numeric_limits<double>::quiet_NaN();

            debug_hud.draw(
                { last_pts, frame_count, av_diff, has_audio, ms.decode_time_ms.load() },
                fb_w, fb_h, global_ps);
        }

        // ── Stream inspector (I key) ──────────────────────────────────────
        if (s_show_inspector.load()) {
            // Refresh the Decode line so it flips to "VideoToolbox" once the
            // first HW frame lands (hw_active_ turns on inside Decoder::pull).
            if (decode_line_idx < inspector_lines.size()) {
                inspector_lines[decode_line_idx].text =
                    std::format("  Decode   {}", video_decoder.hw_type_name());
            }
            debug_hud.draw_inspector(inspector_lines, fb_w, fb_h, global_ps);
        }

        // ── A/V drift graph (G key) ───────────────────────────────────────
        {
            // Push every frame so the time axis advances correctly even during pause.
            double av_diff = (has_audio && !paused)
                ? (last_pts - audio_player.clock() + audio_player.output_latency_seconds() * speed) * 1000.0
                : std::numeric_limits<double>::quiet_NaN();
            debug_hud.push_drift(av_diff);
        }
        if (s_show_drift.load())
            debug_hud.draw_drift_graph(fb_w, fb_h, global_ps,
                                       bottom_stack_base_fb + strips_fb + 8.0f * global_ps);

        // ── Track selector (T key) ────────────────────────────────────────
        if (s_show_tracks.load()) {
            // Convert window-space cursor/click → framebuffer space.
            float cx = static_cast<float>(s_cursor_x.load()) * global_ps;
            float cy = static_cast<float>(s_cursor_y.load()) * global_ps;
            double raw_click_x = s_click_x.exchange(-1.0);
            double raw_click_y = s_click_y.exchange(-1.0);
            float  click_x = (raw_click_x >= 0.0) ? static_cast<float>(raw_click_x) * global_ps : -1.0f;
            float  click_y = (raw_click_y >= 0.0) ? static_cast<float>(raw_click_y) * global_ps : -1.0f;

            auto tc = debug_hud.draw_tracks(
                audio_track_items,    cur_audio_stream,
                video_track_items,    cur_video_stream,
                subtitle_track_items, cur_subtitle_stream,
                fb_w, fb_h, global_ps,
                click_x, click_y, cx, cy);

            if (tc.audio >= 0 && tc.audio != cur_audio_stream)
                switch_audio(tc.audio, paused, audio_clock_valid_at);
            if (tc.video >= 0 && tc.video != cur_video_stream) {
                cur_video_stream = tc.video;
                demuxer.request_video_switch(tc.video);
                AVFrame* stale;
                while (frameq.try_pop(stale)) av_frame_free(&stale);
            }
            // Subtitle: -2 = "Off" clicked (disable), >=0 = pick stream.
            // Routed through apply_sub_choice so every selection path agrees.
            if (tc.subtitle == -2 && sub_active >= 0) {
                apply_sub_choice(-1, SubBy::User);
            } else if (tc.subtitle >= 0 && tc.subtitle != cur_subtitle_stream) {
                for (std::size_t i = 0; i < sub_choices.size(); ++i)
                    if (sub_choices[i].kind == SubChoice::Embedded && sub_choices[i].stream_idx == tc.subtitle)
                        apply_sub_choice(static_cast<int>(i), SubBy::User);
            }
        } else {
            // Discard any pending click while selector is hidden.
            s_click_x.store(-1.0);
            s_click_y.store(-1.0);
        }

        // Upload newly decoded thumbnails — skip during active scrubbing so that
        // a thumbnail arriving closer to the cursor doesn't cause the highlight
        // to jump while the mouse is held still.
        if (!scrubbing)
            thumb_strip.upload_pending();

        // Keyframe list for the seek-bar ticks: copied once it exists, and once
        // more after the thumbnail thread finishes (it corrects DTS→PTS offsets).
        if (kf_pts_ui.empty() || (!kf_pts_final && thumb_strip.ready())) {
            if (thumb_strip.ready()) kf_pts_final = true;
            kf_pts_ui = thumb_strip.keyframes_copy();
        }

        // bar_pts: position shown in the seek bar and used for the thumbnail strip.
        //   During scrubbing   → cursor position (real-time feedback).
        //   Post-seek settling → last_seek_pos, held until decoded PTS catches up.
        //     (avformat_seek_file snaps to the keyframe *before* the target, so the
        //      first decoded PTS is typically earlier than the seek target.  If we
        //      used last_pts directly, the bar would jump backward on the first frame
        //      after seek and then advance back — visually confusing.)
        //   Normal playback    → last_pts (advances with decoded frames).
        // thumb_pts: PTS of the frame actually decoded and displayed — used for the
        //   thumbnail highlight.  This is the keyframe the decoder snapped to, which
        //   can differ from bar_pts by up to one keyframe interval.  Using thumb_pts
        //   keeps the highlight in sync with what the user actually sees.
        double bar_pts;
        if (scrubbing && last_scrub_frac >= 0.0)
            bar_pts = last_scrub_frac * duration;
        else if (last_pts >= last_seek_pos)
            bar_pts = last_pts;       // normal playback: decoded PTS has passed the seek target
        else
            bar_pts = last_seek_pos;  // post-seek: hold at target until decoder catches up

        double thumb_pts;
        if (scrubbing && scrub_display_pts >= 0.0)
            thumb_pts = scrub_display_pts;  // exactly what the decoder showed
        else
            thumb_pts = bar_pts;            // fallback: cursor pos or last seek

        // ── Bottom stack: engineer strips above the controls ──────────────
        // Lowest → highest: timeline ticks → waveform → keyframe filmstrip.
        {
            const float ps = global_ps;
            float bottom_off = bottom_stack_base_fb;

            // ── Timeline ticks (N key, only when manifest has tags) ───────────
            const auto& mtags = manifest.tags();
            if (show_timeline) {
                // Build TagEntry list from ManifestTag list
                std::vector<DebugHUD::TagEntry> tag_entries;
                for (const auto& t : mtags) {
                    float r = 0.9f, g = 0.3f, b = 0.3f;  // red default
                    std::string lbl = t.raw;
                    switch (t.kind) {
                        case ManifestTagKind::Discontinuity:
                            r=0.9f; g=0.25f; b=0.25f; lbl = "DISCONTINUITY"; break;
                        case ManifestTagKind::CueOut:
                            r=0.9f; g=0.65f; b=0.10f; lbl = "CUE-OUT " + t.value; break;
                        case ManifestTagKind::CueIn:
                            r=0.20f; g=0.80f; b=0.30f; lbl = "CUE-IN"; break;
                        case ManifestTagKind::Period:
                            r=0.50f; g=0.50f; b=0.90f; lbl = t.value; break;
                        case ManifestTagKind::Event:
                            r=0.70f; g=0.70f; b=0.20f; lbl = t.value; break;
                        case ManifestTagKind::Map:
                            r=0.60f; g=0.60f; b=0.60f; lbl = "MAP"; break;
                        case ManifestTagKind::ProgramDateTime:
                            r=0.50f; g=0.70f; b=0.70f;
                            lbl = "DATE-TIME " + t.value.substr(0, 16);
                            break;
                        default: r=0.55f; g=0.55f; b=0.55f; break;
                    }
                    tag_entries.push_back({ t.pts, lbl, r, g, b });
                }

                // Consume any pending click for the timeline zone
                double raw_clk_x = s_click_x.load();
                double raw_clk_y = s_click_y.load();
                float  clk_x     = (raw_clk_x >= 0.0) ? static_cast<float>(raw_clk_x) * ps : -1.0f;
                float  clk_y     = (raw_clk_y >= 0.0) ? static_cast<float>(raw_clk_y) * ps : -1.0f;

                double tick_seek = debug_hud.draw_timeline_ticks(
                    tag_entries, duration, fb_w, fb_h, ps,
                    bottom_off, clk_x, clk_y);

                if (tick_seek >= 0.0) {
                    // Consume click and seek
                    s_click_x.store(-1.0);
                    s_click_y.store(-1.0);
                    s_seek_delta.store(0.0);
                    last_any_seek_at = now;
                    demuxer.request_seek(tick_seek);
                    AVFrame* stale;
                    while (frameq.try_pop(stale)) av_frame_free(&stale);
                    last_pts  = tick_seek;
                    next_pts_time = now;
                    audio_clock_valid_at = now + std::chrono::milliseconds(300);
                    if (paused) release_paused_audio();
                }
                bottom_off += static_cast<float>(DebugHUD::kTimelinePx) * ps;

                // Tag inspector panel
                debug_hud.draw_manifest_tags(tag_entries, last_pts, fb_w, fb_h, ps);
            }

            // ── Waveform strip (W key) ────────────────────────────────────────
            if (show_wave) {
                static float wave_buf[AudioPlayer::kWaveCap];
                audio_player.copy_waveform(wave_buf, AudioPlayer::kWaveCap);
                debug_hud.draw_waveform(wave_buf, AudioPlayer::kWaveCap,
                                        fb_w, fb_h, ps, bottom_off);
                bottom_off += static_cast<float>(DebugHUD::kWaveformPx) * ps;
            }

            // ── Keyframe filmstrip (K key; while dragging / just after a seek) ─
            if (show_filmstrip)
                thumb_strip.draw(thumb_pts, duration, fb_w, fb_h, bottom_off + 4.0f * ps);
        }

        // ── Subtitles ─────────────────────────────────────────────────────────
        // Text path: drawn by PlayerUI (SubtitleOverlay) above the scrims and
        // lifted clear of the controls. Bitmap path: overlay scaled into the
        // video viewport (vx,vy,vw,vh). Both hidden while scrubbing.
        std::string sub_text;
        if (active_sidecar) {
            // Sidecar store holds the whole file: keep cues while scrubbing,
            // following the frame the scrub decoder actually showed.
            sub_text = active_sidecar->at((scrubbing && scrub_display_pts >= 0.0) ? scrub_display_pts : last_pts);
        } else if (!scrubbing && (demuxer.subtitle_stream_index() >= 0 || test_bitmap_sub)) {
            if (subtitle_decoder.is_text_format()) {
                sub_text = subtitle_decoder.at(last_pts);
            } else {
                auto evs = subtitle_decoder.active_bitmaps(last_pts);
                if (!evs.empty()) {
                    std::vector<DebugHUD::BitmapRect> brects;
                    int authored_w = 0, authored_h = 0;
                    for (const auto& e : evs) {
                        if (authored_w == 0) { authored_w = e.authored_w; authored_h = e.authored_h; }
                        for (const auto& r : e.rects)
                            brects.push_back({ r.x, r.y, r.w, r.h, r.rgba.data() });
                    }
                    debug_hud.draw_bitmap_subtitles(brects, authored_w, authored_h,
                                                   vx, vy, vw, vh, fb_w, fb_h,
                                                   player_ui.subtitle_lift_logical() * global_ps);
                }
            }
        }

        // ── Network log panel (N key) ─────────────────────────────────────────
        if (s_show_network.load()) {
            // Convert NetworkLogger::Entry → DebugHUD::NetLogEntry
            auto raw = net_logger.get_recent(20);
            std::vector<DebugHUD::NetLogEntry> net_entries;
            net_entries.reserve(raw.size());
            for (const auto& e : raw)
                net_entries.push_back({ e.time, e.type, e.url, e.r, e.g, e.b });
            debug_hud.draw_network_log(net_entries, fb_w, fb_h, global_ps);
        }

        // Help (H / ?) is an ImGui shortcut card built by PlayerUI;
        // DebugHUD::draw_help is kept but no longer called.

        // ── VMAF panel (V key) ────────────────────────────────────────────────
        if (s_show_vmaf.load()) {
            auto vmaf_results = vmaf_analyzer.results();
            std::vector<DebugHUD::VMAFPanelEntry> vmaf_entries;
            vmaf_entries.reserve(vmaf_results.size());
            for (const auto& r : vmaf_results)
                vmaf_entries.push_back({
                    r.label, r.vmaf_mean, r.vmaf_min, r.vmaf_p5,
                    r.per_frame, r.done, r.error
                });

            float vmaf_pos_frac = (duration > 0.0)
                ? static_cast<float>(last_pts / duration) : 0.0f;

            debug_hud.draw_vmaf_panel(vmaf_entries,
                                      vmaf_analyzer.running(), vmaf_analyzer.progress(),
                                      vmaf_pos_frac,
                                      fb_w, fb_h, global_ps,
                                      bottom_stack_base_fb + strips_fb + 8.0f * global_ps);
        }

        // ── Top-bar info chips (refreshed ~1 Hz) ─────────────────────────────
        if (now - last_info_upd >= std::chrono::seconds(1)) {
            last_info_upd = now;
            const AVCodecParameters* vpar = demuxer.video_codecpar();
            std::string codec = avcodec_get_name(vpar->codec_id);
            std::transform(codec.begin(), codec.end(), codec.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            if (const char* prof = avcodec_profile_name(vpar->codec_id, vpar->profile))
                codec += std::string(" ") + prof;
            ui_info.codec  = codec;
            ui_info.res    = std::format("{}×{}", coded_w, coded_h);
            ui_info.fps    = stream_fps > 0.0 ? fps_text(stream_fps) : "";
            ui_info.hdr    = renderer.hdr_mode() == 0 || renderer.hdr_mode() == 1;
            ui_info.range  = renderer.hdr_mode() == 0 ? "HDR10" : renderer.hdr_mode() == 1 ? "HLG" : "SDR";
        }
        // Cheap and state-dependent (HW becomes active after the first decoded
        // frame): keep these current every frame.
        ui_info.decode  = video_decoder.is_hw_active() ? "HW VT" : "SW";
        ui_info.dropped = static_cast<int>(total_dropped);

        // Overlay toggles (keys or UI) → OSD toast.
        for (auto& f : overlay_flags) {
            const bool v = f.flag->load();
            if (v != f.prev) {
                f.prev = v;
                player_ui.toast(std::format("{} {}", f.name, v ? "on" : "off"));
            }
        }
        if (!ui_osd.empty()) player_ui.toast(ui_osd);   // QA: keep the toast fresh

        // ── ImGui: build UI and render (above all GL content) ────────────────
        {
            PlayerUIInputs in;
            in.duration         = duration;
            in.cur_pts          = bar_pts;
            in.buffered_end_pts = demuxer.read_ahead_pts();
            in.fps              = stream_fps;
            in.paused           = paused;
            in.speed            = speed;
            in.volume           = has_audio ? audio_player.volume() : 0.0f;
            in.has_audio        = has_audio;
            in.show_hud         = s_show_hud.load();
            in.show_inspector   = s_show_inspector.load();
            in.show_drift       = s_show_drift.load();
            in.show_tracks      = s_show_tracks.load();
            in.show_waveform    = s_show_waveform.load();
            in.show_network     = s_show_network.load();
            in.show_help        = s_show_help.load();
            in.show_vmaf        = s_show_vmaf.load();
            in.show_filmstrip   = s_show_filmstrip.load();
            in.has_manifest_variants = !manifest.variants().empty();
            in.fullscreen       = s_fullscreen.load();
            in.info             = ui_info;
            in.audio_tracks     = &demuxer.audio_tracks();
            in.audio_idx        = cur_audio_stream;
            in.sub_choices      = &sub_choices;
            in.sub_active       = sub_active;
            in.recents          = &app.recents;
            in.thumbs           = &thumb_strip;
            in.kf_pts           = &kf_pts_ui;
            in.video_aspect     = video_aspect;
            in.video_rect       = ImVec4(static_cast<float>(vx) / global_ps,
                                         static_cast<float>(fb_h - vy - vh) / global_ps,
                                         static_cast<float>(vw) / global_ps,
                                         static_cast<float>(vh) / global_ps);
            in.extra_bottom_logical = strips_fb / global_ps;
            in.filmstrip_visible    = show_filmstrip;
            in.subtitle_text    = std::move(sub_text);
            in.force_visible    = ui_snap_mode;
            in.force_hidden     = ui_snap_mode && ui_hide_controls;
            in.debug_hover_frac = ui_hover_frac;

            auto ui = player_ui.build(in);

            // ── VMAF results ImGui window ─────────────────────────────────────
            if (s_show_vmaf.load()) {
                auto vmaf_results = vmaf_analyzer.results();
                std::vector<PlayerUI::VMAFResultEntry> vmaf_ui_entries;
                vmaf_ui_entries.reserve(vmaf_results.size());
                for (const auto& r : vmaf_results)
                    vmaf_ui_entries.push_back({
                        r.label, r.vmaf_mean, r.vmaf_min, r.vmaf_p5,
                        r.width, r.height, r.bandwidth, r.done, r.error, r.per_frame
                    });

                float vmaf_pos_frac = (duration > 0.0)
                    ? static_cast<float>(last_pts / duration) : 0.0f;

                bool export_json = player_ui.draw_vmaf_results(
                    vmaf_ui_entries,
                    vmaf_analyzer.running(), vmaf_analyzer.progress(),
                    vmaf_pos_frac);
                if (export_json) {
                    std::string json_path = open_path + ".vmaf.json";
                    vmaf_analyzer.write_json(json_path);
                    logger::info("VMAF report written to {}", json_path);
                }
            }

            player_ui.end_frame();

            // ── QA: --ui-snapshot — capture the whole window (video + chrome) ──
            if (ui_snap_mode && has_frame && ui_sub_at_landed && qa_open_done) {
                ++ui_frames;
                const double hover_t = (ui_hover_frac >= 0.0f && duration > 0.0)
                    ? ui_hover_frac * duration : -1.0;
                std::optional<ThumbnailStrip::ThumbRef> hover_thumb;
                if (hover_t >= 0.0)
                    hover_thumb = thumb_strip.nearest_at_or_before(hover_t, thumb_strip.preview_max_gap());
                const bool thumbs_ok = hover_t < 0.0 || hover_thumb || thumb_strip.all_uploaded();
                const bool subs_ok   = sub_active < 0 || !in.subtitle_text.empty()
                                    || !sub_choices[static_cast<std::size_t>(sub_active)].is_text;
                const bool timeout   = ++ui_total_frames >= 600;
                if (ui_frames >= ui_snap_frame && ((thumbs_ok && subs_ok) || timeout)) {
                    if (timeout && !(thumbs_ok && subs_ok))
                        logger::warn("ui-snapshot: timeout waiting for {}",
                                     !thumbs_ok ? "thumbnail" : "subtitle cue");
                    glFinish();
                    std::vector<std::uint8_t> px(static_cast<std::size_t>(fb_w) * fb_h * 4);
                    glReadBuffer(GL_BACK);
                    glPixelStorei(GL_PACK_ALIGNMENT, 1);
                    glReadPixels(0, 0, fb_w, fb_h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
                    for (auto i = 3u; i < px.size(); i += 4) px[i] = 255;   // opaque PNG
                    stbi_flip_vertically_on_write(1);
                    const int ok = stbi_write_png(ui_snap_path.c_str(), fb_w, fb_h, 4, px.data(), fb_w * 4);
                    std::size_t cut = std::min<std::size_t>(40, in.subtitle_text.size());
                    while (cut > 0 && cut < in.subtitle_text.size()
                           && (static_cast<unsigned char>(in.subtitle_text[cut]) & 0xC0) == 0x80) --cut;
                    std::string sub40 = in.subtitle_text.substr(0, cut);   // whole UTF-8 chars
                    std::replace(sub40.begin(), sub40.end(), '\n', ' ');
                    if (ok)
                        logger::info("ui-snapshot: wrote {} ({}×{}, frame {}, hover={}, thumb_pts={}, sub=\"{}\")",
                            ui_snap_path, fb_w, fb_h, ui_frames,
                            ui_hover_frac >= 0.0f ? std::format("{:.3f}", ui_hover_frac) : std::string("none"),
                            hover_thumb ? std::format("{:.3f}", hover_thumb->pts) : std::string("none"),
                            sub40);
                    else
                        logger::error("ui-snapshot: stbi_write_png failed for {}", ui_snap_path);
                    std::exit(ok ? 0 : 1);
                }
            }

            // Act on commands
            if (ui.quit)               glfwSetWindowShouldClose(window, GLFW_TRUE);
            if (!ui.open_source.empty()) request_open(ui.open_source);
            if (ui.play_pause_clicked) s_paused.store(!paused);
            // Suppress seek-bar commits while scrubbing is active: the scrubbing
            // path (ScrubDecoder + scrub_ended) handles everything.  Firing seeks from
            // both paths simultaneously floods audioq with flush sentinels, which
            // causes the audio decode thread to keep emptying the ring buffer after
            // release, producing the "buzz on one note" artifact.
            if (ui.seek_to >= 0.0 && !scrubbing && !scrub_ended)
                s_seek_delta.store(ui.seek_to - last_pts);

            // Esc during a drag: forget the drag position so scrub_ended (next
            // frame) commits no seek, and put the clock back where it was. The
            // main pipeline never left that position (frames were bypassed).
            if (ui.seek_cancelled) {
                last_scrub_frac = -1.0;
                last_pts        = pre_scrub_pts;
            }

            // ── ImGui seek bar drag → scrubbing integration ───────────────────
            // Route ImGui seek bar position into ScrubDecoder for video preview.
            // Also keep last_scrub_frac updated so that scrub_ended (which fires
            // next frame when ui_seek_dragging flips false) seeks to the right pos.
            if (ui.seek_bar_dragging && !ui_seek_dragging) pre_scrub_pts = last_pts;
            if (ui.seek_bar_dragging && ui.seek_bar_frac >= 0.0f && duration > 0.0) {
                float f = ui.seek_bar_frac;
                if (last_scrub_frac < 0.0 ||
                    std::abs(f - static_cast<float>(last_scrub_frac)) >= static_cast<float>(kScrubMinDelta)) {
                    last_scrub_frac = static_cast<double>(f);
                    scrub_decoder.request(f * static_cast<float>(duration));
                    last_pts = f * duration;
                }
            }
            // Carry drag state forward one frame so the scrubbing flag (read at
            // the top of the loop) sees it on the next iteration.
            ui_seek_dragging = ui.seek_bar_dragging;

            if (ui.speed_set >= 0.0)   s_speed.store(ui.speed_set);
            if (ui.volume_set >= 0.0f && has_audio) {
                audio_player.set_volume(ui.volume_set);
                player_ui.toast(ui.volume_set <= 0.0f ? std::string("Muted")
                    : std::format("Volume {}%", static_cast<int>(std::lround(ui.volume_set * 100.0f))));
            }
            if (ui.mute_toggle)       s_mute_toggle.fetch_add(1);
            if (ui.toggle_fullscreen) s_fullscreen_toggle.fetch_add(1);
            if (ui.step_key != 0)   { s_paused.store(true); s_step_frames.fetch_add(ui.step_key); }
            if (ui.step_frame > 0)  { s_paused.store(true); s_single_steps.fetch_add(1); }
            if (ui.step_frame < 0)  { s_paused.store(true); s_back_steps.fetch_add(1); }
            if (ui.audio_track_set >= 0) switch_audio(ui.audio_track_set, paused, audio_clock_valid_at);
            if (ui.sub_choice_set != -2) apply_sub_choice(ui.sub_choice_set, SubBy::User);
            if (ui.sub_style_changed)    persist_prefs();
            if (ui.sub_open_file) {
                nfdchar_t* sub_path = nullptr;
                nfdfilteritem_t filters[] = { { "Subtitles", "srt,vtt,ass,ssa" } };
                const nfdchar_t* dir = nullptr;
                std::string dir_s;
                if (local_media) {
                    dir_s = std::filesystem::path(open_path).parent_path().string();
                    if (!dir_s.empty()) dir = dir_s.c_str();
                }
                if (NFD_OpenDialogU8(&sub_path, filters, 1, dir) == NFD_OKAY) {
                    apply_sub_choice(add_sidecar_choice(sub_path), SubBy::User);
                    NFD_FreePathU8(sub_path);
                }
            }
            if (ui.toggle_hud)       s_show_hud.store(!s_show_hud.load());
            if (ui.toggle_inspector) s_show_inspector.store(!s_show_inspector.load());
            if (ui.toggle_drift)     s_show_drift.store(!s_show_drift.load());
            if (ui.toggle_tracks)    s_show_tracks.store(!s_show_tracks.load());
            if (ui.toggle_waveform)  s_show_waveform.store(!s_show_waveform.load());
            if (ui.toggle_network)   s_show_network.store(!s_show_network.load());
            if (ui.toggle_help)      s_show_help.store(!s_show_help.load());
            if (ui.toggle_vmaf)      s_show_vmaf.store(!s_show_vmaf.load());
            if (ui.toggle_filmstrip) s_show_filmstrip.store(!s_show_filmstrip.load());

            // VMAF triggers
            if (ui.vmaf_pick_ref && !vmaf_analyzer.running()) {
                nfdchar_t* ref_path = nullptr;
                nfdfilteritem_t filters[] = {
                    { "Video files", "mp4,mkv,mov,avi,m2ts,ts,m3u8" },
                    { "All files",   "*" }
                };
                if (NFD_OpenDialogU8(&ref_path, filters, 2, nullptr) == NFD_OKAY) {
                    vmaf_analyzer.start(ref_path, open_path);
                    s_show_vmaf.store(true);
                    NFD_FreePathU8(ref_path);
                }
            }
            if (ui.vmaf_analyze_manifest && !vmaf_analyzer.running()
                    && !manifest.variants().empty()) {
                vmaf_analyzer.start_manifest(manifest.variants());
                s_show_vmaf.store(true);
            }
            if (ui.vmaf_export_json) {
                std::string json_path = open_path + ".vmaf.json";
                vmaf_analyzer.write_json(json_path);
                logger::info("VMAF report written to {}", json_path);
            }
        }

        glfwSwapBuffers(window);

        // ── OSD: update window title at ~2 Hz ────────────────────────────
        if (now - last_title_upd >= std::chrono::milliseconds(500)) {
            last_title_upd = now;
            int s = static_cast<int>(last_pts);
            std::string speed_str = (speed == 1.0) ? "" : std::format("  [{:.2g}x]", speed);

            // Show "H for help" hint during the first 5 seconds after launch
            bool show_hint = (now - startup_time < std::chrono::seconds(5));
            glfwSetWindowTitle(window,
                std::format("Mirage  {:02d}:{:02d}:{:02d}{}{}{}",
                    s / 3600, (s % 3600) / 60, s % 60,
                    speed_str,
                    paused ? "  [PAUSED]" : "",
                    show_hint ? "   --  H for controls" : "").c_str());
        }

        // ── Open the requested source in place of this one ───────────────
        if (!open_req.empty() && ++open_req_frames >= 2) {
            if (has_audio) audio_player.set_paused(true);   // silent while the open blocks
            std::string err;
            auto next = open_session(app, open_req, err);
            open_req.clear();
            if (next) {
                if (has_audio) app.volume = audio_player.volume();
                return next;
            }
            logger::warn("Open failed, continuing with {}: {}", open_path, err);
            player_ui.toast(err);
            if (has_audio) audio_player.set_paused(paused);
            // The loop stood still during the open: re-anchor frame timing and
            // give the audio clock its usual settle time.
            const auto t = std::chrono::steady_clock::now();
            next_pts_time        = t;
            audio_clock_valid_at = t + std::chrono::milliseconds(300);
        }

        // ── QA: --av-report — write JSON after N wall seconds of playback ──
        // Falls back to a startup deadline so a file that never presents a
        // frame still yields a (mostly empty) report instead of hanging.
        if (!av_report_path.empty() && qa_open_done) {
            const double t     = qa_started ? std::chrono::duration<double>(now - qa_t0).count() : 0.0;
            const bool   stuck = now - startup_time > std::chrono::duration<double>(av_report_secs + 15.0);
            if ((qa_started && t >= av_report_secs) || stuck) {
                std::string segs;
                for (const auto& sg : av_segs)
                    segs += (segs.empty() ? "" : ",\n    ") + av_segment_json(sg);
                std::string req;
                static constexpr const char* kKind[] = { "speed", "pause", "play", "seek", "kf+", "kf-" };
                for (const auto& st : speed_seq)
                    req += std::format("{}{{\"{}\":{},\"at_s\":{}}}", req.empty() ? "" : ",",
                                       kKind[st.kind], st.speed, st.at_sec);
                std::string fname = std::filesystem::path(open_path).filename().string();
                std::erase_if(fname, [](char c) { return c == '"' || c == '\\'; });  // keep JSON valid
                std::ofstream f(av_report_path);
                f << std::format(
                    "{{\n  \"file\": \"{}\",\n  \"initial_speed\": {},\n  \"speed_seq\": [{}],\n"
                    "  \"started\": {},\n  \"wall_s\": {:.3f},\n  \"hw_decode\": {},\n"
                    "  \"has_audio\": {},\n  \"audio_output_latency_s\": {:.4f},\n"
                    "  \"audio_underruns\": {},\n  \"audio_silent_frames\": {},\n"
                    "  \"audio_dropped_frames\": {},\n"
                    "  \"overall\": {},\n  \"segments\": [\n    {}\n  ]\n}}\n",
                    fname, cli_speed, req,
                    qa_started, t, video_decoder.is_hw_active(), has_audio,
                    has_audio ? audio_player.output_latency_seconds() : 0.0,
                    has_audio ? audio_player.underrun_count() - qa_underruns0     : 0,
                    has_audio ? audio_player.silent_frames()  - qa_silent0        : 0,
                    has_audio ? audio_player.dropped_frames() - qa_dropped_audio0 : 0,
                    av_segment_json(av_total), segs);
                logger::info("--av-report: wrote {} ({:.1f} s, {} frames)",
                             av_report_path, t, av_total.presented);
                av_report_path.clear();
                glfwSetWindowShouldClose(window, GLFW_TRUE);   // normal shutdown path
            }
        }

        // ── Sleep ─────────────────────────────────────────────────────────
        if (pending_step_frame) {
            // Spin until the decode thread delivers the stepped frame.
            glfwWaitEventsTimeout(0.005);
        } else if (paused) {
            glfwWaitEventsTimeout(0.05);
        } else if (scrubbing) {
            // Spin with no sleep while scrubbing so frameq is drained and
            // new frames are uploaded as fast as the decode thread produces them.
            glfwPollEvents();
        } else {
            double wait = std::chrono::duration<double>(
                next_pts_time - std::chrono::steady_clock::now()).count();
            wait = std::min(wait, 0.05);
            if (wait > 0.001)
                glfwWaitEventsTimeout(wait);
            else
                glfwPollEvents();
        }
    }

    return nullptr;   // window closed
}
