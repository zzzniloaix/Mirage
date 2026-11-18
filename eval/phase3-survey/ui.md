# §3.3 survey: the player's UI, and a plan for the timeline panel

Phase 3 design survey for the work order's §3.3 (a timeline that can be zoomed and dragged, showing per-frame score curves with event markers on them; a click on a low point that takes the player to the frame and shows what the drop is attributed to; the curves of several variants drawn over one another to compare them). Nothing of Phase 3 is implemented.

- **Code read**: `<repo>` at `75cf6e0` (branch `vmaf/phase-3`). `file:line` means `<repo>/file:line` at that commit.
- **Libraries read**: Dear ImGui v1.91.9, the tag `CMakeLists.txt:113-118` fetches, read from the copy FetchContent placed in the debug build (lines also at `https://github.com/ocornut/imgui/blob/v1.91.9/<file>#L<n>`); FFmpeg n8.1.1 `libavformat/hls.c` and `seek.c`, cited by URL.
- **Runs**: the player was neither built nor run. One run was made: `experiments/hls-seek-probe/`, a library-API program that seeks two HLS playlists the way the player's demuxer does. Its command is `run.sh` and its full output `run-output.txt`; `README.md` there explains it. "By reading" marks claims that come from reading code only.
- **Decisions**: design choices are listed as options for the owner. Where the agent has a recommendation it is labelled as the agent's. Nothing here is decided.

## Summary

