# Mirage VMAF Gap Report (Phase 0)

- Date: 2026-10-21
- Work order: `mirage-vmaf-spec-agent.md`
- Surveyed: commit `a4e02895d2d9c51c620e6c140f5566944c80ec71` (HEAD of branch `fix/high-speed-playback`). After the 2026-10-23 history rewrite this commit is `1bab7a3104cf9149300eafd3d3deb7ae4a9c6f69`, with the same tree `61765e415978c5a9735a3a6a3e245c52d0e6be99` (see `docs/DECISIONS.md` D-012)
- Written by: the agent (Claude), reviewed by the owner

**Scope**
- The main working tree also holds uncommitted FairPlay/AVFoundation changes. They don't touch the VMAF scoring code. They only change the `--vmaf` auto-start branch in `main.cpp` (an added check that AVFoundation sessions don't run VMAF) and how DebugHUD blends. This report doesn't discuss them, and the baseline is built from HEAD.
- Line numbers in this report refer to the HEAD version. Every number or version in it can be traced to a raw output in `eval/baseline/`:
  - `code_facts.txt`: code locations and line counts;
  - `environment.txt`: dependency versions and machine information;
  - `bipbop16x9/`: raw output of the baseline run.

---

## Summary

What the current implementation can do:
- score an HLS ladder serially, one variant at a time, **using the highest-bandwidth variant as the reference**;
- or score a single file against one reference file;
- output per-frame scores, mean / min / p5, and a basic JSON file.

What it doesn't do:
- align frames by PTS;
- configurable model and scaler;
- versions and configuration in the output;
- a consistency check;
- a unified timeline;
- benchmarks;
- a license and reproducible packaging.

§3, the paper's central claim, currently has only scattered data sources, each separate, with no unified data structure or query API.

**The baseline also found that the current scores don't match FFmpeg's libvmaf filter.** On the first 300 frames of the 640x360 rung, the mean per-frame absolute difference is 5.2798; see "Baseline" below. The §5 gate would not pass today.

---

## 1. VMAF code: files, size, architecture

| Location | Contents | Size (HEAD) |
|---|---|---|
| `src/VMAFAnalyzer.h` | `VMAFResult` and `VMAFAnalyzer` declarations | 75 lines |
| `src/VMAFAnalyzer.cpp` | decoding, libvmaf scoring, JSON export | 433 lines |
| `src/PlayerUI.cpp` `draw_vmaf_results` | the "VMAF Analysis" ImGui window: a table plus the per-frame curve of the first result | L1399–1563 |
| `src/DebugHUD.cpp` `draw_vmaf_panel` | small GL panel in the bottom-right corner (V key) | L908–1109 |
| `src/PlayerUI.cpp` menu | Analyze → "VMAF — Single File…" / "VMAF — Manifest Variants" | — |
| `src/main.cpp` | the `--vmaf [ref]` flag, auto-start (L1096–1108), auto-export on completion (L1826–1834), panel wiring, export buttons (L3180–3184, L3422–3425) | 80 lines contain "vmaf" |
| `src/MediaSession.h` | `VMAFAnalyzer vmaf_analyzer;` (L82): the analyzer belongs to a single media session | — |
| `CMakeLists.txt` | `pkg_check_modules(VMAF REQUIRED IMPORTED_TARGET libvmaf)`, links `PkgConfig::VMAF` | — |

**Architecture**

- `VMAFAnalyzer` works in its own `std::jthread`.
- For each (reference, distorted) pair it opens both URLs directly with `avformat_open_input`, creates a decode context (`DecodeCtx`) for each, and software-decodes each one's best video stream.
- Frames from both sides go through `sws_scale` to 8-bit YUV420P at the reference resolution (`SWS_BILINEAR`, L152, L157). They are copied into a `VmafPicture`, and `vmaf_read_pictures` is called frame by frame.
- Scoring stops when either side reaches EOF. It then flushes, reads per-frame scores with `vmaf_score_at_index` and mean / min with `vmaf_score_pooled`. p5 is computed by hand in C++.
- Results live in a locked vector, and the UI copies a snapshot every frame.

## 2. libvmaf: linked library or shell-out

- **Linked as a library, calling the C API directly**:
  - header `#include <libvmaf/libvmaf.h>`;
  - calls `vmaf_init` / `vmaf_model_load` / `vmaf_read_pictures` / `vmaf_score_*`;
  - dynamically links `/opt/homebrew/opt/libvmaf/lib/libvmaf.3.dylib` (`otool -L`, see `environment.txt`).
