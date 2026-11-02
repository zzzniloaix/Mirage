"""Which segment each video packet of a logprobe run came from, named two ways,
against the generator's own layout of the ladder (testgen's layout.json).

  by_pos: AVPacket::pos counts bytes of the one stream FFmpeg's hls demuxer
    feeds its segment demuxer; segment k holds the positions from the sum of
    the sizes (bytes_read at io_close2) of the segments opened before it, to
    that plus its own size. The rule <out>/probe/map_frames_to_segments.py
    tried on the sources survey's fixture.
  by_pts: playlist time = PTS - the stream's start; segment k holds the
    playlist times from the sum of EXTINF before it, to that plus its EXTINF
    (FFmpeg's find_timestamp_in_playlist() rule), with a 1e-9 s slack.
  truth: the generator writes each segment with an encoder of its own, so
    segment k's packets are the next layout["segments"][k]["frames"] packets
    the demuxer returns, in order.

    python3 -I map_by_pos.py <logprobe output> <media playlist> <layout.json>
"""
import json
import re
import sys
from collections import Counter

log = open(sys.argv[1], encoding="utf-8").read().split("\n")
playlist = open(sys.argv[2], encoding="utf-8").read().split("\n")
layout = json.load(open(sys.argv[3], encoding="utf-8"))
extinf = [float(l[len("#EXTINF:"):].split(",")[0]) for l in playlist if l.startswith("#EXTINF:")]

opens, sizes, pb_url, packets = [], {}, {}, []
start = None
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
        start = float(re.search(r"stream_start=(-?[0-9.]+)", line).group(1))
    elif kind == "packet":
        packets.append((float(f[3][len("pts="):]), int(f[9][len("pos="):]),
                        int(f[6][len("key="):]), int(f[7][len("corrupt="):])))

segments = [u for u in opens if not u.endswith(".m3u8")]
bounds, at = [], 0
for u in segments:
    bounds.append((at, at + sizes[u]))
    at += sizes[u]
tbounds, t = [], 0.0
for d in extinf:
    tbounds.append((t, t + d))
    t += d
truth = []
for k, s in enumerate(layout["segments"]):
    truth += [k] * s["frames"]


def by_pos(pos):
    return next((k for k, (a, b) in enumerate(bounds) if a <= pos < b), -1)


def by_pts(pts):
    x = pts - start
    return next((k for k, (a, b) in enumerate(tbounds) if a <= x < b + 1e-9), -1)


print("segments opened, in order:", ", ".join(f"{u} ({sizes[u]} bytes)" for u in segments))
print("EXTINF:", ", ".join(f"{d:g}" for d in extinf), "; stream start:", start)
print("packets read:", len(packets), "; packets in the layout:", len(truth))
print("i\tpts\tpos\tkey\tcorrupt\ttruth\tby_pos\tby_pts")
rows = []
for i, (pts, pos, key, corrupt) in enumerate(packets):
    rows.append((truth[i] if i < len(truth) else -2, by_pos(pos), by_pts(pts)))
    print(f"{i}\t{pts:.6f}\t{pos}\t{key}\t{corrupt}\t" + "\t".join(str(v) for v in rows[-1]))
for i, name in ((1, "by_pos"), (2, "by_pts")):
    counts = Counter(r[i] for r in rows)
    print(f"{name}: packets per segment", dict(sorted(counts.items())),
          f"; agrees with the layout on {sum(r[i] == r[0] for r in rows)} of {len(rows)}")
keys = [i for i, p in enumerate(packets) if p[2]]
print("key packets at read positions:", keys)
