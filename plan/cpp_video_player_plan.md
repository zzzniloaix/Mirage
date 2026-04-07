# C++ Cross-Platform Video Player — Implementation Plan (Revised)
_Primary dev: macOS. Target: macOS + Windows. Updated for modern C++20 + FFmpeg 7._

---

## Goal

Build a working video player from scratch in C++ using FFmpeg for decoding,
OpenGL for rendering, and miniaudio for audio output. The player supports both
local files and adaptive streaming manifests (DASH `.mpd` / HLS `.m3u8`).
The repo compiles and runs on both macOS and Windows with minimal platform-specific code.

---

## What Makes This Different from VLC

VLC is a general-purpose player built for everyone. This player is built for
**video engineers** — the kind of person who needs to inspect a stream, not just watch it.

| Feature | VLC | This player |
|---|---|---|
| DASH / HLS manifest input | Basic | First-class, with stream inspector |
| Frame-by-frame stepping | Hacky | Proper ± 1 frame with keyboard |
| Live debug HUD | Buried in menus | Always-visible overlay |
| A/V sync drift graph | No | Live rolling graph |
| Stream inspector (codec, color space, tracks) | Separate dialog | Side panel |
| Multiple audio track switching | Yes | Yes + language labels |
| Waveform view | No | Yes |
| UI | Dated | Clean, minimal, dark |

---

## Tech Stack

| Concern | Choice | Why |
|---|---|---|
| Decode | **FFmpeg 7** | Handles all containers/codecs; modern send/receive API |
| Manifest streaming | **FFmpeg libavformat** | Built-in DASH + HLS demuxers; just pass a URL |
| Networking | **FFmpeg libavformat** + `avformat_network_init()` | HTTP/HTTPS handled internally; no extra lib needed |
| Windowing + input | **GLFW 3.4** | Lightweight, OpenGL-native, macOS + Windows |
| Rendering | **OpenGL 4.1 Core** | Highest version macOS supports; works on Windows too |
| GL loader | **GLAD2** | Generates a minimal loader for exactly the extensions you need |
| YUV→RGB | **GLSL shader + DSA** | GPU-accelerated; uses modern Direct State Access API |
| Audio | **miniaudio** | Single-header, actively maintained, wraps CoreAudio/WASAPI; no install needed |
| Audio resampling | **libswresample** (FFmpeg) | Converts decoded audio to format miniaudio expects |
| Build system | **CMake 3.28+** | CMakePresets.json, FetchContent, target-based linking |
| Package manager | **vcpkg** | Manifest mode; works on macOS + Windows |
| Language standard | **C++20** | `std::jthread`, `std::stop_token`, `std::span`, `std::format`, Concepts, Semaphores |

> **On Metal**: Skipping intentionally. OpenGL 4.1 works on both platforms with one
> codebase. Metal can be added later as a second renderer behind an abstraction layer.
>
> **On PortAudio**: Replaced with miniaudio — PortAudio development has slowed, and
> miniaudio is a single-header drop-in with no build complexity.

---

## Modern FFmpeg 7 API — Key Changes from the Tutorial

The dranger tutorial is based on FFmpeg 2.x. FFmpeg 7 has changed substantially.
These are the critical API differences you will use throughout the project.

### Stream parameters: `codecpar` not `codec`
```cpp
// OLD (dranger / FFmpeg 2)
AVCodecContext *ctx = stream->codec;

// NEW (FFmpeg 7)
AVCodecParameters *par = stream->codecpar;  // read-only codec info
AVCodecContext *ctx = avcodec_alloc_context3(codec);
avcodec_parameters_to_context(ctx, par);    // fill ctx from params
```

### Stream selection: `av_find_best_stream()`
```cpp
// OLD: manual loop checking codec_type
// NEW:
int video_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
int audio_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, video_idx, &codec, 0);
```

### Packet allocation: heap-allocated only
```cpp
// OLD: AVPacket pkt;  (stack)
// NEW:
AVPacket* pkt = av_packet_alloc();
// ... use ...
av_packet_unref(pkt);   // clear contents, reuse
av_packet_free(&pkt);   // free entirely
```

### Decode: send/receive model (no avcodec_decode_video2)
```cpp
// Send one packet in
avcodec_send_packet(ctx, pkt);

// Pull out zero or more frames
AVFrame* frame = av_frame_alloc();
while (avcodec_receive_frame(ctx, frame) == 0) {
    // process frame
    av_frame_unref(frame);  // clear for reuse
}
av_frame_free(&frame);
```

