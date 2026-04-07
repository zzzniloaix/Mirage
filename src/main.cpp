#include <glad/gl.h>
#include <GLFW/glfw3.h>

#include "Demuxer.h"
#include "Decoder.h"
#include "AudioDecoder.h"
#include "AudioPlayer.h"
#include "VideoRenderer.h"
#include "ScrubBar.h"
#include "Clock.h"
#include "Sync.h"
#include "Queue.h"
#include "Logger.h"

#include <thread>
#include <chrono>
#include <cstdlib>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/frame.h>
}

static std::atomic<double> s_seek_delta{ 0.0 };
static std::atomic<bool>   s_paused{ false };
static std::atomic<double> s_speed{ 1.0 };
static std::atomic<bool>   s_scrubbing{ false };
static std::atomic<double> s_scrub_frac{ -1.0 };  // [0,1], negative = no pending scrub

static constexpr double kSpeedPresets[] = { 0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 4.0 };
static constexpr int    kNumPresets     = static_cast<int>(std::size(kSpeedPresets));

static void on_key(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    if (action != GLFW_PRESS) return;
    if (key == GLFW_KEY_ESCAPE || key == GLFW_KEY_Q)
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    else if (key == GLFW_KEY_SPACE) s_paused.store(!s_paused.load());
    else if (key == GLFW_KEY_RIGHT) s_seek_delta.store(+10.0);
    else if (key == GLFW_KEY_LEFT)  s_seek_delta.store(-10.0);
    else if (key == GLFW_KEY_UP)    s_seek_delta.store(+60.0);
    else if (key == GLFW_KEY_DOWN)  s_seek_delta.store(-60.0);
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
    if (button != GLFW_MOUSE_BUTTON_LEFT) return;

    if (action == GLFW_PRESS) {
        int win_w, win_h;
        glfwGetWindowSize(window, &win_w, &win_h);
        double mx, my;
        glfwGetCursorPos(window, &mx, &my);
        if (my >= win_h - kBarWinPx && win_w > 0) {
            s_scrubbing.store(true);
            s_scrub_frac.store(std::clamp(mx / win_w, 0.0, 1.0));
        }
    } else if (action == GLFW_RELEASE) {
        s_scrubbing.store(false);
    }
}

static void on_cursor_pos(GLFWwindow* window, double mx, double /*my*/)
{
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
                               Queue<AVPacket*>& videoq,
                               Queue<AVFrame*>&  frameq,
                               std::stop_token   st)
{
    AVFrame* frame = av_frame_alloc();

    while (!st.stop_requested()) {
        AVPacket* pkt = nullptr;
        if (!videoq.pop(pkt)) break;  // shut down or EOF sentinel

        if (pkt == nullptr) {
            // EOF sentinel — flush decoder
            decoder.push(nullptr);
        } else if (pkt == flush_sentinel()) {
            // Seek sentinel — flush codec and drop stale frames
            decoder.flush();
            AVFrame* stale;
            while (frameq.try_pop(stale)) av_frame_free(&stale);
            continue;
        } else {
            decoder.push(pkt);
            av_packet_free(&pkt);
        }

        while (decoder.pull(frame)) {
            AVFrame* out = av_frame_clone(frame);
            if (!frameq.push(out)) {
                av_frame_free(&out);
                break;
            }
            av_frame_unref(frame);
        }
    }

    av_frame_free(&frame);
}

