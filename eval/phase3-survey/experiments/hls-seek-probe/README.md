# HLS seek probe

What FFmpeg 8.1.1's hls demuxer does when the player seeks into a media
playlist with `EXT-X-DISCONTINUITY` tags, for the §3.3 survey (`../../ui.md`,
section 2.4). Library APIs only (libavformat, libavcodec, libavutil); no
`ffmpeg` or `ffprobe` binary. Not part of the design or of any test: it
checks a reading of FFmpeg's source.

| File | What it is |
|---|---|
| `seek_probe.cpp` | the program: writes two playlists, seeks them, prints what is read |
| `run.sh` | the exact command: builds `seek_probe.cpp` against Homebrew's ffmpeg-full (the FFmpeg `<repo>/CMakePresets.json` points the build at) and runs it on `./media` |
| `run-output.txt` | everything `./run.sh` printed, from the run of 2026-10-31 |

The generated media (`./media`) and the binary are deleted at the end of
`run.sh`; `seek_probe` makes the media again. `run.sh` writes the Homebrew
prefix as `<homebrew>` and its own directory as `<here>`; the recorded run
printed neither.

## The two playlists

Three 2 s MPEG-TS segments each (MPEG-2 video, 25 fps, 320x192), `EXTINF:2.000`
each, an `EXT-X-DISCONTINUITY` before the second and before the third segment:

| Case | seg0 | seg1 (the "ad") | seg2 |
|---|---|---|---|
| `reset` | PTS 10.000 .. 11.960 | PTS 0.040 .. 2.000 | PTS 12.000 .. 13.960 |
| `continuous` | PTS 10.000 .. 11.960 | PTS 12.000 .. 13.960 | PTS 14.000 .. 15.960 |

The PTS are those read back ("read through from the start" in
`run-output.txt`). The ad of `reset` was written from PTS 0.000; it reads back
from 0.040, one frame later, most likely libavformat's `avoid_negative_ts`
shifting the segment whose first DTS was negative (each packet printed after
a seek has its DTS one frame before its PTS). When the next segment is
opened, the MPEG-TS demuxer logs the last two packets of the one before as
"Packet corrupt"; they are still read (50 packets per segment). Neither
changes what the seeks show.

## The seeks

Each target is sought as `<repo>/src/Demuxer.cpp:505` seeks for a plain seek,
`av_seek_frame(fc, -1, t * AV_TIME_BASE, AVSEEK_FLAG_BACKWARD)` ("ffmpeg"),
and again after the player's clamp of the target to `[start_time,
start_time + duration - 0.5]` with its second try at `start_time + 1 s`
(Demuxer.cpp:487-495, 510; "player"). `start_time` read 10.000 s and
`duration` 6.000 s for both playlists.

| Case | Target (s) | What it is | First video packet after the seek |
|---|---|---|---|
| reset | 11.000 | content before the ad, by PTS | PTS 10.000 (seg0's start) |
| reset | 0.500 | a frame of the ad, by its PTS | ffmpeg: `av_seek_frame` fails ("Operation not permitted"); player: clamped to 10.000, PTS 10.000 (the stream's start) |
| reset | 12.500 | the same ad frame, by its playlist position (first DTS + 2 s + 0.5 s) | PTS 12.000: seg1 was requested, then seg2; the ad was skipped |
| reset | 13.000 | content after the ad, by PTS | PTS 12.000 (seg2's start) |
| reset | 14.500 | content after the ad, by playlist position | end of file: nothing read |
| continuous | 11.000 | content before the ad | PTS 10.000 (seg0's start) |
| continuous | 12.500 | the ad | PTS 12.000 (seg1's start) |
| continuous | 14.500 | content after the ad | PTS 14.000 (seg2's start) |

Why, in FFmpeg n8.1.1's source:

- `av_seek_frame` with stream -1 rescales the target from `AV_TIME_BASE` to
  the default stream's time base and calls the demuxer's `read_seek`; the hls
  demuxer is `AVFMT_NOGENSEARCH`, so when its `read_seek` fails nothing else
  is tried and -1 is returned, which `av_strerror` prints as "Operation not
  permitted" ([seek.c#L610-L638](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/seek.c#L610-L638),
  [hls.c#L2852](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L2852)).
- `hls_read_seek` finds the segment by adding the EXTINF durations to
  `first_timestamp`, the DTS of the first packet read, whatever timestamps
  the segments carry; a target before `first_timestamp` fails; a backward
  video seek is moved to the start of the segment found
  ([hls.c#L1916-L1944](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L1916-L1944),
  [hls.c#L2551-L2554](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L2551-L2554),
  [hls.c#L2667-L2760](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L2667-L2760)).
- After the seek, packets whose DTS is below that position are dropped until
  a keyframe at or after it
  ([hls.c#L2565-L2586](https://github.com/FFmpeg/FFmpeg/blob/n8.1.1/libavformat/hls.c#L2565-L2586)):
  an ad whose timestamps restart below its playlist position is dropped
  whole, and so is content whose timestamps fall below its position.

What it does not cover: a discontinuity whose timestamps jump forward, audio,
fMP4 segments, a master playlist with several variants (the player opens the
master), and the player itself (not run).