### Audio channel layout: `ch_layout` not `channels`
```cpp
// OLD (FFmpeg < 5.1)
wanted.channels      = codec_ctx->channels;
wanted.channel_layout = codec_ctx->channel_layout;

// NEW (FFmpeg 5.1+ / 7)
AVChannelLayout layout;
av_channel_layout_copy(&layout, &codec_ctx->ch_layout);
// query: layout.nb_channels
```

### DASH / HLS manifest input
FFmpeg's `avformat_open_input()` accepts URLs directly — no extra library needed.
The DASH and HLS demuxers are built into `libavformat`.

```cpp
// Must call once at startup before opening any network URL
avformat_network_init();

// Then open exactly like a local file — FFmpeg detects DASH/HLS automatically
AVFormatContext* fmt_ctx = nullptr;

// Optional: set network timeout and buffer size
AVDictionary* opts = nullptr;
av_dict_set(&opts, "timeout",        "5000000", 0);  // 5 second connect timeout (µs)
av_dict_set(&opts, "reconnect",      "1",       0);  // auto-reconnect on drop
av_dict_set(&opts, "reconnect_streamed", "1",   0);
av_dict_set(&opts, "buffer_size",    "1048576", 0);  // 1MB read buffer

avformat_open_input(&fmt_ctx, "https://example.com/manifest.mpd", nullptr, &opts);
// or:
avformat_open_input(&fmt_ctx, "https://example.com/playlist.m3u8", nullptr, &opts);
av_dict_free(&opts);
```

After `avformat_open_input`, the rest of the pipeline — `av_find_best_stream`,
`avcodec_parameters_to_context`, `av_read_frame` — is **identical** to local file
playback. FFmpeg handles segment fetching, bitrate switching, and buffering internally.

**Inspecting available representations (DASH)**
```cpp
// After avformat_find_stream_info, walk programs for DASH representations
for (unsigned i = 0; i < fmt_ctx->nb_programs; i++) {
    AVProgram* prog = fmt_ctx->programs[i];
    // prog->nb_stream_indexes — streams in this representation
    // metadata: av_dict_get(prog->metadata, "variant_bitrate", nullptr, 0)
}
```

**Track / language selection**
DASH manifests often carry multiple audio tracks (languages). After
`avformat_find_stream_info`, each audio stream has metadata you can inspect:
```cpp
for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
    if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        AVDictionaryEntry* lang = av_dict_get(
            fmt_ctx->streams[i]->metadata, "language", nullptr, 0);
        // lang->value e.g. "eng", "fra", "zho"
    }
}
```
To switch audio track: update `audioStream` index in VideoState and flush queues.

**Seeking in adaptive streams**
`av_seek_frame` works for both DASH and HLS, but may be slower (requires fetching
a new segment). Always check the return value and handle `AVERROR` gracefully.

**Cleanup**
```cpp
avformat_network_deinit();  // call once at shutdown
```

### Audio resampling: `swr_alloc_set_opts2()`
```cpp
SwrContext* swr = nullptr;
swr_alloc_set_opts2(
    &swr,
    &out_ch_layout,          // AVChannelLayout* output
    AV_SAMPLE_FMT_FLT,       // output format (float planar for miniaudio)
    target_sample_rate,
    &codec_ctx->ch_layout,   // AVChannelLayout* input
    codec_ctx->sample_fmt,
    codec_ctx->sample_rate,
    0, nullptr
);
swr_init(swr);
```

---

## C++20 Features Used in This Project

| Feature | Where used |
|---|---|
| `std::jthread` | Demuxer thread, video decode thread — auto-joins on destruction |
| `std::stop_token` / `std::stop_source` | Clean thread shutdown without a `quit` flag |
| `std::counting_semaphore` | Frame queue size control (replaces pictq_cond) |
| `std::span<uint8_t>` | Buffer views passed to audio/video without pointer+size pairs |
| `std::format` | Debug logging, on-screen OSD text |
| Concepts | `Queue<T>` template constrained to pointer-like types |
| `[[nodiscard]]` | Return values from open/init functions that must be checked |
| `[[likely]]` / `[[unlikely]]` | Hot paths in decode loop |
| `std::atomic<bool>` with `wait()` | Pause/resume signaling |
| `std::chrono` with `duration_cast` | Frame timing, sleep calculations |
| Designated initializers | Cleaner struct initialization |

---

## Repository Structure