// Audio decode thread: pops packets from audioq, decodes into AudioPlayer ring buffer.
static void audio_decode_loop(AudioDecoder&     audio_decoder,
                               AudioPlayer&      audio_player,
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
            continue;
        }
        audio_decoder.decode(pkt, audio_player);
        av_packet_free(&pkt);
    }
}

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::fprintf(stderr, "Usage: mirage <file-or-url>\n");
        return EXIT_FAILURE;
    }

    // ── Open container ────────────────────────────────────────────────────────
    Demuxer demuxer;
    if (!demuxer.open(argv[1]))
        return EXIT_FAILURE;

    if (demuxer.video_stream_index() < 0) {
        logger::error("No video stream — nothing to display");
        return EXIT_FAILURE;
    }

    // ── Decoders ──────────────────────────────────────────────────────────────
    Decoder video_decoder;
    if (!video_decoder.open(demuxer.video_codecpar()))
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

    glfwSetKeyCallback(window, on_key);
    glfwSetFramebufferSizeCallback(window, on_framebuffer_resize);
    glfwSetMouseButtonCallback(window, on_mouse_button);
    glfwSetCursorPosCallback(window, on_cursor_pos);

    {
        int w, h;
        glfwGetFramebufferSize(window, &w, &h);
        glViewport(0, 0, w, h);
    }

    VideoRenderer renderer;
    if (!renderer.init(video_decoder.width(), video_decoder.height(),
                       video_decoder.pixel_format(),
                       video_decoder.colorspace(),
                       video_decoder.color_range())) {
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

    const double duration = demuxer.duration();  // seconds, or -1 for live

    // ── Queues ────────────────────────────────────────────────────────────────
    Queue<AVPacket*> videoq(64);
    Queue<AVPacket*> audioq(128);
    Queue<AVFrame*>  frameq(8);   // small — decoded frames are large

    // ── Threads ───────────────────────────────────────────────────────────────
    std::jthread demux_thread([&](std::stop_token st) {
        demuxer.read_loop(st, videoq, audioq);
        videoq.shutdown();
        audioq.shutdown();
    });

    std::jthread video_thread([&](std::stop_token st) {
        video_decode_loop(video_decoder, videoq, frameq, st);
        frameq.shutdown();
    });

    std::jthread audio_thread([&](std::stop_token st) {
        if (has_audio)
            audio_decode_loop(audio_decoder, audio_player, audioq, st);
    });

    // ── A/V sync state ────────────────────────────────────────────────────────
    MasterClock master_clock;
    double last_pts   = 0.0;
    double last_delay = 1.0 / 30.0;  // initial guess (30fps)
    bool   has_frame  = false;
    bool   was_paused = false;
    double cur_speed  = 1.0;

    const int vid_w = video_decoder.width();
    const int vid_h = video_decoder.height();

    // When to show the next frame (wall clock)
    auto next_pts_time  = std::chrono::steady_clock::now();
    auto last_title_upd = std::chrono::steady_clock::now();
    // After a seek, ignore the audio clock until it has settled at the new position.
    auto audio_clock_valid_at = std::chrono::steady_clock::now();

    // Scrub state
    bool   was_scrubbing    = false;
    bool   audio_was_playing = false;  // was audio running when scrub started?
    double last_scrub_frac  = -1.0;   // latest drag position (kept for rate-limiting)
    auto   last_scrub_seek_at = std::chrono::steady_clock::time_point{};

    // No vsync — we manage timing ourselves via glfwWaitEventsTimeout
    glfwSwapInterval(0);

    // ── Render loop ───────────────────────────────────────────────────────────
    while (!glfwWindowShouldClose(window)) {
        auto now    = std::chrono::steady_clock::now();
        bool paused = s_paused.load();

        // ── Pause state change ────────────────────────────────────────────
        if (paused != was_paused) {
            was_paused = paused;
            if (has_audio) audio_player.set_paused(paused);
        }

        // ── Scrub state transitions ───────────────────────────────────────
        bool scrubbing = s_scrubbing.load();

        // Always consume the latest drag position so it doesn't go stale.
        {
            double f = s_scrub_frac.exchange(-1.0);
            if (f >= 0.0) last_scrub_frac = f;
        }

        bool scrub_started = scrubbing && !was_scrubbing;
        bool scrub_ended   = !scrubbing && was_scrubbing;

        if (scrub_started) {
            // Mute audio for the duration of the drag so old buffered samples
            // don't play over positions the user has already passed.
            audio_was_playing = has_audio && !paused;
            if (audio_was_playing) audio_player.set_paused(true);
        }

        // ── Seek handling (keyboard delta + scrub bar) ────────────────────
        double seek_target = -1.0;

        double delta = s_seek_delta.exchange(0.0);
        if (delta != 0.0)
            seek_target = std::max(0.0, last_pts + delta);

        if (scrubbing && last_scrub_frac >= 0.0 && duration > 0.0) {
            if (now - last_scrub_seek_at >= std::chrono::milliseconds(50)) {
                seek_target        = last_scrub_frac * duration;
                last_scrub_seek_at = now;
            }
        }

        // On scrub release: always seek to the exact drop position.
        if (scrub_ended && last_scrub_frac >= 0.0 && duration > 0.0) {
            seek_target    = last_scrub_frac * duration;
            last_scrub_frac = -1.0;
        }

        if (seek_target >= 0.0) {
            demuxer.request_seek(seek_target);
            AVFrame* stale;
            while (frameq.try_pop(stale)) av_frame_free(&stale);
            // Keep has_frame = true so the last decoded frame stays on screen
            // while the pipeline seeks. This avoids blank-screen flicker
            // (VLC-style: show last frame until a new one arrives).
            last_pts      = seek_target;
            last_delay    = 1.0 / 30.0;
            next_pts_time = now;
            audio_clock_valid_at = now + std::chrono::milliseconds(300);
        }

        // Scrub ended: flush stale audio and resume from the new position.
        // audio_player is stopped so the flush is safe (no callback racing).
        if (scrub_ended) {
            if (has_audio) {
                audio_player.flush();
                if (audio_was_playing && !paused) audio_player.set_paused(false);
            }
            audio_clock_valid_at = now + std::chrono::milliseconds(400);
            next_pts_time = now;  // immediately resume normal frame advance
        }

        was_scrubbing = scrubbing;

        // ── Speed change ──────────────────────────────────────────────────
        double speed = s_speed.load();
        if (speed != cur_speed) {
            cur_speed = speed;
            if (has_audio) audio_decoder.set_speed(speed);
        }

        // ── Frame advance ─────────────────────────────────────────────────
        if (scrubbing) {
            // TODO: scrubbing smoothness — still feels choppy despite the
            // drain-until-target approach and spin loop.  Root cause is likely
            // that av_seek_frame(AVSEEK_FLAG_BACKWARD) always restarts the
            // decode pipeline from the nearest I-frame, and the entire
            // flush→seek→decode chain has too much latency per seek cycle.
            // Possible approaches to investigate:
            //   1. avformat_seek_file() with a tight [min_ts, ts, max_ts]
            //      window to get more precise keyframe alignment.
            //   2. A dedicated "thumbnail" thread that decodes a single frame
            //      to a scratch buffer independently of the playback pipeline,
            //      so normal decode state is not disturbed during scrubbing.
            //   3. Pre-built keyframe index (libav exposes keyframe positions)
            //      to skip seeking entirely and jump straight to the right GOP.
            //   4. avcodec_flush_buffers() + re-send only the needed packets
            //      instead of a full pipeline restart through the demux thread.
            if (now >= next_pts_time) {
                const double target_pts = last_pts;  // set by seek block
                AVFrame* frame  = nullptr;
                bool     found  = false;

                while (frameq.try_pop(frame)) {
                    double pts = (frame->pts != AV_NOPTS_VALUE)
                        ? frame->pts * av_q2d(video_tb) : target_pts;

                    if (!found && pts >= target_pts - 0.001) {
                        // At or past the target (1 ms tolerance for fp rounding).
                        renderer.upload(frame);
                        has_frame = true;
                        found     = true;
                    }
                    av_frame_free(&frame);
                }

                // found → freeze until next seek fires.
                // !found → loop again immediately (no sleep; render loop spins).
                next_pts_time = found
                    ? now + std::chrono::hours(24)
                    : now;
            }
        } else if (!paused && now >= next_pts_time) {
            // Normal playback
            AVFrame* frame = nullptr;
            if (frameq.try_pop(frame)) {
                double pts = (frame->pts != AV_NOPTS_VALUE)
                    ? frame->pts * av_q2d(video_tb)
                    : last_pts + last_delay;

                // Audio clock is master once it has settled after a seek.
                // During the grace period use video PTS to avoid the sync
                // algorithm stalling on the clock-at-zero transient.
                bool audio_settled = has_audio && now >= audio_clock_valid_at;
                double master = audio_settled ? audio_player.clock() : pts;
                master_clock.set_audio(master);
                master_clock.set_video(pts);

                double delay = compute_video_delay(pts, last_pts, last_delay, master);
                delay = std::clamp(delay / speed, 0.001, 0.5);

                next_pts_time = now + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                          std::chrono::duration<double>(delay));
                last_delay = delay;
                last_pts   = pts;

                renderer.upload(frame);
                av_frame_free(&frame);
                has_frame = true;
            } else {
                next_pts_time = now + std::chrono::milliseconds(10);
            }
        }

        // ── Draw with correct aspect ratio ────────────────────────────────
        int fb_w, fb_h;
        glfwGetFramebufferSize(window, &fb_w, &fb_h);

        int vx, vy, vw, vh;
        compute_video_rect(fb_w, fb_h, vid_w, vid_h, vx, vy, vw, vh);

        glViewport(0, 0, fb_w, fb_h);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        if (has_frame) {
            glViewport(vx, vy, vw, vh);
            renderer.draw();
        }

        // During scrubbing the playhead tracks the cursor exactly (60 Hz),
        // decoupled from the 12 Hz seek/decode cycle so it never jitters.
        double bar_pts = (scrubbing && last_scrub_frac >= 0.0)
            ? last_scrub_frac * duration : last_pts;
        scrub_bar.draw(bar_pts, duration, fb_w, fb_h);

        glfwSwapBuffers(window);

        // ── OSD: update window title at ~2 Hz ────────────────────────────
        if (now - last_title_upd >= std::chrono::milliseconds(500)) {
            last_title_upd = now;
            int s = static_cast<int>(last_pts);
            std::string speed_str = (speed == 1.0) ? "" : std::format("  [{:.2g}x]", speed);
            glfwSetWindowTitle(window,
                std::format("Mirage  {:02d}:{:02d}:{:02d}{}{}",
                    s / 3600, (s % 3600) / 60, s % 60,
                    speed_str,
                    paused ? "  [PAUSED]" : "").c_str());
        }

        // ── Sleep ─────────────────────────────────────────────────────────
        if (paused) {
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

    demux_thread.request_stop();
    video_thread.request_stop();
    audio_thread.request_stop();
    // jthreads auto-join on destruction

    audio_player.stop();
    glfwDestroyWindow(window);
    glfwTerminate();
    return EXIT_SUCCESS;
}
