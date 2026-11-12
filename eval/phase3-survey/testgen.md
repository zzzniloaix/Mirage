# Synthetic content for §3.2's two attribution tests: a prototype (D-069)

A survey for Phase 3's design: nothing of Phase 3 is implemented. This prototypes the media the two acceptance tests of the work order's §3.2 would make within ctest (D-069): a ladder with one segment of one lower rung encoded at a much lower quality ("degraded"), and a ladder with a low-quality "ad" between `EXT-X-DISCONTINUITY` tags, with `EXT-X-CUE-OUT` before it and `EXT-X-CUE-IN` after it ("ad"). It writes them with FFmpeg's libraries only, scores them with Phase 2's `mirage_vmaf --ladder`, and tabulates the scores per segment.

Names used below: `<out>` is this survey's output directory, `<scratch>` the session's scratch directory, `<repo>` the worktree of `vmaf/phase-3` at `75cf6e0`, `<homebrew>` Homebrew's prefix. Every number below comes from a file under `<out>/testgen/` named next to it, or from a cited source. `logs/NN_*.txt` are the records of the final pass of `testgen/run_all.sh`: each has the command, the directory it ran in, its full stdout and stderr, its exit status and its wall time, with local paths replaced by the names above (`testgen/run.py`). The scores are from the Phase 2 program `<scratch>/accept/wt/build/macos-release/mirage_vmaf`, SHA-256 `454db2ed6be26319975df2130b53fc4fd5a60ed6660cbdfef6278ba88dc8435b` (`logs/00_mirage_vmaf_sha256.txt`; its `--help` in `logs/01_mirage_vmaf_help.txt`). Design parameters of the prototype (frame size, frame rate, segment length, quantizers, the detection prototype's window) are the agent's choices for the prototype; none of them is a decision.

## Summary

- Both ladders work as test content. In the degraded ladder (libx264), rung `v270`'s segment 4 scores a mean VMAF of 41.239 against 97.134 to 98.231 in its other segments, and the frames more than 6 points below the pair's median are exactly frames 100 to 124, segment 4's 25 frames; in the control rung `v180`, not degraded, only frame 0 is (`logs/15_degraded_x264_tables.txt`). In the ad ladder (libx264), `v180`'s two ad segments score 27.409 and 27.380 against 90.966 to 91.978 in the main content, the low frames are exactly frames 75 to 124, the ad's 50 frames, between the `EXT-X-CUE-OUT` and the `EXT-X-CUE-IN`, and the control rung `v270`, whose ad is at the normal quantizer, has a single low frame, the first of the ad (`logs/46_ad_x264_tables.txt`).
- The same holds with FFmpeg's own MPEG-2 encoder (`logs/25_degraded_mpeg2_tables.txt`, `logs/55_ad_mpeg2_tables.txt`), and in mezzanine mode for the degraded ladder (`logs/18_degraded_x264_tables_mezzanine.txt`).
- Determinism: each ladder generated twice gives 29 of 29 files with the same SHA-256, for both encoders and both ladders, and a build of the generator without optimization gives the same files (`logs/12`, `22`, `42`, `52`, `91`).
- Time: generating a ladder takes 2.022 to 2.313 s and scoring it 0.457 to 0.515 s on this machine (`logs/10`, `20`, `40`, `50`, `14`, `24`, `45`, `54`); the generator built without optimization, as the debug preset compiles the tests (`-g` and no `-O`, `<repo>/build/macos-debug/build.ninja:1229`), takes 16.941 s (`logs/90`).
- Problems met, each with its evidence in "(d)": FFmpeg's hls demuxer ignores `EXT-X-DISCONTINUITY` and reads all segments as one MPEG-TS stream, so timestamps jump as written and continuity counters are checked across the splice; continuity counters that restart unmarked at the tag, which RFC 8216 allows, make FFmpeg flag packets corrupt (scores unchanged); a sequence whose first DTS would be negative is shifted by the muxer; the ad's PTS can repeat the main content's, so a timeline keyed by PTS alone can't place a frame; `mirage_vmaf`'s discontinuity sequences aren't the playlist's; mezzanine mode can't score the ad ladder against a one-file mezzanine.

## 1. How the existing tests make media

