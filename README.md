# Mirage

A C++20 video player built for video engineers. Supports local files and adaptive streams (HLS/DASH) with a live debug HUD, scrub-bar thumbnail preview, VMAF analysis, and network request inspection.

**Stack:** FFmpeg 8 · OpenGL 4.1 · GLFW 3 · miniaudio · Dear ImGui · libvmaf 3 · macOS

---

## Quick start on a new machine

### 1. Install Homebrew dependencies

```bash
brew install ffmpeg glfw ninja pkg-config libvmaf
```

> **Python / jinja2** — GLAD2 (the OpenGL loader) needs jinja2 at configure time. If you see `ModuleNotFoundError: No module named 'jinja2'` run:
> ```bash
> pip3 install jinja2 --break-system-packages
> ```

### 2. Clone / copy the project

```bash
# If cloning fresh:
git clone <repo-url> Mirage
cd Mirage

# Or copy your existing folder over and cd into it.
```

The `build/` directory does **not** need to be transferred — it is regenerated in step 3.

### 3. Configure

```bash
cmake --preset macos-debug
```

This downloads all remaining dependencies automatically via CMake FetchContent:
- **GLAD2** (OpenGL 4.1 Core loader, generated at configure time)
- **miniaudio** (single-header audio)
- **Dear ImGui** v1.91.9
- **pugixml** v1.14 (DASH MPD parsing)
- **nativefiledialog-extended** v1.2.1 (file picker)
- **Catch2** v3.7.1 (unit tests)

First configure takes ~1–2 minutes on a fast connection.

### 4. Build

```bash
cmake --build --preset macos-debug
```

Output: `build/macos-debug/mirage`

> The linker warning `ignoring duplicate libraries: 'libglad_gl_core_41.a'` is harmless — ignore it.

### 5. Run

```bash
# Local file
./build/macos-debug/mirage /path/to/video.mp4

# HLS stream
./build/macos-debug/mirage https://example.com/stream.m3u8

# DASH stream
./build/macos-debug/mirage https://example.com/manifest.mpd

# VMAF analysis (auto-start, writes .vmaf.json on completion)
./build/macos-debug/mirage /path/to/distorted.mp4 --vmaf /path/to/reference.mp4
```

Or drag-and-drop a file onto the launch screen.

---

## Release build

```bash
cmake --preset macos-release
cmake --build --preset macos-release
./build/macos-release/mirage /path/to/video.mp4
```

---

## Running tests

```bash
cd build/macos-debug
ctest --output-on-failure
```

34 tests covering `NetworkLogger` (URL classification) and `ManifestParser` (HLS/DASH tag parsing).

---

## Controls

### Playback

| Key / Action | Effect |
|---|---|
| `Space` | Play / Pause |
| `→` / `←` | Seek +10 s / −10 s |
| `↑` / `↓` | Seek +60 s / −60 s |
| `[` / `]` | Speed presets (0.25× → 4.0×) |
| `.` | Step forward to next keyframe (auto-pauses) |
| `,` | Step backward to previous keyframe (auto-pauses) |
| `Q` / `Esc` | Quit |

### Scrub bar

Drag the seek bar at the bottom of the window. A thumbnail strip appears showing keyframes. The highlighted thumbnail tracks the frame actually decoded at the current position. Audio is muted during drag and resumes 300 ms after release.

### Debug overlays

| Key | Panel |
|---|---|
| `D` | Debug HUD — PTS, frame count, A/V diff (ms), decode time |
| `I` | Stream inspector — codec, resolution, fps, pixel format, color space, audio format |
| `G` | A/V drift graph — 10-second rolling history, ±100 ms range |
| `T` | Track switcher — click to change audio or video track |
| `W` | Waveform strip — full-width peak display |
| `N` | Network log + manifest tag inspector + timeline tick strip |
| `V` | VMAF panel — per-variant scores + per-frame graph |
| `H` | Key bindings help overlay |

### VMAF analysis (Analyze menu)

- **Single file** — pick a reference file via the file picker; the currently open file is the distorted source.
- **Manifest variants** — compares all HLS variants against the highest-bandwidth one as reference.
- Results appear in the VMAF ImGui window (V key). Export to JSON via the button or `--vmaf` CLI flag.