```
cpp-video-player/
├── CMakeLists.txt
├── CMakePresets.json           # debug/release presets for macOS + Windows
├── vcpkg.json                  # ffmpeg, glfw3 — miniaudio via FetchContent
├── README.md
├── src/
│   ├── main.cpp                # GLFW window, event loop, keyboard input
│   ├── VideoPlayer.h/.cpp      # top-level orchestrator; owns all components
│   ├── Demuxer.h/.cpp          # avformat: open, find streams, read packets, seek
│   ├── Decoder.h/.cpp          # avcodec: send/receive model, software decode
│   ├── AudioDecoder.h/.cpp     # audio-specific decoder + swr resampling
│   ├── Queue.h                 # thread-safe Queue<T> template (C++20 concepts)
│   ├── AudioPlayer.h/.cpp      # miniaudio device + callback; audio clock
│   ├── VideoRenderer.h/.cpp    # OpenGL 4.1 DSA: textures, VAO, shaders
│   ├── Clock.h/.cpp            # master / audio / video clock abstraction
│   ├── Sync.h/.cpp             # A/V sync: compute display delay, adjust audio
│   ├── ManifestParser.h/.cpp   # pre-fetch + parse HLS/DASH; extracts custom tags
│   ├── NetworkLogger.h/.cpp    # intercepts av_log, captures HTTP requests + timing
│   ├── DebugHUD.h/.cpp         # OpenGL overlay: PTS, sync graph, network log panel
│   └── Logger.h                # std::format-based lightweight logger
├── shaders/
│   ├── yuv420p.vert            # fullscreen quad vertex shader
│   ├── yuv420p.frag            # YUV→RGB BT.601 / BT.709 fragment shader
│   └── hud.vert / hud.frag     # 2D overlay rendering for HUD panels
└── third_party/
    ├── miniaudio/              # fetched by CMake FetchContent (single header)
    └── pugixml/                # lightweight XML parser for DASH MPD parsing
```

---

## Component Details

### `Queue<T>` — Thread-Safe Generic Queue (C++20)
```cpp
template<typename T>
concept AvPointer = std::is_pointer_v<T>;

template<AvPointer T>
class Queue {
    std::queue<T>            queue_;
    mutable std::mutex       mutex_;
    std::condition_variable  cv_;
    std::atomic<bool>        flushing_{ false };
public:
    void push(T item);
    bool pop(T& out, bool block = true);   // false = flushing/quit
    void flush();
    int  size() const;
};
```

### `Demuxer`
- `open(std::string url_or_path)` → accepts both local file paths and HTTP(S) URLs (`.mpd`, `.m3u8`, or direct media)
- Calls `avformat_network_init()` once on construction if the input looks like a URL
- `best_stream(AVMediaType)` → wraps `av_find_best_stream()`
- `available_audio_tracks()` → returns list of `{index, language, codec}` for track switcher UI
- `select_audio_track(int idx)` → flushes queues, switches `audioStream` to new index
- `read_loop(std::stop_token)` → runs on `std::jthread`; routes to audioq / videoq; handles stalls/reconnects
- `seek(double seconds)` → `av_seek_frame` + `flush()` on both queues + push flush sentinel
- Respects `stop_token` for clean shutdown

### `NetworkLogger`
Hooks into FFmpeg's internal log system to capture every HTTP request the player makes —
manifest fetches, variant playlist loads, init segments, media segments — without needing
Charles Proxy or any external tool.

```cpp
class NetworkLogger {
public:
    struct Entry {
        std::chrono::steady_clock::time_point timestamp;
        std::string  url;           // full URL
        std::string  type;          // "manifest" | "variant" | "init" | "segment"
        int64_t      bytes = 0;
        double       duration_ms = 0.0;  // time from open to first byte
    };

    static void install();   // call once: av_log_set_callback(...)
    std::span<const Entry> recent(size_t n = 50) const;  // last N entries
};
```

Internally, `av_log_set_callback` captures lines like:
```
Opening 'https://cdn.example.com/hd/seg_00123.m4s' for reading
```
Parse the URL, classify it (`.mpd` = manifest, `.m3u8` = variant, `.mp4`/`.m4s` = segment),
record wall-clock time delta between "Opening" and the next read event on that handle.

### `ManifestParser`
Pre-fetches the manifest URL **before** handing it to FFmpeg to extract tags that
FFmpeg discards or doesn't surface. This splits into two categories:

**Category 1 — Standard tags we specifically want to visualise**

