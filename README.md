# Mirage

A C++20 video player built for video engineers. Supports local files and adaptive streams (HLS/DASH) with a live debug HUD, scrub-bar thumbnail preview, VMAF analysis, and network request inspection.

**Stack:** FFmpeg 8 · OpenGL 4.1 · GLFW 3 · miniaudio · Dear ImGui · libvmaf 3 · macOS

---

## Building

See `docs/BUILD.md`: the Homebrew packages, the presets and their commands, the unit tests, the versions the evaluation was measured with, and the build checks that verify those steps (`eval/build_check/`). The commands below use the `macos-debug` preset's build directory, `build/macos-debug/`.

The player is built for macOS only (`docs/DECISIONS.md` D-031). `mirage_vmaf`, the VMAF analyzer without a window, `mirage_hlscheck` and `mirage_dashcheck`, the HLS and DASH conformance checks without one, and the unit tests also build without the player and its dependencies (the `macos-headless` and `linux-release` presets). `docs/BUILD.md` has Linux instructions and `docker/README.md` a Linux image for the evaluation; neither has been built yet.

---

## Running

```bash
# Local file
./build/macos-debug/mirage /path/to/video.mp4

# HLS stream
./build/macos-debug/mirage https://example.com/stream.m3u8

# DASH stream: only with an FFmpeg built with its DASH demuxer, which
# Homebrew's ffmpeg-full has not; the N inspector's Conformance tab checks
# an MPD by URL without playing it
./build/macos-debug/mirage https://example.com/manifest.mpd

# VMAF analysis (auto-start, writes .vmaf.json on completion)
./build/macos-debug/mirage /path/to/distorted.mp4 --vmaf /path/to/reference.mp4

# VMAF scores without the player: a pair, or an HLS ladder
./build/macos-debug/mirage_vmaf --help

# Does an HLS master declare what its media holds? (BANDWIDTH, CODECS,
# RESOLUTION, FRAME-RATE, VIDEO-RANGE, segment starts; exit status 1 on errors)
./build/macos-debug/mirage_hlscheck --json report.json https://example.com/master.m3u8

# The same for an MPEG-DASH MPD (@bandwidth with @minBufferTime, @codecs,
# @width/@height, @frameRate, colour, audio, @startWithSAP, the timeline,
# @segmentAlignment; exit status 1 on errors). For @bandwidth it gives two
# rates: below "needs" playout runs dry, at "safe" or above it never does
./build/macos-debug/mirage_dashcheck --json report.json https://example.com/manifest.mpd
```

Or drag-and-drop a file onto the launch screen.

---

## Tests

`docs/BUILD.md`, step 4, runs the unit tests (`mirage_tests`, Catch2); `eval/README.md` has the evaluation scripts' tests and `eval/run_all.sh`, which reruns every committed measurement.

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

The time next to the bar counts from the video's first frame on every input, MPEG-TS and HLS included: the first frame reads 00:00.000 and is frame 0. The duration after it, like the bar, runs from there to the media's end, so it can be a little shorter than ffprobe's when the audio starts first. Click it to cycle through time / frames / remaining / PTS; PTS is the frame's absolute PTS in seconds, as ffprobe and the logs give it. The HUD, the frame inspector's PTS row, `B` and the Alignment tab show absolute PTS.

### Debug overlays

| Key | Panel |
|---|---|
| `D` | Debug HUD — PTS, frame count, A/V diff (ms), decode time |
| `I` | Stream inspector — codec, resolution, fps, pixel format, color space as the stream declares it at open, audio format, the video decoder and its decode errors |
| `G` | A/V drift graph — 10-second rolling history, ±100 ms range |
| `T` | Track switcher — click to change audio or video track |
| `W` | Waveform strip — full-width peak display of both channels, the last second of output |
| `N` | Network log + manifest tag inspector + timeline tick strip; Alignment (renditions line up) and Conformance (each EXT-X-STREAM-INF against its media; in its DASH MPD mode, each Representation of an MPD given by URL) tabs |
| `V` | VMAF panel — per-variant scores + per-frame graph |
| `P` | Frame inspector — the frame on screen: type, timestamps, colour tags and the interpretation the renderer follows, HDR10 / HDR10+ / Dolby Vision metadata, side data, encoder settings |
| `S` / `⇧S` | Scopes — luma waveform, R′G′B′ parade, vectorscope, histogram from the frame's code values; `⇧S` picks the next |
| `X` | Pixel probe — Y′CbCr codes, R′G′B′ and PQ/HLG nits under the mouse |
| `B` | Frame sizes / GOP — a bar per frame coloured I/P/B, keyframes, rolling bitrate, whole-stream overview; click to seek |
| `L` | Loudness — EBU R128 momentary / short-term / integrated, LRA, true peak against a delivery target; per-channel meters |
| `H` | Key bindings help overlay |

