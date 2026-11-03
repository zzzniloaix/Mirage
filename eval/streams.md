# Public test streams

The public streams the evaluation has used or considered (work order §8), with what the committed files say about them. Each statement names the file it comes from. The records are:

- `eval/baseline/inputs/`: the masters of both Apple streams and two bipbop playlists, as Phase 0 fetched them on 2026-10-21 (`fetched_utc.txt`), with their HTTP headers;
- `eval/baseline/diagnostics/ffprobe_rungs.txt`: Phase 0's ffprobe of each bipbop video rung;
- `eval/streams/fetched-2026-10-23/`: a fetch of 2026-10-23 by `eval/streams/fetch_streams.py --probe` (below; `docs/DECISIONS.md` P-023): every playlist the two masters' `EXT-X-STREAM-INF` entries name, what they say, and ffprobe's parameters of each video playlist's first video stream. Its `streams.md` has the full tables.
- `eval/streams/d011-candidates-2026-10-24.txt`: candidate source clips for D-011's ladders, examined without downloading them (below; P-061).

## Used: Apple bipbop 16x9 (HLS)

Master playlist: <https://devstreaming-cdn.apple.com/videos/streaming/examples/bipbop_16x9/bipbop_16x9_variant.m3u8> (P-002).

**Where it is used.** The Phase 0 baseline scored the whole ladder with the player's existing analyzer (`eval/baseline/`). Every §5 run of Phase 1 scores the four lower video rungs against gear5 (`eval/consistency/pairs/bipbop16x9.txt`, D-018), and the model runs gear2 against gear5 (`eval/consistency/pairs/bipbop16x9_gear2.txt`). All of them read the streams from Apple's CDN; no committed run used a local copy (P-012, P-016).

**The master's entries** (`eval/baseline/inputs/bipbop_16x9__bipbop_16x9_variant.m3u8`):

| playlist | BANDWIDTH | RESOLUTION | CODECS |
|---|---|---|---|
| `gear1/prog_index.m3u8` | 263851 | 416x234 | mp4a.40.2, avc1.4d400d |
| `gear2/prog_index.m3u8` | 577610 | 640x360 | mp4a.40.2, avc1.4d401e |
| `gear3/prog_index.m3u8` | 915905 | 960x540 | mp4a.40.2, avc1.4d401f |
| `gear4/prog_index.m3u8` | 1030138 | 1280x720 | mp4a.40.2, avc1.4d401f |
| `gear5/prog_index.m3u8` | 1924009 | 1920x1080 | mp4a.40.2, avc1.4d401f |
| `gear0/prog_index.m3u8` | 41457 | | mp4a.40.2 (audio only) |

Besides these six entries the master has five `EXT-X-I-FRAME-STREAM-INF` entries, and `EXT-X-MEDIA` renditions for two audio tracks and eight subtitle tracks (English, French, Spanish and Japanese, each also forced).