- `tests/test_vmaf_ladder.cpp:108-181` (`make_rung()`): one rung is one MPEG-TS segment written with the `mpegts` muxer, a playlist written as text (`:175-179`), a master written as text (`:201-212`). The picture is a gray diagonal ramp that moves 3 steps a frame (`:156-159`), coded by FFmpeg's MPEG-2 encoder, every frame a key frame (`gop_size 1`), with `AV_CODEC_FLAG_QSCALE` and `global_quality = FF_QP2LAMBDA * 2` (`:123-126`).
- `tests/test_vmaf_segments.cpp:46-117` (`make_stream()`): FFmpeg's `hls` muxer cuts the stream into 0.4 s segments (`:72`), and writes the playlist.
- `tests/test_vmaf_analyzer.cpp:307-385` (`make_ts_restart_clip()`): two MPEG-TS files written one after the other, the second with its timestamps from 0 again (`:369`) and `mpegts_flags initial_discontinuity` (`:362`), so that the continuity counters that restart in it are marked.
- `tests/test_hls_variants.cpp:150-171` and `:174-200`: the `hls` muxer with 1 s segments, MPEG-2 with a key frame a second, 2 B-frames "pts != dts, as in real streams" and a bit rate (`:184-186`); `:232-251`, a master with audio groups.
- `tests/TestTemp.h:19-43`: each run's own temporary directory; `tests/HttpFileServer.h:32-36`: a local HTTP server whose rule can hold a path's first requests or cut its body short.