`#EXT-X-DISCONTINUITY` is a standard HLS spec tag (RFC 8216), not a custom one.
It marks a non-contiguous boundary in the stream — the timestamp sequence resets,
encoding parameters may change, and the player must re-sync. Common at ad boundaries,
content stitching points, and live-to-VOD transitions. FFmpeg handles discontinuities
internally but gives you no visibility into when they happen or whether the transition
was clean. We want to surface them explicitly.

Related tags to track alongside:
- `#EXT-X-DISCONTINUITY` — the boundary itself
- `#EXT-X-DISCONTINUITY-SEQUENCE:<n>` — top-level counter; increments across playlist refreshes
- `#EXT-X-MAP` — init segment; often changes at a discontinuity (new codec params)
- `#EXT-X-PROGRAM-DATE-TIME` — wall clock at segment start; useful to correlate across reloads
- `#EXT-X-CUE-OUT` / `#EXT-X-CUE-IN` — SCTE-35 ad cue points, often paired with discontinuities

**Category 2 — Unknown / proprietary tags**

Any `#EXT-X-` line not in the HLS spec is captured as-is so nothing is lost.

```cpp
enum class HlsTagKind {
    Discontinuity,          // #EXT-X-DISCONTINUITY
    DiscontinuitySequence,  // #EXT-X-DISCONTINUITY-SEQUENCE
    Map,                    // #EXT-X-MAP
    ProgramDateTime,        // #EXT-X-PROGRAM-DATE-TIME
    CueOut,                 // #EXT-X-CUE-OUT
    CueIn,                  // #EXT-X-CUE-IN
    Unknown,                // anything else
};

struct ManifestTag {
    double      segment_pts  = 0.0;   // start time of the segment this tag precedes
    int         disc_seq     = -1;    // discontinuity sequence number (-1 if n/a)
    HlsTagKind  kind         = HlsTagKind::Unknown;
    std::string raw;                  // full original tag line
    std::string value;                // parsed attribute string
};

class ManifestParser {
public:
    [[nodiscard]] bool parse_hls(const std::string& url);
    [[nodiscard]] bool parse_dash(const std::string& url);

    std::span<const ManifestTag> all_tags()     const;
    std::span<const ManifestTag> discontinuities() const;  // filtered to Discontinuity kind
    std::span<const ManifestTag> tags_near(double pts, double window_sec = 2.0) const;
    int current_discontinuity_sequence() const;
};
```

**HLS parsing logic:**
```cpp
double running_pts = 0.0;
int    disc_seq    = 0;

for (auto& line : lines) {
    if (line == "#EXT-X-DISCONTINUITY") {
        tags_.push_back({ running_pts, disc_seq, HlsTagKind::Discontinuity, line, "" });

    } else if (line.starts_with("#EXT-X-DISCONTINUITY-SEQUENCE:")) {
        disc_seq = std::stoi(line.substr(30));
        tags_.push_back({ running_pts, disc_seq, HlsTagKind::DiscontinuitySequence, line, line.substr(30) });

    } else if (line.starts_with("#EXTINF:")) {
        running_pts += parse_extinf_duration(line);  // accumulate segment durations

    } else if (line.starts_with("#EXT-X-CUE-OUT")) {
        tags_.push_back({ running_pts, disc_seq, HlsTagKind::CueOut, line, extract_value(line) });

    } else if (line == "#EXT-X-CUE-IN") {
        tags_.push_back({ running_pts, disc_seq, HlsTagKind::CueIn, line, "" });

    } else if (line.starts_with("#EXT-X-") && !is_standard_hls_tag(line)) {
        tags_.push_back({ running_pts, disc_seq, HlsTagKind::Unknown, line, extract_value(line) });
    }
}
```

**DASH**: pugixml walks the MPD XML for `<EventStream>` / `<Event>` elements (the DASH
equivalent of HLS cue points), `Period` boundaries (analogous to discontinuities),
and any elements in non-DASH namespaces.

These tags are surfaced in the HUD on a **timeline strip** and in the tag inspector panel.

### `DebugHUD`
Renders transparent overlay panels on top of the video using a separate 2D OpenGL pass.
All panels are independently toggleable:

