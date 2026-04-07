# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

Mirage is a C++20 video player for video engineers — built on FFmpeg 8, OpenGL 4.1, GLFW, and miniaudio. It supports local files and adaptive streaming (DASH/HLS) with a live debug HUD targeting video engineers.

See `plan/cpp_video_player_plan.md` for the full implementation plan (11 phases).

## Build

Dependencies are managed via **homebrew** (macOS) and fetched at configure time (glad2, miniaudio):

```bash
# First-time setup (homebrew deps)
brew install ffmpeg glfw ninja pkg-config

# Configure
cmake --preset macos-debug

# Build
cmake --build --preset macos-debug

# Run
./build/macos-debug/mirage path/to/video.mp4
```

Build output lands in `build/macos-debug/` (debug) or `build/macos-release/` (release).

> **Note:** glad2 requires `jinja2` for Python 3. If the configure step fails with `ModuleNotFoundError: No module named 'jinja2'`, run:
> `pip3 install jinja2 --break-system-packages`

## Architecture

The plan defines 14 source files under `src/` wired together as:

```
main.cpp           — GLFW window, event loop, keyboard dispatch
VideoPlayer        — top-level orchestrator; owns all components
  Demuxer          — avformat: open file/URL, find streams, read packets, seek
  Decoder          — avcodec send/receive loop (video)
  AudioDecoder     — Decoder + SwrContext resampling → float output
  Queue<T>         — thread-safe template queue (C++20 concepts)
  AudioPlayer      — miniaudio device + callback; drives audio clock
  VideoRenderer    — OpenGL 4.1 DSA: 3×YUV textures, fullscreen quad
  Clock            — pts + wall-time extrapolation; MasterClock selects source
  Sync             — compute_video_delay(), synchronize_audio()
  ManifestParser   — pre-fetch HLS/DASH; extracts discontinuity/cue tags
  NetworkLogger    — av_log_set_callback hook; captures HTTP request timing
  DebugHUD         — 2D OpenGL overlay panels (toggled D/N/T/G/I/W)
  Logger           — std::format-based logger
```

### Thread model
- **Main thread** (GLFW): window events, keyboard, pulls frames → `VideoRenderer`
- **`std::jthread` demux**: `Demuxer::read_loop` → pushes to `audioq` / `videoq`
- **`std::jthread` video decode**: pops from `videoq` → `Decoder` → `FrameQueue`
- **miniaudio callback thread**: pops from `audioq` → `AudioDecoder` → audio out

Queues use `std::mutex` + `std::condition_variable`. `std::counting_semaphore` caps frame queue depth. All threads shut down via `std::stop_token`.

### Key tech choices
| Concern | Choice |
|---|---|
| Decode + streaming | FFmpeg 7 (`libavformat`, `libavcodec`, `libswresample`) |
| Windowing | GLFW 3.4 |
| OpenGL loader | GLAD2 (generated at build time for GL 4.1 Core) |
| YUV→RGB | GLSL shader, BT.601/BT.709 matrix selected from `AVCodecContext::colorspace` |
| Audio | miniaudio (single-header, CoreAudio on macOS) |
| Manifest parsing | FFmpeg built-in DASH/HLS + pugixml for MPD XML |

### FFmpeg 7 API notes (differs from older tutorials)
- Use `stream->codecpar` + `avcodec_parameters_to_context()` (not `stream->codec`)
- Use `av_find_best_stream()` for stream selection
- Use `av_packet_alloc()` / `av_packet_free()` (no stack-allocated `AVPacket`)
- Decode via `avcodec_send_packet()` / `avcodec_receive_frame()` (no `avcodec_decode_video2`)
- Audio channel layout: `codec_ctx->ch_layout` (not `channels` / `channel_layout`)
- Resampling: `swr_alloc_set_opts2()` with `AVChannelLayout*`
