#pragma once

#include "Demuxer.h"
#include "Decoder.h"
#include "AudioDecoder.h"
#include "AudioPlayer.h"
#include "VideoRenderer.h"
#include "ThumbnailStrip.h"
#include "ScrubDecoder.h"
#include "DebugHUD.h"
#include "Queue.h"
#include "ManifestParser.h"
#include "VMAFAnalyzer.h"
#include "SubtitleDecoder.h"
#include "PipelineState.h"
#include "HlsVariants.h"
#include "KeyframeIndex.h"
#include "FfOpen.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/rational.h>
}

struct MediaSessionOptions {
    bool try_hw          = true;    // VideoToolbox HW decode (--no-hw clears it)
    bool test_bitmap_sub = false;   // QA: inject a synthetic bitmap subtitle event
    // HLS over HTTP(S): open a copy of the master that lists each variant
    // playlist once when the served one repeats them (HlsMaster.h);
    // --hls-as-is clears it.
    bool hls_dedupe      = true;
};

// Everything that belongs to one opened media source: the container, the
// decoders and their threads, the queues between them, audio output, the
// video textures, the scrub/thumbnail helpers and the per-media info shown by
// the HUD panels. The window, GL context, ImGui (PlayerUI), DebugHUD and the
// NetworkLogger hook are app-level and outlive any number of sessions.
//
// Lifetime: construct, open(), use from the main thread (GL context current),
// destroy. The destructor stops and joins every thread before freeing FFmpeg,
// GL and miniaudio resources, so a new session can be opened in the same
// window right after the old one is destroyed — or while it is still alive.
class MediaSession {
public:
    MediaSession() = default;
    ~MediaSession();
    MediaSession(const MediaSession&) = delete;
    MediaSession& operator=(const MediaSession&) = delete;

    // Open `path` (file or URL), build decoders, renderer and helpers, and
    // start the demux/decode threads. On failure returns false with a
    // user-facing message in `err`; the session must then just be destroyed.
    [[nodiscard]] bool open(const std::string& path, const MediaSessionOptions& opt, std::string& err);

    // open() in two halves, so a slow (network) open can run off the render
    // thread while the current media keeps playing:
    //  * open_media — any thread: container, manifest, decoders, audio
    //    device. Blocking (an HLS master playlist can take many seconds).
    //  * start — main thread, GL context current: renderer, thumbnails,
    //    scrub decoder, panel info, and the playback threads.
    // On failure either returns false with a user-facing message in `err`.
    [[nodiscard]] bool open_media(const std::string& path, const MediaSessionOptions& opt, std::string& err);
    [[nodiscard]] bool start(const MediaSessionOptions& opt, std::string& err);
    // Any thread: make an in-progress open_media() give up as soon as it can
    // (aborts blocking network I/O). The session must then be destroyed.
    void cancel_open();

    // ── Components (main thread, except where the threads own them) ─────────
    std::string      path;
    Demuxer          demuxer;
    ManifestParser   manifest;        // HLS / DASH only
    VMAFAnalyzer     vmaf_analyzer;
    Decoder          video_decoder;
    AudioPlayer      audio_player;
    AudioDecoder     audio_decoder;
    VideoRenderer    renderer;
    // Keyframes of the video streams as the demuxer and the scrub decoder
    // read them, plus HLS segment starts from the variant playlists
    // (KeyframeIndex.h): keyframe stepping and ticks where ThumbnailStrip's
    // index doesn't apply. Before the helpers: the scrub decoder's thread
    // uses it until destroyed.
    KeyframeIndex    keyframes;
    ThumbnailStrip   thumb_strip;
    ScrubDecoder     scrub_decoder;   // dedicated single-frame decoder for scrubbing
    SubtitleDecoder  subtitle_decoder;

    static constexpr std::size_t kNetVideoPackets = 600;   // ~10 s at 60 fps
    static constexpr std::size_t kNetAudioPackets = 640;   // ~13 s of AAC
    Queue<AVPacket*> videoq{ 64 };
    Queue<AVPacket*> audioq{ 128 };
    Queue<AVPacket*> subq{ 64 };      // subtitle packets — small, reused after track switch
    Queue<AVFrame*>  frameq{ 8 };     // small — decoded frames are large

    // ── Per-media info, fixed after open() ───────────────────────────────────
    bool       has_audio = false;
    bool       local_media = true;    // not a network URL (sidecar subtitle lookup)
    double     duration  = -1.0;      // seconds, or -1 for live
    AVRational video_tb  = { 1, 1 };
    int        coded_w = 0, coded_h = 0;
    int        vid_w = 0, vid_h = 0;  // display size after SAR + rotation
    double     frame_dt = 1.0 / 60.0; // one frame, from avg_frame_rate
    double     video_start_s = 0.0;   // video stream start_time (sidecar cue offset)
    // HLS: playback opened a deduplicated copy of the master — the hls
    // demuxer's program p is EXT-X-STREAM-INF hls_kept[p] of `manifest`.
    // Empty: the master as served (program p = variant p).
    std::vector<int> hls_kept;
    std::chrono::steady_clock::time_point started_at{};   // start(): opened, threads running

    // Stream inspector panel content (static, built once); the Decode line is
    // refreshed each frame so "VideoToolbox" appears when HW activates.
    std::vector<DebugHUD::InspectorLine> inspector_lines;
    std::size_t                          decode_line_idx = 0;