```
┌──────────────────────────────────────────────────┐
│  [D] Debug          PTS: 00:01:23.456            │  ← always-on status bar
│                     Frame: 2041  disc_seq: 3     │  ← discontinuity sequence shown here
│                     A/V diff: +3ms               │
│                     Decode: 1.2ms/frame          │
├──────────────────────────────────────────────────┤
│  [N] Network Log                                 │
│  12:00:01.123  manifest  manifest.mpd      45ms  │
│  12:00:01.201  variant   1080p.m3u8        12ms  │
│  12:00:01.890  init      init-0.mp4        33ms  │
│  12:00:01.950  segment   seg_001.m4s       88ms  │  ← colour-coded: manifest/variant/
│  12:00:02.103  segment   seg_002.m4s       91ms  │    init/segment
│  12:00:02.310  segment   seg_003.m4s  !! 340ms   │  ← red = slow fetch
├──────────────────────────────────────────────────┤
│  [T] Discontinuity / Tag Inspector               │
│  ── disc_seq 2 ──────────────────────────────    │
│    00:00:00.000  DISCONTINUITY       (content)   │
│    00:00:00.000  PROGRAM-DATE-TIME   2024-03-...  │
│    00:00:14.900  CUE-OUT             duration=30  │  ← ad break incoming
│  ── disc_seq 3 ──────────────────────────────    │
│  ▶ 00:01:22.000  DISCONTINUITY       (ad→content)│  ← current position highlighted
│    00:01:22.000  MAP                 init-1.mp4  │  ← init segment changed here
│  ── disc_seq 4 ──────────────────────────────    │
│    00:02:45.000  CUE-OUT             duration=15  │
│  ── unknown tags ────────────────────────────    │
│    00:00:00.000  EXT-X-CUSTOM-FOO    bar=baz     │  ← proprietary tags below
├──────────────────────────────────────────────────┤
│  [G] A/V Sync    ~~~~^~___~~         range ±50ms │  ← rolling 10s drift line
│  [I] Inspector   H.264 1920×1080 BT.709 29.97fps │
│  [W] Waveform    ▁▂▄▇▆▄▂▁▂▃▄▅▃▂                 │
└──────────────────────────────────────────────────┘
```

Key bindings summary:
| Key | Panel |
|---|---|
| `D` | Master debug stats (PTS, frame, sync diff, decode time) |
| `N` | Network request log |
| `T` | Discontinuity + tag inspector (grouped by disc_seq) |
| `G` | A/V sync drift graph |
| `I` | Stream inspector (codec, resolution, color space, tracks) |
| `W` | Waveform strip |
| `1`–`9` | Switch audio track |
| `.` / `,` | Step forward / backward one frame |
| `Space` | Pause / resume |
| `←` `→` | Seek ±10s |
| `↑` `↓` | Seek ±60s |

### `Decoder`
```cpp
class Decoder {
    AVCodecContext* ctx_ = nullptr;
public:
    [[nodiscard]] bool open(AVCodecParameters*);
    void push(AVPacket*);   // avcodec_send_packet
    bool pull(AVFrame*);    // avcodec_receive_frame; true = got frame
    void flush();           // avcodec_flush_buffers
};
```

### `AudioDecoder`
Extends `Decoder` with an `SwrContext` to resample decoded audio:
- Input: whatever format/rate/layout the codec gives
- Output: `AV_SAMPLE_FMT_FLT`, 44100/48000 Hz, stereo (what miniaudio wants)
- Writes resampled `float` samples directly into the audio ring buffer

### `VideoRenderer` — OpenGL 4.1 with DSA
Uses **Direct State Access** (DSA) — no more `glBindTexture` before every upload:
```cpp
// Create 3 textures for Y, U, V planes — DSA style
GLuint textures[3];
glCreateTextures(GL_TEXTURE_2D, 3, textures);
glTextureStorage2D(textures[0], 1, GL_R8, width,   height);     // Y
glTextureStorage2D(textures[1], 1, GL_R8, width/2, height/2);   // U
glTextureStorage2D(textures[2], 1, GL_R8, width/2, height/2);   // V

// Upload frame data (no bind needed)
glTextureSubImage2D(textures[0], 0, 0, 0, width,   height,   GL_RED, GL_UNSIGNED_BYTE, frame->data[0]);
glTextureSubImage2D(textures[1], 0, 0, 0, width/2, height/2, GL_RED, GL_UNSIGNED_BYTE, frame->data[1]);
glTextureSubImage2D(textures[2], 0, 0, 0, width/2, height/2, GL_RED, GL_UNSIGNED_BYTE, frame->data[2]);
```

### GLSL Shaders

