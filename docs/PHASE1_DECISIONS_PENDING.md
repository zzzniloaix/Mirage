# Phase 1: decisions pending for the owner

What is still open for the owner after the decisions of 2026-10-26 (`docs/DECISIONS.md` D-034 to D-041), which adopted the agent's recommendations of this document as of `b2d2824`: D-011's clips and encoding parameters, and D-008's question with the license texts next to glad's loader; the agent's process choice P-066; and the differences between the work order and the implementation (hard rule 4). The decisions are the owner's (hard rule 3 of the work order); every recommendation below is labeled as the agent's. Every number comes from a committed output, named next to it; where there is none, it says MEASUREMENT PENDING. The state of each section of Phase 1 is in `docs/PHASE1_STATUS.md`, and the statements the paper's method text has to make, with their evidence, in `docs/PAPER_METHOD_NOTES.md`.

The items decided on 2026-10-26 follow, in "Decided on 2026-10-26 (D-034 to D-041)", and the packages decided on 2026-10-23 at the end, in "Decided on 2026-10-23 (D-020 to D-033)", each with the text it had when it was decided, as the record of its options, and a note at its start on what was decided and where the work is.

## Open

D-034 left these open: the agent's recommendation for D-011 is that the owner choose, and D-008 has none.

### D-011: the source clips of the self-encoded ladders

**Question.** D-011 has the evaluation encode its own HLS ladders from public source clips, each clip serving as the mezzanine, and leaves the clips and the encoding parameters to the owner when encoding starts. Which clips, and which parameters (rungs, sizes, codec and bitrates, segment length, color tags)?

**What waits for them.**

- §1.2's acceptance: both reference modes on one ladder, with the difference documented (D-029). `mirage_vmaf --ladder` scores a ladder in either mode (P-050), and `eval/consistency/ladder_check.py` checks a ladder run against the same pairs scored one by one (P-051).
- D-026's measurement: a 10-bit SDR ladder scored at 8 bits and at its own bit depth, on both sides.
- How much the scaler and the model change the scores of a ladder encoded from one source (D-024, D-025): `scalers-300/` and `models-300/` measure bipbop's rungs, which were rendered separately.
- §1.4's cases with dropped and duplicated frames and discontinuities in real encodes, and D-028's option (a) for discontinuities, which D-028 keeps until these ladders show what real discontinuities look like.
- Every score meant for the paper: the bipbop scores only compare the tools (D-005, D-011).

**What Phase 1's runs say about the choice.**

- An untagged input is taken as BT.601 with limited range (D-021), and a reference in another pixel format is converted to the distorted frames' tags (D-022). A mezzanine without color tags, such as a YUV4MPEG2 file, is such an input wherever it is read without tags.
- Each side of each pair of the full-length §5 run with the defaults took from 1250.509 to 1282.152 s for 53946 frames against a 1920x1080 reference, four processes at a time (`eval/consistency/defaults-full/checks.txt`, check 13). The clips' length and size set how long a rerun of `eval/run_all.sh` takes; a 4K reference is MEASUREMENT PENDING.
- With a millisecond (Matroska) reference against 90 kHz (MPEG-TS) distorted inputs whose frames line up, FFmpeg's side of the §5 check pairs some frames with an earlier reference frame (`a08` in `eval/consistency/align-cases-d028/`), so the containers and time bases of the mezzanine and the rungs matter for §5.
- §8 needs the clips listed in `eval/streams.md` with a way to fetch them, so their terms have to let others download them.

**Candidates examined** (`eval/streams/d011-candidates-2026-10-24.txt`, P-061). Eight public clips from four sources, examined on 2026-10-24 without downloading them: their license statements on the official pages, their download URLs, sizes and checksums or entity tags, and their properties from ffprobe or from their headers. The list is information, not a choice; the agent recommends none of them over another.