---

## Project structure

```
Mirage/
├── src/
│   ├── main.cpp           — render loop, A/V sync, scrub state machine
│   ├── Demuxer.cpp/h      — avformat: open, seek, track switching
│   ├── Decoder.cpp/h      — avcodec video decode loop
│   ├── AudioDecoder.cpp/h — audio decode + SwrContext + atempo filter
│   ├── AudioPlayer.cpp/h  — miniaudio ring buffer, audio clock, volume/mute
│   ├── VideoRenderer.cpp/h— YUV→RGB24 via sws + OpenGL 4.1 texture
│   ├── ScrubBar.cpp/h     — OpenGL scrub bar overlay
│   ├── ThumbnailStrip.cpp/h — async keyframe thumbnail decoder + GL strip
│   ├── ScrubDecoder.cpp/h — dedicated single-frame decoder for scrub preview
│   ├── PlayerUI.cpp/h     — Dear ImGui menu bar + auto-hiding control bar
│   ├── DebugHUD.cpp/h     — all debug overlay panels
│   ├── Clock.cpp/h        — MasterClock, wall-time extrapolation
│   ├── Sync.cpp/h         — FFplay-style A/V sync compute
│   ├── NetworkLogger.cpp/h— av_log hook, HTTP request classification
│   ├── ManifestParser.cpp/h — HLS/DASH manifest text parsing
│   ├── VMAFAnalyzer.cpp/h — background VMAF analysis via libvmaf 3 C API
│   ├── Queue.h            — thread-safe bounded queue (C++20)
│   └── Logger.h           — std::format logger
├── tests/
│   ├── test_network_logger.cpp
│   └── test_manifest_parser.cpp
├── plan/                  — original implementation plan (reference only)
├── CMakeLists.txt
├── CMakePresets.json      — macos-debug / macos-release presets
└── CLAUDE.md              — AI assistant instructions
```

---

## Thread model

```
main thread       — GLFW events, render loop, frame upload, all seek/scrub logic
demux jthread     — av_read_frame → videoq / audioq
video jthread     — videoq → Decoder → frameq
audio jthread     — audioq → AudioDecoder → AudioPlayer ring buffer
miniaudio thread  — AudioPlayer::fill_buffer() callback (CoreAudio)
ThumbnailStrip    — own jthread + own AVFormatContext (keyframe decode)
ScrubDecoder      — own jthread + own AVFormatContext (scrub preview decode)
```

All threads shut down via `std::stop_token`. Queues unblock `pop()` on `shutdown()`.

---

## Common issues

**`ModuleNotFoundError: No module named 'jinja2'`**
```bash
pip3 install jinja2 --break-system-packages
```

**`pkg-config` can't find FFmpeg / GLFW**

Make sure Homebrew's pkg-config path is on `PKG_CONFIG_PATH`:
```bash
export PKG_CONFIG_PATH="$(brew --prefix)/lib/pkgconfig:$PKG_CONFIG_PATH"
# Then re-run cmake --preset macos-debug
```

**libvmaf model not found at runtime**

The VMAF model is loaded from `/opt/homebrew/share/libvmaf/model/vmaf_v0.6.1.json`. If libvmaf was installed to a non-standard prefix, set:
```bash
export LIBVMAF_MODEL_PATH=/your/prefix/share/libvmaf/model
```
(then update the path in `VMAFAnalyzer.cpp` accordingly)

**clangd shows "file not found" for FFmpeg / miniaudio headers**

This is a compile_commands.json / pkg-config path issue in the IDE — the build itself works correctly. Safe to ignore. The project compiles cleanly with `cmake --build`.

**Window appears but video is black / no audio**

Check that FFmpeg was built with the correct codec support:
```bash
ffprobe /path/to/video.mp4   # should show stream info without errors
```

---

## Development workflow

```bash
# Edit source files, then:
cmake --build --preset macos-debug

# Run with a test file:
./build/macos-debug/mirage ~/Movies/test.mp4

# Run tests after changes to NetworkLogger or ManifestParser:
cd build/macos-debug && ctest --output-on-failure && cd ../..
```

CLion: open the folder, select the `macos-debug` CMake preset, build target `mirage`.
