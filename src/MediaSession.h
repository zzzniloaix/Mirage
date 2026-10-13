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

#include <atomic>
#include <cstddef>
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

    // ── Components (main thread, except where the threads own them) ─────────
    std::string      path;
    Demuxer          demuxer;
    ManifestParser   manifest;        // HLS / DASH only
    VMAFAnalyzer     vmaf_analyzer;
    Decoder          video_decoder;
    AudioPlayer      audio_player;
    AudioDecoder     audio_decoder;
    VideoRenderer    renderer;
    ThumbnailStrip   thumb_strip;
    ScrubDecoder     scrub_decoder;   // dedicated single-frame decoder for scrubbing
    SubtitleDecoder  subtitle_decoder;

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

    // Stream inspector panel content (static, built once); the Decode line is
    // refreshed each frame so "VideoToolbox" appears when HW activates.
    std::vector<DebugHUD::InspectorLine> inspector_lines;
    std::size_t                          decode_line_idx = 0;
    // Track selector items (labels for each stream).
    std::vector<DebugHUD::TrackItem> audio_track_items;
    std::vector<DebugHUD::TrackItem> video_track_items;
    std::vector<DebugHUD::TrackItem> subtitle_track_items;

    std::atomic<double> decode_time_ms{ 0.0 };   // updated by the video decode thread

private:
    bool try_hw_ = true;

    std::jthread demux_thread_;
    std::jthread video_thread_;
    std::jthread audio_thread_;
    std::jthread subtitle_thread_;

    void build_info();
    void start_threads();
    // Stop request → unblock queues / audio → join → free queued items.
    void stop();
};
