"""Three ways to name the segment each video packet of a logprobe run came
from, from the run's own output, and how far they agree:

  by_pos: AVPacket::pos counts bytes of the one stream the hls demuxer feeds
    its segment demuxer (hls.c read_data_continuous()); the segments' sizes
    are the bytes_read of their AVIOContexts at io_close2, in the order they
    were opened, so segment k holds the positions [sum of sizes before k,
    that + size of k).
  by_pts: playlist time = PTS - the stream's start, in floating point, and
    segment k holds playlist times [sum of EXTINF before k, that + EXTINF of
    k), the hls demuxer's own rule (hls.c find_timestamp_in_playlist()), with
    hls::MediaTimeline::segment_at()'s 1e-9 s slack at a segment's end.
  by_pts_half_frame: the same with every boundary moved half a frame earlier
    (the packet duration / 2), so that a frame starting at a boundary is in
    the segment after it whatever the rounding.

    python3 -I map_frames_to_segments.py <logprobe output.txt> <media playlist>
"""
import re
import sys
from collections import Counter

log = open(sys.argv[1], encoding="utf-8").read().split("\n")
playlist = open(sys.argv[2], encoding="utf-8").read().split("\n")
extinf = [float(l[len("#EXTINF:"):].split(",")[0]) for l in playlist if l.startswith("#EXTINF:")]

opens, sizes, pb_url, packets = [], {}, {}, []
start = tb_den = None
for line in log:
    f = line.split("\t")
    if len(f) < 3:
        continue
    kind = f[2]
    if kind == "open_begin":
        opens.append(f[4][len("url="):])
    elif kind == "open_end" and f[4] == "ret=0":
        pb_url[f[5]] = opens[-1]
    elif kind == "close":
        url = pb_url.get(f[4])
        if url is not None:
            sizes[url] = int(f[5][len("bytes_read="):])
    elif kind == "opened":
        start = float(re.search(r"stream_start=([0-9.]+)", line).group(1))
        tb_den = int(re.search(r"time_base=1/([0-9]+)", line).group(1))
    elif kind == "packet":
        packets.append((float(f[3][len("pts="):]), int(f[9][len("pos="):]),
                        int(f[5][len("duration="):])))

segments = [u for u in opens if not u.endswith(".m3u8")]
bounds, at = [], 0
for u in segments:
    bounds.append((at, at + sizes[u]))
    at += sizes[u]
tbounds, t = [], 0.0
for d in extinf:
    tbounds.append((t, t + d))
    t += d


def by_pos(pos):
    return next((k for k, (a, b) in enumerate(bounds) if a <= pos < b), -1)


def by_pts(pts, shift=0.0):
    x = pts - start
    return next((k for k, (a, b) in enumerate(tbounds) if a - shift <= x < b - shift + 1e-9), -1)


print("segments opened:", ", ".join(f"{u} ({sizes[u]} bytes)" for u in segments))
print("EXTINF:", ", ".join(f"{d:g}" for d in extinf), "; stream start:", start)
print("pts\tpos\tby_pos\tby_pts\tby_pts_half_frame")
rows = []
for pts, pos, dur in packets:
    rows.append((by_pos(pos), by_pts(pts), by_pts(pts, dur / tb_den / 2)))
    print(f"{pts:.6f}\t{pos}\t" + "\t".join(str(v) for v in rows[-1]))
for i, name in enumerate(("by_pos", "by_pts", "by_pts_half_frame")):
    counts = Counter(r[i] for r in rows)
    print(f"{name}: packets per segment", dict(sorted(counts.items())),
          f"; agrees with by_pos on {sum(r[i] == r[0] for r in rows)} of {len(rows)}")