- `VMAFAnalyzer` makes no `system` / `popen`-style calls.
- This meets §1.1's "no shelling out".

## 3. libvmaf version, model, configurability

**Version**

| Source | Version |
|---|---|
| Homebrew package `libvmaf` | 3.1.0 |
| Library's own `vmaf_version()` | 3.0.0 |
| `vmaf --version` | 3.0.0 |
| `libvmaf.pc` | 3.0.0 |
| `VMAF_API_VERSION` in `version.h` | 3.0.0 |

The library's self-reported versions don't match the package version. Which one §7's `libvmaf_version` records is for the owner to decide (see the decision list at the end).

**Model**
- Hard-coded as `vmaf_model_load(&model, &model_cfg, "vmaf_v0.6.1")` (L199), which loads the built-in model by version string, with `flags = VMAF_MODEL_FLAGS_DEFAULT`.
- Not configurable, and the output has no model identifier.
- `/opt/homebrew/opt/libvmaf/share/libvmaf/model/` on this machine holds `vmaf_v0.6.1.json`, `vmaf_v0.6.1neg.json`, `vmaf_4k_v0.6.1.json` and others, but the code doesn't use them.

**Other fixed parameters**
- `n_threads = 4` (L188)
- `n_subsample = 1`
- `log_level = NONE`

None of these is configurable or recorded in the output.

## 4. Full-ladder scoring and the reference source

**Supported.** There are two entry points, the "Manifest Variants" menu item and `--vmaf` (without an argument). Both go through `start_manifest(variants)` (`VMAFAnalyzer.cpp` L325–377).

How it works:
1. Sort all `EXT-X-STREAM-INF` entries in the master by `BANDWIDTH`, descending. The first is the reference (equivalent to §1.2's mode B, top-rung).
2. Score every other entry against it, once each.
3. Single-threaded and serial, one pair after another.

Problems:
- **It scores per `EXT-X-STREAM-INF` entry, without deduplicating by video playlist.**
  - Some ladders pair the same video playlist with different audio groups, so the same playlist gets scored more than once. Example: Apple's advanced sample has 24 entries pointing to 8 video playlists; see `eval/baseline/inputs/`.
  - The reference playlist's other entries are also scored against the reference itself.
- Audio-only entries (no `RESOLUTION`) are attempted too, and are recorded as errors when they can't be opened.
- Every pair decodes the reference again, so N entries decode the reference N−1 times.
- Opening a variant doesn't attach the NetworkLogger hook, so these requests show up in the network panel with initiator "other". It also sets none of `timeout` / `rw_timeout` / `reconnect` (the playback path's `Demuxer` does), so network errors fall back to FFmpeg's default behavior.
- The whole VOD is decoded from start to end. There's no time-window option.
- Reference mode A (mezzanine) **exists only in single-file mode**: the currently open file is scored against one hand-picked file (`start(ref, dis)`). A ladder can't be scored against an external reference.
- When the open source is an HLS master, the reference file in `--vmaf <ref>` **is ignored** and the top-rung ladder mode runs anyway. The reason is that `main.cpp` L1099–1105 first checks whether the manifest has variants, and if it does, calls `start_manifest` directly.
- The output has no `reference_mode`.

## 5. Per-frame scores, and alignment with the playback timeline

**Per-frame scores exist**: `VMAFResult::per_frame`, stored by frame index.

**Not aligned by PTS**:
- Reference and distorted frames are paired one-to-one by **decode-order index**: L216–217 each take the next frame, and L260 uses `frame_idx++` as the index.
- PTS is neither read nor stored.
- Whichever side ends first sets the frame count.
- Frame-rate mismatches, dropped frames and duplicated frames are neither detected nor reported. By the code's logic, a ladder with mixed frame rates (e.g. Apple's advanced sample, which has both 30 and 60 fps entries) would be silently misaligned and still produce scores. This comes from reading the code, not from a measurement. §1.4's acceptance test will construct a case to verify it.

**Relation to the playback timeline**:
- The playback-position marker on the UI curve picks the frame as `pos_frac × N` (`PlayerUI.cpp` L1553), where `pos_frac = current PTS / duration`.
- That assumes the scored frames are spread evenly over the whole duration, so it's only an approximation.
- The curve only draws the first completed result.

## 6. JSON export and schema

**Exists**: `write_json(path)` (L392–433), built by concatenating strings by hand. Structure:

