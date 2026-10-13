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

// imgui headers (needed for WantCapture* guards in GLFW callbacks)
#include "imgui.h"
#include <nfd.h>

#include <thread>
#include <chrono>
#include <cstdlib>
#include <limits>
#include <fstream>
#include <filesystem>
#include <algorithm>

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

// Set by GLFW drop callback (main thread only).
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
static std::atomic<double> s_decode_time_ms{ 0.0 };  // updated by video decode thread
static std::atomic<int>    s_step_frames{ 0 };  // +1 = fwd, -1 = bwd; computed in render loop
static std::atomic<int>    s_single_steps{ 0 }; // Shift+. requests; one decoded frame per count
static std::atomic<int>    s_back_steps{ 0 };   // Shift+, requests; one frame backward per count

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
// blocked-push paths that rapid speed switching interacts with.
struct SpeedStep {
    enum Kind { Speed, Pause, Play, Seek } kind = Speed;
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
    if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_Q)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
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
    else if (key == GLFW_KEY_V) s_show_vmaf.store(!s_show_vmaf.load());
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

// Scrub bar interaction — uses window (not framebuffer) coordinates for hit-testing.
// The bar occupies the bottom kBarWinPx logical pixels of the window.
static const int kBarWinPx = ScrubBar::kHitPx;  // window-pixel hit zone for scrub bar