| clip | download URL (record, checks 4, 6 and 9) | license, as quoted from the official page (check 3) or file (check 5) | properties (checks 4 to 8) |
|---|---|---|---|
| El Fuente "Boat" (Netflix) | `http://download.opencontent.netflix.com.s3.amazonaws.com/ElFuente/Netflix_Boat_4096x2160_60fps_10bit_420.y4m`; a copy at `https://media.xiph.org/video/derf/ElFuente/Netflix_Boat_4096x2160_60fps_10bit_420.y4m` | Netflix's page: "Our open source content is available under the Creative Commons Attribution 4.0 International Public License"; the copyright file of 2015 next to Xiph.org's copy: "This video sequence is licensed under the Creative Commons Attribution-NonCommercial-NoDerivatives 4.0 International License" | YUV4MPEG2, 4096x2160, `F60:1`, progressive, 10-bit 4:2:0 (`C420p10`), 300 frames, 7962625844 bytes; the copyright file: "4096x2160p, 10-bit, YCbCr 4:2:0, BT.709, 59.94 fps, 300 frames" |
| El Fuente "Food Market" (Netflix) | `http://download.opencontent.netflix.com.s3.amazonaws.com/ElFuente/Netflix_FoodMarket_4096x2160_60fps_10bit_420.y4m`; a copy at Xiph.org | as for "Boat" | as "Boat", 600 frames, 15925251644 bytes; the copyright file: BT.709, 59.94 fps, 600 frames |
| `water_flyover` (SVT, Natural Complexity) | `ftp://svtopencontent.svt.se/pub/svt_videotestsuite_natural_complexity/REC709/water_flyover_JPEG2000_SDR_3840x2160p50_YUV444_12bit_Lossless.mov` | SVT's page, in Swedish: "Copyright SVT Open Content Video Test Suite 2022 - Natural Complexity är licensierat under Creative Commons Attribution 4.0 International License" | MOV, JPEG 2000, 3840x2160, 50/1 fps, `yuv444p12le`, matrix and primaries bt709, range and transfer not tagged, 1500 frames (30 s), 25453765853 bytes, its SHA-256 in the server's list |
| `smoke_sauna` (SVT) | the same directory, `smoke_sauna_JPEG2000_SDR_3840x2160p50_YUV444_12bit_Lossless.mov` | as for `water_flyover` | as `water_flyover`, 3000 frames (60 s), 42688158218 bytes |
| `forest_lake` (SVT) | the same directory, `forest_lake_JPEG2000_SDR_3840x2160p50_YUV444_12bit_Lossless.mov` | as for `water_flyover` | as `water_flyover`, 3000 frames (60 s), 52722599566 bytes |
| Sintel trailer (Blender Foundation; Xiph.org's copy) | `https://media.xiph.org/video/derf/y4m/sintel_trailer_2k_1080p24.y4m` | the film's page: "“Sintel” is licensed as Creative Commons Attribution 3.0." | YUV4MPEG2, 1920x1080, `F24:1`, progressive, 8-bit 4:2:0 (`C420jpeg`), 1253 frames, 3897338763 bytes |
| Big Buck Bunny (Blender Foundation; Xiph.org's copy) | `https://media.xiph.org/video/derf/y4m/big_buck_bunny_1080p24.y4m.xz` | the film's page: "The results of the Peach open movie project has been licensed under the Creative Commons Attribution 3.0 license." | as the Sintel trailer, 14315 frames; 10842485720 bytes compressed, 44525461935 uncompressed |
| Elephants Dream (Blender Foundation; Xiph.org's copy) | `https://media.xiph.org/video/derf/y4m/elephants_dream_1080p24.y4m.xz` | the film's page: "The movie “Elephants Dream” and all data on the DVDs and almost all of the contents on this website is licensed under the Creative Commons Attribution license." | as the Sintel trailer, 15691 frames; 7656985024 bytes compressed, 48805380591 uncompressed |

What the record shows about them, for the choice:

- **Terms.** For El Fuente, Netflix's page and the license file in its bucket name the Creative Commons Attribution 4.0 license, where the copyright files of 2015 at Xiph.org name Attribution-NonCommercial-NoDerivatives 4.0, whose NoDerivatives term would bear on publishing ladders encoded from the clips. Which terms hold is the owner's to establish, with D-008's question below. SVT's page names Attribution 4.0 for the suite. The Blender pages name Attribution 3.0 for Sintel and Big Buck Bunny and Attribution, without a version in the sentence quoted, for Elephants Dream; the pages of Big Buck Bunny and Elephants Dream exclude their logos and DVD artwork from the license.
- **Bit depth and color.** Only El Fuente is a 10-bit SDR source of the kind D-026's measurement needs; SVT's sequences are 12-bit 4:4:4, which an encode would first convert. YUV4MPEG2 files carry no color matrix, range, primaries or transfer: the El Fuente files and the Blender films' copies are untagged, so their tags would have to be set where they are read (D-021). The El Fuente headers give 60/1 fps, where their copyright files give 59.94 fps.
- **Length and size.** 300 to 15691 frames; the SVT files are 25 to 53 GB each and the El Fuente files 8 and 16 GB.

**Agent's recommendation.** Choose the clips and the encoding parameters, so that §1.2's acceptance and D-026's measurement can run on them now that the ladder mode is built (D-029); a 10-bit SDR source would serve D-026. Which clips is the owner's choice.

### D-008: whether the license has to be confirmed

D-008 chose GPL-3.0-or-later and left open whether legal or the company's open-source process has to confirm it before the repository is made public again. `LICENSE` is committed as D-008 decided (P-020). Two questions wait on the answer: the license texts next to glad's committed loader ("D-031: the license texts next to glad's loader", below) and the terms of the clips D-011 chooses (above). This is the owner's to check; the agent has no recommendation of its own.

### D-031: the license texts next to glad's loader (P-053)

**Question.** D-031 has the license terms of glad's committed loader stated next to it. Should the license texts be committed as well, and how do the terms sit with GPL-3.0-or-later?

**Today.** `third_party/glad/README.md` states the terms: the SPDX line of `gl.h` and `gl.c`, `(WTFPL OR CC0-1.0) AND Apache-2.0`, where the Apache-2.0 part is for what comes from the Khronos specification (glad's README); the Khronos Group's notice at the top of `khrplatform.h`; and glad's `LICENSE` at the pinned commit, for the generator and the Khronos files it ships. No license text is copied into the repository.

**Options.**

1. As today.
2. glad's `LICENSE` file next to the generated files.
3. The texts of Apache-2.0, and of WTFPL or CC0-1.0, next to them.

**Agent's recommendation.** None of the agent's own: this belongs with D-008's open question ("D-008" above), whether legal or the company's open-source process has to confirm the license, whose answer decides what the files need next to them.

## For information

What implementing the decisions found, which asks for no decision.

### target()'s range tag for a yuv420p reference (D-021; for information)

For a yuv420p reference without a range tag, distorted frames tagged limited in a yuvj format or one that isn't plain YUV stay limited in Mirage (`target()`, as implemented), where FFmpeg's graph tags them unspecified. libswscale converts both alike, so the frames libvmaf gets are the same (`tests/test_vmaf_color.cpp`), and the analyzer gives no warning; only the report's `dis_conversion` names another `to` range than FFmpeg's scale filter, which the delta tool would show as a warning. D-021 keeps the rules as implemented; `negotiated()` in their place would change that tag and no pixel.

### How eval/run_all.sh reruns the reference format cases (P-034)

`format-cases-after/` is rerun with the program of `d993686`, the commit its `build.txt` names, because HEAD's converts a reference that isn't yuv420p since D-022; `format-cases-d022/` is rerun with HEAD's, as the other runs made after D-016 are.

### D-028's timestamp origin and a clip cut with its timestamps kept (for information)

With times measured from each input's start (D-028), a clip cut from the reference with its timestamps kept, in a container that holds only its video, starts at its first frame and pairs from there: each frame with the reference frame as many frames earlier in the picture as were cut, as `ffmpeg` without `-copyts` pairs it, and only the reference frames left over at the end fail. With an earlier container start, such as an audio track from 0, the cut frames show as unmatched reference frames instead. The test "a later start, measured from each input's start" (`tests/test_vmaf_analyzer.cpp`) has both. With absolute timestamps, D-028's alternative, the cut clip would pair with the reference frames of its pictures.

### FFmpeg at the end of the distorted input, against the plan's reading (for information)

The plan's "What FFmpeg does" (`docs/plans/frame-alignment.md`) says, from the sources, that scoring ends with the distorted input. The runs show that once the distorted input has ended, FFmpeg's libvmaf filter scores each later reference frame against the last distorted frame (`a04`, `a05` in `eval/consistency/align-cases-d028/`): framesync keeps the distorted input's last frame, and its end hands the events to the reference (`libavfilter/framesync.c`). The plan's status line says so; its text is left as it was approved.

### The player's ladder analysis against mirage_vmaf's (for information)

The player's VMAF ladder analysis (`VMAFAnalyzer::start_manifest()`, from the "Manifest Variants" menu and `--vmaf`) is as before: every `EXT-X-STREAM-INF` entry but the one of the highest `BANDWIDTH` is scored against it, audio-only entries and entries that repeat a playlist included, in descending `BANDWIDTH`, a failed entry recorded as an error, without a mezzanine mode (`docs/VMAF_GAP_REPORT.md`, item 4). D-029 gives `mirage_vmaf` its own rules and chose not to mirror the player. Whether the player should follow `mirage_vmaf`'s rules is the owner's choice, a change to the player (D-007 if it touches its decode path), and is not made here.

### CLAUDE.md's build notes (for information)

`CLAUDE.md` still says that glad2 and miniaudio are fetched at configure time, and has a note on installing jinja2 for glad. Since D-031 glad isn't fetched and the build runs no Python. The file is the owner's and wasn't edited.

### Dear ImGui's loader define (for information)

`CMakeLists.txt` gives `imgui_impl` the define `IMGUI_IMPL_OPENGL_LOADER_GLAD2`, with a comment that glad is the loader of Dear ImGui's OpenGL 3 backend. The backend of the pinned Dear ImGui, v1.91.9, has used a loader of its own since 2021 (the changelog at the top of its `backends/imgui_impl_opengl3.cpp`), so the define has no effect. The line was left as it was.

### The full-length report with the defaults, and D-027's numbers (for information)

D-024's full-length §5 report with bicubic passes over every frame of the four pairs (`eval/consistency/defaults-full/`, P-059). D-027's rationale gives how far the three percentile definitions differ from runs with bilinear: at most 0.000276 over every frame and 0.093440 over 300 frames. With the defaults they differ by at most 0.001160 over every frame and 0.083044 over 300 frames, gear2's p1 in both (`eval/pooling/defaults-full/pooling.md`, `eval/pooling/defaults-300/pooling.md`). With the defaults too the minimum is the first frame's score in every series over every frame (`eval/pooling/defaults-full/pooling.md`). D-027 is the owner's and unchanged.

## The agent's process choice to confirm: P-066

D-033 confirmed P-006 to P-032, and D-041 P-033 to P-065 (in the decided sections below). The choices made in implementing D-035 to D-038 are P-066 in `docs/DECISIONS.md`, pending the owner's confirmation.

| Entries | What | Alternatives worth a look |
|---|---|---|
| P-066 | how D-035 to D-038 are implemented: the network limits in `src/VMAFNet.h`, the second minimum's name and computation, `MIRAGE_LIBVMAF_PACKAGE` and `--ffmpeg-libvmaf-package`; the negotiation cases' clips and run; the unit tests at `bbc669d`, and a rerun of every step, which a process that had stopped writing its log and the machine's sleep cut short, committed as it ended, with its two unfinished steps rerun on their own | FfOpen's own open in the player only; other names; a version variable that overrides the path's; other clips for (a) |

**Agent's recommendation**: confirm it.

## Decided on 2026-10-26 (D-034 to D-041)

D-034 adopted the agent's recommendations of this document as of `b2d2824`; D-035 to D-041 record each decision. The items keep the text they had then.

### D-022's wording and FFmpeg's negotiation, where FFmpeg changes the distorted frames' color (P-033)

**Decided** (2026-10-26): D-035: FFmpeg's negotiation, option 1, as the code has it since `22e2874`. The committed delta run of both kinds of input is `eval/consistency/negotiation-cases/`: all four cases pass, every frame within FFmpeg's 6 decimals (its `checks.txt`, check 4; P-066).

**The departure.** D-022 has Mirage convert a reference that isn't yuv420p to the distorted frames' color matrix and range, and leave the distorted frames' color as it is. FFmpeg 8.1.1's graph does that whenever the distorted input's buffer source offers one matrix and one range, that is, for plain YUV frames with a range tag or in a yuvj format, as in every committed case. For two kinds of distorted input it does not, and neither does Mirage, which follows FFmpeg there:

- (a) a distorted input without a range tag, against a reference that has one or is yuvj: libavfilter gives both inputs the reference's range, so a full range reference makes the distorted frames full range;
- (b) a distorted input in a format that isn't plain YUV, such as gray or RGB: both inputs get the reference's matrix and range.

**Today.** `vmafcolor::negotiated()` (`src/VMAFColor.h`) gives FFmpeg's tags. `tests/test_vmaf_color.cpp` compares them with libavfilter's for every combination of five kinds of pixel format on both inputs, with five tag sets on the reference and four on the distorted input, and the frames' bytes for the formats of `vmafcolor::verified_formats()`, distorted inputs without a range tag among them. No committed delta run has such an input (MEASUREMENT PENDING). The code has no switch for D-022's wording: for these inputs the wording would first need the owner's definition, such as which matrix a reference gets against distorted frames that aren't YCbCr.

**Options.**

1. FFmpeg's negotiation, as today: libavfilter's tags and, for the formats the test checks, the bytes libavfilter's graph gives libvmaf.
2. D-022's wording: convert only the reference, to the distorted frames' tags as they are, with a rule for distorted frames that aren't YCbCr. Mirage would then convert these inputs otherwise than plain FFmpeg, and §5 would be expected to fail for them (not measured).
3. Refuse such pairs, as pairs with a matrix libswscale can't convert are refused (D-021).

**Agent's recommendation.** Option 1: D-022's rationale is parity with a plain `ffmpeg -lavfi libvmaf` run, and its wording describes the committed cases, in which the two give the same. Whichever the owner chooses, a committed delta run of cases (a) and (b) would then show what both tools do with such inputs (MEASUREMENT PENDING).

### A network read without a time limit (P-062)

**Decided** (2026-10-26): D-036: option 2, the player's 5 s limits on a network input's reads and an interrupt callback on the analysis's stop token, in `b8eb634` (`src/VMAFNet.h`; P-066).

**What happened.** In the rerun of every step at `61241e2` the machine went to sleep with its lid closed while the last two pairs of `defaults-full` were being scored, and their inputs lost Apple's CDN (`eval/run_all/2026-10-24-full/README.md`, `power_log.txt`). Three of the four scoring processes of those pairs logged TLS reads that timed out and a host name that didn't resolve, skipped the segments they couldn't open and ran to their ends. The fourth, the `mirage_vmaf` of `gear2_vs_gear5`, logged no error: its stderr log has no line with "error", "fail" or "timed out". The workflow stopped it with SIGTERM, and it exited with status -15 after 5801.1 s of the time the machine was awake (`logs/defaults-full_plain.log` of that record; P-062). Whether it was waiting on a read then is in no committed file; the code and FFmpeg's defaults below are why it could have been.

**And again.** In the rerun of every step at `a4692a2` the machine went to sleep with its lid closed while the same two pairs were being scored (`eval/run_all/2026-10-26-fix/README.md`, `power_log.txt`; P-065). This time none of their four scoring processes, `mirage_vmaf` and `ffmpeg` alike, logged an error or skipped a segment. Their stderr logs were last written at the sleep, and after the machine woke each used 0.05 s of CPU time in 120 s (`stall_check.txt`): they made no progress until they were stopped with SIGTERM. What they were waiting on is in no committed file; the code and FFmpeg's defaults below are why a read could wait that long.

**After D-036.** In the rerun of every step at `bbc669d`, the first with D-036's limits, the machine went to sleep with its lid closed while three pairs of `defaults-full` were being scored (`eval/run_all/2026-10-26-d035/README.md`, `power_log.txt`; P-066). The stderr logs of `mirage_vmaf` in those pairs hold TLS reads that timed out, 4, 4 and 20 lines, and in `gear1_vs_gear5` segments that failed to open. It exited with status 0, its reports scoring 52442, 4425 and 15639 of the 53946 frame pairs, each with D-028's warning, most of the failed frames after a discontinuity, where D-028's rule stops pairing (`mirage.stdout.txt` of each pair). The `ffmpeg` of `gear2_vs_gear5`, which has no read limit, had last written its log 3 min 54 s before the sleep, and made no progress until it was stopped (`stall_check.txt`). The text below is as it was written before D-036.

**Today.**

- The analyzer opens each input with `avformat_open_input()` without options and without an interrupt callback (`DecodeCtx::open` in `src/VMAFAnalyzer.cpp`), and `src/VMAFLadder.cpp` reads a master playlist with `avio_open2()` without options.
- FFmpeg 8.1.1's TCP protocol limits a connection attempt to 5 s by default, but not a read or a write: its `timeout` option defaults to none, the `rw_timeout` of every protocol to 0, which is none, and TCP keepalive is off (`libavformat/tcp.c`, `avio.c` and `network.c` of the 8.1.1 sources). A read on a connection that stays open without data then waits until an interrupt callback stops it.
- The player's own inputs have limits: `src/FfOpen.cpp` sets `timeout` and `rw_timeout` to 5 s; `src/Demuxer.cpp` sets 5 s and 8 s, HTTP reconnects, an interrupt callback and the hls demuxer's `seg_max_retry` of 10, so that a segment that fails to open is tried again before it is skipped (FFmpeg's default skips it at once).
- The player's VMAF analysis is the same analyzer. By the code, its `cancel()` requests a stop, which the analysis checks between frames, and waits for the worker thread, so a cancel during such a read waits as long as the read.
- The §5 tool stops a scoring process after 6 h (`run_delta.py --timeout`, `DEFAULT_TIMEOUT`). The `ffmpeg` command of the §5 check sets no I/O limit either.
- When segments are skipped, D-028's pairing shows it: in `gear1_vs_gear5` of that rerun, `mirage_vmaf` counted 395 of 28920 frames as failed, 348 of them after a discontinuity, and warned (`eval/run_all/2026-10-24-full/consistency/defaults-full/plain/gear1_vs_gear5/mirage.stdout.txt`).

**Question.** Should the analyzer limit its network reads, for `mirage_vmaf` and the player's analysis, and how?

**Options.**

1. As today: no limit in the analyzer; the §5 tool's 6 h per process.
2. The analyzer sets `timeout` and `rw_timeout` on network inputs to the values of `src/FfOpen.cpp`, 5 s, and gives each input an interrupt callback on its stop token, so that a read that waits longer fails and a cancel interrupts a read; `src/VMAFLadder.cpp` sets the same limits for the master. A read that fails then makes the hls demuxer skip the segment, as in `gear1_vs_gear5` above.
3. Option 2 with the reconnect and `seg_max_retry` options of `src/Demuxer.cpp`, so that a short loss of the network is waited out on Mirage's side; FFmpeg's side of the §5 check would still skip the segment.
4. Option 2 or 3 with the limit as an option of `mirage_vmaf` and its config file (D-030), recorded in the report.

A limit changes no score of a run whose reads succeed. Where a read fails, the pair's frames differ from those of an uninterrupted run, and the §5 check fails the pair, as it does today when a segment can't be opened.

**Agent's recommendation.** Option 2, with the player's values: a read that waits longer becomes a failure that the report and the §5 check show, and the player's cancel interrupts a read. It changes the analyzer the player shares, so it waits for the owner (D-007).

### D-027: the minimum without the first frame (P-039)

**Decided** (2026-10-26): D-037: option 4, `min_without_context_first_frames` in the report next to D-027's value, in `c810480` (P-066).

**Question.** D-027 reports the minimum without the first frame under its own name. Which name, and what for a pair of one frame? Since D-028 a pair can be scored in several libvmaf contexts, and the first pair of each context has no motion to a frame before it, as the pair's first frame has none: should the value leave out the first pair of every context as well?

**Today.** `min_without_first_frame`, the name `eval/pooling/pooling.py` gives it: the smallest score of the pairs 1 to n − 1, `null` for a pair of one frame. It leaves out the pair's first frame only, however many libvmaf contexts the pair was scored in.

- Where a pair is scored in one context, its first frame is the only one without motion to a frame before it. So it is over every frame of the four bipbop pairs with the defaults: one context per pair (`eval/consistency/defaults-full/checks.txt`, check 6), and in FFmpeg's logs frame 0 the only frame whose `integer_motion2` is 0 (`eval/pooling/defaults-full/pooling.md`); with bilinear as well (`eval/pooling/README.md`, on `acceptance-full/`).
- Where D-028 starts a new context after a gap, the value can be the first pair of that context. Of the nine alignment cases scored since D-028, `a01_dropped_distorted`, the stream with a frame dropped on purpose, has two contexts and the others one (`eval/consistency/align-cases-d028/checks.txt`, check 5). In its report (`eval/consistency/align-cases-d028/main/a01_dropped_distorted/mirage.json`) `alignment.vmaf_contexts` is `[{first_index: 0, pairs: 5}, {first_index: 5, pairs: 4}]`, and `pooled.min_without_first_frame` is 72.71141969606174: the score of pair 5, the first pair of the second context, whose two frames score 74.78632698606295 in the control `a00_aligned`, where they start no context. The lowest score of the pairs that start no context is pair 1's, 74.77789802017037 (`align-cases-d028/checks.txt`, check 4, which lists every pair's score in both cases). The last pair of the first context differs from the control too, upward (pair 4: 74.9322319648982 against 74.91274986019386, the same check), since libvmaf's motion2 of a frame also uses the next frame (P-008).

**Options.**

1. As today: only the pair's first frame left out, as D-027 words it.
2. Another name for today's value, such as `min_excluding_first`.
3. The first pair of every libvmaf context left out instead, under the same name or another.
4. Both: today's value as D-027 names it, and, under a name of its own, the minimum without the first pair of every context; for a pair scored in one context the two are the same.

**Agent's recommendation.** Option 4: D-027's value stays as decided, and the second value carries D-027's reason for it, a minimum that says something about the encode, to every pair that has no motion to a frame before it, as `a01` shows the first pair of a later context to be. Which value, and under which names, is the owner's choice.

### D-006 outside Homebrew: the libvmaf package version on Linux (P-054, P-055)

**Decided** (2026-10-26): D-038: option 2, CMake's `MIRAGE_LIBVMAF_PACKAGE` and `run_delta.py --ffmpeg-libvmaf-package`, which the Dockerfile gives, in `97bd25a` (P-066); not built.

**Question.** D-006 has the reports record libvmaf's package version next to its run-time version. The package version is read from a Homebrew Cellar path: by `CMakeLists.txt` for `mirage_vmaf`'s reports, and by `eval/consistency/run_delta.py` for the libvmaf of FFmpeg's libvmaf filter. Outside Homebrew, on Linux and in the Docker image, both give `unknown`. What should they record there?

**Options.**

1. As today: `unknown`, with the run-time version as on macOS.
2. A CMake cache variable a build sets where the library's path names no version, which the Dockerfile would set to 3.1.0, the release it builds, and the same as an option of `run_delta.py`.
3. libvmaf's pkg-config version: it is the run-time version (3.0.0 for the package 3.1.0, D-006), so it would add nothing.

**Agent's recommendation.** Option 2: whoever builds libvmaf knows the release, and D-006 asks for both versions.

### The plan for the intermittent Demuxer test (D-032)

**Decided** (2026-10-26): D-039: experiments 1 to 3, as `docs/plans/demuxer-switch-test.md` describes them; experiment 4 only if they point at the switch. Their records: MEASUREMENT PENDING.

D-032 asked for a plan to find out whether the timing of `Demuxer: seamless video and audio switches on a local file` or the track switch is at fault; it is `docs/plans/demuxer-switch-test.md`. From reading the code, every recorded failure is a packet of the old track past the take-over point that a consumer took before the demuxer served the switch request, and the test releases its consumers right after the request; the plan's experiments tell the test's timing apart from a delay or a fault in the switch. Nothing on the decode path or in the test changes before the owner approves it (D-007).

**Decisions it needs.** Approval of its experiments 1 to 3, run on the machine of the measurements; and whether experiment 4 may run the player in scripted runs, if the first three point at the switch.

**Agent's recommendation.** Experiments 1 to 3; experiment 4 only if they point at the switch.

### A reference whose frames change size (P-063)

**Decided** (2026-10-26): D-040: option 1, the error of `8d14316` (P-063).

**Question.** The decisions don't cover a reference whose frames change size midway. Until `8d14316` the analyzer scored such a pair without an error or a warning: a yuv420p reference's frames of another size were copied with the first size's geometry, and a reference in another pixel format was converted back to the first size (read from the code). What should it do?

**Today** (`src/VMAFAnalyzer.cpp`; the test "a reference whose frames change size is an error" in `tests/test_vmaf_analyzer.cpp`): the analysis ends with an error that names both sizes and the reference frame's position in its input, so `mirage_vmaf` writes no report and exits with status 1 (P-006), and the player's VMAF panels show the error as they show the analyzer's other errors (not seen on screen). The check is made on decoded frames, so no decoding changes, nor anything for a reference whose size stays the same. A distorted input whose frames change size is scaled frame by frame to the reference's size, as before. FFmpeg's libvmaf filter takes inputs of one size only (`eval/consistency/ffmpeg-libvmaf-filter/checks.txt`, check 4). No committed run has an input whose size changes, and what FFmpeg's §5 graph does with one is not measured in a committed run (MEASUREMENT PENDING).

**Options.**

1. As today: an error.
2. Each reference frame of another size converted to the first size, as D-022's conversion did for references in other pixel formats, with a warning.
3. Each run of frames of one size scored in a libvmaf context of its own, at that size, with the distorted frames scaled to it, and the sizes recorded in the report.

**Agent's recommendation.** Option 1: FFmpeg has no score to match for such a pair, and an error can't pass for a complete report.

### The warning that remains of P-030 (P-033)

**Decided** (2026-10-26): D-040: option 1, today's warning.

**Question.** What should the analyzer warn about once it converts such references as FFmpeg does?

**Today.** It warns, once per input, when frames it converts, the distorted frames always and the reference when it isn't yuv420p, are in a pixel format outside `vmafcolor::verified_formats()`: the formats `tests/test_vmaf_color.cpp` compares byte for byte with the graph run through libavfilter, as reference and as distorted input. NV12 and NV21 are outside: with a change of matrix, libswscale converts them differently on four threads than on one (the same test), and FFmpeg's scale filters use as many threads as `ffmpeg` finds CPUs, where Mirage uses one (P-014). The player shows no warning.

**Options.**

1. Today's warning.
2. No warning.
3. A warning only for formats that aren't plain YUV, which no committed delta run covers.
4. As many swscale threads as FFmpeg's scale filters use, which might give NV12 and NV21 FFmpeg's bytes on the same machine (not measured), with a heavier background analysis in the player.

**Agent's recommendation.** Option 1: it says exactly where the conversion isn't checked, and changes no score.

### D-030: what a config file accepts (P-040)

**Decided** (2026-10-26): D-040: as today.

**Question.** D-030 decides the format (`name = value` lines with the long options' names), the option table, the command line's precedence and the report's path and SHA-256. Which of the details it leaves open should hold?

**Today** (`src/VMAFCli.h`; tests in `tests/test_vmaf_cli.cpp`):

- Blank lines and lines whose first character other than white space is `#` are skipped; there are no comments after a value, no quotes and no escapes: the value is the rest of the line after the first `=`, without the white space around it.
- Every option but `--config` can be set in the file, the inputs and the output included, each once; each value is checked as on the command line, also where the command line replaces it.
- `--model` and `--model-file` are one setting: either on the command line replaces either in the file; both in one place are an error.
- Relative paths are taken from the current directory, as on the command line.
- A file that can't be read or has an error ends `mirage_vmaf` with status 2, the file and line named.

**Options.** As today; comments after a value; only the scoring settings in the file, not the inputs and the output; relative paths from the file's directory; settings the command line replaces left unchecked.

**Agent's recommendation.** As today: nothing a command line can say is written differently in the file, and a file with an error doesn't run.

### D-028: the values its pairing leaves open (P-043, P-044)

**Decided** (2026-10-26): D-040: as today.

**Question.** D-028 fixes the rule, the tolerance, the timestamp origin, options (b) and (a), the restarted libvmaf contexts, the report's fields and #7. Which of the values it leaves open should hold?

**Today** (`src/VMAFAlign.h`; tests in `tests/test_vmaf_align.cpp` and `tests/test_vmaf_analyzer.cpp`):

- **Frame rates**: `av_guess_frame_rate()`, the rate `ffmpeg` gives its filter graph for a stream. A reference without one ends the analysis with an error, as do time bases with no common one within 2^40 ticks per second; a distorted input without one is taken to have the reference's.
- **The tolerance** includes its bound. Each frame of the grid input takes the nearest frame of the other input within it, the earlier of two equally near.
- **The coarser grid**: frame rates compared exactly. A frame of the finer input that pairs with nothing fails only as the nearest to a point of the grid that no frame of the grid input takes, nearer than the tolerance; the others are off the grid, counted and not failures, and a frame halfway between two points counts for neither.
- **Discontinuities**: a timestamp that goes back at all, or forward by more than 10 s, the default of `ffmpeg`'s `dts_delta_threshold`. `ffmpeg` itself takes a backward step for a discontinuity only beyond 0.1 s.
- **#7's boundary**: a failure rate of exactly 1% doesn't warn, as D-023's boundary passes a frame exactly at 0.01.
- **Without a container start time**, times count from the input's first timestamped frame, as the plan says; `ffmpeg` then shifts nothing.
- **Both inputs are read to their ends**, unless the frame limit stops the run, so that the rest of the longer input is counted. Before D-028 the pairing stopped at the shorter input's end.
- **The report** has, beyond the plan's fields, the grid, each input's frame rate and start, the frames that failed once pairing stopped (`after_discontinuity`), the discontinuity limit, the frames off the grid, the libvmaf contexts, and each pair's frame positions in their inputs.
- **No option sets them**: like libvmaf's threads, these values are fixed in the code, so neither `mirage_vmaf`'s command line nor its config file has them; the report records them.

**Options.** As today; any of the alternatives of P-043 and P-044; or some of the values as options, each one more row of `mirage_vmaf`'s option table and so in the config file as well (D-030).

**Agent's recommendation.** As today: each value is FFmpeg's where FFmpeg has one and the plan's otherwise, and the tests pin each.

### The delta tool's comparison by timestamp (P-047)

**Decided** (2026-10-26): D-040: as today.

**Question.** D-028 has the §5 delta tool compare the two sides by timestamp, a pair passing only if both sides scored the same timestamps, and the differences in what each side counted as failed reported. Which of the details it leaves open should hold?

**Today** (`eval/consistency/vmafdelta.py`; tests in `eval/consistency/tests/`):

- FFmpeg's pairs come from three `showinfo` filters in its graph and FFmpeg 8.1.1's framesync rule; where they aren't the frames libvmaf passed on and scored, the pair fails and no scores are compared.
- A pair of one side is a pair of the other when it has the same frames, by their positions in the inputs, and both frames must have the same timestamps on both sides: pairs of the same frames whose timestamps `ffmpeg` corrected fail as `TIMESTAMP MISMATCH` (`a07_restart` in `eval/consistency/align-cases-before/`).
- Every frame the two sides treat differently, `mirage_vmaf`'s failures included, is listed and warned about; it fails the pair only through its pairs or its scores.
- The pairs further apart than the tolerance are counted with half the reference's frame duration from `mirage_vmaf`'s alignment, or from ffprobe's `r_frame_rate` for a report without one.
- P-009's warning about `mirage_vmaf` pairs of two different PTS is gone; the comparison settles it.
- FFmpeg's verbose log is longer: for each frame and each of the three filters, a line with its timestamp, one with its color tags, and one for each item of side data the frame carries; over every frame of a bipbop pair the log holds 91659880 to 92092360 bytes, 1877848 to 1903556 bytes as committed, xz-compressed (`eval/consistency/defaults-full/checks.txt`, check 12).
- With `--frames N`, FFmpeg's `trim` passes the first N frames of each input on (P-008), where `mirage_vmaf` scores N pairs (P-044). Where the inputs' frames line up the two are the same frames; where they don't, they can be other frames, and the comparison then reports the pairs only one side scored. The alignment runs score every frame.

**Options.** As today; pairs matched by their timestamps only; failing a pair on the differences in failure accounting too; the comparison by position as a fallback where FFmpeg's pairs can't be established; FFmpeg's `metadata` filter writing the frames to separate files, which would leave the verbose log as it was.

**Agent's recommendation.** As today: the positions name the frames exactly, so a pair both sides scored is scored on the same pictures, and a pairing the tool can't establish fails rather than passing pairs nobody knows.

### What the alignment cases show where the two rules differ (P-048)

**Decided** (2026-10-26): D-040: option 1, with case (4) decided again with D-011's ladders.

**Question.** Where the inputs' frames don't line up, Mirage's pairing (D-028) and FFmpeg's libvmaf filter score other pairs, so §5 fails there by design (difference 12). The runs show where (`eval/consistency/align-cases-d028/`; `eval/consistency/README.md`, "`align-cases-before/`, `align-cases-d028/`"):

1. FFmpeg scores pairs of frames of different times where Mirage counts failures: a distorted frame with the reference frame before it when a reference frame is missing (`a02`), both frames under a duplicated timestamp (`a03`), and, once the distorted input has ended, each later reference frame against the last distorted frame (`a04`, `a05`).
2. With a Matroska reference, whose timestamps are milliseconds, against a 90 kHz MPEG-TS distorted input whose frames line up, FFmpeg's framesync pairs 4 of the 10 distorted frames with the reference frame before theirs, whose rounded time is later (`a08`). Framesync has an option for the nearest frame, `ts_sync_mode=nearest` (`eval/consistency/ffmpeg-libvmaf-filter/checks.txt`, check 3), which the §5 graph doesn't set.
3. After a gap Mirage starts a new libvmaf context (D-028), where FFmpeg's motion features span the gap, so the pair after the gap scores differently (`a01`).
4. `ffmpeg` corrects MPEG-TS timestamps that restart and pairs every frame, where Mirage stops pairing (D-028's option (a); `a07`).
5. With a later start in a container that starts with its video, both sides measure from each input's start and pair the frames three pictures apart (`a05`); in a container that starts at 0 both pair the same frames, and the pair passes with Mirage's unmatched frames reported (`a06`).

Should §5 stay as it is for such inputs?

**Options.**

1. As today: the §5 comparison is FFmpeg's plain graph; for inputs whose frames don't line up the reports show the differences, and the paper states that parity holds where the frames line up.
2. Options in FFmpeg's graph that make it pair more as rule A does, such as `ts_sync_mode=nearest` for (2); the comparison would then no longer be a plain `ffmpeg -lavfi libvmaf` run.
3. Mirage following FFmpeg in some of them, such as `ffmpeg`'s correction of timestamp jumps for (4), the plan's option (c); this changes D-028.

**Agent's recommendation.** Option 1 now, and (4) decided again with D-011's ladders, as D-028 foresees for discontinuities.

For D-011, whose clips and encoding parameters stay the owner's: (2) shows that a reference and distorted inputs whose timestamps have different time bases can make FFmpeg's side pair other frames although the frames line up.

### D-029: the values its ladder scoring leaves open (P-050)

**Decided** (2026-10-26): D-040: as today.

**Question.** D-029 fixes the design of ladder scoring in `mirage_vmaf`: top-rung by default, a mezzanine optionally; each video playlist scored once; entries without video skipped and listed as skipped; one failed pair fails the run; one report per reference mode, with a pairs list. Which of the details it leaves open should hold?

**Today** (`src/VMAFLadder.h`; tests in `tests/test_vmaf_ladder.cpp`, `test_vmaf_cli.cpp` and `test_vmaf_report.cpp`):

- **What an entry is**: an `EXT-X-STREAM-INF` tag with the URI line after it, in an HLS master playlist (no DASH). I-frame playlists and `EXT-X-MEDIA` renditions, video renditions included, aren't entries and aren't scored: D-029's "each video playlist" is read as the playlists of these entries, the ones the §1.2 package asked about.
- **What has video**: an entry with a `RESOLUTION`, or with a video codec's sample entry in `CODECS` (`avc1`, `avc3`, `hvc1`, `hev1`, `dvh1`, `dvhe`, `av01`, `vp09`, `vp08`, `mp4v`), the rule of `eval/streams/fetch_streams.py`; read from the master, without opening the entry. An entry without video is skipped also when it names a video playlist.
- **The top rung**: the playlist of the video entry with the highest `BANDWIDTH`; of equals that name different playlists, the first in the master, with a warning in the report, and on stderr when the run ends.
- **A master read strictly**: the first line `#EXTM3U`, every entry with a `BANDWIDTH` of digits only, up to 2^63 − 1, and a URI line, no URI line without an entry, every URI resolvable (for http(s), relative and absolute paths and http(s) URLs; not dot segments, `//host` references, query- or fragment-only references or other schemes; for a local master, the URI joined to the master's directory as it is, dot segments included, or taken as it is when it has a scheme or starts with `/`). Anything else, or a master with nothing to score, ends the run without a report, exit status 1, as `mirage_vmaf --help` says. FFmpeg's hls demuxer and the player take a missing `BANDWIDTH` as 0.
- **The order of the pairs**: the master's, by each playlist's first entry; the player's `start_manifest()` scores in descending `BANDWIDTH`.
- **One mode per run**: `--mezzanine` replaces the top rung as the reference and scores the top rung's playlist too; both modes on one ladder are two runs, two reports.
- **The first failed pair stops the run**, since no report follows it; each pair decodes the reference again.
- **The command line**: `--ladder` and `--mezzanine` exclude `--ref` and `--dis` in one place; in a config file and on the command line, the command line's form leaves out the file's other form.

**Options.** As today; any of the alternatives of P-050: scoring `EXT-X-MEDIA` video renditions as well, probing each entry for video instead of reading its attributes, taking a missing `BANDWIDTH` as 0, refusing a master with equal highest bandwidths, ordering the pairs by `BANDWIDTH`, both modes in one run, scoring every pair before failing.

**Agent's recommendation.** As today: the rules are FFmpeg's where FFmpeg reads the master and the repository's where it already decides what has video, and a master the tool would have to guess about is refused rather than scored with a reference nobody chose.

### D-031: the headless configuration's details (P-054)

**Decided** (2026-10-26): D-040: as today.

**Question.** D-031 fixes a CMake option without the player's dependencies, a Linux preset and Linux instructions. Should the details it leaves open hold?

**Today** (`CMakeLists.txt`, `CMakePresets.json`, `docs/BUILD.md` steps 3 and 6):

- The option is `MIRAGE_HEADLESS`, off by default. On, it leaves out OpenGL, GLFW, glad, Dear ImGui and nativefiledialog-extended, and also stb and the player's snapshot check (`snapshot_diff`, `pathb_regression`), which only the player uses; it builds `mirage_vmaf` and all of `mirage_tests`.
- A third macOS preset, `macos-headless`, so that the headless configuration is built and checked on macOS with ffmpeg-full; its `mirage_vmaf` is identical to the release preset's (`eval/build_check/macos-2026-10-24-5ae44a4.txt`).
- One Linux preset, `linux-release`, Release only, offered only on a Linux host, with no `PKG_CONFIG_PATH`.
- `eval/run_all.sh` is unchanged. On Linux it builds the older commits' programs with their `macos-release` preset, as on macOS, which needs the player's configure-time dependencies there.

**Options.** As today; any of P-054's alternatives: the name `MIRAGE_BUILD_PLAYER`; stb and `snapshot_diff` kept in the headless configuration; only the VMAF tests; a Linux debug preset; no `macos-headless` preset.

**Agent's recommendation.** As today: the option leaves out exactly what only the player uses, and each preset is a configuration a step of `docs/BUILD.md` gives.

### D-031: the Docker image's details (P-055)

**Decided** (2026-10-26): D-040: as today, Debian's packages to be pinned from `snapshot.debian.org` once a container runtime shows that the image builds.

**Question.** D-031 fixes a base image pinned by digest, FFmpeg 8.1.1 and libvmaf built from release sources checked by SHA-256 at the measured versions, `mirage_vmaf` built from the repository, and `eval/run_all.sh` as the command. Should the details it leaves open hold?

**Today** (`docker/Dockerfile`, `docker/README.md`; not built, MEASUREMENT PENDING):

- The base image is Debian 13 slim of 2026-10-20, by the digest of its image index (`docker/base-image.txt`).
- FFmpeg has the configure options of Homebrew's ffmpeg-full formula for what the evaluation uses (GPL version 3, GnuTLS, libvmaf, libx264), without the formula's other external libraries, its macOS options and `ffplay`; libvmaf the formula's meson options and its model files.
- x264 is the commit of Homebrew's x264 formula r3222, the H.264 encoder of the reference format clips.
- Debian's packages are not pinned; the image lists the versions it installed.
- The build context is the repository's `.git` alone, cloned with `--single-branch`: the image holds the commit checked out where it is built, with its branch's history and a clean working tree.
- The image has the development files of OpenGL, GLFW and GTK 3, which the older commits whose programs `run_all.sh` builds need to configure.
- Its entry point is `run_all.sh` with the image's `mirage_vmaf`, FFmpeg and model directory, `/out` its default output directory; it runs as root.

**Options.** As today; any of P-055's alternatives: Ubuntu 24.04 or another base; Debian's packages from `snapshot.debian.org` at the base image's date, or each pinned by version; all of the formula's external libraries; Debian's libx264; the older programs built into the image, or an image without the steps that need them; a user other than root.

**Agent's recommendation.** As today, adding Debian's packages from `snapshot.debian.org` at the base image's date once a container runtime can show that the image builds: the compilers and libraries would then be pinned as the base image is.

### Difference 9: the player's own JSON export

**Decided** (2026-10-26): D-040: left until §7's schema.

§1.1 to §1.3 ask the output to carry the libvmaf version and the model identifier, the `reference_mode` and the scaler. `mirage_vmaf`'s report does (`src/VMAFReport.h`). The player's own JSON export (`VMAFAnalyzer::write_json` in `src/VMAFAnalyzer.cpp`) still has its Phase 0 layout, which a test pins, without any of them, and the player has no scaler or model option. Since D-024 and D-027 the player's analysis scales with bicubic and its p5 is the interpolated one, so the export's numbers follow the new defaults without naming them.

**Options.** Give `write_json` those fields; have the player write `mirage_vmaf`'s report format; leave the player's export until §7's schema.

**Agent's recommendation.** Leave it until §7's schema, which decides one format for both; the paper's numbers come from `mirage_vmaf`'s reports. D-020 noted that the package had no firm recommendation, and this one is new.

### Commit messages that state a wrong count or rest on no record (for information)

**Decided** (2026-10-26): D-040: left as they are, disclosed in `docs/AI_CONTRIBUTIONS.md`.

The agent doesn't amend commits, so these messages stay as they were written; `docs/AI_CONTRIBUTIONS.md` lists them, and the documents and P-entries state the facts correctly:

- `1d4a261` says the `LICENSE` text is the revision "the FSF publishes today", which was not checked against gnu.org; P-020 doesn't rely on it.
- `ee29d56` says the release build had nothing to rebuild after the reconfigure; a committed record backs only that `mirage_vmaf` kept its SHA-256 (`eval/build_check/macos-2026-10-23-6a6661f.txt`).
- `22e2874` says its negotiation test covers five tag sets on both inputs; the test has five on the reference and four on the distorted input (`tests/test_vmaf_color.cpp`; P-033).
- `f718679` names the closed captions of bipbop's frames, seen in a scratch run only.
- `a0d69fc` says the HLS variants test failed in release builds, the headless one included, and `8720a6e` that reconfiguring an existing build tree rebuilt nothing; both rest on scratch runs, not on a committed record (P-057).

**Options.** Leave them as they are, disclosed; or correct them in a rewrite of the branch's history before the PR merges, as D-012 rewrote the history once, which gives every later commit a new SHA.

**Agent's recommendation.** Leave them, disclosed: the committed records and the documents name commits by SHA (each `build.txt`, `eval/run_all/`, `eval/test_runs/`, the P-entries), and a rewrite would leave them naming commits that no longer exist, as D-012's rewrite left the old SHAs in the raw outputs with a table to map them.

### The agent's process choices P-033 to P-065

**Decided** (2026-10-26): D-041 confirmed P-033 to P-065 as their entries state them.

D-033 confirmed P-006 to P-032 (listed in the decided section below). The measurement and process choices made in implementing D-020 to D-033 are P-033 to P-065 in `docs/DECISIONS.md`, each pending the owner's confirmation; where one leaves a question open, the question is above. The alternatives worth a look, as each entry lists them:

| Entries | What | Alternatives worth a look | Open question above |
|---|---|---|---|
| P-033 | how D-022 is implemented: FFmpeg 8.1.1's negotiation for any reference, the reference converted with `sws_scale_frame()` and bicubic flags on one thread, `ref_conversion`, the warning for formats the conversion test doesn't check | D-022's wording where FFmpeg changes the distorted frames' color; no warning, or one for formats that aren't plain YUV only; as many swscale threads as FFmpeg | "D-022's wording and FFmpeg's negotiation" (a departure from D-022's wording, first above), "The warning that remains of P-030" |
| P-034 | the reference format cases after D-022, and `format-cases-after` rerun with the program of `d993686` | `format-cases-after` with HEAD's program; the program of `9855319` | "How eval/run_all.sh reruns the reference format cases" |
| P-035 | the run-time versions of five FFmpeg libraries in the report; a warning when the two sides' differ | the build-time versions as well; a failure | |
| P-036 | D-032's fixes: a temporary directory per test process, the subtitle test's fixture written by the test, the analyzer's decode context freed by its destructor | a directory per test case; a committed fixture | |
| P-037, P-042, P-046, P-049, P-052 | the reruns of the committed measurements after each round, `--skip-full`, from clean worktrees | every step each time | |
| P-038 | D-024's bicubic default in one place; `run_delta.sh`'s default on both sides; every rerun naming its scaler, model and threads | `run_delta.sh` left at bilinear; the earlier command blocks rewritten | |
| P-039 | D-027's pooled values computed from the per-frame scores, sums in frame order, without fused multiply-add; `pooling.py --check-pooled` | `vmaf_score_pooled()` for the mean, minimum and harmonic mean; exactly rounded sums; another name for the minimum without the first frame | "D-027: the minimum without the first frame" |
| P-040 | what a config file accepts | comments after a value; only the scoring settings; paths relative to the file | "D-030: what a config file accepts" |
| P-041 | the 300-frame run with the defaults and its checks | the checks uncommitted; `--scaler bicubic` named | |
| P-043 | the aligner's rules and the values D-028 leaves open | P-043's alternatives (frame rates, tolerance bound, grids, discontinuities, the 1% boundary) | "D-028: the values its pairing leaves open" |
| P-044 | the analysis with the pairing: both inputs read to their ends, libvmaf contexts, the warning's channels, the report's fields | decoding threads per input; pairing stopped at the shorter input's end; every warning in the player | the same, and "D-028's timestamp origin" |
| P-045 | the analyzer tests of D-028 on generated clips, §1.4's acceptance among them | clips made by the `ffmpeg` program; committed fixtures | |
| P-047 | the delta tool's comparison by timestamp | pairs matched by timestamps only; failing on the differences in failure accounting; the comparison by position as a fallback | "The delta tool's comparison by timestamp" |
| P-048 | the alignment cases before and after D-028, and §1.4's acceptance in them | clips made with libav*; a bipbop rung with frames removed | "What the alignment cases show where the two rules differ" |
| P-050 | D-029's ladder scoring and the values it leaves open | `EXT-X-MEDIA` video renditions scored as well; a missing `BANDWIDTH` taken as 0; the pairs by `BANDWIDTH`; both modes in one run | "D-029: the values its ladder scoring leaves open" |
| P-051 | the ladder check and its run on bipbop's master | a delta run in ladder mode; every frame | |
| P-053 | glad's loader committed, `regenerate.sh`, the license terms stated | glad from PyPI; the license texts copied | "D-031: the license texts next to glad's loader" |
| P-054 | `MIRAGE_HEADLESS`, the presets `macos-headless` and `linux-release`, the Linux instructions | `MIRAGE_BUILD_PLAYER`; a Linux debug preset | "D-031: the headless configuration's details" |
| P-055 | the Dockerfile, not built | another base image; Debian's packages pinned; all of the formula's libraries | "D-031: the Docker image's details", "D-006 outside Homebrew" |
| P-056 | the README's build part a pointer to `docs/BUILD.md`, its stale facts corrected; `vcpkg.json` removed | a short quick start kept; the thread model completed | |
| P-057 | a test fixed: a stop token that outlives the context it guards | the test left as it was; `FfOpen` keeping its own copy of the token | |
| P-058 | the build check at `5ae44a4` | the venv steps kept; the Python suites in the check | |
| P-059 | the full-length §5 report with the defaults, its pooling check, its `run_all.sh` steps and checks | the scaler and model named in the run; `--jobs 1` | |
| P-060 | the records at the end of the round: the unit tests at `12715a6`, and a rerun of every step of `eval/run_all.sh` | `--skip-full`; the unit tests in a clean worktree | |
| P-061 | information for D-011: eight candidate clips examined without downloading them | other sources; downloading the clips | "D-011" |
| P-062 | the rerun of every step at `61241e2`, which the machine's sleep cut short: its record with the failed run's reports, logs and the machine's sleep and wake events; a scoring process stopped before the 6 h limit; its two unfinished steps rerun on their own | the whole rerun again; only the usual files of the record; the process left to the 6 h limit | "A network read without a time limit" |
| P-063 | a reference whose frames change size ends the analysis with an error, in `mirage_vmaf` and the player | converting such frames to the first size, with a warning; a libvmaf context for each size | "A reference whose frames change size" |
| P-064 | `run.cmd.txt` names the options its command line gave, besides every option the run had | the arguments with their values; the same line in `delta.json` | |
| P-065 | the records after the review fixes: the unit tests at `a4692a2`, and a rerun of every step at `a4692a2`, which the machine's sleep cut short, committed with the failed run's reports, logs, sleep and wake events and the stalled processes' CPU times; its two unfinished steps rerun on their own | the whole rerun again; the processes left to the 6 h limit | "A network read without a time limit" |

## Differences between the work order and the implementation found in this work (hard rule 4)

Reported, not edited:

1. §1.3 gives §5 passing as the reason for bicubic; §5 passes with all three scalers (#3). D-024 chose bicubic, the work order's suggestion and FFmpeg's scale filter's default.
2. §7's `reference_mode` has only `mezzanine` and `top-rung`; `mirage_vmaf` writes `explicit-pair` (§1.2 below). D-029 keeps `explicit-pair`; §7's enumeration gains it when the schema is written.
3. §5's command, `ffmpeg -i <distorted> -i <reference> -lavfi libvmaf=model=<same>`, has no scale filter, and FFmpeg's libvmaf filter refuses inputs of different sizes (`input width must match`, `eval/consistency/ffmpeg-libvmaf-filter/checks.txt`, check 4). The §5 tool adds `scale=W:H:flags=<scaler>` to the distorted input, as Phase 0's diagnostic did (P-005, P-008). The paper's method text states it (D-032; `docs/PAPER_METHOD_NOTES.md`, statement 20).
4. §8 asks for FFmpeg 8.x, libvmaf 3.x, OpenGL 4.1, miniaudio and GLFW to be pinned, with their exact versions recorded. The versions are recorded (`docs/BUILD.md`, "Versions"), miniaudio is pinned by commit, and the generated loader fixes OpenGL at 4.1 core; FFmpeg, libvmaf and GLFW come from Homebrew, which can't pin them (#11c). Since D-031 the Docker image pins FFmpeg 8.1.1, libvmaf 3.1.0 and x264 by SHA-256 or commit, but not Debian's packages, and it hasn't been built (`docker/README.md`).
5. §8 asks for build instructions for Linux and macOS, Apple Silicon included. Only macOS on Apple Silicon is documented and checked (#11b). Since D-031 `docs/BUILD.md` has Linux instructions for `mirage_vmaf` and the unit tests, without the player, which haven't been built on Linux (P-054).
6. §8's acceptance is a clean machine building as the README says. On the machine of the build checks, the release preset stops at glad with the default Python, and the README's quick start (`macos-debug`) runs the same glad step; the README now points to `docs/BUILD.md`, whose steps build both presets; and the check ran in a clean worktree, not on a clean machine (P-022). Since D-031 the README's build part points to `docs/BUILD.md`, and in the check at `5ae44a4`, again in a clean worktree, both presets and the headless preset built with no Python involved (`eval/build_check/macos-2026-10-24-5ae44a4.txt`, P-058).
7. §8 has `eval/run_all.sh` rerun all the paper's numbers and figures. There are none yet (D-005), so it reruns the committed Phase 1 measurements (P-024).
8. §8 asks for `eval/streams.md` with a fetch script, and lists DASH-IF test vectors and Unified Streaming demo streams. `eval/streams/fetch_streams.py` fetches the playlists and probes the streams but keeps no media (P-023), and the DASH-IF and Unified Streaming streams were not examined (`eval/streams.md`).
9. §1.1 to §1.3 ask the output to carry the libvmaf version and the model identifier, the `reference_mode` and the scaler. `mirage_vmaf`'s report does (`src/VMAFReport.h`). The player's own JSON export (`VMAFAnalyzer::write_json` in `src/VMAFAnalyzer.cpp`) still has its Phase 0 layout, which a test pins, without any of them, and the player has no scaler or model option. Options: give `write_json` those fields; have the player write `mirage_vmaf`'s report format; leave the player's export until §7's schema. Still open: "Difference 9" above.
10. §5's target is a per-frame difference "< 0.01"; D-019 passes a frame at exactly 0.01 (D-019 below). D-023 keeps D-019, and the paper's method text states the boundary (`docs/PAPER_METHOD_NOTES.md`, statement 21).
11. §5's target, under 0.01 per frame with the default configuration, is met for yuv420p references, and not for every reference in another pixel format: not where its tags differ from the distorted input's, nor for the full range yuvj420p reference with the same tags. The reason is the color conversion, which §5 asks the documents to state ("D-016: references that aren't yuv420p" below; `eval/consistency/README.md`). Since D-022 it is met in every reference format case as well (`eval/consistency/format-cases-d022/`), with the scaler of the committed runs, bilinear.
12. §5's target is not met for inputs whose frames don't line up: of the 9 synthetic alignment cases 2 pass, the control whose frames line up and the later start in a container that starts at 0, in which both sides pair the same frames with the same scores (`eval/consistency/align-cases-d028/`). The reason is frame alignment, which §5 names: D-028's rule A, its new libvmaf context after a gap and its pairing stopped at a discontinuity differ from FFmpeg's framesync ("What the alignment cases show" above; `eval/consistency/README.md`).
13. §7's schema lists a ladder's scores as `ladder [ { variant_uri, bandwidth, resolution, codecs, frames [ { pts, vmaf } ], pooled { ... }, alignment { ... } } ]`. `mirage_vmaf`'s ladder report has the master's entries, with their attributes, under `ladder`, and the scores under `pairs`, one per video playlist, each with the entries that name it, its frames, pooled values and alignment (D-029; `src/VMAFReport.h`). The schema is §7's work, in Phase 5.

## Decided on 2026-10-23 (D-020 to D-033)

The packages below were open until the owner's decisions of 2026-10-23: D-020 adopted the agent's recommendations in them, and D-021 to D-033 record each decision. Each keeps the text it had when it was decided, as the record of its options and of the measurements behind them; the note at its start says what was decided and where the work is. "Today" in them is the day each was written, and "above" and "below" refer to this section unless they name another.

### D-016: untagged streams, and matrices libswscale can't convert (§5)

**Decided**: D-021, option 1 and (a), with the FFmpeg libraries' versions in the report. **Done**: the rules for a yuv420p reference are as before (`src/VMAFColor.h`, `target()`); `mirage_vmaf`'s report records the run-time versions of libavutil, libavcodec, libavformat, libswscale and libavfilter (`ffmpeg_libraries`, P-035), and the delta reports show them next to those of the `ffmpeg` program (`eval/consistency/format-cases-d022/checks.txt`, check 8).

**Question.** D-016 has Mirage convert the distorted frames' color matrix and range to the reference's, as FFmpeg's filter graph does, with untagged streams handled the way FFmpeg handles them, and asks the agent to report what that means in practice for the owner to confirm. Is the behavior below what D-016 means?

**Today** (`src/VMAFColor.h`, P-014). For a yuv420p reference, Mirage does what FFmpeg 8.1.1's §5 graph does in the color tag cases (`eval/consistency/README.md`, "Color tag cases"; `eval/consistency/color-cases-before/`). This is what the code does now, in `mirage_vmaf` and in the player, whose VMAF panel runs the same analyzer; a decision other than option 1 and (a) below changes that code, and the committed runs after D-016 that it affects would have to be made again. For a reference that isn't yuv420p, FFmpeg's graph does something else: next package.

- The distorted frames get the reference's matrix tag, a missing tag included.
- They get the reference's range tag. When the reference has none, they keep a limited or missing range, and a full range becomes unspecified.
- In the pixels, libswscale takes a missing matrix as BT.601's and a missing range as limited. So an untagged distorted input against a BT.709 reference is converted from BT.601 to BT.709, a BT.709 distorted input against an untagged reference is converted to BT.601, and a full range distorted input against a reference without a range tag is converted to limited range.
- The primaries and the transfer are never converted; the chroma siting is dropped, and interlaced frames are scaled as progressive.
- A matrix libswscale has no coefficients for, such as YCgCo, on either input ends the analysis with an error, in the player too; `mirage_vmaf` then writes no report and exits with status 1. FFmpeg can't build its graph for such a pair: it exits with status 178 with YCgCo on either input (`color-cases-before/refused/delta.md`) and on both (`format-cases-before/refused/delta.md`). Before D-016, Mirage scored such inputs without converting.

**Measured.**

- Before D-016, Mirage failed 9 of the 18 color tag cases FFmpeg scores (`color-cases-before/main/delta.md`); after it, all 18 pass, every frame agreeing at the 6 decimals of FFmpeg's log, and both tools refuse the 2 YCgCo cases (`color-cases-after/checks.txt`, checks 2 and 5).
- On bipbop, gear2 and gear1 (smpte170m against gear5's bt709) failed over the first 300 frames before D-016 and pass after it (`eval/consistency/diag-before-300/`, `diag-after-300/`). Over every frame of the four pairs, both tools convert gear2 and gear1 the same way and agree on every frame (`eval/consistency/acceptance-full/plain/delta.md`; `checks.txt`, checks 2 and 5).
- Both tools refuse YCgCo on both inputs too (`format-cases-after/checks.txt`, check 9).
- Not measured: RGB and gray inputs, and FFmpeg versions other than 8.1.1 (P-013's limits). References in other YUV formats are measured in the next package; 10-bit and yuvj420p distorted inputs against a yuv420p reference agree after D-016 (`format-cases-after/`, `d01` and `d02`).

**Options.**

1. Confirm: FFmpeg 8.1.1's behavior for a yuv420p reference, as implemented and as the code, the player's included, does today.
2. Deviate for untagged inputs, for example by converting nothing when either input lacks a tag. Plain FFmpeg would then disagree on those pairs, and §5 would need the relabel workaround for them (P-010). The code changes, and the runs after D-016 with untagged inputs would be made again.
3. Make the handling of untagged inputs configurable, with option 1 as the default.

For a matrix libswscale lacks: (a) refuse, as the code, the player's included, does today; (b) score without converting and warn in the report, as before D-016, which changes the code and the refused runs after D-016.

**Also open.**

- **The FFmpeg libraries' versions in the report.** The rules are those of FFmpeg 8.1.1's libavfilter and libswscale, and Homebrew's current ffmpeg-full is a major version newer (`eval/build_check/macos-2026-10-23-6a6661f.txt`, "installed vs current stable"). `mirage_vmaf`'s report records libvmaf's versions (D-006) but not FFmpeg's; the delta runs record `ffmpeg -version` in `environment.txt`. Options: record the run-time libswscale and libavfilter versions in the report; leave them to the delta runs; also warn when FFmpeg's major version isn't 8.
- **D-016's Background** says the §5 work would reproduce the effect with committed outputs (MEASUREMENT PENDING). They exist now: `diag-before-300/`, `diag-after-300/`, `color-cases-before/`, `color-cases-after/` and `acceptance-full/`. D-016 is the owner's entry and wasn't edited; a note citing them, as D-001 has its correction, is the owner's to add.

**Agent's recommendation.** Option 1, and (a) for matrices libswscale lacks. D-016's rationale is matching a plain `ffmpeg -lavfi libvmaf` run, which the committed runs show Mirage doing frame for frame for yuv420p references, and FFmpeg gives no score to match for a pair it refuses. Record the libswscale and libavfilter versions in the report, and rerun the color tag cases after any FFmpeg upgrade. The paper should say that an untagged input is taken as BT.601 with limited range.

### D-016: references that aren't yuv420p (§5, #5)

**Decided**: D-022, option 1. **Done** (P-033): Mirage converts such a reference as FFmpeg's graph does, to the matrix and range libavfilter negotiates for both inputs, with `sws_scale_frame()` and the bicubic flags of the scale filter FFmpeg inserts, and records it (`ref_conversion`). In the reference format cases after D-022, every case passes, `j01` with its scores from before D-016 (`eval/consistency/format-cases-d022/checks.txt`, checks 4 and 5; P-034). P-030's warning is gone; the analyzer warns instead about frames in a pixel format whose conversion isn't checked against FFmpeg's. Where FFmpeg's negotiation changes the distorted frames' color, which D-022's wording doesn't foresee, Mirage follows FFmpeg, a departure from D-022's wording that is provisional until the owner decides ("A departure from a decision's wording" above). The rest of this package describes the code before D-022.

**Question.** D-016 has Mirage convert the distorted frames to the reference's color matrix and range, "as FFmpeg's filter graph does". For a reference whose frames aren't yuv420p, FFmpeg 8.1.1's §5 graph does something else: it converts the reference to the distorted frames' matrix and range, with a scale filter it inserts itself, and leaves the distorted frames' tags alone. Mirage converts the distorted frames as it does for a yuv420p reference, so its scores differ from FFmpeg's for such a reference wherever the two inputs' tags differ, and, because of how it converts the reference, for a full range yuvj420p reference even with the same tags. Should Mirage follow FFmpeg for such references, or D-016's wording?

**Today** (`src/VMAFColor.h`, `src/VMAFAnalyzer.cpp`; `mirage_vmaf` and the player alike):

- Mirage converts the reference to 8-bit yuv420p with a legacy `sws_getContext()` call and bilinear flags, without converting its matrix; for a yuvj420p reference that call also turns full range into limited range (`tests/test_vmaf_color.cpp`). The distorted frames are converted to the reference's tags, as `vmafcolor::target()` derives them.
- Since `0ff3178`, `mirage_vmaf`'s report has a warning for a reference that isn't yuv420p, and the delta reports list it; no score changed (P-030).
- FFmpeg's graph puts `auto_scale_0` before the reference's `format=yuv420p`, with bicubic flags (`0x00000004`) where the distorted input's scale filter has bilinear's (`0x00000002`), and it converts the reference to the distorted frames' matrix and range; the distorted input's scale filter keeps their tags (`eval/consistency/format-cases-before/checks.txt`, check 2). The FFmpeg 8.1.1 sources explain the logs: once a scale filter is inserted on the reference's side, the matrix and range both inputs share are free to choose, and libavfilter reduces them to the tags of the distorted input's scale filter's input, which comes earlier in the graph (`libavfilter/avfiltergraph.c`, `query_formats`, `reduce_formats`).

**Measured** (`eval/consistency/format-cases-before/`, `format-cases-after/`: synthetic clips, 10 frames each; P-029; the table in `eval/consistency/README.md`):

- A 10-bit, 4:2:2 or 4:4:4 reference with another matrix than the distorted input: Mirage fails every case after D-016, by up to 10.931963 (`f05`; `f01`, `f02`, `f03`, `f05`, `f07` in `format-cases-after/main/delta.md`). Before D-016 they failed too, by 20.072068 to 32.029205 (`format-cases-before/main/delta.md`).
- The same references with the same tags as the distorted input: Mirage agrees with FFmpeg on every frame (`f04`, `f06`, `f08`; `format-cases-after/checks.txt`, check 4).
- A lossless full range H.264 reference, which FFmpeg's decoder gives as yuvj420p:
  - against a limited range distorted input (`j01`), Mirage agreed with FFmpeg before D-016 and fails after it, by up to 13.274013: since D-016 it converts the distorted frames to full range, where FFmpeg converts the reference to limited range;
  - against a full range distorted input (`j02`), it fails before and after D-016, by up to 23.673382: its conversion of the reference makes it limited range, where FFmpeg keeps it full. Mirage's scores of `j02` are FFmpeg's of `j01` on every frame at 6 decimals (`format-cases-after/checks.txt`, check 8).
- 10-bit and yuvj420p distorted inputs against a yuv420p reference agree after D-016 (`d01`, `d02`); `d01` failed before it.
- No committed bipbop run is affected: every bipbop rung is yuv420p (`eval/baseline/diagnostics/ffprobe_rungs.txt`).
- Not measured: RGB and gray inputs, 12-bit and 4:4:4 distorted inputs, distorted inputs without a range tag against such references, and real streams in these formats (MEASUREMENT PENDING).

**Options.**

1. Follow FFmpeg: when the reference isn't yuv420p, convert the reference to the distorted frames' matrix and range through `sws_scale_frame()` with the flags of FFmpeg's inserted scale filter (bicubic), and leave the distorted frames' color as it is, still scaled with `--scaler`. Every case of `format-cases-after/` would then be expected to pass, which its rerun step in `eval/run_all.sh` would check, `j01` with its scores before D-016. It changes scores for such references, and for 10-bit references it decides how the reference is brought to 8 bits, which belongs with #5.
2. Follow D-016's wording: keep converting the distorted frames to the reference's tags, and only make the reference's conversion keep a yuvj420p reference's range (`j02`). Plain FFmpeg then disagrees whenever such a reference's tags differ from the distorted input's, and the §5 comparison would need a workaround on FFmpeg's side for those pairs, which no committed run has tried.
3. Refuse such references, as matrices libswscale lacks are refused, until option 1 or 2 is built; or refuse them only when the tags differ or the reference is yuvj420p.
4. Keep today's behavior, with the warning.

**Agent's recommendation.** Option 1, decided together with #5. D-016's rationale is matching a plain `ffmpeg -lavfi libvmaf` run, and for these references FFmpeg's conversion is of the reference; the committed cases are its acceptance test. Until then, option 4: the warning keeps the wrong scores from being silent without changing any score. Before the paper uses any such reference, the decision has to be made and the cases rerun.

### D-019: a frame exactly at the threshold (§5)

**Decided**: D-023, option 1: a frame exactly at 0.01 passes, and the paper's method text states the boundary. **Done**: the delta tool already implemented it (`test_threshold_is_exclusive`); the statement is number 21 of `docs/PAPER_METHOD_NOTES.md`. The rest of this package describes the question as it was.

**Question.** The work order's §5 target is a per-frame difference "< 0.01". D-019 fails a frame if |Mirage − FFmpeg| > 0.01, so a frame at exactly 0.01 passes D-019 but misses the work order's wording. Which holds?

**Today.** The delta tool implements D-019, and a test pins the boundary (`test_threshold_is_exclusive` in `eval/consistency/tests/test_vmafdelta.py`). In the committed runs made after D-016 that scored frames with a yuv420p reference, the largest |delta| is 0.000000 at the 6 decimals of FFmpeg's log (the runs' `delta.md` in `diag-after-300/`, `color-cases-after/main/`, `scalers-300/`, `models-300/` and `acceptance-full/`, and the pairs `d01` and `d02` of `format-cases-after/main/`), so no frame has come near the boundary. The other cases of `format-cases-after/main/` either agree at 0.000000 or fail far above it (previous package).

**Options.**

1. Keep D-019, and state the boundary in the method text.
2. Fail a frame when |delta| ≥ 0.01, as the work order's wording has it.

**Agent's recommendation.** Option 1: the two differ only for a frame at exactly 0.01.

### #3 The default scaler (D-003)

**Decided**: D-024, bicubic. **Done** (P-038): the analyzer's default, and so the player's and `mirage_vmaf`'s, is bicubic, and so is `run_delta.sh`'s on both sides; `eval/run_all.sh` reruns the runs made with bilinear with bilinear named. Over the first 300 frames of the four pairs, a run with the defaults has the scores of `scalers-300/bicubic` on every frame, and `mirage_vmaf` without `--scaler` scores as it does (`eval/consistency/defaults-300/checks.txt`, checks 2 to 4; P-041). The full-length §5 report with bicubic passes over every frame of the four pairs (`eval/consistency/defaults-full/`, P-059). The rest of this package describes the code before D-024.

**Question.** Which swscale scaler brings the distorted frames to the reference's size by default, in the GUI and in `mirage_vmaf`, and so in the paper's scores?

**Today.** Bilinear, as since before Phase 0; every committed §5 run used bilinear on both sides, except the bicubic and lanczos runs of `scalers-300/` below. `--scaler` now chooses bilinear, bicubic or lanczos, and the report records it.

**Measured** (`eval/consistency/scalers-300/`, first 300 frames of the four pairs of D-018, each scaler on both sides; P-016):

- Mirage matches FFmpeg with each of the three scalers: every frame of every pair agrees at the 6 decimals of FFmpeg's log (`checks.txt`, check 3), and all three runs pass D-019.
- The scaler changes the scores a lot, on both sides alike. Pooled means, Mirage's side, from `scaler_effect.md`:

  | pair | bilinear | bicubic | lanczos |
  |---|---|---|---|
  | gear4_vs_gear5 | 74.640607 | 84.258060 | 83.843581 |
  | gear3_vs_gear5 | 53.729092 | 64.843002 | 67.063929 |
  | gear2_vs_gear5 | 28.552935 | 36.817663 | 39.024426 |
  | gear1_vs_gear5 | 0.006117 | 6.781918 | 8.881025 |

  No frame of any pair scores lower with bicubic or lanczos than with bilinear; on average the scores rise by 6.775801 to 11.113910 points with bicubic and 8.874908 to 13.334836 with lanczos, depending on the pair (`scaler_effect.md`).
- FFmpeg's scale filter given no `flags=` uses the flags of `flags=bicubic` (`eval/consistency/ffmpeg-scale-default/checks.txt`). So a plain `ffmpeg -lavfi` graph that scales without naming a scaler runs with the swscale flags of `--scaler bicubic`, and is expected to score as Mirage does with it, not as with today's default; no such graph was scored here.
- Not measured: the effect on a ladder encoded from one source (D-011's clips), and over every frame (MEASUREMENT PENDING).

**A difference from the work order** (hard rule 4; nothing was edited to hide it): §1.3 suggests bicubic as the default "so that the §5 consistency check can pass". The runs show that §5 passes with each of the three scalers when both sides use the same one; the scaler decides the scores, not whether the two tools agree.

**Options.**

1. Bilinear: today's default, and the setting of the committed §5 runs other than the scaler runs.
2. Bicubic: the work order's suggestion, and what FFmpeg's scale filter does when no scaler is named.
3. Lanczos.

Whatever the choice, the §5 runs give FFmpeg the same `flags=` (`run_delta.sh --scaler`), and every report states the scaler. The full-length §5 report of D-018 was made with today's default, bilinear (`eval/consistency/acceptance-full/`); another default means making it again.

**Also open, with the scaler.** `--scaler` sets the distorted frames' scaler only; the reference keeps its size and is only converted to 8-bit yuv420p, with bilinear flags whatever `--scaler` says (P-017; since D-022, a reference that isn't yuv420p is converted with the bicubic flags of the scale filter FFmpeg inserts for it, whatever `--scaler` says, P-033). Every bipbop reference is yuv420p. For a reference in another format, FFmpeg's §5 graph converts it with a scale filter it inserts itself, to the distorted frames' color and with bicubic flags, where the distorted input's scale filter had bilinear's (`eval/consistency/format-cases-before/checks.txt`, check 2; no other `flags=` was run with such a reference); Mirage's scores then differ from FFmpeg's where the tags differ (the package "D-016: references that aren't yuv420p").

**Agent's recommendation.** Bicubic. It is the work order's suggestion and FFmpeg's own default when a scale filter names no scaler, so the paper's scores would be what a reader gets from a plain `ffmpeg` command with a plain `scale` filter. The continuity argument for bilinear is weak, because no score so far is paper data (D-005). Whichever is chosen, the paper should say which scaler, since the measured effect is several points.

### #4 The default model (D-004)

**Decided**: D-025, option 1: vmaf_v0.6.1 stays the default, and the other models stay options, recorded in each report. **Done**: nothing changed but a comment (`cde637b`); the full-length report with the defaults uses it (`eval/consistency/defaults-full/`, P-059). The rest of this package describes the question as it was.

**Question.** Which libvmaf model scores pairs by default? The gap report also asks whether choosing the model from the resolution counts as a default.

**Today.** The built-in vmaf_v0.6.1. `--model` takes any built-in model of the linked libvmaf (this one has vmaf_v0.6.1, vmaf_v0.6.1neg, vmaf_4k_v0.6.1 among them), `--model-file` a model file, and the report gives `model`, `model_source` and `model_sha256` (P-017).

**Measured** (`eval/consistency/models-300/`, gear2 against gear5, first 300 frames, bilinear, each model on both sides; P-016):

| model | pooled mean (Mirage's side, `model_effect.md`) | frames up / same / down against vmaf_v0.6.1 |
|---|---|---|
| vmaf_v0.6.1 (built-in) | 28.552935 | |
| vmaf_v0.6.1neg (built-in) | 19.498402 | 0 / 0 / 300 |
| vmaf_4k_v0.6.1 (built-in) | 47.442375 | 300 / 0 / 0 |
| the package's `vmaf_v0.6.1.json` (`--model-file`) | 28.552935 | 0 / 300 / 0 |

- Every frame agrees between Mirage and FFmpeg at FFmpeg's 6 decimals with every model (`checks.txt`, check 3), and the model file scores as the built-in model does (check 6). This meets §1.1's acceptance: changing the model gives observably different scores, and the output names the model.
- FFmpeg's libvmaf filter's own default model is `version=vmaf_v0.6.1` (`eval/consistency/ffmpeg-libvmaf-filter/checks.txt`).
- Not measured: the models on other rungs, other content, or every frame.

**Options.**

1. vmaf_v0.6.1: today's default and FFmpeg's libvmaf filter default.
2. vmaf_v0.6.1neg: the variant whose features limit enhancement gain (FFmpeg's log names its features with `_egl_1`, `models-300/checks.txt`, check 5); on gear2 it scores every frame lower.
3. vmaf_4k_v0.6.1.
4. A model chosen from the reference's resolution. That needs a rule, with a resolution limit the owner sets, and the report would have to state which model each pair got.

**Agent's recommendation.** vmaf_v0.6.1 as the default: FFmpeg's libvmaf filter uses it unless told otherwise, and every committed §5 run used it apart from the other models' runs in `models-300/`. The other models stay options, recorded in each report; a rule by resolution can come later as an option, once the owner has set its resolution limit.

### #5 Bit depth

**Decided**: D-026, option 1 for now: every input stays converted to 8-bit yuv420p, and once D-011's ladders exist a 10-bit SDR ladder is scored at 8 bits and at its own bit depth, on both sides, before the owner decides again (MEASUREMENT PENDING; the candidate clips are in "D-011" above). **Done**: nothing changed. The rest of this package describes the question as it was.

**Question.** At what bit depth are frames handed to libvmaf?

**Today.** Every input becomes 8-bit yuv420p before scoring (`src/VMAFAnalyzer.cpp`, `kScorePixFmt` and `kScoreBpc`), and the report states it (`scored_as`). FFmpeg's §5 graph does the same with `format=yuv420p` on both inputs, so the §5 comparison is of 8-bit scoring on both sides. A 10-bit input is reduced to 8 bits by swscale on both sides: the distorted frames in the same step as their scaling, and the reference in Mirage's own conversion call and on FFmpeg's side in a scale filter FFmpeg inserts. bipbop 16x9 is yuv420p (`eval/baseline/diagnostics/ffprobe_rungs.txt`); the synthetic reference format cases have 10-bit inputs (`eval/consistency/format-cases-after/`, P-029):

- a 10-bit distorted input against a yuv420p reference: Mirage agrees with FFmpeg on every frame (`d01`);
- a 10-bit reference with the distorted input's tags: it agrees on every frame (`f04`);
- a 10-bit reference with other tags: it doesn't, because FFmpeg converts the reference's color as well (the package "D-016: references that aren't yuv420p"). Since D-022 Mirage converts it as FFmpeg does, and these cases pass too (`eval/consistency/format-cases-d022/`, `f01` to `f03`).

**What the libraries take.** FFmpeg's libvmaf filter takes higher bit depths than 8 as well, in 4:2:0, 4:2:2 and 4:4:4 (its format list in `libavfilter/vf_libvmaf.c` of the FFmpeg 8.1.1 sources, read, not run), and libvmaf's `vmaf_picture_alloc()` takes a bit depth.

**Options.**

1. 8 bits for every input (today).
2. The inputs' own bit depth when both have more than 8 bits (the §5 graph would then use the matching `format=` on both sides).
3. Configurable, with 1 or 2 as the default.

HDR content (PQ or HLG transfer) is a further question: the models above are SDR models, and nothing in this work handles HDR.

**What a measurement needs.** A ladder of 10-bit SDR rungs encoded from one 10-bit source, which D-011's self-encoded ladders can provide, and each rung scored at 8 and at its own bit depth, on both sides. No suitable public stream was at hand on 2026-10-23 (the streams this repository uses are 8-bit), so option 2 is not measured (MEASUREMENT PENDING). The synthetic 10-bit check of the §5 comparison exists now: `color_cases.py --set formats` (above).

**Agent's recommendation.** Keep 8 bits until D-011's ladders exist, then measure option 2 against option 1 on them. Decide together with the package "D-016: references that aren't yuv420p", which is about how a 10-bit reference is converted.

### #6 Pooling definitions (D-004)

**Decided**: D-027. **Done** (P-039): `mirage_vmaf`'s report gives the mean, harmonic mean and minimum as libvmaf pools them, the minimum without the first frame (`min_without_first_frame`) and p1 to p50 by linear interpolation; the analyzer computes them from the per-frame scores, so the player's p5 is the interpolated one. On the first 300 frames with the defaults, each report's mean and min are those of libvmaf's `vmaf_score_pooled()` at full precision, its mean, min and harmonic mean those of FFmpeg's log at 6 decimals, and all nine values those of `pooling.py` at full precision (`eval/consistency/defaults-300/checks.txt`, checks 5 to 7; P-041). Over every frame of the four pairs with the defaults, the nine values of each report are those `pooling.py` computes at full precision, and the mean, min and harmonic mean FFmpeg's at 6 decimals (`eval/consistency/defaults-full/checks.txt`, checks 9 and 10; P-059). The rest of this package describes the code before D-027.

**Question.** How are `mean`, `harmonic_mean`, `min` and `p1` / `p5` / `p10` / `p25` / `p50` (work order §1.5) defined?

**Today.** `mirage_vmaf` reports libvmaf's pooled mean and min (`vmaf_score_pooled`); the player's analyzer also computes `vmaf_p5` as `sorted[n * 5 / 100]`, which `mirage_vmaf` leaves out (P-006). Nothing was changed.

**Measured** (`eval/pooling/bipbop16x9/pooling.md`, made by `eval/pooling/pooling.py` from the Phase 0 baseline's full-length per-frame scores and from both sides of `eval/consistency/diag-after-300/plain`; P-018; summary in `eval/pooling/README.md`):

- **The minimum is the first frame's score** in every series, as the gap report observed. In 1280x720, 960x540 and 640x360 the first frame is the only frame at the minimum; in 416x234 the minimum is 0, which 5976 frames of the full length and 294 of the first 300 score. Without the first frame the full-length minima are 73.1728, 48.4413 and 22.1403, where the minima are 69.7469, 45.1244 and 20.0523. In FFmpeg's logs the first frame's `integer_motion2` and `integer_motion` are 0, and in each rung it is the only frame of the first 300 with `integer_motion2` 0.
- **Percentile definitions**: today's rule (`sorted[floor(n*p/100)]`), nearest rank (`sorted[ceil(n*p/100) - 1]`) and linear interpolation (Hyndman and Fan's definition 7) differ by at most 0.0003 on the full-length series (4 decimals), and by up to 0.093440 (gear2, p1), 0.082396 (gear3, p1) and 0.071134 (gear3, p5) on 300 frames.
- **Harmonic mean**: libvmaf's harmonic mean in FFmpeg's logs is `n / sum(1 / (x + 1)) - 1` for all four rungs, at the log's 6 decimals; the plain `n / sum(1 / x)` differs (gear2: 28.434599 against 28.428817), and it is undefined when a frame scores 0, as 416x234's frames do.
- The analyzer's recorded `vmaf_p5` is today's rule, and its recorded min is the series' minimum, on every rung.
- **Every frame after D-016** (`eval/pooling/acceptance-full/pooling.md`, from both sides of the committed §5 report; P-027): the minimum is again the first frame's score in every series, and for gear4, gear3 and gear2 the only frame at it; without the first frame, gear2's minimum is 18.126492 where its minimum is 16.070202. gear1's minimum is 0, which 53790 of its 53946 frames score, so its plain harmonic mean is undefined. The three percentile definitions differ by at most 0.000276 (gear3, p1). libvmaf's harmonic mean in FFmpeg's logs is the shifted form for all four pairs, e.g. 27.751821 for gear2, where the plain form gives 27.746387.

**Options.**

- Percentiles: today's rule; nearest rank; linear interpolation. libvmaf has no percentile pooling, so Mirage computes them in every option.
- Harmonic mean: libvmaf's (`VMAF_POOL_METHOD_HARMONIC_MEAN`, the shifted form, which FFmpeg's log also reports); the plain form, with a rule for frames at 0.
- Minimum: libvmaf's, over every frame (then it is the first frame's score, as above); or also, as a separately named value, the minimum without the first frame.
- Mean: libvmaf's mean, which the reports already carry and FFmpeg's log shows too.

**Agent's recommendation.** Linear interpolation for the percentiles, the default of common analysis tools, so readers recomputing them from the per-frame scores get the same values; libvmaf's harmonic mean, which is defined with frames at 0 and is what FFmpeg reports; libvmaf's minimum for parity with FFmpeg, with the first-frame effect stated in the paper and, if the owner wants a minimum that means something, a second value without the first frame, under its own name.

### #7 Frame alignment, and approval of the plan (§1.4, D-007)

**Decided**: D-028, the plan with its recommended choices, and for #7 every failure counted, a threshold of 1% and a warning. **Done** in the analyzer that the player and `mirage_vmaf` share (`src/VMAFAlign.h`, `src/VMAFAnalyzer.cpp`; P-043 to P-045): pairing by rule A with a tolerance of half the reference's frame duration, times from each input's start, the coarser grid for a frame-rate mismatch, pairing stopped at the first discontinuity, a new libvmaf context after each gap, the counts and the list of failures in the report (`alignment`), and the warning above 1% in the report, on stderr and in the player's VMAF panels. The committed 300-frame and synthetic runs, whose frames line up, keep their pairs and scores (`eval/run_all/2026-10-23-d028/`; P-046). The §1.4 acceptance, a stream with a frame dropped on purpose, is a test of the analyzer (`tests/test_vmaf_analyzer.cpp`) and a committed delta run: on that stream `mirage_vmaf` reports the failed frame and warns, every pair's two frames at the same PTS, where before D-028 it scored the pairs after the gap one picture apart without a warning (`eval/consistency/align-cases-d028/checks.txt`, check 4; `align-cases-before/`; P-048); the unit test record is `eval/test_runs/macos-2026-10-24-12715a6.txt` (P-060). The §5 delta tool compares the two sides' pairs by their frames and timestamps, FFmpeg's from `showinfo` filters in its graph, and reports what each side counted as failed (P-047); the alignment cases are scored by both tools before and after D-028 (P-048). What implementing D-028 left open is in "Open after D-020" above. The rest of this package describes the code before D-028.

The plan is `docs/plans/frame-alignment.md`. It asks for approval of pairing by timestamp in the analyzer that the GUI and `mirage_vmaf` share (D-007), and lists the choices it needs: the matching rule (the reference's timeline, or FFmpeg's framesync rule, whose defaults are in `eval/consistency/ffmpeg-libvmaf-filter/checks.txt`), the timestamp origin, frame-rate mismatches, discontinuities, what a gap does to libvmaf's motion feature, the report's fields, and #7 itself: the failure-rate definition, the threshold (the work order suggests 1%) and whether exceeding it warns or refuses. The plan ends with the agent's recommendation. Nothing of it is implemented; its acceptance test, the work order's stream with dropped frames, is part of the plan.

### §1.2 Reference modes

**Decided**: D-029, the agent's recommendation below, with `explicit-pair` kept for a pair given on the command line. **Done** (P-050): `mirage_vmaf --ladder <master>` scores each video playlist of an HLS master playlist once, against the top rung (D-002), or with `--mezzanine` against that video; entries without video are skipped and listed; a pair that can't be scored fails the run, which then writes no report; one run writes one report, of one reference mode, with a `pairs` list. On bipbop's master, over the first 300 frames, each pair of the ladder report has every value of the same pair scored on its own, and the audio-only entry is listed as skipped (`eval/consistency/ladder-300/checks.txt`, checks 3 to 7; P-051). §1.2's acceptance, both modes on one ladder, waits for D-011's clips (MEASUREMENT PENDING). What implementing D-029 left open is in "Open after D-020" above. The rest of this package describes the code before D-029.

- **`reference_mode` of `mirage_vmaf`**: `explicit-pair`, because the command line names both inputs (P-017). The work order's §7 lists only `mezzanine` and `top-rung`; confirm the name, or choose another, such as letting the user declare one of the two.
- **Ladder scoring in `mirage_vmaf`** (top rung by default per D-002, a mezzanine optionally) is not built (P-019). It needs the owner's or §2's choices: which `EXT-X-STREAM-INF` entries are scored (audio-only entries, entries that share a video playlist, equal bandwidths), what a failed rung does to the run and its exit status given P-006, and how one report holds several pairs while keeping the two modes apart before §7's schema. **Agent's recommendation**: score each video playlist once, skip entries without video and list them as skipped, let one failed rung fail the run as P-006 does for a pair, and write one report per reference mode with a `pairs` list, so that mode A and mode B never share a report.
- **§1.2's acceptance**, both modes on one ladder with the difference documented, needs a ladder with a mezzanine: D-011's source clips (MEASUREMENT PENDING).

### The config file (the work order's engineering constraints)

**Decided**: D-030, the agent's recommendation below. **Done** (P-040): `mirage_vmaf --config FILE`, `name = value` lines read through the option table, the command line replacing the file's settings, and the report's `config_file` with the file's path and SHA-256. A run whose settings come from a config file scores as the same run from the command line (`eval/consistency/defaults-300/checks.txt`, check 4). What D-030 leaves open is in "Open after D-020" above. The rest of this package describes the code before D-030.

The work order asks for every configurable item as a command line option and in a config file. `mirage_vmaf`'s options are all on its command line: `--ref`, `--dis`, `--out`, `--frames`, `--scaler`, `--model`, `--model-file`. Fixed in the code, not configurable: libvmaf's threads and subsampling, the 8-bit yuv420p scoring format (#5), and pairing in decode order (#7). The player gets no new flags.

**A design for the owner to choose from** (not built: the format is the owner's call):

- `mirage_vmaf --config FILE`, with the same names as the long options, one setting per line (`scaler = bicubic`), read through the same option table as the command line, so that both accept exactly the same settings; or JSON; or TOML, which needs a parser the repository doesn't have.
- The command line overrides the file.
- The report records the file's path and SHA-256 next to the effective settings, which it already records.
- For `run_delta.sh`, the same, or a waiver for the eval tooling, since `run.cmd.txt` records every option of a run.

**Agent's recommendation**: the `name = value` lines through the option table, with the command line overriding the file and the file's SHA-256 in the report: no new dependency, and nothing that can be set one way but not the other.

### #11 The rest of the build strategy (§8, D-009)

**Decided**: D-031, the agent's recommendations below: #11a option 2, #11b option 2, #11c option 1. **Done**: glad's generated loader is committed in `third_party/glad/` and built as a plain static library, with `regenerate.sh` and its license terms, so the build runs no Python (P-053); `MIRAGE_HEADLESS` and the presets `macos-headless` and `linux-release`, with Linux instructions in `docs/BUILD.md` (P-054); a Dockerfile in `docker/` (P-055); the build check at `5ae44a4` shows the macOS side (`eval/build_check/macos-2026-10-24-5ae44a4.txt`, P-058). Not built: the Linux preset, the §5 run on Linux that D-031 asks for, and the Docker image, for want of a Linux host or container runtime (MEASUREMENT PENDING). What D-031 leaves open is in "Open after D-020" above. The rest of this package describes the build before D-031.

D-009 standardized FFmpeg on ffmpeg-full and left two parts of #11 open: whether glad's generated output is committed, and the scope of Linux support. The work order's §8 also asks for a Docker or Nix environment to reproduce the evaluation, which needs both answers. Nothing of this is implemented. What the §8 work that needed no decision did is in `docs/BUILD.md`, `eval/streams.md` and `eval/README.md`; the process choices behind it are P-020 to P-025 in `docs/DECISIONS.md`.

#### #11a Is glad's generated output committed?

**Today.** glad 2.0.8 (`CMakeLists.txt`, pinned by tag) generates the OpenGL 4.1 core loader while the player builds, with the Python interpreter CMake found when it configured (`glad_add_library(glad_gl_core_41 STATIC REPRODUCIBLE API gl:core=4.1)`). On the machine of the build checks, Homebrew's Python 3.14.7 can't import `pyexpat`, so the build stops at that step with the default Python, and the venv of `docs/BUILD.md` step 3 (Python 3.13 with jinja2 and MarkupSafe pinned) avoids it (P-003; both records in `eval/build_check/`). `mirage_vmaf` and `mirage_tests` don't use OpenGL and build without the venv.

**Measured** (`eval/build_check/macos-2026-10-23-6a6661f.txt`, "Generated glad files, release vs debug"):

- glad generates three files: `include/KHR/khrplatform.h` (11131 bytes), `include/glad/gl.h` (872844 bytes) and `src/gl.c` (623188 bytes), with their SHA-256 in the record.
- The debug and the release build generate identical files, and P-003 found them identical to the main working tree's build output.
- `gl.h` says `Loader generated by glad 2.0.8 on -` (no date, because of `REPRODUCIBLE`) and `SPDX-License-Identifier: (WTFPL OR CC0-1.0) AND Apache-2.0`.

**Options.**

1. Keep generating at build time, with the venv documented (today). Every build of the player needs Python with jinja2, and a Homebrew Python update can break the step again as Python 3.14.7 did.
2. Commit the three generated files, for example under `third_party/glad/`, build them as a plain static library, and keep a script that regenerates them with glad 2.0.8 in the pinned venv and compares the bytes. The build then runs no Python. The repository gains the three files (sizes above), and their license terms (WTFPL or CC0-1.0, and Apache-2.0 for what comes from the Khronos specification, as `gl.h` states) have to be stated next to them; how that sits with GPL-3.0-or-later belongs with D-008's open question, whether legal or the company's open-source process has to confirm the license.
3. Pin the generation environment instead: have CMake create the venv itself at configure time with the pinned packages, or refuse a Python newer than one known to work. The build stays dependent on Python and, for a venv, on PyPI at configure time.

**Agent's recommendation.** Option 2. The glad step is the only build step that failed in either build check, the files it generates are the same in every build so far, and committing them removes Python from the player's build.

#### #11b How far does Linux support go?

**Today.** Only macOS on Apple Silicon is built and documented:

- Both presets of `CMakePresets.json` put `/opt/homebrew/opt/ffmpeg-full/lib/pkgconfig` on `PKG_CONFIG_PATH`.
- `CMakeLists.txt` configures every target at once: OpenGL and GLFW are required, and nativefiledialog-extended v1.2.1 requires GTK on Linux, or D-Bus with its portal option (its `src/CMakeLists.txt` in the fetched sources, read, not built), even when only `mirage_vmaf` is to be built. Apple frameworks (CoreVideo, IOSurface) are linked under `if(APPLE)`.
- The player has macOS-specific code: VideoToolbox decoding (`src/Decoder.cpp`) and the IOSurface texture path (`src/VideoRenderer.cpp`, under `#ifdef __APPLE__`).
- The libvmaf package version that D-006 records is read from a Homebrew Cellar path (`CMakeLists.txt`, `eval/consistency/run_delta.py`); elsewhere it would read `unknown`.
- The eval scripts take the FFmpeg, ffprobe and model directory as options, with Homebrew paths as defaults, and `run_delta.py` finds linked libraries with `otool -L` or `ldd`.

Nothing has been built or run on Linux, so whether the code compiles there, which FFmpeg 8 and libvmaf 3 builds a distribution offers, and whether the scores equal those measured on Apple Silicon are all MEASUREMENT PENDING.

**Options.**

1. macOS only, stated in the paper. This differs from §8, which asks for build instructions for Linux and macOS.
2. Headless Linux: `mirage_vmaf`, `mirage_tests` and the eval tooling on Linux; the player stays macOS-only. Needs a CMake option that configures without the player's dependencies (OpenGL, GLFW, glad, Dear ImGui, nativefiledialog-extended), a Linux preset, Linux instructions in `docs/BUILD.md`, and a §5 run on Linux.
3. Full Linux support, the player included: an OpenGL 4.1 core context (for example through Mesa), GLFW, nativefiledialog-extended with GTK or its portal backend, and software decoding where VideoToolbox is missing.

**Agent's recommendation.** Option 2. Every committed score comes from `mirage_vmaf` and the eval tooling, which use only FFmpeg and libvmaf; the player isn't needed to reproduce a number, and a headless build is what a Docker image (#11c) can run.

#### #11c Docker or Nix?

**Today.** Neither exists. §8 asks for one of them to reproduce the evaluation.

**What the build checks show.** Homebrew can't install the versions the measurements used (`docs/BUILD.md`, step 1, and the help of `brew pin` and `brew extract` in `eval/build_check/macos-2026-10-23-6a6661f.txt`), and its current ffmpeg-full, libvmaf and GLFW are already newer than those (the same record, "installed vs current stable").

**Options.**

1. A Docker image for Linux: a base image pinned by digest; FFmpeg 8.1.1 and libvmaf built from release sources checked by SHA-256, the versions of the committed measurements; `mirage_vmaf` built from the repository; `eval/run_all.sh` as the command it runs, whose comparison says whether the container reproduces the committed scores. Needs #11b option 2 or 3, and doesn't cover the macOS build or the player.
2. A Nix flake: every dependency pinned through one nixpkgs revision, on macOS (aarch64-darwin) and Linux, so the macOS build could be reproduced as well. Needs a nixpkgs revision, or overrides, with the FFmpeg and libvmaf versions wanted. Not examined here.
3. Both.
4. Neither: the versions recorded in `docs/BUILD.md`, with `eval/run_all.sh` to show whether another environment reproduces the scores.

**Agent's recommendation.** Option 1, with #11b option 2. A container can build FFmpeg and libvmaf at exactly the versions of the measurements, which Homebrew can't, and `eval/run_all.sh` already reruns and compares everything without a display. Whether the container's scores equal those measured on Apple Silicon would be its first measurement.

### README.md beyond the brew line

**Decided**: D-031, option 2. **Done** (P-056): the README's build part is a pointer to `docs/BUILD.md`, and the parts listed below are corrected from the repository's files; the controls, the thread model and the license section are as they were. The rest of this package describes the README before D-031.

**Question.** The §8 work changed `README.md` only where it had to: the brew line now installs ffmpeg-full (D-009), a note points to `docs/BUILD.md`, and a License section states GPL-3.0-or-later (D-008). Other parts describe an older state of the repository. Should they be brought up to date, and how?

**What differs from the repository today.**

- "Running tests" counts 34 tests covering `NetworkLogger` and `ManifestParser`, run with `ctest`; `mirage_tests` is now built from 26 test files (`CMakeLists.txt`), and `docs/BUILD.md` step 4 runs it without the `[audio]` tests, as the build checks do.
- "First configure takes ~1–2 minutes on a fast connection" rests on no committed record. The configures of the verification run of `eval/run_all.sh` took 83.3 s and 97.0 s ("cmake --preset macos-release" in `eval/run_all/2026-10-23/run_all.md`), on one machine and connection.
- "libvmaf model not found at runtime" says the model is loaded from a file under Homebrew's prefix; the analyzer loads built-in models by version (`vmaf_model_load` in `src/VMAFAnalyzer.cpp`) and reads a file only with `--model-file`.
- The Python note suggests `pip3 install jinja2 --break-system-packages`, which installs into the Python that Homebrew manages; the build checks used the venv of `docs/BUILD.md` step 3.
- The project tree lists two test files, and neither `tools/`, `docs/` nor `eval/`.
- The FetchContent list leaves out stb, and gives miniaudio without the commit `CMakeLists.txt` now pins it to (P-021).

**Options.**

1. Correct these parts from the repository's files and the committed records.
2. Make the README's build part a short pointer to `docs/BUILD.md`, and correct the rest.
3. Leave the README as it is.

**Agent's recommendation.** Option 2: one place for the build steps that the build checks verify, and a README whose facts match the repository.

### Smaller items found in Phase 1

- **The subtitle test's fixture.** In a clean checkout `SidecarSubtitles — decodes data/sub_test/test.srt` fails: it reads `data/sub_test/test.srt`, which `.gitignore` leaves out of the repository (`eval/build_check/macos-2026-10-23-6a6661f.txt`, "Unit tests"); in the main working tree the file exists and the test passes (`eval/test_runs/macos-2026-10-23-8aca01a.txt`). Options: commit the file with a `.gitignore` exception; move it to a tracked test directory; have the test write it to a temporary file, as the other media tests generate their inputs; leave it, as `docs/BUILD.md` says. **Agent's recommendation**: have the test write it, a change to the test only. **Decided**: D-032. **Done**: the test writes its fixture in the run's own temporary directory (`97f0098`, P-036); the build check at `5ae44a4` ran it in a clean worktree, where it passed (`eval/build_check/macos-2026-10-24-5ae44a4.txt`; P-058).
- **An intermittent test.** `Demuxer: seamless video and audio switches on a local file` (`tests/test_demuxer_switch.cpp`) fails now and then, at the commit Phase 1 started from as on this branch: run alone 650 times, a build of `37472fd` failed 5 times and one of `855a71b` 4 times, each time at one of the same two assertions (`eval/test_runs/demuxer-switch-150-2026-10-23.txt` and `demuxer-switch-500-2026-10-23.txt`, P-031; summed up in `eval/test_runs/README.md`). The test and `src/Demuxer.*` are the same at both commits. It passed in the test records at `8aca01a` and `9855319` (`eval/test_runs/`). A fix may touch `Demuxer`, which is on the decode path (D-007). Options: leave it and rerun on failure; a plan that finds out whether the test's timing or the track switch is at fault; leave it out of unattended runs. **Agent's recommendation**: the plan, since a race in track switching would matter for §3 and §4. **Decided**: D-032, the plan. **Done**: `docs/plans/demuxer-switch-test.md`, for the owner's approval ("The plan for the intermittent Demuxer test" above).
- **D-009's alternatives.** D-009 says that standardizing on `ffmpeg` would need a separately installed FFmpeg with libvmaf, but Homebrew's `ffmpeg` 8.1.1 on the machine of the measurements was built with `--enable-libvmaf` as well (`eval/baseline/environment.txt`, line 163). The decision stands on its other reasons. D-009 is the owner's entry and wasn't edited; options: a correction note, as D-001 has; leave it. **Decided**: D-032, a correction note. **Done**: D-009 has it (`docs/DECISIONS.md`).
- **Server addresses in the committed logs.** The delta runs keep FFmpeg's verbose logs, xz-compressed, and these include FFmpeg's TCP connection lines with the IP addresses of Apple's CDN servers: public addresses, not local information, so the runs' privacy scan doesn't flag them (P-011). Options: keep the logs as FFmpeg wrote them; filter the network lines out of the stored logs, which would then no longer be raw; lower the log level of FFmpeg's network layer only. **Agent's recommendation**: keep them raw. **Decided**: D-032, keep them raw. **Done**: nothing changed; the full-length run with the defaults keeps them as well.
- **D-008's open question**: whether legal or the company's open-source process has to confirm GPL-3.0-or-later before the repository is made public again. `LICENSE` is committed as D-008 decided (P-020). This is the owner's to check. Still open: "D-008" above.
- **`vcpkg.json`.** The repository's root `vcpkg.json` lists FFmpeg and GLFW, without libvmaf or versions, and the build doesn't use it (`docs/VMAF_GAP_REPORT.md`, "Specific gaps for §8"); `docs/BUILD.md` doesn't mention it. Options: remove it; make it a second, pinned way to get the dependencies, libvmaf included, together with #11b and #11c; leave it. **Agent's recommendation**: remove it, unless #11b or #11c chooses vcpkg. **Decided**: D-031. **Done**: removed (`20d551c`, P-056).

### Process choices P-006 to P-032

**Decided**: D-033 confirmed P-006 to P-032 as their entries state them. The choices made since then are P-033 to P-065 ("The agent's process choices to confirm" above). The rest of this package is the list as it was.

P-006 to P-032 in `docs/DECISIONS.md` are the agent's measurement and process choices of Phase 1, each pending the owner's confirmation. The alternatives the agent pointed out for each, as its entry lists them:

| Entries | What | Alternatives worth a look |
|---|---|---|
| P-006 | what `mirage_vmaf` records, and when it writes no report | the first decoded frame's color tags as well as the decoder's; libvmaf's package version read at run time from the loaded library; a report with an error field instead of none |
| P-007, P-022 | the build checks in clean worktrees | a virtual machine, another macOS account or a hosted runner, starting without the Homebrew packages |
| P-008 | FFmpeg's side of the delta tool | Phase 0's `-frames:v` instead of `trim` (FFmpeg's scores were the same over 300 frames, `diag-before-300/checks.txt`, check 1) |
| P-009 | how a pair passes | make some warnings failures, such as different libvmaf versions on the two sides or pairs with different PTS |
| P-010 | the relabel diagnostic | relabel the color matrix only |
| P-011 | what a delta run records | uncompressed logs; absolute paths |
| P-012, P-015, P-016, P-026 | the runs, with the release build of the main working tree at a clean HEAD | builds in clean worktrees; the scaler and model runs over every frame |
| P-013 | the synthetic color tag cases | H.264 clips encoded without tags |
| P-014 | how Mirage converts | the tags of the decoders' stream parameters or of the first pair instead of each frame's; as many swscale threads as FFmpeg's scale filter |
| P-015 | the delta tool's warning when the two sides' conversions differ | a failure instead |
| P-017 | `--scaler`, `--model`, `--model-file`, `reference_mode` | `--scaler` for the reference's conversion too |
| P-018 | the pooling measurement | more percentile definitions |
| P-019 | ladder scoring and the config file not built yet | build them now (their open choices are above) |
| P-020 | the `LICENSE` text | SPDX license notices in every source file |
| P-021 | the pins | the five tagged dependencies pinned by commit as well; stb at `003501f6`, which the main working tree's Phase 1 builds fetched |
| P-023 | the stream fetch keeps playlists only | mirror the media for runs without network, after checking that a copy scores as the URLs do |
| P-024, P-025 | `eval/run_all.sh` and its verification run | commit the rerun outputs; rerun the runs after D-016 with the programs they were made with |
| P-027 | the full-length steps of `run_all.sh`, and pooling over every frame | leave the full-length run out of `run_all.sh` |
| P-028 | the record of the test runs behind the PR description | the outputs in the PR description only |
| P-029 | the reference format cases, synthetic and lossless, before and after D-016 | real streams in these formats; more formats, such as RGB, gray or 12-bit |
| P-030 | a warning, not a refusal, for a reference that isn't yuv420p, until the owner decides | refuse such references; warn only where the tags differ |
| P-031 | repeated runs of the intermittent Demuxer test at the start and the head of Phase 1 | the whole suite repeated; one series only |
| P-032 | the unit test record and a rerun of every measurement after the review fixes' code changes | keep citing the earlier records; a rerun without the full-length steps |

**Agent's recommendation**: confirm them all.