**The video streams** (ffprobe, `eval/streams/fetched-2026-10-23/streams.md`; the same width, height, pixel format, range, color matrix, frame rates and start time as Phase 0's `ffprobe_rungs.txt`):

| rung | size | codec | pix_fmt | range | color matrix | primaries | transfer | frame rate | start |
|---|---|---|---|---|---|---|---|---|---|
| gear1 | 416x234 | h264 Main | yuv420p | tv | smpte170m | smpte170m | bt709 | 30000/1001 | 10.000000 |
| gear2 | 640x360 | h264 Main | yuv420p | tv | smpte170m | smpte170m | bt709 | 30000/1001 | 10.000000 |
| gear3 | 960x540 | h264 Main | yuv420p | tv | bt709 | bt709 | bt709 | 30000/1001 | 10.000000 |
| gear4 | 1280x720 | h264 Main | yuv420p | tv | bt709 | bt709 | bt709 | 30000/1001 | 10.000000 |
| gear5 | 1920x1080 | h264 Main | yuv420p | tv | bt709 | bt709 | bt709 | 30000/1001 | 10.000000 |

- All five have the time base 1/90000.
- The color matrix tags differ between the rungs: gear1 and gear2 are tagged smpte170m (BT.601), the others bt709. That is what D-016 is about (`eval/consistency/README.md`).

**The playlists** (`eval/streams/fetched-2026-10-23/streams.md`):

- Each video playlist has 181 segments, all byte ranges of a single `main.ts`, `EXT-X-PLAYLIST-TYPE:VOD`, a target duration of 11 and `EXT-X-ENDLIST`. The first segment's `EXTINF` is 9.9766 in every rung. The `EXTINF` durations add up to 1800.0119 s in gear2 to gear5 and to 1800.0453 s in gear1, whose last segment is longer (4.2485 against 4.2151).
- The audio-only gear0 has 180 segments.
- Phase 0's run of the player's analyzer scored 53946 frames in each video rung (`eval/baseline/bipbop16x9/summary.txt`).

**Unchanged since Phase 0.** On 2026-10-23 the master and the gear1 and gear5 playlists were byte for byte the copies Phase 0 fetched on 2026-10-21 (`eval/streams/fetched-2026-10-23/streams.md`). The master's `Last-Modified` header is Mon, 29 Jan 2024 21:26:16 GMT in both fetches.

**Limits.** The rungs were rendered separately rather than scaled from one source picture: in Phase 0's screenshots of frame 300, each rung's overlay states its own resolution and gear number (`eval/baseline/diagnostics/frame_n300_gear1.png`, `frame_n300_gear5.png`; the "Baseline" section of `docs/VMAF_GAP_REPORT.md`). Scores of a rung against gear5 therefore compare tools and settings, not quality (D-005, D-011).

## Considered: Apple's advanced HLS sample, TS segments

Master playlist: <https://devstreaming-cdn.apple.com/videos/streaming/examples/img_bipbop_adv_example_ts/master.m3u8>. Phase 0 kept its text (`eval/baseline/inputs/img_bipbop_adv_example_ts__master.m3u8`); the fetch of 2026-10-23 got the same bytes from this URL (`eval/streams/fetched-2026-10-23/streams.md`).

**Why it was not used.** P-002: the existing scorer would score each of the master's 24 entries one by one, including the entries that name the same playlist. It is kept for later, dedicated tests of frame-rate mismatches (§1.4, `docs/plans/frame-alignment.md`) and of entries that share a playlist (§2). Nothing committed has scored it.

**What it is** (`eval/streams/fetched-2026-10-23/streams.md`):

- 24 `EXT-X-STREAM-INF` entries naming 8 video playlists, `v2` to `v9`, each paired with three audio groups: `aud1` (AAC, `CHANNELS="2"`), `aud2` (AC-3, `CHANNELS="6"`) and `aud3` (E-AC-3, `CHANNELS="6"`); also 6 I-frame playlists, a closed-caption rendition and a subtitle rendition.
- The video playlists: `v2` 480x270, `v3` 640x360 and `v4` 768x432 at `FRAME-RATE=30.000`; `v5` 960x540, `v6` 1280x720, and `v7`, `v8` and `v9`, three 1920x1080 playlists at different bandwidths, at `FRAME-RATE=60.000`.
- Each video playlist has 100 segments of 6 s, each its own file, 600 s in all.
- ffprobe: h264 High, yuv420p, **no color tags** (range, matrix, primaries and transfer all unset), frame rate 30/1 for `v2` to `v4` and 60/1 for `v5` to `v9`, time base 1/90000, and a start of 10.033333 s at 30 fps and 10.016667 s at 60 fps.
- So its pairs would exercise D-016's handling of untagged inputs, which D-021 decided for a yuv420p reference, as these playlists are: an untagged input is taken as BT.601 with limited range (D-033 confirmed P-014); and, between a 60 fps reference and the 30 fps playlists, a frame-rate mismatch and inputs that start at different times, which Mirage's pairing in decode order did not detect; since D-028 it pairs by timestamp, on the coarser grid and with times from each input's start (`src/VMAFAlign.h`). No committed run has scored these playlists.

**On `vmaf/phase-2`** (a note of 2026-10-29; the text above is Phase 1's): D-049 made it §2's acceptance ladder. Its runs across pool sizes read a local copy of its first segments ("The local copy of the advanced sample" below), and its runs over every frame read Apple's CDN (`eval/ladder/`; `docs/PHASE2_STATUS.md`), so the statements above that nothing committed has scored it describe Phase 1 only.

### The local copy of the advanced sample (D-049)

`eval/streams/local-advanced-2026-10-26/` is the record of the copy `eval/streams/local_copy.py` made on 2026-10-26 (`copy.md`): the master as fetched, and each of its 8 video playlists cut after its first 2 segments, with an `EXT-X-ENDLIST`. The copy itself, media included, isn't in the repository.

- **What its SHA256SUMS checks.** It lists the copy's 25 files by their paths in the copy, the segments included, so that it checks a copy made again: from that copy's directory, `shasum -a 256 -c <repo>/eval/streams/local-advanced-2026-10-26/SHA256SUMS`.
- **In the record itself** it can't be checked as it stands, since the segments aren't there. The record's `playlists/` holds the master and the 8 cut playlists at the copy's paths, so from there `shasum -a 256 -c --ignore-missing ../SHA256SUMS` checks those 9 files and leaves out the segments. `copy.json` and `copy.md` are in no checksum file.
- The record stays as committed (D-060). The SHA256SUMS of the fetch of 2026-10-23 next to it, `eval/streams/fetched-2026-10-23/`, lists that record's own files instead.

## Named by the work order, not examined

§8 of the work order also lists DASH-IF test vectors and Unified Streaming demo streams. None of them was examined in this work, so no URL or property is recorded here (MEASUREMENT PENDING).

## The evaluation's own ladders (D-011)

The evaluation encodes its own HLS ladders from public source clips, which serve as the mezzanine (D-011). The owner chooses the clips and the encoding parameters when encoding starts. On 2026-10-24 eight candidate clips from four sources were examined without downloading them: the license statements of their official pages, their download URLs, sizes and checksums or entity tags, and their properties from their headers (`eval/streams/d011-candidates-2026-10-24.txt`; `docs/DECISIONS.md` P-061). `docs/PHASE1_DECISIONS_PENDING.md`, "D-011", sums them up for the owner. None of them is chosen, and no committed run uses one.

## Generated inputs

Not streams, but the other inputs the committed runs and tests use:

- `eval/consistency/color_cases.py` makes the color tag cases from FFmpeg's `testsrc2` pattern; `eval/consistency/color-cases-before/clips.txt` records how each clip was made and its tags and pixel checksums.
- The unit tests generate their own small clips (`docs/BUILD.md`, step 4).

## Fetching the playlists again

```
python3 -I -B eval/streams/fetch_streams.py <out_dir> [--streams FILE] [--probe] [--ffprobe FFPROBE]
python3 -I -B -m unittest discover -s eval/streams/tests -v
```

- `<out_dir>`: a new or empty directory, outside the repository for a run whose outputs are to be committed (the run records the repository's commit and whether its working tree was clean).
- `--streams`: one master playlist URL per line; the default, `eval/streams/streams.txt`, lists the two Apple masters above.
- It fetches each master and every playlist its `EXT-X-STREAM-INF` entries name, and keeps them under the names Phase 0 used in `eval/baseline/inputs/`, each with its HTTP status and the headers that identify its content (`Last-Modified`, `ETag`, `Content-Length`, `Content-Type`). `streams.md` and `streams.json` say what each playlist contains and whether it is byte for byte Phase 0's copy, where Phase 0 kept one.
- `--probe` also runs ffprobe (by default ffmpeg-full's, D-009) on each playlist with video; ffprobe then reads the first segments. No media is kept.
- Exit status: 0 everything was fetched (and probed); 1 a fetch or a probe failed (`streams.md` lists the failures); 2 bad arguments. A playlist that differs from Phase 0's copy is reported, not a failure.
- The tests need no network: they serve the copies in `eval/baseline/inputs/` to the script.