static void on_mouse_button(GLFWwindow* window, int button, int action, int /*mods*/)
{
    if (g_player_ui) g_player_ui->notify_activity();
    // Let ImGui consume mouse events when it owns a window under the cursor.
    if (ImGui::GetIO().WantCaptureMouse) return;
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;

    if (action == GLFW_PRESS) {
        int win_w, win_h;
        glfwGetWindowSize(window, &win_w, &win_h);
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        if (my >= win_h - kBarWinPx && win_w > 0) {
            s_scrubbing.store(true);
            s_scrub_frac.store(std::clamp(mx / win_w, 0.0, 1.0));
        } else {
            // General click — track selector hit testing handled in render loop.
            s_click_x.store(mx);
            s_click_y.store(my);
        }
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

// Video decode thread: pops packets from videoq, decodes, pushes frames to frameq.
static void video_decode_loop(Decoder& decoder,
                               Demuxer& demuxer,
                               Queue<AVPacket*>& videoq,
                               Queue<AVFrame*>&  frameq,
                               bool              try_hw,
                               std::stop_token   st)
{
    AVFrame* frame = av_frame_alloc();

    while (!st.stop_requested()) {
        AVPacket* pkt = nullptr;
        if (!videoq.pop(pkt)) break;  // shut down or EOF sentinel

        if (pkt == nullptr) {
            // EOF — flush decoder and drain remaining buffered frames.
            decoder.push(nullptr);
            while (decoder.pull(frame)) {
                AVFrame* out = av_frame_clone(frame);
                if (!frameq.push(out)) { av_frame_free(&out); break; }
                av_frame_unref(frame);
            }
        } else if (pkt == flush_sentinel()) {
            // Seek or stream-switch sentinel — flush codec and drop stale frames.
            decoder.flush();
            AVFrame* stale;
            while (frameq.try_pop(stale)) av_frame_free(&stale);
            // If this sentinel followed a video stream switch, reopen the decoder.
            if (demuxer.consume_video_reopen())
                (void)decoder.open(demuxer.video_codecpar(), try_hw);
        } else {
            // Normal packet — decode and time.
            auto t0 = std::chrono::steady_clock::now();
            decoder.push(pkt);
            av_packet_free(&pkt);

            int pulled = 0;
            while (decoder.pull(frame)) {
                ++pulled;
                AVFrame* out = av_frame_clone(frame);
                if (!frameq.push(out)) {
                    av_frame_free(&out);
                    break;
                }
                av_frame_unref(frame);
            }

            if (pulled > 0) {
                auto t1 = std::chrono::steady_clock::now();
                s_decode_time_ms.store(
                    std::chrono::duration<double, std::milli>(t1 - t0).count()
                    / static_cast<double>(pulled));
            }
        }
    }

    av_frame_free(&frame);
}

// Audio decode thread: pops packets from audioq, decodes into AudioPlayer ring buffer.
static void audio_decode_loop(AudioDecoder&     audio_decoder,
                               AudioPlayer&      audio_player,
                               Demuxer&          demuxer,
                               Queue<AVPacket*>& audioq,
                               std::stop_token   st)
{
    while (!st.stop_requested()) {
        AVPacket* pkt = nullptr;
        if (!audioq.pop(pkt)) break;
        if (pkt == nullptr) break;  // EOF sentinel
        if (pkt == flush_sentinel()) {
            audio_decoder.flush();
            audio_player.flush();
            // If this sentinel followed an audio stream switch, reopen the decoder.
            if (demuxer.consume_audio_reopen())
                (void)audio_decoder.reopen(demuxer.audio_codecpar(), audio_player,
                                           demuxer.audio_time_base());
            continue;
        }
        audio_decoder.decode(pkt, audio_player);
        av_packet_free(&pkt);
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

// ── main ──────────────────────────────────────────────────────────────────────

int main(int argc, char* argv[])
{
    // ── GLFW + OpenGL ─────────────────────────────────────────────────────────
    if (!glfwInit()) {
        logger::error("Failed to initialise GLFW");
        return EXIT_FAILURE;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "Mirage", nullptr, nullptr);
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

    // ── Choose file: command-line arg or launcher screen ──────────────────────
    std::string open_path;
    bool        auto_vmaf     = false;
    std::string vmaf_ref_path;   // --vmaf ref.mp4 for single-file mode
    int         cli_sub_idx   = -2;   // -2 = unset, -1 = explicit off, >=0 = stream idx
    bool        test_bitmap_sub = false;  // QA: inject synthetic bitmap sub event
    bool        try_hw          = true;   // VideoToolbox HW decode on by default; --no-hw to disable
    // QA snapshot hook: `--snapshot <out.ppm> <frame_number>` captures the video
    // viewport after N frames have been drawn, writes a PPM, and exits. Used
    // to visually diff HW vs SW rendering paths without needing a human eye.
    std::string snapshot_path;
    int         snapshot_at_frame = 30;
    // QA A/V hooks: `--speed <x>`, `--speed-seq <x@t,...>`, `--av-report <out.json> <secs>`.
    double                 cli_speed = 1.0;
    std::vector<SpeedStep> speed_seq;
    std::string            av_report_path;
    double                 av_report_secs = 10.0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--vmaf") {
            auto_vmaf = true;
            if (i + 1 < argc && argv[i + 1][0] != '-')
                vmaf_ref_path = argv[++i];
        } else if (arg == "--sub" && i + 1 < argc) {
            cli_sub_idx = std::atoi(argv[++i]);
        } else if (arg == "--test-bitmap-sub") {
            test_bitmap_sub = true;
        } else if (arg == "--no-hw") {
            try_hw = false;
        } else if (arg == "--snapshot" && i + 2 < argc) {
            snapshot_path     = argv[++i];
            snapshot_at_frame = std::atoi(argv[++i]);
        } else if (arg == "--speed" && i + 1 < argc) {
            cli_speed = std::atof(argv[++i]);
            if (!is_speed_preset(cli_speed)) {
                logger::error("--speed {}: not a preset speed", argv[i]);
                return EXIT_FAILURE;
            }
        } else if (arg == "--speed-seq" && i + 1 < argc) {
            if (!parse_speed_seq(argv[++i], speed_seq)) {
                logger::error("--speed-seq '{}': expected speed@secs[,speed@secs...] with preset speeds",
                              argv[i]);
                return EXIT_FAILURE;
            }
        } else if (arg == "--av-report" && i + 2 < argc) {
            av_report_path = argv[++i];
            av_report_secs = std::atof(argv[++i]);
        } else if (open_path.empty()) {
            open_path = arg;
        }
    }

    // ── Network logger (must be installed before any FFmpeg call) ─────────────
    NetworkLogger net_logger;
    net_logger.install();

    // ── Pick a source + open container.  On failure, fall back to the launcher
    // with an error banner instead of exiting — a bad URL shouldn't kill the app.
    Demuxer       demuxer;
    auto          recents      = load_recents();
    std::string   launcher_err;

    while (true) {
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
                open_path = player_ui.draw_launcher(recents, launcher_err);
                player_ui.end_frame();

                glfwSwapBuffers(window);
            }
            if (open_path.empty()) {
                // User closed the window without choosing
                g_player_ui = nullptr;
                player_ui.shutdown();
                glfwDestroyWindow(window);
                glfwTerminate();
                return EXIT_SUCCESS;
            }
        }

        if (demuxer.open(open_path.c_str())) {
            if (demuxer.video_stream_index() >= 0) {
                save_recent(open_path);
                break;                                  // success — exit retry loop
            }
            logger::error("No video stream — nothing to display");
            launcher_err = "No video stream in: " + open_path;
            demuxer.close();
        } else {
            launcher_err = "Failed to open: " + open_path;
        }
        // Retry: clear the path so the launcher loop above re-runs.
        open_path.clear();
    }

    // ── Manifest parser (HLS / DASH only — non-fatal) ─────────────────────────
    ManifestParser manifest;
    if (ManifestParser::is_manifest(open_path.c_str())) {
        if (!manifest.parse(open_path.c_str()))
            logger::warn("ManifestParser: failed to parse {}", open_path);
    }

    // ── VMAF analyzer ─────────────────────────────────────────────────────────
    VMAFAnalyzer vmaf_analyzer;
    bool vmaf_was_running = false;  // for detecting completion (auto-export)

    if (auto_vmaf) {
        s_show_vmaf.store(true);
        if (!manifest.variants().empty()) {
            logger::info("VMAFAnalyzer: auto-starting manifest variant analysis");
            vmaf_analyzer.start_manifest(manifest.variants());
        } else if (!vmaf_ref_path.empty()) {
            logger::info("VMAFAnalyzer: auto-starting single-file analysis vs {}",
                         vmaf_ref_path);
            vmaf_analyzer.start(vmaf_ref_path, open_path);
        } else {
            logger::warn("VMAFAnalyzer: --vmaf used but no ref file or manifest variants "
                         "— open VMAF panel (V) and use Analyze menu");
        }
        vmaf_was_running = vmaf_analyzer.running();
    }

    // ── Decoders ──────────────────────────────────────────────────────────────
    Decoder video_decoder;
    if (!video_decoder.open(demuxer.video_codecpar(), try_hw))
        return EXIT_FAILURE;

    AudioPlayer  audio_player;
    AudioDecoder audio_decoder;
    const bool   has_audio = demuxer.audio_stream_index() >= 0;

    if (has_audio) {
        if (!audio_player.init(48000))           return EXIT_FAILURE;
        if (!audio_decoder.open(demuxer.audio_codecpar(), audio_player,
                                demuxer.audio_time_base()))
            return EXIT_FAILURE;
    }

    const AVRational video_tb = demuxer.video_time_base();

    VideoRenderer renderer;
    if (!renderer.init(video_decoder.width(), video_decoder.height(),
                       video_decoder.pixel_format(),
                       video_decoder.colorspace(),
                       video_decoder.color_range(),
                       video_decoder.color_transfer(),
                       video_decoder.color_primaries())) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return EXIT_FAILURE;
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

    const double duration = demuxer.duration();  // seconds, or -1 for live

    ThumbnailStrip thumb_strip;
    // Non-fatal: live streams or formats without a keyframe index won't have thumbnails.
    (void)thumb_strip.init(open_path.c_str(),
                     demuxer.video_stream_index(),
                     demuxer.video_time_base(),
                     duration,
                     video_decoder.width(),
                     video_decoder.height());

    // Dedicated single-frame decoder for smooth scrubbing.
    // Opened once; receives request() calls during mouse drags.
    ScrubDecoder scrub_decoder;
    scrub_decoder.open(open_path.c_str(),
                       demuxer.video_stream_index(),
                       demuxer.video_time_base());

    // ── Stream inspector panel content (static, built once) ──────────────────
    std::vector<DebugHUD::InspectorLine> inspector_lines;
    std::size_t decode_line_idx = 0;   // set below; refreshed each frame so
                                        // "VideoToolbox" appears when HW activates
    {
        using L = DebugHUD::InspectorLine;
        AVFormatContext* fc = demuxer.fmt_ctx();

        // Container section
        inspector_lines.push_back({ "CONTAINER", true });
        inspector_lines.push_back({ std::format("  Format   {}",
            (fc->iformat && fc->iformat->name) ? fc->iformat->name : "?") });
        double dur = demuxer.duration();
        if (dur > 0.0) {
            int d_s = static_cast<int>(dur);
            inspector_lines.push_back({ std::format("  Duration {:02d}:{:02d}:{:02d}",
                d_s / 3600, (d_s % 3600) / 60, d_s % 60) });
        } else {
            inspector_lines.push_back({ "  Duration live" });
        }

        // Video section
        inspector_lines.push_back({ " ", false });  // blank spacer
        inspector_lines.push_back({ "VIDEO", true });
        AVCodecParameters* vpar = demuxer.video_codecpar();
        inspector_lines.push_back({ std::format("  Codec    {}",
            avcodec_get_name(vpar->codec_id)) });
        inspector_lines.push_back({ std::format("  Size     {}x{}",
            video_decoder.width(), video_decoder.height()) });
        {
            AVStream* vs = fc->streams[demuxer.video_stream_index()];
            double fps = (vs->avg_frame_rate.den > 0)
                ? av_q2d(vs->avg_frame_rate) : 0.0;
            inspector_lines.push_back({ std::format("  FPS      {:.4g}", fps) });
        }
        {
            const char* pf = av_get_pix_fmt_name(video_decoder.pixel_format());
            inspector_lines.push_back({ std::format("  Pix fmt  {}", pf ? pf : "?") });
        }
        // Decode path — text is refreshed each frame in the render loop so it
        // flips from "Software" to "VideoToolbox" once the first HW frame lands.
        decode_line_idx = inspector_lines.size();
        inspector_lines.push_back({ std::format("  Decode   {}",
            try_hw ? "VideoToolbox (requested)" : "Software") });
        {
            const char* cs = av_color_space_name(video_decoder.colorspace());
            const char* cr = av_color_range_name(video_decoder.color_range());
            inspector_lines.push_back({ std::format("  Color    {} / {}",
                cs ? cs : "?", cr ? cr : "?") });
            const char* trc_name = av_color_transfer_name(video_decoder.color_transfer());
            const char* dynrange =
                renderer.hdr_mode() == 0 ? "HDR (PQ → tonemap)"  :
                renderer.hdr_mode() == 1 ? "HDR (HLG → tonemap)" : "SDR";
            inspector_lines.push_back({ std::format("  Transfer {}", trc_name ? trc_name : "?") });
            inspector_lines.push_back({ std::format("  Dynamic  {}", dynrange) });
        }

        // Audio section
        if (has_audio) {
            inspector_lines.push_back({ " ", false });  // blank spacer
            inspector_lines.push_back({ "AUDIO", true });
            AVCodecParameters* apar = demuxer.audio_codecpar();
            inspector_lines.push_back({ std::format("  Codec    {}",
                avcodec_get_name(apar->codec_id)) });
            inspector_lines.push_back({ std::format("  Rate     {} Hz",
                apar->sample_rate) });
            inspector_lines.push_back({ std::format("  Channels {}",
                apar->ch_layout.nb_channels) });
            const char* sf = av_get_sample_fmt_name(
                static_cast<AVSampleFormat>(apar->format));
            inspector_lines.push_back({ std::format("  Format   {}", sf ? sf : "?") });
        }
        (void)L{};  // suppress unused typedef warning
    }

    // ── Track selector items (built once; labels for each stream) ────────────
    std::vector<DebugHUD::TrackItem> audio_track_items;
    std::vector<DebugHUD::TrackItem> video_track_items;
    std::vector<DebugHUD::TrackItem> subtitle_track_items;
    {
        int n = 1;
        for (const auto& t : demuxer.audio_tracks()) {
            std::string lbl;
            if (!t.language.empty())
                lbl = std::format("{}. {}  {}  {}Hz  {}ch",
                    n++, t.language, t.codec_name, t.sample_rate, t.channels);
            else
                lbl = std::format("{}. Track {}  {}  {}Hz  {}ch",
                    n++, t.stream_idx, t.codec_name, t.sample_rate, t.channels);
            if (t.bit_rate > 0)
                lbl += std::format("  {}k", t.bit_rate / 1000);
            audio_track_items.push_back({ t.stream_idx, std::move(lbl) });
        }
        n = 1;
        for (const auto& t : demuxer.video_tracks()) {
            std::string lbl = std::format("{}. {}  {}x{}  {:.4g}fps",
                n++, t.codec_name, t.width, t.height, t.fps);
            if (t.bit_rate > 0)
                lbl += std::format("  {}k", t.bit_rate / 1000);
            video_track_items.push_back({ t.stream_idx, std::move(lbl) });
        }
        n = 1;
        for (const auto& t : demuxer.subtitle_tracks()) {
            std::string lbl;
            if (!t.language.empty())
                lbl = std::format("{}. {}  {}", n++, t.language, t.codec_name);
            else
                lbl = std::format("{}. Track {}  {}", n++, t.stream_idx, t.codec_name);
            if (!t.is_text) lbl += "  (bitmap)";
            subtitle_track_items.push_back({ t.stream_idx, std::move(lbl) });
        }
    }

    // ── Queues ────────────────────────────────────────────────────────────────
    Queue<AVPacket*> videoq(64);
    Queue<AVPacket*> audioq(128);
    Queue<AVPacket*> subq(64);     // subtitle packets — small, reused after track switch
    Queue<AVFrame*>  frameq(8);    // small — decoded frames are large

    // ── Subtitle decoder (lazily opened on track switch) ──────────────────────
    SubtitleDecoder subtitle_decoder;

    // ── QA-only: --test-bitmap-sub seeds a synthetic event with three rects
    // that exercise the GL render path independent of any codec/demuxer.
    // Authored on a 1920x1080 canvas; the render path scales into the video
    // viewport via authored_w/h.
    if (test_bitmap_sub) {
        auto fill = [](BitmapSubRect& r, std::uint8_t R, std::uint8_t G,
                       std::uint8_t B, std::uint8_t A) {
            r.rgba.assign(static_cast<std::size_t>(r.w) * r.h * 4, 0);
            for (int y = 0; y < r.h; ++y)
                for (int x = 0; x < r.w; ++x) {
                    std::uint8_t* p = r.rgba.data() + (y * r.w + x) * 4;
                    p[0] = R; p[1] = G; p[2] = B; p[3] = A;
                }
        };
        auto stroke_border = [](BitmapSubRect& r, int t, std::uint8_t R,
                                std::uint8_t G, std::uint8_t B) {
            for (int y = 0; y < r.h; ++y)
                for (int x = 0; x < r.w; ++x) {
                    if (x >= t && x < r.w - t && y >= t && y < r.h - t) continue;
                    std::uint8_t* p = r.rgba.data() + (y * r.w + x) * 4;
                    p[0] = R; p[1] = G; p[2] = B; p[3] = 255;
                }
        };

        BitmapSubEvent ev;
        ev.start_pts  = 0.0;
        ev.end_pts    = 1e9;          // visible for entire playback
        ev.authored_w = 1920;
        ev.authored_h = 1080;

        // Rect 1: top-left R/G/B/W swatch — verifies channel ordering.
        // Four 50px stripes side by side, opaque.
        {
            BitmapSubRect r;
            r.x = 40; r.y = 40; r.w = 200; r.h = 60;
            r.rgba.assign(static_cast<std::size_t>(r.w) * r.h * 4, 0);
            const std::uint8_t cols[4][4] = {
                {255,   0,   0, 255},   // red
                {  0, 255,   0, 255},   // green
                {  0,   0, 255, 255},   // blue
                {255, 255, 255, 255},   // white
            };
            for (int y = 0; y < r.h; ++y)
                for (int x = 0; x < r.w; ++x) {
                    int band = std::min(3, x / 50);
                    std::uint8_t* p = r.rgba.data() + (y * r.w + x) * 4;
                    p[0] = cols[band][0]; p[1] = cols[band][1];
                    p[2] = cols[band][2]; p[3] = cols[band][3];
                }
            ev.rects.push_back(std::move(r));
        }

        // Rect 2: center semi-transparent yellow box with opaque white border —
        // verifies alpha blend against underlying video.
        {
            BitmapSubRect r;
            r.x = 560; r.y = 480; r.w = 800; r.h = 120;
            fill(r, 255, 220, 0, 110);          // ~43% alpha
            stroke_border(r, 6, 255, 255, 255);
            ev.rects.push_back(std::move(r));
        }

        // Rect 3: bottom banner — horizontal alpha gradient on cyan to verify
        // per-pixel alpha across the rect; opaque white border.
        {
            BitmapSubRect r;
            r.x = 360; r.y = 920; r.w = 1200; r.h = 120;
            r.rgba.assign(static_cast<std::size_t>(r.w) * r.h * 4, 0);
            for (int y = 0; y < r.h; ++y)
                for (int x = 0; x < r.w; ++x) {
                    std::uint8_t* p = r.rgba.data() + (y * r.w + x) * 4;
                    p[0] = 0;
                    p[1] = 200;
                    p[2] = 255;
                    p[3] = static_cast<std::uint8_t>(255 * x / (r.w - 1));
                }
            stroke_border(r, 6, 255, 255, 255);
            ev.rects.push_back(std::move(r));
        }

        subtitle_decoder.inject_test_bitmap(std::move(ev));
        logger::info("--test-bitmap-sub: injected synthetic 3-rect event");
    }

    // ── Threads ───────────────────────────────────────────────────────────────
    std::jthread demux_thread([&](std::stop_token st) {
        demuxer.read_loop(st, videoq, audioq, &subq);
        videoq.shutdown();
        audioq.shutdown();
        subq.shutdown();
    });

    std::jthread video_thread([&](std::stop_token st) {
        video_decode_loop(video_decoder, demuxer, videoq, frameq, try_hw, st);
        frameq.shutdown();
    });

    std::jthread audio_thread([&](std::stop_token st) {
        if (has_audio)
            audio_decode_loop(audio_decoder, audio_player, demuxer, audioq, st);
    });

    // Subtitle decode thread idles until a track is selected (subq receives
    // no packets while subtitle_idx_ < 0). On a track switch the demuxer
    // pushes a flush sentinel into subq; the thread sees the reopen flag and
    // reinitialises the codec.
    std::jthread subtitle_thread([&](std::stop_token st) {
        while (!st.stop_requested()) {
            AVPacket* pkt = nullptr;
            if (!subq.pop(pkt)) break;
            if (pkt == nullptr) break;                   // EOF
            if (pkt == flush_sentinel()) {
                subtitle_decoder.flush();
                if (demuxer.consume_subtitle_reopen())
                    (void)subtitle_decoder.reopen(demuxer.subtitle_codecpar(),
                                                  demuxer.subtitle_time_base());
                continue;
            }
            subtitle_decoder.decode_packet(pkt);
            av_packet_free(&pkt);
        }
    });

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

    const int coded_w = video_decoder.width();
    const int coded_h = video_decoder.height();

    // Compute display dimensions applying SAR and rotation metadata.
    // Priority: stream-level SAR > codec-level SAR > 1:1 (square pixels).
    AVRational sar = demuxer.video_sar();
    if (sar.num <= 0 || sar.den <= 0) sar = video_decoder.sample_aspect_ratio();
    if (sar.num <= 0 || sar.den <= 0) sar = {1, 1};

    // Apply SAR to get the "natural" display width (height is unchanged).
    int disp_w = static_cast<int>(coded_w * static_cast<double>(sar.num) / sar.den + 0.5);
    int disp_h = coded_h;
    if (disp_w <= 0) disp_w = coded_w;  // clamp in case of bad metadata

    // Swap display dimensions if the stream carries a 90° or 270° rotation tag.
    // (Common for videos recorded on phones stored landscape with a rotation flag.)
    const double rotation = demuxer.video_rotation();
    const bool transposed = (rotation > 45.0 && rotation < 135.0)   // 90°
                         || (rotation > 225.0 && rotation < 315.0); // 270°
    const int vid_w = transposed ? disp_h : disp_w;
    const int vid_h = transposed ? disp_w : disp_h;

    // Currently active stream indices (updated when track switches complete).
    int cur_audio_stream    = demuxer.audio_stream_index();
    int cur_video_stream    = demuxer.video_stream_index();
    int cur_subtitle_stream = demuxer.subtitle_stream_index();

    // CLI: --sub N selects a subtitle stream index at startup. Validates
    // against actual available subtitle tracks; ignored if invalid.
    if (cli_sub_idx >= 0) {
        bool valid = false;
        for (const auto& t : demuxer.subtitle_tracks())
            if (t.stream_idx == cli_sub_idx) { valid = true; break; }
        if (valid) {
            demuxer.request_subtitle_switch(cli_sub_idx);
            cur_subtitle_stream = cli_sub_idx;
        } else {
            logger::warn("--sub {}: no such subtitle stream", cli_sub_idx);
        }
    }


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

    // Estimate one-frame duration from the video stream's average frame rate.
    // Falls back to ~16.67ms (60fps) if the container doesn't declare it.
    double frame_dt = 1.0 / 60.0;
    {
        AVRational afr = demuxer.fmt_ctx()->streams[demuxer.video_stream_index()]->avg_frame_rate;
        if (afr.num > 0 && afr.den > 0) frame_dt = static_cast<double>(afr.den) / afr.num;
    }
    last_frame_dur = frame_dt;

    // Seconds for a decoded frame: its PTS, else libavcodec's best-effort guess
    // (MPEG-TS without PTS on every frame), else one frame after the last one.
    auto frame_pts_sec = [&](const AVFrame* f) {
        int64_t ts = (f->pts != AV_NOPTS_VALUE) ? f->pts : f->best_effort_timestamp;
        return (ts != AV_NOPTS_VALUE) ? ts * av_q2d(video_tb) : last_pts + last_frame_dur;
    };

    // ── QA: --speed / --speed-seq / --av-report state ─────────────────────────
    if (cli_speed != 1.0) s_speed.store(cli_speed);
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
        if (delta != 0.0)
            seek_target = std::max(0.0, last_pts + delta);

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
                if (kf >= 0.0)
                    seek_target = kf;
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
                }
                logger::info("--speed-seq: t={:.2f} event {} {}", t, static_cast<int>(ev.kind), ev.speed);
            }
        }

        // ── Speed change ──────────────────────────────────────────────────
        double speed = s_speed.load();
        if (speed != cur_speed) {
            cur_speed = speed;
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
                int64_t dropped_now = 0;
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

                master_clock.set_audio(master);
                master_clock.set_video(pts);

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

        // Reserve menu bar space at the top (menu bar height is in logical pixels).
        int menu_bar_fb = static_cast<int>(player_ui.menu_bar_height() * global_ps);
        // Video occupies the fb area below the menu bar.
        int video_fb_h = fb_h - menu_bar_fb;

        int vx, vy, vw, vh;
        compute_video_rect(fb_w, video_fb_h, vid_w, vid_h, vx, vy, vw, vh);
        // In GL framebuffer coords (y=0 at bottom), the video area starts at y=0
        // and ends at y=video_fb_h. No extra y offset is needed because the menu
        // bar occupies the top of the screen (high y in GL space isn't the video area).

        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (has_frame) {
            glViewport(vx, vy, vw, vh);
            renderer.draw();

            // QA snapshot: grab the video-viewport region after N frames drawn,
            // write PNG, exit. glFinish() ensures the draw is done; the negative
            // stride passed to stbi_write_png flips GL's bottom-up rows.
            if (!snapshot_path.empty() && frame_count >= snapshot_at_frame) {
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
                { last_pts, frame_count, av_diff, has_audio, s_decode_time_ms.load() },
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
        if (s_show_drift.load()) {
            // Bottom offset: scrub bar + optional timeline + optional waveform + thumb area
            float bottom_off = player_ui.visible_bar_height_logical() * global_ps;
            if (s_show_network.load() && !manifest.tags().empty() && duration > 0.0)
                bottom_off += static_cast<float>(DebugHUD::kTimelinePx) * global_ps;
            if (s_show_waveform.load() && has_audio)
                bottom_off += static_cast<float>(DebugHUD::kWaveformPx) * global_ps;
            bottom_off += static_cast<float>(ThumbnailStrip::kStripPx) + 10.0f * global_ps;
            debug_hud.draw_drift_graph(fb_w, fb_h, global_ps, bottom_off);
        }

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

            if (tc.audio >= 0 && tc.audio != cur_audio_stream) {
                cur_audio_stream = tc.audio;
                if (has_audio) {
                    audio_player.set_paused(true);
                    demuxer.request_audio_switch(tc.audio);
                    audio_clock_valid_at = now + std::chrono::milliseconds(400);
                    if (!paused) audio_player.set_paused(false);
                }
            }
            if (tc.video >= 0 && tc.video != cur_video_stream) {
                cur_video_stream = tc.video;
                demuxer.request_video_switch(tc.video);
                AVFrame* stale;
                while (frameq.try_pop(stale)) av_frame_free(&stale);
            }
            // Subtitle: -2 = "Off" clicked (disable), >=0 = pick stream.
            if (tc.subtitle == -2 && cur_subtitle_stream >= 0) {
                cur_subtitle_stream = -1;
                demuxer.request_subtitle_switch(-1);
            } else if (tc.subtitle >= 0 && tc.subtitle != cur_subtitle_stream) {
                cur_subtitle_stream = tc.subtitle;
                demuxer.request_subtitle_switch(tc.subtitle);
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

        // ── Bottom stack: compute cumulative offset for stacked UI strips ──────
        // Each strip sits above the one below, lowest → highest:
        //   scrub bar → timeline ticks → waveform → thumbnail strip
        {
            const float ps = global_ps;

            float bottom_off = player_ui.visible_bar_height_logical() * global_ps;

            // ── Timeline ticks (N key, only when manifest has tags) ───────────
            const auto& mtags = manifest.tags();
            if (s_show_network.load() && !mtags.empty() && duration > 0.0) {
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
            if (s_show_waveform.load() && has_audio) {
                static float wave_buf[AudioPlayer::kWaveCap];
                audio_player.copy_waveform(wave_buf, AudioPlayer::kWaveCap);
                debug_hud.draw_waveform(wave_buf, AudioPlayer::kWaveCap,
                                        fb_w, fb_h, ps, bottom_off);
                bottom_off += static_cast<float>(DebugHUD::kWaveformPx) * ps;
            }

            // ── Thumbnails ────────────────────────────────────────────────────
            bool show_thumbs = scrubbing ||
                (now - last_any_seek_at < std::chrono::milliseconds(1500));
            if (show_thumbs)
                thumb_strip.draw(thumb_pts, duration, fb_w, fb_h, bottom_off);
        }

        // ── Subtitles ─────────────────────────────────────────────────────────
        // Text path: anchored just above the ImGui control bar.
        // Bitmap path: overlay scaled into the video viewport (vx,vy,vw,vh).
        // Both hidden while scrubbing.
        if (!scrubbing && (demuxer.subtitle_stream_index() >= 0 || test_bitmap_sub)) {
            if (subtitle_decoder.is_text_format()) {
                std::string sub = subtitle_decoder.at(last_pts);
                if (!sub.empty()) {
                    float sub_bottom_off = player_ui.visible_bar_height_logical() * global_ps;
                    debug_hud.draw_subtitle(sub, fb_w, fb_h, global_ps, sub_bottom_off);
                }
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
                                                   vx, vy, vw, vh, fb_w, fb_h);
                }
            }
        }

        // Custom ScrubBar replaced by VLC-style ImGui control bar.
        // scrub_bar.draw(bar_pts, duration, fb_w, fb_h);

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

        // ── Help overlay (H key) — drawn last so it's on top of everything ──
        if (s_show_help.load()) {
            debug_hud.draw_help(fb_w, fb_h, global_ps);
        }

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

            float vmaf_bottom_off = player_ui.visible_bar_height_logical() * global_ps;
            if (s_show_network.load() && !manifest.tags().empty() && duration > 0.0)
                vmaf_bottom_off += static_cast<float>(DebugHUD::kTimelinePx) * global_ps;
            if (s_show_waveform.load() && has_audio)
                vmaf_bottom_off += static_cast<float>(DebugHUD::kWaveformPx) * global_ps;
            vmaf_bottom_off += static_cast<float>(ThumbnailStrip::kStripPx) + 10.0f * global_ps;

            debug_hud.draw_vmaf_panel(vmaf_entries,
                                      vmaf_analyzer.running(), vmaf_analyzer.progress(),
                                      vmaf_pos_frac,
                                      fb_w, fb_h, global_ps, vmaf_bottom_off);
        }

        // ── ImGui: build UI and render (above all GL content) ────────────────
        {
            auto ui = player_ui.build(
                duration, bar_pts,
                paused, speed, has_audio ? audio_player.volume() : 1.0f,
                has_audio,
                s_show_hud.load(), s_show_inspector.load(), s_show_drift.load(),
                s_show_tracks.load(), s_show_waveform.load(), s_show_network.load(),
                s_show_help.load(), s_show_vmaf.load(),
                !manifest.variants().empty());

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

            // Act on commands
            if (ui.quit)               glfwSetWindowShouldClose(window, GLFW_TRUE);
            if (ui.play_pause_clicked) s_paused.store(!paused);
            // Suppress seek-bar commits while GLFW scrubbing is active: the scrubbing
            // path (ScrubDecoder + scrub_ended) handles everything.  Firing seeks from
            // both paths simultaneously floods audioq with flush sentinels, which
            // causes the audio decode thread to keep emptying the ring buffer after
            // release, producing the "buzz on one note" artifact.
            if (ui.seek_to >= 0.0 && !scrubbing && !scrub_ended)
                s_seek_delta.store(ui.seek_to - last_pts);

            // ── ImGui seek bar drag → scrubbing integration ───────────────────
            // Route ImGui seek bar position into ScrubDecoder for video preview.
            // Also keep last_scrub_frac updated so that scrub_ended (which fires
            // next frame when ui_seek_dragging flips false) seeks to the right pos.
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
            if (ui.volume_set >= 0.0f && has_audio) audio_player.set_volume(ui.volume_set);
            if (ui.toggle_hud)       s_show_hud.store(!s_show_hud.load());
            if (ui.toggle_inspector) s_show_inspector.store(!s_show_inspector.load());
            if (ui.toggle_drift)     s_show_drift.store(!s_show_drift.load());
            if (ui.toggle_tracks)    s_show_tracks.store(!s_show_tracks.load());
            if (ui.toggle_waveform)  s_show_waveform.store(!s_show_waveform.load());
            if (ui.toggle_network)   s_show_network.store(!s_show_network.load());
            if (ui.toggle_help)      s_show_help.store(!s_show_help.load());
            if (ui.toggle_vmaf)      s_show_vmaf.store(!s_show_vmaf.load());

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

        // ── QA: --av-report — write JSON after N wall seconds of playback ──
        // Falls back to a startup deadline so a file that never presents a
        // frame still yields a (mostly empty) report instead of hanging.
        if (!av_report_path.empty()) {
            const double t     = qa_started ? std::chrono::duration<double>(now - qa_t0).count() : 0.0;
            const bool   stuck = now - startup_time > std::chrono::duration<double>(av_report_secs + 15.0);
            if ((qa_started && t >= av_report_secs) || stuck) {
                std::string segs;
                for (const auto& sg : av_segs)
                    segs += (segs.empty() ? "" : ",\n    ") + av_segment_json(sg);
                std::string req;
                static constexpr const char* kKind[] = { "speed", "pause", "play", "seek" };
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

    // ── Shutdown ──────────────────────────────────────────────────────────────
    videoq.shutdown();
    audioq.shutdown();
    frameq.shutdown();
    audio_player.interrupt();   // wake an audio thread blocked on a full ring

    demux_thread.request_stop();
    video_thread.request_stop();
    audio_thread.request_stop();
    // jthreads auto-join on destruction

    audio_player.stop();
    net_logger.uninstall();
    g_player_ui = nullptr;
    player_ui.shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
