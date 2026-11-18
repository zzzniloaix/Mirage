# §3.1's data sources in today's code

A survey for the design of Phase 3 (the work order's §3, the unified event timeline). Nothing of Phase 3 is implemented; this maps what the code has now, for `mirage_vmaf` (headless) and for the player separately, source by source and event type by event type.

Read at branch `vmaf/phase-3`, commit `75cf6e0`, with FFmpeg 8.1.1 (Homebrew's `ffmpeg-full`) and the repository's debug build.

**Conventions**

- `<repo>` is that worktree; every `file:line` is in it. `<out>` is this survey's directory, `<scratch>` the session's scratch directory, `<homebrew>` Homebrew's prefix.
- A **run** is a command I ran, saved in `<out>/runs/<name>/`: `cmd.txt` (the working directory and the exact command), `output.txt` (everything it wrote to stdout and stderr) and `exit.txt` (exit status, start and end in UTC). Only the local absolute paths in them were replaced, by the placeholders above (`<out>/probe/sanitize.sh`). Appendix A lists the runs. The probes are C++ programs on FFmpeg's library APIs (`<out>/probe/`). Two of them are compiled from the repository's own sources. No `ffmpeg` or `ffprobe` binary was run. `<out>/probe/build.sh` rebuilds the probes into `<out>/probe/bin/`, which I deleted after the runs because the binaries' debug information carries local paths.
- FFmpeg is cited as URLs at the tag `n8.1.1`, and RFC 8216 by section. I fetched local copies to check the line numbers, then removed them, because upstream files carry their authors' email addresses. `<out>/ffmpeg-n8.1.1-src/FETCHED.txt` and `<out>/rfc/FETCHED.txt` give the fetch commands and the SHA-256 of each file. Run `check-scte35-fixture` reads the RFC from `<out>/rfc/`, so it needs the RFC fetched again to be rerun.
- Every number here comes from a cited line or URL, or from a saved run. Where there is neither, it says MEASUREMENT PENDING.
- Design choices are listed with their options and trade-offs in section 11. Where I have a recommendation, it is labelled as the agent's, and none of them is a decision.

---

## Answers in brief

- **(a) VMAF frames.** A ladder report keys each frame of a pair by the pair's own counter, `index`. Each frame entry also carries `ref_index` and `dis_index` (each frame's position in its input, from 0), `ref_pts` and `dis_pts`, and `vmaf`. The two PTS are in seconds: `best_effort_timestamp` × the stream's time base, raw, not shifted by the input's start (`<repo>/src/VMAFReport.cpp:193-201`, `<repo>/src/VMAFAnalyzer.cpp:731-736`; run `mirage-vmaf-ladder`: frame 0 has `ref_pts` 0.04 and `alignment.reference.start` 0.04).
  - No frame is mapped to a segment today. `mirage_vmaf` neither reads nor parses any media playlist, and it does not compile `HlsAlign.cpp` (`<repo>/CMakeLists.txt:233-246`).
  - `hls::MediaTimeline` already has each segment's URI, media sequence number, byte range, playlist start and duration, and discontinuity sequence (`<repo>/src/HlsAlign.h:33-75`). What is missing is the link from a frame to a segment.
  - On the fixture, mapping by the packet's byte position (`AVPacket::pos` against the segments' sizes, which SegmentWatch's hooks can see) named the right segment for 40 of 40 packets. Mapping by PTS against EXTINF sums got 37 of 40 with plain floating point and 39 of 40 with a half-frame tolerance (run `map-frames-to-segments`).
- **(b) HTTP timing.**
  - **SegmentWatch** sees every file the input's hls demuxer opens, the playlist included. At `io_open` it gets the URL (plus ` (from byte N)` for a byte range) and the return code. At `io_close2` it gets the `AVIOContext`, whose `bytes_read` and `error` are readable. It records no time and no status (`<repo>/src/VMAFSegments.h:59-80,123-147`; run `logprobe-file`).
  - **NetworkLogger** records request rows. Its `io_open`/`io_close2` hooks give open and close times, bytes and local files. Its process-global `av_log` callback parses `request:`, `header='…'`, `Statistics:`, `HTTP error`, `Will reconnect at`, `URL read error:` and `Error reading HTTP response:` lines from http.c and avio, which give status, time sent, first byte and headers (`<repo>/src/NetworkLogger.cpp:208-235,543-755`).
  - **Installed in a process that reads as the analyzer does**, NetworkLogger gave the playlist and every segment of an HTTP input with status, time sent, first byte and end. Bytes read were unknown (only Content-Length was there), every row had initiator `other`, and a local input gave no row at all (run `netlog-probe`).
  - **So `mirage_vmaf` could record** per segment, without the player's code: start, open end (which is after the response headers for HTTP), close and bytes, through SegmentWatch. The HTTP status and the time of the first byte are only in `av_log` lines, or in the open's error code when it fails. But the analyzer's reads are paced by decoding and scoring, and the open and `avformat_find_stream_info` read ahead, so close times don't measure the network. In run `logprobe-file`, the playlist and three segments were opened before the first packet came back.
- **(c) Playlist events.**
  - **FFmpeg's hls demuxer** parses none of EXT-X-DISCONTINUITY, PROGRAM-DATE-TIME, CUE-OUT/CUE-IN, DATERANGE, OATCLS or unknown tags. Each is skipped with a VERBOSE `Skip ('…')` log line (hls.c:990; run `logprobe-file`: 11 such lines).
  - **`ManifestParser`** (player and tests only) keeps them as tags positioned in *playlist* time, the sum of EXTINF before the tag. It has defects, shown in run `manifest-probe-tagged`:
    - CUE-OUT-CONT comes out as CueOut.
    - `#EXT-OATCLS-SCTE35` is dropped.
    - DATERANGE is "Unknown", raw text.
    - `disc_seq` doesn't count discontinuities.
  - **`hls::parse_media_timeline`** attaches DISCONTINUITY, discontinuity sequence numbers and PDT to segments, and ignores CUE, DATERANGE and other tags.
  - **To get to media PTS**, add the media time of the segment's start: from its first frame, or from its head bytes (`HlsAlign` `SegmentStart`). Nothing does this for events today. The player compares tag times with stream PTS directly.
- **(d) Decoder events.**
  - **Recorded today:** nothing per frame. `mirage_vmaf` ignores `avcodec_send_packet`'s result, and only an error from `avcodec_receive_frame` ends the input (`<repo>/src/VMAFAnalyzer.cpp:204-236`).
  - **Concealed decode errors** reach stderr only, as the decoder's `av_log` lines. A damaged segment scored to a complete report with a VMAF minimum of 0 and no trace in the report, while 6 frames carried `decode_error_flags`=4 (runs `logprobe-corrupt`, `mirage-vmaf-pair-corrupt`).
  - **Keyframes and picture types** (`AV_FRAME_FLAG_KEY`, `pict_type`) can be read at the same place `record_pts` records (run `logprobe-gop`).
  - **Frames a decoder drops** show today only as the pairing's failures (`unmatched_reference`, …), with index and PTS, in `alignment.failures`.
- **(e) Variant switches.**
  - **The player has no ABR.** Only `switch_video` in `<repo>/src/main.cpp:1541-1567` calls `Demuxer::request_video_switch`, from the track panel or `--qa-switch` (run `grep-checks`).
  - **Every step is logged as free text** to stdout, with `logger::uptime()` seconds: request, demuxer take-over point, reposition, first packet, decoder reopen, first frame presented. None of them names the rung.
  - **A structured event would need** the from and to rung (master index, BANDWIDTH, URI) and the media take-over point. It would also need the frames dropped around it, which nothing counts today, and one clock shared with the network rows (section 6).
- **(f) Process-global state.**
  - FFmpeg's `av_log` callback and level are process-global (log.c:59,441,460-470,477,492). Run `logprobe-concurrent` showed one thread's callback replacement redirect the other thread's log lines.
  - `NetworkLogger` is one instance per process, with process-global saved `io_open`/`io_close2`.
  - SegmentWatch and NetworkLogger's hook both claim `AVFormatContext::opaque`, so they can't both be on one context as written.
  - The player has two steady-clock epochs: `logger::uptime()` and `NetworkLogger::now_us()`.
  - SegmentWatch, the analyzer and the parsers are per instance or pure, and safe for the pool (section 9).

---

## 1. What each build target compiles

`mirage_vmaf` compiles only `tools/mirage_vmaf.cpp` and these sources (`<repo>/CMakeLists.txt:233-246`):

- VMAFAlign, VMAFAnalyzer, VMAFColor, VMAFLadder, VMAFPool, VMAFProcess
- FileHash, HlsMaster, VMAFCli, VMAFReport, JsonText

It links FFmpeg and libvmaf only (`:250-253`).

Not in `mirage_vmaf`:

- `NetworkLogger`, `NetDescribe`, `NetProbe`, `NetInspector*`
- `ManifestParser`, which also needs pugixml (`<repo>/src/ManifestParser.cpp:21`)
- `HlsAlign`, `HlsVariants`
- `Decoder`, `Demuxer`, `KeyframeIndex`, `MediaSession`, `main.cpp`

`VMAFAnalyzer.cpp` includes `ManifestParser.h` only for the `VariantStream` struct, which `start_manifest` takes (`<repo>/src/VMAFAnalyzer.cpp:3,827-857`).

`mirage_tests` compiles most of them: `NetworkLogger`, `NetDescribe`, `NetProbe`, `ManifestParser`, `HlsAlign`, `HlsVariants`, `Decoder`, `Demuxer` and `KeyframeIndex`. It does not compile `NetInspector*`, `MediaSession` or `main.cpp` (`<repo>/CMakeLists.txt:264-329`).

The `macos-headless` preset sets `MIRAGE_HEADLESS`, which leaves the player out (`<repo>/CMakePresets.json:29-36`, `<repo>/CMakeLists.txt:7-13,149`).

The player compiles everything above plus the UI (`<repo>/CMakeLists.txt:150-194`).

So anything a §3.1 source needs that lives in the player-only files, or in `ManifestParser`/`HlsAlign`, has to be added to `mirage_vmaf`'s sources before the headless build can use it. `HlsAlign.cpp` has no FFmpeg or ImGui dependency (`<repo>/src/HlsAlign.h:12`). `ManifestParser.cpp` pulls in pugixml and `NetworkLogger` (`<repo>/src/ManifestParser.cpp:4,21,121-124,157`).

---

## 2. VMAF per-frame scores — answer (a)

**(1) Where.**

- **The analysis** is `VMAFAnalyzer::analyze_pair` (`<repo>/src/VMAFAnalyzer.cpp:412-795`). It returns a `VMAFResult` (`<repo>/src/VMAFAnalyzer.h:204-245`), which holds:
  - `per_frame`, the score of each scored pair, by pair index;
  - with `VMAFOptions::record_pts`, the parallel vectors `ref_pts`, `dis_pts`, `ref_index` and `dis_index` (`<repo>/src/VMAFAnalyzer.h:171-179,237-244`);
  - `alignment`, a `VMAFAlignment` with each input's `start`, its discontinuity sequences (`VMAFSequence`: `first_index`, `first_pts`, `frames`), the libvmaf contexts (`VMAFContextRun`), and every frame that failed to pair (`VMAFAlignmentFailure`: `input`, `index`, `pts`, `reason`) (`<repo>/src/VMAFAnalyzer.h:102-167`);
  - the inputs' `time_base` (`VMAFInputInfo`, `<repo>/src/VMAFAnalyzer.h:70-84`).
- **`mirage_vmaf` always records PTS**: `record_pts = true` at `<repo>/tools/mirage_vmaf.cpp:158`.
- **The values recorded per pair** (`<repo>/src/VMAFAnalyzer.cpp:731-736`):
  - `ref_pts` and `dis_pts`: `seconds(best_effort_timestamp)`, which is the stream time base × the timestamp, NaN without one (`:280-285`);
  - `ref_index` and `dis_index`: the aligner's `Step::ref_index` and `dis_index` (`<repo>/src/VMAFAlign.h:186-189`).
- **The report** (`<repo>/src/VMAFReport.cpp:193-201`; layout at `<repo>/src/VMAFReport.h:57-59`) gives each pair a `frames` list of `{index, ref_index, dis_index, ref_pts, dis_pts, vmaf}`. NaN is written as null.
- **In a ladder report** each pair carries this list. Every pair is scored against the same reference URL, and its inputs are opened anew for each pair (`<repo>/src/VMAFLadder.h:39-42`, `<repo>/src/VMAFLadder.cpp:509-510`).
- **The player's own analyzer** (`MediaSession::vmaf_analyzer`, `<repo>/src/MediaSession.h:82`) scores with default options: `start_manifest` passes `{}`, so `record_pts` is false (`<repo>/src/VMAFAnalyzer.cpp:872`, `VMAFAnalyzer.h:173`). Its `write_json` writes `per_frame` without any PTS (`<repo>/src/VMAFAnalyzer.cpp:907-948`). The player's curve places the playhead at `pos_frac × N` frames (`<repo>/src/PlayerUI.cpp:1558-1564`).

**What a pair's frame carries — answer to (a), first half.**

- A frame is keyed by `index`, the pair's scored-pair counter (`frame_idx`, `<repo>/src/VMAFAnalyzer.cpp:561,677`).
- It carries both frames' positions in their inputs and both frames' raw PTS in seconds. These are not measured from the input's start, although the pairing measures times from it (`<repo>/src/VMAFAlign.h:17-22`).
- Run `mirage-vmaf-ladder` shows a frame entry `{"index": 0, "ref_index": 0, "dis_index": 0, "ref_pts": 0.04, "dis_pts": 0.04, "vmaf": …}` with `"start": 0.04, "start_from": "container"` and `"time_base": "1/90000"`.
- Frames that didn't pair are not in `frames`. They are in `alignment.failures` with their input, index, PTS and reason (`<repo>/src/VMAFReport.cpp:109-116`).

**(2) Time base.**

- **What the report gives** is seconds as a double, raw, in each input's own clock, plus the time base string per input (`<repo>/src/VMAFReport.cpp:20-29`).
- **Ticks are not in the report.** The aligner compares exact ticks of a common time base internally (`<repo>/src/VMAFAlign.h:17-22`, `<repo>/src/VMAFAlign.cpp:91-106`).
- **The reference variant's timeline** is already the key: rule A pairs every distorted frame to a reference frame (`<repo>/src/VMAFAlign.h:23-25`). So `ref_pts`, or `ref_index`, is the join key for overlaying several variants' curves.
  - Each pair decodes the reference from its start, so the same reference frame should get the same `ref_index` and `ref_pts` in every pair. That is expected from the code, not measured across pairs: MEASUREMENT PENDING.
  - On the distorted input's coarser grid (a lower frame rate), only the grid's reference frames are scored (`<repo>/src/VMAFAlign.h:26-34`), so the curves of different variants can have different subsets of reference frames.

**(3) `mirage_vmaf` today.**

- **Today** it produces the scores, the PTS and the indices.
- **Frame → segment — answer to (a), second half.**
  - **Today:** not possible. The report has no segment field, and `mirage_vmaf` reads only the master playlist (`vmafladder::read_master`, `<repo>/src/VMAFLadder.cpp:293-334`). The hls demuxer reads each media playlist itself, and its timeline tags are skipped (section 4.1).
  - **What it would take:**
    - Read each scored input's media playlist (section 11, choice 3).
    - Parse it with `hls::parse_media_timeline`, which gives per segment `seq`, `uri` (absolute), `range` (EXT-X-BYTERANGE), `start`, `duration`, `disc_seq`, `pdt` and `line` (`<repo>/src/HlsAlign.h:33-47`, `<repo>/src/HlsAlign.cpp:478-639`). It also has `segment_at(t)` and `index_of(seq)` (`:460-476`). It is pure code and could be compiled into `mirage_vmaf`.
    - Link each decoded frame of each input to the segment it came from. Three ways were tried on the fixture (run `map-frames-to-segments`, from run `logprobe-file`'s output):
      - **Byte position.** `AVPacket::pos` counts bytes of the single stream the hls demuxer feeds its segment demuxer (hls.c `read_data_continuous`, [hls.c#L1641-L1755](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L1641-L1755)). Each segment's size is its `AVIOContext::bytes_read` at `io_close2`, in open order. SegmentWatch's hooks see each segment's open and its `bytes_read` at close, and the analyzer's read loop sees each packet's `pos` (`<repo>/src/VMAFAnalyzer.cpp:218-236`). Result: 40 of 40 packets named, 10 per segment.
      - **PTS against EXTINF sums**, FFmpeg's own placement rule (`find_timestamp_in_playlist`, [hls.c#L1916-L1945](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L1916-L1945)). Plain floating point named the right segment for 37 of 40: `pts − start` lands just below each boundary. Moving every boundary half a frame earlier got 39 of 40: the muxer's last EXTINF is 0.36 s for 10 frames at 25 fps (run `fixture`), so the last frame falls past it.
      - **Open order.** The segment opened last when a packet is read is not that packet's segment (run `logprobe-file`):
        - before the first packet came back, `avformat_open_input` had opened the playlist and segment 0;
        - `avformat_find_stream_info` had then opened segments 1 and 2, reading 21 frames;
        - the last segment was closed before its last two packets were read.
    - Carry the segment from the packet to the decoded frame:
      - by PTS (packets and frames share PTS);
      - or with `AV_CODEC_FLAG_COPY_OPAQUE`, which copies a packet's opaque to its frame ([avcodec.h#L245-L279](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavcodec/avcodec.h#L245-L279)). Neither it nor `AVPacket::pos` is read anywhere in the repository's `src/`, `tools/` or `tests/`. The only match is a test that sets `pkt->pos = -1` (`<repo>/tests/test_rtmp_input.cpp:217`; run `grep-checks`).
    - Record the segment per pair, like `ref_index` and `dis_index`: a segment of the reference and one of the distorted input, since rungs can be cut differently (`<repo>/docs/PHASE2_SURVEY.md:50`).

**(4) Gaps.**

- No segment per frame.
- No ticks: seconds as doubles are fragile at exact boundaries (above).
- The top rung's own score is not measured in top-rung mode, where it is the reference (`<repo>/src/VMAFLadder.h:36-38`). A D-068 session that plays the top rung over a span has no score there (section 11, choice 6).
- The player's analyzer records no PTS.
- The byte-position mapping is verified only on MPEG-TS without EXT-X-MAP, ID3 or encryption. hls.c also resets the position at a seek ([hls.c#L2732-L2733](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L2732-L2733)), which `mirage_vmaf` never does. For fMP4: MEASUREMENT PENDING.

---

## 3. Network timing: NetworkLogger (player) and SegmentWatch (`mirage_vmaf`) — answer (b)

The work order's "NetworkDebugger (av_log interception)" is `NetworkLogger` in the code (section 10).

### 3.1 NetworkLogger — the player

**(1) Where.** `<repo>/src/NetworkLogger.h:76-263`, `<repo>/src/NetworkLogger.cpp`.

**One row per HTTP request or local-file open.** `NetRequest` (`<repo>/src/NetworkLogger.h:23-68`) holds:

| Field(s) | What it is |
|---|---|
| `id`, `seq` | row id, change counter |
| `url`, `method`, `protocol`, `initiator` | the request |
| `t_start`, `t_sent`, `t_first_byte`, `t_end`, `t_last_byte` | times |
| `bytes`, `content_length` | body bytes read, Content-Length |
| `range_start`, `range_end` | requested byte range |
| `status`, `status_text`, `http_version`, `mime` | response |
| `request_headers`, `response_headers` | headers |
| `error`, `reconnects`, `conn_id` | failure details |
| `reused_connection`, `retry_of_failed`, `failed`, `partial` | flags |
| `body_*` | the kept body |

It has two feeds.

**Feed 1: hooks.** `hook_format_context` swaps a context's `io_open`/`io_close2` (`<repo>/src/NetworkLogger.cpp:299-352`), giving:

- open begin and end, error, initiator, the `offset`/`end_offset` range (`:311-318`);
- a row for a local file, done at open (`:498-517`);
- bytes from `AVIOContext::bytes_read`, at close and by `poll()` (`:523-541,926-974`);
- for handles of the hls demuxer, a tap on their bodies through `update_checksum` (`:320-324,775-787`).

The player hooks its playback demuxer (`<repo>/src/Demuxer.cpp:876`) and installs the logger before any FFmpeg call (`<repo>/src/main.cpp:985-986`).

**Feed 2: `av_log` lines**, through the process-global callback (`:172-177,208-235`). The callback matches the format string against eight prefixes (`:221-224`) and parses these lines:

| Line | Emitted by | What NetworkLogger takes from it |
|---|---|---|
| `request: …` | DEBUG, [http.c#L1640](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L1640) | a new row: method, path, Range, request headers, `t_sent`; on a keep-alive handle, the end of the previous request (`:595-697`) |
| `header='HTTP/1.1 …'` | TRACE, [http.c#L1454](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L1454) | `status`, `status_text`, `http_version`, `t_first_byte`; then Content-Length, Content-Type and the response headers (`:699-730`) |
| `Statistics: …` | VERBOSE from avio_close ([avio.c#L636](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/avio.c#L636)), DEBUG from http_close ([http.c#L2073](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L2073)) | the context closed (`:732-755`) |
| `HTTP error …` | WARNING, [http.c#L897](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L897) | failed |
| `URL read error: …` | [http.c#L2041](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L2041) | failed |
| `Error reading HTTP response: …` | [http.c#L1449](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L1449) | failed |
| `Will reconnect at …` | [http.c#L452](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L452), [#L1879](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L1879) | reconnects (`:579-592`) |
| `Opening '…'` | INFO or DEBUG, [options.c#L145-L153](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/options.c#L145-L153) | the full URL of the next request on this thread (`:548-558`) |

The callback sees these lines whatever the `av_log` level: `av_vlog` calls it at every level ([log.c#L460-L470](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavutil/log.c#L460-L470)), and only the default callback filters by level ([log.c#L396](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavutil/log.c#L396)).

Not parsed:

- `http_code=` (TRACE, [http.c#L1250](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L1250));
- `Latency: X ms avg, Y ms max`, logged at close (DEBUG, [http.c#L2081-L2083](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L2081-L2083)). It is measured around the header read with `av_gettime` ([http.c#L1678-L1686](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L1678-L1686));
- hls.c's `HLS request for url …` (VERBOSE, [hls.c#L1397](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L1397)) and its `Failed to open segment` / `failed too many times, skipping` warnings ([hls.c#L1682-L1688](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L1682-L1688)).

**What it computes.**

- **Timing phases:** connect/queue = `t_start→t_sent`, wait (TTFB) = `t_sent→t_first_byte`, download = `t_first_byte→t_end` (`<repo>/src/NetInspector.cpp:899-951`).
- **Throughput:** body bytes of the requests completed in the last 10 s (`<repo>/src/NetInspector.cpp:986-996`).
- **Partial bodies:** a request whose body stops growing for `kIdleUs` (1.5 s, `<repo>/src/NetworkLogger.h:185`) is marked partial (`<repo>/src/NetworkLogger.cpp:967-971`).
- **QA report:** `net_report_json` (`<repo>/src/NetworkLogger.cpp:1027-1067`).
- **Bounds:** at most `kMaxRequests` = 4000 rows, oldest evicted (`<repo>/src/NetworkLogger.h:194`, `<repo>/src/NetworkLogger.cpp:385-398`).

**Request → segment.**

- **Rows are keyed by URL, not by segment.**
- **The player's inspector names a row's segment** by `"url@offset"` against the timeline of the newest playlist body (`<repo>/src/NetInspectorAlign.cpp:243-257,269-279`). This code is inline in an ImGui class, player-only.
- **Its rendition** comes from `NetCatalog` (`<repo>/src/NetDescribe.cpp:196-241`).

**(2) Time base.**

- **Times are steady-clock µs from the first call of `NetworkLogger::now_us()`** (`<repo>/src/NetworkLogger.cpp:188-193`).
- **`wall_ms()`** maps them to Unix ms with one offset, sampled at its first call (`:195-204`). A later change of the system clock is not followed.
- **On the timeline**, a row reaches media PTS only through the segment it fetched: URL and range → `MediaTimeline` segment → that segment's frames' PTS on its variant → `ref_pts` through the pairs. Nothing links a row to media time today.

**(3) In `mirage_vmaf`:** not compiled (section 1). Run `netlog-probe` compiled it from the repository and read the fixture as the analyzer reads an input:

- **HTTP, with SegmentWatch on the context** (the analyzer's open): five rows, the playlist and four segments. Each had `status` 200, `t_sent` = `t_start`, `t_first_byte` and `t_end`. `bytes` was −1 (`content_length` 15604), the initiator was `other` and `conn_id` 0. The logger saw this context through `av_log` alone, as its header says contexts it doesn't hook are seen (`<repo>/src/NetworkLogger.h:87-88`).
- **The same stream from its local path:** no row.
- **HTTP with `hook_format_context` instead of SegmentWatch:** rows with initiator `playback`, `t_start` < `t_sent` (connection setup included) and `bytes` 15604.

**(4) Gaps.**

- No per-pair or per-input tag: `NetInitiator` is a fixed enum (`<repo>/src/NetworkLogger.h:18`).
- With SegmentWatch on the context, no bytes read and no local files.
- One logger per process (section 9).
- Can't share a context with SegmentWatch.
- Bounded at 4000 rows.
- The `poll()`-driven fields need a caller.
- Rows aren't linked to segments outside the inspector.

### 3.2 SegmentWatch — `mirage_vmaf`

**(1) Where.**

- **The class** is `vmafsegments::SegmentWatch` (`<repo>/src/VMAFSegments.h:49-157`). It is header-only, and `DecodeCtx::open` installs it on each input (`<repo>/src/VMAFAnalyzer.cpp:166-194`, `watch` at `:172`).
- **On `io_open`** it records the file name and the return code. The name is `url`, plus ` (from byte N)` when hls.c passes an `offset` option for a byte range (`<repo>/src/VMAFSegments.h:123-137`; hls.c sets `offset`/`end_offset` only for a byte range, [hls.c#L1390-L1395](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L1390-L1395)).
- **On `io_close2`** it records the `AVIOContext`'s `error` (`:139-147`).
- **It keeps** only the first file the demuxer went on without, as an error (`:59-90`).
- **The options**, from `vmafsegments::set_hls_options`: `seg_max_retry` 10, `http_persistent` 0, `http_multiple` 0. So each file is opened through `io_open`, one at a time (`:26-40`).

**What it sees** (runs `logprobe-file` and `logprobe-http`, which wrap a recorder around the repository's SegmentWatch):

- every file the context opens: the playlist itself (opened through `io_open` by `init_input`, [demux.c#L180](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/demux.c#L180)) and each segment, by URL as hls.c resolved it;
- the open's result;
- at close, `bytes_read` and `error`. Locally and over HTTP, `bytes_read` was 15604 per segment, the files' size (run `fixture`).

It records no time and no bytes today.

**(2) Time base:** none. A recorder at the same place would take the steady clock.

**(3) `mirage_vmaf` today:** it sees the files but keeps only the error. What it would take to record per segment is in section 3.3.

**(4) Gaps:**

- no times, bytes or status kept;
- hls.c's own log lines about a segment (`Failed to open segment … of playlist …`, by sequence number) are not tied to it;
- a crypto URL is passed as `crypto+<url>` ([hls.c#L1408-L1427](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L1408-L1427)). Encrypted streams are out of scope.

### 3.3 What `mirage_vmaf` could record per segment, and how

All of the following from the input's own context, with no player code.

| Item | Local file | HTTP (with `mirage_vmaf`'s options) |
|---|---|---|
| Request start | time before calling the original `io_open` | the same; it includes connection setup, which shows as `Starting connection attempt` / `Successfully connected` (VERBOSE, [network.c#L432](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/network.c#L432), [#L488](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/network.c#L488)) |
| Request sent | — | `request:` line (DEBUG) — `av_log` only |
| First byte | `io_open` returning (no response exists) | `io_open` returns after the response headers are read (run `logprobe-http`: the header lines precede `open_end`); the status line's time from `header='HTTP/…'` (TRACE) — `av_log` only; the header wait per connection from `Latency:` at close (DEBUG) — `av_log` only, one request per connection here |
| Completion | `io_close2` | `io_close2` |
| Bytes | `bytes_read` at `io_close2` | the same; Content-Length from a `header=` line |
| Status | — | the status line or `http_code=` (TRACE) — `av_log` only. A failed open's error code gives `AVERROR_HTTP_BAD_REQUEST`, `UNAUTHORIZED`, `FORBIDDEN`, `NOT_FOUND`, `TOO_MANY_REQUESTS`, `OTHER_4XX` or `SERVER_ERROR` ([http.c#L592-L608](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L592-L608)) |

No AVOption exports the response code in 8.1.1. http.c exports `mime_type`, `http_version` and `location`; `reply_code` is the code a server mode sends ([http.c#L180-L206](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/http.c#L180-L206)).

To read the `av_log`-only items per input, a recorder would have to claim the lines of the `URLContext` behind each handle. That context is `AVIOContext::opaque`, as NetworkLogger uses it (`<repo>/src/NetworkLogger.cpp:285`), and the claiming would go through the one process-global callback (section 9).

**Caveat on meaning.**

- **The analyzer pulls reads as it decodes and scores.** The open and `avformat_find_stream_info` read ahead: in run `logprobe-file`, the playlist and three segments were opened before the first packet came back.
- **So completion and duration measure consumption, not the network.** A session's "segment that took 2.3 seconds" would come from the session's source, a scripted plan or a player log (D-068), or from a separate fetch (section 11, choice 5).

---

## 4. Playlist events — answer (c)

Event types:

- EXT-X-DISCONTINUITY
- EXT-X-DISCONTINUITY-SEQUENCE
- EXT-X-PROGRAM-DATE-TIME
- EXT-X-CUE-OUT and EXT-X-CUE-IN
- EXT-X-DATERANGE
- unknown and vendor EXT-X-* tags
- DASH period boundaries

The fixture's `dis/tagged.m3u8` has one of each HLS tag (Appendix B).

### 4.1 FFmpeg's hls demuxer (what `mirage_vmaf` gets today)

- **What hls.c parses:** STREAM-INF, KEY, MEDIA, TARGETDURATION, MEDIA-SEQUENCE, PLAYLIST-TYPE, MAP, START, ENDLIST, EXTINF and BYTERANGE ([hls.c#L861-L988](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L861-L988)).
- **Every other `#` line is skipped** with a VERBOSE log line `Skip ('<line>')` ([hls.c#L989-L991](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L989-L991)).
- **Run `logprobe-file`:** 11 `Skip` lines for VERSION, PDT, both DISCONTINUITY tags, both DATERANGE tags, OATCLS, CUE-OUT, CUE-OUT-CONT, CUE-IN and the vendor tag. Nothing else of them reaches the caller.
- **Discontinuities:** the segments' own timestamps come through unchanged ([hls_read_packet](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L2519) doesn't rewrite them; `<repo>/docs/PHASE2_SURVEY.md:46-50`, M5). The aligner starts a new discontinuity sequence only where they jump (`<repo>/src/VMAFAlign.h:56-73`).
  - Run `mirage-vmaf-pair-tagged`: the tagged playlist's two DISCONTINUITY tags over continuous timestamps gave `"discontinuities": 0` and one sequence of 40 frames.
  - The aligner's sequences are therefore not the playlist's discontinuity sequences.

### 4.2 ManifestParser — player and tests

**(1) Where.**

- **`ManifestParser::parse_hls_text`** (`<repo>/src/ManifestParser.cpp:230-348`) produces `ManifestTag {pts, disc_seq, kind, raw, value}` (`<repo>/src/ManifestParser.h:8-26`).
- **Kinds:** Discontinuity, DiscontinuitySequence, Map, ProgramDateTime, CueOut, CueIn, Period, Event and Unknown.

Behaviour, with run `manifest-probe-tagged` (9 tags from the tagged playlist):

| Tag | Code | What it becomes |
|---|---|---|
| EXT-X-DISCONTINUITY | `line ==` exact (`:302-304`) | Discontinuity. `disc_seq` is not incremented: both have `disc_seq` 0 in the run. `tests/test_manifest_parser.cpp:95` asserts this inherited value. |
| EXT-X-DISCONTINUITY-SEQUENCE | `std::stoi(line.substr(30))` (`:306-310`) | DiscontinuitySequence. The tag may appear more than once in this parser, and `std::stoi` throws on a non-numeric value, with no handler. RFC 8216 puts the tag before the first segment ([§4.3.3.3](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.3.3)). |
| EXT-X-PROGRAM-DATE-TIME | (`:316-318`) | ProgramDateTime, value unparsed |
| EXT-X-CUE-OUT | `starts_with("#EXT-X-CUE-OUT")` (`:320-322`) | CueOut, value = text after the first ':' (`0.8`) |
| EXT-X-CUE-OUT-CONT | caught by the same prefix | **CueOut** (`ElapsedTime=0.4,Duration=0.8` in the run) |
| EXT-X-CUE-IN | `line == "#EXT-X-CUE-IN"` (`:324-326`) | CueIn; a CUE-IN with attributes would be Unknown |
| EXT-X-DATERANGE | (`:328-338`) | **Unknown**; value = the raw attribute list, SCTE35-OUT/IN undecoded |
| `#EXT-OATCLS-SCTE35` | doesn't start with `#EXT-X-` | **dropped**: no tag in the run |
| other `#EXT-X-*` | all but VERSION, TARGETDURATION, MEDIA-SEQUENCE, PLAYLIST-TYPE, ENDLIST, KEY and ALLOW-CACHE (`:328-338`), and STREAM-INF, MEDIA, I-FRAME-STREAM-INF and INDEPENDENT-SEGMENTS earlier (`:249-300`) | Unknown — BYTERANGE, GAP, START and SESSION-DATA included |
| DASH Period, Event | `parse_dash_text` (`:386-511`) | Period at `period_start`, which comes from `start` or accumulated `duration` (`:409-421,502-504`); Event at `period_start + presentationTime/timescale` (`:423-441`) |

**(2) Time base.**

- **`pts` is playlist time**: the sum of EXTINF before the tag (`running_pts`, `:236,340-344`), although `ManifestParser.h:21` calls it "stream time in seconds".
- **Placement:** a tag written between a segment's EXTINF and its URI is placed after that segment's EXTINF has been added.
- **No segment index is kept.**
- **How the player uses it:**
  - for an HLS master URL, the player parses only the master, so `tags()` holds no timeline tag. `<repo>/src/NetInspector.cpp:1166-1169` says so in the UI;
  - the media playlists the player fetches are parsed for EXTINF durations only (`MediaSession::playlist_loop` and `use_media_playlist`, `<repo>/src/MediaSession.cpp:836-877`);
  - the player compares `tag.pts` directly with the stream's PTS. It draws ticks (`<repo>/src/main.cpp:2888-2914`) and seeks a clicked tag to `t.pts` (`<repo>/src/NetInspector.cpp:1179-1190`). That is right only where media time equals playlist time. The fixture's stream starts at 0.04 s (run `logprobe-file`).

**(3) `mirage_vmaf`:** not compiled, and it needs pugixml.

**(4) Gaps:** the defects in the table; no segment link; playlist time only; master-only in the player.

### 4.3 `hls::parse_media_timeline` — HlsAlign (player inspector and tests)

**(1) Where.** `<repo>/src/HlsAlign.cpp:478-639`, which produces `MediaTimeline` and `Segment` (`<repo>/src/HlsAlign.h:27-79`).

What it parses (`:526-571`):

- EXTINF, TARGETDURATION, MEDIA-SEQUENCE, DISCONTINUITY-SEQUENCE, VERSION, PLAYLIST-TYPE, ENDLIST, INDEPENDENT-SEGMENTS;
- DISCONTINUITY: `Segment::discontinuity`, and `disc_seq` incremented (`:603-608`);
- GAP;
- PROGRAM-DATE-TIME: Unix seconds through `parse_iso8601`, carried to following segments within a period (`:548-551,609-614`);
- BYTERANGE, with the implicit offset rule (`:587-601`);
- MAP: `maps`, and `Segment::map`;
- KEY: the method;
- SKIP.

Every other tag is ignored (`:572`): CUE-*, DATERANGE, OATCLS and vendor tags.

URIs are resolved by RFC 3986 rules (`resolve_uri`, `:291-339`); the header says FFmpeg resolves them this way (`HlsAlign.h:81-83`).

Run `manifest-probe-tagged`:

- four segments, `seq` 0–3;
- `start` 0, 0.4, 0.8, 1.2;
- `disc_seq` 0, 1, 1, 2;
- `pdt` only on segment 0. After the discontinuity it isn't carried on (`:612`);
- the problem "no EXT-X-TARGETDURATION": the hls muxer wrote `TARGETDURATION:0` (run `fixture`).

**(2) Time base.**

- **`Segment::start` is playlist time.**
- **Media time comes from segment heads.** `parse_segment_start` reads a segment's first bytes: MPEG-TS PES PTS, fMP4 tfdt and trun with the init segment's timescale and edit list, the packed-audio ID3 timestamp, or WebVTT's X-TIMESTAMP-MAP (`HlsAlign.h:89-143`).
- **`analyze_alignment`** turns these into an offset per period, `media − playlist` (`probes_of`, `HlsAlign.cpp:1239-1263`; `RenditionRow::offset`, `HlsAlign.h:169`).
- **The inspector's playhead in playlist time** is `cur_pts − offset` (`<repo>/src/NetInspectorAlign.cpp:113-120`).

**(3) `mirage_vmaf`:** not compiled. It is pure code with no dependency, so it could be.

**(4) Gaps:** no cue, DATERANGE or vendor tags; no tag positions as events. Its media-time offsets need the segments' head bytes, which the player gets from NetworkLogger's body tap (`<repo>/src/NetInspectorAlign.cpp:266-316`) and `mirage_vmaf` has no way to get today.

### 4.4 `hls::parse_media_playlist` and `KeyframeIndex` — player

- **`hls::parse_media_playlist`** (`<repo>/src/HlsVariants.cpp:97-140`) gives EXTINF durations and the flags `vod`, `discontinuity`, `gap` and `independent`; it gives no positions (run `manifest-probe-tagged`).
- **`KeyframeIndex`** predicts segment starts as the stream's first frame plus the sum of EXTINF. It uses a prediction only while the keyframes read confirm it, and only when every EXTINF is a whole number of frames (`<repo>/src/KeyframeIndex.h:12-20`). It does not use a playlist with discontinuities (`<repo>/src/MediaSession.cpp:863-871`).

### 4.5 From a tag's position to media PTS

- **The position:** a tag precedes the URI of segment k, the next segment in the playlist.
- **Media PTS of segment k's start**, two ways:
  - the PTS of the first frame decoded from segment k: exact once frames are mapped to segments (section 2);
  - or playlist time plus an offset. Across a discontinuity whose timestamps jump, that needs the offset of each period (section 4.3) and EXTINF that is exact.
- **Onto the reference's timeline:** through the pairs. A distorted frame in segment k is paired with the reference frame whose `ref_pts` is the key. A tag written in the reference's playlist too gives the reference's own segment directly.
- **PDT:** a wall-clock date for a segment's first sample ([RFC 8216 §4.3.2.6](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.2.6)). It is how a DATERANGE's START-DATE gets to playlist time; a playlist with DATERANGE must carry PDT ([§4.3.2.7](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.2.7)).

---

## 5. SCTE-35 (D-070): cue-out and cue-in, the splice point, the ad's duration

**(1) Where.**

- **ManifestParser:** CueOut and CueIn as text, with CUE-OUT-CONT conflated into CueOut; DATERANGE as Unknown raw text; OATCLS dropped (section 4.2).
- **Nothing decodes a `splice_info_section`** (`splice_insert`, `time_signal`, `break_duration`, `splice_time`). Nothing parses CUE-OUT's duration, CUE-OUT-CONT's elapsed time and duration, or DATERANGE's `PLANNED-DURATION`, `DURATION` and `END-DATE`.
- **HlsAlign and FFmpeg ignore all of them** (sections 4.1, 4.3).

**(2) Time base.**

- **CUE-* tags:** the following segment's position, as in section 4.5.
  - CUE-OUT, CUE-IN and OATCLS are not in RFC 8216 (run `grep-checks`).
  - Two CUE-OUT forms appear here: a bare number in this survey's fixture, and `DURATION=30` in the repository's tests (`<repo>/tests/test_manifest_parser.cpp:59`).
  - Which forms to read, and their units, is part of D-070's work.
- **DATERANGE:** a wall-clock START-DATE, mapped to playlist time through the segments' PDT. With SCTE35-OUT it is the splice's program time ([RFC 8216 §4.3.2.7.1](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.2.7.1)).
- **A decoded `splice_time` and `break_duration`** would be in the units the SCTE 35 standard defines. Not checked here.

**(3) `mirage_vmaf`:** nothing.

**(4) Gaps:** everything D-070 lists. The fixture carries RFC 8216 §8.10's example SCTE35-OUT and SCTE35-IN bytes verbatim, and the same OUT bytes in base64 as `#EXT-OATCLS-SCTE35` (run `check-scte35-fixture`). A decoder's tests could start from those, but their decoded meaning is not checked here.

---

## 6. ABR and player variant switches — answer (e)

**(1) Where.**

- **No ABR.** There is no automatic switching in `src/` or `tools/`: no match for `abr`, `adaptive bitrate` or bandwidth or throughput estimation, and `request_video_switch` has one caller, `<repo>/src/main.cpp:1553` (run `grep-checks`). D-068's rationale says the same.
- **The manual switch** is the `switch_video` lambda (`<repo>/src/main.cpp:1541-1567`), called from:
  - the track panel (`:3127-3149`);
  - `--qa-switch "v720@4,…"`, where `v<height>` or `v#<stream>` is applied at wall seconds since the first presented frame (`:553-556,2222-2232`).
- **What it does:**
  - logs `switch-trace: request video -> stream {} at pts {:.3f}[ (paused)] (t={:.3f})` (`:1547-1548`);
  - computes the seamless take-over target `keep` = `last_pts` + a lead. The lead is learned from the last measured fetch time (`:1482-1492,1552`);
  - calls `Demuxer::request_video_switch(stream, last_pts, paused, keep)`.
- **The demuxer** handles it in `read_loop` (`<repo>/src/Demuxer.cpp:311-342`):
  - the take-over point `resume` is where the old track's kept packets end (`trim_queue`, `:291-309`);
  - it logs `Video stream switched to index {} ({exact|seamless}, resume at {:.3f}) (t=…)` (`:340-341`);
  - it repositions and logs `switch-trace: repositioned to {:.3f}` (`:404-446`);
  - it logs the first packet and first keyframe of the new stream (`:759-770`);
  - it measures the fetch time `switch_fetch_s_` (`:754-757`).
- **The video decode thread** reopens the decoder at the sentinel and logs `switch-trace: video decoder reopened, new frames from {:.3f}` (`<repo>/src/MediaSession.cpp:174-183`). It logs the first decoded frame (`:112-117`).
  - Frames before the take-over point are decoded but dropped unseen (`skip`, `:65-83`), and nothing counts them.
  - Packets before the last keyframe before that point are not decoded at all. Their number is only in a log line (`switch-trace: skipped {} packets …`, `:186-202`).
- **The render thread** logs `switch-trace: first new frame presented pts=… WxH — video switch took N ms` (`<repo>/src/main.cpp:1688-1699`).
- **The rung** is not in any of these lines.
  - It can be found from the stream index: `hls_video_variant_[stream]` → `manifest.variants()[v]`, which has `url`, `bandwidth`, `width`, `height` and `codecs` (`<repo>/src/MediaSession.cpp:543-546`, `HlsVariants.h:95-96`).
  - For the routed pair of streams, `MediaSession::playing_hls_variant` gives it (`<repo>/src/MediaSession.cpp:879-883`).
  - The toast shows only `WxH · fps` (`<repo>/src/main.cpp:1494-1500,1555`).

**(2) Time base.**

- **`last_pts`** is seconds on the shown stream's timeline: the frame's `pts` × its `time_base` (`<repo>/src/main.cpp:1621-1625`).
- **`resume`** is seconds computed in the old stream's time base (`<repo>/src/Demuxer.cpp:295-305`).
- **`t=`** is `logger::uptime()`: steady seconds since its first call (`<repo>/src/Logger.h:12-16`). That is not `NetworkLogger::now_us()`'s epoch (section 9).
- **Onto the reference timeline:** the player's PTS and `mirage_vmaf`'s are both the segments' raw timestamps, since hls.c doesn't rewrite them and neither program shifts them. That they agree for the same frame is expected from the code, not measured: MEASUREMENT PENDING.

**(3) `mirage_vmaf`:** no player, so no switches. Under D-068, Phase 3's tests use a scripted plan instead.

**(4) What a structured switch event would need**, for a later player session log under D-068:

- one session clock shared with the network rows;
- the request's time and media position;
- the stream and rung before and after: master index, BANDWIDTH, RESOLUTION, URI;
- seamless or exact, `keep` and the demuxer's `resume`. `resume` is the boundary between two spans of the session;
- the reposition target, and the time of the new stream's first packet;
- the PTS and time of its first frame presented;
- the count and PTS range of the frames dropped before the take-over, and of late frames dropped near it. Neither is counted per switch today. D-068's limits say frames dropped at a switch show "as the decoder's events", which don't exist yet;
- the new variant's segment requests: NetworkLogger rows with initiator `playback`, tied to segments.

Adding logging calls inside `switch_video`, `read_loop`, `video_decode_loop` or `on_present` touches the render and decode path. Whether that needs D-007's plan is the owner's call (section 11, choice 10).

---

## 7. Decoder events: dropped frames, decode errors, keyframes — answer (d)

**(1) Where.**

- **`mirage_vmaf`'s decode** is `DecodeCtx` (`<repo>/src/VMAFAnalyzer.cpp:147-322`):
  - it opens the decoder with default options (`:186-190`);
  - `next_video_frame` (`:197-241`) loops on `avcodec_receive_frame`. Any error other than EAGAIN and EOF is kept in `error` and ends the input (`:207`), and a read error ends it the same way (`:221-229`);
  - `avcodec_send_packet`'s return value is ignored (`:226,231`);
  - each frame's `best_effort_timestamp` goes to the aligner (`:691-693`). The frame itself waits in `ref_queue`/`dis_queue` until it is paired or dropped (`:473,699-711`).
- **Per-frame fields that are readable there** but not read:
  - `AVFrame::pict_type`, `flags` with `AV_FRAME_FLAG_KEY`, `AV_FRAME_FLAG_CORRUPT` and `AV_FRAME_FLAG_DISCARD`, and `decode_error_flags` with `FF_DECODE_ERROR_*` ([frame.h#L519](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavutil/frame.h#L519), [#L638-L646](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavutil/frame.h#L638-L646), [#L714-L718](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavutil/frame.h#L714-L718));
  - `AVPacket::flags` with `AV_PKT_FLAG_KEY` and `AV_PKT_FLAG_CORRUPT` ([packet.h#L643-L644](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavcodec/packet.h#L643-L644)).
- **Decode errors.** Concealed errors leave no trace in `mirage_vmaf`'s output. In run `logprobe-corrupt` (the fixture with one segment damaged, run `corrupt-segment`):
  - the decoder logged 17 ERROR lines;
  - six frames came out with `decode_error_flags`=4, the frames at PTS 0.60–0.80;
  - no `AV_FRAME_FLAG_CORRUPT`, no packet flagged corrupt, no `avcodec_send_packet` error.
  
  In run `mirage-vmaf-pair-corrupt`, `mirage_vmaf` printed the same 17 decoder lines to stderr and scored a complete report. The `vmaf` of the frames at PTS 0.60–0.80 ranged from 0 to 34.6, against 51.7–57.9 for the four frames before them. The report has no field about the errors.
  
  A log line is logged while a packet is decoded, before the previous frame comes out, so a log line doesn't name its frame. The per-frame flags do.
- **Keyframes.** Not recorded. Run `logprobe-gop` (a fixture with a GOP of 10 and up to 2 B-frames): 5 frames with `AV_FRAME_FLAG_KEY` and 5 key packets; picture types 5 I, 9 P, 26 B.
- **Dropped frames.**
  - The offline decode has no drop of its own.
  - A frame a decoder never outputs shows by its effect on the pairing: as `unmatched_reference` (or `extra_distorted` etc.) in `alignment.failures`, with its input, index, PTS and reason (`<repo>/src/VMAFAnalyzer.h:102-109`, `<repo>/src/VMAFReport.cpp:109-116`). Those are the only per-frame decode-side events in a ladder report today.
  - Counting packets sent against frames received would show frames a decoder discarded. Not done.
- **The player.**
  - `StageStats` counts packets in and frames out per decode thread (`<repo>/src/PipelineState.h:36-43`, `<repo>/src/MediaSession.cpp:42-46,213`).
  - Late frames dropped by the render loop are summed in `total_dropped` (`<repo>/src/main.cpp:1751,2307-2329`), shown as a chip (`:3040`), and given per speed segment by `--av-report` (`frames_dropped_late`, `:219-236`). Nothing gives a PTS.
  - Frames skipped before a seek or switch target are not counted (section 6).
  - `Decoder::push` ignores `avcodec_send_packet` errors (`<repo>/src/Decoder.cpp:151-154`).
  - Keyframes: `KeyframeIndex::observe` gets every video packet's pts, dts and key flag from the playback demuxer (`<repo>/src/Demuxer.cpp:779-787`) and keeps keyframes with exact PTS (`<repo>/src/KeyframeIndex.h:50-57,103`). That is player-only.

**(2) Time base.** Frame and packet PTS in the stream's time base, the same clock as `ref_pts`/`dis_pts`.

**(3) `mirage_vmaf` would need:**

- per-frame fields recorded at `next_video_frame`, carried with the `AVFrame` to `analyze_pair`'s `record_pts` block (`:731-736`), as `ref_*` and `dis_*` vectors. The pairing doesn't change;
- the failures already in the report, turned into events;
- optionally, a count of packets sent against frames received.

**(4) Gaps:** as above.

---

## 8. Clocks today, and one time base

| Source | Clock | Code |
|---|---|---|
| VMAF frames, failures, sequences | stream PTS in seconds (double), raw; time base given per input | `<repo>/src/VMAFAnalyzer.cpp:280-285,731-736` |
| Pair timing in ladder reports | `started_s`, `ended_s`: steady seconds from the run's start | `<repo>/src/VMAFLadder.cpp:397-400,504,516` |
| ManifestParser tags | playlist time (sum of EXTINF before the tag) | `<repo>/src/ManifestParser.cpp:236,340-344` |
| HlsAlign segments | playlist time; PDT as Unix seconds; per-period media offset from segment heads | `<repo>/src/HlsAlign.cpp:583,609-614,1239-1263` |
| NetworkLogger rows | steady µs since the first `now_us()`; wall ms through one sampled offset | `<repo>/src/NetworkLogger.cpp:188-204` |
| Player logs | `logger::uptime()`, steady seconds since its first call | `<repo>/src/Logger.h:12-16` |
| Player media position | shown frame's PTS in seconds on its stream's timeline | `<repo>/src/main.cpp:1621-1625` |
| hls.c's placement | `first_timestamp` + sum of EXTINF, AV_TIME_BASE | [hls.c#L1916-L1945](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L1916-L1945) |

The reference variant's PTS is what the ladder already keys on, so that timeline exists today.

- **Playlist events** need segment k → the PTS of its first frame on the input that carries the event, then the pairs → `ref_pts`.
- **Network rows** need URL and range → segment.
- **Player events** need media seconds on the played stream → the same raw PTS. That the two agree is MEASUREMENT PENDING (section 6).
- **Wall clock** is meaningful only for observed events (network, player). Offline events have none, unless PDT gives a program date (section 11, choice 7).

---

## 9. What is process-global or not thread-safe — answer (f)

`mirage_vmaf --ladder` runs several pairs at once in one process (`<repo>/src/VMAFLadder.cpp:490-548`).

| Mechanism | Scope | Evidence | Matters for the pool because |
|---|---|---|---|
| FFmpeg's `av_log` callback | one per process (`static atomic_uintptr_t av_log_callback`) | [log.c#L441](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavutil/log.c#L441), [#L492-L495](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavutil/log.c#L492-L495); run `logprobe-concurrent`: before thread 2 replaced the callback, both threads' lines reached callback A; after, thread 1's lines (its last segment's `Opening`, `Statistics`, …) reached callback B | every pair's lines arrive in one function, on the logging thread. Telling inputs apart needs the logging object (`URLContext`, `AVFormatContext`), which the callback gets as `avcl`. Two components can't each install their own. |
| FFmpeg's `av_log` level | one per process | [log.c#L59](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavutil/log.c#L59), [#L477-L480](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavutil/log.c#L477-L480); `mirage_vmaf` sets WARNING (`<repo>/tools/mirage_vmaf.cpp:155`) | it filters only the default callback ([log.c#L396](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavutil/log.c#L396)): a custom callback sees TRACE lines anyway (runs `logprobe-*`, with the level at WARNING) |
| `NetworkLogger::instance_` | one logger per process | `<repo>/src/NetworkLogger.h:112,262`, `<repo>/src/NetworkLogger.cpp:23,172-186` | one table for all pairs; the initiator enum can't name a pair |
| `g_orig_open`, `g_orig_close2` | process-global, set by the first hooked context | `<repo>/src/NetworkLogger.cpp:244-245,337-352` | every hooked context must have the stock callbacks |
| `AVFormatContext::opaque` | per context, but claimed by both SegmentWatch (`<repo>/src/VMAFSegments.h:100,126,141`) and NetworkLogger's hook (`<repo>/src/NetworkLogger.cpp:307,351`) | code | the two can't share a context as written. NetworkLogger wraps only the callbacks it took as stock, from the first context it hooks (`:340-347`). With SegmentWatch first, on a context after that, it doesn't hook at all. On the very first context it would take SegmentWatch's callbacks as its "originals" and then overwrite `opaque` (`:351`), which SegmentWatch reads as itself. With the hook first, SegmentWatch takes `opaque` and the hook reads it as a `NetInitiator*`. |
| `TlsOpen t_open` | thread_local | `<repo>/src/NetworkLogger.cpp:145-159` | ties log lines to the open running on the same thread; fine with one open per thread at a time, as each pair's decode is |
| `NetworkLogger::now_us()` and `wall_ms()` epochs; `logger::uptime()` epoch | function-local statics (thread-safe init); two different epochs | `<repo>/src/NetworkLogger.cpp:188-204`, `<repo>/src/Logger.h:12-16` | network times and switch logs can't be compared without converting |
| `mirage_vmaf`'s signal flag | process-global | `<repo>/tools/mirage_vmaf.cpp:52-55` | none (cancel only) |
| `SegmentWatch` | per input context | `<repo>/src/VMAFSegments.h:92-103` | safe: per pair |
| `VMAFAnalyzer::analyze`, `DecodeCtx` | per call | `<repo>/src/VMAFAnalyzer.cpp:881-892,147-322` | safe |
| `vmafladder::score`'s `on_pair` | serialized by `callback_mtx` | `<repo>/src/VMAFLadder.cpp:396,520-526` | a timeline built in the callback sees one pair at a time |
| `hls::parse_media_timeline`, `parse_segment_start`, `analyze_alignment` | pure functions | `<repo>/src/HlsAlign.h:12` | safe |
| `ManifestParser` | an object without locks; the player parses on one thread and reads after a join | `<repo>/src/MediaSession.cpp:427-439,490` | safe if one per thread |
| `KeyframeIndex` | mutex inside | `<repo>/src/KeyframeIndex.h:38-39,160-164` | player only |

---

## 10. Differences from the work order, to report to the owner

These are facts of the code; none of them is fixed here.

1. **"NetworkDebugger (av_log interception)"** is `NetworkLogger`. It intercepts `av_log` and also hooks `io_open`/`io_close2` and taps bodies (section 3.1). It is built into the player and `mirage_tests` only.
2. **Network timing is per request, not per segment.** Rows are keyed by URL; only the inspector maps them to sequence numbers. In `mirage_vmaf` nothing is recorded, and there reads are paced by scoring (section 3.3).
3. **"ABR / player: variant switch events"**: Mirage has no ABR. Manual switches leave free-text log lines, with no rung (section 6). D-068 already records this.
4. **Decoder events (frames dropped, errors in decoding, where keyframes are)**:
   - `mirage_vmaf` records none per frame, and concealed decode errors don't reach its report (section 7);
   - the player counts late drops in aggregate, and keeps keyframes in `KeyframeIndex` for UI stepping only;
   - frames skipped before a switch's take-over point are counted nowhere. That matters to D-068's limits.
5. **Events from parsing HLS and DASH (EXT-X-DISCONTINUITY, where DASH periods begin and end, tags private to a vendor)**:
   - `mirage_vmaf` parses no media playlist, and FFmpeg's hls demuxer skips these tags;
   - the player's `ManifestParser` reads them only from a playlist opened directly, with the defects of section 4.2;
   - DASH: the linked libavformat has a `dash` demuxer (run `demuxers`), but `mirage_vmaf`'s ladder reads HLS masters only (`<repo>/src/VMAFLadder.h:15-22`). D-070 leaves DASH EventStream as an interface.
6. **The timeline keyed by media PTS, with the wall clock kept as well**: today's sources use five clocks (section 8). `ManifestTag::pts` is playlist time despite its name.

---

## 11. Design choices for the owner

Each choice lists its options and their trade-offs. The recommendations are the agent's; none is a decision.

### 1. The timeline's key

- **(a) Reference PTS in the reference stream's own time base**, as an integer, with the time base recorded. Exact, as the aligner's comparisons are (`<repo>/src/VMAFAlign.h:17-22`). Needs ticks the report doesn't carry today.
- **(b) Seconds as a double**, as reports give PTS today. Simple and already present, but boundary comparisons need tolerances: 3 of 40 packets were misplaced at exact boundaries (run `map-frames-to-segments`).
- **(c) Reference frame index.** Stable across pairs only where the same reference frames are scored (coarser grids differ), and events between frames have no index.

Agent's recommendation: (a), with seconds as a derived field for reading, and the reference frame index as an attribute of VMAF events. §7's `timeline[].pts` and `frames[].pts` are Phase 5's to fix.

### 2. Frame → segment

- **(a) Byte position.** `pkt->pos` against the cumulative `bytes_read` of the files SegmentWatch saw. Exact on the fixture, and no extra I/O. Relies on hls.c's single continuous stream; unverified for fMP4 with EXT-X-MAP, ID3 timestamps and encryption: MEASUREMENT PENDING.
- **(b) PTS against EXTINF sums**, FFmpeg's own rule. No extra I/O, but approximate: float rounding, and EXTINF that is not exact (the fixture's last segment). Across discontinuities with jumps it needs an offset per period.
- **(c) Segment heads.** `hls::parse_segment_start` on each segment's first bytes, taken by a body tap like `NetworkLogger::tap_body` or by range reads like `NetProbe`. Exact media start times as FFmpeg reports them, and already unit-tested (`<repo>/tests/test_hls_align.cpp:365-468`). Costs bytes, code and an init-segment parse for fMP4.

Agent's recommendation: (a), with (b) computed as a cross-check whose disagreements beyond half a frame go into the report as warnings; (c) only if (a) fails on fMP4.

### 3. How `mirage_vmaf` gets each media playlist's text

- **(a) A separate avio read of each input's playlist URL**, as `read_master` reads the master, with its SHA-256 recorded as the master's is (`<repo>/src/VMAFLadder.cpp:332`, `<repo>/src/VMAFReport.cpp:274-277`). One more request per playlist, and the copy could differ from what the demuxer read, but VOD only is in scope.
- **(b) Tap the playlist bytes the hls demuxer reads.** SegmentWatch sees that open ([demux.c#L180](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/demux.c#L180)), but avformat_open_input probes that handle and swaps its buffer (`<repo>/src/NetworkLogger.h:144-149`).

Agent's recommendation: (a).

### 4. Which parser carries the playlist events

- **(a) Extend `hls::parse_media_timeline`** with tag events attached to the segment they precede, including D-070's tags. It is pure code, already has segments, discontinuity sequence numbers and PDT, and builds into `mirage_vmaf` without pugixml.
- **(b) Fix and extend `ManifestParser`.** It already has CueOut/CueIn/Unknown, but it has the defects of section 4.2, no segments, and pugixml and NetworkLogger as dependencies.
- **(c) A new parser.**

Agent's recommendation: (a), leaving `ManifestParser` to the player's UI until the player reads the timeline. Changing its tested behaviour, such as the inherited `disc_seq` (`<repo>/tests/test_manifest_parser.cpp:95`), would be the owner's call too.

### 5. HTTP timing in `mirage_vmaf`

- **(a) A per-input recorder in SegmentWatch's hooks:** times, bytes, error codes. Pool-safe, no global state, but no exact status, and close times paced by scoring.
- **(b) (a), plus `av_log` lines claimed by each handle's `URLContext`** for status, request sent, first byte and `Latency:`. This adds one process-global dispatcher, and the lines are TRACE/DEBUG text whose wording FFmpeg can change.
- **(c) Install `NetworkLogger` as it is:** log-only rows with status and times. No bytes read, no local files, no per-input tag, and a conflict with SegmentWatch if hooked (run `netlog-probe`).
- **(d) No network timing from the scoring reads.** A session's HTTP timing comes from its own source: the scripted plan in tests (D-068), or later the player's log.

For §3.2's example of a segment request lasting 2.3 s in a test, a server that delays a response by a set time is one way. `tests/HttpFileServer.h` can only hold a path's first requests unanswered or cut a body short (`:31-35`), so a delay would be a new rule.

Agent's recommendation: (d) for the session's timing, and (a) to record what the scoring reads saw, labelled as scoring-paced. (b) only if the per-segment status is needed from `mirage_vmaf`.

### 6. Sessions that play the reference in top-rung mode

The top rung isn't scored against itself.

- **(a) Mark those spans "reference, not scored"** and leave them out of drop detection.
- **(b) Take them as 100**: assumed, not measured.
- **(c) Require mezzanine mode for such sessions.**
- **(d) Score the top rung against itself once**: measured, at the cost of a pair.

Agent's recommendation: (a), stated in the report.

### 7. "Wallclock recorded too"

- **(a) Observation time**: the system clock of the event as recorded, with NetworkLogger's single-offset mapping or a fresh reading per event.
- **(b) Program date-time** from PDT.
- **(c) Both**: observation time for observed events (network, player), null for offline-derived ones (VMAF, playlist), and PDT in the payload where present.

Agent's recommendation: (c).

### 8. Discontinuity sequences

The aligner's sequences come from timestamp jumps, the playlist's from tags (section 4.1). Mapping events by segment (choice 2a) avoids having to match the two. Otherwise, tags whose timestamps continue would need special handling.

Agent's recommendation: follow choice 2.

### 9. Where a tag sits in time

- **(a) At the start of the next segment**: the PTS of its first frame.
- **(b) At its playlist time plus an offset.**

Agent's recommendation: (a). For DATERANGE, START-DATE through PDT, with the segment as a fallback.

### 10. The player's switch log, later (D-068)

- **(a) Logging-only hooks** in `switch_video`, `read_loop`, `video_decode_loop` and `on_present`, with a D-007 plan.
- **(b) Parse the existing `switch-trace` text**: no code change, but fragile, with no rung and the uptime clock.
- **(c) Leave it to Phase 4.**

Agent's recommendation: (a) when the player log is taken up. It is not needed for Phase 3's scripted-plan tests.

---

## 12. Open issues

- **MEASUREMENT PENDING:**
  - Mapping frames to segments by byte position on fMP4 (EXT-X-MAP), with ID3, and on real ladders, D-011's.
  - That `ref_index` and `ref_pts` are identical for the same reference frame across the pairs of one ladder run.
  - That the player and `mirage_vmaf` give the same PTS for the same frame of a variant opened through the master and alone, MPEG-TS's 33-bit clock wrap included (`<repo>/src/HlsAlign.cpp:1265-1271` unwraps it for the inspector).
- **Decode errors:** `decode_error_flags` was seen only from FFmpeg's MPEG-2 decoder, with concealment (`FF_DECODE_ERROR_CONCEALMENT_ACTIVE`). What other decoders set (H.264 and HEVC, `AV_FRAME_FLAG_CORRUPT`, frames they don't output) is not measured.
- **D-068 doesn't say where a scripted session's HTTP timing comes from.** It says a session has "the HTTP timing of its segments", and that tests take sessions from a scripted plan. The candidates are the plan itself, a separate fetch, or the scoring reads, which are paced by scoring (section 3.3). The work order's example of a 2.3 s segment request needs one of them in the tests. This is for the owner (section 11, choice 5).
- **SCTE-35 inside MPEG-TS segments**, which D-070 leaves as an interface: whether FFmpeg's hls demuxer exposes that data stream is still unverified, as D-070 says. Not probed here.
- **The fixture's hls muxer output** has `TARGETDURATION:0` and a last EXTINF shorter than its frames (run `fixture`). Fixtures for D-069 should check their playlists, or set them explicitly, if EXTINF placement is tested.
- **This survey's fixture is not D-069's.** Its "damage" is transmission damage, not "a segment encoded at a deliberately low quality". It and the GOP variant served only as probes.
- **Surveyed but not run:** the player's own reading. No GUI player run was made, so everything about the player is from its code.
- **`<repo>/docs/VMAF_GAP_REPORT.md:230-231`** (Phase 0) says the Decoder "has a dropped-frame counter". The counter is the render loop's `total_dropped` (`<repo>/src/main.cpp:1751`), not in `Decoder`. Noted, not edited.

---

## Appendix A. Runs (`<out>/runs/<name>/`)

| Name | What | Program |
|---|---|---|
| `build` | builds the probes | `<out>/probe/build.sh` |
| `fixture` | makes the fixture into `runs/fixture-data/` and prints its playlists and file sizes | `make_fixture.cpp` |
| `fixture-gop` | adds `fixture-data/gop/`: GOP 10, B-frames | `make_fixture.cpp --gop` |
| `corrupt-segment` | copies `dis/` to `dis_corrupt/` and damages `seg1.ts` (11 of its 83 TS packets) | `corrupt_segment.py` |
| `check-scte35-fixture` | the fixture's SCTE-35 bytes against RFC 8216 §8.10 | `check_scte35_fixture.py` |
| `manifest-probe-tagged` | `ManifestParser`, `parse_media_timeline` and `parse_media_playlist` on `dis/tagged.m3u8`, compiled from `<repo>/src` | `manifest_probe.cpp` |
| `logprobe-file` | the analyzer's open of `dis/tagged.m3u8` from its path: every `av_log` line, `io_open`/`io_close2`, packet and frame | `logprobe.cpp` |
| `logprobe-http` | the same over HTTP from `tests/HttpFileServer.h` | `logprobe.cpp` |
| `logprobe-concurrent` | two inputs on two threads; thread 2 replaces the `av_log` callback | `logprobe.cpp` |
| `logprobe-gop` | the GOP fixture: `pict_type`, key flags | `logprobe.cpp` |
| `logprobe-corrupt` | the damaged segment: decoder errors, `decode_error_flags` | `logprobe.cpp` |
| `map-frames-to-segments` | packet → segment by byte position and by PTS, from `logprobe-file`'s output | `map_frames_to_segments.py` |
| `netlog-probe` | `NetworkLogger` (from `<repo>/src`) beside the analyzer's open, over HTTP and locally, and with its own hook | `netlog_probe.cpp` |
| `demuxers` | which demuxers the linked libavformat has | `demuxers.cpp` |
| `grep-checks` | the text searches cited: ABR code, `request_video_switch`'s callers, `COPY_OPAQUE`, `AVPacket::pos`, CUE tags in RFC 8216 and in the tests | `grep_checks.sh` |
| `mirage-vmaf-help` | `mirage_vmaf --help` | `<repo>/build/macos-debug/mirage_vmaf` |
| `mirage-vmaf-ladder` | `--ladder master.m3u8` → `report.json` | the same |
| `mirage-vmaf-pair-tagged` | `--ref ref/index.m3u8 --dis dis/tagged.m3u8` → `report.json` | the same |
| `mirage-vmaf-pair-corrupt` | `--ref ref/index.m3u8 --dis dis_corrupt/index.m3u8` → `report.json` | the same |

## Appendix B. The fixture (`<out>/runs/fixture-data/`, from run `fixture`)

- **What it is:** two HLS VOD streams made with FFmpeg's hls muxer through the libraries, as `tests/test_vmaf_segments.cpp:46-117` makes its own. Each has 40 frames at 25 fps, MPEG-2, every frame a key frame, `hls_time` 0.4: four MPEG-TS segments.
  - `ref/` is 192x108, with segments of 30644–30832 bytes;
  - `dis/` is 96x54, with segments of 15604 bytes.
- **`master.m3u8`** names `ref/` (BANDWIDTH 2000000) and `dis/` (BANDWIDTH 500000).
- **`dis/tagged.m3u8`** is `dis/index.m3u8` with these tags written in:
  - PDT;
  - DISCONTINUITY before segments 1 and 3;
  - a DATERANGE with SCTE35-OUT, and `#EXT-OATCLS-SCTE35` and CUE-OUT:0.8, before segment 1;
  - CUE-OUT-CONT before segment 2;
  - CUE-IN, a DATERANGE with SCTE35-IN and a vendor tag, before segment 3.
  
  Its timestamps are continuous: the tags are text only.
- **The DATERANGE values** are RFC 8216 §8.10's example values (PLANNED-DURATION and DURATION 59.993 and the SCTE-35 bytes). They are not consistent with the fixture's 0.8 s break and are interpreted by none of the code surveyed.