- The player already has every ingredient of §3.3, but separately: two per-frame score views of the in-player analyzer, a zoomable/draggable timeline in the network sidebar's Alignment tab, a GL time ruler for manifest tags, and a seek bar with a scrub pipeline. None of them maps scores to media time. The in-player analyzer records no PTS, so the panel should read mirage_vmaf's reports.
- Plan: a new ImGui panel docked above the control stack. It loads a mirage_vmaf report, matches it to the open master (by SHA-256, else by URL) and to the variant on screen, and draws one curve per variant on the reference's timeline. Curves are per-column min/max envelopes so dips stay visible, with event lanes, detected drops and the playhead. Wheel zooms, drag pans, double-click fits, and a click picks the lowest frame near the cursor, shows its attribution and seeks there through the existing `s_seek_to` entry point. Its math (view, envelope, hit test, report loading, matching, time mapping, seek decision) lives in ImGui-free files tested in `mirage_tests`.
- Measured with the probe: with timestamps that continue across `EXT-X-DISCONTINUITY`, a seek by a frame's PTS lands at the start of that frame's segment. With an ad whose timestamps restart, **no seek reaches the ad's frames** in FFmpeg 8.1.1's hls demuxer. By PTS the seek fails, and the player's clamp then sends it to the stream's start (the probe reproduces the clamp of `Demuxer.cpp:487-495`; the player itself wasn't run). By playlist position the ad is skipped. Clicking a low point inside such an ad cannot seek to that frame without a change to the decode path (D-007). That is a difference from the work order for the owner to decide on, together with how D-069's synthetic ad is timestamped.
- By the agent's reading, none of the recommended changes touches the render or decode path. The changes that would are listed in section 3; the owner decides.

## 1. What exists

### 1.1 Per-frame score curves: two views of one in-player analyzer

| What | Where | Notes |
|---|---|---|
| The analyzer | `MediaSession.h:82` (`VMAFAnalyzer vmaf_analyzer`) | One worker `std::jthread` (`VMAFAnalyzer.h:291`). `start()` scores one pair, and `start_manifest()` scores every variant against the highest BANDWIDTH one, the pairs one after another on that worker (`VMAFAnalyzer.cpp:799-879`). The reference is chosen by sorting on BANDWIDTH (`:831-836`), not by mirage_vmaf's plan (`VMAFLadder.h:15-38`: playlists deduplicated by URL, entries without video skipped). |
| No PTS in its results | `VMAFAnalyzer.h:169-173`, `VMAFAnalyzer.cpp:819, 872` | Both calls pass default `VMAFOptions`, whose `record_pts` is false, so `ref_pts` / `dis_pts` stay empty. mirage_vmaf sets `record_pts = true` (`tools/mirage_vmaf.cpp:158`). |
| "VMAF Analysis" ImGui window | `PlayerUI::draw_vmaf_results`, `PlayerUI.cpp:1399-1574` | A movable window, 640×480 at the centre on first use (`:1406-1411`), with a table (`:1442-1494`). Its curve shows the first completed entry only (`:1504-1507`). x is the frame index, and frames are averaged per pixel column (`:1536-1555`). The marker sits at `pos_frac = last_pts / duration` (`main.cpp:3173-3174`), and the readout shows frame `pos_frac × N` (`PlayerUI.cpp:1557-1569`). An `InvisibleButton` covers the graph (`:1516`), but its clicks are not used. There is no zoom, pan, event marker or second curve. |
| GL panel (V) | `DebugHUD::draw_vmaf_panel`, `DebugHUD.cpp:908-1120` | Bottom right, above the strips (`main.cpp:3008-3012`). It draws the first done entry only (`DebugHUD.cpp:1019-1021`), with the same per-column average (`:1061-1077`) as a 1-px `GL_LINE_STRIP` (`:1079-1089`). The marker is at `pos_frac` (`:1091-1102`). Its only input is the × that main.cpp draws over it (`main.cpp:3189-3242`). |
| Feeding them | `main.cpp:2995-3013`, `3161-3187` | While V is on, `results()` is copied twice per frame, every per-frame vector included (`main.cpp:2996, 3163`; `docs/VMAF_GAP_REPORT.md:164`, where its cost is MEASUREMENT PENDING). Opening another source destroys the session and its results (`docs/VMAF_GAP_REPORT.md:165`). |
| Its threads vs playback | `MediaSession.h:215-219`, `MediaSession.cpp:958-973` | The session's demux, video, audio, subtitle and playlist threads. The analyzer has its own decoders (`docs/VMAF_GAP_REPORT.md:158`) and is cancelled and joined when the session is destroyed. |

### 1.2 Timelines

**The Alignment tab's timeline** (`NetInspectorAlign.cpp:581-799`) is the one zoomable, draggable timeline in the player, and the pattern to reuse:

- **View state**: `view_t0_`, `view_t1_` and `view_fit_` (`NetInspector.h:176-177`). It fits the data until the user zooms or pans (`NetInspectorAlign.cpp:618-622`). On a live reload it follows the moving window (`:348-375`).
- **Mapping and interaction**: time↔x lambdas (`:633-635`) on an `InvisibleButton` over the track (`:638-640`). The wheel zooms about the cursor by 0.8 / 1.25, clamped to [0.5 s, 1.5 × extent + 1 s] (`:642-649`). Dragging pans (`:650-655`) and a double-click fits (`:656-660`). A click is a release that moved less than 3 px (`MouseDragMaxDistanceSqr < 9`, `:661-662`), and it selects the segment under it (`:789-792`, not a seek).
- **Drawing**: under a clip rect (`:664, 767`): an axis at `nice_step` intervals (`:666-674`), one row per rendition with segment bars, discontinuity lines and timestamp diamonds (`:675-758`), and a playhead (`:759-766`) from `playlist_time_now = cur_pts − offset` (`:113-120`). Tooltips (`:769-788`). Clicking a finding scrolls the view to its time (`:555-563`).
- **Not unit-testable**: the zoom and pan math is inline in the draw code. `nice_step`, `axis_label`, `wall_label` and `utc_text` sit in an anonymous namespace (`:69-107`).

**The manifest tags' time ruler**:

- **Drawing**: `main.cpp:2887-2938` with `DebugHUD::draw_timeline_ticks` (`DebugHUD.cpp:588-646`). It is a GL strip of `kTimelinePx` = 10 framebuffer px times the scale (`DebugHUD.h:115`, `main.cpp:2777`), shown with N when the manifest has tags (`main.cpp:2772`). Ticks are coloured by `ManifestTagKind` (`:2895-2913`) at x = `tag.pts / duration` (`DebugHUD.cpp:612, 627`). Each tag costs one GL draw call and one buffer upload (`:621-635`), while the batched `tick_verts` is built and never used (`:609-615`).
- **Click handling**: click-to-seek is wired through the GLFW click globals (`main.cpp:2917-2934`, `DebugHUD.cpp:639-645`), but never fires (F1 in 1.8).
- **The Manifest tab** of the network sidebar lists the same tags. Clicking a row seeks through `NetInspector::Out::seek_to` (`NetInspector.h:40`), which main.cpp applies unless scrubbing (`main.cpp:3116`).
- **Where the tags come from**: `ManifestParser::parse_hls_text` (`ManifestParser.cpp:230-348`), on the playlist that was opened. For an HLS master it sees no segment tags (`docs/VMAF_GAP_REPORT.md:209, 223`).

### 1.3 Seek bar, scrubbing, previews

- **The seek bar**: `ui::seek_bar` (`UiWidgets.cpp:79-174`) is an `InvisibleButton` over a `kSeekHit` = 24 logical px zone (`UiTheme.h:67`), hovered even while another item is active (`UiWidgets.cpp:85`). Esc cancels a drag (`:90-94`). It seeks only on release (`:96-106`; contract in `UiWidgets.h:69-72`). It maps the clock linearly onto [0, duration] (`:135-138`). Keyframe ticks are decimated, and not drawn at all when there are 20000 or more (`:152-159`).
- **The controls**: `PlayerUI::build_controls` (`PlayerUI.cpp:582-794`) places the seek bar at the top of the full-width `##controls` window (`:585-588, 603-604`). A drag counts as activity (`:609`). The hover card (`:1100-1161`) is raised by `extra_bottom_logical` (`:1125-1126`).
- **Scrubbing**: the ImGui drag is carried one frame into the scrub logic (`main.cpp:1578, 1949-1954, 3349-3365`). The scrub goes to the ScrubDecoder (`:2011-2019`), and the release seeks once (`:2021-2025`). Seek commands are suppressed while scrubbing (`:3333-3339`), and Esc restores the position (`:3341-3347`).
- **`ScrubBar`** (`ScrubBar.h`, `ScrubBar.cpp`) is the old GL bar. main.cpp only calls `init()` (`main.cpp:875-880`), and nothing draws it.
- **Thumbnails**: `ThumbnailStrip` decodes them on its own thread (`ThumbnailStrip.h:32-47`). They feed the hover card and the keyframe filmstrip, which is drawn in the bottom stack while scrubbing or within 1.5 s of a seek (`main.cpp:2774-2775, 2949-2951`). Its `bottom_off` exists so that it sits above the ImGui bar (`ThumbnailStrip.h:82-87`).

### 1.4 Layout

- **Top bar and control stack**: the top bar is `kTopBarH` (`UiTheme.h:77`). The control stack is `kControlStackH` = 84 logical px (`UiTheme.h:82-84`), and its height eases with the chrome's fade, down to 0 or to the 2 px mini line (`PlayerUI.cpp:303-307, 343-345`). The chrome fades `kHideDelay` after the last activity (`UiTheme.h:88`), unless one of these holds (`PlayerUI.cpp:335-342`):
  - playback is paused;
  - a popup, a drag or the Open card is open;
  - the cursor is within the stack's height of the bottom or in the top bar, for up to 30 s without activity.
- **The bottom stack** (`main.cpp:2768-2780, 2881-2952`) starts at `bottom_stack_base_fb`, the control stack's eased height. From lowest to highest it holds the tag ruler, the waveform and the filmstrip, and their sum `strips_fb` (`:2776-2779`) becomes `extra_bottom_logical` (`:3088`). That value lifts the hover card and the subtitles (`PlayerUI.cpp:1125-1126, 1165-1170`), and the drift graph and the GL VMAF panel sit above it (`main.cpp:2819-2820, 3012`). The video rectangle does not depend on it (`compute_video_rect(fb_av_w, fb_h, …)`, `main.cpp:2736-2738`).
- **The right inset**: the network sidebar takes `dock_width()` (`main.cpp:2732-2736`). The chrome lays itself out in the rest (`:3089`, `PlayerUI.cpp:321`). The sidebar has a left-edge resize handle (`NetInspector.cpp:312-328`), and its width is kept in `prefs.txt` (`main.cpp:399-456, 1230-1236`).
- **Cards**: the track panel (`TrackPanel.cpp:115-125`) and the help and Open cards (`PlayerUI.cpp:959-1096`) are centred windows.

### 1.5 Input routing, keys, Esc

- **Callback order**: the app's GLFW callbacks are registered before ImGui's, so ImGui chains to them (`main.cpp:850-867`).
- **What reaches the app**: `on_key` returns when ImGui wants the keyboard (`main.cpp:241-242`), and `on_mouse_button` when it wants the mouse (`:316-317`).
- **ImGui's rules** (v1.91.9): `WantCaptureMouse` is set while a window is hovered or a button is held (`imgui.cpp:5123`). `WantCaptureKeyboard` is set while an item is active, or while keyboard navigation is active (`imgui.cpp:5128-5137`). Navigation is active for the nav window unless it has `NoNavInputs` (`imgui.cpp:13045`). PlayerUI turns keyboard navigation on (`PlayerUI.cpp:192`).
- **What the code does about it**:
  - The network sidebar drops focus as soon as no widget is held, because otherwise "Space would re-press the last button instead of pausing" (`NetInspector.cpp:387-394`).
  - The GL panels' close buttons do the same (`main.cpp:3231-3233`).
  - The track panel uses `NoNav` on its window and its lists (`TrackPanel.cpp:123-125, 151`).
  - The chrome windows use `NoNav` and `NoBringToFrontOnFocus` (`PlayerUI.cpp:35-39`).
  - The "VMAF Analysis" window has none of these (`PlayerUI.cpp:1411`).
- **Keys** (`main.cpp:238-303`): Esc, Q, Space, the arrows, D I G T W N H (and ?), K F M, C (and ⇧C), A V O, `,` `.` (and with ⇧), `[` `]`. The letters bound to nothing are B, E, J, L, P, R, S, U, X, Y and Z.
- **Esc** closes the panel opened last among `esc_panels` (`main.cpp:1387-1401, 1878-1898`). Overlay toggles show a toast (`:1784-1792, 3042-3049`). `--ui-overlays` (`:1759-1772`) and `--speed-seq panel:<key>` (`:152-156, 2204-2215`) toggle panels by letter.
- **Past problems recorded in the code**:
  1. The seek bar used to emit a seek every drag frame. That flooded the audio queue with flush sentinels: "buzz on one note" (`main.cpp:3333-3337`). Seeks now go out on release only and are suppressed while scrubbing (commit `9a60b77`: "seek bar emits seek_to only on release").
  2. ImGui owns the mouse over its windows, so GLFW-side scrubbing never saw a seek-bar drag. The drag state is now carried one frame into the scrub logic, and the GLFW path is driven by nothing else (`main.cpp:310-312, 1950-1954, 3363-3365`).
  3. The filmstrip was drawn under the ImGui bar. `bottom_off` now stacks it above (`ThumbnailStrip.h:82-87`; `9a60b77`).
  4. Esc pressed while ImGui holds the keyboard (a text field, a held bar) never reaches `on_key` (`main.cpp:244-249`, `PlayerUI.cpp:1089-1091`).
  5. Keyboard navigation left a clicked sidebar focused (`NetInspector.cpp:387-392`).

### 1.6 The player's clock, the seek API, and the report's timestamps

- **The player's clock**: `last_pts` is the PTS in seconds of the frame on screen, in the frame's own time base or the stream's. It is not shifted by the stream's start time (`main.cpp:1617-1625, 2420-2425`). HLS timestamps can start late: the demuxer's comment cites bipbop at 10 s (`Demuxer.cpp:481-486`, `KeyframeIndex.h:12-14`). The seek bar maps [0, duration] onto this clock (`UiWidgets.cpp:135-138`, `ThumbnailStrip.cpp:434-436`).
- **The seek entry point**:
  - `s_seek_to` takes an absolute target from the seek bar, the sidebar, the tags and the QA scripts (`main.cpp:75`). It is consumed at `:2001-2009` and becomes a request with `show_from` = the target (`:2120-2147`).
  - Keyframe steps pass a separate landing point (`seek_at`, `seek_key_after`, `:2038-2117`, `2127`).
  - `Demuxer::request_seek` (`Demuxer.h:76-84`, `Demuxer.cpp:66-74`) clamps the target to [start_time, start_time + duration − 0.5] (`Demuxer.cpp:481-495`). It then calls `av_seek_frame(fmt_ctx_, -1, secs * AV_TIME_BASE, AVSEEK_FLAG_BACKWARD)`, with a second try at start_time + 1 s (`:496-510`).
  - The video decode thread then drops frames whose raw PTS is below `show_from` (`MediaSession.cpp:60-83, 168-171`).
- **FFmpeg's hls demuxer** (n8.1.1):
  - It finds a target's segment by adding EXTINF durations to `first_timestamp`, the DTS of the first packet read, whatever timestamps the segments carry ([hls.c#L1916-L1944](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L1916-L1944), [#L2551-L2554](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L2551-L2554)).
  - It fails a target before `first_timestamp`, and moves a backward video seek to the segment's start ([#L2667-L2760](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L2667-L2760)).
  - It then drops packets whose DTS is below that position ([#L2565-L2586](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L2565-L2586)).
  - The hls demuxer is `AVFMT_NOGENSEARCH` ([#L2852](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L2852)), so a failed `read_seek` is final ([seek.c#L622-L638](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/seek.c#L622-L638)).
  - `KeyframeIndex` already relies on this seek clock: it predicts segment start i as "sum(EXTINF[0..i)) after the stream's first frame", and needs every EXTINF to be a whole number of frames (`KeyframeIndex.h:12-20, 30-35`).
- **mirage_vmaf's report**:
  - `frames[].ref_pts` / `dis_pts` are the frames' best-effort timestamps in seconds, unshifted (`VMAFReport.h:57`, `VMAFAnalyzer.cpp:280-285, 731-734`). That is the same form as the player's clock.
  - Pairing measures each input from its own start (`VMAFAlign.h:17-22`). The report gives each input's start and its discontinuity sequences, `{first_index, first_pts, frames}` (`VMAFReport.h:43-48`).
  - A ladder report also gives the master's URL, location and SHA-256, its entries, and the pairs with their entries (`VMAFReport.h:85-110`).

### 1.7 QA flags and scripts

| Flag | Where | What a §3.3 run uses it for |
|---|---|---|
| `--qa-no-focus` | `main.cpp:797-827` | The window takes no keyboard focus, so typing elsewhere can't reach the run |
| `--window WxH` | `main.cpp:797-806` | A fixed window size |
| `--ui-snapshot <png> <frame>` | `main.cpp:535-538, 910-912, 3288-3326` | Whole-window capture; the chrome is forced visible (`:3093`) and prefs are ignored (`:1006-1008`) |
| `--speed-seq "…"` | `main.cpp:110-175, 2173-2220` | Scripted `pause@t`, `seek:<s>@t`, `panel:<key>@t`, `snap@t` (numbered captures, `:3254-3287`); a seek logs "seek landed at pts" (`:2442-2444`) |
| `--ui-overlays DIGW…` | `main.cpp:1759-1772` | Panels on at start |
| `--ui-net <spec>`, `--align-report <txt> <secs>` | `main.cpp:544, 562-566, 1775-1778, 3117-3123` | The precedent for a panel's QA spec and a text report of its state |
| `--qa-switch "v720@4,…"` | `main.cpp:553-556, 953-969, 2222-2251` | Scripted variant switches |
| `--hls-as-is`, `--no-hw` | `main.cpp:931-934` | Master as served; software decode |

`eval/baseline/run_baseline.sh:15` starts the player with an isolated `HOME`, a `perl alarm` safety net, `--qa-no-focus` and `--window 1280x720`. `tests/pathb_snapshot_regression.sh` needs a display and is a convenience target outside ctest (`CMakeLists.txt:351-369`). It makes its clip with the `ffmpeg` binary, which new tests may not do.

### 1.8 Findings by reading that bear on §3.3

| # | Finding | Evidence |
|---|---|---|
| F1 | The tag ruler's click-to-seek never fires. The GLFW click is cleared before the ruler reads it, in the same iteration, and events are pumped only at the end of the loop. | Cleared at `main.cpp:2824-2825`, read at `:2918-2919`, events at `:3501-3519` |
| F2 | Tag times are playlist time, a running EXTINF sum from 0, while the header calls them "stream time". The ruler draws them on the seek bar's raw-PTS scale, so its ticks sit off by the stream's start time. | `ManifestParser.cpp:236, 340-343`, `ManifestParser.h:21`, `DebugHUD.cpp:612` vs `UiWidgets.cpp:135-138` |
| F3 | `#EXT-X-CUE-OUT-CONT` is taken as a cue-out. `#EXT-OATCLS-SCTE35` is dropped. `EXT-X-DATERANGE` is an Unknown tag with its raw line. This matters for D-070 if ManifestParser is reused. | `ManifestParser.cpp:320, 328-338` |
| F4 | Both existing curves average the frames of a column, so a dip narrower than a column flattens. Attribution needs the minimum. | `PlayerUI.cpp:1542-1552`, `DebugHUD.cpp:1061-1077` |
| F5 | The curve's marker and readout assume frames spread evenly from PTS 0 to the duration. A late start or failed pairs puts them on the wrong frame. | `PlayerUI.cpp:1557-1569`, `main.cpp:3173-3174`, `docs/VMAF_GAP_REPORT.md:126-128` |
| F6 | "VMAF Analysis" has neither `NoNav` nor the focus drop. By ImGui's rule, a click in it can leave `WantCaptureKeyboard` set and the player's keys dead. Not run. | `PlayerUI.cpp:1411`, `imgui.cpp:5128-5137, 13045` |
| F7 | `ScrubBar` is initialised and never drawn. | `main.cpp:875-880` (no other use in `src/`) |
| F8 | The bottom stack mixes units. `kStripPx` is framebuffer px not multiplied by the scale, while the other strips are scaled. | `ThumbnailStrip.h:123-124`, `main.cpp:2777-2779` |
| F9 | The GL HUD widens lines with `glLineWidth(scale)` in a forward-compatible 4.1 core context, where only width 1 is guaranteed. | `DebugHUD.cpp:346, 633`, `main.cpp:820-823`, [Khronos glLineWidth](https://registry.khronos.org/OpenGL-Refpages/gl4/html/glLineWidth.xhtml) |
| F10 | The in-player analyzer is not §2's. It records no PTS, scores pairs sequentially, chooses its own reference, and its results die with the session. | 1.1 |

## 2. Plan: a timeline panel that loads mirage_vmaf's report

### 2.1 What it is

An ImGui panel, `TimelinePanel`, docked above the control stack over the full width left of the network sidebar. It is resizable by its top edge, as the sidebar is by its left edge. The placement is the owner's (D1 in 2.6).

```
┌ Timeline · <report> · matched: master SHA-256 · top-rung   [▣ <ref>] [▣ <variant>] [▣ <variant>] [Fit] [×] ┐
│ <t>         <t>         <t>         <t>   …                                     │ ATTRIBUTION        │
│ score lanes: one curve per variant (min/max envelope) + the session's curve     │ interval, drop,    │
│ shaded intervals: the report's detected drops (attributions[])                  │ events by source   │
│ ▲ playhead (the frame on screen, on the reference's timeline)                   │ with offsets;      │
│ event lanes: network · playlist · SCTE-35 · ABR · decoder                       │ [Seek] / why not   │
└─────────────────────────────────────────────────────────────────────────────────┴────────────────────┘
[ seek bar + pills, unchanged ]
```

**Interactions**, those of the Alignment tab (`NetInspectorAlign.cpp:637-662`):

- **Wheel** zooms about the cursor.
- **Drag** pans.
- **Double-click** fits.
- **Hover** shows a tooltip: the time (reference PTS, discontinuity sequence), each shown variant's score there, and the events within a few px.
- **Click** (a release that moved less than the click threshold) picks a low point: the lowest score among the frames under the cursor ± a hit radius in logical px, on the hovered curve, the earliest of equal minima. The panel then:
  - highlights the frame;
  - shows its attribution: the report's detected interval holding it, or else §3.2's query around the frame;
  - seeks there when the frame is reachable (2.4), or says why not.
- **Variant chips** show and hide curves. The variant on screen is marked.
- **Esc** closes the panel, which joins `esc_panels`.
- **A key** toggles it (the letter is D8 in 2.6).

**Drawing**: everything with ImGui draw lists, as the Alignment tab does: axis, curves, intervals, markers and playhead, under one clip rect. Nothing goes through the GL HUD (F9).

### 2.2 Input: the report

- **Schema**: the panel reads what §3.1, §3.2 and §7 define. That is today's ladder report (`VMAFReport.h:61-110`: entries, reference, `pairs[].frames[]` with `ref_pts`, `dis_pts`, `vmaf`, and `alignment` with each input's sequences), plus `schema_version`, `timeline[]` and `attributions[]` from §7. The panel does not define the schema.
- **Attribution**: agent's recommendation: deserialize into §3.1's model and call §3.2's query API compiled into the player, so that the panel and the CLI subcommand give the same attribution and the UI implements none of its own.
- **A JSON reader is needed**: the repository writes JSON by hand and has no reader (`JsonText.h:1-21`). This is D2 in 2.6.
- **Load off the main thread**, on a worker `std::jthread`. The model is handed to the main thread as an immutable snapshot under a mutex, as `ThumbnailStrip` hands its decoded thumbnails over (`ThumbnailStrip.h:45-47`). How long real reports take to load, and the memory they need, is MEASUREMENT PENDING.
- **Lifetime**: the panel lives in `App`, beside `net_inspector` (`main.cpp:585`), so a loaded report survives a source change. It is matched again at each new session (as `app.net_inspector.reset()` is called, `main.cpp:1774`).

### 2.3 Matching the open stream and the variant on screen

**Pure function** (`timeline::match`, unit-tested). Its inputs are the report's `ladder {url, location, sha256, entries[]}` and the player's `ms.path`, `manifest.final_url()`, `manifest.text()` and `manifest.variants()` (`ManifestParser.h:71-80`).

1. **Same master bytes**: SHA-256 of `manifest.text()` (`FileHash.cpp` is already linked into the player, `CMakeLists.txt:176`) equals `ladder.sha256`. Entries then correspond by EXT-X-STREAM-INF order, provided both lists have the same length and each pair agrees on BANDWIDTH and resolved URL. Otherwise the match falls back to step 2 for the entries, keeping the "same bytes" status. The check is needed because the two parsers enumerate entries differently. ManifestParser takes the URI only from the line right after the tag, so a blank or comment line in between drops the entry (`ManifestParser.cpp:254, 281`). VMAFLadder allows blank lines and comments in between (`VMAFLadder.h:15-18`).
2. **Same address**: `ladder.location` equals `final_url`, or `ladder.url` equals the open path. The master changed since it was scored, so the panel warns, and entries match by resolved URL. The two resolvers differ: ManifestParser's is naive (`ManifestParser.cpp:180-189`), while VMAFLadder follows FFmpeg (`VMAFLadder.h:116-123`). A test must pin the cases where they disagree.
3. **Otherwise** the report is for another stream: shown only on the user's override, with a red chip.

**A pair report** (`--ref`/`--dis`) for a local file matches its `dis.url` to the open path (canonical paths) or by file hash. §7's `reference_identity` is "file hash or variant URI".

**The variant on screen**:

- Use the stream of the frame on screen, `shown_stream` (`main.cpp:1634, 1702-1710`), through `ms.playing_hls_variant()` (`MediaSession.h:150-154`).
- Not the existing `playing_variant`: it uses the routed stream (`main.cpp:3035`), which the demuxer changes when it takes a switch request (`:1544-1567`), before the first new frame is on screen (`:1688-1710`). It is also refreshed only on the one-second info tick (`:3015-3036`).
- In top-rung mode the reference has no pair (`VMAFLadder.h:36-38`). Playing it, the panel marks the reference row "reference (not scored)". In mezzanine mode every variant has a pair.

### 2.4 Time: the axis, the playhead, the seek

**The axis** is the reference's timeline (§3.1 asks for a single time base, with variants compared on the reference variant's timeline). Within one discontinuity sequence, the reference's PTS serve. Where a sequence's timestamps restart, PTS repeat and cannot be one axis. Options; the choice belongs to §3.1's model and is the owner's:

- (a) Key = (sequence, PTS), with sequences laid end to end on screen. This keeps the work order's "keyed by media PTS" exact.
- (b) The reference's playlist time (Σ EXTINF, `hls::Segment::start`, `HlsAlign.h:33-35`). It is monotonic and is the clock FFmpeg's hls demuxer seeks by, but it needs each frame's segment.
- (c) PTS made continuous across sequences.

Agent's recommendation: (a) in the model. The panel draws with (c), using the report's sequences (`first_pts`, and their frames' last PTS). Labels show the continuous time, with a mark at each sequence start, and tooltips give (sequence, raw PTS).

**The playhead**: take `last_pts` of the frame on screen and find the frame of the variant's pair whose `dis_pts` is nearest, by binary search within the current sequence. That frame's position on the reference's axis is the playhead. The panel tracks the current sequence: it moves to the next sequence when the clock jumps as `VMAFAlign.h:56-59` defines a discontinuity, and is set from the target on a seek. Playing the reference itself, the frame's own PTS is the playhead.

**The seek: measured.** `experiments/hls-seek-probe/` wrote two HLS VOD playlists of three 2 s MPEG-TS segments with an `EXT-X-DISCONTINUITY` before the second and third. It sought them as `Demuxer.cpp:505` does, before and after the player's clamp (`:487-495, 510`). Results from `run-output.txt`; both playlists read `start_time` 10.000 s and `duration` 6.000 s:

| Playlist (PTS of seg0 / ad / seg2) | Target | First video packet after the seek |
|---|---|---|
| reset (10.000–11.960 / 0.040–2.000 / 12.000–13.960) | 11.000, content before the ad | 10.000: seg0's start |
| reset | 0.500, an ad frame by its PTS | `av_seek_frame` fails ("Operation not permitted"); the player's clamp makes it 10.000: **the stream's start** |
| reset | 12.500, the same ad frame by playlist position | 12.000: seg1 requested, then seg2; **the ad is skipped** |
| reset | 13.000, content after the ad by PTS | 12.000: seg2's start |
| reset | 14.500, content after the ad by playlist position | end of file |
| continuous (10.000–11.960 / 12.000–13.960 / 14.000–15.960) | 11.000 / 12.500 / 14.500 | 10.000 / 12.000 / 14.000: each segment's start |

**Rules** for the panel, from the probe and the sources in 1.6:

1. **Timestamps that continue** from the first segment (PTS ≈ first DTS + playlist position, within EXTINF rounding): seek by the frame's PTS through `s_seek_to`. The seek lands at its segment's start, and the decode thread shows from the frame (`show_from`). This is the seek bar's own path, with nothing new below main.cpp.
2. **Content after an ad whose timestamps restarted**, when the content's PTS continue from before the ad: seek by PTS. The probe landed at its segment's start (13.000 → 12.000).
3. **Frames of an ad whose timestamps restarted**: no `av_seek_frame` target reaches them in FFmpeg 8.1.1. The panel must not send the PTS as it is, because the player's clamp would restart the stream. Options for the owner (D4):
   - (i) show "not reachable by a seek" in the card, and offer a seek to the start of the segment before the discontinuity, from which playing reaches the frame;
   - (ii) a "play to frame, then pause" request, like `--ui-sub-at`'s seek, play and pause (`main.cpp:1779-1781`), a new kind of request in the render loop: D-007;
   - (iii) another way for `Demuxer` to position the hls demuxer: D-007.
   Agent's recommendation: (i) for Phase 3. Report the difference from the work order (where a click on a low point takes the player to that frame) to the owner, and take (ii) or (iii) through a D-007 plan if wanted.
4. **Not measured**: a discontinuity whose timestamps jump forward, fMP4 segments, and a master with several variants (the player opens the master, possibly a deduplicated copy: `MediaSession.h:37-40, 113-116`). The probe didn't cover them: MEASUREMENT PENDING.

**Which rule applies** is a pure function, unit-tested, of:

- the frame's sequence in the playing variant (from the report's `dis` sequences);
- the variant's media playlist: segment starts and discontinuities through `hls::parse_media_timeline` (`HlsAlign.h:54-79`);
- the stream's start.

It compares a sequence's first PTS with the start plus the playlist position of its first segment, within a tolerance:

- **The tolerance must allow at least one frame.** `hls.c`'s `first_timestamp` is the first packet's DTS (`hls.c#L2551-L2554`), which can precede the first PTS: in the probe, seg0's first packet has PTS 10.000 and DTS 9.960. `Demuxer` knows `start_time` (`Demuxer.cpp:487-488`), not that DTS.
- **The media playlist** can come from the player's own fetches. The network logger keeps bodies; the Alignment tab reads them (`NetInspectorAlign.cpp:244-251`). It can also come from a fetch by the panel's worker (`ManifestParser::fetch`, `ManifestParser.h:84-89`). Agent's recommendation instead: the report carries each scored playlist's segment table and SHA-256, so the panel needs no network and can tell when the playlist changed (a §7 proposal for the owner, D11).

### 2.5 Files: new and changed

**New, ImGui-free**: compiled into `mirage` and `mirage_tests`, as `UiFormat.cpp` and `HlsAlign.cpp` are (`CMakeLists.txt:150-194, 264-329`). This matters because `mirage_tests` links no ImGui or GL, and the headless presets build no player (`CMakeLists.txt:7-13`, D-031).

| File | Contents |
|---|---|
| `src/TimelineView.{h,cpp}` | View state (t0, t1, fit) and its math: x↔t, zoom about a point with span limits, pan by px, fit, and clamping to the data. `nice_step` / `axis_label` move here from `NetInspectorAlign.cpp:69-107`. Per-column min/max envelopes with a precomputed min/max pyramid, so zoom and pan cost O(columns). Hit testing (lowest frame near x; nearest marker). Marker clustering. Decimation like `decimate_ticks` (`UiFormat.h:22-27`) |
| `src/TimelineReport.{h,cpp}` (or §3.1's model files, if they hold it) | Loading a report into the model; `match` (2.3); time mapping and sequence tracking (2.4); the seek decision (2.4) |
| `tests/test_timeline_view.cpp`, `tests/test_timeline_report.cpp` | Section 5.1 |

**New, player only**:

| File | Contents |
|---|---|
| `src/TimelinePanel.{h,cpp}` | `class TimelinePanel` with `Inputs`, `Out`, `load()`, `draw()`, `reset()`, `qa_apply()` and `report_text()`, modelled on `NetInspector` (`NetInspector.h:28-68`). Window `##timeline` with `NoNav`, `NoScrollWithMouse` and `NoFocusOnAppearing`, plus the focus drop of `NetInspector.cpp:387-394`. It calls `PlayerUI::notify_activity()` while hovered (2.1, 4.1) |

**Changed**:

| File | How |
|---|---|
| `CMakeLists.txt` | Add the new sources to `mirage` (`:150-194`), and the ImGui-free ones and the tests to `mirage_tests` (`:264-329`) |
| `src/main.cpp` | `s_show_timeline` beside the other toggles (`:81-96`). A key in `on_key` (`:238-303`). `CliOptions` and parsing for `--timeline <report>`, `--ui-timeline <spec>` and `--timeline-report <txt> <secs>` (`:516-567, 893-982`). `App::timeline_panel` (`:569-607`), reset per session (`:1774`). `esc_panels` grows to 9 (`:1397-1400`), plus `overlay_flags` (`:1786-1791`), `--ui-overlays` (`:1760-1772`) and the `--speed-seq panel:` letters (`:152-156, 2204-2215`). Its height is added to `strips_fb` (`:2776-2779`), which lifts the hover card, the subtitles, the drift graph and the GL VMAF panel. `draw()` runs after the network sidebar (`:3099-3124`), and its `seek_to` is applied under the same guard as `:3116` and `:3338-3339`. The height is kept in prefs as `net_width` is (`:399-456`). `"##timeline"` joins the card list the GL close buttons avoid (`:3202-3203`). `show_timeline` / `toggle_timeline` are wired (`:3063-3071, 3395-3403`) |
| `src/PlayerUI.{h,cpp}` | `PlayerUIInputs::show_timeline` and `PlayerUIState::toggle_timeline` (`PlayerUI.h:37-46, 62-64`). A View menu item (`PlayerUI.cpp:551-564`), a row in the engineer-overlays popover (`:937-955`) and in the help card (`:976-988`), and a term in `any_overlay` (`:751-752`) |
| `src/NetInspectorAlign.cpp` (optional) | Use the moved `nice_step` / `axis_label` (`:69-107`); behaviour unchanged |
| `src/DebugHUD.{h,cpp}`, `src/main.cpp:2887-2938` (optional) | Retire the GL tag ruler in favour of the panel's playlist lane, or fix its click (F1). This is D6 |

Not changed: `Demuxer`, `Decoder`, `MediaSession`'s threads, `VideoRenderer`, `ScrubDecoder`, and the seek handling block (`main.cpp:1998-2147`). No document is edited to match this plan.

### 2.6 Choices for the owner

| # | Choice | Options and trade-offs | Agent's recommendation |
|---|---|---|---|
| D1 | Placement | (A) A bottom dock above the controls: full width and next to the seek bar, but it covers the lower video and must follow the stack's fade and the hover card's lift. (B) A fourth tab of the network sidebar: its docking, resizing, focus handling and `Out::seek_to` come for free, but it is narrow and tied to N. (C) A floating window like "VMAF Analysis": simplest, but it can cover the seek bar and its position isn't kept (`PlayerUI.cpp:193`). Within (A), lowest in the stack means it never moves when the filmstrip comes and goes (`main.cpp:2774-2779`), and the filmstrip shows above it while scrubbing. Highest keeps the seek-bar-aligned strips next to the bar, but the panel shifts when the filmstrip shows | (A), lowest in the stack |
| D2 | JSON reader | (a) A reader written in the repository, like its writers (`JsonText.h`): no dependency, but code to write and test, and it must stream large arrays. (b) A header-only library: less code, but a new dependency (the dependency list is D-031's) | (a) |
| D3 | The axis key | 2.4 (a), (b) or (c); belongs to §3.1 | (a) in the model, (c) on screen |
| D4 | Frames of a timestamp-reset ad | 2.4 rule 3 (i), (ii) or (iii) | (i) now; the difference reported |
| D5 | D-069's synthetic ad | Timestamps that continue (rule 1: test (2)'s cue point can be clicked to), that restart (rule 3: realistic for inserted ads, not reachable by a seek), or both ladders | Both, so that the test covers both rules |
| D6 | The GL tag ruler | Keep as is (F1, F2 stay), fix its click and time base, or replace it with the panel's playlist lane | Replace once the panel shows playlist events |
| D7 | The in-player analyzer in the panel | Show mirage_vmaf reports only, or also the in-player results, which need `record_pts` (`VMAFAnalyzer.cpp:819, 872`) and still differ from §2 (F10) | Reports only |
| D8 | Its key | One of B E J L P R S U X Y Z (1.5) | The owner's |
| D9 | Config | The work order asks that each setting be available both from the command line and from a configuration file. The player's settings file is `prefs.txt`, key=value, written by the app (`main.cpp:397-456`) and ignored in snapshot runs (`:1006-1008`). Does it count as that config file for `--timeline` and the panel's settings? | Ask the owner |
| D10 | Dragging on the panel | Pan the view, or scrub the video. Scrubbing would go through the ScrubDecoder path (`main.cpp:2011-2019, 3349-3365`): D-007 | Pan |
| D11 | Playlist tables in the report | The report carries each scored playlist's segment table and SHA-256 (a §7 addition), or the panel fetches or reads the playlists itself | In the report |
| D12 | Re-running detection in the panel | Show the report's detected drops only, or also let the panel re-detect with another threshold (§3.2's 6 VMAF points and N frames) through §3.2's API | The report's only; the parameters it was run with are shown |

## 3. D-007: what touches the render or decode path

D-007 requires a written plan, approved by the owner, before any refactor that changes the render or decode path (`docs/DECISIONS.md:94-100`). D-068 says the render and decode paths don't change for §3's sessions (`docs/DECISIONS.md:680-690`). The agent's reading of each change follows; the owner decides.

| Change | Where | Touches the path? | Agent's reading |
|---|---|---|---|
| View math, report loading, matching, mapping, seek decision | New ImGui-free files | No | New code that reads no player state |
| The panel | `src/TimelinePanel.*`, drawn after `player_ui.build()` | No | An ImGui window in the overlay frame, as the network sidebar and the track panel are (`main.cpp:3097-3159`) |
| Toggles, menu, overlays popover, help | `src/PlayerUI.*` | No | Chrome |
| Key, CLI/QA flags, `App` member, `esc_panels`, toasts, `--ui-overlays`, `--speed-seq` letter | `src/main.cpp` | No | Input and QA plumbing |
| The panel's height in `strips_fb` | `main.cpp:2776-2779` | No | Overlay layout: it moves the strips, the GL panels and the hover card; the video rectangle doesn't depend on it (`main.cpp:2736-2738`) |
| Seeking on a click through `s_seek_to`, guarded as `:3116` | `src/main.cpp` | No | The entry point the seek bar, the sidebar, the tags and the QA scripts already use (`main.cpp:75`); `Demuxer` and the decoder are unchanged |
| A seek with its own landing point (`seek_at` ≠ `show_from`), or "play to frame, then pause" | `main.cpp:1998-2147` | **Yes** | It changes the render loop's seek handling, which drives `Demuxer::request_seek` and the paused-frame logic |
| Reaching frames of a timestamp-reset sequence | `Demuxer.cpp:458-560` | **Yes** | It needs another way to position the hls demuxer |
| Scrubbing the video by dragging on the panel | `main.cpp:1949-2025, 3349-3365` | **Yes** | The scrub pipeline |
| Recording the player's own switches as a session log (D-068's "later from a log the player records") | Observing `switch_video` and `on_present` (`main.cpp:1544-1567, 1688-1710`) | Owner's call | It only observes, but the code sits in the present step; with D-068's rule, the agent would ask before writing it |
| `record_pts` for the in-player analyzer (D7) | `VMAFAnalyzer.cpp:819, 872` | No for playback | The analyzer has its own decoders (`docs/VMAF_GAP_REPORT.md:158`). D-007's example is moving it out of `MediaSession`, which this doesn't do |
| Moving `nice_step` / `axis_label` | `NetInspectorAlign.cpp:69-107` | No | UI helpers |
| Retiring or fixing the GL tag ruler | `main.cpp:2887-2938`, `DebugHUD.*` | No | Overlay |

## 4. Risks

### 4.1 ImGui's input capture against the seek bar and the bottom stack

- **Covering the seek bar.** The seek bar is an `InvisibleButton` inside the full-width `##controls` window, which never comes to the front (`NoBringToFrontOnFocus`, `PlayerUI.cpp:35-39, 585-588`). A panel window that overlaps it takes its hover and clicks: `WantCaptureMouse` follows the hovered window (`imgui.cpp:5123`). The panel must end above `visible_bar_height_logical()`, with its height in `extra_bottom_logical`. Otherwise the hover card, drawn in the controls window's draw list, lands under the panel (`PlayerUI.cpp:1104-1105, 1125-1126`).
- **The fade moves it.** The stack's height eases to 0 when the chrome hides (`PlayerUI.cpp:303-307`). Cursor movement counts as activity (`main.cpp:331-333`). A cursor resting over the panel, above the stack, doesn't count as "in the chrome" (`PlayerUI.cpp:337-338`). So during playback the chrome fades `kHideDelay` after the cursor stops (`UiTheme.h:88`), and a panel that follows the stack slides down under it. Mitigation: `notify_activity()` while the panel is hovered, as a seek-bar drag does (`PlayerUI.cpp:609`), or pin the panel's base at the stack's full height while it is open.
- **One seek per click.** The panel must emit one seek per click release, never during a drag and never while scrubbing (the "buzz" history: `main.cpp:3333-3339`). It must apply that seek after `build()` under the same guards as the sidebar's (`:3116`). Telling a click from a pan by drag distance, as `NetInspectorAlign.cpp:661-662` does, keeps a pan's release from seeking.
- **No GLFW callbacks.** The panel cannot use the GLFW mouse callbacks: they get nothing while ImGui wants the mouse (`main.cpp:316-317`), and the click globals are cleared every frame (F1). It must be pure ImGui.
- **Keyboard.** A click can leave the panel as ImGui's nav window, and then `WantCaptureKeyboard` silences Space, the arrows and Esc (`imgui.cpp:5128-5137, 13045`; `main.cpp:241-242`). Use `NoNav` on the window and its children, and drop focus when no item is active (`NetInspector.cpp:387-394`). Not doing so is F6's risk.
- **Wheel.** The panel's wheel zoom applies only while it is hovered (`NetInspectorAlign.cpp:642`), with `NoScrollWithMouse` (`NetInspector.cpp:303-306`). The volume's wheel works only over the volume control (`PlayerUI.cpp:690-694`).
- **Double-click.** It fits the view. The chrome's double-click toggles fullscreen only when ImGui doesn't want the mouse (`PlayerUI.cpp:350-352`), which is never over the panel.
- **Esc.** While a drag holds an ImGui item, Esc goes to ImGui, not to `on_key` (`main.cpp:244-249`). Esc pressed during a pan won't close the panel.

### 4.2 Drawing many points

- **16-bit indices.** ImGui's draw lists use 16-bit indices by default (`imgui.h:266`). The OpenGL3 backend lets large lists split into several commands (`imgui_impl_opengl3.cpp:358`), but only between primitives: `PrimReserve` doesn't check a single primitive's size (its FIXME, `imgui_draw.cpp:680-686`). `AddPolyline` reserves up to four vertices per point (a thick non-textured line, `imgui_draw.cpp:795-797`), so one call must keep points × 4 within what 16-bit indices address (2^16 vertices): at most 16384 points. Draw at most one min/max pair per framebuffer column, in chunks.
- **Averaging hides drops** (F4). Draw the min/max envelope with a line through the minimum or the mean, and pick on the minimum.
- **Cost.** It grows with frames × variants. A min/max pyramid built at load makes each frame O(columns × variants). How long real ladders take to build it, and the memory it needs, is MEASUREMENT PENDING.
- **No per-frame copying.** Don't copy the model every frame, as `results()` is copied today (`main.cpp:2996, 3163`). The panel reads its own immutable snapshot.
- **Markers.** Segment requests, keyframes and decoder events can be dense. Cluster them per pixel with a count, show keyframes only above a pixel spacing (like `decimate_ticks`), and give details on hover. The GL ruler's draw call per tag (`DebugHUD.cpp:621-635`) is the pattern to avoid.

### 4.3 HiDPI

- **Units.** ImGui works in logical px, and the backend sets `DisplayFramebufferScale` every frame (`imgui_impl_glfw.cpp:853`; `UiTheme.h:3-6`). Fonts are rasterised at the framebuffer scale and drawn at 1/scale (`UiFonts.h:7-8`, `UiFonts.cpp:138`). Envelopes are built per framebuffer column (logical width × scale), so curves keep full resolution on Retina. Hit radii are in logical px. The cache is rebuilt when the width or the scale changes; a window moved between displays changes the scale (`PlayerUI.cpp:284-291`, `main.cpp:2727-2730`).
- **Lines.** ImGui tessellates lines into triangles. The GL HUD relies on `glLineWidth(scale)`, where only width 1 is guaranteed (F9). The panel uses ImGui only.
- **Mixed units in the stack.** The bottom stack is computed in framebuffer px and converted once (`main.cpp:2771-2779, 3088`), and the filmstrip's height isn't scaled (F8). The panel's height is defined in logical px and multiplied by the scale where `strips_fb` is summed.

### 4.4 Time mapping and matching

- **Timestamp-reset ads can't be reached by a seek** (2.4 rule 3, measured). A raw-PTS seek into one is clamped by the player to the stream's start (`Demuxer.cpp:487-495`). The panel must decide before it sends a seek.
- **EXTINF rounding.** The hls demuxer's seek clock is first DTS + Σ EXTINF (`hls.c#L1916-L1944`). Rounded EXTINF values drift from the PTS (`KeyframeIndex.h:19-20`), so near a segment boundary a PTS seek can land in the neighbouring segment. `show_from` then drops frames up to the target only if it landed before it.
- **Master vs media playlist.** mirage_vmaf scores each variant's media playlist directly, while the player plays the master, possibly a deduplicated copy (`MediaSession.h:37-40, 113-116`), with every variant as a program. That both give the same PTS is unverified: a test (5.2).
- **Matching can fail quietly**: the parsers' different entry enumeration and URL resolution (2.3). The panel shows how it matched and refuses silent index matching.
- **The tag ruler's offset** (F2), if the ruler stays.
- **Live streams** are out of scope. For `duration <= 0` the panel shows a notice, as the seek bar has a live mode (`UiWidgets.cpp:129-133`).

## 5. How to test it

### 5.1 Unit tests (`mirage_tests`, ctest, no ImGui or GL)

- **View**:
  - t→x→t round trip.
  - Zoom about a point keeps the time under the cursor fixed (randomised spans).
  - Zoom respects the span limits.
  - A pan of Δ px moves the view by Δ × span / width.
  - Fit and clamping.
  - `nice_step` and `axis_label` give the Alignment tab's results after the move (the same step table).
- **Envelope**:
  - Per-column min/max equals brute force on random series.
  - **A one-frame dip survives downsampling** (the regression test for F4).
  - Every pyramid level agrees with brute force.
  - null and NaN scores are skipped; empty columns.
- **Hit testing**:
  - The lowest frame within the radius, the earliest of equal minima.
  - Nothing outside the data.
  - The nearest marker.
  - Clusters keep their counts.
- **Loading**:
  - A minimal valid report, with JSON fixtures written in the test as string literals.
  - Unknown fields ignored.
  - A wrong `schema_version` refused.
  - null PTS.
  - Missing pairs; `not_scored` entries.
  - Frames within a sequence in order.
- **Matching**:
  - Same SHA-256 → by index.
  - Changed bytes at the same address → by URL, with a warning.
  - **A blank line between `EXT-X-STREAM-INF` and its URI** → index matching refused, URL matching used.
  - Resolver disagreements.
  - Top rung on screen → "reference".
  - Mezzanine mode.
  - A pair report against a local file.
- **Mapping**:
  - Player PTS → frame → axis, for continuous, reset and coarser-grid pairs (`VMAFAlign.h`'s grid rule).
  - Sequence tracking across a reset and after a seek.
- **Seek decision**:
  - Continuous → PTS.
  - Content after a reset ad → PTS.
  - Inside a reset ad → "not reachable", with the fallback.
  - The first-DTS tolerance.
- **Attribution card**: events grouped by source with offsets from the interval's start, from `attributions[]`. A pick outside any detected interval calls §3.2's query.

### 5.2 A library-API test of FFmpeg's seek behaviour

The probe (`experiments/hls-seek-probe/seek_probe.cpp`) becomes a ctest case. It writes the same two playlists through libavformat and libavcodec, in the style of D-069's generators, and checks the four behaviours the seek decision relies on: continuous → segment start; content after a reset ad by PTS → its segment start; ad by PTS → failure; ad by position → skipped. An FFmpeg upgrade that changes `hls_read_seek` then fails a test instead of the panel. It needs nothing downloaded and no binary.

### 5.3 A scripted QA run

The run needs a display, so it stays outside ctest, as `pathb_regression` does (`CMakeLists.txt:351-369`). It is started as the repository's scripted runs are: an isolated `HOME`, an alarm, `--qa-no-focus` and `--window` (`eval/baseline/run_baseline.sh:15`). The inputs are D-069's ladders, written to disk by their generators (a "keep the media" switch is to be added to the tests), and mirage_vmaf's report on each:

```
<repo>/build/macos-debug/mirage_vmaf --ladder <scratch>/qa/ad/master.m3u8 --out <scratch>/qa/ad/report.json

HOME=<scratch>/home-qa perl -e 'alarm shift; exec @ARGV' <secs> \
  <repo>/build/macos-debug/mirage <scratch>/qa/ad/master.m3u8 \
  --qa-no-focus --window 1280x720 \
  --timeline <scratch>/qa/ad/report.json \
  --ui-timeline "fit=1,pick=attribution:0@<t_pick>" \
  --speed-seq "pause@<t_pause>,snap@<t_snap>" \
  --ui-snapshot <out>/qa/ad.png 0 \
  --timeline-report <out>/qa/ad.txt <t_report> \
  > <out>/qa/ad.log 2>&1
```

**The new flags**, part of this plan:

- `--timeline <report>` loads a report.
- `--ui-timeline <spec>` stages the panel, in the style of `--ui-net` (`NetInspector.h:59-65`). Proposed keys: `fit=1`, `zoom=<t0>:<t1>`, `variants=all|<entry>,…`, and `pick=<axis time>` or `pick=attribution:<n>`. A pick takes an optional `@<t>`, the wall seconds after the first frame, as `--speed-seq`'s events do.
- `--timeline-report <txt> <secs>` writes the panel's state as text, as `--align-report` does (`main.cpp:3117-3123`).

**Ordering**:

- `--ui-snapshot` keeps the chrome visible and leaves prefs alone (`main.cpp:3093, 1006-1008`), so the panel's base doesn't move.
- `<t_pause>` comes before `<t_pick>`. A seek made while paused shows its target frame (`main.cpp:2133-2138`) and logs where it landed (`:2442-2444`).
- `<t_report>` comes before `<t_snap>`, because the last snap ends the run (`main.cpp:3285-3286`).

**What is checked:**

- **`ad.txt`**:
  - how the report matched, and which variant is on screen;
  - the picked frame (sequence, PTS, score);
  - the attribution card's content. Test (2) must list the cue-out and the discontinuity; test (1) must list the degraded segment's URI;
  - the seek's target, or why there was none (rule 3).
- **`ad.log`**: "--speed-seq: seek landed at pts …" (`main.cpp:2442-2444`) within half a frame of the picked frame, for a reachable frame. A seek made while playing logs "first frame after seek pts=…" instead (`main.cpp:1711-1715`).
- **`ad.png`**: for review.

A second run per ladder adds `--qa-switch` to exercise the playhead across a variant switch.

### 5.4 What stays manual

The QA flags inject no ImGui mouse input beyond the seek bar's hover (`--ui-hover`), so the input-capture cases of 4.1 need a person:

- hovering the panel while the chrome fades;
- a pan whose release is over the seek bar;
- Space and Esc after clicking the panel;
- wheel over the panel vs over the volume control.

A 1x display next to a Retina one (HiDPI, 4.3) also needs a person: MEASUREMENT PENDING.

## 6. Open issues

1. The axis key for streams whose timestamps restart at a discontinuity (D3), with §3.1's model.
2. Clicking a low point in an ad whose timestamps restart cannot seek to that frame with FFmpeg 8.1.1's hls demuxer (measured). This is a difference from the work order for the owner (D4). Any fix beyond (i) needs a D-007 plan.
3. How D-069's synthetic ad is timestamped (D5). It decides whether acceptance test (2)'s cue point can be clicked to.
4. Untested seek cases: forward timestamp jumps, fMP4, a master with several variants. MEASUREMENT PENDING.
5. Whether the player's PTS through the master equal mirage_vmaf's through the media playlist (4.4).
6. JSON reader (D2), placement (D1), key (D8), the config-file question (D9), and the report's playlist tables (D11).
7. The report's size, load time and memory for real ladders: MEASUREMENT PENDING.
8. Findings by reading to confirm with a run: F1 (dead tag click), F2 (tag offset), F6 (VMAF window keyboard focus).
9. Whether recording the player's own switches as a session log counts as touching the render path (section 3).
10. How "rung" numbers in attribution text (the work order's example of a switch between two numbered rungs) are defined: §3.1's to fix, the panel only shows them.