**yuv420p.vert** — fullscreen quad (no VBO needed, generate positions in shader):
```glsl
#version 410 core
out vec2 uv;
void main() {
    // Generate a full-screen triangle from vertex ID
    vec2 pos = vec2((gl_VertexID & 1) * 2.0 - 1.0,
                    (gl_VertexID >> 1) * 2.0 - 1.0);
    uv = pos * 0.5 + 0.5;
    uv.y = 1.0 - uv.y;    // flip Y (OpenGL origin is bottom-left)
    gl_Position = vec4(pos, 0.0, 1.0);
}
```

**yuv420p.frag** — YUV→RGB with selectable color matrix:
```glsl
#version 410 core
uniform sampler2D tex_y, tex_u, tex_v;
uniform mat3 yuv_matrix;   // pass BT.601 or BT.709 from CPU
in  vec2 uv;
out vec4 frag_color;
void main() {
    vec3 yuv = vec3(
        texture(tex_y, uv).r - 0.0625,
        texture(tex_u, uv).r - 0.5,
        texture(tex_v, uv).r - 0.5
    );
    frag_color = vec4(clamp(yuv_matrix * yuv, 0.0, 1.0), 1.0);
}
```
BT.601 vs BT.709 matrix is chosen at runtime based on `AVCodecContext::colorspace`.

### `AudioPlayer` — miniaudio
miniaudio is a single-header library. The callback model is identical to the SDL/PortAudio approach in the tutorial:
```cpp
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

void audio_callback(ma_device* device, void* out, const void*, ma_uint32 frame_count) {
    auto* player = static_cast<AudioPlayer*>(device->pUserData);
    player->fill_buffer(static_cast<float*>(out), frame_count);
}

// Init:
ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
cfg.playback.format   = ma_format_f32;
cfg.playback.channels = 2;
cfg.sampleRate        = 48000;
cfg.dataCallback      = audio_callback;
cfg.pUserData         = this;
ma_device_init(nullptr, &cfg, &device_);
ma_device_start(&device_);
```

### `Clock`
```cpp
class Clock {
    double pts_       = 0.0;
    double set_time_  = 0.0;   // std::chrono wall time when pts_ was last updated
    double speed_     = 1.0;
public:
    void   set(double pts);
    double get() const;        // pts + (now - set_time) * speed
    void   pause(bool paused);
};

class MasterClock {
    enum class Source { Audio, Video, External };
    Source     source_{ Source::Audio };
    Clock      audio_, video_, external_;
public:
    double get() const;
    void   set_source(Source s) { source_ = s; }
};
```

### `Sync`
```cpp
// Video: compute when to show the next frame
double compute_video_delay(double pts, double last_pts, double last_delay,
                           double master_clock);

// Audio: shrink or expand sample buffer to stay in sync
std::span<float> synchronize_audio(std::span<float> samples, double pts,
                                   double master_clock, SyncState& state);
```

---

## Thread Model

```
main thread (GLFW)
│  - creates window + OpenGL context
│  - keyboard: seek ±10s/±60s, space=pause, Q=quit
│  - timer fires → pull from FrameQueue → VideoRenderer::draw()
│
├── std::jthread: demux_thread
│     Demuxer::read_loop(stop_token)
│     → audioq.push() or videoq.push()
│
├── std::jthread: video_decode_thread
│     loop: videoq.pop() → Decoder::push/pull → FrameQueue::push()
│
└── miniaudio callback thread (managed by miniaudio)
      AudioPlayer::fill_buffer()
      → audioq.pop() → AudioDecoder → resample → write to output buffer
      → update audio_clock
```

All queues use `std::mutex` + `std::condition_variable`.
`std::counting_semaphore` controls max frame queue depth (prevents unbounded memory).
All threads check `std::stop_token` or `flushing_` flag for clean shutdown.

---

## CMake Setup

### CMakePresets.json
```json
{
  "version": 6,
  "configurePresets": [
    {
      "name": "macos-debug",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/macos-debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_TOOLCHAIN_FILE": "${sourceDir}/vcpkg/scripts/buildsystems/vcpkg.cmake"
      }
    },
    {
      "name": "windows-debug",
      "generator": "Visual Studio 17 2022",
      "binaryDir": "${sourceDir}/build/windows-debug",
      "cacheVariables": {
        "CMAKE_BUILD_TYPE": "Debug",
        "CMAKE_TOOLCHAIN_FILE": "${sourceDir}/vcpkg/scripts/buildsystems/vcpkg.cmake"
      }
    }
  ]
}
```

