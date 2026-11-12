"""Four candidate time axes for the section 3.1 timeline, computed from a
Phase 2 ladder report, and an interval query on each: a prototype of the
design's question on the time key, not of its code.

For the reference's scored frames (one pair's frames, by ref_index), each axis
gives a position in seconds:

  pts         the frame's ref_pts as the report has it (raw best-effort PTS);
  seq_pts     the report's reference discontinuity sequences
              (alignment.reference.sequences) laid end to end: within sequence
              k, T_k + (ref_pts - first_pts_k); T_0 = 0, T_k+1 = T_k + (the
              last ref_pts of k - first_pts_k) + one frame duration;
  playlist    the reference playlist's time: the start of the frame's segment
              (the sum of EXTINF before it) + (ref_pts - the ref_pts of the
              segment's first frame); the frame's segment from the
              generator's layout (layout.json), which runs map-by-pos-* check
              against the byte-position rule;
  index       ref_index x the reference's frame duration.

Arithmetic: exact by default. Each ref_pts becomes whole ticks of the
reference's time base (the pair's ref.time_base, round(pts x den / num)), the
frame duration is 1 / alignment.reference.frame_rate and each EXTINF its
decimal text, all as fractions, so that every comparison is exact, as the
aligner compares times (<repo>/src/VMAFAlign.h:17-22). With --float, every
value is a double instead, as a timeline of seconds in doubles would have it.

Events: each segment of the media playlist as a span from its first frame to
the next segment's first frame (the last one to its last frame + one frame),
and each tag before a segment's URI as a point at the segment's first frame.
The interval [first, last] (reference frame indices, inclusive, taken from
the command line) becomes [pos(first), pos(last) + one frame duration); a
point overlaps it when it lies in it, a span when they intersect; points at
the interval's end are listed apart. Offsets are from the interval's start.

    python3 -I axis_probe.py [--float] <report.json> <layout.json> \
        <media playlist> <the pair's dis.url> <first>:<last>
"""
import json
import sys
from fractions import Fraction

args = sys.argv[1:]
exact = True
if args and args[0] == "--float":
    exact = False
    args = args[1:]
report = json.load(open(args[0], encoding="utf-8"))
layout = json.load(open(args[1], encoding="utf-8"))
playlist = open(args[2], encoding="utf-8").read().split("\n")
variant = args[3]
first, last = (int(v) for v in args[4].split(":"))

pair = next(p for p in report["pairs"] if p["dis"]["url"] == variant)
frames = sorted(pair["frames"], key=lambda f: f["ref_index"])
tb_num, tb_den = (int(v) for v in pair["ref"]["time_base"].split("/"))
fr_num, fr_den = (int(v) for v in pair["alignment"]["reference"]["frame_rate"].split("/"))
seqs = pair["alignment"]["reference"]["sequences"]


def seconds(v):
    """A report's seconds value: as ticks of the time base, exactly, or as it is."""
    if not exact:
        return float(v)
    return Fraction(round(Fraction(repr(v)) * tb_den / tb_num) * tb_num, tb_den)


d = Fraction(fr_den, fr_num) if exact else fr_den / fr_num

# The playlist: each segment's EXTINF, URI and the tags before it.
segs, tags, extinf = [], [], None
for line in playlist:
    line = line.strip()
    if not line or line == "#EXTM3U":
        continue
    if line.startswith("#EXTINF:"):
        text = line[len("#EXTINF:"):].split(",")[0]
        extinf = Fraction(text) if exact else float(text)
    elif line.startswith("#EXT-X-DISCONTINUITY-SEQUENCE") or line.startswith("#EXT-X-ENDLIST"):
        continue
    elif line.startswith("#EXT-X-DISCONTINUITY") or line.startswith("#EXT-X-CUE"):
        tags.append(line)
    elif line.startswith("#"):
        continue
    else:
        segs.append({"uri": line, "extinf": extinf, "tags": tags})
        tags, extinf = [], None
t = 0 if exact else 0.0
for s, lay in zip(segs, layout["segments"]):
    s["start"] = t
    t += s["extinf"]
    s["first_frame"], s["frames"] = lay["first_frame"], lay["frames"]


def seq_of(i):
    k = 0
    for j, s in enumerate(seqs):
        if i >= s["first_index"]:
            k = j
    return k


def seg_of(i):
    return next(k for k, s in enumerate(segs) if s["first_frame"] <= i < s["first_frame"] + s["frames"])


pts = {f["ref_index"]: seconds(f["ref_pts"]) for f in frames}
first_pts = [seconds(s["first_pts"]) for s in seqs]
last_pts = {}
for i, p in pts.items():
    last_pts[seq_of(i)] = max(last_pts.get(seq_of(i), p), p)
T = [0 if exact else 0.0]
for k in range(1, len(seqs)):
    T.append(T[k - 1] + (last_pts[k - 1] - first_pts[k - 1]) + d)
seg_first_pts = {k: pts[s["first_frame"]] for k, s in enumerate(segs)}

axes = {
    "pts": lambda i: pts[i],
    "seq_pts": lambda i: T[seq_of(i)] + (pts[i] - first_pts[seq_of(i)]),
    "playlist": lambda i: segs[seg_of(i)]["start"] + (pts[i] - seg_first_pts[seg_of(i)]),
    "index": lambda i: i * d,
}


def show(v):
    return f"{float(v):.6f}" if exact else repr(v)


idx = sorted(pts)
print(f"report: {args[0]}; pair: {variant}; arithmetic: {'exact ticks' if exact else 'doubles'}; "
      f"reference frames scored: {len(idx)}; frame duration: {show(d)} s")
print("reference sequences (first_index, first_pts, frames):",
      [(s["first_index"], s["first_pts"], s["frames"]) for s in seqs])
print("distinct ref_pts:", len(set(pts.values())),
      "; distinct (sequence, ref_pts):", len({(seq_of(i), pts[i]) for i in idx}))
dup = {}
for i in idx:
    dup.setdefault(pts[i], []).append(i)
print("ref_pts shared by two or more frames (first 3):",
      [(show(p), v) for p, v in sorted(dup.items()) if len(v) > 1][:3])

for name, pos in axes.items():
    steps = [(pos(b) - pos(a), a, b) for a, b in zip(idx, idx[1:])]
    rising = all(s > 0 for s, _, _ in steps)
    big = max(steps)
    small = min(steps)
    print(f"\n== axis {name} ==")
    print(f"rising at every frame: {rising}; largest step {show(big[0])} s (frames {big[1]}->{big[2]}); "
          f"smallest step {show(small[0])} s (frames {small[1]}->{small[2]})")
    a, b = pos(first), pos(last) + d
    after = show(pos(last + 1)) if last + 1 in pts else "none"
    print(f"interval: frames {first}-{last} -> [{show(a)}, {show(b)}) s, {show(b - a)} s long; "
          f"the frame after it, {last + 1}, at {after}")
    for k, s in enumerate(segs):
        s0 = pos(s["first_frame"])
        nxt = segs[k + 1]["first_frame"] if k + 1 < len(segs) else None
        s1 = pos(nxt) if nxt is not None else pos(s["first_frame"] + s["frames"] - 1) + d
        if s0 < b and s1 > a:
            print(f"  overlaps: segment {k} {s['uri']} [{show(s0)}, {show(s1)}), offset {show(s0 - a)} s")
        for tag in s["tags"]:
            where = ("overlaps" if a <= s0 < b else "at the end" if s0 == b
                     else "before" if s0 < a else "after")
            print(f"  {where}: {tag} before segment {k} at {show(s0)}, offset {show(s0 - a)} s")