```
{ "results": [ { "label", "ref", "dis", "bandwidth", "resolution",
                 "vmaf_mean", "vmaf_min", "vmaf_p5", "per_frame": [ … ] }
               | { …, "error" } ] }
```

Problems:
- These fields are missing:
  - a schema version;
  - tool version, git commit, libvmaf version;
  - model, reference mode, scaler, environment;
  - per-frame PTS.
- Strings aren't JSON-escaped: a `"` or `\` in a URL or label produces invalid JSON.
- `"resolution"` holds the **reference's** resolution (L145 `result.width = ref_w`), while `"label"` holds the distorted variant's resolution. The field name is misleading.
- **The output path is `open_path + ".vmaf.json"`**, both for the auto-export (main.cpp L1830) and for the export buttons (L3182, L3423).
  - When the open source is a URL, this is a relative path like `https:/<host>/<path>/master.m3u8.vmaf.json`, and the write only succeeds if those subdirectories happen to exist under the current directory.
  - When `write_json` returns false, the callers **still print** "VMAF report written to …" (L1831–1832, L3183–3184, L3424–3425).
  - To capture real output, the baseline run created those directories beforehand; see `docs/DECISIONS.md` P-004.
- It's written only when an analysis finishes (`--vmaf`) or when the export button is pressed.

## 7. Concurrency between scoring and playback

Structurally, **scoring doesn't run on the render thread**:
- Scoring runs in its own `std::jthread`, with its own demux / decode contexts, and doesn't share a decoder with playback.

But note:
- This is **offline** scoring of whole variant files, not scoring of "the frames being played".
- There's no frame queue and no backpressure policy. The only statistic is a single `progress()` float.
- How scoring competes with playback for CPU hasn't been measured (**MEASUREMENT PENDING**).
- Once opened with V (the GL panel and the ImGui window share one toggle), the render thread calls `results()` twice per frame (`main.cpp` L2996, L3163). Each call copies all results under the lock, including every result's per-frame vector. The cost grows with the frame count and hasn't been measured (**MEASUREMENT PENDING**).
- The analyzer is a member of `MediaSession`. Opening another source destroys the session, cancels the analysis and loses the results.

## 8. Existing test coverage of VMAF

**None.**
- No file under `tests/` mentions VMAF (`grep -rli vmaf tests` prints nothing).
- `VMAFAnalyzer.cpp` isn't in the source list of `mirage_tests`.

## 9. LICENSE

**The repository has no LICENSE or COPYING file** (`git ls-files` finds no match). Under §8 of the work order, this alone **fails the submission requirements**.