### CMakeLists.txt (sketch)
```cmake
cmake_minimum_required(VERSION 3.28)
project(cpp-video-player CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# vcpkg-managed deps
find_package(PkgConfig REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
pkg_check_modules(FFMPEG REQUIRED
    libavformat libavcodec libavutil libswresample libswscale)

# miniaudio: single header, fetch at configure time
include(FetchContent)
FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG        master)
FetchContent_MakeAvailable(miniaudio)

# GLAD2: generate via glad.dav1d.de or include pre-generated
add_subdirectory(third_party/glad)

add_executable(player
    src/main.cpp
    src/VideoPlayer.cpp
    src/Demuxer.cpp
    src/Decoder.cpp
    src/AudioDecoder.cpp
    src/AudioPlayer.cpp
    src/VideoRenderer.cpp
    src/Clock.cpp
    src/Sync.cpp
)

target_include_directories(player PRIVATE
    ${FFMPEG_INCLUDE_DIRS}
    ${miniaudio_SOURCE_DIR}
)
target_link_libraries(player PRIVATE
    glfw
    glad
    ${FFMPEG_LIBRARIES}
)

if(APPLE)
    target_compile_definitions(player PRIVATE GL_SILENCE_DEPRECATION)
    target_link_libraries(player PRIVATE "-framework OpenGL")
elseif(WIN32)
    target_link_libraries(player PRIVATE opengl32)
endif()
```

### vcpkg.json
```json
{
  "name": "cpp-video-player",
  "version": "0.1.0",
  "dependencies": [
    {
      "name": "ffmpeg",
      "features": ["avcodec", "avformat", "swresample", "swscale", "openssl"]
    },
    "glfw3"
  ]
}
```
> The `openssl` feature enables HTTPS support in FFmpeg's network layer, required
> for fetching DASH/HLS manifests from `https://` URLs.

---

## Implementation Phases

### Phase 1 — Project Scaffolding
- [ ] Create repo, add `CMakeLists.txt` + `CMakePresets.json` + `vcpkg.json`
- [ ] Fetch miniaudio via FetchContent, set up GLAD2
- [ ] Hello world: open a GLFW 4.1 core profile window, close on Escape/Q

### Phase 2 — Open File & Inspect Streams
- [ ] `Demuxer::open()` — `avformat_open_input` + `avformat_find_stream_info`
- [ ] `av_find_best_stream()` for video + audio
- [ ] `avcodec_parameters_to_context()` (not the old `avcodec_copy_context`)
- [ ] Print codec name, resolution, fps, sample rate to stdout

### Phase 3 — Decode Video & Render First Frames
- [ ] `Decoder::open()` + send/receive loop for video
- [ ] `VideoRenderer` setup: GLAD, fullscreen quad VAO, compile shaders, create 3 YUV textures
- [ ] Decode first 10 frames, display them (static display, no loop)
- [ ] Verify YUV→RGB looks correct on screen

### Phase 4 — Add Audio
- [ ] `AudioDecoder` with `SwrContext` (use `swr_alloc_set_opts2` + `AVChannelLayout`)
- [ ] `AudioPlayer` with miniaudio callback
- [ ] Play audio only (no video yet) — verify sound works on macOS

### Phase 5 — Threading + Queues
- [ ] Implement `Queue<T>` template with C++20 concepts
- [ ] Launch `std::jthread` for demux, stop via `std::stop_token`
- [ ] Launch `std::jthread` for video decode
- [ ] Use `std::counting_semaphore` to cap frame queue depth
- [ ] Main thread pulls frames and calls `VideoRenderer::draw()` on a timer

### Phase 6 — A/V Sync
- [ ] Implement `Clock` + `MasterClock`
- [ ] `compute_video_delay()` — compare video PTS to audio clock
- [ ] Schedule next refresh using `std::chrono` instead of `av_gettime`
- [ ] `synchronize_audio()` — shrink/expand sample buffer
- [ ] Test: video and audio locked together

### Phase 7 — Seeking
- [ ] Keyboard: left/right = ±10s, up/down = ±60s (GLFW key callbacks)
- [ ] `Demuxer::seek()` — `av_seek_frame` + flush queues + push flush sentinel packet
- [ ] Detect sentinel in decode threads → `avcodec_flush_buffers`

### Phase 8 — Polish
- [ ] Correct aspect ratio (letterbox/pillarbox, scale to fit window)
- [ ] Window resize: update OpenGL viewport + aspect ratio
- [ ] Pause/resume: space bar; use `std::atomic<bool>` + `wait()`
- [ ] On-screen OSD: current PTS timestamp using `std::format`
- [ ] Error handling: `[[nodiscard]]` on all init functions, early exits with descriptive messages