A reading of FFmpeg 8.1.1's sources (not run): its MPEG-2 encoder takes a fixed quantizer from each frame's `quality` (`libavcodec/mpegvideo_enc.c:3618-3652`, `estimate_qp()`), not from `global_quality`, which that file doesn't read; `ffmpeg`'s `-q:v` sets it on every frame (`fftools/ffmpeg_enc.c:803`). The MPEG-2 helpers of the tests set `global_quality = FF_QP2LAMBDA * 2` (`tests/test_vmaf_ladder.cpp:126`, `tests/test_vmaf_segments.cpp:60`, `tests/test_vmaf_analyzer.cpp:320` and `:413`, `tests/test_rtmp_input.cpp:61`) and leave `frame->quality` at the 0 a new frame has (`libavutil/frame.c:33`), so their quantizer is the clip to `qmin` (`mpegvideo_enc.c:215-217`), whose default is 2 (`libavcodec/options_table.h:105`): the 2 they ask for, by coincidence. The prototype sets `frame->quality`. (Line numbers here are of the n8.1.1 tag's files, kept in `testgen/ffmpeg-n8.1.1/`, fetched from `https://raw.githubusercontent.com/FFmpeg/FFmpeg/n8.1.1/<path>`.)

## 2. The prototype

### 2.1 Files and build

- `testgen/src/testgen.cpp`: the prototype, C++20, one file, libavformat, libavcodec, libavutil and libswscale only. Commands: `encoders` (the build's encoders, by `avcodec_find_encoder_by_name`), `degraded <dir>`, `ad <dir>`, `mezzanine <file.mkv>`, `probe <url>` (reads a playlist through FFmpeg's hls demuxer and prints every packet and decoded frame, the timestamp jumps, the packets and frames flagged corrupt and the continuity check failures FFmpeg logs).
- `testgen/build.sh`: `clang++ -std=c++20 -O2 -Wall -Wextra -o testgen src/testgen.cpp $(pkg-config --cflags --libs libavformat libavcodec libavutil libswscale)` with `PKG_CONFIG_PATH=<homebrew>/opt/ffmpeg-full/lib/pkgconfig`; the expanded command and pkg-config's flags in `logs/02_build.txt`. `testgen/build_O0.sh`: the same with `-O0` (`logs/03_build_O0.txt`).
- `testgen/run_all.sh <mirage_vmaf> <debug mirage_vmaf>`: every step below, in order, each through `testgen/run.py`; `testgen/tables.py`: the per-segment tables from a report and the ladder's `layout.json`; `testgen/tree_sha256.py`, `compare_reports.py`, `report_summary.py`, `frames_at.py`: the comparisons and extracts the logs cite.
- `testgen/reports/`: the reports (they hold relative paths only), with the `layout.json` of each ladder; `testgen/media/`: the ladders and mezzanines (regenerable, not meant to be committed; their sizes in 3.7).
- The build's encoders: libx264, libx265, mpeg2video, mpeg4, ffv1 and others are found, libopenh264 isn't (`logs/04_encoders.txt`). The machine: Apple M3 Pro, 12 physical cores, macOS 26.6.2, shared with other work, load averages 2.92 3.74 4.00 when the pass started (`logs/05_machine.txt`).

### 2.2 The source, and why not the ramp

The source is computed at the top rung's size, 640x360, and scaled down with libswscale (Lanczos, `SWS_ACCURATE_RND | SWS_BITEXACT`) for the lower rungs, as a ladder's rungs are encoded from one mezzanine. The main content is fractal value noise, octaves of 2- to 64-pixel cells each weighted by its cell size (amplitude falling as 1/frequency), moving 1.5 px left and 0.5 px up a frame (sub-pixel motion, so that motion compensation doesn't copy whole pixels), with a textured disc and a striped disc on their own paths (sharp edges, local motion) and low-frequency color. It is computed with an integer hash and integer arithmetic only, so its pixels don't depend on floating point.

Why it and not the existing tests' ramp, from the same ladder generated with each (`testgen degraded --source ramp`):

| | noise (this prototype) | ramp (the existing tests') |
|---|---|---|
| top rung, 8 s at QP 22, bytes | 596336 (`logs/10`) | 163184 (`logs/31`) |
| measured BANDWIDTH v360 / v270 / v180 | 624160 / 565504 / 345920 (`logs/10`) | 169952 / 234624 / 166944 (`logs/31`): v270's is the highest, so the ladder was refused (`logs/30_ramp_gen_measured_bandwidth.txt`) and written with the existing tests' nominal 900000 / 600000 / 300000 to be scored |
| v270 / v180 mean VMAF in segment 0 at QP 22 | 97.867 / 91.622 (`logs/15`) | 82.958 / 72.193 (`logs/33`) |
| ITU-T P.910 SI / TI of the luma | 56.624 / 21.325 (`logs/10`) | 65.893 / 27.570 (`logs/31`) |

The ramp's wrap from 255 to 0 is a sharp edge that raises SI and TI and costs its 480x270 rung more than its top rung (212064 bytes against 163184, `logs/31`), so neither SI nor TI says how compressible this kind of synthetic picture is; the bytes and the BANDWIDTH order do. With the ramp the top-rung rule (D-002) would pick `v270` as the reference. The ramp's drop at the degraded segment is clear too (36.422 against 82.958 to 84.194, `logs/33`). SI and TI here are computed by the prototype as ITU-T P.910 defines them (Sobel magnitude, border pixels left out, the largest standard deviation over the frames; TI between consecutive frames only); the edition's clause wasn't checked (`https://www.itu.int/rec/T-REC-P.910`).

### 2.3 The ladders

Both: three rungs, `v360` 640x360, `v270` 480x270 and `v180` 320x180; 8 segments of 25 frames at 25 fps (1 s, 200 frames); MPEG-TS segments; a master with BANDWIDTH and AVERAGE-BANDWIDTH measured from the segments (RFC 8216 §4.3.4.2: the peak and average segment bit rate), RESOLUTION, FRAME-RATE and, for H.264, CODECS from the first SPS (`avc1.64001e`, `avc1.640015`, `avc1.64000c`, `logs/10`); and `layout.json`, what each segment is (its first frame, PTS, period, tags and each rung's quantizer), which `tables.py` reads. The main content's first PTS is 10 s.

- **degraded**: segment 4 (frames 100 to 124, playlist time 4 to 5 s) of `v270` at the low quantizer; `v180` is the control. Defaults: libx264 QP 22, segment 4 at 46; MPEG-2 qscale 4, segment 4 at 31.
- **ad**: main content in segments 0 to 2, the ad in segments 3 and 4, main content in 5 to 7; `v180`'s ad at the low quantizer, `v270` the control. Its media playlist (`media/ad-x264-a/v180/index.m3u8`):

```
#EXTM3U
#EXT-X-VERSION:3
#EXT-X-TARGETDURATION:1
#EXT-X-MEDIA-SEQUENCE:0
#EXT-X-DISCONTINUITY-SEQUENCE:0
#EXT-X-PLAYLIST-TYPE:VOD
#EXTINF:1.000000,
main_000.ts
#EXTINF:1.000000,
main_001.ts
#EXTINF:1.000000,
main_002.ts
#EXT-X-DISCONTINUITY
#EXT-X-CUE-OUT:2.000
#EXTINF:1.000000,
ad_000.ts
#EXT-X-CUE-OUT-CONT:1.000/2.000
#EXTINF:1.000000,
ad_001.ts
#EXT-X-DISCONTINUITY
#EXT-X-CUE-IN
#EXTINF:1.000000,
main_005.ts
#EXTINF:1.000000,
main_006.ts
#EXTINF:1.000000,
main_007.ts
#EXT-X-ENDLIST
```

  The cue tags have the plain form of AWS Elemental's "Elemental" ad markers, `EXT-X-CUE-OUT:30.000`, `EXT-X-CUE-OUT-CONT: 8.308/30` (elapsed / duration) and `EXT-X-CUE-IN` (`https://docs.aws.amazon.com/medialive/latest/ug/sample-manifests-hls.html`), placed as in AWS MediaTailor's origin playlist example: `EXT-X-CUE-OUT` before the first segment of the break, `EXT-X-CUE-OUT-CONT` before each one after it, `EXT-X-CUE-IN` before the first segment after the break (`https://docs.aws.amazon.com/mediatailor/latest/ug/manifest-hls-example.html`). The main content's timeline goes on through the break, as where an ad replaces content of the same length: segment 5 holds the content's frames from 5 s, PTS 15 s. The ad's timestamps start at 10 s by default, the main content's own origin (`--ad-origin`). The `EXT-X-DATERANGE` and `#EXT-OATCLS-SCTE35` forms are left out: both carry a splice_info_section, and the only citable ones found don't fit a 2 s ad: RFC 8216 §8.10's example is a 59.993 s break ("Media Segment declarations for 60s worth of media", `https://www.rfc-editor.org/rfc/rfc8216#section-8.10`), and AWS's `#EXT-OATCLS-SCTE35` example goes with `#EXT-X-CUE-OUT:201.467` (the page above).
- The ad's content (`--ad-content`): **calm**, the default, is the main content's kind of picture with another lattice, other motion, a textured box, a striped box and other colors (SI 56.624 main, 61.405 ad, `logs/40`); **busy**, the first version, adds diagonal bands, a checkered box and saturated color noise (ad SI 104.485, `logs/56`). Section 3.2 shows why calm is the default here.

### 2.4 How the segments are written

- Each segment of each rung has an encoder of its own, so each starts on a key frame and the quantizer changes exactly at the segment's first frame. libx264: preset veryfast, constant QP through its `qp` option, `keyint` 25, `scenecut=0`, `threads` 1 (its wrapper's default is automatic, `libavcodec/libx264.c:1595`, `:1323`), BT.709 tags. Its SEI gives the settings: `x264 - core 165 r3222 b35605a ... threads=1 lookahead_threads=1 ... bframes=3 b_pyramid=2 ... keyint=25 keyint_min=13 scenecut=0 ... rc=cqp mbtree=0 qp=22 ip_ratio=1.40 pb_ratio=1.30 aq=0` (`logs/10`; segment 4 at QP 46 shows I frames at QP 43, P at 46, B at 47.65 in the same log's stderr). MPEG-2: GOP 25, 2 B-frames, closed GOP, the quantizer on every frame (section 1), and scene change detection off, which a closed GOP needs (`mpegvideo_enc.c:782-788`). (libx264 can change its constant QP between frames, `libx264.c:232-238`, but with B-frames and its lookahead the change would not be known to land on the segment's first frame; not tried.)
- One `mpegts` muxer writes the consecutive segments of a rung within a discontinuity sequence, changing its output file at each segment and sending PAT and PMT again (`mpegts_flags resend_headers`, as FFmpeg's hls muxer does, `libavformat/hlsenc.c:1791`), so continuity counters and timestamps go on from one segment to the next, as RFC 8216 §3 requires within a sequence. A new sequence gets a new muxer, whose counters start again, and the discontinuity indicator on its first packets (`initial_discontinuity`), unless `--no-ts-discontinuity`.
- The probe confirms what was written: 200 packets and frames, the first PTS 10.000000 s and DTS 9.920000 s (libx264's two B-frame delay), no corrupt packet, no continuity failure (`logs/13_degraded_x264_probe_v270.txt`); MPEG-2's first DTS is 9.960000 s (`logs/23`).

## 3. Results

### 3.1 The degraded-segment ladder

libx264, top-rung mode, the reference `v360` (`logs/15_degraded_x264_tables.txt`):

| segment | reference frames | v270 QP | v270 mean | v270 min | v180 QP | v180 mean | v180 min |
|---|---|---|---|---|---|---|---|
| 0 | 0-24 | 22 | 97.867 | 91.957 | 22 | 91.622 | 84.557 |
| 1 | 25-49 | 22 | 97.226 | 95.854 | 22 | 90.966 | 88.095 |
| 2 | 50-74 | 22 | 98.231 | 96.664 | 22 | 91.978 | 90.275 |
| 3 | 75-99 | 22 | 97.774 | 96.132 | 22 | 91.315 | 89.057 |
| **4** | **100-124** | **46** | **41.239** | **38.443** | 22 | 91.199 | 89.265 |
| 5 | 125-149 | 22 | 98.044 | 96.606 | 22 | 91.676 | 89.379 |
| 6 | 150-174 | 22 | 97.134 | 94.397 | 22 | 91.494 | 88.929 |
| 7 | 175-199 | 22 | 97.296 | 95.638 | 22 | 91.009 | 87.963 |

- Where the drop starts and ends: in `v270`, the frames more than 6 points below the pair's median (97.563) are frames 100 to 124, segment 4's frames 0 to 24 of 25, and no other. In `v180` only frame 0 is (the first frame of the libvmaf context, whose motion features are 0, D-037). Every pair's reference PTS is the layout's (same log).
- A prototype of the work order's automatic detection, in `tables.py` (a frame is low when more than 6 points below the median of the last 50 scores that weren't low, a drop is more than 12 low frames; the window and N are the agent's choices for this table): one drop in `v270`, frames 100 to 124, baseline 97.963 at its start, lowest score 38.443; none in `v180` (same log).
- MPEG-2: `v270`'s segment 4 at qscale 31 has a mean of 62.022 against 96.923 to 97.671 elsewhere, the low frames are again frames 100 to 124 (and frame 0), the detection prototype finds one drop, frames 100 to 124; `v180` none (`logs/25_degraded_mpeg2_tables.txt`).
- Mezzanine mode, against a lossless FFV1 mezzanine of the source (19744829 bytes, `logs/16`): `v360` scores 100.000 in segments 1 to 7 and a mean of 99.817 in segment 0; `v270`'s segment 4 has a mean of 40.917, the drop is frames 100 to 124; `v180` none (`logs/18_degraded_x264_tables_mezzanine.txt`). The top rung's own quality is flat, so in top-rung mode the drop is `v270`'s.

### 3.2 The ad-boundary ladder

libx264, calm ad, top-rung mode (`logs/46_ad_x264_tables.txt`):

| segment | file | tags before it | v180 QP | v180 mean | v180 min | v270 QP | v270 mean | v270 min |
|---|---|---|---|---|---|---|---|---|
| 0 | main_000.ts | | 22 | 91.622 | 84.557 | 22 | 97.867 | 91.957 |
| 1 | main_001.ts | | 22 | 90.966 | 88.095 | 22 | 97.226 | 95.854 |
| 2 | main_002.ts | | 22 | 91.978 | 90.275 | 22 | 98.231 | 96.664 |
| **3** | **ad_000.ts** | EXT-X-DISCONTINUITY, EXT-X-CUE-OUT:2.000 | **46** | **27.409** | **22.237** | 22 | 98.094 | 91.734 |
| **4** | **ad_001.ts** | EXT-X-CUE-OUT-CONT:1.000/2.000 | **46** | **27.380** | **22.696** | 22 | 97.242 | 95.689 |
| 5 | main_005.ts | EXT-X-DISCONTINUITY, EXT-X-CUE-IN | 22 | 91.685 | 89.379 | 22 | 98.045 | 96.606 |
| 6 | main_006.ts | | 22 | 91.494 | 88.929 | 22 | 97.134 | 94.397 |
| 7 | main_007.ts | | 22 | 91.009 | 87.963 | 22 | 97.296 | 95.638 |

- In `v180` the frames more than 6 points below the median (91.140) are frames 75 to 124, from the ad's first frame (segment 3, its frame 0) to its last (segment 4, its frame 24), and frame 0; the detection prototype finds one drop, frames 75 to 124 (baseline 91.692, lowest 22.237). Its first frame is the one the `EXT-X-CUE-OUT` and the first `EXT-X-DISCONTINUITY` come before, and the frame after its last is the one the `EXT-X-CUE-IN` comes before. In `v270` only frame 75 is below its median (97.934) by more than 6: the first frame of a new libvmaf context; the prototype finds no low frame (same log).
- The control, `v180`'s ad at QP 22 (`--q-low 22`): its ad segments score 90.599 and 90.027 against 90.966 to 91.978, frames 0 and 75 below the median by more than 6, no drop (`logs/49_ad_x264_control_tables.txt`). The change of content alone moves `v180`'s scores little; the drop is the ad's quantizer.
- The busy ad: at QP 46 `v180`'s ad scores 23.089 and 22.906, the drop frames 75 to 124 (`logs/58_ad_x264_busy_tables.txt`); at QP 22, 84.814 and 85.635, with 27 frames below the median (91.311) by more than 6, in runs of 1 to 3 frames, and no drop by the prototype (`logs/59c_ad_x264_busy_control_tables.txt`). Its content alone takes `v180` close to the 6-point threshold, and its segments cost 474512 and 478272 bytes at `v360` against 67868 and 68432 for the calm ad's (`logs/56`, `logs/40`). Hence calm as the default.
- MPEG-2 (calm ad): `v180`'s ad at qscale 31 scores 47.984 and 46.974, the drop frames 75 to 124; `v270`'s ad 96.216 and 95.510, no drop (`logs/55_ad_mpeg2_tables.txt`).
- The report's frames at the splices (`logs/46b_ad_x264_frames_at_splices.txt`): frame 74 at PTS 12.96 s scores 91.983, frame 75 at PTS 10 s 29.249, frame 124 at 11.96 s 29.201, frame 125 at 15 s 94.296. Frames 0 and 75 both have `ref_pts` 10: see (d).

### 3.3 (a) Determinism

| ladder | files the same in two generations | bytes in them |
|---|---|---|
| degraded, libx264 | 29 of 29 (`logs/12_degraded_x264_sha256.txt`) | 1409333 |
| degraded, MPEG-2 | 29 of 29 (`logs/22_degraded_mpeg2_sha256.txt`) | 2447887 |
| ad, libx264 | 29 of 29 (`logs/42_ad_x264_sha256.txt`) | 1387448 |
| ad, MPEG-2 | 29 of 29 (`logs/52_ad_mpeg2_sha256.txt`) | 2415852 |
| degraded, libx264, generator built with -O0 against -O2 | 29 of 29 (`logs/91_degraded_x264_sha256_O0.txt`) | 1409333 |

- The settings chosen for it: integer arithmetic for the source; `SWS_BITEXACT | SWS_ACCURATE_RND`; `AV_CODEC_FLAG_BITEXACT` and one thread for both encoders; `AVFMT_FLAG_BITEXACT` for the muxers.
- The x264 is core 165 r3222 `b35605a` (`logs/10`), the commit Homebrew's formula and the Docker image pin (`<repo>/docker/README.md:37`, `<repo>/docker/Dockerfile:60`), and libx264 reported using ARMv8 NEON, DotProd and I8MM (`logs/10`, stderr). Whether another machine or the Docker image writes the same bytes: MEASUREMENT PENDING. The x264 SEI, with its version, is in every segment's first packet, so another x264 writes other bytes; a test should compare behavior, not hashes.
- The mezzanines were each written once; not compared.
- The debug build of the Phase 3 branch's `mirage_vmaf` (SHA-256 `fe02dbcec650396151ce83fbe1deeffed4eaed19531e0e322b7fddee84625e76`, `logs/96`) gives the release program's frames, alignment and pooled values on both libx264 ladders (`logs/99a_debug_compare_degraded.txt`, `logs/99b_debug_compare_ad.txt`).

### 3.4 (b) Wall times

On the machine of 2.1, shared with other work, one run each; no benchmark.

| ladder | generation, process (its own count: source part) | scoring, `mirage_vmaf --ladder`, process |
|---|---|---|
| degraded, libx264 | 2.312 s (1.613 s) `logs/10` | 0.491 s `logs/14` |
| degraded, MPEG-2 | 2.060 s (1.638 s) `logs/20` | 0.457 s `logs/24` |
| ad, libx264 | 2.313 s (1.605 s) `logs/40` | 0.515 s `logs/45` |
| ad, MPEG-2 | 2.022 s (1.606 s) `logs/50` | 0.463 s `logs/54` |
| degraded mezzanine, FFV1 | 2.374 s `logs/16` | 1.106 s, three pairs `logs/17` |
| degraded, libx264, generator at -O0 | 16.941 s (15.959 s) `logs/90` | |
| debug `mirage_vmaf` | | 0.458 s degraded `logs/97`, 0.462 s ad `logs/98` |

Each report's pool ran its two pairs at once (`max_running` 2 of 11 jobs), each pair 0.368 to 0.404 s for the four ladders above (`logs/95_report_summary.txt`). The source's integer noise is most of the generation time, and nearly all of it without optimization.

### 3.5 (c) How `mirage_vmaf` pairs the ad's discontinuity sequences

- What the analyzer gets: FFmpeg's hls demuxer passes each segment's timestamps through, so `v180` jumps by −2.960000 s before frame 75 (12.96 to 10.00 s) and by +3.040000 s before frame 125 (11.96 to 15.00 s), and `v360`, the reference, at the same frames by the same amounts (`logs/43_ad_x264_probe_v180.txt`, `logs/44_ad_x264_probe_v360.txt`).
- The report (`logs/46`): each input has two sequences, `{first_index 0, first_pts 10, frames 75}` and `{first_index 75, first_pts 10, frames 125}`; two libvmaf contexts, 75 and 125 pairs; `discontinuities` 2 (one jump in each input); 200 frames paired, 0 failed, for both pairs. A timestamp that goes back, or forward by more than 10 s, is a discontinuity (`logs/01`, the `--help` text; `"discontinuity_jump": 10` in the report), so the +3.04 s jump after the ad is none: the ad and the main content after it are one sequence of the report, where the playlist has three discontinuity sequences. The jumps are at the same frame in both inputs, so D-050 lets each pair of sequences pair (D-046).
- With the ad's timestamps from 1 s (`--ad-origin 1`): the jumps are −11.960000 s and +12.040000 s (`logs/71`), each input has three sequences, `{0, 10, 75}`, `{75, 1, 50}` and `{125, 15, 75}`, three libvmaf contexts, `discontinuities` 4, 200 frames paired, and the drop in `v180` is again frames 75 to 124 (`logs/73_ad_x264_origin1_tables.txt`). Segment 5's minimum in `v180` is then 86.785, its first frame starting a context, against 89.379 with the ad from 10 s.
- Against a one-file mezzanine of the stitched programme, whose timestamps go on through the ad (FFV1, 19725131 bytes, `logs/80`): the reference (the mezzanine) has one sequence and each rung two, the jump is in one input only, and D-050 stops the pairing there: 75 pairs, 250 frames failed (1 unmatched reference, 249 after a discontinuity), failure rate 0.7692307692307693, exit status 0 with D-028's warning, for all three pairs (`logs/81_ad_x264_score_mezzanine.txt`, `logs/82_ad_x264_tables_mezzanine.txt`).

### 3.6 (d) Problems met

1. **FFmpeg's hls demuxer doesn't read `EXT-X-DISCONTINUITY`.** Its playlist parser handles STREAM-INF, KEY, MEDIA, TARGETDURATION, MEDIA-SEQUENCE, PLAYLIST-TYPE, MAP, START, ENDLIST, EXTINF and BYTERANGE (`libavformat/hls.c:861-980`), and it opens one demuxer per playlist (`hls.c:2271-2272`, `:2362`, `:2371`) on a reader that goes from segment to segment as one stream (`read_data_continuous()`, `hls.c:1641-1754`). So the timestamps jump as written (3.5), and the MPEG-TS demuxer checks continuity counters across segment files and across the splice.
2. **Continuity counters at a discontinuity.** RFC 8216 §3 has counters "continue uninterrupted" except at segments "explicitly signaled as discontinuities" (`https://www.rfc-editor.org/rfc/rfc8216#section-3`), so a conforming stream may restart them after the tag. FFmpeg's MPEG-TS demuxer then logs a continuity check failure (at debug level) and flags the PES corrupt unless the packet carries the discontinuity indicator (`libavformat/mpegts.c:3012-3034`), and the generic layer warns "Packet corrupt" (`libavformat/demux.c:587-595`). Without the indicator (`--no-ts-discontinuity`): 8 continuity failures logged, 2 packets flagged corrupt, 0 frames flagged corrupt, 0 decode errors (`logs/61_ad_x264_nodisc_probe_v180.txt`); the flagged packets are 73 and 123, the next to last before each splice, while the MPEG-TS demuxer's own warnings name the DTS of the last, packets 74 and 124 (1159200 and 1069200): the flag moves to the frame its parser returns then (`demux.c:1291`, the agent's reading). `mirage_vmaf` prints 16 "Packet corrupt" lines on stderr against none with the indicator (`logs/65_ad_x264_nodisc_count_warnings.txt`), and its frames, alignment and pooled values are the same as with the indicator (`logs/64_ad_x264_nodisc_compare.txt`). The report doesn't record these warnings.
3. **A sequence whose first DTS would be negative is shifted.** With the ad's timestamps from 0 and libx264's two-frame B delay, the ad's first frame lands at 0.080000 s, its first packet at DTS 0 (`logs/75_ad_x264_origin0_probe_v180.txt`), because the `mpegts` muxer of that sequence makes timestamps non-negative (`libavformat/mux.c:413-419`, `handle_avoid_negative_ts()` from `:636`). The layout then no longer gives the ad's PTS. The generator's sequences start at 1 s or later for this reason.
4. **PTS repeat across the splice.** With the ad's timestamps from the main content's origin, frames 0 and 75 both have reference PTS 10 s (`logs/46b`). A timeline "keyed by media PTS" (§3.1) can't place them by PTS alone; the frame's position in the reference (`ref_index`), or the discontinuity sequence with the PTS, tells them apart.
5. **The report's discontinuity sequences aren't the playlist's** (3.5): a forward jump of up to 10 s isn't one, so the ad and what follows it can be one sequence of the report. The playlist's tags give the boundaries.
6. **Mezzanine mode and the ad ladder** (3.5): a one-file mezzanine has none of the rungs' jumps, so D-050 stops the pairing at the ad.
7. **The first frame of each libvmaf context scores lower** (D-037): frame 0 of `v180` scores 84.557 in a segment whose mean is 91.622 (`logs/46b`), and frame 75 is `v270`'s only frame below its median by more than 6 points, in segment 3, whose minimum is 91.734 against a mean of 98.094 (`logs/46`). A detector that reports single frames would report every splice.
8. **Prototype issues fixed on the way**: MPEG-2's closed GOP refused without scene detection off (`mpegvideo_enc.c:782-788`); the ramp ladder refused by the BANDWIDTH check (`logs/30`), which leaves its rung directories, without a master, in `media/ramp-refused/`; TI first computed across the jump in the main content's frames, now between consecutive frames only. The development runs before the final pass are in `testgen/explore/` (superseded, not cited; the log of the first, failed MPEG-2 run there was overwritten by its rerun under the same name).

### 3.7 Sizes

Each ladder: three rungs, 8 s, 200 frames, 29 files (24 segments, 3 media playlists, the master, `layout.json`): 1409333 bytes (degraded, libx264), 2447887 (degraded, MPEG-2), 1387448 (ad, libx264), 2415852 (ad, MPEG-2) (`logs/12`, `22`, `42`, `52`). The top rungs: 596336, 1035880, 580920 and 1028172 bytes (`logs/10`, `20`, `40`, `50`). The FFV1 mezzanines: 19744829 and 19725131 bytes (`logs/16`, `logs/80`).

## 4. Choices for the owner

Each is the owner's; the recommendations are the agent's.

1. **The encoder of the tests.** (a) libx264: the codec of real HLS ladders, a CODECS value, B-frames with PTS ≠ DTS; pinned to the same commit in Homebrew and the Docker image (3.3); GPL, as Mirage is (D-008); needs a build with libx264. (b) FFmpeg's MPEG-2 encoder: in every FFmpeg build, what the existing tests use; no HLS codec value; needs `frame->quality` and scene detection off. Both give a clear drop (41.239 against 97.134 to 98.231, and 62.022 against 96.923 to 97.671, 3.1). Agent's recommendation: (a), with the tests skipped where `avcodec_find_encoder_by_name("libx264")` finds nothing, as an existing test skips without a model file (`tests/test_vmaf_ladder.cpp:822`).
2. **The source.** The fractal noise of 2.2, or the existing ramp, whose ladder fails D-002's precondition (2.2). Agent's recommendation: the noise.
3. **The ad's content.** Calm (at the normal quantizer its segments score 90.599 and 90.027 in `v180`, the main content 90.966 to 91.978) or busy (84.814 and 85.635, with frames past the 6-point threshold, and segments of 474512 and 478272 bytes at `v360`, the main content's 70876 to 76704) (3.2, `logs/56`). Agent's recommendation: calm.
4. **The ad's timestamps.** (a) From the main content's origin: PTS repeat across the splice (3.6, 4) and the report's sequences differ from the playlist's (3.6, 5), the hard case for §3.1's time base. (b) From another origin with a jump of more than 10 s: the report's sequences match the playlist's. (c) Both, as two cases. Agent's recommendation: (a) for the acceptance test, since it is the case the timeline's key has to handle; (b) as a unit test of the mapping from playlist to frames.
5. **Continuity counters at the splice.** Marked (`initial_discontinuity`, no warnings), or unmarked (RFC 8216 allows it; FFmpeg flags packets, scores unchanged), or both. Agent's recommendation: marked in the acceptance tests; unmarked as a separate case if §3.1's decoder events are to include FFmpeg's corrupt flags, which would need the `av_log` messages or the packet flags recorded, since the report has neither.
6. **SCTE-35 binary forms in the test.** (a) Leave `EXT-X-DATERANGE` and `#EXT-OATCLS-SCTE35` out of the ladders, as here. (b) RFC 8216 §8.10's splice_info_sections as test vectors of D-070's decoder, outside the ladders. (c) An ad as long as their 59.993 s break. Agent's recommendation: (a) with (b).
7. **The generator in a Debug build of the tests**, 16.941 s against 2.312 s (3.4; the debug preset's flags in the Summary): (a) the generator's source compiled with optimization in every build type; (b) a faster noise (a lattice table per octave, fewer octaves); (c) smaller frames; (d) each ladder made once per run, as the existing fixtures are (`tests/test_vmaf_ladder.cpp:192-216`). The effect of (a) to (c) in a Debug build: MEASUREMENT PENDING. Agent's recommendation: (d) with (a).
8. **Mezzanine mode in the tests.** The ad test can't use a one-file mezzanine (3.5); an HLS mezzanine with the rungs' jumps, or a pairing that reads the playlist's tags (D-050's alternative), would be needed. Agent's recommendation: the acceptance tests in top-rung mode only (D-002's default), and mezzanine mode for the degraded test at most.
9. **The detection's window and N.** Single-frame dips at the start of each libvmaf context (3.6, 7) and the busy ad's alternate frames (3.2) are what N has to pass over; the drops here last 25 and 50 frames. The values are the owner's (§3.2 suggests only the 6 points).

## 5. Open issues

- The same bytes on another machine or in the Docker image: MEASUREMENT PENDING (3.3).
- §3.2's slow segment request (2.3 s in its example): these ladders are local files. Served by `tests/HttpFileServer.h`, a segment can be held or cut, but not answered late (`tests/HttpFileServer.h:32-36`); a delay rule would be needed for §3.1's HTTP events in the attribution test. Not prototyped.
- Ad boundaries without cue tags: in AWS MediaTailor's stitched playlist the cue tags are gone and only `EXT-X-DISCONTINUITY` marks the ad ("Ad markers (EXT-X-CUE-OUT, EXT-X-CUE-IN) are replaced with actual ad segments", `https://docs.aws.amazon.com/mediatailor/latest/ug/manifest-hls-example.html`). The test's playlist has both; whether §3.1's timeline also treats a bare discontinuity as an ad boundary is a design question.
- The timeline's key (3.6, 4 and 5) and where it takes discontinuity sequences from are design questions for §3.1, not settled by this content.
- In top-rung mode the reference's ad is at the normal quantizer here; an ad low in every rung, the top rung's included, would score well against itself (D-002's limit); not generated.
- The FFmpeg source lines cited are of the n8.1.1 tag; `testgen/ffmpeg-n8.1.1/` and `testgen/refs/rfc8216.txt` are copies for reading, better cited by URL than committed. `testgen/media/` is regenerable and large.