The `P`, `S`, `X`, `B` and `L` panels, the `N` inspector's Conformance tab and `mirage_dashcheck` are proofs of concept: `plan/engineer_tools_status.md` says what each checks and what is left.

### VMAF analysis (Analyze menu)

- **Single file** — pick a reference file via the file picker; the currently open file is the distorted source.
- **Manifest variants** — compares all HLS variants against the highest-bandwidth one as reference.
- Results appear in the VMAF ImGui window (V key). Export to JSON via the button or `--vmaf` CLI flag.

---

## Project structure

```
Mirage/
├── src/                   — the player and its VMAF analyzer; among the files:
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
│   ├── Clock.h            — MasterClock, wall-time extrapolation
│   ├── Sync.cpp/h         — FFplay-style A/V sync compute
│   ├── NetworkLogger.cpp/h— av_log hook, HTTP request classification
│   ├── ManifestParser.cpp/h — HLS/DASH manifest text parsing
│   ├── VMAFAnalyzer.cpp/h — background VMAF analysis via libvmaf 3 C API
│   ├── VMAF*.cpp/h        — the analyzer's frame pairing, color conversion, pooling,
│   │                        ladder scoring, and mirage_vmaf's command line and report
│   ├── Queue.h            — thread-safe bounded queue (C++20)
│   └── Logger.h           — std::format logger
├── tools/mirage_vmaf.cpp  — mirage_vmaf, the VMAF analyzer without a window
├── tools/mirage_hlscheck.cpp — mirage_hlscheck, the HLS conformance check without a window
├── tools/mirage_dashcheck.cpp — mirage_dashcheck, the DASH conformance check (src/Dash*)
├── tests/                 — mirage_tests (Catch2), and the HW-vs-SW snapshot check
├── third_party/glad/      — the OpenGL 4.1 core loader glad generated
├── docs/                  — the build, the decision log, the Phase 1 status
├── eval/                  — the evaluation's scripts and committed measurements
├── docker/                — a Linux image for eval/run_all.sh (not built yet)
├── plan/                  — original implementation plan (reference only)
├── CMakeLists.txt
├── CMakePresets.json      — macos-debug / macos-release / macos-headless / linux-release
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

**`pkg-config` can't find FFmpeg**

The build uses Homebrew's `ffmpeg-full`, which Homebrew doesn't link into its prefix: the presets put its pkg-config directory, `/opt/homebrew/opt/ffmpeg-full/lib/pkgconfig`, first on `PKG_CONFIG_PATH`. Configure with a preset, or put that directory on `PKG_CONFIG_PATH` yourself (`docs/BUILD.md`, step 1).

**`vmaf_model_load failed`**

The analyzer loads libvmaf's built-in models by their version, `vmaf_v0.6.1` by default, and fails when the linked libvmaf has no such built-in model. `mirage_vmaf --model-file <model.json>` loads a model file instead; Homebrew's libvmaf keeps its model files in `/opt/homebrew/opt/libvmaf/share/libvmaf/model`.

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

# Run the unit tests (docs/BUILD.md, step 4):
perl -e 'alarm 900; exec @ARGV' build/macos-debug/mirage_tests '~[audio]'
```

CLion: open the folder, select the `macos-debug` CMake preset, build target `mirage`.

---

## License

Mirage is licensed under the GNU General Public License, version 3 or (at your option) any later version: `GPL-3.0-or-later` (`docs/DECISIONS.md` D-008). The license text is in [`LICENSE`](LICENSE). The libraries Mirage uses keep their own licenses; item 9 of `docs/VMAF_GAP_REPORT.md` lists them.