### Phase 9 — DASH / HLS Manifest Streaming
- [ ] `avformat_network_init()` at startup; `avformat_network_deinit()` at shutdown
- [ ] Accept URL string as input (detect `http://` / `https://` vs local path)
- [ ] Set reconnect + timeout options via `AVDictionary` before `avformat_open_input`
- [ ] Test with a public DASH stream (e.g. `https://dash.akamaized.net/...`) and HLS stream
- [ ] Verify seek works over network (graceful slow seek, no crash on stall)
- [ ] Graceful stall handling: if `av_read_frame` returns `AVERROR(EAGAIN)`, wait and retry

### Phase 10 — Video Engineer Debug HUD
- [ ] **Debug HUD** (`D`): live PTS, frame number, A/V sync diff (ms), decode time per frame
- [ ] **Frame stepping**: `.` forward 1 frame, `,` backward 1 frame (auto-pauses)
- [ ] **Stream inspector** (`I`): codec, resolution, fps, pixel format, color space, sample rate, channel layout, container
- [ ] **Audio track switcher**: `1`–`9` keys; display language label from stream metadata
- [ ] **A/V sync drift graph** (`G`): rolling 10s line graph rendered in OpenGL
- [ ] **Waveform view** (`W`): audio waveform strip rendered below video

### Phase 11 — Network Debugging & Discontinuity / Tag Inspector
- [ ] **`NetworkLogger`**: install `av_log_set_callback`, parse HTTP open/read log lines, classify by type (manifest / variant / init / segment), record URL + timing + byte count; flag slow fetches in red
- [ ] **Network log panel** (`N`): scrolling list of recent requests, colour-coded by type; URL (shortened), download time in ms, bytes; red highlight for fetches over threshold
- [ ] **`ManifestParser` — HLS**: fetch `.m3u8` raw text; parse line-by-line tracking running PTS via `#EXTINF` durations; record `#EXT-X-DISCONTINUITY`, `#EXT-X-DISCONTINUITY-SEQUENCE`, `#EXT-X-MAP`, `#EXT-X-PROGRAM-DATE-TIME`, `#EXT-X-CUE-OUT/IN`; capture any remaining non-standard tags as Unknown
- [ ] **`ManifestParser` — DASH**: fetch `.mpd`, parse XML with pugixml; extract `Period` boundaries, `EventStream`/`Event` elements, and non-DASH-namespace elements with their presentation times
- [ ] **Tag inspector panel** (`T`): list grouped by discontinuity sequence; highlight entry matching current PTS; show `disc_seq` counter in main debug bar; separate section for unknown/proprietary tags
- [ ] **Timeline strip**: thin horizontal bar below video showing discontinuity markers and cue points as coloured ticks — click to seek to that point
- [ ] Add **pugixml** via CMake FetchContent for DASH XML parsing

---

## What You'll Practice in C++

Each phase targets specific C++ concepts:

- **Phase 1–2**: Project setup, namespaces, RAII, `[[nodiscard]]`
- **Phase 3**: Classes, constructors/destructors, `std::span`, OpenGL C++ wrappers
- **Phase 4**: Callbacks, function pointers vs `std::function`, audio ring buffer
- **Phase 5**: `std::jthread`, `std::stop_token`, `std::counting_semaphore`, templates + Concepts
- **Phase 6**: `std::chrono`, floating point precision, clock abstractions
- **Phase 7**: State machines, thread coordination, sentinel/marker patterns
- **Phase 8**: `std::format`, `std::atomic`, move semantics, `std::filesystem`
- **Phase 9**: Network I/O, error recovery, URL parsing, `AVDictionary` options
- **Phase 10**: Data visualization with OpenGL, rolling ring buffers, 2D overlay rendering
- **Phase 11**: Log interception, string parsing, XML parsing with pugixml, timeline data structures

---

## Key References

- **FFmpeg 7 API changelog**: https://ffmpeg.org/doxygen/trunk/ (search each function)
- **miniaudio docs**: https://miniaud.io/docs/
- **pugixml docs**: https://pugixml.org/docs/manual.html
- **GLFW docs**: https://www.glfw.org/docs/latest/
- **GLAD2 generator**: https://gen.glad.sh/
- **OpenGL DSA guide**: https://www.khronos.org/opengl/wiki/Direct_State_Access
- **vcpkg**: https://vcpkg.io/en/packages
- **Dranger notes (this project)**: `dranger_ffmpeg_video_player_notes_EN.md` / `_CN.md`