    std::atomic<double> decode_time_ms{ 0.0 };   // updated by the video decode thread
    // Bumped by the video decode thread at every flush sentinel (seek or
    // track switch). Each decoded frame carries the generation, the stream it
    // was decoded from and the seek it follows in AVFrame::opaque (frame_tag)
    // and its stream time base in AVFrame::time_base, so the render thread
    // can tell when the first frame after a seek / switch is on screen.
    std::atomic<int>    video_epoch{ 0 };
    // The seek (Demuxer::request_seek serial) whose flush sentinel the video
    // decode thread took last: what it decodes from then on is from there.
    std::atomic<std::uint64_t> video_seek_applied{ 0 };
    // Audio stream the audio decoder is currently set up for (audio thread).
    std::atomic<int>    decoded_audio_stream{ -1 };
    // Render thread, right after Demuxer::request_seek: its serial. Until the
    // audio thread reaches that seek's flush sentinel, the audio it pops is
    // from before the seek and is dropped undecoded — so it never fills the
    // ring of a stopped device and blocks short of the sentinel.
    std::atomic<std::uint64_t> audio_drop_until_seek{ 0 };

    // What the decode threads are doing (startup watchdog).
    StageStats video_stats;
    StageStats audio_stats;
    // Queue depths and every thread's state right now (any thread). The
    // render-side fields (since_start_s, paused, scrubbing, focused) are
    // left for the caller.
    PipelineSnapshot pipeline_snapshot() const;

    // HLS: the EXT-X-STREAM-INF (index into manifest.variants()) being played
    // with these streams routed (network sidebar), -1 = unknown. Any thread:
    // uses only what start() gathered before the demuxer began reading (the
    // demux thread may rewrite stream metadata, e.g. timed ID3).
    int playing_hls_variant(int video_stream, int audio_stream) const;

    // Keyframe stepping and the seek bar's keyframe ticks for video stream
    // `stream` come from ThumbnailStrip's container index (not HLS, and the
    // stream that played at open — the one it indexed); else from `keyframes`.
    bool container_keyframes(int stream) const { return !is_hls_ && stream == initial_video_; }
    // Render thread: a switch to video stream `stream` was requested. Fetch
    // its variant playlist for `keyframes` (once per stream, HLS).
    void prepare_video_stream(int stream);
    // Render thread: the switch completed (the stream's first frame is on
    // screen). Scrubbing and frame-back steps decode `stream` from now on.
    void follow_video_stream(int stream);
    // Before start(): read embedded subtitle stream `stream` from the first
    // packet, its decoder open — the track playback starts with
    // (SubtitleSelect). Selected once reading has begun, it is a switch that
    // repositions the container to read the cues already passed: at the
    // start, a second fetch of the first segments.
    void preselect_subtitle(int stream) { preselected_subtitle_ = stream; }

    // AVFrame::opaque of a decoded frame: seek serial (26 bits, see
    // video_seek_applied) | decoder generation (26 bits) | stream (12 bits).
    // A seek's frames are those whose serial is at least its own: a
    // generation read at the request can be one that a flush still on its
    // way is about to bump, and frames from before the seek would then pass.
    static constexpr std::uint64_t kTagMask = (1ull << 26) - 1;
    static std::uint64_t tag_serial(std::uint64_t seek_serial) { return seek_serial & kTagMask; }
    static intptr_t frame_tag(int epoch, int stream, std::uint64_t seek_serial)
    {
        return static_cast<intptr_t>((tag_serial(seek_serial) << 38)
                                   | ((static_cast<std::uint64_t>(epoch) & kTagMask) << 12)
                                   | (static_cast<std::uint64_t>(stream) & 0xFFF));
    }
    static std::uint64_t tag_bits(const AVFrame* f) { return static_cast<std::uint64_t>(reinterpret_cast<intptr_t>(f->opaque)); }
    static int tag_epoch(const AVFrame* f)  { return static_cast<int>((tag_bits(f) >> 12) & kTagMask); }
    static int tag_stream(const AVFrame* f) { return static_cast<int>(tag_bits(f) & 0xFFF); }
    // The frame comes from seek `serial` (request_seek) or a later one.
    static bool tag_after_seek(const AVFrame* f, std::uint64_t serial) { return (tag_bits(f) >> 38) >= tag_serial(serial); }

private:
    bool try_hw_ = true;
    std::atomic<bool> cancelled_{ false };
    std::atomic<bool> manifest_abort_{ false };   // the manifest fetch gives up (cancel / failed open)
    std::string       hls_master_;                // the master copy playback opened (hls_kept), for the helpers
    std::vector<hls::ProgramInfo> hls_programs_;  // programs of the HLS context, at start()
    std::vector<int>  hls_video_variant_;         // per stream: hls::variant_index_for, at start()
    bool              is_hls_        = false;
    int               initial_video_ = -1;        // video stream playing at open
    int               preselected_subtitle_ = -1; // preselect_subtitle
    // Per stream, gathered at start() (the demux thread may later rewrite
    // codec parameters): what a helper opening the stream's own variant
    // playlist checks it against, the time base, and the media playlist
    // (HLS) its keyframe segment starts come from ("" = none).
    std::vector<ffopen::VariantInput> video_inputs_;
    std::vector<AVRational>           stream_tb_;
    std::vector<std::string>          playlist_url_;
    // Media playlists to fetch for `keyframes`, in order, on playlist_thread_.
    std::mutex                  pl_mtx_;
    std::condition_variable_any pl_cv_;
    std::vector<int>            pl_queue_;
    std::vector<char>           pl_asked_;   // per stream: queued once

    std::jthread demux_thread_;
    std::jthread video_thread_;
    std::jthread audio_thread_;
    std::jthread subtitle_thread_;
    std::jthread playlist_thread_;

    void build_info();
    void start_threads();
    void playlist_loop(std::stop_token st);
    // `text` (stream's media playlist, from `origin`) → keyframes.set_segments.
    void use_media_playlist(int stream, const std::string& text, const std::string& origin);
    // Stop request → unblock queues / audio → join → free queued items.
    void stop();
};