Dependency licenses (from each dependency's own LICENSE file and Homebrew metadata):

| Dependency | How it's included | License |
|---|---|---|
| FFmpeg (the build actually linked is **ffmpeg-full 8.1.1**, see below) | Homebrew, dynamically linked | **GPL-3.0-or-later**: this build's configure options include `--enable-gpl --enable-version3`, with `libx264` / `libx265` / `libzimg` / `libvmaf` and without `--enable-nonfree` (`environment.txt`). Homebrew's metadata also gives GPL-3.0-or-later |
| libvmaf 3.x | Homebrew | BSD-2-Clause-Patent |
| GLFW 3.4 | Homebrew | Zlib |
| Dear ImGui v1.91.9 | FetchContent | MIT |
| miniaudio (`master`) | FetchContent | public domain or MIT-0, either one |
| stb (`master`) | FetchContent | MIT or public domain, either one |
| glad2 v2.0.8 | FetchContent | the generator itself is MIT; for the generated code and the Khronos headers, see glad's LICENSE |
| pugixml v1.14 | FetchContent | MIT |
| nativefiledialog-extended v1.2.1 | FetchContent | Zlib |
| Catch2 v3.7.1 (tests only) | FetchContent | BSL-1.0 |

**Needs an owner decision (possibly with the company's legal team)**
- Which license Mirage itself uses. The CFP's examples are GPLv2, LGPLv2 and BSD.
- How the chosen license fits with linking a GPLv3+ FFmpeg build, and which terms apply to binary distribution.
- The repository's `origin` is `zzzniloaix/Mirage` on github.com (the SSH alias `github-zzzniloaix` resolves to `github.com`, checked with `ssh -G` on 2026-10-23). The first draft wrongly said the company's GitHub; this has been corrected. The repository was public from 2026-04-03 and was made private on 2026-10-23. It has to be made public again before submission. The owner will confirm whether releasing it as open source has to go through the company's process.

## 10. Status of work order §1–§8, section by section

| Section | Status | Exists | Missing |
|---|---|---|---|
| §1.1 libvmaf 3 integration | Partial | Links the C API directly | Runtime report of the version and model identifier; selectable models (v0.6.1 / neg / 4k); custom model path |
| §1.2 Two reference modes | Partial | Ladders only have top-rung; mezzanine exists only in single-file mode | Mezzanine mode for ladders; a `reference_mode` field; separate reports for the two modes |
| §1.3 Resolution alignment | Partial | Distorted frames are upsampled to the reference resolution | The scaler is hard-coded to bilinear, not configurable, and not in the output |
| §1.4 Frame alignment | Not implemented | — | PTS pairing; handling of frame-rate mismatches, dropped frames and duplicated frames; failed-frame count and threshold warning |
| §1.5 Score pooling | Partial | mean, min, p5 | harmonic_mean, p1/p10/p25/p50; p5 is currently a non-interpolated nearest-rank (`sorted[n*5/100]`, L284), and the definition needs to be pinned down |
| §2 Ladder orchestration | Partial | Parses the master's BANDWIDTH / RESOLUTION / CODECS (ManifestParser); automatically picks the highest bandwidth as the reference; completed results are kept on cancel | Thread pool and concurrency level; memory cap; deduplication by video playlist; handling of discontinuities and mismatched segment durations; a cancel button in the UI (today an analysis is only cancelled by starting a new one or on destruction) |
| §3.1 Timeline model | Partial (scattered data sources, no unified model) | See below | A unified event structure keyed by PTS; SCTE-35; ABR/switch events; VMAF per-frame scores on the timeline |
| §3.2 Attribution queries | Not implemented | — | Query API, CLI subcommand, automatic detection, the two attribution test cases |
| §3.3 UI | Partial | A single per-frame curve; the network sidebar's timeline and alignment view; a time ruler for manifest tags (tags only exist when the open source is a media playlist) | Zooming / dragging the timeline, click-to-seek with attribution, multi-variant overlay, event markers over the score curve |
| §4 Concurrent scoring | Partial | Separate thread and decoder (offline scoring) | Scoring of played frames, bounded queues, the three backpressure policies, live statistics, dropped-frame measurement |
| §5 Consistency check | Not implemented | This machine's ffmpeg-full has `--enable-libvmaf` and can serve as the comparison tool (for checking only, not part of the implementation). Phase 0's diagnostic comparison already found a discrepancy; see "Baseline" | The check tool and delta report; finding and removing the existing discrepancy |
| §6 Benchmarks | Not implemented | The playback side already has QA tools (`--av-report`, `--net-report`), unrelated to VMAF | bench / sweep modes, resource statistics, environment metadata |
| §7 JSON schema | Partial | The JSON from item 6 above | A versioned schema file, and every field §7 lists |
| §8 Reproducible packaging | Not implemented | Some dependencies pin a tag | See below |

**Existing data sources for §3.1** (each one has to be wired into the unified timeline)

- **NetworkLogger**
  - Each HTTP request has start, sent, first byte, end, byte count and status code. Times are steady-clock microseconds and can be converted to wall-clock time.
  - Not keyed by media PTS.
- **ManifestParser**
  - Parses `EXT-X-DISCONTINUITY`, `DISCONTINUITY-SEQUENCE`, `PROGRAM-DATE-TIME`, `CUE-OUT` / `CUE-IN`, unknown `EXT-X-*` tags, and DASH Period / Event.
  - Only parses the playlist it opened. When an HLS master is opened it gets no segment-level events, because those tags only appear in media playlists.
  - Doesn't parse the SCTE-35 attributes of `EXT-X-DATERANGE`, and doesn't decode splice_info.
- **HlsAlign** (`MediaTimeline`)
  - Maps every segment of a media playlist to playlist time, with discontinuities and PDT, and checks the timestamps inside segments.
  - Used by the network sidebar's alignment view.
- **Player switching**
  - Mirage doesn't do ABR. When the user switches variants manually it only writes log text and produces no structured event.
- **Decoder**
  - Has a dropped-frame counter; `KeyframeIndex` records keyframe positions.

**Specific gaps for §8**

- No LICENSE (see item 9).
- **Build dependencies aren't pinned, and the docs disagree**:
  - The FetchContent declarations for miniaudio and stb use `GIT_TAG master`.
  - Homebrew dependencies aren't pinned. This machine has ffmpeg / ffmpeg-full 8.1.1, libvmaf 3.1.0 and GLFW 3.4, while Homebrew's current formulae are already ffmpeg 9.0.2, libvmaf 3.2.1 and glfw 3.5.1 (`environment.txt`). A clean machine following the README today would get a different major version of FFmpeg.
  - README and CLAUDE.md say `brew install ffmpeg`, while `CMakePresets.json` points `PKG_CONFIG_PATH` at **ffmpeg-full**. Installing per the README on a clean machine links a different FFmpeg build. This machine has both installed, both at 8.1.1, but with different configure options.
- **This machine currently can't build from scratch**:
  - glad generates the GL loader with Python at build time.
  - On this machine, the `pyexpat` extension of Homebrew Python 3.14.7 links the system `/usr/lib/libexpat.1.dylib` and needs `_XML_SetAllocTrackerActivationThreshold`, which the system library lacks, so `import pyexpat` fails. A clean build therefore fails at this step. The raw errors are in `eval/baseline/environment.txt` and `eval/baseline/build_failure_python314.log`. Python 3.13 doesn't have this problem.
  - The baseline worked around it with a scratch venv (DECISIONS P-003).
  - Options are committing glad's generated output or pinning the generation environment. This is in Phase 1's scope and waits for the owner's decision.
- Only a macOS preset exists. There are no Linux build instructions, no Docker / Nix, no `eval/run_all.sh` and no `eval/streams.md`.
- The root `vcpkg.json` lists ffmpeg and glfw3 but not libvmaf, and pins no versions. The CMake build doesn't use it.

---

## Baseline (Apple bipbop 16x9, 2026-10-21)

The whole process and all raw outputs are in `eval/baseline/`; see `eval/baseline/README.md`.

**It ran.** The build and the output path each needed one workaround (P-003, P-004). Not a line of existing code was changed.

**Results**
- Reference: gear5, 1920x1080, `BANDWIDTH=1924009`.
- Source: `bipbop16x9/report.vmaf.json` and `summary.txt`.

| Distorted (existing label) | Frames | mean | min | p5 | `resolution` in the JSON |
|---|---|---|---|---|---|
| 1280x720  1.0 Mbps | 53946 | 74.5632 | 69.7469 | 73.8895 | 1920x1080 |
| 960x540  0.9 Mbps | 53946 | 53.2220 | 45.1244 | 52.0560 | 1920x1080 |
| 640x360  0.6 Mbps | 53946 | 33.0648 | 20.0523 | 31.4198 | 1920x1080 |
| 416x234  0.3 Mbps | 53946 | 1.1455 | 0.0000 | 0.0000 | 1920x1080 |
| 0x0  0.0 Mbps (gear0, audio-only) | — | — | — | — | recorded as the error `cannot open distorted: …/gear0/prog_index.m3u8` |

**Resources**
- Source: `bipbop16x9/time_l.txt`, the output of `/usr/bin/time -l`.

| Item | Value |
|---|---|
| Wall clock | 3172.96 s |
| user | 12899.81 s |
| sys | 188.51 s |
| maximum resident set size | 640106496 bytes |
| peak memory footprint | 812352904 bytes |

- These numbers cover the **whole player process**: from launch, through playing while scoring, until 2 s after the report was written. They aren't "scoring only" numbers, and the existing tool can't measure the two separately.

**Problems visible directly in the output**

1. The `"resolution"` column holds the reference's 1920x1080 everywhere, consistent with the analysis in item 6.
2. The audio-only entry entered the scoring list and was recorded as an error. Its label is "0x0  0.0 Mbps": `BANDWIDTH=41457`, shown in Mbps with one decimal place, is 0.0.
3. For the four rungs that have scores, **min equals the first frame's score** (`first_frame` in `summary.txt`). In other words, the existing min pooling just picks up the first frame. §1.5 has to take this into account when it defines pooling.
4. The 416x234 rung:
   - Not a single frame scores above 5 (max 4.6752).
   - Nothing suggests a timing misalignment. Every rung runs at 30000/1001 and starts at 10.0 s, and the PTS sequences of the first 12 frames are identical (`diagnostics/ffprobe_rungs.txt`). Decoding from the start, the frame with index 300 (counting from 0) shows the frame counter 000300 on both rungs (`diagnostics/frame_n300_*.png`).
   - But the two rungs don't show the same picture: each overlay states its own resolution and gear number, and in the low rung the resolution test grid has already blurred into gray blocks.
   - So this ladder's low rungs aren't "scaled encodes of the reference picture". The ladder works as a functional baseline but not as a subject for measuring absolute quality. Choosing streams for the paper's evaluation needs to account for this (decision 14).

**Diagnostic comparison: current scores disagree with FFmpeg libvmaf**

This isn't the §5 implementation. It was done in Phase 0 only to judge whether the baseline scores can be trusted.

Conditions:
- Tool: the `libvmaf` filter of FFmpeg 8.1.1 (ffmpeg-full).
- Model `vmaf_v0.6.1`, `n_threads=4`.
- The distorted video goes through `scale=1920:1080:flags=bilinear`; the reference is used as is.
- Range: the first 300 frames.
- Both sides use **the same** `libvmaf.3.dylib` (`diagnostics/libvmaf_used_by_ffmpeg.txt`).
- The filter's input order is #0 distorted, #1 reference, and it calls `vmaf_read_pictures(ref, dist)`, the same as Mirage.
- The commands and raw logs are in `diagnostics/`. The comparison was done by `tools/compare_first_frames.py`, with its output in `diagnostics/compare_first300.txt`.

| Distorted | Mirage mean, first 300 frames | FFmpeg mean | Per-frame \|Δ\| max | Per-frame \|Δ\| mean | Frames with \|Δ\| > 0.01 |
|---|---|---|---|---|---|
| 640x360 | 33.8328 | 28.5529 | 5.6747 | 5.2798 | 300 / 300 |
| 416x234 | 1.8845 | 0.0061 | 3.8838 | 1.8784 | 290 / 300 |

- Mirage is higher on every frame: the signed mean difference equals the absolute mean difference.
- §5's target is "per-frame absolute difference < 0.01 with the default configuration". **The current implementation doesn't meet it.**
- The cause hasn't been located. The scoring library, the model and the reference frames (same size, same format) are all the same. The pairing of the first 300 frames is also the same: the opening PTS sequences match, and so do the frame counters at index 300. The remaining known difference is **how the distorted frames are scaled**:
  - Mirage: `sws_getContext(..., SWS_BILINEAR, ...)` plus `sws_scale`;
  - the comparison: the `scale` filter with `flags=bilinear`.
  - This is only a candidate cause. Candidates have to be ruled out one by one in §5 before drawing a conclusion.
- This shows the work order was right to put §5 before §3: until the consistency problem is resolved, **no score produced by the current implementation (including the table above) should be used as paper data**.

---

## Decisions needed from the owner

Under hard rule 3 of the work order, every item below waits for the owner's call. The agent won't choose on its own.

1. **Scope**: where Phase 1 starts, and the order of §8 versus §1 / §5.
2. **§1.2 default reference**: mezzanine or top-rung.
3. **§1.3 default scaler**: the work order suggests bicubic; the current code uses bilinear.
4. **Default model**: v0.6.1, neg or 4k. Also, whether picking the model automatically by resolution counts as the default behavior.
5. **Bit depth**: everything is currently converted to 8-bit YUV420P, so 10-bit / HDR content is truncated to 8 bits before scoring. This affects scores.
6. **Pooling definitions**: nearest-rank or interpolated percentiles; libvmaf's built-in harmonic mean or our own.
7. **Frame-alignment failure threshold**: the work order suggests 1%. Above the threshold, only warn, or refuse to produce scores.
8. **§4 default backpressure policy**: one of the three.
9. **Version recording**: libvmaf's package version is 3.1.0 and it reports 3.0.0. Record both, or only one.
10. **License**: which license Mirage uses, and which FFmpeg build to choose (item 9).
11. **Build strategy**: whether glad's generated output is committed; whether the FFmpeg dependency is standardized on ffmpeg or ffmpeg-full; the scope of Linux support.
12. **Branches and PRs**: which branch the VMAF work is based on and where PRs go. Right now `fix/high-speed-playback` is 2 unpushed commits ahead of origin, and the working tree also has uncommitted FairPlay changes.
13. **Refactors that change the render or decode path**: e.g. §4's scoring of played frames, or moving the analyzer out of `MediaSession`. Every such refactor gets a written plan first.
14. **Which ladders the evaluation uses**: bipbop 16x9's low rungs aren't scaled encodes of the reference picture (see "Baseline"). Both the absolute-quality evaluation and §3's attribution tests need "a ladder encoded from one source". One can be encoded from a public source clip, or a suitable public stream can be picked.
15. **How to treat the Phase 0 baseline scores**: until §5 resolves the discrepancy, the baseline scores are only a "record of the current state" and stay out of the paper. Please confirm whether you accept this.
